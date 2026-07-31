"""Report adapter tests for SCC1 capture evidence.

Break caught: trusting report JSON rather than the raw SCC1/SBR4 windows and
the immutable route-manifest identity that binds them.
"""
from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from test_camera_idle_contract import Q_BRIDGES, build_scc1, mutate_word
from verify_camera_idle_capture import decode_capture_report, verify_capture_report


ROOT = Path(__file__).resolve().parents[2]
ROUTE = ROOT / "tools/saturn/routes/bob_default_camera_v1.json"


def report(*, role: str = "camera-baseline", raw: bytes | None = None,
           decoded: object | None = None) -> dict:
    if raw is None:
        raw = build_scc1(variant=1 if role == "camera-baseline" else 2,
                         bridges=(0, 0) if role == "camera-baseline" else Q_BRIDGES)
    window: dict[str, object] = {"data": list(raw)}
    if decoded is not None:
        window["decoded"] = decoded
    return {
        "capture_role": role,
        "route_manifest_sha256": hashlib.sha256(ROUTE.read_bytes()).hexdigest(),
        "camera_idle_window": window,
    }


class VerifyCameraIdleCaptureTest(unittest.TestCase):
    def test_raw_capture_report_validates_against_manifest(self) -> None:
        value = report()
        capture = decode_capture_report(value)
        verify_capture_report(value, route_path=ROUTE, expected_idle_start_tick=31,
                              expected_bridge_counts=(0, 0))
        self.assertEqual(capture.samples[0].source_tick, 2031)

    def test_rejects_missing_raw_window_and_raw_decoded_disagreement(self) -> None:
        with self.assertRaises(ValueError):
            decode_capture_report({"capture_role": "camera-baseline"})
        bad = report(decoded={"header": []})
        with self.assertRaises(ValueError):
            decode_capture_report(bad)

    def test_rejects_manifest_digest_and_declared_raw_role_disagreement(self) -> None:
        bad_digest = report()
        bad_digest["route_manifest_sha256"] = "0" * 64
        with self.assertRaises(ValueError):
            verify_capture_report(bad_digest, route_path=ROUTE, expected_idle_start_tick=31,
                                  expected_bridge_counts=(0, 0))
        with self.assertRaises(ValueError):
            verify_capture_report(report(role="camera-q", raw=build_scc1()), route_path=ROUTE,
                                  expected_idle_start_tick=31, expected_bridge_counts=Q_BRIDGES)

    def test_cli_rejects_a_raw_mutation(self) -> None:
        raw = mutate_word(build_scc1(), 23, 3)
        with self.assertRaises(ValueError):
            decode_capture_report(report(raw=raw))


if __name__ == "__main__":
    unittest.main()
