#!/usr/bin/env python3
"""Attribute every idle SH-2 cycle to a wait site and a cause, on the shipped build.

T2.13's `capture_softfloat_profile.py` answered *what is executing*: 70-73% of
sampled SH-2 cycles sit in two symbols, `___slave_polling_entry` (the slave) and
`_sm64_saturn_source_runtime_wait_vblank` (the master).  It could not answer the
question that matters next -- *what is each processor waiting on* -- for three
reasons, all of which this tool fixes:

1.  **It sampled the two CPUs in alternating bursts**, so it never observed the
    pair (master PC, slave PC) at one emulated instant.  "Is the slave idle
    because the master is computing, or because the master is idle too?" is a
    joint question and needs a joint sample.  `Saturn::StepMasterSH2()` advances
    the slave by exactly the master's cycles, so pausing after a master step and
    reading both register files is one coherent snapshot.

2.  **It attributed by PC only.**  `_sm64_saturn_source_runtime_wait_vblank` is a
    leaf (both `vdp2_tvmd_vblank_in_wait` and `..._out_wait` are `__always_inline`
    in libyaul), so PR still holds the caller's return address for the whole
    duration of the spin.  Reading PR while inside it attributes the wait to its
    exact call site with a 100% hit rate -- unlike call-edge sampling, which
    T2.14 section 7.1 measured as far too sparse to attribute anything.

3.  **Burst sampling after `exec.run_for` is raster-phase-locked.**  `run_for`
    stops at an emulator frame boundary, so every burst starts at the same point
    in the field.  Both idle sites here are *field-locked* -- the master's spin
    ends at VBLANK-OUT by construction -- so a phase-locked sampler can bias the
    idle share in either direction.  This tool therefore traces **contiguously**:
    within one window there is no gap, no phase selection, and the residency is
    exact rather than estimated.  Several windows at different frame phases give
    coverage; the per-window spread is reported so the reader can see it.

What a window produces:

*   exact per-CPU cycle residency by symbol (master from `exec.stepi`'s own
    `cycles_advanced`; slave strided, weighted by the same cycles);
*   the **run-length structure** of every symbol -- how many times the master
    entered its VBlank spin, and how long each excursion lasted -- which is what
    turns "60% of the master is idle" into "N waits per frame of D cycles each";
*   **wait-site attribution** from PR sampled at each entry to a wait symbol;
*   the **joint contingency table** master-state x slave-state, whose
    both-idle cell is the pure scheduler stall; and
*   a strided VDP1/VDP2 register witness (EDSR/LOPR/COPR, TVSTAT) so the
    "is VDP1 starved?" question is answered on the same build in the same run.

Nothing is rebuilt and nothing is instrumented: this reads the shipped product
ELF exactly as `capture_softfloat_profile.py` does.
"""

from __future__ import annotations

import argparse
import bisect
import hashlib
import json
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))

from capture_route_views import YmirClient  # noqa: E402
from capture_softfloat_profile import SymbolIndex, load_symbols  # noqa: E402
from capture_sourceboot_boot_trace import run_bios_handoff  # noqa: E402
import route_warmup  # noqa: E402
from capture_sourceboot_throughput import (  # noqa: E402
    build_elf_identity_probe,
    wait_for_target_identity,
)

# NTSC 320-pixel mode SH-2 clock, ymir-core/include/ymir/sys/clocks.hpp:27.
DEFAULT_SH2_CLOCK_HZ = 26_846_590.91

# The two symbols T2.13 identified as idle, plus the fork-join barrier the
# reference study named.  Everything else is "work" by definition here; the
# classification is by symbol and is stated in the report, not hidden.
MASTER_IDLE_SYMBOLS = frozenset({"_sm64_saturn_source_runtime_wait_vblank"})
SLAVE_IDLE_SYMBOLS = frozenset({"___slave_polling_entry"})
BARRIER_SYMBOLS = frozenset({"_sm64_saturn_dual_worker_run"})

VDP1_EDSR = 0x25D00010  # EDSR, LOPR, COPR are three consecutive 16-bit regs
VDP2_TVSTAT = 0x25F80004


def state_class(symbol: str | None, cpu: str) -> str:
    if symbol is None:
        return "unmapped"
    if cpu == "master" and symbol in MASTER_IDLE_SYMBOLS:
        return "idle:vblank-spin"
    if cpu == "slave" and symbol in SLAVE_IDLE_SYMBOLS:
        return "idle:notification-wait"
    if symbol in BARRIER_SYMBOLS:
        return "idle:fork-join-barrier"
    return "work"


