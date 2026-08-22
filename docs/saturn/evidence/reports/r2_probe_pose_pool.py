#!/usr/bin/env python3
"""R2 pose-slot pooling: read the pool's on-target evidence out of guest LWRAM.

Adapted from `r3_probe_cf.py` (same worktree), which established the
boot/identity/warm-up sequence and the `mem.peek` decode of
`sm64_saturn_complete_actor_probe`.  The struct grew twelve fields; this tool
decodes them and turns them into named checks:

  * `pool_selftest_mask` -- the boot-time claim / release / reuse / exhaustion
    / disjointness sequence run against real LWRAM on the real SH-2;
  * `pool_frames_checked` / `pool_alias_faults` -- the per-tick witness that
    two simultaneously held cold slots never see each other's bytes;
  * the hot pool's occupancy and slot size, i.e. Mario's two generations
    really are served by the allocator rather than by a static array.

Nothing here writes guest state.  `mem.peek` does not advance the machine.
"""

from __future__ import annotations

import argparse
import json
import struct
import subprocess
import sys
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

PROBE_STRUCT = struct.Struct(">I4I4H4H6H12H")
PROBE_MAGIC = 0x53414E4D

SELFTEST_BITS = [
    ("hot_init", 1 << 0),
    ("cold_init", 1 << 1),
    ("mario_claimed", 1 << 2),
    ("mario_generations_disjoint", 1 << 3),
    ("two_tenants_claimed", 1 << 4),
    ("two_tenants_disjoint", 1 << 5),
    ("exhaustion_refuses", 1 << 6),
    ("exhaustion_harmless", 1 << 7),
    ("release_kills_handle", 1 << 8),
    ("reuse_ok", 1 << 9),
    ("oversize_refused", 1 << 10),
]
SELFTEST_ALL = 0
for _name, _bit in SELFTEST_BITS:
    SELFTEST_ALL |= _bit


def resolve_symbols(nm: Path, elf: Path, names: set[str]) -> dict[str, dict[str, int]]:
    out = subprocess.run(
        [str(nm), "--print-size", "--defined-only", str(elf)],
        capture_output=True, text=True, check=True,
    ).stdout
    found: dict[str, dict[str, int]] = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 4:
            addr, size, _kind, name = parts
            size_v = int(size, 16)
        elif len(parts) == 3:
            addr, _kind, name = parts
            size_v = 0
        else:
            continue
        if name in names:
            found[name] = {"address": int(addr, 16), "size": size_v}
    missing = names - set(found)
    if missing:
        raise SystemExit(f"symbols not found in {elf}: {sorted(missing)}")
    return found


def peek(client: Any, address: int, count: int) -> bytes:
    data = bytearray()
    while len(data) < count:
        chunk = min(64, count - len(data))
        result = client.call(
            "mem.peek", {"address": address + len(data), "count": chunk})
        values = result.get("data")
        if not isinstance(values, list) or len(values) != chunk:
            raise RuntimeError(
                f"short peek at 0x{address + len(data):08X}: "
                f"{len(values) if isinstance(values, list) else values!r}/{chunk}")
        data.extend(bytes(values))
    return bytes(data)


