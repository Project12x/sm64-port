#!/usr/bin/env python3
"""Peek the shipped build's live actor command-slot counters -- no rebuild.

Sprint 2 T2.19a.  T2.8 published "552.2 commands per present, actor 97.5 =
17.7%" from the COPR/fence instrument and named it "the number a future
command-count task should start from".  That figure does not separate the two
things a command-count task needs to tell apart:

  * ``s_actor_draw_count``     -- one Gouraud polygon per surviving Mario
    primitive (``saturn_demo_render.c:demo_finalize_mario_draws``); and
  * ``s_actor_texture_count``  -- the *additional* alpha-keyed RGB1555
    distorted sprite emitted for a surviving primitive that carries a texture
    tile.  This is the whole of the "Mario double-emit": at most 50, because
    only 50 of the 644 compiled primitives have a non-``0xFFFF`` entry in
    ``sm64_mario_texture_tile_start``.

Both are plain file-scope ``uint16_t`` statics in the product ELF, recomputed
every frame, so they need no build, no probe and no diagnostic tuple -- only
their addresses from the ELF symbol table.

``sm64_saturn_vdp1_backend_t`` supplies the denominator from the same peek
window: ``list.count`` is the published command count for the frame currently
built, and ``commands.peak`` is the arena high-water mark, which is monotone
and therefore immune to the phase-locked-sampling artifact T2.16 found.

Unlike the T2.14 funnel counters these values are *instantaneous*, not
cumulative: they are overwritten each frame rather than accumulated.  Rates are
therefore reported as sample distributions and histograms, never as deltas.
Samples taken while ``demo_finalize_mario_draws`` is mid-rebuild can observe a
partially refilled counter, which is why the histogram is published in full.
"""

from __future__ import annotations

import argparse
import collections
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
import route_warmup  # noqa: E402
from capture_sourceboot_throughput import (  # noqa: E402
    build_elf_identity_probe,
    wait_for_target_identity,
)

# saturn_demo_render.c file-scope statics.  s_actor_command_count and
# s_actor_texture_count are adjacent, so one aligned 4-byte peek reads both
# from the same instant.
ACTOR_U16: dict[str, int] = {
    "s_actor_command_count": 0,
    "s_actor_texture_count": 2,
}
ACTOR_BYTES = 4

# sm64_saturn_vdp1_backend_t = { vdp1_cmdt_list_t list; command_arena_t
# commands; }.  vdp1_cmdt_list_t is {cmdts:4, count:2} __aligned(4), size 8
# (static_assert in cmdt.h:224); sm64_saturn_command_arena_t is
# {capacity, setup_count, cursor, previous_end, live_count, peak} u16 then a
# bool.  All natural alignment, no pointers inside the arena, so host offsets
# equal SH-2 offsets.
BACKEND_U16: dict[str, int] = {
    "list_count": 4,
    "arena_capacity": 8,
    "arena_setup_count": 10,
    "arena_cursor": 12,
    "arena_previous_end": 14,
    "arena_live_count": 16,
    "arena_peak": 18,
}
BACKEND_BYTES = 24

PROFILE_U32: dict[str, int] = {"frame_serial": 0}
PROFILE_BYTES = 4


def peek(client: YmirClient, address: int, count: int) -> bytes:
    """Read `count` bytes at `address`.

    Kept local rather than imported from capture_route_counters.py so this
    capture does not couple to that module's evolving offset tables.
    """
    window = client.call("mem.peek", {"address": address, "count": count})
    data = window.get("data", [])
    if not isinstance(data, list) or len(data) < count:
        raise RuntimeError(f"short peek at {address:#010x}: {len(data)}/{count} bytes")
    return bytes(data[:count])


def decode_u16(buf: bytes, table: dict[str, int]) -> dict[str, int]:
    return {name: int.from_bytes(buf[off:off + 2], "big") for name, off in table.items()}


def decode_u32(buf: bytes, table: dict[str, int]) -> dict[str, int]:
    return {name: int.from_bytes(buf[off:off + 4], "big") for name, off in table.items()}


