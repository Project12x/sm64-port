#!/usr/bin/env python3
"""Read the play_sound() rejection counters out of a running sourceboot build.

play_sound() in src/port/saturn/sourceboot/source_audio_semantics.c has six
paths that drop a sound and return, and before this probe only two of them
touched a counter -- gAudioErrorFlags, shared between them.  A sound that never
reached the 68K driver therefore left no evidence of WHICH gate ate it.

This tool reads the per-path counters so the specific gate is named rather than
guessed.

Measured 2026-09-03 on build 1373745fe9536f7a over 98 s of route gameplay:
calls 1349, accepted 256, reject_policy_refresh 1093, every other gate 0.
`accepted` saturates at exactly SM64_SATURN_AUDIO_REQUEST_CAPACITY (256) and
never moves again -- the request table fills once and is never retired, so
every later play_sound() is refused forever.  Run alongside
probe_audio_timeseries.py, which over the same window reports only mb.heartbeat
and scsp.monitor changing: the 68K driver is alive, and the control/SFX/
completion rings never move.  The policy layer accepts requests and publishes
none of them.

Read-only: no target memory is written.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

from capture_route_views import YmirClient
from capture_sourceboot_boot_trace import run_bios_handoff
from probe_audio_mailbox import peek

PROBE_SYMBOL = "g_sm64_saturn_play_sound_probe"
PROBE_MAGIC = 0x50534E44  # 'PSND'

# Field order must match sm64_saturn_play_sound_probe_t.
FIELDS = (
    "magic",
    "calls",
    "reject_uninitialized",
    "reject_null_pos",
    "reject_no_sound",
    "reject_bank_range",
    "reject_sound_id_invalid",
    "reject_no_source_token",
    "reject_policy_refresh",
    "accepted",
    "last_sound_bits",
    "last_accepted_sound_bits",
)
PROBE_BYTES = len(FIELDS) * 4

# Ymir's exec.run_for silently no-ops above this; advancing further must be
# done in chunks or the run reports success having advanced nothing.
MAX_FRAMES_PER_CALL = 3600


def resolve_symbol(nm: Path, elf: Path, symbol: str) -> int:
    out = subprocess.run([str(nm), str(elf)], capture_output=True, text=True,
                         check=True).stdout
    # sh-elf prefixes C symbols with an underscore; accept either spelling.
    wanted = {symbol, f"_{symbol}"}
    matches = [line.split()[0] for line in out.splitlines()
               if line.split() and line.split()[-1] in wanted]
    if not matches:
        raise SystemExit(
            f"{symbol} is not in {elf}. Build with the instrumentation applied.")
    if len(set(matches)) != 1:
        raise SystemExit(f"{symbol} is ambiguous in {elf}: {sorted(set(matches))}")
    return int(matches[0], 16)


def be32(data: list[int], off: int) -> int:
    return ((data[off] << 24) | (data[off + 1] << 16) |
            (data[off + 2] << 8) | data[off + 3])


def read_probe(client: YmirClient, address: int) -> dict[str, int]:
    raw = peek(client, address, PROBE_BYTES)
    return {name: be32(raw, i * 4) for i, name in enumerate(FIELDS)}


def advance(client: YmirClient, frames: int) -> None:
    remaining = frames
    while remaining > 0:
        step = min(remaining, MAX_FRAMES_PER_CALL)
        client.call("exec.run_for", {"frames": step})
        remaining -= step


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ymir", type=Path, default=Path(
        r"D:\Code\RetroDev\sm64-saturn-port\ymir-agent\build-agent2"
        r"\apps\ymir-headless\Release\ymir-headless.exe"))
    parser.add_argument("--ipl", type=Path, required=True)
    parser.add_argument("--cue", type=Path, required=True,
                        help="absolute path to the CUE (YmirClient changes cwd)")
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--nm", type=Path, default=Path(
        r"D:\Code\RetroDev\sm64-saturn-port\work\yaul-install\bin\sh-elf-nm.exe"))
    parser.add_argument("--warmup-frames", type=int, default=3000)
    parser.add_argument("--interval-frames", type=int, default=1800)
    parser.add_argument("--samples", type=int, default=4)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--timeout", type=int, default=1800)
    args = parser.parse_args()

    address = resolve_symbol(args.nm.resolve(), args.elf.resolve(), PROBE_SYMBOL)
    print(f"{PROBE_SYMBOL} @ 0x{address:08X}")

    client = YmirClient(args.ymir.resolve(), args.ipl.resolve(),
                        args.cue.resolve(), args.timeout)
    samples: list[dict[str, Any]] = []
    try:
        run_bios_handoff(
            client,
            lambda frames: client.call("exec.run_for", {"frames": frames}),
            lambda _label: None)
        advance(client, args.warmup_frames)
        elapsed = args.warmup_frames
        for _ in range(args.samples):
            values = read_probe(client, address)
            values["frames"] = elapsed
            samples.append(values)
            advance(client, args.interval_frames)
            elapsed += args.interval_frames
    finally:
        try:
            client.shutdown()
        except (RuntimeError, TimeoutError):
            client.abort()

    live = [s for s in samples if s["magic"] == PROBE_MAGIC]
    if not live:
        print("\nPROBE NEVER ARMED: magic is unset in every sample, so "
              "play_sound() was never entered once.")
        print("The sound loss is UPSTREAM of play_sound -- the game never "
              "requests a sound on this route.")
    else:
        cols = [f for f in FIELDS if f != "magic"]
        width = max(len(c) for c in cols)
        print(f"\n{'field':<{width}} " +
              " ".join(f"{s['frames']:>9}" for s in samples))
        for col in cols:
            row = " ".join(f"{s[col]:>9}" for s in samples)
            print(f"{col:<{width}} {row}")

        last = samples[-1]
        print()
        if last["calls"] == 0:
            print("play_sound() was NEVER CALLED. The loss is upstream of it.")
        elif last["accepted"] == 0:
            drops = {c: last[c] for c in cols
                     if c.startswith("reject_") and last[c] > 0}
            top = max(drops, key=drops.get) if drops else None
            print(f"play_sound() called {last['calls']} times, accepted NONE.")
            if top:
                print(f"Dominant gate: {top} ({drops[top]} of {last['calls']}).")
                print(f"All gates hit: {drops}")
            else:
                print("No rejection counter moved either -- accounting gap.")
        else:
            print(f"play_sound() accepted {last['accepted']} of "
                  f"{last['calls']} calls.")
            saturated = (len(samples) > 1 and
                         samples[-1]["accepted"] == samples[-2]["accepted"] and
                         samples[-1]["calls"] > samples[-2]["calls"])
            if saturated:
                print(f"SATURATED: accepted stopped at {last['accepted']} while "
                      f"calls kept climbing, and {last['reject_policy_refresh']} "
                      "calls now fail at policy_play_refresh.")
                print("The request table filled and is never retired -- compare "
                      "against SM64_SATURN_AUDIO_REQUEST_CAPACITY in "
                      "src/port/saturn/audio/saturn_audio_policy.h.")
                print("Check probe_audio_timeseries.py: if the control/SFX rings "
                      "never move, the policy accepts requests and publishes "
                      "none, so nothing is ever there to complete and retire.")
            else:
                print("Accepted is still climbing; the policy layer is keeping "
                      "up. Any remaining loss is downstream of it.")

    if args.output:
        args.output.write_text(json.dumps(
            {"symbol": PROBE_SYMBOL, "address": address,
             "elf": str(args.elf), "samples": samples}, indent=1),
            encoding="utf-8")
        print(f"\nwrote {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
