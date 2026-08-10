#!/usr/bin/env python3
"""Contracts for the fixed-width Saturn feature/package identity generator."""

from __future__ import annotations

import copy
import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


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
            "atan2_variant": 2,
            "demo_path": 1,
            "demo_view_radius": 6000,
            "slave_render": 1,
            "camera_idle_start_tick": 0,
            "camera_idle_discovery": 0,
            "camera_range_capture": 0,
            "bsp_fragment_flat": 0,
            "fast3d_q16_trace": 0,
            "experimental_skip_geo_walk": 0,
            "object_pool_capacity": 240,
            "artifacts": self.artifacts,
        }
        self.v1_spec = self.spec
        self.v2_spec = copy.deepcopy(self.spec)
        self.v2_spec["identity_version"] = 2
        descriptor_bytes = {
            "target_profile": b"canonical-target-profile-v1\n",
            "package_set": b"canonical-package-set-v1\n",
            "toolchain_attestation": b"canonical-toolchain-attestation-v1\n",
        }
        for field, payload in descriptor_bytes.items():
            path = self.root / f"{field}.json"
            path.write_bytes(payload)
            digest = hashlib.sha256(payload).hexdigest()
            self.v2_spec[field] = {"path": str(path), "sha256": digest}
            setattr(self, f"{field}_sha256", digest)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def mutate_descriptor(self, spec: dict[str, object], field: str) -> dict[str, object]:
        changed = copy.deepcopy(spec)
        path = self.root / f"{field}-mutated.json"
        path.write_bytes(f"mutated-{field}\n".encode("ascii"))
        changed[field] = {
            "path": str(path),
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        }
        return changed

    def test_v1_fixture_remains_404_bytes_and_parses_identically(self) -> None:
        built = identity.build_identity(self.v1_spec)
        self.assertEqual(len(built.raw), 404)
        self.assertEqual(identity.parse_identity(built.raw)["version"], 1)
        self.assertEqual(
            hashlib.sha256(built.raw).hexdigest(),
            "faa7288b4c9fdf90ae14f01ab3af3752649b8e1ca78d77c47033425c9d68b23f",
        )

    def test_v2_extends_v1_layout_to_exactly_500_bytes(self) -> None:
        built = identity.build_identity(self.v2_spec)
        parsed = identity.parse_identity(built.raw)
        self.assertEqual(identity.IDENTITY_V1_SIZE, 404)
        self.assertEqual(identity.IDENTITY_V2_SIZE, 500)
        self.assertEqual(identity.SUPPORTED_IDENTITY_SIZES, (404, 500))
        self.assertEqual(identity.IDENTITY_SIZE, 500)
        self.assertEqual(len(built.raw), 500)
        self.assertEqual(parsed["version"], 2)
        self.assertEqual(parsed["size"], 500)
        self.assertEqual(parsed["target_profile_hash"], self.target_profile_sha256)
        self.assertEqual(parsed["package_set_root_hash"], self.package_set_sha256)
        self.assertEqual(
            parsed["toolchain_attestation_hash"],
            self.toolchain_attestation_sha256,
        )
        self.assertEqual(built.raw[404:436].hex(), self.target_profile_sha256)
        self.assertEqual(built.raw[436:468].hex(), self.package_set_sha256)
        self.assertEqual(built.raw[468:500].hex(), self.toolchain_attestation_sha256)

    def test_v2_requires_every_root_descriptor(self) -> None:
        for field in ("target_profile", "package_set", "toolchain_attestation"):
            with self.subTest(field=field):
                spec = copy.deepcopy(self.v2_spec)
                del spec[field]
                with self.assertRaisesRegex(ValueError, field):
                    identity.build_identity(spec)

    def test_v2_effective_config_covers_all_new_roots(self) -> None:
        self.assertEqual(
            identity.V2_ROOT_DESCRIPTOR_FIELDS,
            ("target_profile", "package_set", "toolchain_attestation"),
        )
        baseline = identity.build_identity(self.v2_spec)
        for field in identity.V2_ROOT_DESCRIPTOR_FIELDS:
            changed = self.mutate_descriptor(self.v2_spec, field)
            with self.subTest(field=field):
                self.assertNotEqual(
                    baseline.values["effective_config_hash"],
                    identity.build_identity(changed).values["effective_config_hash"],
                )

    def test_v2_json_exposes_only_hash_verified_effective_config(self) -> None:
        built = identity.build_identity(self.v2_spec)
        manifest = identity.output_manifest(built)
        canonical = identity.canonical_effective_config(
            manifest["effective_config"]
        )
        self.assertEqual(
            hashlib.sha256(canonical).hexdigest(),
            manifest["identity"]["effective_config_hash"],
        )
        tampered = identity.BuiltIdentity(
            raw=built.raw,
            canonical_config=b'{"schema":"tampered"}',
            values=built.values,
        )
        with self.assertRaisesRegex(ValueError, "effective config.*hash"):
            identity.output_manifest(tampered)

    def test_v2_c_initializer_appends_all_root_arrays_in_binary_order(self) -> None:
        emitted = identity.emit_c_include(identity.build_identity(self.v2_spec).raw)
        positions = []
        for digest in (
            self.target_profile_sha256,
            self.package_set_sha256,
            self.toolchain_attestation_sha256,
        ):
            rendered = ", ".join(
                f"0x{byte:02x}" for byte in bytes.fromhex(digest)
            )
            positions.append(emitted.index(f"{{ {rendered} }}"))
        self.assertEqual(positions, sorted(positions))

    def test_cli_v2_json_uses_hash_verified_output_manifest(self) -> None:
        spec_path = self.root / "identity-v2-spec.json"
        output_path = self.root / "identity-v2.json"
        spec_path.write_text(json.dumps(self.v2_spec), encoding="utf-8")
        with mock.patch.object(
            sys,
            "argv",
            [
                "gen_build_identity.py",
                "--spec",
                str(spec_path),
                "--output-json",
                str(output_path),
            ],
        ):
            self.assertEqual(identity.main(), 0)
        manifest = json.loads(output_path.read_text(encoding="utf-8"))
        self.assertEqual(manifest["identity"]["version"], 2)
        canonical = identity.canonical_effective_config(
            manifest["effective_config"]
        )
        self.assertEqual(
            hashlib.sha256(canonical).hexdigest(),
            manifest["identity"]["effective_config_hash"],
        )

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
            "object_pool_capacity": 0,
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
            "object_pool_capacity": 208,
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

    def test_effective_config_hash_covers_every_additional_compiler_control(self) -> None:
        baseline_built = identity.build_identity(self.spec)
        baseline = identity.parse_identity(baseline_built.raw)
        baseline_label = identity.identity_label(baseline_built.raw)
        mutations = {
            "atan2_variant": 1,
            "demo_path": 0,
            "demo_view_radius": 5000,
            "slave_render": 0,
            "camera_idle_start_tick": 600,
            "camera_idle_discovery": 1,
            "camera_range_capture": 1,
            "bsp_fragment_flat": 1,
            "fast3d_q16_trace": 1,
            "experimental_skip_geo_walk": 1,
            "object_pool_capacity": 208,
        }
        for field, value in mutations.items():
            with self.subTest(field=field):
                spec = copy.deepcopy(self.spec)
                spec[field] = value
                changed_built = identity.build_identity(spec)
                changed = identity.parse_identity(changed_built.raw)
                self.assertNotEqual(
                    changed["effective_config_hash"],
                    baseline["effective_config_hash"],
                )
                self.assertNotEqual(changed_built.raw, baseline_built.raw)
                self.assertNotEqual(
                    identity.identity_label(changed_built.raw), baseline_label
                )
                with self.assertRaisesRegex(ValueError, "compiled identity drift"):
                    identity.validate_spec_expectations(self.spec, {field: value})

    def test_label_is_derived_from_validated_compiled_identity(self) -> None:
        built = identity.build_identity(self.spec)
        label = identity.identity_label(built.raw)
        directory_tag = identity.identity_directory_tag(built.raw)
        self.assertIn("feat101", label)
        self.assertIn("pipe4", label)
        self.assertRegex(directory_tag, r"^id-[0-9a-f]{16}$")
        self.assertLess(len(directory_tag), len(label))
        mutated = bytearray(built.raw)
        mutated[8:12] = (0).to_bytes(4, "big")
        with self.assertRaisesRegex(ValueError, "feature|identity"):
            identity.identity_label(bytes(mutated), expected=built.raw)
        with self.assertRaisesRegex(ValueError, "feature|identity"):
            identity.identity_directory_tag(bytes(mutated), expected=built.raw)

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
