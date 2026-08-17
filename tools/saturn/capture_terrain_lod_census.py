#!/usr/bin/env python3
"""Terrain/actor command split and LOD tier census on the shipped route.

Sprint 2 T2.19c.  `capture_route_counters.py` (T2.14) decodes only the first
128 bytes of `sm64_saturn_fast3d_profile_t`, which stops one field short of
every counter that separates terrain from actor emission.  Every counter this
tool needs already exists in the shipped product ELF and is already
incremented on the product path -- they simply sit past byte 128 and no
instrument has ever read them.

So this is a pure *reader* change: no target source is modified, no rebuild is
required, and the numbers describe an already-released artifact.  It reuses
`capture_route_counters.py`'s Ymir client, BIOS handoff, identity gate and
per-`frame_serial` delta machinery unchanged.

What it answers that T2.14 could not:

  * `demo_bob_results_master + demo_bob_results_slave` is the terrain command
    count for the frame (one record == one VDP1 command; see
    `saturn_demo_render.c:2856`, where the only fan-out is the pentagon split
    that yields 2).  `demo_actor_primitives_emitted` is the actor count.
    Their ratio replaces the "terrain is ~82% of commands" figure, which was
    only ever `1 - actor/total` from T2.8 and therefore lumped HUD, Mario and
    sky into "terrain".
  * `demo_lod_tier_near/_mid/_far` give the per-frame tier histogram, so the
    saving of any tier-indexed rule is arithmetic on measured populations
    rather than an assertion.
  * `demo_lod_primitives_suppressed` / `demo_lod_texture_downgrades` measure
    what the existing LOD actually does today.

Offsets were obtained by parsing the struct declaration in
`src/port/saturn/gfx/saturn_fast3d_frontend.h` with C alignment rules; the six
fields T2.14 already recorded (0, 4, 44, 48, 80, 84) reproduce exactly, which
is the cross-check that the parse is right.  Both ABIs are 32-bit with natural
alignment and the struct has no pointer members, so host offsets equal SH-2
offsets.
"""
from __future__ import annotations

import argparse
import json
import statistics
import sys
import time
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))

from capture_route_counters import decode_u32, peek  # noqa: E402
from capture_route_views import YmirClient  # noqa: E402
from capture_sourceboot_boot_trace import run_bios_handoff  # noqa: E402
from capture_sourceboot_throughput import (  # noqa: E402
    build_elf_identity_probe,
    wait_for_target_identity,
)

# Offsets within sm64_saturn_fast3d_profile_t.  All uint32_t.
PROFILE_U32: dict[str, int] = {
    "frame_serial": 0,
    "command_count": 4,
    "triangles_transformed": 44,
    "triangles_emitted": 48,
    "triangles_vdp1_emitted": 80,
    "reject_vdp1_arena_capacity": 84,
    "demo_actor_primitives_emitted": 268,
    "vdp1_commands_last": 300,
    "demo_bob_primitives_visible": 316,
    "demo_bob_primitives_near_rejected": 324,
    "demo_bob_primitives_spatial_dropped": 352,
    "demo_bob_clip_to_two": 364,
    "demo_bob_clip_recovery": 368,
    "demo_bob_results_master": 376,
    "demo_bob_results_slave": 380,
    "demo_lod_tier_near": 416,
    "demo_lod_tier_mid": 420,
    "demo_lod_tier_far": 424,
    "demo_lod_transitions": 428,
    "demo_lod_primitives_suppressed": 432,
    "demo_lod_texture_downgrades": 436,
    "flat_primitives": 444,
    "gouraud_primitives": 448,
    "vdp1_commands": 476,
    "pipeline_faults": 484,
}
PROFILE_BYTES = 672


