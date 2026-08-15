#!/usr/bin/env python3
"""Render an SM64 .m64 music sequence to a mono WAV using decomp bank JSONs
and ROM-extracted AIFF samples.

Usage:
  render_m64_wav.py SEQ.m64 --banks-dir sound/sound_banks --samples-dir sound/samples \
      --out OUT.wav [--bank 22] [--rate 32000] [--seconds 40] [--tempo-scale 1.0] \
      [--trim-start-tick N --trim-ticks N] [--pad-to-seconds X] [--no-perc]

Contains only file-format logic (no Nintendo data). Opcode semantics are a
close port of the US switch statements in src/audio/seqplayer.c
(sequence_player_process_sequence / sequence_channel_process_script /
seq_channel_layer_process_script) and src/audio/effects.c volume math.
Tick rate matches src/audio/heap.c: gTempoInternalToExternal = 14360 with
240 sequence updates per second, 48 tatums per beat.

Simplifications (logged): vibrato, portamento (pitch jumps to target),
reverb, envelopes reduced to attack/sustain/release, pan ignored (mono).

Stdlib only. Works on Python 3.12 (uses struct, not the removed aifc).
"""

import argparse
import json
import math
import os
import struct
import sys
import wave

TATUMS_PER_BEAT = 48
TEMPO_INTERNAL_TO_EXTERNAL = 14360  # US value, heap.c comment "14360 on US"
SEQ_UPDATES_PER_SEC = 240           # process_sequences runs 240x/sec

# gNoteFrequencies (data.c): 2^((k-39)/12), halved for k >= 117
NOTE_FREQ = [2.0 ** ((k - 39) / 12.0) * (0.5 if k >= 117 else 1.0) for k in range(128)]


def log(msg):
    print(msg, file=sys.stderr)


# ---------------------------------------------------------------- AIFF loading

def parse_f80(b):
    """80-bit IEEE 754 extended float (AIFF COMM sample rate)."""
    expon = ((b[0] & 0x7F) << 8) | b[1]
    hi, lo = struct.unpack(">II", b[2:10])
    mant = (hi << 32) | lo
    if mant == 0:
        return 0.0
    val = mant * 2.0 ** (expon - 16383 - 63)
    return -val if (b[0] & 0x80) else val


class Sample:
    __slots__ = ("name", "data", "rate", "loop_start", "loop_end")

    def __init__(self, name, data, rate, loop_start, loop_end):
        self.name = name
        self.data = data            # list of float in [-1, 1)
        self.rate = rate
        self.loop_start = loop_start  # None if not looped
        self.loop_end = loop_end


