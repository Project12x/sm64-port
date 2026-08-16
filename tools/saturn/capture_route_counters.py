#!/usr/bin/env python3
"""Peek the shipped build's live render/collision counters -- no rebuild, no probe.

Sprint 2 T2.14.  Three counter blocks already exist as globals in the product
ELF, and none of them had ever been captured:

  * ``sourceboot_fast3d.profile``  -- the per-triangle funnel
    (``triangles_transformed`` / ``_emitted`` / ``_vdp1_emitted`` and every
    ``reject_*`` bucket).  The publisher that copies these into
    ``sourceboot_route_checkpoint`` is compiled out whenever
    ``SATURN_SOURCEBOOT_LIVE_INPUT=1`` (``sourceboot/main.c``), which is the
    shipped configuration -- hence the long-standing gap in the evidence tree.
    The struct itself is live regardless, so peeking it needs no build and no
    diagnostic tuple.
  * ``sState``                     -- ``sm64_saturn_source_runtime_state_t``.
    ``submitted_tasks``/``unhandled_tasks`` witness whether the source display
    list reaches ``exec_display_list`` at all; ``scene_graph_walks`` witnesses
    whether the geo walk ran.
  * ``gNumCalls``                  -- the engine's own exact ``find_floor`` /
    ``find_ceil`` / ``find_wall`` tally (``surface_collision.c``).  Nothing on
    the shipped route resets it, so per-frame rates come from deltas.

Addresses come from the ELF symbol table; pass them explicitly so the caller
stays honest about which build was measured.

The counters are *cumulative* on the demo path: the per-frame ``memset`` lives
in ``sm64_saturn_fast3d_frontend_submit``, which never runs when
``SATURN_DEMO_PATH=1``.  Rates are therefore reported as deltas per unit of
``frame_serial``, not as raw reads.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import statistics
import sys
import time
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))

from capture_route_views import YmirClient  # noqa: E402
from capture_sourceboot_boot_trace import run_bios_handoff  # noqa: E402
from capture_sourceboot_throughput import (  # noqa: E402
    build_elf_identity_probe,
    wait_for_target_identity,
)

# Offsets within sm64_saturn_fast3d_profile_t (saturn_fast3d_frontend.h),
# obtained from offsetof() on a host compile of that header.  All uint32_t.
PROFILE_U32: dict[str, int] = {
    "frame_serial": 0,
    "command_count": 4,
    "triangle_count": 24,
    "triangles_transformed": 44,
    "triangles_emitted": 48,
    "reject_near_far": 52,
    "reject_backface": 56,
    "reject_degenerate": 60,
    "reject_vertex_range": 64,
    "reject_command_capacity": 68,
    "triangles_vdp1_emitted": 80,
    "reject_vdp1_arena_capacity": 84,
    "reject_w_nonpositive": 88,
    "reject_z_near": 92,
    "reject_z_far": 96,
    "reject_offscreen": 100,
    "reject_span": 104,
}
PROFILE_BYTES = 128

# sm64_saturn_source_runtime_state_t (saturn_source_runtime.h), uint32_t fields.
STATE_U32: dict[str, int] = {
    "input_polls": 0,
    "submitted_tasks": 4,
    "unhandled_tasks": 8,
    "audio_ticks": 12,
    "preflight_tasks": 16,
    "preflight_failures": 20,
    "input_replay_ticks": 24,
    "scene_graph_walks": 36,
    "scene_graph_walks_suppressed": 40,
}
STATE_BYTES = 44

# struct NumTimesCalled (object_list_processor.h): three s16.
NUMCALLS_S16: dict[str, int] = {"floor": 0, "ceil": 2, "wall": 4}
NUMCALLS_BYTES = 6


def peek(client: YmirClient, address: int, count: int) -> bytes:
    window = client.call("mem.peek", {"address": address, "count": count})
    data = window.get("data", [])
    if not isinstance(data, list) or len(data) < count:
        raise RuntimeError(f"short peek at {address:#010x}: {len(data)}/{count} bytes")
    return bytes(data[:count])


def decode_u32(buf: bytes, table: dict[str, int]) -> dict[str, int]:
    return {name: int.from_bytes(buf[off:off + 4], "big") for name, off in table.items()}


def decode_s16(buf: bytes, table: dict[str, int]) -> dict[str, int]:
    out: dict[str, int] = {}
    for name, off in table.items():
        raw = int.from_bytes(buf[off:off + 2], "big")
        out[name] = raw - 0x10000 if raw >= 0x8000 else raw
    return out


def rates(
    rows: list[dict[str, Any]], block: str, field: str, *, wrap: int | None
) -> dict[str, float]:
    """Per-frame_serial deltas, tolerating an s16 wrap when `wrap` is given."""
    per: list[float] = []
    for first, second in zip(rows, rows[1:]):
        frames = second["profile"]["frame_serial"] - first["profile"]["frame_serial"]
        if frames <= 0:
            continue
        delta = second[block][field] - first[block][field]
        if wrap is not None and delta < 0:
            delta += wrap
        if delta < 0:
            continue
        per.append(delta / frames)
    if not per:
        return {"intervals": 0}
    return {
        "intervals": len(per),
        "mean": round(statistics.mean(per), 4),
        "median": round(statistics.median(per), 4),
        "min": round(min(per), 4),
        "max": round(max(per), 4),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ymir", type=Path, required=True)
    parser.add_argument("--ipl", type=Path, required=True)
    parser.add_argument("--game", type=Path, required=True, help="exact matching CUE")
    parser.add_argument("--elf", type=Path, required=True, help="exact matching ELF")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--profile-address",
        type=lambda value: int(value, 0),
        required=True,
        help="address of sourceboot_fast3d (profile is its first member)",
    )
    parser.add_argument(
        "--state-address",
        type=lambda value: int(value, 0),
        required=True,
        help="address of saturn_source_runtime.c's sState",
    )
    parser.add_argument(
        "--numcalls-address",
        type=lambda value: int(value, 0),
        default=None,
        help="address of gNumCalls; omit to skip the collision tally",
    )
    parser.add_argument("--startup-vblanks", type=int, default=4096)
    parser.add_argument(
        "--warmup-vblanks",
        type=int,
        default=1800,
        help="free-run VBlanks after identity match; the ELF is resident long "
        "before the gameplay stage renders",
    )
    parser.add_argument("--samples", type=int, default=120)
    parser.add_argument("--gap-vblanks", type=int, default=11)
    parser.add_argument("--timeout", type=float, default=2400.0)
    args = parser.parse_args(argv)

    client = YmirClient(args.ymir, args.ipl, args.game, args.timeout)
    rows: list[dict[str, Any]] = []
    started = time.time()
    try:
        run_bios_handoff(
            client,
            lambda frames: client.call("exec.run_for", {"frames": frames}),
            lambda _label: None,
        )
        identity = wait_for_target_identity(
            client,
            build_elf_identity_probe(args.elf),
            startup_vblanks=args.startup_vblanks,
        )
        remaining = args.warmup_vblanks
        while remaining > 0:
            chunk = min(remaining, 3600)
            client.call("exec.run_for", {"frames": chunk})
            remaining -= chunk
        for index in range(args.samples):
            row: dict[str, Any] = {
                "sample": index,
                "profile": decode_u32(
                    peek(client, args.profile_address, PROFILE_BYTES), PROFILE_U32
                ),
                "state": decode_u32(
                    peek(client, args.state_address, STATE_BYTES), STATE_U32
                ),
            }
            if args.numcalls_address is not None:
                row["numcalls"] = decode_s16(
                    peek(client, args.numcalls_address, NUMCALLS_BYTES), NUMCALLS_S16
                )
            rows.append(row)
            client.call("exec.run_for", {"frames": args.gap_vblanks})
    finally:
        try:
            client.shutdown()
        except Exception:  # noqa: BLE001 - shutdown is best-effort
            client.abort()

    summary: dict[str, Any] = {"profile": {}, "state": {}, "numcalls": {}}
    for field in PROFILE_U32:
        if field == "frame_serial":
            continue
        summary["profile"][field] = rates(rows, "profile", field, wrap=None)
    for field in STATE_U32:
        summary["state"][field] = rates(rows, "state", field, wrap=None)
    if args.numcalls_address is not None:
        for field in NUMCALLS_S16:
            summary["numcalls"][field] = rates(rows, "numcalls", field, wrap=0x10000)

    report = {
        "schema": "sm64-saturn-route-counters-v1",
        "elf": str(args.elf),
        "elf_sha256": hashlib.sha256(args.elf.read_bytes()).hexdigest(),
        "game": str(args.game),
        "identity": identity,
        "addresses": {
            "profile": hex(args.profile_address),
            "state": hex(args.state_address),
            "numcalls": None
            if args.numcalls_address is None
            else hex(args.numcalls_address),
        },
        "sampling": {
            "startup_vblanks": args.startup_vblanks,
            "warmup_vblanks": args.warmup_vblanks,
            "samples": args.samples,
            "gap_vblanks": args.gap_vblanks,
        },
        "wall_seconds": round(time.time() - started, 1),
        "per_frame": summary,
        "rows": rows,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    print(f"route counters: {len(rows)} samples, {report['wall_seconds']}s")
    for block in ("profile", "state", "numcalls"):
        if not summary[block]:
            continue
        print(f"  --- {block} (per frame_serial) ---")
        for field, stats in summary[block].items():
            if not stats.get("intervals"):
                continue
            print(
                f"    {field:32s} mean={stats['mean']:10.3f}"
                f" median={stats['median']:10.3f}"
                f" min={stats['min']:9.3f} max={stats['max']:9.3f}"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
