#!/usr/bin/env python3
"""Measure gObjectPool occupancy on the canonical flags-on geo-walk config.

Task 2 of the memory-residency campaign (docs/superpowers/plans/
2026-08-09-memory-residency-campaign.md): sample
``g_sm64_saturn_object_pool_probe`` (src/port/saturn/runtime/
saturn_object_pool_probe.h) through headless Ymir every
``--sample-interval`` emulated frames out to at least
``--post-bios-frames`` post-BIOS-handoff frames, and record the real
observed ``current_allocated`` / ``peak_allocated`` / ``alloc_failures``
time series so the owner's Task 3 capacity gate is decided from measured
data, not an estimate.

This runs on the standard BOB route as the canonical geo-walk build boots
it: BIOS handoff into the level's default spawn point. The canonical
config built for this measurement (SATURN_FEATURE_COMPLETE_MARIO_ANIMATION=1
SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=1 SATURN_FEATURE_SEMANTIC_AUDIO=0
SATURN_RENDERER_PIPELINE=4 SATURN_DIAGNOSTIC_MODE=0) does not enable
SATURN_SOURCEBOOT_LIVE_INPUT or SATURN_SOURCEBOOT_ROUTE_REPLAY, so Mario
never leaves the spawn point during this capture -- report honestly
whether the sampled window actually reached any macro-object-dense area,
it almost certainly did not.

Pattern-copied from capture_sourceboot_boot_trace.py (BIOS handoff macro,
sh-elf-nm symbol resolution through the DLL-safe MSYS wrapper, artifact
identity binding, protocol/diagnostics evidence shape) and
capture_route_views.py's YmirClient transport.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import time
from pathlib import Path
from typing import Any

from capture_hwtest import artifact_identity
from capture_route_views import YmirClient
from capture_sourceboot_boot_trace import (
    NM,
    bind_capture_artifacts,
    cpu_cache_through_alias,
    protocol_and_diagnostics,
    resolve_probe_symbol,
    run_bios_handoff,
    wrapped_nm_command,
)

ROOT = Path(__file__).resolve().parents[2]

PROBE_SYMBOL = "g_sm64_saturn_object_pool_probe"
PROBE_MAGIC = 0x4F504F4C
PROBE_WORD_COUNT = 5
PROBE_BYTES = PROBE_WORD_COUNT * 4

# Session environment rule: chunk every exec.run_for call at <=600 frames.
# 300 keeps every chunk boundary aligned with the plan's own 300-frame
# sample cadence regardless of the caller's --sample-interval.
RUN_FOR_CHUNK_FRAMES = 300
DEFAULT_SAMPLE_INTERVAL_FRAMES = 300
# 300 * 67 = 20,100 -- an exact multiple of the default sample interval
# that clears the plan's >=20,000-frame measurement floor.
DEFAULT_POST_BIOS_FRAMES = 20100
MIN_POST_BIOS_FRAMES = 20000

POOL_CAPACITY_HEADER = ROOT / "src/game/object_list_processor.h"
POOL_CAPACITY_RE = re.compile(r"#define\s+OBJECT_POOL_CAPACITY\s+(\d+)")


def read_pool_capacity() -> int:
    """Read the real compiled-in capacity rather than hardcoding a copy
    that could silently drift from src/game/object_list_processor.h."""
    source = POOL_CAPACITY_HEADER.read_text(encoding="utf-8")
    match = POOL_CAPACITY_RE.search(source)
    if match is None:
        raise ValueError(f"OBJECT_POOL_CAPACITY not found in {POOL_CAPACITY_HEADER}")
    return int(match.group(1))


def resolve_probe_address(elf: Path, *, nm: Path = NM, run: Any = subprocess.run) -> int:
    completed = run(
        wrapped_nm_command(elf, nm=nm), check=False, capture_output=True, text=True
    )
    if completed.returncode != 0:
        raise ValueError(
            f"DLL-safe sh-elf-nm wrapper failed for {elf}: {completed.stderr.strip()}"
        )
    return resolve_probe_symbol(completed.stdout, PROBE_SYMBOL)


def decode_probe(data: list[int]) -> dict[str, Any]:
    """Decode a raw probe read leniently -- never raises.

    Like capture_sourceboot_boot_trace.py's capture_trace_checkpoint(), early
    checkpoints (before the BIOS has actually finished the CD-boot sequence
    and loaded the ELF's .data image into RAM) legitimately read back
    whatever was in that RAM before the game's own .data copy landed there
    -- zero, or leftover BIOS/loader garbage. That is not a capture failure;
    it just means the sample predates the probe's real value, so it is
    reported as ``magic_valid: False`` with the raw words preserved for
    evidence rather than treated as an error.
    """
    if len(data) != PROBE_BYTES:
        raise ValueError(
            f"object-pool probe read returned {len(data)} bytes, expected {PROBE_BYTES}"
        )
    words = [
        int.from_bytes(bytes(data[index:index + 4]), byteorder="big")
        for index in range(0, PROBE_BYTES, 4)
    ]
    magic_valid = words[0] == PROBE_MAGIC
    return {
        "magic": words[0],
        "magic_valid": magic_valid,
        "current_allocated": words[1] if magic_valid else None,
        "peak_allocated": words[2] if magic_valid else None,
        "alloc_failures": words[3] if magic_valid else None,
        "frames_sampled": words[4] if magic_valid else None,
        "raw_words": words,
    }


def read_probe(client: YmirClient, address: int) -> dict[str, Any]:
    result = client.call("mem.peek", {"address": address, "count": PROBE_BYTES})
    data = result.get("data")
    if not isinstance(data, list):
        raise ValueError("Ymir object-pool probe read has no byte data")
    return decode_probe(data)


def validate_post_bios_frames(frames: int) -> int:
    if frames < MIN_POST_BIOS_FRAMES:
        raise ValueError(
            f"--post-bios-frames must be >= {MIN_POST_BIOS_FRAMES} "
            "(Task 2's measurement floor)"
        )
    return frames


def validate_sample_interval(interval: int) -> int:
    if interval <= 0 or interval > 600:
        raise ValueError(
            "--sample-interval must be a positive value <= 600 "
            "(exec.run_for chunk cap)"
        )
    return interval


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ymir", type=Path, required=True, help="ymir-headless executable")
    parser.add_argument("--ipl", type=Path, required=True, help="Saturn BIOS image")
    parser.add_argument("--game", type=Path, required=True, help="built sourceboot .cue path")
    parser.add_argument("--elf", type=Path, required=True, help="matching sourceboot ELF")
    parser.add_argument("--output", type=Path, required=True, help="JSON evidence report")
    parser.add_argument(
        "--post-bios-frames", type=int, default=DEFAULT_POST_BIOS_FRAMES,
        help=f"bounded frames to sample after BIOS handoff (default: {DEFAULT_POST_BIOS_FRAMES})",
    )
    parser.add_argument(
        "--sample-interval", type=int, default=DEFAULT_SAMPLE_INTERVAL_FRAMES,
        help=f"emulated-frame interval between probe samples (default: {DEFAULT_SAMPLE_INTERVAL_FRAMES})",
    )
    parser.add_argument("--timeout", type=float, default=1800.0)
    args = parser.parse_args()

    for label, path in (("Ymir", args.ymir), ("IPL", args.ipl), ("game", args.game), ("ELF", args.elf)):
        if not path.is_file():
            parser.error(f"{label} is not a file: {path}")
    try:
        args.post_bios_frames = validate_post_bios_frames(args.post_bios_frames)
        args.sample_interval = validate_sample_interval(args.sample_interval)
    except ValueError as error:
        parser.error(str(error))
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.post_bios_frames % args.sample_interval != 0:
        parser.error("--post-bios-frames must be an exact multiple of --sample-interval")

    args.ymir = args.ymir.resolve()
    args.ipl = args.ipl.resolve()
    args.game = args.game.resolve()
    args.elf = args.elf.resolve()
    args.output = args.output.resolve()
    try:
        artifacts = bind_capture_artifacts(args.game, args.elf)
    except (OSError, ValueError) as error:
        parser.error(str(error))

    pool_capacity = read_pool_capacity()
    probe_address = resolve_probe_address(args.elf)

    wall_start = time.perf_counter()
    emulated_frames = 0
    client: YmirClient | None = None
    samples: list[dict[str, Any]] = []
    final_probe: dict[str, Any] | None = None
    failure: BaseException | None = None
    try:
        client = YmirClient(args.ymir, args.ipl, args.game, args.timeout)

        def run_for(frames: int) -> None:
            nonlocal emulated_frames
            remaining = frames
            while remaining > 0:
                chunk = min(remaining, RUN_FOR_CHUNK_FRAMES)
                client.call("exec.run_for", {"frames": chunk})
                emulated_frames += chunk
                remaining -= chunk

        def sample(label: str) -> None:
            probe = read_probe(client, probe_address)
            samples.append({"label": label, "emulated_frames": emulated_frames, **probe})

        run_bios_handoff(client, run_for, sample)

        elapsed = 0
        while elapsed < args.post_bios_frames:
            run_for(args.sample_interval)
            elapsed += args.sample_interval
            sample(f"post-bios-{elapsed}")

        final_probe = read_probe(client, probe_address)
        client.shutdown()
    except BaseException as error:
        failure = error
        if client is not None:
            client.abort()

    post_bios_samples = [s for s in samples if s["label"].startswith("post-bios-")]
    valid_samples = [s for s in samples if s["magic_valid"]]
    valid_post_bios_samples = [s for s in post_bios_samples if s["magic_valid"]]
    first_valid_sample = valid_samples[0] if valid_samples else None
    peak_allocated_observed = max(
        (s["peak_allocated"] for s in valid_samples), default=None
    )
    alloc_failures_observed = max(
        (s["alloc_failures"] for s in valid_samples), default=None
    )
    current_allocated_min = min(
        (s["current_allocated"] for s in valid_post_bios_samples), default=None
    )
    current_allocated_max = max(
        (s["current_allocated"] for s in valid_post_bios_samples), default=None
    )

    report: dict[str, Any] = {
        "evidence_kind": "ymir-object-pool-occupancy-capture",
        "diagnostic_only": True,
        "manual_gui_launch": False,
        "target_build": False,
        "performance_measurement": False,
        "route_note": (
            "canonical geo-walk config: no SATURN_SOURCEBOOT_LIVE_INPUT / "
            "SATURN_SOURCEBOOT_ROUTE_REPLAY -- Mario remains at BOB's "
            "default spawn point for the whole capture window; this does "
            "not exercise macro-object-dense areas of the level."
        ),
        "ymir": str(args.ymir),
        "ipl": str(args.ipl),
        "game": artifact_identity(args.game),
        "elf": artifact_identity(args.elf),
        "artifacts": artifacts,
        "probe_symbol": PROBE_SYMBOL,
        "probe_address": probe_address,
        "probe_cache_through_address": cpu_cache_through_alias(probe_address),
        "pool_capacity": pool_capacity,
        "sample_interval_frames": args.sample_interval,
        "requested_post_bios_frames": args.post_bios_frames,
        "emulated_frames": emulated_frames,
        "post_bios_sample_count": len(post_bios_samples),
        "valid_sample_count": len(valid_samples),
        "first_valid_sample": first_valid_sample,
        "samples": samples,
        "final_probe": final_probe,
        "peak_allocated_observed": peak_allocated_observed,
        "alloc_failures_observed": alloc_failures_observed,
        "current_allocated_min_post_bios": current_allocated_min,
        "current_allocated_max_post_bios": current_allocated_max,
        "wall_seconds": time.perf_counter() - wall_start,
    }
    if client is not None:
        report.update(protocol_and_diagnostics(client))
    if failure is not None:
        report["failure"] = {"message": str(failure), "type": type(failure).__name__}

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 1 if failure is not None else 0


if __name__ == "__main__":
    raise SystemExit(main())
