"""Raw SBR4/SCC1 role-comparison tests.

Break caught: accepting swapped role evidence, mutated raw/decoded telemetry,
or a Q role that does not improve only the simulation counter.
"""
from __future__ import annotations

import hashlib
import json
import struct
import unittest
from pathlib import Path

from compare_route_reports import PROBE_FIELDS
from compare_route_reports import compare_reports, load_route
from compare_camera_route_reports import compare_camera_route_reports
from test_camera_idle_contract import Q_BRIDGES, build_scc1

ROOT = Path(__file__).resolve().parents[2]
ROUTE = ROOT / "tools/saturn/routes/bob_default_camera_v1.json"


def sbr4(*, sim: int = 100, atan2: int = 2, reject: int = 0) -> dict:
    values = {name: 0 for name in PROBE_FIELDS}
    values.update({
        "magic": 0x53425234, "version": 4, "atan2_variant": atan2,
        "replay_ticks": 2000, "global_timer": 2000, "mario_action": 42,
        "mario_pos_x_bits": 0, "mario_pos_y_bits": 0, "mario_pos_z_bits": 0,
        "mario_face_angle_x": 1, "mario_face_angle_y": 2, "mario_face_angle_z": 3,
        "camera_pos_x_bits": 0, "camera_pos_y_bits": 0, "camera_pos_z_bits": 0,
        "camera_mode": 7, "triangles_transformed": 4, "triangles_emitted": 4,
        "triangles_vdp1_emitted": 4, "sim_frt_ticks_accum": sim,
        "reject_backface": reject,
    })
    raw = struct.pack(f">{len(PROBE_FIELDS)}I", *(values[name] for name in PROBE_FIELDS))
    return {"data": list(raw), "decoded": values}


def report(role: str, *, sim: int, atan2: int = 2, marker: int | None = None,
           reject: int = 0, raw_scc1: bytes | None = None) -> dict:
    variant = 1 if role == "camera-baseline" else 2
    if marker is None:
        marker = variant
    if raw_scc1 is None:
        raw_scc1 = build_scc1(variant=variant, bridges=(0, 0) if variant == 1 else Q_BRIDGES)
    sha = ("a" if variant == 1 else "b") * 64
    return {
        "capture_role": role,
        "route_manifest_sha256": hashlib.sha256(ROUTE.read_bytes()).hexdigest(),
        "artifacts": {"elf": {"sha256": sha, "size": 1},
                      "image": {"sha256": ("c" if variant == 1 else "d") * 64, "size": 1}},
        "elf_camera_variant": marker,
        "elf_route_id": 2,
        "route_window": sbr4(sim=sim, atan2=atan2, reject=reject),
        "camera_idle_window": {"data": list(raw_scc1)},
    }


class CompareCameraRouteReportsTest(unittest.TestCase):
    def test_accepts_ordered_four_raw_reports_and_requires_sim_improvement(self) -> None:
        baseline = [report("camera-baseline", sim=100), report("camera-baseline", sim=101)]
        q = [report("camera-q", sim=90), report("camera-q", sim=91)]
        result = compare_camera_route_reports(baseline, q, ROUTE, True)
        self.assertTrue(result["deterministic"])

    def test_rejects_swapped_roles_atan2_and_elf_marker(self) -> None:
        baseline = [report("camera-baseline", sim=100), report("camera-baseline", sim=101)]
        q = [report("camera-q", sim=90), report("camera-q", sim=91)]
        for bad in (
            ([q[0], baseline[1]], q),
            ([report("camera-baseline", sim=100, atan2=1), baseline[1]], q),
            ([report("camera-baseline", sim=100, marker=2), baseline[1]], q),
        ):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                compare_camera_route_reports(bad[0], bad[1], ROUTE, True)

    def test_rejects_same_role_behavior_drift_raw_decoded_mutation_and_reject_change(self) -> None:
        baseline = [report("camera-baseline", sim=100), report("camera-baseline", sim=101)]
        q = [report("camera-q", sim=90), report("camera-q", sim=91)]
        drift = report("camera-baseline", sim=101, reject=1)
        with self.assertRaises(ValueError):
            compare_camera_route_reports([baseline[0], drift], q, ROUTE, True)
        malformed = report("camera-q", sim=90)
        malformed["route_window"]["decoded"] = {"magic": 0}
        with self.assertRaises(ValueError):
            compare_camera_route_reports(baseline, [malformed, q[1]], ROUTE, True)
        with self.assertRaises(ValueError):
            compare_camera_route_reports(baseline, [report("camera-q", sim=100), q[1]], ROUTE, True)

    def test_legacy_comparator_remains_the_camera_role_rejection_boundary(self) -> None:
        baseline = report("camera-baseline", sim=100)
        q = report("camera-q", sim=90)
        for item in (baseline, q):
            item.update({"evidence_kind": "fixture", "game": "sm64", "frames": 1,
                         "post_poke_frames": 1, "protocol": "fixture",
                         "degradation": {"view_radius": 1, "poly_tier": 1},
                         "probe_window": item["route_window"]})
        old_route = ROOT / "tools/saturn/routes/bob_parity_v1.json"
        result = compare_reports(baseline, q, load_route(old_route))
        self.assertFalse(result["deterministic"])
        self.assertIn("capture roles must be ordered legacy then q16", result["errors"])


if __name__ == "__main__":
    unittest.main()
