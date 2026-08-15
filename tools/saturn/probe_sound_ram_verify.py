#!/usr/bin/env python3
"""Verify that the bytes STAGED in SCSP sound RAM match what the packager built.

Every prior audio investigation confirmed the control path (mailbox, rings,
SCSP slot registers) and the PCM file ON DISK.  Nobody ever compared the copy
that actually landed in sound RAM against the packager output.  A truncated,
misaligned, partially overwritten or wrapped staging copy would make the SCSP
play perfectly correct registers over corrupt data.

This probe boots the candidate in Ymir headless, runs to a confirmed gameplay
depth (mailbox reports the music voice live), then peeks sound RAM in chunks
and diffs three regions byte for byte:

  * SFXB metadata block   sound-RAM 0x05000  vs bob_sfx_metadata.bin
  * whole PCM bank        sound-RAM 0x08000  vs bob_sfx_pcm.bin
  * music sample region   sound-RAM 0x3ACB8  vs bob_theme_8k.pcm8

It also dumps the region immediately past the music loop end, which is what
the SCSP would play if it ever over-reads.

ADDRESSING: sound RAM is peeked through the SH-2 CPU bus at
0x25A00000 + <sound-RAM offset>, exactly as probe_audio_timeseries.py does for
the mailbox (0x25A04000) and the music diagnostics (0x25A07F00).  Every offset
printed in the report is a SOUND-RAM-RELATIVE offset unless the name says
"cpu_address".

Read-only: no target memory is written, no source file is touched.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
import time
from pathlib import Path
from typing import Any

from capture_route_views import YmirClient
from capture_sourceboot_boot_trace import run_bios_handoff
from probe_audio_mailbox import be16, peek

REPO_ROOT = Path(__file__).resolve().parents[2]

# Ymir's headless debug service hard-caps one exec.run_for at this many frames
# and one mem.peek at kMaxPeekBytes bytes (apps/ymir-headless/src/debug_service.cpp).
YMIR_MAX_RUN_FOR_FRAMES = 3600
YMIR_MAX_PEEK_BYTES = 65536
PEEK_CHUNK_BYTES = 32768

SOUND_RAM_BASE = 0x25A00000
SOUND_RAM_BYTES = 0x80000
MAILBOX_OFFSET = 0x4000
DIAGNOSTIC_OFFSET = 0x7F00
SFX_BUNDLE_OFFSET = 0x5000
BANK_OFFSET = 0x8000
SCSP_SLOT_ADDRESS = 0x25B00000

AUDIO_DIR = REPO_ROOT / "build" / "saturn" / "audio"
DEFAULT_MUSIC_PCM = AUDIO_DIR / "bob_theme_8k.pcm8"
DEFAULT_BANK_PCM = AUDIO_DIR / "generated" / "sourceboot-sfx" / "bob_sfx_pcm.bin"
DEFAULT_SFX_METADATA = AUDIO_DIR / "generated" / "sourceboot-sfx" / "bob_sfx_metadata.bin"
DEFAULT_DRIVER = REPO_ROOT / "build" / "saturn" / "audio68k" / "pcm68k-heartbeat.bin"


def peek_range(client: YmirClient, sound_ram_offset: int, count: int) -> bytes:
    """Read `count` bytes of sound RAM starting at a sound-RAM-relative offset."""
    out = bytearray()
    remaining = count
    offset = sound_ram_offset
    while remaining > 0:
        chunk = min(remaining, PEEK_CHUNK_BYTES)
        data = peek(client, SOUND_RAM_BASE + offset, chunk)
        if len(data) != chunk:
            raise RuntimeError(
                "short peek at sound-RAM 0x{:05X}: asked {}, got {}".format(
                    offset, chunk, len(data))
            )
        out.extend(data)
        offset += chunk
        remaining -= chunk
    return bytes(out)


def hexdump(data: bytes, base_offset: int, length: int) -> list[str]:
    lines = []
    for row in range(0, min(length, len(data)), 16):
        window = data[row:row + 16]
        hexpart = " ".join(f"{b:02x}" for b in window)
        ascii_part = "".join(chr(b) if 32 <= b < 127 else "." for b in window)
        lines.append(f"{base_offset + row:08x}  {hexpart:<47s}  |{ascii_part}|")
    return lines


def window_around(data: bytes, index: int, radius: int = 16) -> tuple[int, bytes]:
    start = max(0, index - radius)
    return start, data[start:start + 2 * radius]


def mismatch_runs(expected: bytes, observed: bytes, limit: int = 64
                  ) -> list[dict[str, int]]:
    """Collapse differing byte offsets into contiguous runs."""
    runs: list[dict[str, int]] = []
    n = min(len(expected), len(observed))
    index = 0
    while index < n and len(runs) < limit:
        if expected[index] != observed[index]:
            start = index
            while index < n and expected[index] != observed[index]:
                index += 1
            runs.append({"start": start, "length": index - start})
        else:
            index += 1
    return runs


def stride_analysis(offsets: list[int]) -> dict[str, Any]:
    """Look for a periodic pattern in the differing byte offsets."""
    if len(offsets) < 3:
        return {"periodic": False, "reason": "fewer than 3 differing bytes"}
    gaps = [offsets[i + 1] - offsets[i] for i in range(len(offsets) - 1)]
    unique = sorted(set(gaps))
    info: dict[str, Any] = {
        "gap_min": min(gaps),
        "gap_max": max(gaps),
        "distinct_gaps": unique[:16],
        "distinct_gap_count": len(unique),
    }
    if len(unique) == 1:
        info["periodic"] = True
        info["stride"] = unique[0]
        return info
    # Constant modulus is the other classic signature (every Nth byte in a
    # fixed-size block, e.g. an odd/even byte-lane fault or a block-header stomp).
    for modulus in (2, 4, 8, 16, 32, 64, 256, 512, 1024, 2048, 4096, 8192,
                    16384, 32768, 65536):
        residues = {off % modulus for off in offsets}
        if len(residues) <= max(1, modulus // 8) and len(residues) < len(offsets):
            info["periodic"] = True
            info["modulus"] = modulus
            info["residues"] = sorted(residues)[:32]
            return info
    info["periodic"] = False
    return info


def identify_bytes(window: bytes, corpora: dict[str, bytes]) -> dict[str, Any]:
    """Try to name what the observed bytes actually are."""
    findings: dict[str, Any] = {}
    if not window:
        return findings
    if all(b == 0 for b in window):
        findings["all_zero"] = True
    if all(b == window[0] for b in window):
        findings["constant_byte"] = window[0]
    printable = sum(1 for b in window if 32 <= b < 127 or b in (9, 10, 13))
    findings["printable_fraction"] = round(printable / len(window), 3)
    probe = window[:16] if len(window) >= 16 else window
    for name, corpus in corpora.items():
        if not corpus:
            continue
        hit = corpus.find(probe)
        if hit >= 0:
            findings["found_in_" + name] = hit
    return findings


def compare_region(name: str, sound_ram_offset: int, expected: bytes,
                   observed: bytes, corpora: dict[str, bytes]) -> dict[str, Any]:
    result: dict[str, Any] = {
        "region": name,
        "sound_ram_offset": sound_ram_offset,
        "sound_ram_offset_hex": f"0x{sound_ram_offset:05X}",
        "cpu_address_hex": f"0x{SOUND_RAM_BASE + sound_ram_offset:08X}",
        "expected_bytes": len(expected),
        "observed_bytes": len(observed),
        "expected_sha256": hashlib.sha256(expected).hexdigest(),
        "observed_sha256": hashlib.sha256(observed).hexdigest(),
    }
    n = min(len(expected), len(observed))
    diff_offsets = [i for i in range(n) if expected[i] != observed[i]]
    result["differing_bytes"] = len(diff_offsets)
    result["match"] = (len(diff_offsets) == 0 and len(expected) == len(observed))
    if not diff_offsets:
        result["verdict"] = "EXACT MATCH"
        return result
    first = diff_offsets[0]
    last = diff_offsets[-1]
    result["first_mismatch_offset"] = first
    result["first_mismatch_offset_hex"] = f"0x{first:05X}"
    result["first_mismatch_sound_ram_hex"] = f"0x{sound_ram_offset + first:05X}"
    result["last_mismatch_offset"] = last
    result["last_mismatch_offset_hex"] = f"0x{last:05X}"
    result["mismatch_fraction"] = round(len(diff_offsets) / n, 6)
    start, exp_win = window_around(expected, first)
    _, obs_win = window_around(observed, first)
    result["first_mismatch_expected_hexdump"] = hexdump(exp_win, start, len(exp_win))
    result["first_mismatch_observed_hexdump"] = hexdump(obs_win, start, len(obs_win))
    result["runs"] = mismatch_runs(expected, observed)
    result["run_count_capped_at"] = 64
    result["stride"] = stride_analysis(diff_offsets)
    result["contiguous_tail"] = (last == n - 1 and
                                 len(diff_offsets) == n - first)
    result["observed_identity_at_first_mismatch"] = identify_bytes(obs_win, corpora)
    result["verdict"] = "MISMATCH"
    return result


def signed_stats(data: bytes) -> dict[str, Any]:
    if not data:
        return {}
    vals = [b - 256 if b > 127 else b for b in data]
    return {
        "count": len(vals),
        "min": min(vals),
        "max": max(vals),
        "mean": round(sum(vals) / len(vals), 3),
        "abs_mean": round(sum(abs(v) for v in vals) / len(vals), 3),
    }


# Candidate address spaces a stray per-frame writer could really have meant.
# (label, CPU address, bytes) -- all cache-through views.
SEARCH_SPACES = (
    ("lwram", 0x20200000, 0x100000),
    ("hwram", 0x26000000, 0x100000),
    ("vdp1_vram", 0x25C00000, 0x80000),
    ("vdp1_framebuffer", 0x25C80000, 0x40000),
    ("vdp2_vram", 0x25E00000, 0x80000),
)


def locate_window_source(client: YmirClient, window: bytes,
                         probe_bytes: int = 256) -> dict[str, Any]:
    """Search the other address spaces for the bytes sitting in the window.

    If the live sound-RAM window is a stray copy of a buffer that belongs
    somewhere else, the real buffer is still in that other space right now.
    """
    needles = {
        "head": window[:probe_bytes],
        "middle": window[len(window) // 2:len(window) // 2 + probe_bytes],
        "tail": window[-probe_bytes:],
    }
    findings: dict[str, Any] = {"probe_bytes": probe_bytes, "spaces": {}}
    for label, address, size in SEARCH_SPACES:
        try:
            blob = bytearray()
            remaining = size
            offset = 0
            while remaining > 0:
                chunk = min(remaining, PEEK_CHUNK_BYTES)
                blob.extend(peek(client, address + offset, chunk))
                offset += chunk
                remaining -= chunk
        except (RuntimeError, TimeoutError) as error:
            findings["spaces"][label] = {"error": str(error)}
            continue
        data = bytes(blob)
        hits = {}
        for name, needle in needles.items():
            index = data.find(needle)
            hits[name] = {
                "found": index >= 0,
                "offset": index,
                "cpu_address_hex": f"0x{address + index:08X}" if index >= 0 else None,
            }
        findings["spaces"][label] = {
            "cpu_base_hex": f"0x{address:08X}",
            "bytes": size,
            "sha256": hashlib.sha256(data).hexdigest(),
            "hits": hits,
        }
        print(f"[verify] searched {label}: "
              + ", ".join(f"{n}={'HIT@' + h['cpu_address_hex'] if h['found'] else 'miss'}"
                          for n, h in hits.items()),
              file=sys.stderr, flush=True)
    return findings


# SCSP common-control and DSP register blocks (CPU-bus view).  The SCSP's own
# effect DSP writes a ring buffer straight into sound RAM at RBP/RBL, entirely
# behind the SH-2's back, so these registers are the only place a sound-RAM
# stomp with no SH-2 writer can come from.
SCSP_COMMON_ADDRESS = 0x25B00400
SCSP_COMMON_BYTES = 0x40
SCSP_DSP_COEF_ADDRESS = 0x25B00700   # COEF 0x700..0x73F
SCSP_DSP_MADRS_ADDRESS = 0x25B00780  # MADRS 0x780..0x7BF
SCSP_DSP_MPRO_ADDRESS = 0x25B00800   # MPRO 0x800..0xBFF (64 x 64-bit steps)
SCSP_DSP_MPRO_BYTES = 0x400


def probe_scsp_dsp(client: YmirClient) -> dict[str, Any]:
    """Read the SCSP common control + effect-DSP blocks and decode RBP/RBL."""
    common = peek(client, SCSP_COMMON_ADDRESS, SCSP_COMMON_BYTES)
    coef = peek(client, SCSP_DSP_COEF_ADDRESS, 0x40)
    madrs = peek(client, SCSP_DSP_MADRS_ADDRESS, 0x40)
    mpro = peek(client, SCSP_DSP_MPRO_ADDRESS, SCSP_DSP_MPRO_BYTES)
    words = [be16(common, i * 2) for i in range(SCSP_COMMON_BYTES // 2)]
    reg400 = words[0]
    reg402 = words[1]
    # RBL selects 8k/16k/32k/64k WORDS; RBP is the base in 4k-word units.
    rbl = (reg402 >> 7) & 0x3
    rbp = reg402 & 0x7F
    ring_words = 8192 << rbl
    return {
        "common_words": words,
        "reg400_hex": f"0x{reg400:04X}",
        "reg402_hex": f"0x{reg402:04X}",
        "mem4mb": bool(reg400 & 0x0200),
        "dac18b": bool(reg400 & 0x0100),
        "mvol": reg400 & 0x000F,
        "rbl": rbl,
        "rbp": rbp,
        "ring_words": ring_words,
        "ring_bytes": ring_words * 2,
        "ring_base_if_rbp_x2000": f"0x{rbp * 0x2000:05X}",
        "ring_base_if_rbp_x1000": f"0x{rbp * 0x1000:05X}",
        "ring_base_if_rbp_x4000": f"0x{rbp * 0x4000:05X}",
        "coef_nonzero": sum(1 for b in coef if b != 0),
        "madrs_nonzero": sum(1 for b in madrs if b != 0),
        "madrs_words": [be16(madrs, i * 2) for i in range(0x20)],
        "mpro_nonzero": sum(1 for b in mpro if b != 0),
        "mpro_all_zero": all(b == 0 for b in mpro),
        "mpro_first_64_hex": bytes(mpro[:64]).hex(),
    }


def probe_sound_ram(client: YmirClient, *, warmup_frames: int,
                    gameplay_poll_frames: int, gameplay_max_frames: int,
                    music_offset: int, tail_bytes: int,
                    music_pcm: Path, bank_pcm: Path, sfx_metadata: Path,
                    driver: Path | None,
                    raw_dump_dir: Path | None = None,
                    resample_frames: int = 0,
                    locate_source: bool = False) -> dict[str, Any]:
    frame = 0
    started = time.perf_counter()

    def run_for(count: int) -> None:
        nonlocal frame
        remaining = count
        while remaining > 0:
            chunk = min(remaining, YMIR_MAX_RUN_FOR_FRAMES)
            client.call("exec.run_for", {"frames": chunk})
            remaining -= chunk
            frame += chunk

    run_bios_handoff(client, run_for, lambda _label: None)
    handoff_frame = frame
    print(f"[verify] BIOS handoff done at frame {frame}", file=sys.stderr, flush=True)
    run_for(warmup_frames)
    print(f"[verify] warmup done at frame {frame} "
          f"({time.perf_counter() - started:.1f}s wall)", file=sys.stderr, flush=True)

    def mailbox_state() -> dict[str, int]:
        mailbox = peek(client, SOUND_RAM_BASE + MAILBOX_OFFSET, 0x40)
        diagnostics = peek(client, SOUND_RAM_BASE + DIAGNOSTIC_OFFSET, 0x22)
        return {
            "mb.magic": be16(mailbox, 0x00),
            "mb.status": be16(mailbox, 0x04),
            "mb.heartbeat": be16(mailbox, 0x06),
            "mb.voices_started": be16(mailbox, 0x12),
            "mb.active_voice_count": be16(mailbox, 0x3C),
            "mb.invalid_samples": be16(mailbox, 0x1A),
            "music.starts": be16(diagnostics, 0),
            "music.faults": be16(diagnostics, 2),
            "music.active": be16(diagnostics, 6),
        }

    gameplay_frame = None
    waited = 0
    state = mailbox_state()
    while True:
        if state["music.active"] == 1 and state["mb.voices_started"] >= 1:
            gameplay_frame = frame
            break
        if waited >= gameplay_max_frames:
            break
        run_for(gameplay_poll_frames)
        waited += gameplay_poll_frames
        state = mailbox_state()
    print(f"[verify] music active at frame {gameplay_frame} "
          f"(waited {waited} extra frames)", file=sys.stderr, flush=True)

    # SCSP slot 0 registers: proves the dump is aimed where the hardware reads.
    slot0 = peek(client, SCSP_SLOT_ADDRESS, 0x20)
    keys = be16(slot0, 0x00)
    sa = ((keys & 0x000F) << 16) | be16(slot0, 0x02)
    scsp = {
        "keys": keys,
        "kyonb": bool(keys & 0x0800),
        "pcm8b": bool(keys & 0x0010),
        "lpctl": (keys >> 5) & 0x3,
        "sa": sa,
        "sa_hex": f"0x{sa:05X}",
        "lsa": be16(slot0, 0x04),
        "lea": be16(slot0, 0x06),
        "eg": be16(slot0, 0x08),
        "release": be16(slot0, 0x0A),
        "attenuation": be16(slot0, 0x0C),
        "pitch": be16(slot0, 0x10),
        "pan_send": be16(slot0, 0x16),
    }
    print(f"[verify] SCSP slot0 SA={scsp['sa_hex']} LSA={scsp['lsa']} "
          f"LEA={scsp['lea']} PCM8B={scsp['pcm8b']} LPCTL={scsp['lpctl']}",
          file=sys.stderr, flush=True)

    scsp_dsp = probe_scsp_dsp(client)
    print(f"[verify] SCSP reg402={scsp_dsp['reg402_hex']} RBP={scsp_dsp['rbp']} "
          f"RBL={scsp_dsp['rbl']} ring={scsp_dsp['ring_bytes']}B "
          f"base(x0x2000)={scsp_dsp['ring_base_if_rbp_x2000']} "
          f"MPRO nonzero={scsp_dsp['mpro_nonzero']}",
          file=sys.stderr, flush=True)

    expected_music = music_pcm.read_bytes()
    expected_bank = bank_pcm.read_bytes()
    expected_meta = sfx_metadata.read_bytes()
    corpora = {
        "bank_blob": expected_bank,
        "metadata_blob": expected_meta,
        "music_pcm": expected_music,
    }
    if driver is not None and driver.is_file():
        corpora["driver_image"] = driver.read_bytes()

    print(f"[verify] dumping metadata {len(expected_meta)}B, bank "
          f"{len(expected_bank)}B, music {len(expected_music)}B",
          file=sys.stderr, flush=True)

    observed_meta = peek_range(client, SFX_BUNDLE_OFFSET, len(expected_meta))
    observed_bank = peek_range(client, BANK_OFFSET, len(expected_bank))
    # The music region lives inside the bank; slice it out of the same dump so
    # the two comparisons are guaranteed to describe one coherent memory state.
    music_in_bank = music_offset - BANK_OFFSET
    if 0 <= music_in_bank and music_in_bank + len(expected_music) <= len(observed_bank):
        observed_music = observed_bank[music_in_bank:music_in_bank + len(expected_music)]
        music_source = "sliced from the bank dump"
    else:
        observed_music = peek_range(client, music_offset, len(expected_music))
        music_source = "peeked directly"

    loop_end = music_offset + len(expected_music)
    tail_count = min(tail_bytes, SOUND_RAM_BYTES - loop_end)
    tail = peek_range(client, loop_end, tail_count) if tail_count > 0 else b""

    regions = [
        compare_region("sfxb_metadata", SFX_BUNDLE_OFFSET, expected_meta,
                       observed_meta, corpora),
        compare_region("pcm_bank", BANK_OFFSET, expected_bank,
                       observed_bank, corpora),
        compare_region("music_sample", music_offset, expected_music,
                       observed_music, corpora),
    ]

    tail_report: dict[str, Any] = {
        "sound_ram_offset": loop_end,
        "sound_ram_offset_hex": f"0x{loop_end:05X}",
        "cpu_address_hex": f"0x{SOUND_RAM_BASE + loop_end:08X}",
        "bytes": len(tail),
        "sha256": hashlib.sha256(tail).hexdigest(),
        "all_zero": all(b == 0 for b in tail) if tail else None,
        "nonzero_bytes": sum(1 for b in tail if b != 0),
        "distinct_values": len(set(tail)),
        "first_nonzero_offset": next((i for i, b in enumerate(tail) if b != 0), None),
        "identity": identify_bytes(tail[:64], corpora),
        "hexdump_first_256": hexdump(tail, loop_end, 256),
        "hexdump_last_64": hexdump(tail[-64:], loop_end + max(0, len(tail) - 64), 64),
        # Signed-8 statistics either side of the loop point: a click at the seam
        # shows up as a DC step, and over-read shows up as a level jump.
        "signed_stats": signed_stats(tail),
        "music_last_64_signed_stats": signed_stats(observed_music[-64:]),
    }

    if raw_dump_dir is not None:
        raw_dump_dir.mkdir(parents=True, exist_ok=True)
        (raw_dump_dir / "observed_bank.bin").write_bytes(observed_bank)
        (raw_dump_dir / "observed_metadata.bin").write_bytes(observed_meta)
        (raw_dump_dir / "observed_past_loop_end.bin").write_bytes(tail)
        print(f"[verify] raw dumps written to {raw_dump_dir}", file=sys.stderr,
              flush=True)

    # Liveness: is the corrupt window a one-shot stomp or a buffer somebody is
    # still writing?  Re-read the same window after more emulated time and diff
    # it against itself.
    resample: dict[str, Any] = {"requested_frames": resample_frames}
    source_search: dict[str, Any] = {}
    bank_region = regions[1]
    if locate_source and not bank_region["match"]:
        window = observed_bank[bank_region["first_mismatch_offset"]:
                               bank_region["last_mismatch_offset"] + 1]
        source_search = locate_window_source(client, window)
    if resample_frames > 0 and not bank_region["match"]:
        window_start = BANK_OFFSET + bank_region["first_mismatch_offset"]
        window_end = BANK_OFFSET + bank_region["last_mismatch_offset"] + 1
        window_len = window_end - window_start
        before = observed_bank[bank_region["first_mismatch_offset"]:
                               bank_region["last_mismatch_offset"] + 1]
        run_for(resample_frames)
        after = peek_range(client, window_start, window_len)
        changed = [i for i in range(window_len) if before[i] != after[i]]
        resample.update({
            "window_sound_ram_hex": f"0x{window_start:05X}..0x{window_end - 1:05X}",
            "window_bytes": window_len,
            "frames_advanced": resample_frames,
            "frame_after": frame,
            "bytes_changed": len(changed),
            "static": len(changed) == 0,
            "first_change_offset_hex": (f"0x{window_start + changed[0]:05X}"
                                        if changed else None),
            "sha256_before": hashlib.sha256(before).hexdigest(),
            "sha256_after": hashlib.sha256(after).hexdigest(),
        })
        if raw_dump_dir is not None:
            (raw_dump_dir / "corrupt_window_before.bin").write_bytes(before)
            (raw_dump_dir / "corrupt_window_after.bin").write_bytes(after)
        print(f"[verify] resample after {resample_frames} frames: "
              f"{len(changed)} of {window_len} bytes changed",
              file=sys.stderr, flush=True)

    return {
        "evidence_kind": "sm64-saturn-sound-ram-staging-verify",
        "addressing": {
            "space": "SH-2 CPU bus view of SCSP sound RAM",
            "sound_ram_base_cpu_address": f"0x{SOUND_RAM_BASE:08X}",
            "note": "all region offsets in this report are sound-RAM-relative",
        },
        "handoff_frame": handoff_frame,
        "warmup_frames": warmup_frames,
        "gameplay_confirmed_frame": gameplay_frame,
        "final_frame": frame,
        "mailbox_state": state,
        "scsp_slot0": scsp,
        "scsp_dsp": scsp_dsp,
        "music_observed_source": music_source,
        "inputs": {
            "music_pcm": str(music_pcm),
            "bank_pcm": str(bank_pcm),
            "sfx_metadata": str(sfx_metadata),
            "driver": str(driver) if driver else None,
        },
        "regions": regions,
        "past_loop_end": tail_report,
        "corrupt_window_resample": resample,
        "window_source_search": source_search,
        "wall_seconds": round(time.perf_counter() - started, 2),
    }


def format_summary(report: dict[str, Any]) -> str:
    lines = ["== staged sound RAM vs packager output =="]
    for region in report["regions"]:
        lines.append(
            f"{region['region']:<16s} @ {region['sound_ram_offset_hex']} "
            f"({region['expected_bytes']} B): {region['verdict']}"
        )
        if region["match"]:
            continue
        lines.append(f"  differing bytes : {region['differing_bytes']} "
                     f"({region['mismatch_fraction'] * 100:.4f}% of the region)")
        lines.append(f"  first mismatch  : region offset "
                     f"{region['first_mismatch_offset_hex']} "
                     f"= sound RAM {region['first_mismatch_sound_ram_hex']}")
        lines.append(f"  last mismatch   : region offset "
                     f"{region['last_mismatch_offset_hex']}")
        lines.append(f"  contiguous tail : {region['contiguous_tail']}")
        lines.append(f"  stride analysis : {json.dumps(region['stride'])}")
        lines.append("  expected:")
        lines.extend("    " + line for line in region["first_mismatch_expected_hexdump"])
        lines.append("  observed:")
        lines.extend("    " + line for line in region["first_mismatch_observed_hexdump"])
        lines.append("  observed identity: "
                     + json.dumps(region["observed_identity_at_first_mismatch"]))
        lines.append("  first runs      : " + json.dumps(region["runs"][:8]))
    tail = report["past_loop_end"]
    lines.append("")
    lines.append(f"== {tail['bytes']} bytes past the music loop end "
                 f"({tail['sound_ram_offset_hex']}) ==")
    lines.append(f"all_zero={tail['all_zero']} nonzero={tail['nonzero_bytes']} "
                 f"distinct={tail['distinct_values']} "
                 f"first_nonzero={tail['first_nonzero_offset']}")
    lines.append("identity: " + json.dumps(tail["identity"]))
    lines.append("signed stats: " + json.dumps(tail["signed_stats"]))
    lines.append("music last 64 signed stats: "
                 + json.dumps(tail["music_last_64_signed_stats"]))
    lines.extend(tail["hexdump_first_256"])
    search = report.get("window_source_search") or {}
    if search.get("spaces"):
        lines.append("")
        lines.append("== where else do the window's bytes live right now? ==")
        for label, info in search["spaces"].items():
            if "error" in info:
                lines.append(f"{label}: ERROR {info['error']}")
                continue
            marks = ", ".join(
                f"{name}={'HIT ' + hit['cpu_address_hex'] if hit['found'] else 'miss'}"
                for name, hit in info["hits"].items())
            lines.append(f"{label} @ {info['cpu_base_hex']}: {marks}")
    dsp = report.get("scsp_dsp") or {}
    if dsp:
        lines.append("")
        lines.append("== SCSP common control / effect DSP ==")
        lines.append(f"reg400={dsp['reg400_hex']} MEM4MB={dsp['mem4mb']} "
                     f"MVOL={dsp['mvol']}")
        lines.append(f"reg402={dsp['reg402_hex']} RBP={dsp['rbp']} RBL={dsp['rbl']} "
                     f"-> ring buffer {dsp['ring_bytes']} bytes, base "
                     f"{dsp['ring_base_if_rbp_x2000']} (RBP*0x2000) / "
                     f"{dsp['ring_base_if_rbp_x1000']} (RBP*0x1000) / "
                     f"{dsp['ring_base_if_rbp_x4000']} (RBP*0x4000)")
        lines.append(f"DSP program bytes nonzero: {dsp['mpro_nonzero']} of 1024 "
                     f"(all_zero={dsp['mpro_all_zero']}), COEF nonzero "
                     f"{dsp['coef_nonzero']}, MADRS nonzero {dsp['madrs_nonzero']}")
        lines.append(f"MPRO[0:64]={dsp['mpro_first_64_hex']}")
    resample = report.get("corrupt_window_resample") or {}
    if resample.get("window_bytes"):
        lines.append("")
        lines.append("== corrupt-window liveness ==")
        lines.append(f"window {resample['window_sound_ram_hex']} "
                     f"({resample['window_bytes']} B) re-read after "
                     f"{resample['frames_advanced']} frames: "
                     f"{resample['bytes_changed']} bytes changed "
                     f"(static={resample['static']})")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ymir", type=Path, required=True)
    parser.add_argument("--ipl", type=Path, required=True)
    parser.add_argument("--cue", type=Path, required=True,
                        help="absolute path to the CUE (YmirClient changes cwd)")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--warmup-frames", type=int, default=3600)
    parser.add_argument("--gameplay-poll-frames", type=int, default=300)
    parser.add_argument("--gameplay-max-frames", type=int, default=7200)
    parser.add_argument("--music-offset", type=lambda v: int(v, 0), default=0x3ACB8,
                        help="sound-RAM-relative start of the music sample "
                             "(SCSP SA), default 0x3ACB8")
    parser.add_argument("--tail-bytes", type=int, default=1024,
                        help="bytes to dump immediately past the music loop end")
    parser.add_argument("--music-pcm", type=Path, default=DEFAULT_MUSIC_PCM)
    parser.add_argument("--bank-pcm", type=Path, default=DEFAULT_BANK_PCM)
    parser.add_argument("--sfx-metadata", type=Path, default=DEFAULT_SFX_METADATA)
    parser.add_argument("--driver", type=Path, default=DEFAULT_DRIVER,
                        help="68K driver image, used only to name stray bytes")
    parser.add_argument("--raw-dump-dir", type=Path,
                        help="write the observed sound-RAM dumps as .bin here")
    parser.add_argument("--locate-source", action="store_true",
                        help="search LWRAM/HWRAM/VDP1/VDP2 for the bytes sitting "
                             "in the mismatched window, to name the buffer whose "
                             "contents actually landed in sound RAM")
    parser.add_argument("--resample-frames", type=int, default=0,
                        help="after the first dump, advance this many frames and "
                             "re-read the mismatched window to tell a one-shot "
                             "stomp from a buffer still being written")
    parser.add_argument("--timeout", type=float, default=5400.0)
    args = parser.parse_args(argv)

    for label, path in (("Ymir", args.ymir), ("IPL", args.ipl), ("CUE", args.cue),
                        ("music PCM", args.music_pcm), ("bank PCM", args.bank_pcm),
                        ("SFX metadata", args.sfx_metadata)):
        if not path.is_file():
            parser.error(f"{label} is not a file: {path}")

    client = YmirClient(args.ymir.resolve(), args.ipl.resolve(),
                        args.cue.resolve(), args.timeout)
    try:
        report = probe_sound_ram(
            client,
            warmup_frames=args.warmup_frames,
            gameplay_poll_frames=args.gameplay_poll_frames,
            gameplay_max_frames=args.gameplay_max_frames,
            music_offset=args.music_offset,
            tail_bytes=args.tail_bytes,
            music_pcm=args.music_pcm,
            bank_pcm=args.bank_pcm,
            sfx_metadata=args.sfx_metadata,
            driver=args.driver,
            raw_dump_dir=args.raw_dump_dir,
            resample_frames=args.resample_frames,
            locate_source=args.locate_source,
        )
    finally:
        try:
            client.shutdown()
        except (RuntimeError, TimeoutError):
            client.abort()

    report["summary"] = format_summary(report)
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                               encoding="utf-8")
    print(report["summary"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