def decode_probe(raw: bytes) -> dict[str, Any]:
    f = PROBE_STRUCT.unpack(raw)
    mask = f[20]
    return {
        "magic": f[0],
        "magic_ok": f[0] == PROBE_MAGIC,
        "bank_animation_count": f[13],
        "bank_vertex_count": f[14],
        "staged_slices": f[15],
        "evaluate_calls": f[16],
        "fallback_hits": f[17],
        "last_fallback_requested_id": f[18],
        "pool_ready": f[19],
        "pool_selftest_mask": mask,
        "pool_selftest": {name: bool(mask & bit) for name, bit in SELFTEST_BITS},
        "pool_hot_live": f[21],
        "pool_hot_slot_bytes": f[22],
        "pool_cold_live": f[23],
        "pool_cold_peak_live": f[24],
        "pool_cold_claim_failures": f[25],
        "pool_cold_stale_resolves": f[26],
        "pool_cold_slot_count": f[27],
        "pool_frames_checked": f[28],
        "pool_alias_faults": f[29],
        "pool_cold_arena_bytes": f[30],
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--tools", type=Path, required=True)
    ap.add_argument("--ymir", type=Path, required=True)
    ap.add_argument("--ipl", type=Path, required=True)
    ap.add_argument("--game", type=Path, required=True)
    ap.add_argument("--elf", type=Path, required=True)
    ap.add_argument("--nm", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--start-tick", type=int, default=100)
    ap.add_argument("--end-tick", type=int, default=200)
    ap.add_argument("--startup-vblanks", type=int, default=4096)
    ap.add_argument("--timeout", type=float, default=3000.0)
    args = ap.parse_args()

    sys.path.insert(0, str(args.tools))
    from capture_route_views import YmirClient  # noqa: E402
    from capture_sourceboot_boot_trace import run_bios_handoff  # noqa: E402
    from capture_sourceboot_throughput import (  # noqa: E402
        build_elf_identity_probe, wait_for_target_identity,
    )
    import route_warmup  # noqa: E402

    game = args.game.resolve()
    elf = args.elf.resolve()

    syms = resolve_symbols(
        args.nm, elf,
        {"_sm64_saturn_complete_actor_probe", "_complete_actor_hot_arena",
         "_complete_actor_cold_arena", "_complete_actor_joint_workspace",
         "_complete_actor_hot_pool", "_complete_actor_cold_pool"},
    )
    probe_addr = syms["_sm64_saturn_complete_actor_probe"]["address"]
    probe_size = syms["_sm64_saturn_complete_actor_probe"]["size"]
    if probe_size < PROBE_STRUCT.size:
        raise SystemExit(
            f"probe symbol is {probe_size} B, expected >= {PROBE_STRUCT.size}")

    warm_addrs = route_warmup.resolve_warmup_symbols(elf)
    identity_probe = build_elf_identity_probe(elf)

    report: dict[str, Any] = {
        "schema": "sm64-saturn-r2-pose-pool-probe-v1",
        "created_utc": datetime.now(UTC).isoformat(),
        "elf": str(elf), "game": str(game), "symbols": syms,
        "status": "failed",
    }
    client = YmirClient(args.ymir, args.ipl, game, args.timeout, sh2_cache=False)
    try:
        run_bios_handoff(
            client, lambda frames: client.call("exec.run_for", {"frames": frames}),
            lambda _l: None,
        )
        report["target_identity"] = wait_for_target_identity(
            client, identity_probe, startup_vblanks=args.startup_vblanks)
        report["warmup"] = route_warmup.warm_up_to_replay_tick(
            client, warm_addrs, target_tick=args.start_tick)

        first = None
        probe = None
        last_tick = -1
        vblanks = 0
        while vblanks < 6000:
            pos = route_warmup.read_route_position(client, warm_addrs)
            tick = pos["replay_ticks"]
            if tick != last_tick:
                last_tick = tick
                probe = decode_probe(peek(client, probe_addr, PROBE_STRUCT.size))
                if first is None:
                    first = probe
                if tick >= args.end_tick:
                    break
            client.call("exec.run_for", {"frames": 1})
            vblanks += 1

        report["probe_first"] = first
        report["probe_last"] = probe
        report["vblanks_advanced"] = vblanks

        checks: dict[str, Any] = {}
        if probe is not None and first is not None:
            checks["probe_magic_ok"] = probe["magic_ok"]
            checks["pool_ready"] = probe["pool_ready"] == 1
            checks["pool_selftest_all_passed"] = (
                probe["pool_selftest_mask"] == SELFTEST_ALL)
            checks["hot_pool_serves_both_mario_generations"] = (
                probe["pool_hot_live"] == 2)
            checks["hot_slot_is_the_expected_size"] = (
                probe["pool_hot_slot_bytes"] == 3000)
            checks["two_cold_tenants_held"] = probe["pool_cold_live"] == 2
            checks["cold_pool_partitioned"] = probe["pool_cold_slot_count"] == 16
            checks["cold_arena_is_10240_bytes"] = (
                probe["pool_cold_arena_bytes"] == 10240)
            checks["no_alias_fault_all_session"] = probe["pool_alias_faults"] == 0
            checks["alias_check_actually_ran"] = probe["pool_frames_checked"] > 0
            checks["alias_check_advanced_over_the_route"] = (
                probe["pool_frames_checked"] > first["pool_frames_checked"])
            checks["alias_checks_match_evaluate_calls"] = (
                probe["pool_frames_checked"] == probe["evaluate_calls"])
            checks["exhaustion_was_exercised"] = (
                probe["pool_cold_claim_failures"] > 0)
            checks["stale_handle_was_rejected"] = (
                probe["pool_cold_stale_resolves"] > 0)
            checks["bridge_fallback_never_used"] = probe["fallback_hits"] == 0
            checks["lwram_slices_still_staged"] = probe["staged_slices"] == 1
            checks["evaluate_ran"] = probe["evaluate_calls"] > 0
        report["checks"] = checks
        report["status"] = ("complete" if checks and all(checks.values())
                            else "checks_failed")
        client.shutdown()
    except Exception as error:  # noqa: BLE001
        report["error"] = f"{type(error).__name__}: {error}"
        report["stderr"] = client.stderr[-4000:] if client else ""
        try:
            client.abort()
        except Exception:  # noqa: BLE001
            pass
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=1, sort_keys=True) + "\n",
                           encoding="utf-8")
    print(f"status={report['status']}")
    print(json.dumps(report.get("checks", {}), indent=1))
    print(json.dumps(report.get("probe_last", {}), indent=1))
    return 0 if report["status"] == "complete" else 1


if __name__ == "__main__":
    raise SystemExit(main())
