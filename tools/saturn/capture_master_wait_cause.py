#!/usr/bin/env python3
"""Classify *why* the master SH-2 entered its VBlank spin, on the shipped build.

`capture_idle_attribution.py` (T2.16) answers *where* the master waits: it reads
PR at each entry into `_sm64_saturn_source_runtime_wait_vblank` and attributes
the spin to its call site.  Since T2.17 collapsed the epoch stall there has been
exactly one call site left -- the `SM64_SATURN_FRAME_WAIT_VBLANK` arm of
`sourceboot_frame_pipeline_dispatch` -- so the site no longer discriminates
anything.  W6a then moved master idle from exactly 0.000000 to 5.22%, which made
the *cause* of that one site the decision-relevant quantity.

This tool adds the cause.  `sm64_saturn_frame_pipeline_step()` has four distinct
paths that return `WAIT_VBLANK`, and every one of them leaves a different
`sm64_saturn_frame_pipeline_t` behind.  At each entry to the spin the whole
scheduler struct is read out of LWRAM and the branch is re-derived in Python
from the same predicates the target evaluated, so the classification is a
decode of the machine's own state rather than an inference from timing:

  publish-pending  `publish_pending` still set -- publication has not been
                   acknowledged.  Transient by construction.
  service-epoch    the active generation still needs SERVICE and this field's
                   single service slot is already spent.
  transfer-epoch   the active generation still needs its command-VRAM transfer
                   and this field's single submitting-poll slot is already
                   spent.  This is the arm the W0 VDP1-busy deferral lands in:
                   `transfer_deferred()` clears `transfer_started` but keeps
                   `transfer_poll_vblank`, so a deferred submit waits a field.
  frame-complete   nothing is admissible at all: no presentation opportunity is
                   pending, no render is active (or it is fully transferred),
                   and there is no simulation credit.  The master has finished
                   the frame and is waiting for the raster to hand it the next
                   one.

Each spin also carries a strided VDP1 witness *inside* the wait, which is what
separates a hardware-gated wait from a scheduler-quantised one: if VDP1 raises
`EDSR.CEF` (plot complete) long before the master resumes, the hardware
condition released early and the remaining time is the schedule's, not the
chip's.

Nothing is rebuilt and nothing is instrumented; this reads the sealed product
ELF through `exec.stepi` / `regs.read` / `mem.peek` exactly as T2.16 does.
"""

from __future__ import annotations

import argparse
import ymir_binary
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

DEFAULT_SH2_CLOCK_HZ = 26_846_590.91

MASTER_IDLE_SYMBOL = "_sm64_saturn_source_runtime_wait_vblank"

VDP1_EDSR = 0x25D00010  # EDSR, LOPR, COPR: three consecutive 16-bit registers
VDP2_TVSTAT = 0x25F80004
VDP1_EDSR_CEF = 0x0002

# sm64_saturn_frame_pipeline_t, src/port/saturn/runtime/saturn_frame_pipeline.h.
# Big-endian SH-2, natural alignment; the 0x4c size nm reports for
# `_sourceboot_frame_pipeline` is the check that this layout is right.
PIPELINE_U32 = (
    ("last_vblank_count", 0),
    ("displayed_generation", 4),
    ("simulation_generation", 8),
    ("render_generation", 12),
    ("render_completed_generation", 16),
    ("transfer_generation", 20),
    ("transfer_completed_generation", 24),
    ("queued_snapshot_generation", 28),
    ("action_generation", 32),
    ("render_service_vblank", 36),
    ("transfer_poll_vblank", 40),
    ("transfer_submit_vblank", 44),
    ("dropped_sim_tick_credits", 48),
    ("previous_frame_reuse_count", 52),
    ("publish_generation", 72),
)
PIPELINE_U8 = (
    ("available_sim_credit", 56),
    ("sim_ticks_this_presentation", 57),
    ("sim_vblank_remainder", 58),
)
PIPELINE_BOOL = (
    ("presentation_pending", 59),
    ("render_active", 60),
    ("queued_snapshot_valid", 61),
    ("render_completed_valid", 62),
    ("transfer_completed_valid", 63),
    ("render_service_started", 64),
    ("transfer_started", 65),
    ("publish_pending", 66),
    ("render_service_vblank_valid", 67),
    ("transfer_poll_vblank_valid", 68),
    ("transfer_submit_vblank_valid", 69),
)
PIPELINE_BYTES = 76