class RunTracker:
    """Accumulate contiguous residency runs for one CPU's symbol stream."""

    def __init__(self) -> None:
        self.cycles: dict[str, int] = {}
        self.steps: dict[str, int] = {}
        self.runs: dict[str, list[int]] = {}
        self._current: str | None = None
        self._run_cycles = 0
        self._run_steps = 0

    def observe(self, symbol: str, cost: int) -> bool:
        """Charge `cost` to `symbol`. Returns True when a new run just opened."""
        self.cycles[symbol] = self.cycles.get(symbol, 0) + cost
        self.steps[symbol] = self.steps.get(symbol, 0) + 1
        opened = False
        if symbol != self._current:
            self._close()
            self._current = symbol
            opened = True
        self._run_cycles += cost
        self._run_steps += 1
        return opened

    def _close(self) -> None:
        if self._current is not None and self._run_cycles > 0:
            self.runs.setdefault(self._current, []).append(self._run_cycles)
        self._run_cycles = 0
        self._run_steps = 0

    def finish(self) -> None:
        self._close()
        self._current = None


def read_io(client: YmirClient) -> dict[str, int]:
    edsr = client.call("mem.peek", {"address": VDP1_EDSR, "count": 6})["data"]
    tvstat = client.call("mem.peek", {"address": VDP2_TVSTAT, "count": 2})["data"]
    return {
        "edsr": (edsr[0] << 8) | edsr[1],
        "lopr": (edsr[2] << 8) | edsr[3],
        "copr": (edsr[4] << 8) | edsr[5],
        "tvstat": (tvstat[0] << 8) | tvstat[1],
    }


def trace_window(
    client: YmirClient,
    index: SymbolIndex,
    *,
    steps: int,
    slave_stride: int,
    io_stride: int,
    run_log_min_cycles: int,
) -> dict[str, Any]:
    master = RunTracker()
    slave = RunTracker()
    run_log: list[dict[str, Any]] = []
    run_open_cycle = 0
    run_open_symbol: str | None = None
    joint: dict[str, int] = {}
    wait_sites: dict[str, int] = {}
    wait_site_entries: dict[str, int] = {}
    slave_during_master_idle: dict[str, int] = {}
    master_during_slave_idle: dict[str, int] = {}
    io_samples: list[dict[str, int]] = []
    total_cycles = 0
    started = time.monotonic()

    slave_symbol = "<unsampled>"
    pending_slave_cycles = 0

    for step_index in range(steps):
        step = client.call("exec.stepi", {"target": "sh2.master"})
        pc_before = int(step["pc_before"])
        cost = int(step["cycles_advanced"])
        total_cycles += cost
        hit = index.lookup(pc_before)
        master_symbol = hit[0] if hit is not None else "<unmapped>"
        opened = master.observe(master_symbol, cost)

        if opened:
            # An ordered log of the long runs reconstructs the frame's real
            # action sequence, which the aggregated histogram cannot show.
            span = total_cycles - cost - run_open_cycle
            if run_open_symbol is not None and span >= run_log_min_cycles:
                run_log.append(
                    {
                        "symbol": run_open_symbol,
                        "start_cycle": run_open_cycle,
                        "cycles": span,
                    }
                )
            run_open_symbol = master_symbol
            run_open_cycle = total_cycles - cost

        if opened and master_symbol in MASTER_IDLE_SYMBOLS:
            regs = client.call("regs.read", {"target": "sh2.master"})
            pr = int(regs["pr"])
            site = index.lookup(pr)
            label = f"{site[0]}+0x{pr - site[1]:x}" if site is not None else f"<0x{pr:08x}>"
            wait_site_entries[label] = wait_site_entries.get(label, 0) + 1

        pending_slave_cycles += cost
        if step_index % slave_stride == 0:
            sregs = client.call("regs.read", {"target": "sh2.slave"})
            shit = index.lookup(int(sregs["pc"]))
            slave_symbol = shit[0] if shit is not None else "<unmapped>"

        # Charge the stride's cycles to whatever the slave was last observed in.
        if slave_symbol != "<unsampled>":
            slave.observe(slave_symbol, cost)

        mclass = state_class(master_symbol, "master")
        sclass = state_class(slave_symbol, "slave")
        joint[f"{mclass}|{sclass}"] = joint.get(f"{mclass}|{sclass}", 0) + cost
        if mclass.startswith("idle"):
            wait_sites[master_symbol] = wait_sites.get(master_symbol, 0) + cost
            slave_during_master_idle[slave_symbol] = (
                slave_during_master_idle.get(slave_symbol, 0) + cost
            )
        if sclass.startswith("idle"):
            master_during_slave_idle[master_symbol] = (
                master_during_slave_idle.get(master_symbol, 0) + cost
            )

        if io_stride and step_index % io_stride == 0:
            sample = read_io(client)
            sample["cycle"] = total_cycles
            io_samples.append(sample)

    master.finish()
    slave.finish()
    return {
        "wall_seconds": round(time.monotonic() - started, 2),
        "steps": steps,
        "total_cycles": total_cycles,
        "master_cycles": master.cycles,
        "master_steps": master.steps,
        "master_runs": master.runs,
        "slave_cycles": slave.cycles,
        "slave_runs": slave.runs,
        "joint_cycles": joint,
        "wait_site_entries": wait_site_entries,
        "slave_during_master_idle": slave_during_master_idle,
        "master_during_slave_idle": master_during_slave_idle,
        "io_samples": io_samples,
        "run_log": run_log,
    }


