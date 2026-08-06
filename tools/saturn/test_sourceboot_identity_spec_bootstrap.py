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
        for relative in bootstrap.SOURCE_CLOSURE_ROOTS:
            (self.root / relative).mkdir(parents=True, exist_ok=True)
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

    def test_config_source_closure_and_route_mutations_reseal_the_spec(self) -> None:
        baseline = config()
        bootstrap.write_spec(self.root, self.output, baseline)
        first = json.loads(self.output.read_text(encoding="utf-8"))

        changed = copy.deepcopy(baseline)
        changed["polygon_tier"] = 1
        bootstrap.write_spec(self.root, self.output, changed)
        second = json.loads(self.output.read_text(encoding="utf-8"))
        self.assertEqual(second["polygon_tier"], 1)
        self.assertNotEqual(identity.build_identity(first).raw, identity.build_identity(second).raw)

        for relative in (
            "src/port/saturn/sourceboot/main.c",
            "src/port/saturn/sourceboot/sourceboot-cart.x",
            "src/port/saturn/gfx/saturn_actor_instance.c",
            "tools/saturn/bootstrap_sourceboot_identity_spec.py",
        ):
            before = json.loads(self.output.read_text(encoding="utf-8"))
            path = self.root / relative
            path.write_text(path.read_text(encoding="utf-8") + "changed\n", encoding="utf-8")
            bootstrap.write_spec(self.root, self.output, changed)
            after = json.loads(self.output.read_text(encoding="utf-8"))
            self.assertNotEqual(
                before["artifacts"]["source_hash"]["sha256"],
                after["artifacts"]["source_hash"]["sha256"],
                relative,
            )

        route = self.root / "tools/saturn/routes/bob_parity_v1.json"
        route.write_text("changed route\n", encoding="utf-8")
        bootstrap.write_spec(self.root, self.output, changed)
        third = json.loads(self.output.read_text(encoding="utf-8"))
        self.assertNotEqual(
            second["artifacts"]["route_artifact_hash"]["sha256"],
            third["artifacts"]["route_artifact_hash"]["sha256"],
        )

    def test_exact_available_payload_mutations_reseal_named_package_fields(self) -> None:
        baseline = config()
        bootstrap.write_spec(self.root, self.output, baseline)
        for field, relative in (
            ("scene_package_hash", bootstrap.SCENE_PAYLOAD),
            ("scene_dependency_set_hash", bootstrap.SCENE_DEPENDENCY_PAYLOAD),
            ("actor_package_hash", bootstrap.ACTOR_PAYLOAD),
            ("animation_package_hash", bootstrap.ANIMATION_PAYLOAD),
        ):
            before = json.loads(self.output.read_text(encoding="utf-8"))
            path = self.root / relative
            path.write_bytes(path.read_bytes() + b"payload-change")
            bootstrap.write_spec(self.root, self.output, baseline)
            after = json.loads(self.output.read_text(encoding="utf-8"))
            self.assertNotEqual(
                before["artifacts"][field]["sha256"],
                after["artifacts"][field]["sha256"], field,
            )

        semantic = copy.deepcopy(baseline)
        semantic["features.semantic_audio"] = 1
        with self.assertRaisesRegex(ValueError, "staged S64A/AUDIO.DAT"):
            bootstrap.write_spec(self.root, self.output, semantic)

    def test_rejects_invalid_input_before_reusing_a_stale_spec(self) -> None:
        valid = config()
        bootstrap.write_spec(self.root, self.output, valid)
        preserved = self.output.read_bytes()
        invalid = copy.deepcopy(valid)
        invalid["cart_mbit"] = 16
        with self.assertRaisesRegex(ValueError, "cart_mbit"):
            bootstrap.write_spec(self.root, self.output, invalid)
        self.assertEqual(self.output.read_bytes(), preserved)

        (self.root / bootstrap.ACTOR_PAYLOAD).unlink()
        with self.assertRaisesRegex(ValueError, "actor_package_hash.*not a file"):
            bootstrap.write_spec(self.root, self.output, valid)
        self.assertEqual(self.output.read_bytes(), preserved)

    def test_makefile_requires_a_successful_bootstrap_not_a_wildcard_fallback(self) -> None:
        makefile = (TOOLS_DIR.parents[1] / "src/port/saturn/sourceboot/Makefile").read_text(encoding="utf-8")
        self.assertIn("bootstrap_sourceboot_identity_spec.py", makefile)
        self.assertIn("SOURCEBOOT_BUILD_IDENTITY_BOOTSTRAP_STATUS", makefile)
        self.assertIn("sourceboot build identity bootstrap failed", makefile)
        self.assertNotIn("ifneq ($(wildcard $(SOURCEBOOT_BUILD_IDENTITY_SPEC)),)", makefile)


if __name__ == "__main__":
    unittest.main()
