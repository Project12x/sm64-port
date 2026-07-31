"""Raw Phase A camera role-comparison tests.

Breaks caught: accepting a role or ELF marker mismatch, non-deterministic raw
windows, or collapsing bypass and fixed-candidate timing labels together.
"""
from __future__ import annotations

import hashlib
import struct
import unittest
from pathlib import Path

from compare_camera_route_reports import compare_camera_route_reports
from compare_route_reports import PROBE_FIELDS
from test_camera_idle_contract import build_scc1, mutate_every_sample_word

ROOT = Path(__file__).resolve().parents[2]
ROUTE = ROOT / "tools/saturn/routes/bob_default_camera_v1.json"
ROLE_IDS = {
    "camera-source-baseline": 1,
    "camera-bypass-diagnostic": 2,
    "camera-fixed-candidate": 3,
}


def sbr4(*, sim: int, reject: int = 0) -> dict:
    values = {name: 0 for name in PROBE_FIELDS}
    values.update(magic=0x53425234, version=4, atan2_variant=2, replay_ticks=2000,
                  global_timer=2000, mario_action=42, camera_mode=7,
                  triangles_transformed=4, triangles_emitted=4,
                  triangles_vdp1_emitted=4, reject_backface=reject,
                  sim_frt_ticks_accum=sim)
    raw = struct.pack(f">{len(PROBE_FIELDS)}I", *(values[name] for name in PROBE_FIELDS))
    return {"data": list(raw), "decoded": values}


def report(role: str, *, sim: int, marker: int | None = None,
           raw_scc1: bytes | None = None, reject: int = 0) -> dict:
    variant = ROLE_IDS[role]
    if marker is None:
        marker = variant
    if raw_scc1 is None:
        raw_scc1 = build_scc1(variant=variant, bridges=(0, 0), generation=0)
    return {
        "capture_role": role,
        "route_manifest_sha256": hashlib.sha256(ROUTE.read_bytes()).hexdigest(),
        "artifacts": {"elf": {"sha256": chr(96 + variant) * 64, "size": 1},
                      "image": {"sha256": chr(99 + variant) * 64, "size": 1}},
        "elf_camera_variant": marker,
        "elf_route_id": 2,
        "route_window": sbr4(sim=sim, reject=reject),
        "camera_idle_window": {"data": list(raw_scc1)},
    }


class CompareCameraRouteReportsTest(unittest.TestCase):
    def compare(self, candidate_role: str, baseline: list[dict], candidate: list[dict]) -> dict:
        return compare_camera_route_reports(
            baseline, candidate, candidate_role=candidate_role, route=ROUTE,
            require_sim_improvement=True,
        )

    def test_labels_bypass_and_fixed_candidates_distinctly(self) -> None:
        baseline = [report("camera-source-baseline", sim=100), report("camera-source-baseline", sim=101)]
        for candidate_role in ("camera-bypass-diagnostic", "camera-fixed-candidate"):
            with self.subTest(candidate_role=candidate_role):
                candidate = [report(candidate_role, sim=90), report(candidate_role, sim=91)]
                result = self.compare(candidate_role, baseline, candidate)
                self.assertTrue(result["deterministic"])
                self.assertEqual(result["baseline_role"], "camera-source-baseline")
                self.assertEqual(result["candidate_role"], candidate_role)
                self.assertEqual(result["pairs"][0]["sim_frt_ticks_delta"], -10)

    def test_rejects_unknown_role_role_order_and_elf_marker_mismatch(self) -> None:
        baseline = [report("camera-source-baseline", sim=100), report("camera-source-baseline", sim=101)]
        bypass = [report("camera-bypass-diagnostic", sim=90), report("camera-bypass-diagnostic", sim=91)]
        with self.assertRaises(ValueError):
            self.compare("camera-q", baseline, bypass)
        with self.assertRaises(ValueError):
            self.compare("camera-bypass-diagnostic", bypass, baseline)
        with self.assertRaises(ValueError):
            self.compare("camera-bypass-diagnostic", baseline,
                         [report("camera-bypass-diagnostic", sim=90, marker=3), bypass[1]])

    def test_rejects_same_role_sbr4_and_raw_scc1_mutation(self) -> None:
        baseline = [report("camera-source-baseline", sim=100), report("camera-source-baseline", sim=101)]
        fixed = [report("camera-fixed-candidate", sim=90), report("camera-fixed-candidate", sim=91)]
        with self.assertRaises(ValueError):
            self.compare("camera-fixed-candidate", [baseline[0], report("camera-source-baseline", sim=101, reject=1)], fixed)
        changed = mutate_every_sample_word(build_scc1(variant=3, generation=0), 4, 0x40000000)
        fixed[1]["camera_idle_window"]["data"] = list(changed)
        with self.assertRaises(ValueError):
            self.compare("camera-fixed-candidate", baseline, fixed)

    def test_rejects_candidate_that_does_not_improve_simulation(self) -> None:
        baseline = [report("camera-source-baseline", sim=100), report("camera-source-baseline", sim=101)]
        bypass = [report("camera-bypass-diagnostic", sim=100), report("camera-bypass-diagnostic", sim=91)]
        with self.assertRaises(ValueError):
            self.compare("camera-bypass-diagnostic", baseline, bypass)


if __name__ == "__main__":
    unittest.main()