def distribution(values: list[int]) -> dict[str, Any]:
    if not values:
        return {"samples": 0}
    counts = collections.Counter(values)
    return {
        "samples": len(values),
        "mean": round(statistics.mean(values), 4),
        "median": statistics.median(values),
        "mode": counts.most_common(1)[0][0],
        "min": min(values),
        "max": max(values),
        "histogram": {str(key): counts[key] for key in sorted(counts)},
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ymir", type=Path, required=True)
    parser.add_argument("--ipl", type=Path, required=True)
    parser.add_argument("--game", type=Path, required=True, help="exact matching CUE")
    parser.add_argument("--elf", type=Path, required=True, help="exact matching ELF")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--actor-address",
        type=lambda value: int(value, 0),
        required=True,
        help="address of s_actor_command_count (s_actor_texture_count follows)",
    )
    parser.add_argument(
        "--backend-address",
        type=lambda value: int(value, 0),
        required=True,
        help="address of sourceboot_vdp1_backend",
    )
    parser.add_argument(
        "--profile-address",
        type=lambda value: int(value, 0),
        required=True,
        help="address of sourceboot_fast3d (frame_serial is its first member)",
    )
    parser.add_argument("--startup-vblanks", type=int, default=4096)
    route_warmup.add_warmup_arguments(parser)
    parser.add_argument("--samples", type=int, default=150)
    parser.add_argument(
        "--gap-vblanks",
        type=int,
        default=7,
        help="stride between samples; keep it coprime with the observed frame "
        "length so the sweep is not phase-locked (T2.16)",
    )
    parser.add_argument("--timeout", type=float, default=2400.0)
    args = parser.parse_args(argv)
    # Rejects an unreachable tick target before an emulator frame is spent.
    warmup_plan = route_warmup.plan_warmup(args, args.elf)

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
        # T2.19d: warm up to a route tick, not a VBlank count.  A warm-up
        # that does not reach its target raises rather than sampling early.
        warmup = route_warmup.execute_warmup(client, warmup_plan)
        for index in range(args.samples):
            actor = decode_u16(peek(client, args.actor_address, ACTOR_BYTES), ACTOR_U16)
            backend = decode_u16(
                peek(client, args.backend_address, BACKEND_BYTES), BACKEND_U16
            )
            profile = decode_u32(
                peek(client, args.profile_address, PROFILE_BYTES), PROFILE_U32
            )
            rows.append(
                {
                    "sample": index,
                    "frame_serial": profile["frame_serial"],
                    "actor": actor,
                    "backend": backend,
                }
            )
            client.call("exec.run_for", {"frames": args.gap_vblanks})
    finally:
        try:
            client.shutdown()
        except Exception:  # noqa: BLE001 - shutdown is best-effort
            client.abort()

    command_counts = [row["actor"]["s_actor_command_count"] for row in rows]
    texture_counts = [row["actor"]["s_actor_texture_count"] for row in rows]
    draw_counts = [
        row["actor"]["s_actor_command_count"] - row["actor"]["s_actor_texture_count"]
        for row in rows
    ]
    list_counts = [row["backend"]["list_count"] for row in rows]
    live_counts = [row["backend"]["arena_live_count"] for row in rows]

    summary: dict[str, Any] = {
        "s_actor_command_count": distribution(command_counts),
        "s_actor_texture_count": distribution(texture_counts),
        "s_actor_draw_count_derived": distribution(draw_counts),
        "backend_list_count": distribution(list_counts),
        "backend_arena_live_count": distribution(live_counts),
        "arena_peak": rows[-1]["backend"]["arena_peak"] if rows else None,
        "arena_capacity": rows[-1]["backend"]["arena_capacity"] if rows else None,
    }
    if list_counts and max(list_counts) > 0:
        denominator = statistics.mean([value for value in list_counts if value > 0])
        summary["actor_share_of_published_commands"] = round(
            statistics.mean(command_counts) / denominator, 6
        )
        summary["double_emit_share_of_published_commands"] = round(
            statistics.mean(texture_counts) / denominator, 6
        )
        summary["denominator_mean_nonzero_list_count"] = round(denominator, 4)

    report = {
        "schema": "sm64-saturn-actor-command-share-v1",
        "elf": str(args.elf),
        "elf_sha256": hashlib.sha256(args.elf.read_bytes()).hexdigest(),
        "game": str(args.game),
        "identity": identity,
        "addresses": {
            "actor": hex(args.actor_address),
            "backend": hex(args.backend_address),
            "profile": hex(args.profile_address),
        },
        "sampling": {
            "startup_vblanks": args.startup_vblanks,
            "warmup": warmup,
            "samples": args.samples,
            "gap_vblanks": args.gap_vblanks,
        },
        "wall_seconds": round(time.time() - started, 1),
        "summary": summary,
        "rows": rows,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    print(f"actor command share: {len(rows)} samples, {report['wall_seconds']}s")
    for name in (
        "s_actor_command_count",
        "s_actor_texture_count",
        "s_actor_draw_count_derived",
        "backend_list_count",
    ):
        block = summary[name]
        if block.get("samples"):
            print(
                f"  {name}: mean {block['mean']} median {block['median']} "
                f"mode {block['mode']} min {block['min']} max {block['max']}"
            )
    print(f"  arena peak {summary['arena_peak']} / capacity {summary['arena_capacity']}")
    if "actor_share_of_published_commands" in summary:
        print(
            f"  actor share {summary['actor_share_of_published_commands']:.4%}, "
            f"double-emit share "
            f"{summary['double_emit_share_of_published_commands']:.4%}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