def load_aiff(path):
    with open(path, "rb") as f:
        d = f.read()
    if d[:4] != b"FORM" or d[8:12] not in (b"AIFF", b"AIFC"):
        raise ValueError("not an AIFF file: %s" % path)
    off = 12
    rate = None
    nframes = 0
    bits = 16
    ssnd = None
    markers = {}
    sustain = None  # (mode, begin_marker, end_marker)
    while off + 8 <= len(d):
        cid = d[off:off + 4]
        sz = struct.unpack(">I", d[off + 4:off + 8])[0]
        body = d[off + 8:off + 8 + sz]
        if cid == b"COMM":
            _nch, nframes, bits = struct.unpack(">hIh", body[:8])
            rate = parse_f80(body[8:18])
        elif cid == b"SSND":
            data_off = struct.unpack(">I", body[:4])[0]
            ssnd = body[8 + data_off:]
        elif cid == b"MARK":
            (nmark,) = struct.unpack(">H", body[:2])
            p = 2
            for _ in range(nmark):
                mid, pos = struct.unpack(">Hi", body[p:p + 6])
                p += 6
                slen = body[p]
                p += 1 + slen
                if (slen + 1) % 2:
                    p += 1
                markers[mid] = pos
        elif cid == b"INST":
            # baseNote, detune, lowNote, highNote, lowVel, highVel, gain(s16),
            # sustainLoop{playMode u16, begin u16, end u16}, releaseLoop{...}
            if len(body) >= 14:
                mode, beg, end = struct.unpack(">3H", body[8:14])
                sustain = (mode, beg, end)
        off += 8 + sz + (sz & 1)
    if rate is None or ssnd is None:
        raise ValueError("missing COMM/SSND in %s" % path)
    if bits != 16:
        raise ValueError("unsupported bit depth %d in %s" % (bits, path))
    n = min(nframes, len(ssnd) // 2)
    ints = struct.unpack(">%dh" % n, ssnd[:2 * n])
    data = [s / 32768.0 for s in ints]
    loop_start = loop_end = None
    if markers:
        # SM64 extracted AIFFs carry 'start'/'end' markers (ids 1, 2) for the
        # ADPCM sustain loop; INST playMode is not reliably set, so treat the
        # presence of both markers as a forward loop.
        beg_id, end_id = 1, 2
        if sustain and sustain[1] in markers and sustain[2] in markers and sustain[1] != sustain[2]:
            beg_id, end_id = sustain[1], sustain[2]
        if beg_id in markers and end_id in markers:
            ls, le = markers[beg_id], markers[end_id]
            if 0 <= ls < le <= n:
                loop_start, loop_end = ls, le
    return Sample(os.path.basename(path), data, rate, loop_start, loop_end)


# ---------------------------------------------------------------- bank loading

class Sound:
    __slots__ = ("sample", "tuning")

    def __init__(self, sample, tuning):
        self.sample = sample
        self.tuning = tuning


class Instrument:
    __slots__ = ("name", "release_rate", "lo", "hi", "sound", "sound_lo", "sound_hi")


class Bank:
    def __init__(self):
        self.instrument_list = []   # setinstr id -> Instrument or None
        self.percussion = []        # drum index -> Sound (with release/pan ignored)


class SampleCache:
    def __init__(self, samples_dir, sample_bank):
        self.dir = os.path.join(samples_dir, sample_bank)
        self.cache = {}

    def get(self, name):
        if name not in self.cache:
            self.cache[name] = load_aiff(os.path.join(self.dir, name + ".aiff"))
        return self.cache[name]


def resolve_sound(spec, cache):
    """spec is either "sample_name" or {"sample": name, "tuning": f}."""
    if isinstance(spec, dict):
        name = spec["sample"]
        samp = cache.get(name)
        tuning = spec.get("tuning", samp.rate / 32000.0)
    else:
        samp = cache.get(spec)
        tuning = samp.rate / 32000.0  # assemble_sound.py: rate / 32000
    return Sound(samp, tuning)


def load_bank(banks_dir, bank_name, samples_dir):
    with open(os.path.join(banks_dir, bank_name + ".json")) as f:
        j = json.load(f)
    cache = SampleCache(samples_dir, j["sample_bank"])
    bank = Bank()
    insts = j["instruments"]
    for entry in j.get("instrument_list", []):
        if entry is None:
            bank.instrument_list.append(None)
            continue
        ij = insts[entry]
        inst = Instrument()
        inst.name = entry
        inst.release_rate = ij.get("release_rate", 0x20)
        inst.lo = ij.get("normal_range_lo", 0)
        inst.hi = ij.get("normal_range_hi", 127)
        inst.sound = resolve_sound(ij["sound"], cache)
        inst.sound_lo = resolve_sound(ij["sound_lo"], cache) if "sound_lo" in ij else None
        inst.sound_hi = resolve_sound(ij["sound_hi"], cache) if "sound_hi" in ij else None
        bank.instrument_list.append(inst)
    for pj in insts.get("percussion", []):
        bank.percussion.append(
            (resolve_sound(pj["sound"], cache), pj.get("release_rate", 0x20)))
    return bank


# ---------------------------------------------------------------- script state

class ScriptState:
    __slots__ = ("pc", "stack", "depth", "rem_loop_iters")

    def __init__(self, pc):
        self.pc = pc
        self.stack = [0, 0, 0, 0]
        self.depth = 0
        self.rem_loop_iters = [0, 0, 0, 0]


def read_u8(data, st):
    v = data[st.pc]
    st.pc += 1
    return v


def read_u16(data, st):
    v = (data[st.pc] << 8) | data[st.pc + 1]
    st.pc += 2
    return v


def read_cu16(data, st):
    """m64_read_compressed_u16: hi bit continues into a second byte."""
    v = data[st.pc]
    st.pc += 1
    if v & 0x80:
        v = ((v << 8) & 0x7F00) | data[st.pc]
        st.pc += 1
    return v


def s8(v):
    return v - 256 if v >= 128 else v


class NoteEvent:
    __slots__ = ("start_sec", "gate_sec", "release_rate", "freq_scale",
                 "amp", "sample", "is_drum", "channel")

    def __init__(self, **kw):
        for k, v in kw.items():
            setattr(self, k, v)


class Layer:
    def __init__(self, channel):
        self.channel = channel
        self.enabled = False
        self.state = None
        self.delay = 0
        self.duration = 0
        self.note_duration = 0x80
        self.transposition = 0
        self.continuous_notes = False
        self.play_percentage = 0
        self.short_note_default_play_percentage = 0
        self.velocity_square = 0.0
        self.instrument = None
        self.portamento = False
        self.finished = 1
        self.cur_note = None  # active NoteEvent for early-decay bookkeeping

    def start(self, pc):
        self.enabled = True
        self.state = ScriptState(pc)
        self.delay = 0
        self.duration = 0
        self.note_duration = 0x80
        self.transposition = 0
        self.continuous_notes = False
        self.play_percentage = 0
        self.velocity_square = 0.0
        self.instrument = None
        self.portamento = False
        self.finished = 0
        self.cur_note = None


class Channel:
    def __init__(self, player, idx):
        self.player = player
        self.idx = idx
        self.enabled = False
        self.finished = 1
        self.state = None
        self.delay = 0
        self.large_notes = False
        self.transposition = 0
        self.volume = 1.0
        self.volume_scale = 1.0
        self.freq_scale = 1.0
        self.instrument = None      # Instrument, or "drums", or None
        self.has_instrument = False
        self.stop_script = False
        self.dyn_table = None
        self.layers = [None, None, None, None]

    def init_full(self):
        """sequence_channel_init: full state reset (seq-level 0xD7)."""
        self.__init__(self.player, self.idx)

    def enable(self, pc):
        """sequence_channel_enable: keeps instrument/largeNotes/volume/etc."""
        self.enabled = True
        self.finished = 0
        self.state = ScriptState(pc)
        self.delay = 0
        for i in range(4):
            if self.layers[i] is not None:
                self.free_layer(i)

    def disable(self):
        self.enabled = False
        self.finished = 1
        for i, l in enumerate(self.layers):
            if l is not None:
                self.free_layer(i)

    def set_layer(self, i):
        if self.layers[i] is None:
            self.layers[i] = Layer(self)
        return self.layers[i]

    def free_layer(self, i):
        l = self.layers[i]
        if l is not None:
            l.enabled = False
            l.finished = 1
        self.layers[i] = None


class Renderer:
    def __init__(self, seq_data, bank, args):
        self.data = seq_data
        self.bank = bank
        self.args = args
        self.notes = []
        self.tick = 0
        self.time_sec = 0.0
        self.tick_times = []        # seconds at each tick boundary
        self.tempo_bpm = 120
        self.master_vol = 1.0
        self.transposition = 0
        self.mute_scale = 1.0
        self.seq_delay = 0
        self.seq_state = ScriptState(0)
        self.seq_done = False
        self.channels = [Channel(self, i) for i in range(16)]
        self.skipped = {}
        self.loop_jump_tick = None  # tick when a seq-level backward jump ran
        self.loop_target_tick = None
        self.jump_target_off = None
        self.note_count = 0
        self.perc_count = 0

    def skip(self, level, op, n=1):
        key = "%s_%02X" % (level, op)
        self.skipped[key] = self.skipped.get(key, 0) + n

    def tick_len(self):
        tempo = min(self.tempo_bpm * TATUMS_PER_BEAT, TEMPO_INTERNAL_TO_EXTERNAL)
        ticks_per_sec = SEQ_UPDATES_PER_SEC * tempo / TEMPO_INTERNAL_TO_EXTERNAL
        return 1.0 / (ticks_per_sec * self.args.tempo_scale)

    # ------------------------------------------------------------ seq script
    def seq_step(self):
        if self.seq_done:
            return
        if self.seq_delay > 1:
            self.seq_delay -= 1
            return
        data, st = self.data, self.seq_state
        value = 0
        while True:
            cmd = read_u8(data, st)
            if cmd == 0xFF:  # seq_end
                if st.depth == 0:
                    self.seq_done = True
                    log("[seq] end at pc=0x%04X tick=%d" % (st.pc, self.tick))
                    break
                st.depth -= 1
                st.pc = st.stack[st.depth]
                continue
            if cmd == 0xFD:  # seq_delay
                self.seq_delay = read_cu16(data, st)
                break
            if cmd == 0xFE:  # seq_delay1
                self.seq_delay = 1
                break
            if cmd >= 0xC0:
                if cmd == 0xFC:  # call
                    t = read_u16(data, st)
                    st.stack[st.depth] = st.pc
                    st.depth += 1
                    st.pc = t
                elif cmd == 0xF8:  # loop
                    st.rem_loop_iters[st.depth] = read_u8(data, st)
                    st.stack[st.depth] = st.pc
                    st.depth += 1
                elif cmd == 0xF7:  # loopend
                    st.rem_loop_iters[st.depth - 1] = (st.rem_loop_iters[st.depth - 1] - 1) & 0xFF
                    if st.rem_loop_iters[st.depth - 1] != 0:
                        st.pc = st.stack[st.depth - 1]
                    else:
                        st.depth -= 1
                elif cmd in (0xFB, 0xFA, 0xF9, 0xF5):  # jump / branches
                    t = read_u16(data, st)
                    take = (cmd == 0xFB or
                            (cmd == 0xFA and value == 0) or
                            (cmd == 0xF9 and value < 0) or
                            (cmd == 0xF5 and value >= 0))
                    if take:
                        if t < st.pc and self.loop_jump_tick is None:
                            self.loop_jump_tick = self.tick
                            self.jump_target_off = t
                            log("[seq] loop jump at tick %d -> 0x%04X" % (self.tick, t))
                        st.pc = t
                elif cmd == 0xF2:  # reservenotes
                    read_u8(data, st)
                elif cmd == 0xF1:  # unreservenotes
                    pass
                elif cmd == 0xDF:  # transpose
                    self.transposition = s8(read_u8(data, st))
                elif cmd == 0xDE:  # transpose rel
                    self.transposition += s8(read_u8(data, st))
                elif cmd == 0xDD:  # settempo
                    self.tempo_bpm = read_u8(data, st)
                elif cmd == 0xDC:  # addtempo
                    self.tempo_bpm += s8(read_u8(data, st))
                elif cmd == 0xDB:  # setvol
                    self.master_vol = read_u8(data, st) / 127.0
                elif cmd == 0xDA:  # changevol
                    self.master_vol += s8(read_u8(data, st)) / 127.0
                elif cmd == 0xD7:  # initchannels (full sequence_channel_init)
                    mask = read_u16(data, st)
                    for i in range(16):
                        if mask & (1 << i):
                            self.channels[i].init_full()
                elif cmd == 0xD6:  # disablechannels
                    mask = read_u16(data, st)
                    for i in range(16):
                        if mask & (1 << i):
                            self.channels[i].disable()
                elif cmd == 0xD5:  # setmutescale
                    self.mute_scale = s8(read_u8(data, st)) / 127.0
                elif cmd == 0xD4:  # mute
                    pass
                elif cmd == 0xD3:  # setmutebhv
                    read_u8(data, st)
                elif cmd in (0xD2, 0xD1):  # short note vel/dur tables
                    off = read_u16(data, st)
                    if cmd == 0xD2:
                        self.short_vel_table = off
                    else:
                        self.short_dur_table = off
                elif cmd == 0xD0:  # noteallocpolicy
                    read_u8(data, st)
                elif cmd == 0xCC:  # setval
                    value = read_u8(data, st)
                elif cmd == 0xC9:  # bitand
                    value &= read_u8(data, st)
                elif cmd == 0xC8:  # subtract
                    value -= read_u8(data, st)
                else:
                    self.skip("seq", cmd)
            else:
                lo = cmd & 0x0F
                hi = cmd & 0xF0
                if hi == 0x00:  # testchdisabled
                    value = self.channels[lo].finished
                elif hi == 0x50:
                    value -= 0  # seqVariation; not tracked
                elif hi == 0x70:
                    pass
                elif hi == 0x80:
                    value = 0
                elif hi == 0x90:  # startchannel
                    t = read_u16(data, st)
                    self.channels[lo].enable(t)
                else:
                    self.skip("seq", cmd)

    # -------------------------------------------------------- channel script
    def chan_step(self, ch):
        if not ch.enabled:
            return
        if ch.stop_script:
            for l in ch.layers:
                if l is not None:
                    self.layer_step(l)
            return
        if ch.delay != 0:
            ch.delay -= 1
        data, st = self.data, ch.state
        value = 0
        if ch.delay == 0:
            while True:
                cmd = read_u8(data, st)
                if cmd == 0xFF:  # end / return
                    if st.depth == 0:
                        ch.disable()
                        break
                    st.depth -= 1
                    st.pc = st.stack[st.depth]
                    continue
                if cmd == 0xFE:  # delay1
                    break
                if cmd == 0xFD:  # delay
                    ch.delay = read_cu16(data, st)
                    break
                if cmd == 0xF3:  # hang
                    ch.stop_script = True
                    break
                if cmd > 0xC0:
                    if cmd == 0xFC:  # call
                        t = read_u16(data, st)
                        st.stack[st.depth] = st.pc
                        st.depth += 1
                        st.pc = t
                    elif cmd == 0xF8:  # loop
                        st.rem_loop_iters[st.depth] = read_u8(data, st)
                        st.stack[st.depth] = st.pc
                        st.depth += 1
                    elif cmd == 0xF7:  # loopend
                        st.rem_loop_iters[st.depth - 1] = (st.rem_loop_iters[st.depth - 1] - 1) & 0xFF
                        if st.rem_loop_iters[st.depth - 1] != 0:
                            st.pc = st.stack[st.depth - 1]
                        else:
                            st.depth -= 1
                    elif cmd == 0xF6:  # break
                        st.depth -= 1
                    elif cmd in (0xFB, 0xFA, 0xF9, 0xF5):
                        t = read_u16(data, st)
                        take = (cmd == 0xFB or
                                (cmd == 0xFA and value == 0) or
                                (cmd == 0xF9 and value < 0) or
                                (cmd == 0xF5 and value >= 0))
                        if take:
                            st.pc = t
                    elif cmd == 0xF2:  # reservenotes
                        read_u8(data, st)
                    elif cmd == 0xF1:  # unreservenotes
                        pass
                    elif cmd == 0xC1:  # setinstr
                        self.set_instrument(ch, read_u8(data, st))
                    elif cmd == 0xC2:  # setdyntable
                        ch.dyn_table = read_u16(data, st)
                    elif cmd == 0xC3:
                        ch.large_notes = False
                    elif cmd == 0xC4:
                        ch.large_notes = True
                    elif cmd == 0xC5:  # dynsetdyntable
                        if value != -1 and ch.dyn_table is not None:
                            e = ch.dyn_table + 2 * value
                            ch.dyn_table = (data[e] << 8) | data[e + 1]
                    elif cmd == 0xC6:  # setbank
                        read_u8(data, st)
                        self.skip("chan", cmd)
                    elif cmd == 0xC7:  # writeseq (self-modifying!)
                        add = read_u8(data, st)
                        t = read_u16(data, st)
                        self.data = bytearray(self.data) if not isinstance(self.data, bytearray) else self.data
                        data = self.data
                        data[t] = (value + add) & 0xFF
                    elif cmd == 0xC8:
                        value -= s8(read_u8(data, st))
                    elif cmd == 0xC9:
                        value &= read_u8(data, st)
                    elif cmd == 0xCC:
                        value = s8(read_u8(data, st))
                    elif cmd == 0xCA:  # mutebhv
                        read_u8(data, st)
                    elif cmd == 0xCB:  # readseq
                        t = (read_u16(data, st) + value) & 0xFFFF
                        value = s8(data[t])
                    elif cmd == 0xD0:  # stereo effects
                        read_u8(data, st)
                    elif cmd == 0xD1:  # allocpolicy
                        read_u8(data, st)
                    elif cmd == 0xD2:  # sustain
                        read_u8(data, st)
                        self.skip("chan", cmd)
                    elif cmd == 0xD3:  # pitchbend (semis of +-octave/127)
                        ch.freq_scale = 2.0 ** (s8(read_u8(data, st)) / 12.0 / (127.0 / 12.0))
                    elif cmd == 0xD4:  # reverb
                        read_u8(data, st)
                        self.skip("chan", cmd)
                    elif cmd == 0xD6:  # updatesperframe
                        read_u8(data, st)
                    elif cmd == 0xD7:  # vibrato rate
                        read_u8(data, st)
                        self.skip("chan", cmd)
                    elif cmd == 0xD8:  # vibrato extent
                        read_u8(data, st)
                        self.skip("chan", cmd)
                    elif cmd == 0xD9:  # decay/release rate
                        ch.release_override = read_u8(data, st)
                    elif cmd == 0xDA:  # setenvelope
                        read_u16(data, st)
                        self.skip("chan", cmd)
                    elif cmd == 0xDB:  # transpose
                        ch.transposition = s8(read_u8(data, st))
                    elif cmd == 0xDC:  # panmix
                        read_u8(data, st)
                    elif cmd == 0xDD:  # pan
                        read_u8(data, st)
                    elif cmd == 0xDE:  # freqscale
                        ch.freq_scale = read_u16(data, st) / 32768.0
                    elif cmd == 0xDF:  # volume
                        ch.volume = read_u8(data, st) / 127.0
                    elif cmd == 0xE0:  # volume scale
                        ch.volume_scale = read_u8(data, st) / 128.0
                    elif cmd in (0xE1, 0xE2):  # vibrato linear (3 operands)
                        read_u8(data, st); read_u8(data, st); read_u8(data, st)
                        self.skip("chan", cmd)
                    elif cmd == 0xE3:  # vibrato delay
                        read_u8(data, st)
                    elif cmd == 0xE4:  # dyncall
                        if value != -1 and ch.dyn_table is not None:
                            e = ch.dyn_table + 2 * value
                            t = (data[e] << 8) | data[e + 1]
                            st.stack[st.depth] = st.pc
                            st.depth += 1
                            st.pc = t
                    else:
                        self.skip("chan", cmd)
                else:
                    lo = cmd & 0x0F
                    hi = cmd & 0xF0
                    if hi == 0x00:  # testlayerfinished
                        l = ch.layers[lo]
                        value = l.finished if l is not None else -1
                    elif hi == 0x10:  # startchannel
                        t = read_u16(data, st)
                        self.channels[lo].enable(t)
                    elif hi == 0x20:  # disablechannel
                        self.channels[lo].disable()
                    elif hi == 0x30 or hi == 0x40:  # io r/w other channel
                        read_u8(data, st)
                    elif hi == 0x50 or hi == 0x70 or hi == 0x80:  # io ops
                        pass
                    elif hi == 0x60:  # notepriority
                        pass
                    elif hi == 0x90:  # setlayer
                        t = read_u16(data, st)
                        ch.set_layer(lo).start(t)
                    elif hi == 0xA0:  # freelayer
                        ch.free_layer(lo)
                    elif hi == 0xB0:  # dynsetlayer
                        if value != -1 and ch.dyn_table is not None:
                            e = ch.dyn_table + 2 * value
                            t = (data[e] << 8) | data[e + 1]
                            ch.set_layer(lo).start(t)
                    else:
                        self.skip("chan", cmd)
        for l in ch.layers:
            if l is not None:
                self.layer_step(l)

    def set_instrument(self, ch, inst_id):
        if inst_id >= 0x80:
            ch.instrument = None
            ch.has_instrument = False
            self.skip("chan_synthwave", inst_id)
        elif inst_id == 0x7F:
            ch.instrument = "drums"
            ch.has_instrument = True
        else:
            il = self.bank.instrument_list
            if inst_id < len(il) and il[inst_id] is not None:
                ch.instrument = il[inst_id]
                ch.has_instrument = True
            else:
                ch.instrument = None
                ch.has_instrument = False
                log("[chan %d] unknown instrument id %d" % (ch.idx, inst_id))

    # ---------------------------------------------------------- layer script
    def layer_note_decay(self, layer):
        if layer.cur_note is not None:
            n = layer.cur_note
            end = self.time_sec
            if end > n.start_sec:
                n.gate_sec = min(n.gate_sec, end - n.start_sec)
            layer.cur_note = None

    def layer_step(self, layer):
        if not layer.enabled:
            return
        if layer.delay > 1:
            layer.delay -= 1
            if layer.delay <= layer.duration:
                self.layer_note_decay(layer)
            return
        if not layer.continuous_notes:
            self.layer_note_decay(layer)
        ch = layer.channel
        data, st = self.data, layer.state
        while True:
            cmd = read_u8(data, st)
            if cmd <= 0xC0:
                break
            if cmd == 0xFF:
                if st.depth == 0:
                    layer.enabled = False
                    layer.finished = 1
                    self.layer_note_decay(layer)
                    return
                st.depth -= 1
                st.pc = st.stack[st.depth]
            elif cmd == 0xFC:  # call
                t = read_u16(data, st)
                st.stack[st.depth] = st.pc
                st.depth += 1
                st.pc = t
            elif cmd == 0xF8:  # loop
                st.rem_loop_iters[st.depth] = read_u8(data, st)
                st.stack[st.depth] = st.pc
                st.depth += 1
            elif cmd == 0xF7:  # loopend
                st.rem_loop_iters[st.depth - 1] = (st.rem_loop_iters[st.depth - 1] - 1) & 0xFF
                if st.rem_loop_iters[st.depth - 1] != 0:
                    st.pc = st.stack[st.depth - 1]
                else:
                    st.depth -= 1
            elif cmd == 0xFB:  # jump
                st.pc = read_u16(data, st)
            elif cmd == 0xC1:  # short note velocity
                v = read_u8(data, st)
                layer.velocity_square = float(v * v)
            elif cmd == 0xCA:  # pan
                read_u8(data, st)
            elif cmd == 0xC2:  # transpose
                layer.transposition = read_u8(data, st)
            elif cmd == 0xC9:  # short note duration
                layer.note_duration = read_u8(data, st)
            elif cmd == 0xC4:
                layer.continuous_notes = True
                self.layer_note_decay(layer)
            elif cmd == 0xC5:
                layer.continuous_notes = False
                self.layer_note_decay(layer)
            elif cmd == 0xC3:  # short note default play percentage
                layer.short_note_default_play_percentage = read_cu16(data, st)
            elif cmd == 0xC6:  # layer setinstr
                v = read_u8(data, st)
                if v < 127:
                    il = self.bank.instrument_list
                    if v < len(il) and il[v] is not None:
                        layer.instrument = il[v]
                    else:
                        log("[layer] unknown instrument id %d" % v)
            elif cmd == 0xC7:  # portamento: mode, note, time
                mode = read_u8(data, st)
                read_u8(data, st)
                if mode & 0x80:  # PORTAMENTO_IS_SPECIAL
                    read_u8(data, st)
                else:
                    read_cu16(data, st)
                layer.portamento = True
                self.skip("layer", cmd)
            elif cmd == 0xC8:
                layer.portamento = False
            elif 0xD0 <= cmd <= 0xDF:  # velocity from table
                v = self.data[getattr(self, "short_vel_table", 0) + (cmd & 0xF)]
                layer.velocity_square = float(v * v)
            elif 0xE0 <= cmd <= 0xEF:  # duration from table
                layer.note_duration = self.data[getattr(self, "short_dur_table", 0) + (cmd & 0xF)]
            else:
                self.skip("layer", cmd)

        if cmd == 0xC0:  # rest
            layer.delay = read_cu16(data, st)
            self.layer_note_decay(layer)  # stopSomething path decays the note
            return

        # note command
        vel = None
        if ch.large_notes:
            kind = cmd & 0xC0
            if kind == 0x00:
                pp = read_cu16(data, st)
                vel = read_u8(data, st)
                layer.note_duration = read_u8(data, st)
                layer.play_percentage = pp
            elif kind == 0x40:
                pp = read_cu16(data, st)
                vel = read_u8(data, st)
                layer.note_duration = 0
                layer.play_percentage = pp
            else:  # 0x80
                pp = layer.play_percentage
                vel = read_u8(data, st)
                layer.note_duration = read_u8(data, st)
            layer.velocity_square = float(vel * vel)
            semi = cmd - kind
        else:
            kind = cmd & 0xC0
            if kind == 0x00:
                pp = read_cu16(data, st)
                layer.play_percentage = pp
            elif kind == 0x40:
                pp = layer.short_note_default_play_percentage
            else:
                pp = layer.play_percentage
            semi = cmd - kind

        layer.delay = pp
        layer.duration = (layer.note_duration * pp) // 256

        if not ch.has_instrument:
            self.layer_note_decay(layer)
            return  # stopSomething: no instrument yet

        gate_ticks = pp - layer.duration if 0 < layer.duration < pp else pp
        tick_sec = self.tick_len()
        amp = ((layer.velocity_square / 16129.0) * ch.volume * ch.volume_scale
               * self.master_vol)

        if ch.instrument == "drums":
            if self.args.no_perc:
                return
            drum_semi = (semi + ch.transposition + layer.transposition) & 0xFF
            if not self.bank.percussion:
                return
            if drum_semi >= len(self.bank.percussion):
                drum_semi = len(self.bank.percussion) - 1
            sound, rel = self.bank.percussion[drum_semi]
            note = NoteEvent(start_sec=self.time_sec, gate_sec=gate_ticks * tick_sec,
                             release_rate=rel, freq_scale=sound.tuning * ch.freq_scale,
                             amp=amp, sample=sound.sample, is_drum=True,
                             channel=ch.idx)
            self.perc_count += 1
        else:
            note_semi = (semi + self.transposition + ch.transposition
                         + layer.transposition) & 0xFF
            if note_semi >= 0x80:
                self.layer_note_decay(layer)
                return
            inst = layer.instrument if layer.instrument is not None else ch.instrument
            if inst is None or inst == "drums":
                self.layer_note_decay(layer)
                return
            if note_semi < inst.lo and inst.sound_lo is not None:
                sound = inst.sound_lo
            elif note_semi > inst.hi and inst.sound_hi is not None:
                sound = inst.sound_hi
            else:
                sound = inst.sound
            rel = getattr(ch, "release_override", None)
            if rel is None:
                rel = inst.release_rate
            note = NoteEvent(start_sec=self.time_sec, gate_sec=gate_ticks * tick_sec,
                             release_rate=rel,
                             freq_scale=NOTE_FREQ[note_semi] * sound.tuning * ch.freq_scale,
                             amp=amp, sample=sound.sample, is_drum=False,
                             channel=ch.idx)
        self.notes.append(note)
        self.note_count += 1
        layer.cur_note = note

    # ------------------------------------------------------------- main loop
    def run(self, max_seconds):
        while self.time_sec < max_seconds and not self.seq_done:
            self.tick_times.append(self.time_sec)
            self.seq_step()
            for ch in self.channels:
                self.chan_step(ch)
            self.time_sec += self.tick_len()
            self.tick += 1
        # close out any held notes
        for ch in self.channels:
            for l in ch.layers:
                if l is not None:
                    self.layer_note_decay(l)


# --------------------------------------------------------------------- mixing

def release_seconds(rate_byte):
    """Crude map of N64 ADSR release rate byte to a fade time."""
    return 0.02 + (255 - min(rate_byte, 255)) / 255.0 * 0.18


def mix(notes, out_rate, total_seconds):
    n_out = int(total_seconds * out_rate) + 1
    buf = [0.0] * n_out
    for note in notes:
        samp = note.sample
        data = samp.data
        ndata = len(data)
        step = note.freq_scale * (32000.0 / out_rate)
        start_i = int(note.start_sec * out_rate)
        rel = release_seconds(note.release_rate)
        if note.is_drum:
            # drums ring out for their whole (short) sample
            dur = ndata / step / out_rate
        else:
            dur = note.gate_sec + rel
        n_samples = int(dur * out_rate)
        if start_i >= n_out:
            continue
        n_samples = min(n_samples, n_out - start_i)
        attack = max(1, int(0.002 * out_rate))
        gate_i = int(note.gate_sec * out_rate)
        rel_i = max(1, int(rel * out_rate))
        amp = note.amp
        pos = 0.0
        loop_start, loop_end = samp.loop_start, samp.loop_end
        for i in range(n_samples):
            ip = int(pos)
            if loop_end is not None and pos >= loop_end:
                pos = loop_start + (pos - loop_end)
                ip = int(pos)
            if ip + 1 >= ndata:
                break
            frac = pos - ip
            v = data[ip] + (data[ip + 1] - data[ip]) * frac
            g = amp
            if i < attack:
                g *= i / attack
            if not note.is_drum and i >= gate_i:
                r = 1.0 - (i - gate_i) / rel_i
                if r <= 0.0:
                    break
                g *= r
            buf[start_i + i] += v * g
            pos += step
    return buf


def write_wav(path, buf, rate, peak_target=0.9):
    peak = max(1e-9, max(abs(v) for v in buf))
    scale = peak_target / peak if peak > peak_target else 1.0
    ints = struct.pack("<%dh" % len(buf),
                       *[max(-32768, min(32767, int(v * scale * 32767))) for v in buf])
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(ints)
    rms = math.sqrt(sum(v * v for v in buf) / len(buf)) * scale
    log("wrote %s: %.2f s, peak %.3f (scale %.3f), rms %.4f"
        % (path, len(buf) / rate, min(peak, peak_target), scale, rms))
    return peak, rms


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("sequence")
    ap.add_argument("--banks-dir", required=True)
    ap.add_argument("--samples-dir", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--bank", help="bank json stem; default from sequences.json")
    ap.add_argument("--rate", type=int, default=32000)
    ap.add_argument("--seconds", type=float, default=40.0)
    ap.add_argument("--tempo-scale", type=float, default=1.0,
                    help="speed multiplier (>1 = faster, pitch unchanged)")
    ap.add_argument("--trim-start-tick", type=int, default=None)
    ap.add_argument("--trim-ticks", type=int, default=None)
    ap.add_argument("--pad-to-seconds", type=float, default=None,
                    help="loop-pad the trimmed window up to this length")
    ap.add_argument("--edge-fade-ms", type=float, default=3.0)
    ap.add_argument("--no-perc", action="store_true")
    args = ap.parse_args()

    with open(args.sequence, "rb") as f:
        seq_data = f.read()

    bank_name = args.bank
    if bank_name is None:
        seq_json = os.path.join(os.path.dirname(os.path.abspath(args.banks_dir)),
                                "sequences.json")
        stem = os.path.splitext(os.path.basename(args.sequence))[0]
        with open(seq_json) as f:
            mapping = json.load(f)
        bank_name = mapping[stem][0]
        log("bank for %s (from sequences.json): %s" % (stem, bank_name))

    bank = load_bank(args.banks_dir, bank_name, args.samples_dir)
    log("bank %s: %d instrument slots, %d drums"
        % (bank_name, len(bank.instrument_list), len(bank.percussion)))

    r = Renderer(seq_data, bank, args)
    r.run(args.seconds)
    log("parsed %d ticks (%.2f s): %d notes (%d melodic, %d percussion)"
        % (r.tick, r.time_sec, r.note_count, r.note_count - r.perc_count,
           r.perc_count))
    if r.skipped:
        log("skipped opcodes: %s" % ", ".join(
            "%s x%d" % kv for kv in sorted(r.skipped.items())))
    if r.note_count < 20:
        log("ERROR: fewer than 20 notes parsed; refusing to render garbage")
        sys.exit(1)

    buf = mix(r.notes, args.rate, r.time_sec + 1.0)

    if args.trim_start_tick is not None and args.trim_ticks is not None:
        t0 = r.tick_times[args.trim_start_tick]
        t1_tick = args.trim_start_tick + args.trim_ticks
        t1 = (r.tick_times[t1_tick] if t1_tick < len(r.tick_times)
              else r.time_sec)
        i0, i1 = int(t0 * args.rate), int(t1 * args.rate)
        log("trim: ticks %d..%d -> %.3f..%.3f s (%.3f s)"
            % (args.trim_start_tick, t1_tick, t0, t1, t1 - t0))
        buf = buf[i0:i1]
        if args.pad_to_seconds is not None:
            want = int(args.pad_to_seconds * args.rate)
            unit = len(buf)
            while len(buf) < want:
                buf.extend(buf[:min(unit, want - len(buf))])
        fade = int(args.edge_fade_ms / 1000.0 * args.rate)
        for i in range(min(fade, len(buf))):
            buf[i] *= i / fade
            buf[-1 - i] *= i / fade

    write_wav(args.out, buf, args.rate)


if __name__ == "__main__":
    main()
