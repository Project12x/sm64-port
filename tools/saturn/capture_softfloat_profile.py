#!/usr/bin/env python3
"""Burst-sample the running sourceboot build and attribute SH-2 cycles to symbols.

The whole-image arithmetic census (`sprint2-arithmetic-census.md`) is a *static*
reachability count: 1,827 hot-reachable soft-float call sites is a lower bound on
what is linked, and says nothing at all about what executes per frame.  This tool
answers the dynamic half of that question without building anything.

Mechanism.  `ymir-headless` exposes `exec.stepi`, which returns `pc_before`,
`pc_after` and `cycles_advanced` for a single executed instruction.  That is a
complete cycle-attributed sample: the instruction at `pc_before` cost
`cycles_advanced` cycles.  Accumulating those over many short bursts, spread
across many VBlanks, yields a statistical cycle profile of the real route on the
real build.  `pc_after == symbol_start` while `pc_before` lies outside that
symbol additionally witnesses a *call into* the symbol, so helper invocation
counts fall out of the same sweep.

Sampling is deliberately bursty rather than uniform: one RPC per instruction is
far too slow to cover a whole frame, so the tool advances `--gap-vblanks` at full
speed, then single-steps `--burst-steps` instructions, and repeats.  Samples
inside one burst are serially correlated; samples across bursts are not.  The
report states the burst geometry so the correlation is visible rather than
implied, and both CPUs are sampled because `slave_render=1` puts real render work
on the slave.

Stepping does not perturb emulated time.  Ymir's `StepMasterSH2`/`StepSlaveSH2`
advance the same deterministic machine the free-running loop advances; only host
wall-clock differs.  The profile is therefore of the shipped build's real
execution, not of an instrumented variant, and no rebuild is required.
"""

from __future__ import annotations

import argparse
import bisect
import hashlib
import json
import re
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))

from capture_route_views import YmirClient  # noqa: E402
from capture_sourceboot_boot_trace import run_bios_handoff  # noqa: E402
import route_warmup  # noqa: E402
from capture_sourceboot_throughput import (  # noqa: E402
    build_elf_identity_probe,
    wait_for_target_identity,
)

# NTSC 320-pixel mode SH-2 clock, from ymir-core/include/ymir/sys/clocks.hpp:27.
# Only used to convert measured cycle shares into per-frame figures; the raw
# shares in the report do not depend on it.
DEFAULT_SH2_CLOCK_HZ = 26_846_590.91

# Helper classes, applied to the *demangled-free* SH ELF symbol name, which
# carries a leading underscore ("___mulsf3" is C's "__mulsf3").
HELPER_CLASSES: tuple[tuple[str, re.Pattern[str]], ...] = (
    ("soft-double", re.compile(r"^___(?:add|sub|mul|div|neg|eq|ne|ge|le|gt|lt|unord)df[23]$")),
    ("soft-double-convert", re.compile(r"^___(?:fix|float)[a-z]*df[a-z]*$|^___extendsfdf2$|^___truncdfsf2$")),
    ("soft-float", re.compile(r"^___(?:add|sub|mul|div|neg|eq|ne|ge|le|gt|lt|unord)sf[23]$")),
    ("soft-float-convert", re.compile(r"^___(?:fix|float)[a-z]*sf[a-z]*$")),
    ("soft-float-support", re.compile(r"^___(?:clzsi2|powisf2)$")),
    ("divide", re.compile(r"^___(?:u?div|u?mod)(?:si3|di3)$|^___udiv_qrnnd_16$")),
    ("shift", re.compile(r"^___(?:ashl|ashr|lshr)si3(?:_r0)?$|^___(?:ashl|ashr|lshr)di3$")),
    ("libm", re.compile(r"^_(?:sinf|cosf|sqrtf|atan2f|asinf|sinsf|powf)$")),
    # Not arithmetic at all, but they dominate the raw cycle count and would
    # silently deflate every share below if they were left in "application".
    ("idle", re.compile(r"^(?:___slave_polling_entry|_sm64_saturn_source_runtime_wait_vblank)$")),
    ("cd-io", re.compile(r"^_cd_block_")),
)

# Classes whose cycles are float arithmetic the port could in principle delete.
FLOAT_CLASSES = (
    "soft-double",
    "soft-double-convert",
    "soft-float",
    "soft-float-convert",
    "soft-float-support",
    "libm",
)


def classify(symbol: str) -> str | None:
    for name, pattern in HELPER_CLASSES:
        if pattern.match(symbol):
            return name
    return None


