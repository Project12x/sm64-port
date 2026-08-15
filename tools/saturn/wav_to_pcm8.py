#!/usr/bin/env python3
"""Convert a WAV file to raw signed 8-bit mono PCM for the SCSP.

Usage: wav_to_pcm8.py IN.wav OUT.pcm8 --rate 8000 [--max-seconds 28]
Owner-provided WAVs are ROM-derived and must never be committed.
"""
import argparse, struct, sys, wave

def load_wav_mono_float(path):
    with wave.open(path, "rb") as w:
        ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    if width == 2:
        samples = struct.unpack("<%dh" % (n * ch), raw)
        scale = 32768.0
    elif width == 1:
        samples = [b - 128 for b in raw]
        scale = 128.0
    else:
        raise SystemExit("unsupported sample width: %d" % width)
    mono = [sum(samples[i * ch:(i + 1) * ch]) / (ch * scale) for i in range(n)]
    return mono, rate

def resample_linear(mono, src_rate, dst_rate):
    if src_rate == dst_rate:
        return list(mono)
    out_n = int(len(mono) * dst_rate / src_rate)
    out = []
    for i in range(out_n):
        pos = i * src_rate / dst_rate
        j = int(pos)
        frac = pos - j
        a = mono[j]
        b = mono[j + 1] if j + 1 < len(mono) else a
        out.append(a + (b - a) * frac)
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input"); ap.add_argument("output")
    ap.add_argument("--rate", type=int, default=8000)
    ap.add_argument("--max-seconds", type=float, default=28.0)
    args = ap.parse_args()
    mono, src_rate = load_wav_mono_float(args.input)
    out = resample_linear(mono, src_rate, args.rate)
    limit = int(args.rate * args.max_seconds)
    if len(out) > limit:
        out = out[:limit]
        print("note: trimmed to %.1f s" % args.max_seconds, file=sys.stderr)
    data = bytes((max(-128, min(127, round(s * 127.0))) & 0xFF) for s in out)
    with open(args.output, "wb") as f:
        f.write(data)
    print("%s: %d bytes @ %d Hz (%.1f s)" % (args.output, len(data), args.rate, len(data) / args.rate))

if __name__ == "__main__":
    main()
