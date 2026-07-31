#!/usr/bin/env python3
"""Compare four raw camera-route reports without changing the legacy SBR4 gate."""
from __future__ import annotations

import hashlib
import json
import argparse
from pathlib import Path
from typing import Any, Mapping

from camera_idle_contract import ROLE_IDS, Scc1Capture, Scc1Error, compare_same_role, validate_scc1
from compare_route_reports import PROBE_FIELDS, SHA256_RE, decode_probe, load_route, validate_artifacts
from verify_camera_idle_capture import decode_capture_report

TIMING_FIELDS = frozenset({"frame_serial", "sim_frt_ticks_accum", "render_frt_ticks_accum",
    "render_frt_ticks_last", "master_wait_ticks", "slave_busy_ticks", "slave_jobs_completed", "slave_timeouts",
    "camera_ticks_last", "camera_ticks_accum", "camera_invocations", "camera_ticks_max"})
SBR4_BEHAVIOR_FIELDS = tuple(field for field in PROBE_FIELDS if field not in TIMING_FIELDS)


def _route(route: Path | Mapping[str, Any]) -> tuple[dict[str, Any], str | None]:
    if isinstance(route, Path):
        return json.loads(route.read_text(encoding="utf-8")), hashlib.sha256(route.read_bytes()).hexdigest()
    if isinstance(route, Mapping):
        digest = route.get("manifest_sha256") or route.get("route_manifest_sha256")
        if not isinstance(digest, str) or SHA256_RE.fullmatch(digest.lower()) is None:
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


def _camera_capture(report: Mapping[str, Any], role: str, route_id: int) -> Scc1Capture:
    try:
        capture = decode_capture_report(dict(report))
        validate_scc1(capture, expected_role=role, expected_idle_start_tick=capture.header[10],
                      expected_route_id=route_id)
    except Scc1Error as error:
        raise ValueError(str(error)) from error
    return capture


def _validate_report(report: Mapping[str, Any], *, role: str, marker: int,
                     route: Mapping[str, Any], route_digest: str,
                     ) -> tuple[dict[str, int], Mapping[str, Any], Scc1Capture]:
    if report.get("capture_role") != role:
        raise ValueError(f"capture role must be {role}")
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
    if probe["replay_ticks"] != 2000:
        raise ValueError("raw SBR4 replay tick must equal the frozen 2000-tick anchor")
    capture = _camera_capture(report, role, int(route.get("route_id", 2)))
    return probe, report["artifacts"], capture


def _same_role(first: dict[str, int], second: dict[str, int], role: str) -> None:
    for field in SBR4_BEHAVIOR_FIELDS:
        if first[field] != second[field]:
            raise ValueError(f"{role} raw SBR4 non-timing field {field} differs between runs")


def _cross_role(baseline: dict[str, int], candidate: dict[str, int], pair: int,
                require_sim_improvement: bool) -> dict[str, Any]:
    if require_sim_improvement and candidate["sim_frt_ticks_accum"] >= baseline["sim_frt_ticks_accum"]:
        raise ValueError(f"run pair {pair}: candidate simulation ticks did not strictly improve")
    return {
        "baseline_sim_frt_ticks_accum": baseline["sim_frt_ticks_accum"],
        "candidate_sim_frt_ticks_accum": candidate["sim_frt_ticks_accum"],
        "sim_frt_ticks_delta": candidate["sim_frt_ticks_accum"] - baseline["sim_frt_ticks_accum"],
        "timing_deltas": {field: candidate[field] - baseline[field] for field in TIMING_FIELDS},
    }


def compare_camera_route_reports(baseline_runs: list[dict[str, Any]], candidate_runs: list[dict[str, Any]], *,
                                 candidate_role: str, route: Path | Mapping[str, Any],
                                 require_sim_improvement: bool) -> dict[str, Any]:
    """Raw-decode deterministic source-baseline and one candidate role pair."""
    baseline_role = "camera-source-baseline"
    if len(baseline_runs) != 2 or len(candidate_runs) != 2:
        raise ValueError("camera comparison requires exactly two baseline and two candidate runs")
    if candidate_role not in ("camera-bypass-diagnostic", "camera-fixed-candidate"):
        raise ValueError("candidate_role must be a Phase A candidate role")
    manifest, digest = _route(route)
    if (manifest.get("route_version") != "bob-default-camera-v1" or manifest.get("route_id") != 2
            or manifest.get("checkpoint_tick") != 2000):
        raise ValueError("selected route is not bob-default-camera-v1")
    reports = [*baseline_runs, *candidate_runs]
    decoded = [_validate_report(item, role=role, marker=marker, route=manifest, route_digest=digest,
                                )
               for item, role, marker in zip(reports, (baseline_role, baseline_role, candidate_role, candidate_role),
                                             (ROLE_IDS[baseline_role], ROLE_IDS[baseline_role], ROLE_IDS[candidate_role], ROLE_IDS[candidate_role]), strict=True)]
    probes = [item[0] for item in decoded]
    artifacts = [item[1] for item in decoded]
    captures = [item[2] for item in decoded]
    for first, second, role in ((0, 1, baseline_role), (2, 3, candidate_role)):
        for name in ("elf", "image"):
            if artifacts[first][name]["sha256"] != artifacts[second][name]["sha256"]:
                raise ValueError(f"{role} {name} identities differ between deterministic runs")
    for name in ("elf", "image"):
        for pair in ((0, 2), (1, 3)):
            if artifacts[pair[0]][name]["sha256"] == artifacts[pair[1]][name]["sha256"]:
                raise ValueError(f"baseline and candidate {name} identities must differ in each pair")
    _same_role(probes[0], probes[1], baseline_role)
    _same_role(probes[2], probes[3], candidate_role)
    compare_same_role(captures[0], captures[1])
    compare_same_role(captures[2], captures[3])
    return {"deterministic": True, "route_version": manifest["route_version"],
            "baseline_role": baseline_role, "candidate_role": candidate_role,
            "require_sim_improvement": require_sim_improvement,
            "pairs": [_cross_role(probes[0], probes[2], 1, require_sim_improvement),
                      _cross_role(probes[1], probes[3], 2, require_sim_improvement)]}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline_run1", type=Path)
    parser.add_argument("baseline_run2", type=Path)
    parser.add_argument("candidate_run1", type=Path)
    parser.add_argument("candidate_run2", type=Path)
    parser.add_argument("--route", type=Path, required=True)
    parser.add_argument("--require-sim-improvement", action="store_true")
    parser.add_argument("--candidate-role", choices=("camera-bypass-diagnostic", "camera-fixed-candidate"), required=True)
    args = parser.parse_args(argv)
    try:
        reports = [json.loads(path.read_text(encoding="utf-8")) for path in
                   (args.baseline_run1, args.baseline_run2, args.candidate_run1, args.candidate_run2)]
        result = compare_camera_route_reports(reports[:2], reports[2:], candidate_role=args.candidate_role,
                                              route=args.route, require_sim_improvement=args.require_sim_improvement)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
