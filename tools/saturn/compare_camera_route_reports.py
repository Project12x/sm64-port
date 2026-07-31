#!/usr/bin/env python3
"""Compare four raw camera-route reports without changing the legacy SBR4 gate."""
from __future__ import annotations

import hashlib
import json
import math
import struct
import argparse
from pathlib import Path
from typing import Any, Mapping

from camera_idle_contract import (Scc1Capture, Scc1Error, compare_camera_roles,
                                  compare_same_role, validate_scc1)
from compare_route_reports import (EXACT_BEHAVIOR_FIELDS, PROBE_FIELDS, REJECT_FIELDS,
                                   decode_probe, load_route, validate_artifacts)
from verify_camera_idle_capture import decode_capture_report

TIMING_FIELDS = frozenset({"frame_serial", "sim_frt_ticks_accum", "render_frt_ticks_accum",
    "render_frt_ticks_last", "master_wait_ticks", "slave_busy_ticks", "slave_jobs_completed", "slave_timeouts"})
SBR4_BEHAVIOR_FIELDS = tuple(field for field in PROBE_FIELDS if field not in TIMING_FIELDS)
EXACT_CROSS_FIELDS = (frozenset(EXACT_BEHAVIOR_FIELDS) - {
    "camera_pos_x_bits", "camera_pos_y_bits", "camera_pos_z_bits"}) | frozenset(REJECT_FIELDS) | {
    "fault_flags", "triangles_transformed", "triangles_emitted", "triangles_vdp1_emitted"}


def _route(route: Path | Mapping[str, Any]) -> tuple[dict[str, Any], str | None]:
    if isinstance(route, Path):
        return json.loads(route.read_text(encoding="utf-8")), hashlib.sha256(route.read_bytes()).hexdigest()
    if isinstance(route, Mapping):
        digest = route.get("manifest_sha256") or route.get("route_manifest_sha256")
        if not isinstance(digest, str) or len(digest) != 64:
            raise ValueError("a mapping route requires an explicit manifest sha256")
        return dict(route), digest.lower()
    raise ValueError("camera route must be a manifest path or object")


def _marker(report: Mapping[str, Any], name: str) -> Any:
    if name in report:
        return report[name]
    markers = report.get("elf_markers")
    return markers.get(name.removeprefix("elf_")) if isinstance(markers, Mapping) else None


def _route_probe(report: Mapping[str, Any]) -> dict[str, int]:
    window = report.get("route_window")
    if not isinstance(window, dict):
        raise ValueError("capture is missing raw SBR4 bytes")
    return decode_probe({"probe_window": window})


def _float(bits: int) -> float:
    value = struct.unpack(">f", bits.to_bytes(4, "big"))[0]
    if not math.isfinite(value):
        raise ValueError("SBR4 position is non-finite")
    return value


def _camera_capture(report: Mapping[str, Any], role: str, route_id: int,
                    expected_q_bridge_counts: tuple[int, int]) -> Scc1Capture:
    try:
        capture = decode_capture_report(dict(report))
        expected_bridges = (0, 0) if role == "camera-baseline" else expected_q_bridge_counts
        validate_scc1(capture, expected_role=role, expected_idle_start_tick=capture.header[10],
                      expected_route_id=route_id, expected_bridge_counts=expected_bridges)
    except Scc1Error as error:
        raise ValueError(str(error)) from error
    return capture


def _validate_report(report: Mapping[str, Any], *, role: str, marker: int,
                     route: Mapping[str, Any], route_digest: str,
                     expected_q_bridge_counts: tuple[int, int]) -> tuple[dict[str, int], Mapping[str, Any], Scc1Capture]:
    if report.get("capture_role") != role:
        raise ValueError(f"capture roles must be ordered camera-baseline/camera-q ({role} expected)")
    validate_artifacts(dict(report), role)
    if _marker(report, "elf_camera_variant") != marker:
        raise ValueError(f"{role} ELF camera marker must be {marker}")
    if _marker(report, "elf_route_id") != route.get("route_id"):
        raise ValueError("ELF route marker does not match the selected manifest")
    if report.get("route_manifest_sha256", report.get("route_manifest_digest")) != route_digest:
        raise ValueError("capture route-manifest digest does not match the selected manifest")
    probe = _route_probe(report)
    if probe["atan2_variant"] != 2:
        raise ValueError(f"{role} raw SBR4 atan2 variant must be 2")
    if probe["replay_ticks"] != route.get("checkpoint_tick", 2000):
        raise ValueError("raw SBR4 replay tick does not match route anchor")
    capture = _camera_capture(report, role, int(route.get("route_id", 2)), expected_q_bridge_counts)
    return probe, report["artifacts"], capture


def _same_role(first: dict[str, int], second: dict[str, int], role: str) -> None:
    for field in SBR4_BEHAVIOR_FIELDS:
        if first[field] != second[field]:
            raise ValueError(f"{role} raw SBR4 non-timing field {field} differs between runs")