def load_data_symbols(nm: Path, elf: Path) -> dict[str, tuple[int, int]]:
    """Return {name: (address, size)} for object (non-text) symbols."""
    raw = subprocess.run(
        [str(nm), "-S", "--numeric-sort", "--defined-only", str(elf)],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    found: dict[str, tuple[int, int]] = {}
    for line in raw.splitlines():
        parts = line.split()
        if len(parts) == 4:
            address, size, _kind, name = parts
        elif len(parts) == 3:
            address, _kind, name = parts
            size = "0"
        else:
            continue
        try:
            found[name] = (int(address, 16), int(size, 16))
        except ValueError:
            continue
    return found


def decode_pipeline(data: list[int]) -> dict[str, Any]:
    def u32(offset: int) -> int:
        return (
            (data[offset] << 24)
            | (data[offset + 1] << 16)
            | (data[offset + 2] << 8)
            | data[offset + 3]
        )

    state: dict[str, Any] = {}
    for name, offset in PIPELINE_U32:
        state[name] = u32(offset)
    for name, offset in PIPELINE_U8:
        state[name] = data[offset]
    for name, offset in PIPELINE_BOOL:
        state[name] = bool(data[offset])
    return state


def classify_wait(state: dict[str, Any]) -> dict[str, Any]:
    """Re-derive which `sm64_saturn_frame_pipeline_step()` arm returned WAIT.

    The predicates are transcribed from saturn_frame_pipeline.c in the order the
    target evaluates them, so exactly one arm can claim a given struct.
    """
    if state["publish_pending"]:
        return {"cause": "publish-pending", "detail": "publication not acknowledged"}

    render_done = (
        state["render_completed_valid"]
        and state["render_completed_generation"] == state["render_generation"]
    )
    transfer_done = (
        state["transfer_completed_valid"]
        and state["transfer_completed_generation"] == state["render_generation"]
    )

    if state["render_active"] and not render_done:
        serviced_this_vblank = (
            state["render_service_vblank_valid"]
            and state["render_service_vblank"] == state["last_vblank_count"]
        )
        if serviced_this_vblank and not state["presentation_pending"]:
            return {
                "cause": "service-epoch",
                "detail": "render service already spent in this field",
            }
        return {
            "cause": "unexplained-service",
            "detail": "render incomplete but service slot appears free",
        }

    if state["render_active"] and render_done and not transfer_done:
        polling_in_submit_field = (
            state["transfer_started"]
            and state["transfer_generation"] == state["render_generation"]
            and state["transfer_submit_vblank_valid"]
            and state["transfer_submit_vblank"] == state["last_vblank_count"]
        )
        polled_this_vblank = (
            not polling_in_submit_field
            and state["transfer_poll_vblank_valid"]
            and state["transfer_poll_vblank"] == state["last_vblank_count"]
        )
        if polled_this_vblank and not state["presentation_pending"]:
            return {
                "cause": "transfer-epoch",
                "detail": (
                    "command-VRAM submit slot already spent in this field; "
                    + (
                        "submit was deferred (transfer_started clear)"
                        if not state["transfer_started"]
                        else "submit issued, queue not retired"
                    )
                ),
            }
        return {
            "cause": "unexplained-transfer",
            "detail": "transfer incomplete but poll slot appears free",
        }

    if state["presentation_pending"]:
        return {
            "cause": "unexplained-presentation",
            "detail": "presentation pending yet WAIT was returned",
        }

    return {
        "cause": "frame-complete",
        "detail": (
            "no presentation opportunity, no admissible render, "
            f"sim credit {state['available_sim_credit']}"
        ),
    }


def read_io(client: YmirClient) -> dict[str, int]:
    edsr = client.call("mem.peek", {"address": VDP1_EDSR, "count": 6})["data"]
    tvstat = client.call("mem.peek", {"address": VDP2_TVSTAT, "count": 2})["data"]
    return {
        "edsr": (edsr[0] << 8) | edsr[1],
        "lopr": (edsr[2] << 8) | edsr[3],
        "copr": (edsr[4] << 8) | edsr[5],
        "tvstat": (tvstat[0] << 8) | tvstat[1],
    }


def read_u32(client: YmirClient, address: int) -> int:
    data = client.call("mem.peek", {"address": address, "count": 4})["data"]
    return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3]


