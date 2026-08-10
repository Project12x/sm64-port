#!/usr/bin/env python3
"""Contracts for composing sourceboot identity v2 from sealed inputs."""

from __future__ import annotations

import copy
import hashlib
import json
import shutil
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

    def _build_identity(self, spec: dict[str, object], root: Path | None = None):
        resolved = copy.deepcopy(spec)
        selected_root = self.root if root is None else root
        descriptors = list(resolved["artifacts"].values())
        descriptors.extend(resolved[field] for field in identity.V2_ROOT_DESCRIPTOR_FIELDS)
        for descriptor in descriptors:
            descriptor["path"] = str(selected_root / descriptor["path"])
        return identity.build_identity(resolved)

    @staticmethod
    def _published_paths(output: Path) -> tuple[Path, ...]:
        return (
            *(output.parent / "saturn-package-manifests" / f"{kind}-packages.json"
              for kind in PACKAGE_CLASSES),
            output.parent / "saturn-target-profile-v1.json",
            output.parent / "saturn-package-set-v1.json",
            output,
        )

    def _published_bytes(self, output: Path | None = None) -> dict[str, bytes]:
        selected = self.output if output is None else output
        return {
            path.relative_to(selected.parent).as_posix(): path.read_bytes()
            for path in self._published_paths(selected) if path.is_file()
        }

    def _owned_temporary_paths(self, output: Path | None = None) -> tuple[Path, ...]:
        selected = self.output if output is None else output
        return tuple(
            path.with_name(path.name + ".tmp")
            for path in self._published_paths(selected)
        )

    def _assert_no_owned_temporaries(self, output: Path | None = None) -> None:
        self.assertFalse(any(path.exists() for path in self._owned_temporary_paths(output)))

    def _root_vector(self, spec: dict[str, object]) -> dict[str, str]:
        parsed = identity.parse_identity(self._build_identity(spec).raw)
        fields = (*identity.ARTIFACT_HASH_FIELDS, *identity.V2_APPENDED_HASH_FIELDS)
        return {field: parsed[field] for field in fields}

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
        package_set = json.loads(
            (self.root / spec["package_set"]["path"]).read_text(encoding="utf-8")
        )
        self.assertIn("texture", package_set["package_class_hashes"])
        texture_manifest = self.output.parent / "saturn-package-manifests/texture-packages.json"
        self.assertEqual(
            package_set["package_class_hashes"]["texture"],
            hashlib.sha256(texture_manifest.read_bytes()).hexdigest(),
        )
        self.assertNotIn(texture_manifest.relative_to(self.root).as_posix(), {
            descriptor["path"] for descriptor in spec["artifacts"].values()
        })
        self._build_identity(spec)
        self._assert_no_owned_temporaries()

    def test_capture_tool_and_evidence_changes_do_not_reseal_v2_spec(self) -> None:
        first = self._build_identity(self.write_v2_spec()).raw
        for relative in (
            "tools/saturn/capture_object_pool_occupancy.py",
            "tools/saturn/test_gen_build_identity.py",
            "docs/saturn/evidence/local.json",
        ):
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("changed\n", encoding="utf-8")
        second = self._build_identity(self.write_v2_spec()).raw
        self.assertEqual(first, second)

    def test_closure_profile_package_and_toolchain_mutations_reseal_separately(self) -> None:
        fixture_files = (
            self.root / "src/game.c", self.source_closure, self.profile,
            self.payloads["texture"], self.toolchain_attestation,
        )
        originals = {path: path.read_bytes() for path in fixture_files}
        for fixture, expected_changes in (
            ("closure", {"source_hash"}),
            ("profile", {"target_profile_hash"}),
            ("package-set", {"target_profile_hash", "package_set_root_hash"}),
            ("toolchain", {"toolchain_attestation_hash"}),
        ):
            with self.subTest(fixture=fixture):
                for path, raw in originals.items():
                    path.write_bytes(raw)
                baseline = self._root_vector(self.write_v2_spec())
                changed = self._root_vector(self.write_v2_spec(mutate=fixture))
                actual_changes = {
                    field for field in baseline if baseline[field] != changed[field]
                }
                self.assertEqual(actual_changes, expected_changes)
                for path, raw in originals.items():
                    path.write_bytes(raw)
                self.assertEqual(self._root_vector(self.write_v2_spec()), baseline)

    def test_spec_is_canonical_relative_and_relocation_equivalent(self) -> None:
        first = self.write_v2_spec()
        first_bytes = self.output.read_bytes()
        self.assertEqual(first_bytes, canonical_json_bytes(first))
        descriptors = list(first["artifacts"].values())
        descriptors.extend(first[field] for field in identity.V2_ROOT_DESCRIPTOR_FIELDS)
        for descriptor in descriptors:
            self.assertFalse(Path(descriptor["path"]).is_absolute())
            self.assertNotIn("\\", descriptor["path"])

        relocated = Path(self.temporary.name) / "relocated"
        shutil.copytree(self.root, relocated)
        relocated_output = relocated / self.output.relative_to(self.root)
        bootstrap.write_spec(
            relocated, relocated_output, config(),
            relocated / self.profile.relative_to(self.root),
            relocated / self.source_closure.relative_to(self.root),
            relocated / self.toolchain_attestation.relative_to(self.root),
            "development",
        )
        second = json.loads(relocated_output.read_text(encoding="utf-8"))
        self.assertEqual(first_bytes, relocated_output.read_bytes())
        self.assertEqual(self._build_identity(first).raw,
                         self._build_identity(second, relocated).raw)

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

    def test_bootstrap_rejects_stale_toolchain_attestation_without_overwriting_spec(self) -> None:
        self.write_v2_spec()
        preserved = self.output.read_bytes()
        self.toolchain_attestation.write_bytes(
            self.toolchain_attestation.read_bytes() + b"drift"
        )
        with self.assertRaisesRegex(ValueError, "toolchain attestation.*stale"):
            self.write_v2_spec()
        self.assertEqual(self.output.read_bytes(), preserved)

    def test_bootstrap_revalidates_generated_class_manifest_before_spec_replace(self) -> None:
        self.write_v2_spec()
        preserved = self._published_bytes()
        import target_profile

        original = target_profile.resolve_target_profile

        def resolve_then_drift(*args, **kwargs):
            resolved = original(*args, **kwargs)
            resolved.package_class_manifests["actor"].write_bytes(b"stale\n")
            return resolved

        with patch.object(target_profile, "resolve_target_profile", side_effect=resolve_then_drift):
            with self.assertRaisesRegex(ValueError, "actor package class manifest.*stale"):
                self.write_v2_spec()
        self.assertEqual(self._published_bytes(), preserved)
        self._assert_no_owned_temporaries()

    def test_staged_profile_mutation_after_resolver_cannot_publish(self) -> None:
        self.write_v2_spec()
        preserved = self._published_bytes()
        import target_profile

        original = target_profile.resolve_target_profile

        def resolve_then_mutate_staged_profile(*args, **kwargs):
            resolved = original(*args, **kwargs)
            staged_profile = Path(args[1])
            staged_profile.write_bytes(staged_profile.read_bytes() + b"changed-generation")
            return resolved

        with patch.object(
            target_profile,
            "resolve_target_profile",
            side_effect=resolve_then_mutate_staged_profile,
        ):
            with self.assertRaisesRegex(
                ValueError, "staged target profile changed during identity composition"
            ):
                self.write_v2_spec()
        self.assertEqual(self._published_bytes(), preserved)
        self._assert_no_owned_temporaries()

    def test_input_snapshot_drift_preserves_every_published_file(self) -> None:
        self.write_v2_spec()
        preserved = self._published_bytes()
        for selected in (self.source_closure, self.toolchain_attestation, self.profile):
            with self.subTest(selected=selected.name):
                original = selected.read_bytes()
                changed = original + b"drift"
                if selected == self.profile:
                    document = json.loads(original.decode("utf-8"))
                    document["release_config"]["object_pool_capacity"] = 240
                    changed = canonical_json_bytes(document)
                real_read_bytes = Path.read_bytes
                real_read_text = Path.read_text
                mutated = False

                def mutate_after(value, path):
                    nonlocal mutated
                    if path == selected and not mutated:
                        mutated = True
                        path.write_bytes(changed)
                    return value

                def read_bytes_then_mutate(path):
                    return mutate_after(real_read_bytes(path), path)

                def read_text_then_mutate(path, *args, **kwargs):
                    return mutate_after(real_read_text(path, *args, **kwargs), path)

                try:
                    with patch.object(Path, "read_bytes", new=read_bytes_then_mutate), \
                            patch.object(Path, "read_text", new=read_text_then_mutate):
                        with self.assertRaisesRegex(
                            ValueError, "changed during identity composition"
                        ):
                            self.write_v2_spec()
                    self.assertEqual(self._published_bytes(), preserved)
                finally:
                    selected.write_bytes(original)

    def test_publication_failure_rolls_back_every_prior_file(self) -> None:
        self.write_v2_spec()
        preserved = self._published_bytes()
        document = json.loads(self.profile.read_text(encoding="utf-8"))
        document["output_names"]["elf"] = "replacement.elf"
        self._write_json(self.profile, document)
        real_write = bootstrap.write_if_changed
        publication = True
        real_publication_writes: list[str] = []

        def fail_package_set_once(path: Path, data: bytes) -> None:
            nonlocal publication
            if path.name == "saturn-package-set-v1.json" and publication:
                publication = False
                raise OSError("injected publication failure")
            real_write(path, data)
            if publication:
                real_publication_writes.append(path.name)

        with patch.object(bootstrap, "write_if_changed", side_effect=fail_package_set_once):
            with self.assertRaisesRegex(OSError, "injected publication failure"):
                self.write_v2_spec()
        self.assertEqual(self._published_bytes(), preserved)
        self.assertIn("saturn-target-profile-v1.json", real_publication_writes)
        self._assert_no_owned_temporaries()

    def test_publication_failure_removes_every_new_file(self) -> None:
        output = self.root / "build/fresh/saturn-build-identity-spec-v2.json"
        real_write = bootstrap.write_if_changed
        failed = False

        def fail_profile_once(path: Path, data: bytes) -> None:
            nonlocal failed
            if path.name == "saturn-target-profile-v1.json" and not failed:
                failed = True
                raise OSError("injected first publication failure")
            real_write(path, data)

        with patch.object(bootstrap, "write_if_changed", side_effect=fail_profile_once):
            with self.assertRaisesRegex(OSError, "injected first publication failure"):
                bootstrap.write_spec(
                    self.root, output, config(), self.profile, self.source_closure,
                    self.toolchain_attestation, "development",
                )
        self.assertFalse(any(path.exists() for path in self._published_paths(output)))
        self._assert_no_owned_temporaries(output)

    def test_rollback_failure_preserves_original_error_and_attempts_every_path(self) -> None:
        class PublicationFailure(OSError):
            pass

        self.write_v2_spec()
        document = json.loads(self.profile.read_text(encoding="utf-8"))
        document["output_names"]["elf"] = "rollback-diagnostic.elf"
        self._write_json(self.profile, document)
        real_write = bootstrap.write_if_changed
        publication = True
        successful_publication: list[str] = []
        rollback_attempts: list[str] = []

        def fail_publication_and_one_restore(path: Path, data: bytes) -> None:
            nonlocal publication
            if publication:
                if path.name == "saturn-package-set-v1.json":
                    publication = False
                    raise PublicationFailure("original publication failure")
                real_write(path, data)
                successful_publication.append(path.name)
                return
            rollback_attempts.append(path.name)
            if path.name == "saturn-target-profile-v1.json":
                path.with_name(path.name + ".tmp").write_bytes(b"rollback debris")
                raise OSError("injected rollback failure")
            real_write(path, data)

        with patch.object(
            bootstrap, "write_if_changed", side_effect=fail_publication_and_one_restore
        ):
            try:
                self.write_v2_spec()
            except BaseException as error:
                raised = error
            else:
                self.fail("publication failure was not raised")
        self.assertIsInstance(raised, PublicationFailure)
        self.assertIn("saturn-target-profile-v1.json", successful_publication)
        self.assertEqual(
            set(rollback_attempts),
            {path.name for path in self._published_paths(self.output)},
        )
        self.assertIn("original publication failure", str(raised))
        self.assertTrue(any(
            "rollback failed" in note and "saturn-target-profile-v1.json" in note
            for note in getattr(raised, "__notes__", ())
        ))
        self._assert_no_owned_temporaries()

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
