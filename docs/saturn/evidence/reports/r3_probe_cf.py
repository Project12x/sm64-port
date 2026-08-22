#!/usr/bin/env python3
"""R3 C/F: read the shipped reduced bake and the LWRAM staging out of guest RAM.

Reads `sm64_saturn_complete_actor_probe` (`.lwram_bss`, filled once when the
bank is validated) and, per replay tick over a route span, the *requested*
animation id from `sourceboot_mario_snapshot`.  Answers four questions with
target memory rather than with source:

  * is the shipped bank the reduced one (`bank_animation_count` is still 209,
    but an unbaked id's record aliases its neighbour's value stream);
  * did the LWRAM staging of the invariant vertex/joint slices succeed
    (`staged_slices`);
  * did the per-frame path ever need the bridge's last-resort fallback
    (`fallback_hits`, against `evaluate_calls`);
  * which animation ids the route actually requests, so "the route is inside
    the baked set" is measured and not assumed.

Nothing here writes guest state.  `mem.peek` does not advance the machine.
"""

from __future__ import annotations

import argparse
import json
import struct
import subprocess
import sys
from collections import Counter
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

PROBE_STRUCT = struct.Struct(">I4I4H4H6H")
PROBE_MAGIC = 0x53414E4D


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
    fields = PROBE_STRUCT.unpack(raw)
    return {
        "magic": fields[0],
        "magic_ok": fields[0] == PROBE_MAGIC,
        "probe_values_offset": list(fields[1:5]),
        "probe_animation_id": list(fields[5:9]),
        "probe_frame_count": list(fields[9:13]),
        "bank_animation_count": fields[13],
        "bank_vertex_count": fields[14],
        "staged_slices": fields[15],
        "evaluate_calls": fields[16],
        "fallback_hits": fields[17],
        "last_fallback_requested_id": fields[18],
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--tools", type=Path, required=True)
    ap.add_argument("--ymir", type=Path, required=True)
    ap.add_argument("--ipl", type=Path, required=True)
    ap.add_argument("--game", type=Path, required=True)
    ap.add_argument("--elf", type=Path, required=True)
    ap.add_argument("--nm", type=Path, required=True)
    ap.add_argument("--bank-report", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--start-tick", type=int, default=100)
    ap.add_argument("--end-tick", type=int, default=240)
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
    bank = json.loads(args.bank_report.read_text(encoding="utf-8"))
    bank_records = {int(item["animation_id"]): item for item in bank["animations"]}

    syms = resolve_symbols(
        args.nm, elf,
        {"_sourceboot_mario_snapshot", "_sm64_saturn_complete_actor_probe"},
    )
    snapshot_addr = syms["_sourceboot_mario_snapshot"]["address"]
    probe_addr = syms["_sm64_saturn_complete_actor_probe"]["address"]
    probe_size = syms["_sm64_saturn_complete_actor_probe"]["size"]
    if probe_size < PROBE_STRUCT.size:
        raise SystemExit(f"probe symbol is {probe_size} B, expected >= {PROBE_STRUCT.size}")

    warm_addrs = route_warmup.resolve_warmup_symbols(elf)
    identity_probe = build_elf_identity_probe(elf)

    report: dict[str, Any] = {
        "schema": "sm64-saturn-r3-cf-probe-v1",
        "created_utc": datetime.now(UTC).isoformat(),
        "elf": str(elf), "game": str(game), "symbols": syms,
        "bank_payload_sha256": bank.get("payload_sha256"),
        "bank_selection": bank.get("animation_selection", {}).get("policy"),
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

        requested = Counter()
        last_tick = -1
        vblanks = 0
        probe = None
        while vblanks < 6000:
            pos = route_warmup.read_route_position(client, warm_addrs)
            tick = pos["replay_ticks"]
            if tick != last_tick:
                last_tick = tick
                req_id, _req_frame = struct.unpack(
                    ">hh", peek(client, snapshot_addr + 44, 4))
                requested[req_id] += 1
                probe = decode_probe(peek(client, probe_addr, PROBE_STRUCT.size))
                if tick >= args.end_tick:
                    break
            client.call("exec.run_for", {"frames": 1})
            vblanks += 1

        report["probe"] = probe
        report["requested_animation_ids"] = {
            str(key): value for key, value in sorted(requested.items())}
        report["vblanks_advanced"] = vblanks

        checks: dict[str, Any] = {}
        if probe is not None:
            checks["probe_magic_ok"] = probe["magic_ok"]
            checks["lwram_slices_staged"] = probe["staged_slices"] == 1
            checks["bridge_fallback_never_used"] = probe["fallback_hits"] == 0
            checks["evaluate_ran"] = probe["evaluate_calls"] > 0
            # Every probed id must agree with the shipped bank report, and the
            # unbaked id must alias the baked neighbour's value stream.
            agreement = []
            for index, animation_id in enumerate(probe["probe_animation_id"]):
                record = bank_records.get(animation_id, {})
                agreement.append({
                    "animation_id": animation_id,
                    "enum_name": record.get("enum_name"),
                    "baked": record.get("baked"),
                    "substitute_animation_id": record.get("substitute_animation_id"),
                    "target_frame_count": probe["probe_frame_count"][index],
                    "report_frame_count": record.get("frame_count"),
                    "target_values_offset": probe["probe_values_offset"][index],
                    "report_values_offset": record.get("values_offset"),
                    "match": (probe["probe_frame_count"][index] == record.get("frame_count")
                              and probe["probe_values_offset"][index]
                              == record.get("values_offset")),
                })
            report["probe_agreement"] = agreement
            checks["target_bank_matches_report"] = all(
                item["match"] for item in agreement)
            checks["unbaked_id_has_frames_on_target"] = all(
                item["target_frame_count"] > 0 for item in agreement)
            unbaked = [item for item in agreement if item["baked"] is False]
            checks["unbaked_id_aliases_its_substitute"] = bool(unbaked) and all(
                item["target_values_offset"]
                == bank_records[item["substitute_animation_id"]]["values_offset"]
                for item in unbaked)
        checks["route_ids_are_all_baked"] = all(
            bank_records.get(int(key), {}).get("baked") is True
            for key in report["requested_animation_ids"])
        report["checks"] = checks
        report["status"] = "complete" if all(checks.values()) else "checks_failed"
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
    print(json.dumps(report.get("probe", {}), indent=1))
    print(json.dumps(report.get("requested_animation_ids", {}), indent=1))
    return 0 if report["status"] == "complete" else 1


if __name__ == "__main__":
    raise SystemExit(main())