def load_symbols(nm: Path, elf: Path) -> list[tuple[int, int, str]]:
    """Return (address, size, name) sorted by address, sizes filled by gap."""
    raw = subprocess.run(
        [str(nm), "-S", "--numeric-sort", "--defined-only", str(elf)],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    entries: list[tuple[int, int, str]] = []
    for line in raw.splitlines():
        parts = line.split()
        if len(parts) == 4:
            address, size, kind, name = parts
        elif len(parts) == 3:
            address, kind, name = parts
            size = "0"
        else:
            continue
        if kind.upper() not in {"T", "W"}:
            continue
        try:
            entries.append((int(address, 16), int(size, 16), name))
        except ValueError:
            continue
    entries.sort(key=lambda item: (item[0], -item[1]))
    # Collapse duplicate addresses, keep the largest-size definition.
    collapsed: list[tuple[int, int, str]] = []
    for entry in entries:
        if collapsed and collapsed[-1][0] == entry[0]:
            continue
        collapsed.append(entry)
    # A zero-size symbol owns everything up to the next symbol.
    filled: list[tuple[int, int, str]] = []
    for index, (address, size, name) in enumerate(collapsed):
        if size == 0 and index + 1 < len(collapsed):
            size = collapsed[index + 1][0] - address
        filled.append((address, size, name))
    if not filled:
        raise ValueError(f"{nm} reported no text symbols for {elf}")
    return filled


class SymbolIndex:
    def __init__(self, symbols: list[tuple[int, int, str]]) -> None:
        self._symbols = symbols
        self._starts = [entry[0] for entry in symbols]
        self.by_name = {entry[2]: entry for entry in symbols}

    def lookup(self, pc: int) -> tuple[str, int] | None:
        position = bisect.bisect_right(self._starts, pc) - 1
        if position < 0:
            return None
        address, size, name = self._symbols[position]
        if size and pc >= address + size:
            return None
        return name, address


def profile(
    client: YmirClient,
    index: SymbolIndex,
    *,
    bursts: int,
    burst_steps: int,
    gap_vblanks: int,
    targets: tuple[str, ...],
) -> dict[str, Any]:
    cycles: dict[str, int] = {}
    samples: dict[str, int] = {}
    entries: dict[str, int] = {}
    callers: dict[str, dict[str, int]] = {}
    unmapped_cycles = 0
    unmapped_samples = 0
    total_cycles = 0
    total_samples = 0
    per_target: dict[str, dict[str, int]] = {
        target: {"cycles": 0, "samples": 0} for target in targets
    }
    started = time.monotonic()

    for burst in range(bursts):
        client.call("exec.run_for", {"frames": gap_vblanks})
        target = targets[burst % len(targets)]
        for _ in range(burst_steps):
            step = client.call("exec.stepi", {"target": target})
            pc_before = int(step["pc_before"])
            pc_after = int(step["pc_after"])
            cost = int(step["cycles_advanced"])
            total_cycles += cost
            total_samples += 1
            per_target[target]["cycles"] += cost
            per_target[target]["samples"] += 1
            hit = index.lookup(pc_before)
            if hit is None:
                unmapped_cycles += cost
                unmapped_samples += 1
            else:
                name = hit[0]
                cycles[name] = cycles.get(name, 0) + cost
                samples[name] = samples.get(name, 0) + 1
            landing = index.lookup(pc_after)
            if landing is not None and pc_after == landing[1]:
                if hit is None or hit[0] != landing[0]:
                    entries[landing[0]] = entries.get(landing[0], 0) + 1
                    caller = hit[0] if hit is not None else "<unmapped>"
                    bucket = callers.setdefault(landing[0], {})
                    bucket[caller] = bucket.get(caller, 0) + 1

    return {
        "wall_seconds": round(time.monotonic() - started, 2),
        "total_cycles": total_cycles,
        "total_samples": total_samples,
        "unmapped_cycles": unmapped_cycles,
        "unmapped_samples": unmapped_samples,
        "per_target": per_target,
        "cycles": cycles,
        "samples": samples,
        "entries": entries,
        "callers": callers,
    }


def merge(results: list[dict[str, Any]]) -> dict[str, Any]:
    if len(results) == 1:
        return results[0]
    merged: dict[str, Any] = {
        "wall_seconds": round(sum(item["wall_seconds"] for item in results), 2),
        "total_cycles": sum(item["total_cycles"] for item in results),
        "total_samples": sum(item["total_samples"] for item in results),
        "unmapped_cycles": sum(item["unmapped_cycles"] for item in results),
        "unmapped_samples": sum(item["unmapped_samples"] for item in results),
        "per_target": {},
        "cycles": {},
        "samples": {},
        "entries": {},
        "callers": {},
    }
    for item in results:
        for target, bucket in item["per_target"].items():
            slot = merged["per_target"].setdefault(target, {"cycles": 0, "samples": 0})
            slot["cycles"] += bucket["cycles"]
            slot["samples"] += bucket["samples"]
        for key in ("cycles", "samples", "entries"):
            for name, value in item[key].items():
                merged[key][name] = merged[key].get(name, 0) + value
        for callee, bucket in item["callers"].items():
            slot = merged["callers"].setdefault(callee, {})
            for caller, value in bucket.items():
                slot[caller] = slot.get(caller, 0) + value
    return merged


def summarise(result: dict[str, Any], clock_hz: float, vblanks_per_frame: float) -> dict[str, Any]:
    total = result["total_cycles"]
    cycles_per_frame = clock_hz / 60.0 * vblanks_per_frame
    rows = []
    for name, value in sorted(result["cycles"].items(), key=lambda item: -item[1]):
        share = value / total if total else 0.0
        rows.append(
            {
                "symbol": name,
                "class": classify(name),
                "cycles": value,
                "samples": result["samples"].get(name, 0),
                "cycle_share": round(share, 8),
                "est_cycles_per_frame": round(share * cycles_per_frame, 1),
                "entries_sampled": result["entries"].get(name, 0),
                "est_calls_per_frame": (
                    round(result["entries"].get(name, 0) / total * cycles_per_frame, 1)
                    if total
                    else 0.0
                ),
            }
        )
    classes: dict[str, dict[str, float]] = {}
    for row in rows:
        key = row["class"] or "application"
        bucket = classes.setdefault(key, {"cycles": 0, "entries_sampled": 0})
        bucket["cycles"] += row["cycles"]
        bucket["entries_sampled"] += row["entries_sampled"]
    for key, bucket in classes.items():
        bucket["cycle_share"] = round(bucket["cycles"] / total, 8) if total else 0.0
        bucket["est_calls_per_frame"] = (
            round(bucket["entries_sampled"] / total * cycles_per_frame, 1) if total else 0.0
        )
    idle_cycles = sum(
        row["cycles"] for row in rows if row["class"] in {"idle", "cd-io"}
    )
    working = total - idle_cycles
    float_cycles = sum(row["cycles"] for row in rows if row["class"] in FLOAT_CLASSES)
    for row in rows:
        row["working_share"] = round(row["cycles"] / working, 8) if working else 0.0
    return {
        "clock_hz": clock_hz,
        "idle_cycles": idle_cycles,
        "working_cycles": working,
        "float_cycles": float_cycles,
        "float_share_of_total": round(float_cycles / total, 6) if total else 0.0,
        "float_share_of_working": round(float_cycles / working, 6) if working else 0.0,
        "callers": {
            callee: dict(sorted(bucket.items(), key=lambda item: -item[1])[:8])
            for callee, bucket in sorted(
                result["callers"].items(),
                key=lambda item: -sum(item[1].values()),
            )
            if classify(callee) is not None
        },
        "vblanks_per_frame": vblanks_per_frame,
        "cycles_per_frame": round(cycles_per_frame, 1),
        "classes": classes,
        "symbols": rows,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ymir", type=Path, required=True)
    parser.add_argument("--ipl", type=Path, required=True)
    parser.add_argument("--game", type=Path, required=True, help="exact matching CUE")
    parser.add_argument("--elf", type=Path, required=True, help="exact matching ELF")
    parser.add_argument("--nm", type=Path, required=True, help="sh-elf-nm")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--startup-vblanks", type=int, default=4096)
    # T2.19d: the ELF is resident well before the gameplay stage renders, so
    # sampling immediately after the identity match profiles the CD loader
    # instead of the route.  The warm-up is now counted in route ticks, so
    # builds of different speed are profiled at the same route position.
    route_warmup.add_warmup_arguments(parser)
    parser.add_argument(
        "--phases",
        type=int,
        default=1,
        help=(
            "repeat (advance --phase-vblanks, then sample) this many times and report each "
            "phase separately; the boot stage and the gameplay route look nothing alike"
        ),
    )
    parser.add_argument("--phase-vblanks", type=int, default=0)
    parser.add_argument("--bursts", type=int, default=200)
    parser.add_argument("--burst-steps", type=int, default=250)
    parser.add_argument("--gap-vblanks", type=int, default=1)
    parser.add_argument("--targets", default="sh2.master,sh2.slave")
    parser.add_argument("--clock-hz", type=float, default=DEFAULT_SH2_CLOCK_HZ)
    parser.add_argument("--vblanks-per-frame", type=float, default=12.1379)
    parser.add_argument("--timeout", type=float, default=2400.0)
    parser.add_argument("--top", type=int, default=60)
    args = parser.parse_args(argv)

    symbols = load_symbols(args.nm, args.elf)
    index = SymbolIndex(symbols)
    identity_probe = build_elf_identity_probe(args.elf)
    warmup_plan = route_warmup.plan_warmup(args, args.elf)
    if args.phases <= 1 and args.phase_vblanks > 0:
        # `--phases 1 --phase-vblanks N` is a wall-clock warm-up wearing a
        # different name, and it carries the same cross-build defect T2.19d
        # removed from --warmup-vblanks.  T2.13 and T2.14 both profiled this
        # way.  The multi-phase use of the flag is legitimate; this single-
        # phase use is not.
        print(
            "warning: --phase-vblanks with --phases 1 is a wall-clock warm-up. "
            "Two builds of different speed will be profiled at different route "
            "positions. Use --warmup-ticks instead.",
            file=sys.stderr,
        )
    targets = tuple(part.strip() for part in args.targets.split(",") if part.strip())

    client = YmirClient(args.ymir, args.ipl, args.game, args.timeout)
    try:
        run_bios_handoff(client, lambda frames: client.call("exec.run_for", {"frames": frames}), lambda _label: None)
        identity = wait_for_target_identity(
            client, identity_probe, startup_vblanks=args.startup_vblanks
        )
        warmup = route_warmup.execute_warmup(client, warmup_plan)
        print(
            f"  warm-up: {warmup['mode']} -> replay tick "
            f"{warmup.get('replay_ticks')} (simulation tick "
            f"{warmup.get('global_timer')}) after "
            f"{warmup['vblanks_advanced']} VBlanks"
        )
        phase_results = []
        for _phase in range(max(1, args.phases)):
            advance = args.phase_vblanks
            while advance > 0:
                chunk = min(advance, 3600)
                client.call("exec.run_for", {"frames": chunk})
                advance -= chunk
            phase_results.append(
                profile(
                    client,
                    index,
                    bursts=args.bursts,
                    burst_steps=args.burst_steps,
                    gap_vblanks=args.gap_vblanks,
                    targets=targets,
                )
            )
        route_position_after_profile = (
            route_warmup.read_route_position(client, warmup_plan["addresses"])
            if warmup_plan["addresses"] is not None
            else None
        )
        result = merge(phase_results)
    finally:
        try:
            client.shutdown()
        except Exception:  # noqa: BLE001 - shutdown is best-effort
            client.abort()

    summary = summarise(result, args.clock_hz, args.vblanks_per_frame)
    report = {
        "schema": "sm64-saturn-softfloat-profile-v1",
        "elf": str(args.elf),
        "elf_sha256": hashlib.sha256(args.elf.read_bytes()).hexdigest(),
        "game": str(args.game),
        "identity": identity,
        "sampling": {
            "bursts": args.bursts,
            "burst_steps": args.burst_steps,
            "gap_vblanks": args.gap_vblanks,
            "targets": list(targets),
            "startup_vblanks": args.startup_vblanks,
            "warmup": warmup,
            "route_position_after_profile": route_position_after_profile,
            "phases": args.phases,
            "phase_vblanks": args.phase_vblanks,
        },
        "raw": {
            key: result[key]
            for key in (
                "wall_seconds",
                "total_cycles",
                "total_samples",
                "unmapped_cycles",
                "unmapped_samples",
                "per_target",
            )
        },
        "summary": summary,
        "phase_summaries": [
            summarise(item, args.clock_hz, args.vblanks_per_frame)["classes"]
            for item in phase_results
        ],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    for number, item in enumerate(phase_results):
        classes = summarise(item, args.clock_hz, args.vblanks_per_frame)["classes"]
        head = sorted(classes.items(), key=lambda entry: -entry[1]["cycles"])[:4]
        print(
            f"  phase {number}: "
            + ", ".join(f"{key} {bucket['cycle_share'] * 100:.2f}%" for key, bucket in head)
        )
    print(f"softfloat profile: {result['total_samples']} samples, {result['total_cycles']} cycles, {result['wall_seconds']}s")
    for key, bucket in sorted(summary["classes"].items(), key=lambda item: -item[1]["cycles"]):
        print(f"  class {key:22s} {bucket['cycle_share'] * 100:6.2f}%  calls/frame~{bucket['est_calls_per_frame']}")
    print(
        f"  float = {summary['float_share_of_total'] * 100:.3f}% of sampled cycles, "
        f"{summary['float_share_of_working'] * 100:.3f}% of non-idle cycles "
        f"(idle {summary['idle_cycles']} / {result['total_cycles']})"
    )
    print("  top symbols:")
    for row in summary["symbols"][: args.top]:
        print(
            f"    {row['cycle_share'] * 100:6.3f}%  {row['symbol']:48s}"
            f" cyc/frame~{row['est_cycles_per_frame']:>10}  calls/frame~{row['est_calls_per_frame']}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
