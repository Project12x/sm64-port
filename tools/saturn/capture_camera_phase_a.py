#!/usr/bin/env python3
"""Create a raw-first Phase A camera attribution report from three role pairs."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Mapping

from compare_camera_route_reports import compare_camera_route_reports
from compare_route_reports import decode_probe

ROLES = ("camera-source-baseline", "camera-bypass-diagnostic", "camera-fixed-candidate")


def _runs(payload: Any, role: str) -> list[dict[str, Any]]:
    if not isinstance(payload, list) or len(payload) != 2 or not all(isinstance(item, dict) for item in payload):
        raise ValueError(f"{role} requires exactly two capture reports")
    return list(payload)


def _diagnostics(report: Mapping[str, Any]) -> None:
    for name in ("unsupported_diagnostics", "camera_diagnostics"):
        value = report.get(name, 0)
        if value != 0:
            raise ValueError("capture contains unsupported diagnostics")


def _probe(report: Mapping[str, Any]) -> dict[str, int]:
    _diagnostics(report)
    probe = decode_probe({"probe_window": report.get("route_window")})
    required = ("camera_ticks_accum", "camera_ticks_last", "camera_invocations", "camera_ticks_max")
    if any(name not in probe for name in required):
        raise ValueError("raw SBR4 camera timing extension is absent")
    return probe


def _pair_value(reports: list[dict[str, Any]], field: str) -> int:
    # The paired comparison proves both raw runs are valid; use the first
    # paired observation for the headline arithmetic so its inputs are
    # directly traceable to one complete raw SBR4/SCC1 capture.
    return _probe(reports[0])[field]


def capture_camera_phase_a(role_runs: Mapping[str, Any]) -> dict[str, Any]:
    """Validate three exact role pairs and calculate their Phase A attribution."""
    if set(role_runs) != set(ROLES):
        raise ValueError("Phase A requires exactly source baseline, bypass, and fixed role pairs")
    pairs = {role: _runs(role_runs[role], role) for role in ROLES}
    digests = {run.get("route_manifest_sha256", run.get("route_manifest_digest"))
               for pair in pairs.values() for run in pair}
    if len(digests) != 1 or not isinstance(next(iter(digests)), str):
        raise ValueError("capture route-manifest digest differs between role pairs")

    route = Path(__file__).with_name("routes") / "bob_default_camera_v1.json"
    source_bypass = compare_camera_route_reports(pairs[ROLES[0]], pairs[ROLES[1]],
                                                  candidate_role=ROLES[1], route=route,
                                                  require_sim_improvement=False)
    source_fixed = compare_camera_route_reports(pairs[ROLES[0]], pairs[ROLES[2]],
                                                 candidate_role=ROLES[2], route=route,
                                                 require_sim_improvement=False)
    source_sim = _pair_value(pairs[ROLES[0]], "sim_frt_ticks_accum")
    bypass_sim = _pair_value(pairs[ROLES[1]], "sim_frt_ticks_accum")
    fixed_sim = _pair_value(pairs[ROLES[2]], "sim_frt_ticks_accum")
    source_camera = _pair_value(pairs[ROLES[0]], "camera_ticks_accum")
    fixed_camera = _pair_value(pairs[ROLES[2]], "camera_ticks_accum")
    if source_sim == 0 or source_camera == 0:
        raise ValueError("source baseline timing must be nonzero")
    maximum_opportunity = source_sim - bypass_sim
    realized_saving = source_sim - fixed_sim
    bypass_percent = 100.0 * maximum_opportunity / source_sim
    candidate_camera_percent = 100.0 * fixed_camera / source_camera
    decision_band = ("stop-replacement" if bypass_percent < 5.0 else
                     "justify-or-continue" if bypass_percent <= 10.0 else "proceed-phase-b")
    return {
        "roles": list(ROLES), "route_manifest_sha256": next(iter(digests)),
        "artifact_identities": [{"role": role, "elf": pairs[role][0]["artifacts"]["elf"],
                                  "image": pairs[role][0]["artifacts"]["image"]} for role in ROLES],
        "maximum_opportunity": maximum_opportunity, "realized_saving": realized_saving,
        "bypass_whole_sim_percent": bypass_percent,
        "candidate_camera_stage_percent": candidate_camera_percent,
        "decision_band": decision_band,
        "raw_comparisons": {"bypass": source_bypass, "fixed_candidate": source_fixed},
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_baseline", type=Path, help="JSON array containing exactly two source runs")
    parser.add_argument("bypass", type=Path, help="JSON array containing exactly two bypass runs")
    parser.add_argument("fixed_candidate", type=Path, help="JSON array containing exactly two fixed runs")
    parser.add_argument("--output", type=Path, default=Path("phase_a_attribution.json"))
    args = parser.parse_args(argv)
    try:
        result = capture_camera_phase_a({role: json.loads(path.read_text(encoding="utf-8")) for role, path in
                                         zip(ROLES, (args.source_baseline, args.bypass, args.fixed_candidate), strict=True)})
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(str(error))
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
