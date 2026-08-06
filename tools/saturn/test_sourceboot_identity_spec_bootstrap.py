#!/usr/bin/env python3
"""Contracts for the clean-tree sourceboot build-identity bootstrap."""

from __future__ import annotations

import copy
import json
import sys
import tempfile
import unittest
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import bootstrap_sourceboot_identity_spec as bootstrap
import gen_build_identity as identity


def config() -> dict[str, int]:
    return {
        "features.complete_mario_animation": 0,
        "features.dynamic_actor_closure": 0,
        "features.semantic_audio": 0,
        "renderer_pipeline": 2, "level_id": 9, "area_id": 1, "route_id": 0,
        "route_replay_mode": 1, "live_input_mode": 1, "camera_route": 0,
        "camera_variant": 3, "diagnostic_mode": 0, "bootstrap_ticks": 600,
        "cart_mbit": 32, "cart_stage_sectors": 16, "hot_promotion": 1,
        "near_clip": 1, "bsp_order": 1, "polygon_tier": 2, "fragment_mode": 0,
        "atan2_variant": 2, "demo_path": 1, "demo_view_radius": 6000,
        "slave_render": 1, "camera_idle_start_tick": 0,
        "camera_idle_discovery": 0, "camera_range_capture": 0,
        "bsp_fragment_flat": 0, "fast3d_q16_trace": 0,
        "experimental_skip_geo_walk": 0,
    }


class SourcebootIdentitySpecBootstrapTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name) / "repo"
        self.root.mkdir()
        for relative in bootstrap.all_input_paths():
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(relative + "\n", encoding="utf-8")
        self.output = self.root / "build/generated/saturn_build_identity_spec.json"

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_bootstraps_complete_hash_checked_spec_from_config_and_inputs(self) -> None:
        bootstrap.write_spec(self.root, self.output, config())
        spec = json.loads(self.output.read_text(encoding="utf-8"))
        self.assertEqual(set(spec["artifacts"]), set(identity.ARTIFACT_HASH_FIELDS))
        self.assertEqual(spec["features"], {
            "complete_mario_animation": 0,
            "dynamic_actor_closure": 0,
            "semantic_audio": 0,
        })
        identity.build_identity(spec)

    def test_config_or_canonical_input_mutation_reseals_the_spec(self) -> None:
        baseline = config()
        bootstrap.write_spec(self.root, self.output, baseline)
        first = json.loads(self.output.read_text(encoding="utf-8"))

        changed = copy.deepcopy(baseline)
        changed["polygon_tier"] = 1
        bootstrap.write_spec(self.root, self.output, changed)
        second = json.loads(self.output.read_text(encoding="utf-8"))
        self.assertEqual(second["polygon_tier"], 1)
        self.assertNotEqual(identity.build_identity(first).raw, identity.build_identity(second).raw)

        route = self.root / "tools/saturn/routes/bob_parity_v1.json"
        route.write_text("changed route\n", encoding="utf-8")
        bootstrap.write_spec(self.root, self.output, changed)
        third = json.loads(self.output.read_text(encoding="utf-8"))
        self.assertNotEqual(
            second["artifacts"]["route_artifact_hash"]["sha256"],
            third["artifacts"]["route_artifact_hash"]["sha256"],
        )

    def test_rejects_invalid_input_before_reusing_a_stale_spec(self) -> None:
        valid = config()
        bootstrap.write_spec(self.root, self.output, valid)
        preserved = self.output.read_bytes()
        invalid = copy.deepcopy(valid)
        invalid["cart_mbit"] = 16
        with self.assertRaisesRegex(ValueError, "cart_mbit"):
            bootstrap.write_spec(self.root, self.output, invalid)
        self.assertEqual(self.output.read_bytes(), preserved)

    def test_makefile_requires_a_successful_bootstrap_not_a_wildcard_fallback(self) -> None:
        makefile = (TOOLS_DIR.parents[1] / "src/port/saturn/sourceboot/Makefile").read_text(encoding="utf-8")
        self.assertIn("bootstrap_sourceboot_identity_spec.py", makefile)
        self.assertIn("SOURCEBOOT_BUILD_IDENTITY_BOOTSTRAP_STATUS", makefile)
        self.assertIn("sourceboot build identity bootstrap failed", makefile)
        self.assertNotIn("ifneq ($(wildcard $(SOURCEBOOT_BUILD_IDENTITY_SPEC)),)", makefile)


if __name__ == "__main__":
    unittest.main()
