#!/usr/bin/env python3
"""Artifact-binding contracts for object-pool occupancy evidence."""

from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import capture_object_pool_occupancy as capture
import gen_build_identity as identity


def _spec(root: Path, capacity: int) -> dict[str, object]:
    artifacts: dict[str, dict[str, str]] = {}
    for index, field in enumerate(identity.ARTIFACT_HASH_FIELDS):
        artifact = root / f"{field}.bin"
        artifact.write_bytes(f"artifact-{index}".encode("ascii"))
        artifacts[field] = {
            "path": str(artifact),
            "sha256": hashlib.sha256(artifact.read_bytes()).hexdigest(),
        }
    return {
        "features": {
            "complete_mario_animation": 1,
            "dynamic_actor_closure": 1,
            "semantic_audio": 0,
        },
        "renderer_pipeline": 4,
        "level_id": 9,
        "area_id": 1,
        "route_id": 0,
        "route_replay_mode": 0,
        "live_input_mode": 0,
        "camera_route": 0,
        "camera_variant": 3,
        "diagnostic_mode": 0,
        "bootstrap_ticks": 600,
        "cart_mbit": 32,
        "cart_stage_sectors": 16,
        "hot_promotion": 0,
        "near_clip": 0,
        "bsp_order": 1,
        "polygon_tier": 0,
        "fragment_mode": 0,
        "atan2_variant": 2,
        "demo_path": 0,
        "demo_view_radius": 6000,
        "slave_render": 1,
        "camera_idle_start_tick": 0,
        "camera_idle_discovery": 0,
        "camera_range_capture": 0,
        "bsp_fragment_flat": 0,
        "fast3d_q16_trace": 0,
        "experimental_skip_geo_walk": 0,
        "object_pool_capacity": capacity,
        "artifacts": artifacts,
    }


class ObjectPoolCaptureArtifactBindingTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.spec = _spec(self.root, 208)
        self.spec_path = self.root / "saturn_build_identity_spec.json"
        self.spec_path.write_text(json.dumps(self.spec), encoding="utf-8")
        self.elf = self.root / "sourceboot.elf"
        self.elf.write_bytes(b"ELF-prefix" + identity.build_identity(self.spec).raw + b"ELF-suffix")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_reports_capacity_only_when_the_sealed_spec_is_embedded_in_the_elf(self) -> None:
        self.assertEqual(
            capture.pool_capacity_from_sealed_artifact(self.spec_path, self.elf),
            208,
        )

    def test_rejects_a_spec_that_does_not_match_the_capture_elf(self) -> None:
        self.elf.write_bytes(b"stale ELF without the identity tuple")
        with self.assertRaisesRegex(ValueError, "identity.*ELF"):
            capture.pool_capacity_from_sealed_artifact(self.spec_path, self.elf)


if __name__ == "__main__":
    unittest.main()
