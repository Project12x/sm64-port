"""Host contract for the raw-first Phase A attribution adapter.

Breaks caught: accepting incomplete role pairs, a mismatched route identity,
or reporting the wrong simulation/camera-stage savings.
"""
from __future__ import annotations

import unittest

from capture_camera_phase_a import capture_camera_phase_a
from test_compare_camera_route_reports import report


def pair(role: str, *, sim: int, camera: int) -> list[dict]:
    runs = [report(role, sim=sim), report(role, sim=sim + 1)]
    for run_ in runs:
        run_["route_window"] = dict(run_["route_window"])
        decoded = dict(run_["route_window"]["decoded"])
        decoded.update(camera_ticks_accum=camera, camera_ticks_last=7,
                       camera_invocations=2000, camera_ticks_max=9)
        import struct
        from compare_route_reports import PROBE_FIELDS
        run_["route_window"]["data"] = list(struct.pack(
            f">{len(PROBE_FIELDS)}I", *(decoded[name] for name in PROBE_FIELDS)))
        run_["route_window"]["decoded"] = decoded
    return runs


class CaptureCameraPhaseATest(unittest.TestCase):
    def attribution(self, *, source_sim: int = 1000, bypass_sim: int = 940,
                    fixed_sim: int = 960, source_camera: int = 500,
                    fixed_camera: int = 200) -> dict:
        return capture_camera_phase_a({
            "camera-source-baseline": pair("camera-source-baseline", sim=source_sim, camera=source_camera),
            "camera-bypass-diagnostic": pair("camera-bypass-diagnostic", sim=bypass_sim, camera=0),
            "camera-fixed-candidate": pair("camera-fixed-candidate", sim=fixed_sim, camera=fixed_camera),
        })

    def test_emits_exact_role_order_identities_and_hand_calculated_metrics(self) -> None:
        result = self.attribution()
        self.assertEqual(result["roles"], ["camera-source-baseline", "camera-bypass-diagnostic", "camera-fixed-candidate"])
        self.assertEqual(result["maximum_opportunity"], 60)
        self.assertEqual(result["realized_saving"], 40)
        self.assertEqual(result["bypass_whole_sim_percent"], 6.0)
        self.assertEqual(result["candidate_camera_stage_percent"], 40.0)
        self.assertEqual(result["decision_band"], "justify-or-continue")
        self.assertEqual(len(result["artifact_identities"]), 3)

    def test_classifies_stop_justify_and_proceed_bands(self) -> None:
        self.assertEqual(self.attribution(bypass_sim=960)["decision_band"], "stop-replacement")
        self.assertEqual(self.attribution(bypass_sim=940)["decision_band"], "justify-or-continue")
        self.assertEqual(self.attribution(bypass_sim=890)["decision_band"], "proceed-phase-b")

    def test_rejects_missing_pair_mismatched_manifest_and_unsupported_diagnostics(self) -> None:
        runs = {
            "camera-source-baseline": pair("camera-source-baseline", sim=1000, camera=500),
            "camera-bypass-diagnostic": pair("camera-bypass-diagnostic", sim=940, camera=0),
            "camera-fixed-candidate": pair("camera-fixed-candidate", sim=960, camera=200),
        }
        with self.assertRaisesRegex(ValueError, "exactly two"):
            capture_camera_phase_a({**runs, "camera-fixed-candidate": runs["camera-fixed-candidate"][:1]})
        changed = {role: list(items) for role, items in runs.items()}
        changed["camera-bypass-diagnostic"][1] = dict(changed["camera-bypass-diagnostic"][1])
        changed["camera-bypass-diagnostic"][1]["route_manifest_sha256"] = "f" * 64
        with self.assertRaisesRegex(ValueError, "digest"):
            capture_camera_phase_a(changed)
        bad = {role: list(items) for role, items in runs.items()}
        bad["camera-fixed-candidate"][0] = dict(bad["camera-fixed-candidate"][0])
        bad["camera-fixed-candidate"][0]["unsupported_diagnostics"] = 1
        with self.assertRaisesRegex(ValueError, "unsupported diagnostics"):
            capture_camera_phase_a(bad)


if __name__ == "__main__":
    unittest.main()
