#!/usr/bin/env python3
"""Capture the replay-only SMC1 math corpus through Ymir.

The capture intentionally waits for the existing sourceboot SBR2 checkpoint,
then reads the separately linked SMC1 block from the same paused emulation
state.  It records raw bytes as well as decoded IEEE-754 input bits so a host
differential test can consume the capture without trusting formatted floats.
"""

from __future__ import annotations

import argparse
import json
import struct
import time
from pathlib import Path
from typing import Any

from capture_hwtest import artifact_identity, emulation_timing, newest_sibling_elf, stale_game_image
from capture_route_views import YmirClient, peek_route


MATH_MAGIC = 0x534D4331  # SMC1
MATH_VERSION = 2
MATH_CORPUS_SLOTS = 64
MATH_HEADER_FIELDS = (
    "magic", "version", "replay_ticks", "atan2s_calls", "atan2_lookup_calls",
    "atan2s_samples", "atan2_lookup_samples",
)
MATH_BYTES = (len(MATH_HEADER_FIELDS) + 2 * MATH_CORPUS_SLOTS * 3) * 4
MATH_FIELDS = (
    *MATH_HEADER_FIELDS,
    "corpus",
)


def decode_math(data: list[int]) -> dict[str, int]:
    if len(data) < MATH_BYTES or not all(isinstance(byte, int) and 0 <= byte <= 255 for byte in data):
        raise ValueError(f"SMC1 probe returned fewer than {MATH_BYTES} valid bytes")
    words = struct.unpack(f">{MATH_BYTES // 4}I", bytes(data[:MATH_BYTES]))
    decoded = dict(zip(MATH_HEADER_FIELDS, words[:len(MATH_HEADER_FIELDS)], strict=True))
    cursor = len(MATH_HEADER_FIELDS)
    corpus: list[dict[str, int | str]] = []
    for function, count_field in (("atan2s", "atan2s_samples"), ("atan2_lookup", "atan2_lookup_samples")):
        count = decoded[count_field]
        if count > MATH_CORPUS_SLOTS:
            raise ValueError(f"SMC1 {count_field} exceeds its {MATH_CORPUS_SLOTS}-sample bound")
        samples = words[cursor:cursor + MATH_CORPUS_SLOTS * 3]
        cursor += MATH_CORPUS_SLOTS * 3
        for offset in range(count):
            y_bits, x_bits, result = samples[offset * 3:(offset + 1) * 3]
            corpus.append({"function": function, "y_bits": y_bits, "x_bits": x_bits, "result": result})
    decoded["corpus"] = corpus
    return decoded


def run_boot_macro(client: YmirClient) -> int:
    frames = 0

    def run_for(count: int) -> None:
        nonlocal frames
        client.call("exec.run_for", {"frames": count})
        frames += count

    run_for(120)
    client.call("input.pulse", {"buttons": 0x4000})
    run_for(30)
    client.call("input.pulse", {"buttons": 0x0400})
    run_for(1200)
    for _ in range(5):
        client.call("input.pulse", {"buttons": 0x4000})
        run_for(30)
    client.call("input.pulse", {"buttons": 0xFFF8})
    return frames


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ymir", type=Path, required=True)
    parser.add_argument("--ipl", type=Path, required=True)
    parser.add_argument("--game", type=Path, required=True)
    parser.add_argument("--route-address", type=lambda value: int(value, 0), required=True)
    parser.add_argument("--math-address", type=lambda value: int(value, 0), required=True)
    parser.add_argument("--expected-ticks", type=int, default=2000)
    parser.add_argument("--timeout", type=float, default=1800.0)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.expected_ticks <= 0 or args.timeout <= 0:
        parser.error("--expected-ticks and --timeout must be positive")
    for name in ("ymir", "ipl", "game", "output"):
        setattr(args, name, getattr(args, name).resolve())
    stale = stale_game_image(args.game)
    if stale is not None:
        elf, game_mtime, elf_mtime = stale
        parser.error(f"game image is older than its build ELF ({game_mtime:.3f} < {elf_mtime:.3f})")

    started = time.perf_counter()
    client: YmirClient | None = None
    frames = 0
    try:
        client = YmirClient(args.ymir, args.ipl, args.game, args.timeout)
        frames = run_boot_macro(client)
        route: dict[str, Any] | None = None
        route_data: list[int] = []
        while route is None or int(route["replay_ticks"]) < args.expected_ticks:
            client.call("exec.run_for", {"frames": 30})
            frames += 30
            route, route_data = peek_route(client, args.route_address)
        if int(route["replay_ticks"]) != args.expected_ticks:
            raise RuntimeError(f"route stopped at tick {route['replay_ticks']}, expected {args.expected_ticks}")
        math_window = client.call("mem.peek", {"address": args.math_address, "count": MATH_BYTES})
        math_data = math_window.get("data", [])
        math = decode_math(math_data)
        if math["magic"] != MATH_MAGIC or math["version"] != MATH_VERSION:
            raise RuntimeError("SMC1 magic/version is absent from the fresh symbol address")
        if math["replay_ticks"] != args.expected_ticks:
            raise RuntimeError("SMC1 replay tick does not match the source route checkpoint")
        for field in ("atan2s_calls", "atan2_lookup_calls"):
            if math[field] == 0:
                raise RuntimeError(f"SMC1 {field} is zero on the frozen replay")
        client.shutdown()
    except BaseException:
        if client is not None:
            client.abort()
        raise

    elf = newest_sibling_elf(args.game)
    report = {
        "evidence_kind": "ymir-sourceboot-smc1-math-route",
        "schema_version": MATH_VERSION,
        "ymir": str(args.ymir),
        "ipl": str(args.ipl),
        "game": str(args.game),
        "artifacts": {"game": artifact_identity(args.game), "elf": artifact_identity(elf)},
        "dram_cart": True,
        "bios_input": True,
        "emulated_frames": frames,
        "emulation_timing": emulation_timing(frames, time.perf_counter() - started),
        "route_window": {"address": args.route_address, "data": route_data, "decoded": route},
        "math_window": {"address": args.math_address, "data": math_data[:MATH_BYTES], "decoded": math},
        "corpus_contract": {
            "capacity_per_function": MATH_CORPUS_SLOTS,
            "entry_encoding": "function, IEEE-754 binary32 y bits, IEEE-754 binary32 x bits, u16 angle result",
            "sampling": "bounded 64-entry ring per function; slots are overwritten in call order",
        },
        "protocol": {
            "ready": any(message.get("method") == "instance.ready" for message in client.notifications),
            "stopped_reasons": [message.get("params", {}).get("reason") for message in client.notifications if message.get("method") == "instance.stopped"],
        },
        "diagnostics": {"stderr": client.stderr},
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
