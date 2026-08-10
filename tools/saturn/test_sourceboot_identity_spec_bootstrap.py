#!/usr/bin/env python3
"""Contracts for composing sourceboot identity v2 from sealed inputs."""

from __future__ import annotations

import copy
import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import bootstrap_sourceboot_identity_spec as bootstrap
import gen_build_identity as identity
from hermetic_manifest import canonical_json_bytes
from target_profile import PACKAGE_CLASSES


PACKAGE_ARTIFACT_FIELDS = {
    "route": "route_artifact_hash",
    "input": "input_artifact_hash",
    "camera": "camera_artifact_hash",
    "cart": "cart_profile_hash",
    "level": "scene_package_hash",
    "shared-data": "scene_dependency_set_hash",
    "actor": "actor_package_hash",
    "animation": "animation_package_hash",
    "audio": "audio_package_hash",
}


def config() -> dict[str, int]:
    return {
        "features.complete_mario_animation": 1,
        "features.dynamic_actor_closure": 1,
        "features.semantic_audio": 0,
        "renderer_pipeline": 4, "level_id": 9, "area_id": 1, "route_id": 0,
        "route_replay_mode": 1, "live_input_mode": 1, "camera_route": 0,
        "camera_variant": 3, "diagnostic_mode": 0, "bootstrap_ticks": 600,
        "cart_mbit": 32, "cart_stage_sectors": 8, "hot_promotion": 1,
        "near_clip": 1, "bsp_order": 1, "polygon_tier": 2, "fragment_mode": 0,
        "atan2_variant": 2, "demo_path": 1, "demo_view_radius": 6000,
        "slave_render": 1, "camera_idle_start_tick": 0,
        "camera_idle_discovery": 0, "camera_range_capture": 0,
        "bsp_fragment_flat": 0, "fast3d_q16_trace": 0,
        "experimental_skip_geo_walk": 0, "object_pool_capacity": 208,
    }


class SourcebootIdentitySpecBootstrapTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name) / "repo"
        self.root.mkdir()
        self.output = self.root / "build/generated/saturn-build-identity-spec-v2.json"
        self.source_closure = self.root / "build/sealed/source-closure-v2.json"
        self.toolchain_attestation = self.root / "build/sealed/toolchain-v1.json"
        self.profile = self.root / "tools/saturn/profiles/demo.json"
        self.payloads: dict[str, Path] = {}
        self.descriptors: list[str] = []
        self._write_source_closure("source-v1\n")
        self._write_toolchain_attestation("fixture-gcc 1.0")
        for package_class in PACKAGE_CLASSES:
            payload = self.root / f"payload/{package_class}.bin"
            payload.parent.mkdir(parents=True, exist_ok=True)
            payload.write_text(f"{package_class}-v1\n", encoding="utf-8")
            self.payloads[package_class] = payload
            descriptor = self.root / f"tools/saturn/manifests/{package_class}.json"
            self._write_json(descriptor, {
                "schema": "sm64-saturn-package-descriptor-v1",
                "package_class": package_class,
                "package_id": f"fixture-{package_class}",
                "inputs": [{"path": payload.relative_to(self.root).as_posix()}],
            })
            self.descriptors.append(descriptor.relative_to(self.root).as_posix())
        self._write_profile(release_enabled=True)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    @staticmethod
    def _write_json(path: Path, document: object) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(canonical_json_bytes(document))

    def _write_source_closure(self, source_bytes: str) -> None:
        source = self.root / "src/game.c"
        source.parent.mkdir(parents=True, exist_ok=True)
        source.write_text(source_bytes, encoding="utf-8")
        self._write_json(self.source_closure, {
            "schema": "sm64-saturn-source-closure-v2",
            "inputs": [{
                "path": "src/game.c",
                "sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                "class": "compiled-source",
                "owners": ["compiler"],
            }],
        })

    def _write_toolchain_attestation(self, version: str) -> None:
        self._write_json(self.toolchain_attestation, {
            "schema": "sm64-saturn-toolchain-attestation-v1",
            "target_abi": "sh2eb-none-elf",
            "components": [{
                "id": "fixture-toolchain",
                "version": version,
                "binaries": [{"path": "bin/sh-elf-gcc", "sha256": "0" * 64}],
                "dependencies": [],
            }],
        })

    def _write_profile(self, *, release_enabled: bool, profile_id: str = "fixture-demo") -> None:
        self._write_json(self.profile, {
            "schema": "sm64-saturn-target-profile-v1",
            "profile_id": profile_id,
            "release_enabled": release_enabled,
            "release_config": config(),
            "package_classes": list(PACKAGE_CLASSES),
            "package_descriptors": self.descriptors,
            "output_names": {
                "elf": "game.elf", "source_dat": "SOURCE.DAT",
                "iso": "game.iso", "cue": "game.cue",
            },
        })

    def write_v2_spec(self, mutate: str | None = None) -> dict[str, object]:
        if mutate == "closure":
            self._write_source_closure("source-v2\n")
        elif mutate == "profile":
            document = json.loads(self.profile.read_text(encoding="utf-8"))
            document["output_names"]["elf"] = "renamed.elf"
            self._write_json(self.profile, document)
        elif mutate == "package-set":
            self.payloads["texture"].write_text("texture-v2\n", encoding="utf-8")
        elif mutate == "toolchain":
            self._write_toolchain_attestation("fixture-gcc 2.0")
        bootstrap.write_spec(
            self.root, self.output, config(), self.profile, self.source_closure,
            self.toolchain_attestation, "development",
        )
        return json.loads(self.output.read_text(encoding="utf-8"))

    def test_composes_v2_spec_with_one_to_one_legacy_package_mapping(self) -> None:
        spec = self.write_v2_spec()
        self.assertEqual(spec["identity_version"], 2)
        self.assertEqual(set(spec["artifacts"]), set(identity.ARTIFACT_HASH_FIELDS))
        self.assertEqual(spec["features"], {
            "complete_mario_animation": 1,
            "dynamic_actor_closure": 1,
            "semantic_audio": 0,
        })
        for package_class, artifact_field in PACKAGE_ARTIFACT_FIELDS.items():
            descriptor = spec["artifacts"][artifact_field]
            self.assertEqual(Path(descriptor["path"]).name, f"{package_class}-packages.json")
        package_set = json.loads(Path(spec["package_set"]["path"]).read_text(encoding="utf-8"))
        self.assertIn("texture", package_set["package_class_hashes"])
        texture_manifest = self.output.parent / "saturn-package-manifests/texture-packages.json"
        self.assertEqual(
            package_set["package_class_hashes"]["texture"],
            hashlib.sha256(texture_manifest.read_bytes()).hexdigest(),
        )
        self.assertNotIn(str(texture_manifest.resolve()), {
            descriptor["path"] for descriptor in spec["artifacts"].values()
        })
        identity.build_identity(spec)

    def test_capture_tool_and_evidence_changes_do_not_reseal_v2_spec(self) -> None:
        first = identity.build_identity(self.write_v2_spec()).raw
        for relative in (
            "tools/saturn/capture_object_pool_occupancy.py",
            "tools/saturn/test_gen_build_identity.py",
            "docs/saturn/evidence/local.json",
        ):
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("changed\n", encoding="utf-8")
        second = identity.build_identity(self.write_v2_spec()).raw
        self.assertEqual(first, second)

    def test_closure_profile_package_and_toolchain_mutations_reseal_separately(self) -> None:
        baseline = identity.parse_identity(identity.build_identity(self.write_v2_spec()).raw)
        for fixture, expected_field in (
            ("closure", "source_hash"),
            ("profile", "target_profile_hash"),
            ("package-set", "package_set_root_hash"),
            ("toolchain", "toolchain_attestation_hash"),
        ):
            with self.subTest(fixture=fixture):
                changed = identity.parse_identity(
                    identity.build_identity(self.write_v2_spec(mutate=fixture)).raw
                )
                self.assertNotEqual(baseline[expected_field], changed[expected_field])

    def test_config_must_exactly_match_profile_and_release_disabled_profile_fails(self) -> None:
        self.write_v2_spec()
        preserved = self.output.read_bytes()
        changed = copy.deepcopy(config())
        changed["object_pool_capacity"] = 240
        with self.assertRaisesRegex(ValueError, "config.*exactly match.*profile"):
            bootstrap.write_spec(
                self.root, self.output, changed, self.profile, self.source_closure,
                self.toolchain_attestation, "development",
            )
        self.assertEqual(self.output.read_bytes(), preserved)

        self._write_profile(release_enabled=False, profile_id="sm64-saturn-full")
        with self.assertRaisesRegex(ValueError, "sm64-saturn-full.*release-enabled"):
            bootstrap.write_spec(
                self.root, self.output, config(), self.profile, self.source_closure,
                self.toolchain_attestation, "release",
            )
        self.assertEqual(self.output.read_bytes(), preserved)

    def test_bootstrap_rejects_stale_source_closure_without_overwriting_spec(self) -> None:
        self.write_v2_spec()
        preserved = self.output.read_bytes()
        self.source_closure.write_bytes(self.source_closure.read_bytes() + b"drift")
        with self.assertRaisesRegex(ValueError, "source closure.*stale"):
            self.write_v2_spec()
        self.assertEqual(self.output.read_bytes(), preserved)

    def test_bootstrap_revalidates_generated_class_manifest_before_spec_replace(self) -> None:
        self.write_v2_spec()
        preserved = self.output.read_bytes()
        import target_profile

        original = target_profile.resolve_target_profile

        def resolve_then_drift(*args, **kwargs):
            resolved = original(*args, **kwargs)
            resolved.package_class_manifests["actor"].write_bytes(b"stale\n")
            return resolved

        with patch.object(target_profile, "resolve_target_profile", side_effect=resolve_then_drift):
            with self.assertRaisesRegex(ValueError, "actor package class manifest.*stale"):
                self.write_v2_spec()
        self.assertEqual(self.output.read_bytes(), preserved)

    def test_cli_requires_every_sealed_input_and_mode(self) -> None:
        arguments = [
            "bootstrap_sourceboot_identity_spec.py",
            "--root", str(self.root), "--output", str(self.output),
            "--profile", str(self.profile), "--source-closure", str(self.source_closure),
            "--toolchain-attestation", str(self.toolchain_attestation),
            "--mode", "development",
        ]
        for name, value in config().items():
            arguments.extend(("--set", f"{name}={value}"))
        for required in ("--profile", "--source-closure", "--toolchain-attestation", "--mode"):
            with self.subTest(required=required):
                index = arguments.index(required)
                incomplete = arguments[:index] + arguments[index + 2:]
                with patch.object(sys, "argv", incomplete):
                    with self.assertRaises(SystemExit) as raised:
                        bootstrap.main()
                self.assertEqual(raised.exception.code, 2)

    def test_broad_root_api_is_absent(self) -> None:
        self.assertFalse(hasattr(bootstrap, "SOURCE_CLOSURE_ROOTS"))


if __name__ == "__main__":
    unittest.main()
