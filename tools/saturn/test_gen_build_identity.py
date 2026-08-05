#!/usr/bin/env python3
"""Contracts for the fixed-width Saturn feature/package identity generator."""

from __future__ import annotations

import copy
import hashlib
import sys
import tempfile
import unittest
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

try:
    import gen_build_identity as identity
except ModuleNotFoundError as error:
    raise AssertionError("Saturn build identity generator is missing") from error


class BuildIdentityGeneratorTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.artifacts: dict[str, dict[str, str]] = {}
        for index, field in enumerate(identity.ARTIFACT_HASH_FIELDS):
            path = self.root / f"{field}.bin"
            path.write_bytes(f"artifact-{index}-{field}".encode("ascii"))
            self.artifacts[field] = {
                "path": str(path),
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            }
        self.spec = {
            "features": {
                "complete_mario_animation": 1,
                "dynamic_actor_closure": 0,
                "semantic_audio": 1,
            },
            "renderer_pipeline": 4,
            "level_id": 9,
            "area_id": 1,
            "route_id": 7,
            "route_replay_mode": 1,
            "live_input_mode": 1,
            "camera_route": 0,
            "camera_variant": 3,
            "diagnostic_mode": 0,
            "bootstrap_ticks": 600,
            "cart_mbit": 32,
            "cart_stage_sectors": 8,
            "hot_promotion": 1,
            "near_clip": 1,
            "bsp_order": 1,
            "polygon_tier": 2,
            "fragment_mode": 0,
            "artifacts": self.artifacts,
        }

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_emits_fixed_width_big_endian_versioned_identity(self) -> None:
        built = identity.build_identity(self.spec)
        self.assertEqual(len(built.raw), 404)
        self.assertEqual(built.raw[:4], b"SBI1")
        self.assertEqual(int.from_bytes(built.raw[4:6], "big"), 1)
        self.assertEqual(int.from_bytes(built.raw[6:8], "big"), 404)
        parsed = identity.parse_identity(built.raw)
        self.assertEqual(parsed["feature_bits"], 0b101)
        self.assertEqual(parsed["effective_config_hash"],
                         hashlib.sha256(built.canonical_config).hexdigest())

    def test_accepts_every_boolean_replay_live_input_combination(self) -> None:
        for replay in (0, 1):
            for live in (0, 1):
                with self.subTest(replay=replay, live=live):
                    spec = copy.deepcopy(self.spec)
                    spec["route_replay_mode"] = replay
                    spec["live_input_mode"] = live
                    parsed = identity.parse_identity(identity.build_identity(spec).raw)
                    self.assertEqual((parsed["route_replay_mode"], parsed["live_input_mode"]),
                                     (replay, live))

    def test_rejects_non_boolean_features_and_modes(self) -> None:
        paths = (
            ("features", "complete_mario_animation"),
            ("features", "dynamic_actor_closure"),
            ("features", "semantic_audio"),
            (None, "route_replay_mode"),
            (None, "live_input_mode"),
            (None, "hot_promotion"),
            (None, "near_clip"),
            (None, "bsp_order"),
            (None, "fragment_mode"),
        )
        for parent, field in paths:
            for value in (-1, 2, "1", True):
                with self.subTest(field=field, value=value):
                    spec = copy.deepcopy(self.spec)
                    target = spec[parent] if parent else spec
                    target[field] = value
                    with self.assertRaisesRegex(ValueError, "0 or 1"):
                        identity.build_identity(spec)

    def test_rejects_out_of_contract_scalar_values(self) -> None:
        mutations = {
            "renderer_pipeline": 5,
            "level_id": 65536,
            "area_id": -1,
            "route_id": 65536,
            "camera_route": 2,
            "camera_variant": 4,
            "diagnostic_mode": 3,
            "bootstrap_ticks": 1,
            "cart_mbit": 16,
            "cart_stage_sectors": 5,
            "polygon_tier": 3,
        }
        for field, value in mutations.items():
            with self.subTest(field=field):
                spec = copy.deepcopy(self.spec)
                spec[field] = value
                with self.assertRaises(ValueError):
                    identity.build_identity(spec)

    def test_rejects_each_missing_or_stale_artifact_hash(self) -> None:
        for field in identity.ARTIFACT_HASH_FIELDS:
            with self.subTest(field=field, defect="missing"):
                spec = copy.deepcopy(self.spec)
                del spec["artifacts"][field]
                with self.assertRaisesRegex(ValueError, field):
                    identity.build_identity(spec)
            with self.subTest(field=field, defect="stale"):
                spec = copy.deepcopy(self.spec)
                spec["artifacts"][field]["sha256"] = "00" * 32
                with self.assertRaisesRegex(ValueError, f"{field}.*SHA-256"):
                    identity.build_identity(spec)

    def test_effective_config_hash_covers_every_behavior_scalar_feature_and_artifact(self) -> None:
        baseline = identity.parse_identity(identity.build_identity(self.spec).raw)
        scalar_mutations = {
            "renderer_pipeline": 3, "level_id": 8, "area_id": 2,
            "route_id": 8, "route_replay_mode": 0, "live_input_mode": 0,
            "camera_route": 1, "camera_variant": 2, "diagnostic_mode": 1,
            "bootstrap_ticks": 1200, "cart_mbit": 64, "cart_stage_sectors": 16,
            "hot_promotion": 0, "near_clip": 0, "bsp_order": 0,
            "polygon_tier": 1, "fragment_mode": 1,
        }
        for field, value in scalar_mutations.items():
            with self.subTest(field=field):
                spec = copy.deepcopy(self.spec)
                spec[field] = value
                changed = identity.parse_identity(identity.build_identity(spec).raw)
                self.assertNotEqual(changed["effective_config_hash"],
                                    baseline["effective_config_hash"])
        for field in self.spec["features"]:
            with self.subTest(feature=field):
                spec = copy.deepcopy(self.spec)
                spec["features"][field] ^= 1
                changed = identity.parse_identity(identity.build_identity(spec).raw)
                self.assertNotEqual(changed["effective_config_hash"],
                                    baseline["effective_config_hash"])
        for field in identity.ARTIFACT_HASH_FIELDS:
            with self.subTest(artifact=field):
                spec = copy.deepcopy(self.spec)
                path = Path(spec["artifacts"][field]["path"])
                path.write_bytes(path.read_bytes() + b"-changed")
                spec["artifacts"][field]["sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
                changed = identity.parse_identity(identity.build_identity(spec).raw)
                self.assertNotEqual(changed["effective_config_hash"],
                                    baseline["effective_config_hash"])
                path.write_bytes(path.read_bytes()[:-8])

    def test_label_is_derived_from_validated_compiled_identity(self) -> None:
        built = identity.build_identity(self.spec)
        label = identity.identity_label(built.raw)
        self.assertIn("feat101", label)
        self.assertIn("pipe4", label)
        mutated = bytearray(built.raw)
        mutated[8:12] = (0).to_bytes(4, "big")
        with self.assertRaisesRegex(ValueError, "feature|identity"):
            identity.identity_label(bytes(mutated), expected=built.raw)

    def test_rejects_wrapper_feature_or_scalar_drift_before_emission(self) -> None:
        identity.validate_spec_expectations(
            self.spec,
            {"features.complete_mario_animation": 1, "renderer_pipeline": 4},
        )
        for name, wrong in (
            ("features.complete_mario_animation", 0),
            ("renderer_pipeline", 3),
            ("route_replay_mode", 0),
        ):
            with self.subTest(name=name):
                with self.assertRaisesRegex(ValueError, "compiled identity drift"):
                    identity.validate_spec_expectations(self.spec, {name: wrong})


if __name__ == "__main__":
    unittest.main()