def trace(
    client: YmirClient,
    index: SymbolIndex,
    *,
    steps: int,
    pipeline_address: int,
    transfer_pending_address: int | None,
    vblank_out_address: int | None,
    wait_io_stride: int,
) -> dict[str, Any]:
    cycles: dict[str, int] = {}
    events: list[dict[str, Any]] = []
    total_cycles = 0
    current_symbol: str | None = None
    open_event: dict[str, Any] | None = None
    open_cycle = 0
    steps_in_wait = 0
    started = time.monotonic()

    def snapshot() -> dict[str, Any]:
        raw = client.call(
            "mem.peek", {"address": pipeline_address, "count": PIPELINE_BYTES}
        )["data"]
        shot: dict[str, Any] = {"pipeline": decode_pipeline(raw), "io": read_io(client)}
        if transfer_pending_address is not None:
            shot["vdp1_transfer_pending"] = read_u32(client, transfer_pending_address)
        if vblank_out_address is not None:
            shot["vblank_out_count"] = read_u32(client, vblank_out_address)
        return shot

    for step_index in range(steps):
        step = client.call("exec.stepi", {"target": "sh2.master"})
        pc_before = int(step["pc_before"])
        cost = int(step["cycles_advanced"])
        total_cycles += cost
        hit = index.lookup(pc_before)
        symbol = hit[0] if hit is not None else "<unmapped>"
        cycles[symbol] = cycles.get(symbol, 0) + cost

        if symbol != current_symbol:
            if open_event is not None:
                open_event["cycles"] = total_cycles - cost - open_event["start_cycle"]
                open_event["exit"] = snapshot()
                open_event["steps"] = steps_in_wait
                open_event = None
            current_symbol = symbol
            open_cycle = total_cycles - cost
            if symbol == MASTER_IDLE_SYMBOL:
                regs = client.call("regs.read", {"target": "sh2.master"})
                pr = int(regs["pr"])
                site = index.lookup(pr)
                label = (
                    f"{site[0]}+0x{pr - site[1]:x}"
                    if site is not None
                    else f"<0x{pr:08x}>"
                )
                entry = snapshot()
                open_event = {
                    "index": len(events),
                    "start_cycle": open_cycle,
                    "call_site": label,
                    "call_site_pr": pr,
                    "entry": entry,
                    "cause": classify_wait(entry["pipeline"]),
                    "io_trail": [],
                }
                steps_in_wait = 0
                events.append(open_event)

        if open_event is not None:
            steps_in_wait += 1
            if wait_io_stride and steps_in_wait % wait_io_stride == 0:
                sample = read_io(client)
                sample["cycle_offset"] = total_cycles - open_event["start_cycle"]
                open_event["io_trail"].append(sample)

    if open_event is not None:
        open_event["cycles"] = total_cycles - open_event["start_cycle"]
        open_event["exit"] = snapshot()
        open_event["steps"] = steps_in_wait
        open_event["truncated"] = True

    return {
        "wall_seconds": round(time.monotonic() - started, 2),
        "steps": steps,
        "total_cycles": total_cycles,
        "master_cycles": cycles,
        "wait_events": events,
    }