def _cross_role(baseline: dict[str, int], q_variant: dict[str, int], pair: int,
                require_sim_improvement: bool) -> dict[str, Any]:
    for field in EXACT_CROSS_FIELDS:
        if baseline[field] != q_variant[field]:
            raise ValueError(f"run pair {pair}: raw SBR4 field {field} differs")
    divergences: dict[str, float] = {}
    for prefix in ("mario_pos", "camera_pos"):
        divergence = max(abs(_float(baseline[f"{prefix}_{axis}_bits"]) - _float(q_variant[f"{prefix}_{axis}_bits"]))
                         for axis in ("x", "y", "z"))
        if not divergence < 1.0:
            raise ValueError(f"run pair {pair}: {prefix} divergence is not below one world unit")
        divergences[prefix] = divergence
    if require_sim_improvement and q_variant["sim_frt_ticks_accum"] >= baseline["sim_frt_ticks_accum"]:
        raise ValueError(f"run pair {pair}: Q simulation ticks did not strictly improve")
    return {"mario_position_linf": divergences["mario_pos"], "camera_position_linf": divergences["camera_pos"],
            "baseline_sim_frt_ticks_accum": baseline["sim_frt_ticks_accum"],
            "q_sim_frt_ticks_accum": q_variant["sim_frt_ticks_accum"],
            "timing": {field: (baseline[field], q_variant[field]) for field in TIMING_FIELDS}}


def compare_camera_route_reports(baseline_runs: list[dict[str, Any]], q_runs: list[dict[str, Any]],
                                 route: Path | Mapping[str, Any], require_sim_improvement: bool, *,
                                 q_fraction_bits: int,
                                 expected_q_bridge_counts: tuple[int, int]) -> dict[str, Any]:
    """Raw-decode ordered baseline run1/run2 and Q run1/run2 reports."""
    if len(baseline_runs) != 2 or len(q_runs) != 2:
        raise ValueError("camera comparison requires exactly baseline run1/run2 and Q run1/run2")
    if not isinstance(q_fraction_bits, int) or q_fraction_bits < 0:
        raise ValueError("q_fraction_bits must be a nonnegative integer")
    if (not isinstance(expected_q_bridge_counts, tuple) or len(expected_q_bridge_counts) != 2
            or not all(isinstance(count, int) and count > 0 for count in expected_q_bridge_counts)):
        raise ValueError("expected Q bridge counts must be a nonzero pair")
    manifest, digest = _route(route)
    if manifest.get("route_version") != "bob-default-camera-v1" or manifest.get("route_id") != 2:
        raise ValueError("selected route is not bob-default-camera-v1")
    reports = [*baseline_runs, *q_runs]
    decoded = [_validate_report(item, role=role, marker=marker, route=manifest, route_digest=digest,
                                expected_q_bridge_counts=expected_q_bridge_counts)
               for item, role, marker in zip(reports, ("camera-baseline", "camera-baseline", "camera-q", "camera-q"),
                                             (1, 1, 2, 2), strict=True)]
    probes = [item[0] for item in decoded]
    artifacts = [item[1] for item in decoded]
    captures = [item[2] for item in decoded]
    for first, second, role in ((0, 1, "camera-baseline"), (2, 3, "camera-q")):
        for name in ("elf", "image"):
            if artifacts[first][name]["sha256"] != artifacts[second][name]["sha256"]:
                raise ValueError(f"{role} {name} identities differ between deterministic runs")
    for name in ("elf", "image"):
        for pair in ((0, 2), (1, 3)):
            if artifacts[pair[0]][name]["sha256"] == artifacts[pair[1]][name]["sha256"]:
                raise ValueError(f"baseline and Q {name} identities must differ in each pair")
    _same_role(probes[0], probes[1], "camera-baseline")
    _same_role(probes[2], probes[3], "camera-q")
    compare_same_role(captures[0], captures[1])
    compare_same_role(captures[2], captures[3])
    compare_camera_roles(captures[0], captures[2], q_fraction_bits=q_fraction_bits)
    compare_camera_roles(captures[1], captures[3], q_fraction_bits=q_fraction_bits)
    return {"deterministic": True, "route_version": manifest["route_version"],
            "require_sim_improvement": require_sim_improvement,
            "pairs": [_cross_role(probes[0], probes[2], 1, require_sim_improvement),
                      _cross_role(probes[1], probes[3], 2, require_sim_improvement)]}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline_run1", type=Path)
    parser.add_argument("baseline_run2", type=Path)
    parser.add_argument("q_run1", type=Path)
    parser.add_argument("q_run2", type=Path)
    parser.add_argument("--route", type=Path, required=True)
    parser.add_argument("--require-sim-improvement", action="store_true")
    parser.add_argument("--q-fraction-bits", type=int, required=True)
    parser.add_argument("--q-bridge-export", type=int, required=True)
    parser.add_argument("--q-bridge-import", type=int, required=True)
    args = parser.parse_args(argv)
    try:
        reports = [json.loads(path.read_text(encoding="utf-8")) for path in
                   (args.baseline_run1, args.baseline_run2, args.q_run1, args.q_run2)]
        result = compare_camera_route_reports(reports[:2], reports[2:], args.route,
                                              args.require_sim_improvement,
                                              q_fraction_bits=args.q_fraction_bits,
                                              expected_q_bridge_counts=(args.q_bridge_export, args.q_bridge_import))
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