def run_stats(runs: list[int]) -> dict[str, Any]:
    ordered = sorted(runs)
    count = len(ordered)
    total = sum(ordered)
    return {
        "count": count,
        "total_cycles": total,
        "mean_cycles": round(total / count, 1) if count else 0.0,
        "median_cycles": ordered[count // 2] if count else 0,
        "min_cycles": ordered[0] if count else 0,
        "max_cycles": ordered[-1] if count else 0,
    }


def summarise(windows: list[dict[str, Any]], clock_hz: float, vblanks_per_frame: float) -> dict[str, Any]:
    cycles_per_vblank = clock_hz / 60.0
    cycles_per_frame = cycles_per_vblank * vblanks_per_frame
    total_cycles = sum(w["total_cycles"] for w in windows)

    def merge_counter(key: str) -> dict[str, int]:
        merged: dict[str, int] = {}
        for window in windows:
            for name, value in window[key].items():
                merged[name] = merged.get(name, 0) + value
        return merged

    master_cycles = merge_counter("master_cycles")
    slave_cycles = merge_counter("slave_cycles")
    joint_cycles = merge_counter("joint_cycles")
    wait_entries = merge_counter("wait_site_entries")
    slave_during_master_idle = merge_counter("slave_during_master_idle")
    master_during_slave_idle = merge_counter("master_during_slave_idle")

    master_runs: dict[str, list[int]] = {}
    slave_runs: dict[str, list[int]] = {}
    for window in windows:
        for name, values in window["master_runs"].items():
            master_runs.setdefault(name, []).extend(values)
        for name, values in window["slave_runs"].items():
            slave_runs.setdefault(name, []).extend(values)

    master_total = sum(master_cycles.values())
    slave_total = sum(slave_cycles.values())
    master_idle = sum(v for k, v in master_cycles.items() if k in MASTER_IDLE_SYMBOLS)
    slave_idle = sum(v for k, v in slave_cycles.items() if k in SLAVE_IDLE_SYMBOLS)
    barrier = sum(v for k, v in master_cycles.items() if k in BARRIER_SYMBOLS)

    # Each SH-2 executes the whole frame at the same clock, so machine capacity
    # is split 50/50 between them regardless of how many samples each produced.
    combined_idle_share = 0.5 * (master_idle / master_total if master_total else 0.0) + 0.5 * (
        slave_idle / slave_total if slave_total else 0.0
    )

    io_samples = [s for w in windows for s in w["io_samples"]]
    io_summary: dict[str, Any] = {"samples": len(io_samples)}
    if io_samples:
        cef = sum(1 for s in io_samples if s["edsr"] & 0x0002)
        bef = sum(1 for s in io_samples if s["edsr"] & 0x0001)
        vbl = sum(1 for s in io_samples if s["tvstat"] & 0x0008)
        coprs = [s["copr"] for s in io_samples]
        io_summary.update(
            {
                "edsr_cef_set": cef,
                "edsr_cef_share": round(cef / len(io_samples), 6),
                "edsr_bef_set": bef,
                "tvstat_vblank_set": vbl,
                "tvstat_vblank_share": round(vbl / len(io_samples), 6),
                "copr_min": min(coprs),
                "copr_max": max(coprs),
                "copr_distinct": len(set(coprs)),
            }
        )

    per_window = [
        {
            "steps": w["steps"],
            "total_cycles": w["total_cycles"],
            "vblanks_covered": round(w["total_cycles"] / cycles_per_vblank, 4),
            "master_idle_share": round(
                sum(v for k, v in w["master_cycles"].items() if k in MASTER_IDLE_SYMBOLS)
                / max(1, sum(w["master_cycles"].values())),
                6,
            ),
            "slave_idle_share": round(
                sum(v for k, v in w["slave_cycles"].items() if k in SLAVE_IDLE_SYMBOLS)
                / max(1, sum(w["slave_cycles"].values())),
                6,
            ),
            "wall_seconds": w["wall_seconds"],
        }
        for w in windows
    ]

    frames_covered = total_cycles / cycles_per_frame if cycles_per_frame else 0.0

    return {
        "clock_hz": clock_hz,
        "vblanks_per_frame": vblanks_per_frame,
        "cycles_per_vblank": round(cycles_per_vblank, 1),
        "cycles_per_frame": round(cycles_per_frame, 1),
        "traced_cycles": total_cycles,
        "traced_vblanks": round(total_cycles / cycles_per_vblank, 4),
        "traced_frames_equivalent": round(frames_covered, 4),
        "master": {
            "cycles": master_total,
            "idle_cycles": master_idle,
            "idle_share": round(master_idle / master_total, 6) if master_total else 0.0,
            "barrier_cycles": barrier,
            "barrier_share": round(barrier / master_total, 6) if master_total else 0.0,
            "idle_vblanks_per_frame": round(
                (master_idle / master_total) * vblanks_per_frame, 4
            )
            if master_total
            else 0.0,
        },
        "slave": {
            "cycles": slave_total,
            "idle_cycles": slave_idle,
            "idle_share": round(slave_idle / slave_total, 6) if slave_total else 0.0,
            "idle_vblanks_per_frame": round(
                (slave_idle / slave_total) * vblanks_per_frame, 4
            )
            if slave_total
            else 0.0,
        },
        "combined_idle_share": round(combined_idle_share, 6),
        "joint": {
            key: {
                "cycles": value,
                "share": round(value / total_cycles, 6) if total_cycles else 0.0,
                "vblanks_per_frame": round(
                    (value / total_cycles) * vblanks_per_frame, 4
                )
                if total_cycles
                else 0.0,
            }
            for key, value in sorted(joint_cycles.items(), key=lambda item: -item[1])
        },
        "master_wait_call_sites": dict(
            sorted(wait_entries.items(), key=lambda item: -item[1])
        ),
        "master_vblank_spin_runs": run_stats(
            master_runs.get("_sm64_saturn_source_runtime_wait_vblank", [])
        ),
        "slave_notification_wait_runs": run_stats(
            slave_runs.get("___slave_polling_entry", [])
        ),
        "master_run_structure": {
            name: run_stats(values)
            for name, values in sorted(
                master_runs.items(), key=lambda item: -sum(item[1])
            )[:20]
        },
        "slave_during_master_idle": dict(
            sorted(slave_during_master_idle.items(), key=lambda item: -item[1])[:12]
        ),
        "master_during_slave_idle": dict(
            sorted(master_during_slave_idle.items(), key=lambda item: -item[1])[:20]
        ),
        "master_symbols": [
            {
                "symbol": name,
                "cycles": value,
                "share_of_master": round(value / master_total, 6) if master_total else 0.0,
                "cycles_per_frame": round(
                    (value / master_total) * cycles_per_frame, 1
                )
                if master_total
                else 0.0,
            }
            for name, value in sorted(master_cycles.items(), key=lambda item: -item[1])[:40]
        ],
        "slave_symbols": [
            {
                "symbol": name,
                "cycles": value,
                "share_of_slave": round(value / slave_total, 6) if slave_total else 0.0,
                "cycles_per_frame": round(
                    (value / slave_total) * cycles_per_frame, 1
                )
                if slave_total
                else 0.0,
            }
            for name, value in sorted(slave_cycles.items(), key=lambda item: -item[1])[:40]
        ],
        "vdp_witness": io_summary,
        "per_window": per_window,
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
    # T2.19d: warm up to a fixed route tick rather than a fixed VBlank count,
    # so a faster build is traced at the same route position as a slower one.
    # T2.17 section 6.2 is the measurement this repairs.
    route_warmup.add_warmup_arguments(parser)
    parser.add_argument("--windows", type=int, default=4)
    parser.add_argument("--window-steps", type=int, default=120000)
    parser.add_argument(
        "--window-gap-vblanks",
        type=int,
        default=7,
        help="free-run between windows; 7 is coprime with the 11.2 VB frame so "
        "successive windows land at different frame phases",
    )
    parser.add_argument("--slave-stride", type=int, default=8)
    parser.add_argument("--io-stride", type=int, default=2000)
    parser.add_argument(
        "--run-log-min-cycles",
        type=int,
        default=20000,
        help="record contiguous master runs at least this long, in order, so the "
        "frame's real action sequence is readable instead of only its histogram",
    )
    parser.add_argument("--clock-hz", type=float, default=DEFAULT_SH2_CLOCK_HZ)
    parser.add_argument("--vblanks-per-frame", type=float, default=11.2069)
    parser.add_argument("--timeout", type=float, default=5400.0)
    args = parser.parse_args(argv)

    symbols = load_symbols(args.nm, args.elf)
    index = SymbolIndex(symbols)
    identity_probe = build_elf_identity_probe(args.elf)
    warmup_plan = route_warmup.plan_warmup(args, args.elf)

    client = YmirClient(args.ymir, args.ipl, args.game, args.timeout)
    windows: list[dict[str, Any]] = []
    try:
        run_bios_handoff(
            client,
            lambda frames: client.call("exec.run_for", {"frames": frames}),
            lambda _label: None,
        )
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
        for number in range(args.windows):
            if number > 0 and args.window_gap_vblanks > 0:
                client.call("exec.run_for", {"frames": args.window_gap_vblanks})
            window = trace_window(
                client,
                index,
                steps=args.window_steps,
                slave_stride=args.slave_stride,
                io_stride=args.io_stride,
                run_log_min_cycles=args.run_log_min_cycles,
            )
            windows.append(window)
            print(
                f"  window {number}: {window['steps']} steps, "
                f"{window['total_cycles']} cycles, {window['wall_seconds']}s"
            )
        # Record where the trace actually ended on the route, so the traced
        # span is stated rather than inferred from a VBlank count.
        route_position_after_trace = (
            route_warmup.read_route_position(client, warmup_plan["addresses"])
            if warmup_plan["addresses"] is not None
            else None
        )
    finally:
        try:
            client.shutdown()
        except Exception:  # noqa: BLE001 - shutdown is best-effort
            client.abort()

    summary = summarise(windows, args.clock_hz, args.vblanks_per_frame)
    report = {
        "schema": "sm64-saturn-idle-attribution-v1",
        "elf": str(args.elf),
        "elf_sha256": hashlib.sha256(args.elf.read_bytes()).hexdigest(),
        "game": str(args.game),
        "identity": identity,
        "tracing": {
            "windows": args.windows,
            "window_steps": args.window_steps,
            "window_gap_vblanks": args.window_gap_vblanks,
            "slave_stride": args.slave_stride,
            "io_stride": args.io_stride,
            "run_log_min_cycles": args.run_log_min_cycles,
            "startup_vblanks": args.startup_vblanks,
            "warmup": warmup,
            "route_position_after_trace": route_position_after_trace,
        },
        "summary": summary,
        "windows": windows,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    print(
        f"idle attribution: {summary['traced_vblanks']} VBlanks traced "
        f"({summary['traced_frames_equivalent']} frames), "
        f"combined idle {summary['combined_idle_share'] * 100:.2f}%"
    )
    print(
        f"  master idle {summary['master']['idle_share'] * 100:.2f}% "
        f"= {summary['master']['idle_vblanks_per_frame']} VB/frame; "
        f"slave idle {summary['slave']['idle_share'] * 100:.2f}% "
        f"= {summary['slave']['idle_vblanks_per_frame']} VB/frame"
    )
    print("  joint contingency (share of traced cycles):")
    for key, bucket in list(summary["joint"].items())[:8]:
        print(f"    {key:46s} {bucket['share'] * 100:6.2f}%  {bucket['vblanks_per_frame']} VB/frame")
    print("  master VBlank-spin call sites (entries):")
    for name, count in list(summary["master_wait_call_sites"].items())[:8]:
        print(f"    {name:60s} {count}")
    spin = summary["master_vblank_spin_runs"]
    print(
        f"  master VBlank-spin excursions: {spin['count']} traced, "
        f"mean {spin['mean_cycles']} cycles, median {spin['median_cycles']}, "
        f"max {spin['max_cycles']}"
    )
    print("  VDP witness:", json.dumps(summary["vdp_witness"]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