def summarise(window: dict[str, Any], clock_hz: float, vblanks_per_frame: float) -> dict[str, Any]:
    cycles_per_vblank = clock_hz / 60.0
    cycles_per_frame = cycles_per_vblank * vblanks_per_frame
    total = window["total_cycles"]
    idle = window["master_cycles"].get(MASTER_IDLE_SYMBOL, 0)
    events = window["wait_events"]

    by_cause: dict[str, dict[str, Any]] = {}
    for event in events:
        cause = event["cause"]["cause"]
        bucket = by_cause.setdefault(
            cause, {"entries": 0, "cycles": 0, "max_cycles": 0, "call_sites": {}}
        )
        bucket["entries"] += 1
        bucket["cycles"] += event.get("cycles", 0)
        bucket["max_cycles"] = max(bucket["max_cycles"], event.get("cycles", 0))
        site = event["call_site"]
        bucket["call_sites"][site] = bucket["call_sites"].get(site, 0) + 1
    for bucket in by_cause.values():
        bucket["share_of_idle"] = round(bucket["cycles"] / idle, 6) if idle else 0.0
        bucket["cycles_per_frame"] = (
            round((bucket["cycles"] / total) * cycles_per_frame, 1) if total else 0.0
        )

    # For each long wait: when did VDP1 raise CEF relative to the master's
    # resume? A hardware-gated wait releases at the end; a scheduler-quantised
    # one releases early and the master keeps spinning anyway.
    release = []
    for event in events:
        if event.get("cycles", 0) < 20000:
            continue
        trail = event["io_trail"]
        cef_first = None
        for sample in trail:
            if sample["edsr"] & VDP1_EDSR_CEF:
                cef_first = sample["cycle_offset"]
                break
        release.append(
            {
                "index": event["index"],
                "cause": event["cause"]["cause"],
                "cycles": event["cycles"],
                "vblanks": round(event["cycles"] / cycles_per_vblank, 4),
                "entry_edsr": event["entry"]["io"]["edsr"],
                "entry_cef": bool(event["entry"]["io"]["edsr"] & VDP1_EDSR_CEF),
                "exit_cef": bool(event["exit"]["io"]["edsr"] & VDP1_EDSR_CEF),
                "cef_first_set_cycle_offset": cef_first,
                "cef_first_set_fraction": (
                    round(cef_first / event["cycles"], 4)
                    if cef_first is not None and event["cycles"]
                    else None
                ),
                "samples": len(trail),
            }
        )

    return {
        "clock_hz": clock_hz,
        "vblanks_per_frame": vblanks_per_frame,
        "cycles_per_vblank": round(cycles_per_vblank, 1),
        "cycles_per_frame": round(cycles_per_frame, 1),
        "traced_cycles": total,
        "traced_vblanks": round(total / cycles_per_vblank, 4),
        "traced_frames_equivalent": round(total / cycles_per_frame, 4),
        "master_idle_cycles": idle,
        "master_idle_share": round(idle / total, 6) if total else 0.0,
        "master_idle_cycles_per_frame": round((idle / total) * cycles_per_frame, 1)
        if total
        else 0.0,
        "wait_entries": len(events),
        "by_cause": by_cause,
        "long_wait_release": release,
        "master_symbols": [
            {"symbol": name, "cycles": value}
            for name, value in sorted(
                window["master_cycles"].items(), key=lambda item: -item[1]
            )[:25]
        ],
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    ymir_binary.add_ymir_argument(parser)
    parser.add_argument("--ipl", type=Path, required=True)
    parser.add_argument("--game", type=Path, required=True, help="exact matching CUE")
    parser.add_argument("--elf", type=Path, required=True, help="exact matching ELF")
    parser.add_argument("--nm", type=Path, required=True, help="sh-elf-nm")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--startup-vblanks", type=int, default=4096)
    route_warmup.add_warmup_arguments(parser)
    parser.add_argument("--steps", type=int, default=6_000_000)
    parser.add_argument("--wait-io-stride", type=int, default=400)
    parser.add_argument("--clock-hz", type=float, default=DEFAULT_SH2_CLOCK_HZ)
    parser.add_argument("--vblanks-per-frame", type=float, default=8.5172)
    parser.add_argument("--timeout", type=float, default=5400.0)
    parser.add_argument("--sh2-cache", action="store_true")
    args = parser.parse_args(argv)

    index = SymbolIndex(load_symbols(args.nm, args.elf))
    data_symbols = load_data_symbols(args.nm, args.elf)
    if "_sourceboot_frame_pipeline" not in data_symbols:
        raise SystemExit("_sourceboot_frame_pipeline is absent from the ELF")
    pipeline_address, pipeline_size = data_symbols["_sourceboot_frame_pipeline"]
    if pipeline_size not in (0, PIPELINE_BYTES):
        raise SystemExit(
            f"_sourceboot_frame_pipeline is {pipeline_size} bytes, expected {PIPELINE_BYTES}"
        )
    transfer_pending = data_symbols.get("_sourceboot_vdp1_transfer_pending")
    vblank_out = data_symbols.get("_sourceboot_vblank_out_count")

    identity_probe = build_elf_identity_probe(args.elf)
    warmup_plan = route_warmup.plan_warmup(args, args.elf)

    client = YmirClient(
        args.ymir, args.ipl, args.game, args.timeout, sh2_cache=args.sh2_cache
    )
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
            f"  warm-up: {warmup['mode']} -> replay tick {warmup.get('replay_ticks')} "
            f"after {warmup['vblanks_advanced']} VBlanks"
        )
        window = trace(
            client,
            index,
            steps=args.steps,
            pipeline_address=pipeline_address,
            transfer_pending_address=(
                transfer_pending[0] if transfer_pending is not None else None
            ),
            vblank_out_address=vblank_out[0] if vblank_out is not None else None,
            wait_io_stride=args.wait_io_stride,
        )
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

    summary = summarise(window, args.clock_hz, args.vblanks_per_frame)
    report = {
        "schema": "sm64-saturn-master-wait-cause-v1",
        "elf": str(args.elf),
        "elf_sha256": hashlib.sha256(args.elf.read_bytes()).hexdigest(),
        "game": str(args.game),
        "identity": identity,
        "addresses": {
            "frame_pipeline": f"0x{pipeline_address:08x}",
            "vdp1_transfer_pending": (
                f"0x{transfer_pending[0]:08x}" if transfer_pending is not None else None
            ),
            "vblank_out_count": (
                f"0x{vblank_out[0]:08x}" if vblank_out is not None else None
            ),
        },
        "tracing": {
            "steps": args.steps,
            "wait_io_stride": args.wait_io_stride,
            "startup_vblanks": args.startup_vblanks,
            "warmup": warmup,
            "route_position_after_trace": route_position_after_trace,
            "sh2_cache_emulation": bool(args.sh2_cache),
        },
        "summary": summary,
        "window": window,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    print(
        f"master wait cause: {summary['traced_frames_equivalent']} frames traced, "
        f"idle {summary['master_idle_share'] * 100:.4f}% "
        f"({summary['master_idle_cycles_per_frame']} cycles/frame), "
        f"{summary['wait_entries']} entries"
    )
    for cause, bucket in sorted(
        summary["by_cause"].items(), key=lambda item: -item[1]["cycles"]
    ):
        print(
            f"    {cause:24s} {bucket['entries']:4d} entries  "
            f"{bucket['cycles']:9d} cycles  "
            f"{bucket['share_of_idle'] * 100:6.2f}% of idle  "
            f"{bucket['cycles_per_frame']}/frame"
        )
    for row in summary["long_wait_release"]:
        print(
            f"    long wait #{row['index']}: {row['cause']}, {row['vblanks']} VB, "
            f"entry CEF={row['entry_cef']} exit CEF={row['exit_cef']} "
            f"first CEF at {row['cef_first_set_fraction']} of the wait"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