def rates(rows: list[dict[str, Any]], field: str) -> dict[str, float]:
    """Per-frame_serial deltas of a cumulative counter."""
    per: list[float] = []
    for first, second in zip(rows, rows[1:]):
        frames = second["profile"]["frame_serial"] - first["profile"]["frame_serial"]
        if frames <= 0:
            continue
        delta = second["profile"][field] - first["profile"][field]
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
        "--profile-address", required=True,
        help="address of sourceboot_fast3d (hex or decimal)")
    parser.add_argument("--startup-vblanks", type=int, default=4096)
    parser.add_argument("--warmup-vblanks", type=int, default=1800)
    parser.add_argument("--samples", type=int, default=120)
    parser.add_argument("--gap-vblanks", type=int, default=11)
    parser.add_argument("--timeout", type=float, default=2400.0)
    args = parser.parse_args(argv)

    profile_address = int(str(args.profile_address), 0)
    probe = build_elf_identity_probe(args.elf)
    started = time.time()
    rows: list[dict[str, Any]] = []

    client = YmirClient(args.ymir, args.ipl, args.game, args.timeout)
    try:
        run_bios_handoff(
            client,
            lambda frames: client.call("exec.run_for", {"frames": frames}),
            lambda _label: None,
        )
        identity = wait_for_target_identity(
            client, probe, startup_vblanks=args.startup_vblanks)
        if not identity.get("match"):
            print("identity mismatch; refusing to report", file=sys.stderr)
            return 2
        # exec.run_for silently no-ops above Ymir's 3600-frame cap, so the
        # warm-up is chunked.  See the retracted "Boo hang" investigation.
        remaining = args.warmup_vblanks
        while remaining > 0:
            chunk = min(remaining, 3600)
            client.call("exec.run_for", {"frames": chunk})
            remaining -= chunk
        for index in range(args.samples):
            rows.append({
                "sample": index,
                "profile": decode_u32(
                    peek(client, profile_address, PROFILE_BYTES), PROFILE_U32),
            })
            client.call("exec.run_for", {"frames": args.gap_vblanks})
    finally:
        try:
            client.shutdown()
        except Exception:  # pragma: no cover - best-effort teardown
            pass

    per_frame = {name: rates(rows, name) for name in PROFILE_U32 if name != "frame_serial"}

    def mean_of(name: str) -> float:
        return float(per_frame.get(name, {}).get("mean", 0.0))

    terrain = mean_of("demo_bob_results_master") + mean_of("demo_bob_results_slave")
    actor = mean_of("demo_actor_primitives_emitted")
    total = mean_of("triangles_vdp1_emitted")
    derived = {
        "terrain_commands_per_frame": round(terrain, 4),
        "actor_commands_per_frame": round(actor, 4),
        "vdp1_emissions_per_frame": round(total, 4),
        "terrain_share_of_emissions":
            round(terrain / total, 6) if total else None,
        "actor_share_of_emissions":
            round(actor / total, 6) if total else None,
        "unattributed_per_frame": round(total - terrain - actor, 4),
    }
    tier_total = (mean_of("demo_lod_tier_near") + mean_of("demo_lod_tier_mid")
                  + mean_of("demo_lod_tier_far"))
    derived["lod_tier_population_per_frame"] = round(tier_total, 4)
    if tier_total:
        derived["lod_tier_share"] = {
            tier: round(mean_of(f"demo_lod_tier_{tier}") / tier_total, 6)
            for tier in ("near", "mid", "far")
        }

    report = {
        "schema": "sm64-saturn-terrain-lod-census-v1",
        "elf": str(args.elf),
        "game": str(args.game),
        "identity": identity,
        "profile_address": f"{profile_address:#010x}",
        "sampling": {
            "startup_vblanks": args.startup_vblanks,
            "warmup_vblanks": args.warmup_vblanks,
            "samples": args.samples,
            "gap_vblanks": args.gap_vblanks,
        },
        "wall_seconds": round(time.time() - started, 1),
        "per_frame": per_frame,
        "derived": derived,
        "rows": rows,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(derived, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
