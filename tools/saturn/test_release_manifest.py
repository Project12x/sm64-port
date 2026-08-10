#!/usr/bin/env python3
"""Host contracts for exact Saturn post-link release sealing."""

from __future__ import annotations

import copy
import hashlib
import json
import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import gen_build_identity as identity
import target_profile
from hermetic_manifest import canonical_json_bytes

try:
    import release_manifest
except ModuleNotFoundError:
    release_manifest = None  # type: ignore[assignment]


BOOT_ADDRESS = 0x06010000


def _elf32_with_identity(path: Path, raw_identity: bytes) -> None:
    """Create the smallest big-endian ELF32 needed to map one data symbol."""
    name = b"\x00saturn_build_identity\x00"
    program_offset = 52
    section_offset = 84
    text_offset = 0x200
    symtab_offset = (text_offset + len(raw_identity) + 15) & ~15
    strtab_offset = symtab_offset + 32
    image = bytearray(strtab_offset + len(name))
    image[:4] = b"\x7fELF"
    image[4] = 1
    image[5] = 2
    image[28:32] = program_offset.to_bytes(4, "big")
    image[32:36] = section_offset.to_bytes(4, "big")
    image[42:44] = (32).to_bytes(2, "big")
    image[44:46] = (1).to_bytes(2, "big")
    image[46:48] = (40).to_bytes(2, "big")
    image[48:50] = (4).to_bytes(2, "big")

    program = memoryview(image)[program_offset : program_offset + 32]
    program[0:4] = (1).to_bytes(4, "big")
    program[4:8] = text_offset.to_bytes(4, "big")
    program[8:12] = BOOT_ADDRESS.to_bytes(4, "big")
    program[12:16] = BOOT_ADDRESS.to_bytes(4, "big")
    program[16:20] = len(raw_identity).to_bytes(4, "big")
    program[20:24] = len(raw_identity).to_bytes(4, "big")
    program[24:28] = (4).to_bytes(4, "big")
    program[28:32] = (4).to_bytes(4, "big")

    data_section = memoryview(image)[section_offset + 40 : section_offset + 80]
    data_section[4:8] = (1).to_bytes(4, "big")
    data_section[8:12] = (0x2).to_bytes(4, "big")
    data_section[12:16] = BOOT_ADDRESS.to_bytes(4, "big")
    data_section[16:20] = text_offset.to_bytes(4, "big")
    data_section[20:24] = len(raw_identity).to_bytes(4, "big")
    image[text_offset : text_offset + len(raw_identity)] = raw_identity

    symtab = memoryview(image)[section_offset + 80 : section_offset + 120]
    symtab[4:8] = (2).to_bytes(4, "big")
    symtab[16:20] = symtab_offset.to_bytes(4, "big")
    symtab[20:24] = (32).to_bytes(4, "big")
    symtab[24:28] = (3).to_bytes(4, "big")
    symtab[36:40] = (16).to_bytes(4, "big")

    strtab = memoryview(image)[section_offset + 120 : section_offset + 160]
    strtab[4:8] = (3).to_bytes(4, "big")
    strtab[16:20] = strtab_offset.to_bytes(4, "big")
    strtab[20:24] = len(name).to_bytes(4, "big")

    symbol = symtab_offset + 16
    image[symbol : symbol + 4] = (1).to_bytes(4, "big")
    image[symbol + 4 : symbol + 8] = BOOT_ADDRESS.to_bytes(4, "big")
    image[symbol + 8 : symbol + 12] = len(raw_identity).to_bytes(4, "big")
    image[symbol + 12] = 0x11
    image[symbol + 14 : symbol + 16] = (1).to_bytes(2, "big")
    image[strtab_offset : strtab_offset + len(name)] = name
    path.write_bytes(image)


class ReleaseFixture:
    """Real canonical inputs and a symbol-bearing ELF for release tests."""

    def __init__(self, root: Path, profile_id: str = "sourceboot-bob-demo") -> None:
        self.root = root
        self.release_dir = root / "release"
        self.obj_dir = self.release_dir / "obj"
        self.obj_dir.mkdir(parents=True)
        self.profile = root / "saturn-target-profile-v1.json"
        self.source_closure = root / "saturn-source-closure-v2.json"
        self.package_set = root / "saturn-package-set-v1.json"
        self.toolchain = root / "saturn-toolchain-attestation-v1.json"
        self.identity_json = root / "saturn_build_identity.json"
        self.manifest = self.release_dir / "saturn-release-manifest-v1.json"
        self.outputs = {
            "elf": self.obj_dir / "game.elf",
            "source_dat": self.obj_dir / "SOURCE.DAT",
            "iso": self.release_dir / "game.iso",
            "cue": self.release_dir / "game.cue",
        }
        self.effective_values = {
            "features.complete_mario_animation": 1,
            "features.dynamic_actor_closure": 1,
            "features.semantic_audio": 0,
            "renderer_pipeline": 4,
            "level_id": 9,
            "area_id": 1,
            "route_id": 0,
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
            "object_pool_capacity": 208,
        }
        self._write_canonical(self.profile, {
            "schema": "sm64-saturn-resolved-target-profile-v1",
            "profile_id": profile_id,
            "release_enabled": True,
            "effective_config": self.effective_values,
            "package_manifests": [],
            "output_names": {
                "elf": "game.elf", "source_dat": "SOURCE.DAT",
                "iso": "game.iso", "cue": "game.cue",
            },
        })
        self._write_canonical(self.source_closure, {
            "schema": "sm64-saturn-source-closure-v2", "inputs": [],
        })
        self._write_canonical(self.package_set, {
            "schema": "sm64-saturn-package-set-v2",
            "profile_id": profile_id,
            "packages": [],
            "package_class_hashes": {
                name: hashlib.sha256(f"empty-{name}".encode("ascii")).hexdigest()
                for name in target_profile.PACKAGE_CLASSES
            },
        })
        self._write_canonical(self.toolchain, {
            "schema": "sm64-saturn-toolchain-attestation-v1",
            "target_abi": "sh2eb-none-elf",
            "components": [{
                "id": "yaul-sh-sdk", "version": "fixture",
                "binaries": [], "dependencies": [],
            }],
        })

        self.artifact_paths: dict[str, Path] = {}
        for index, field in enumerate(identity.ARTIFACT_HASH_FIELDS):
            path = self.source_closure if field == "source_hash" else root / f"{field}.bin"
            if path != self.source_closure:
                path.write_bytes(f"artifact-{index}\n".encode("ascii"))
            self.artifact_paths[field] = path
        self.rebuild_identity(2)
        self.outputs["source_dat"].write_bytes(b"source-cart\n")
        self.outputs["iso"].write_bytes(b"disc-image\n")
        self.outputs["cue"].write_text('FILE "game.iso" BINARY\n', encoding="ascii")

    def rebuild_identity(self, version: int) -> None:
        spec: dict[str, object] = {
            "identity_version": version,
            "features": {
                name: self.effective_values[f"features.{name}"]
                for name in identity.FEATURE_BITS
            },
            **{
                field: self.effective_values[field]
                for field in identity.SCALAR_FIELDS
            },
            **{
                field: self.effective_values[field]
                for field in identity.COMPILER_CONFIG_FIELDS
            },
            "artifacts": {
                field: {
                    "path": str(path),
                    "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                }
                for field, path in self.artifact_paths.items()
            },
        }
        if version == 2:
            spec.update({
                "target_profile": self._descriptor(self.profile),
                "package_set": self._descriptor(self.package_set),
                "toolchain_attestation": self._descriptor(self.toolchain),
            })
        self.identity_spec = spec
        built = identity.build_identity(spec)
        self._write_canonical(self.identity_json, identity.output_manifest(built))
        _elf32_with_identity(self.outputs["elf"], built.raw)

    @staticmethod
    def _write_canonical(path: Path, document: dict[str, object]) -> None:
        path.write_bytes(canonical_json_bytes(document))

    @staticmethod
    def _descriptor(path: Path) -> dict[str, str]:
        return {
            "path": str(path),
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        }

    def build(self) -> bytes:
        assert release_manifest is not None
        with mock.patch.object(
            release_manifest,
            "_git_provenance",
            return_value={"git_revision": "a" * 40, "closure_clean": True},
        ):
            return release_manifest.build_release_manifest(
                self.root,
                self.profile,
                self.identity_json,
                self.source_closure,
                self.package_set,
                self.toolchain,
                self.outputs,
                "development",
            )

    def write(self) -> Path:
        self.manifest.write_bytes(self.build())
        return self.manifest


@unittest.skipIf(release_manifest is None, "release_manifest.py is not implemented")
class ReleaseManifestTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.fixture = ReleaseFixture(Path(self.temporary.name))

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_release_manifest_binds_exact_outputs_without_timestamps(self) -> None:
        raw = self.fixture.build()
        document = json.loads(raw)
        self.assertNotIn("created_at", document)
        self.assertNotIn(str(self.fixture.root), raw.decode("ascii"))
        self.assertEqual(set(document["outputs"]), {"elf", "source_dat", "iso", "cue"})
        for record in document["outputs"].values():
            self.assertEqual(len(record["sha256"]), 64)
            self.assertGreater(record["size"], 0)
            self.assertFalse(Path(record["path"]).is_absolute())
        self.assertEqual(raw, canonical_json_bytes(document))

    def test_verifier_rejects_each_mutated_output(self) -> None:
        manifest = self.fixture.write()
        originals = {name: path.read_bytes() for name, path in self.fixture.outputs.items()}
        for name, path in self.fixture.outputs.items():
            with self.subTest(output=name):
                path.write_bytes(originals[name] + b"drift")
                with self.assertRaisesRegex(ValueError, rf"{name}.*SHA-256 mismatch"):
                    release_manifest.verify_release_manifest(manifest)
                path.write_bytes(originals[name])

    def test_verifier_owns_immutable_manifest_and_output_snapshots(self) -> None:
        manifest = self.fixture.write()
        manifest_bytes = manifest.read_bytes()
        output_bytes = {
            name: path.read_bytes() for name, path in self.fixture.outputs.items()
        }
        verified = release_manifest.verify_release_manifest(manifest)

        manifest.write_bytes(b"mutated manifest")
        for name, path in self.fixture.outputs.items():
            path.write_bytes(f"mutated {name}".encode("ascii"))

        self.assertEqual(verified.manifest_bytes, manifest_bytes)
        for name, expected in output_bytes.items():
            self.assertNotEqual(verified.snapshot_outputs[name], verified.outputs[name])
            self.assertEqual(verified.snapshot_outputs[name].read_bytes(), expected)

    def test_manifest_replacement_between_lookup_and_open_is_rejected(self) -> None:
        manifest = self.fixture.write()
        original = manifest.read_bytes()
        alternate = json.loads(original.decode("ascii"))
        alternate["provenance"]["git_revision"] = "1" * 40
        alternate_bytes = canonical_json_bytes(alternate)
        real_open = release_manifest.os.open
        swapped = False

        def swap_then_open(path: object, flags: int, *args: object, **kwargs: object):
            nonlocal swapped
            if Path(path) == manifest and not swapped:
                swapped = True
                manifest.rename(manifest.with_suffix(".original"))
                manifest.write_bytes(alternate_bytes)
            return real_open(path, flags, *args, **kwargs)

        with (
            mock.patch.object(release_manifest.os, "open", side_effect=swap_then_open),
            self.assertRaisesRegex(ValueError, "changed identity|replaced"),
        ):
            release_manifest.verify_release_manifest(manifest)

        self.assertTrue(swapped)
        self.assertEqual(manifest.read_bytes(), alternate_bytes)

    def test_exact_inventory_rejects_an_undeclared_release_file(self) -> None:
        manifest = self.fixture.write()
        (self.fixture.release_dir / "foreign.txt").write_bytes(b"foreign")
        with self.assertRaisesRegex(ValueError, "inventory"):
            release_manifest.verify_release_manifest(manifest, exact_inventory=True)

    def test_verifier_rejects_effective_config_not_bound_to_embedded_identity(self) -> None:
        manifest = self.fixture.write()
        document = json.loads(manifest.read_text(encoding="ascii"))
        changed = copy.deepcopy(document)
        changed["effective_config"]["object_pool_capacity"] += 1
        manifest.write_bytes(canonical_json_bytes(changed))
        with self.assertRaisesRegex(ValueError, "effective config.*identity"):
            release_manifest.verify_release_manifest(manifest)

    def test_verifier_requires_one_cue_file_directive_for_the_hashed_iso(self) -> None:
        manifest = self.fixture.write()
        cue = self.fixture.outputs["cue"]
        cue.write_text('FILE "other.iso" BINARY\n', encoding="ascii")
        document = json.loads(manifest.read_text(encoding="ascii"))
        document["outputs"]["cue"]["sha256"] = hashlib.sha256(cue.read_bytes()).hexdigest()
        document["outputs"]["cue"]["size"] = cue.stat().st_size
        manifest.write_bytes(canonical_json_bytes(document))
        with self.assertRaisesRegex(ValueError, "CUE.*ISO"):
            release_manifest.verify_release_manifest(manifest)

    def test_verifier_rejects_noncanonical_manifest_and_profile_mismatch(self) -> None:
        manifest = self.fixture.write()
        document = json.loads(manifest.read_text(encoding="ascii"))
        manifest.write_text(json.dumps(document, indent=2) + "\n", encoding="ascii")
        with self.assertRaisesRegex(ValueError, "canonical JSON"):
            release_manifest.verify_release_manifest(manifest)
        manifest.write_bytes(canonical_json_bytes(document))
        with self.assertRaisesRegex(ValueError, "required profile"):
            release_manifest.verify_release_manifest(
                manifest, required_profile="sm64-saturn-full"
            )

    def test_builder_rejects_malformed_canonical_input_before_git(self) -> None:
        document = json.loads(self.fixture.source_closure.read_text(encoding="ascii"))
        document["unexpected"] = []
        self.fixture.source_closure.write_bytes(canonical_json_bytes(document))
        with mock.patch.object(
            release_manifest, "_git_provenance",
            side_effect=AssertionError("Git must not run for malformed input"),
        ):
            with self.assertRaisesRegex(ValueError, "source closure.*schema"):
                release_manifest.build_release_manifest(
                    self.fixture.root, self.fixture.profile, self.fixture.identity_json,
                    self.fixture.source_closure, self.fixture.package_set,
                    self.fixture.toolchain, self.fixture.outputs, "development",
                )

    def test_builder_binds_profile_effective_config_to_identity(self) -> None:
        profile = json.loads(self.fixture.profile.read_text(encoding="ascii"))
        profile["effective_config"]["object_pool_capacity"] += 1
        self.fixture._write_canonical(self.fixture.profile, profile)
        self.fixture.rebuild_identity(2)
        with self.assertRaisesRegex(ValueError, "profile effective config.*identity"):
            self.fixture.build()

    def test_builder_binds_profile_output_names_to_artifact_basenames(self) -> None:
        profile = json.loads(self.fixture.profile.read_text(encoding="ascii"))
        profile["output_names"]["elf"] = "different.elf"
        self.fixture._write_canonical(self.fixture.profile, profile)
        self.fixture.rebuild_identity(2)
        with self.assertRaisesRegex(ValueError, "profile output name.*elf"):
            self.fixture.build()

    def test_builder_writes_and_verifies_real_historical_v1(self) -> None:
        self.fixture.rebuild_identity(1)
        manifest = self.fixture.write()
        verified = release_manifest.verify_release_manifest(manifest)
        self.assertEqual(verified.document["identity_version"], 1)
        self.assertEqual(
            verified.document["effective_config"]["object_pool_capacity"], 208
        )
        self.assertEqual(
            verified.document["effective_config"]["features"],
            {
                "complete_mario_animation": 1,
                "dynamic_actor_closure": 1,
                "semantic_audio": 0,
            },
        )

    def test_builder_rejects_case_colliding_closure_paths_and_owners(self) -> None:
        rows = [
            {
                "path": "src/Mario.c", "sha256": "1" * 64,
                "class": "compiled-source", "owners": ["Actor", "actor"],
            },
            {
                "path": "src/mario.c", "sha256": "2" * 64,
                "class": "compiled-source", "owners": ["actor"],
            },
        ]
        self.fixture._write_canonical(self.fixture.source_closure, {
            "schema": "sm64-saturn-source-closure-v2", "inputs": rows,
        })
        with self.assertRaisesRegex(ValueError, "case-colliding.*closure|closure.*case-colliding"):
            self.fixture.build()

    def test_builder_rejects_case_colliding_package_identities(self) -> None:
        rows = [
            {"package_class": "actor", "package_id": "Mario", "manifest_sha256": "1" * 64},
            {"package_class": "actor", "package_id": "mario", "manifest_sha256": "2" * 64},
        ]
        profile = json.loads(self.fixture.profile.read_text(encoding="ascii"))
        profile["package_manifests"] = rows
        self.fixture._write_canonical(self.fixture.profile, profile)
        package_set = json.loads(self.fixture.package_set.read_text(encoding="ascii"))
        package_set["packages"] = rows
        self.fixture._write_canonical(self.fixture.package_set, package_set)
        with self.assertRaisesRegex(ValueError, "case-colliding.*package|package.*case-colliding"):
            self.fixture.build()

    def test_builder_rejects_case_colliding_toolchain_components_and_paths(self) -> None:
        components = [
            {
                "id": "Yaul", "version": "1", "binaries": [
                    {"path": "bin/sh-elf-gcc", "sha256": "1" * 64},
                    {"path": "BIN/SH-ELF-GCC", "sha256": "1" * 64},
                ], "dependencies": [],
            },
            {"id": "yaul", "version": "1", "binaries": [], "dependencies": []},
        ]
        self.fixture._write_canonical(self.fixture.toolchain, {
            "schema": "sm64-saturn-toolchain-attestation-v1",
            "target_abi": "sh2eb-unknown-elf", "components": components,
        })
        with self.assertRaisesRegex(ValueError, "case-colliding.*toolchain|toolchain.*case-colliding"):
            self.fixture.build()

    def test_builder_rejects_toolchain_path_collisions_across_components(self) -> None:
        components = [
            {
                "id": "assembler", "version": "1", "binaries": [
                    {"path": "bin/sh-elf-as", "sha256": "1" * 64},
                ], "dependencies": [],
            },
            {
                "id": "compiler", "version": "1", "binaries": [],
                "dependencies": [
                    {"path": "BIN/SH-ELF-AS", "sha256": "1" * 64},
                ],
            },
        ]
        self.fixture._write_canonical(self.fixture.toolchain, {
            "schema": "sm64-saturn-toolchain-attestation-v1",
            "target_abi": "sh2eb-unknown-elf", "components": components,
        })
        with self.assertRaisesRegex(
            ValueError, "case-colliding.*toolchain|toolchain.*case-colliding"
        ):
            self.fixture.build()

    def test_builder_rejects_empty_schema_identifiers_and_paths(self) -> None:
        cases = ("closure-path", "closure-owner", "package-id", "component-id", "tool-path")
        for case in cases:
            with self.subTest(case=case):
                fixture = ReleaseFixture(self.fixture.root / case)
                if case in ("closure-path", "closure-owner"):
                    row = {
                        "path": "" if case == "closure-path" else "src/actor.c",
                        "sha256": "1" * 64,
                        "class": "compiled-source",
                        "owners": ["" if case == "closure-owner" else "actor"],
                    }
                    fixture._write_canonical(fixture.source_closure, {
                        "schema": "sm64-saturn-source-closure-v2", "inputs": [row],
                    })
                elif case == "package-id":
                    rows = [{
                        "package_class": "actor", "package_id": "",
                        "manifest_sha256": "1" * 64,
                    }]
                    profile = json.loads(fixture.profile.read_text(encoding="ascii"))
                    profile["package_manifests"] = rows
                    fixture._write_canonical(fixture.profile, profile)
                    package_set = json.loads(fixture.package_set.read_text(encoding="ascii"))
                    package_set["packages"] = rows
                    fixture._write_canonical(fixture.package_set, package_set)
                else:
                    component = {
                        "id": "" if case == "component-id" else "compiler",
                        "version": "1", "dependencies": [], "binaries": [],
                    }
                    if case == "tool-path":
                        component["binaries"] = [{"path": "", "sha256": "1" * 64}]
                    fixture._write_canonical(fixture.toolchain, {
                        "schema": "sm64-saturn-toolchain-attestation-v1",
                        "target_abi": "sh2eb-unknown-elf", "components": [component],
                    })
                with self.assertRaisesRegex(ValueError, "invalid"):
                    fixture.build()

    def test_verifier_rejects_portable_output_collisions_before_file_io(self) -> None:
        manifest = self.fixture.write()
        document = json.loads(manifest.read_text(encoding="ascii"))
        document["outputs"]["source_dat"] = {
            **document["outputs"]["elf"], "path": "OBJ/GAME.ELF",
        }
        manifest.write_bytes(canonical_json_bytes(document))
        with (
            mock.patch.object(
                release_manifest, "_measure",
                side_effect=AssertionError("output I/O must not precede schema validation"),
            ),
            self.assertRaisesRegex(ValueError, "case-colliding.*output|output.*case-colliding"),
        ):
            release_manifest.verify_release_manifest(manifest)

    def test_verifier_rejects_windows_alias_and_escape_paths_before_file_io(self) -> None:
        for unsafe_path in ("obj/CON", "obj/game.elf.", "../game.elf", ""):
            with self.subTest(unsafe_path=unsafe_path):
                fixture = ReleaseFixture(
                    self.fixture.root / ("unsafe-" + str(len(unsafe_path))) / hashlib.sha256(
                        unsafe_path.encode("ascii")
                    ).hexdigest()[:8]
                )
                manifest = fixture.write()
                document = json.loads(manifest.read_text(encoding="ascii"))
                document["outputs"]["elf"]["path"] = unsafe_path
                manifest.write_bytes(canonical_json_bytes(document))
                with (
                    mock.patch.object(
                        release_manifest,
                        "_resolve_release_path",
                        side_effect=AssertionError("path I/O must follow portable validation"),
                    ),
                    self.assertRaisesRegex(ValueError, "path.*(invalid|portable|canonical)"),
                ):
                    release_manifest.verify_release_manifest(manifest)

    def test_builder_rejects_hardlinked_output_aliases(self) -> None:
        source_dat = self.fixture.outputs["source_dat"]
        source_dat.unlink()
        os.link(self.fixture.outputs["elf"], source_dat)
        with self.assertRaisesRegex(ValueError, "output.*alias"):
            self.fixture.build()

    def test_verifier_rejects_an_injected_output_alias_race(self) -> None:
        self.fixture.outputs["source_dat"].write_bytes(
            self.fixture.outputs["iso"].read_bytes()
        )
        manifest = self.fixture.write()
        real_copy = release_manifest._copy_verified_snapshot

        def alias_before_copy(source: Path, target: Path, record, label: str):
            if label == "source_dat":
                source.unlink()
                os.link(self.fixture.outputs["iso"], source)
            return real_copy(source, target, record, label)

        with (
            mock.patch.object(
                release_manifest,
                "_copy_verified_snapshot",
                side_effect=alias_before_copy,
            ),
            self.assertRaisesRegex(ValueError, "output alias"),
        ):
            release_manifest.verify_release_manifest(manifest)

    def test_compare_accepts_same_bytes_at_different_host_roots(self) -> None:
        first = self.fixture.write()
        relocated = self.fixture.root / "worktree-b"
        shutil.copytree(self.fixture.release_dir, relocated)
        second = relocated / first.name
        comparison = release_manifest.compare_release_manifests(first, second)
        self.assertTrue(comparison["identical"])
        self.assertEqual(comparison["differing_fields"], [])
        self.assertNotIn(str(self.fixture.root), json.dumps(comparison))

    def test_compare_ignores_relative_layout_and_provenance(self) -> None:
        first = self.fixture.write()
        second_root = self.fixture.root / "second-host"
        nested = second_root / "different" / "layout"
        (nested / "obj").mkdir(parents=True)
        for name, relative in {
            "elf": "different/layout/obj/game.elf",
            "source_dat": "different/layout/obj/SOURCE.DAT",
            "iso": "different/layout/game.iso",
            "cue": "different/layout/game.cue",
        }.items():
            target = second_root / relative
            shutil.copyfile(self.fixture.outputs[name], target)
        document = json.loads(first.read_text(encoding="ascii"))
        for name, relative in {
            "elf": "different/layout/obj/game.elf",
            "source_dat": "different/layout/obj/SOURCE.DAT",
            "iso": "different/layout/game.iso",
            "cue": "different/layout/game.cue",
        }.items():
            document["outputs"][name]["path"] = relative
        document["provenance"] = {
            "git_revision": "b" * 40, "closure_clean": False,
        }
        document["mode"] = "development"
        second = second_root / first.name
        second.write_bytes(canonical_json_bytes(document))

        comparison = release_manifest.compare_release_manifests(first, second)
        self.assertTrue(comparison["identical"])
        self.assertEqual(comparison["differing_fields"], [])

    def test_compare_reports_exact_changed_output_field(self) -> None:
        first = self.fixture.write()
        relocated = self.fixture.root / "worktree-b"
        shutil.copytree(self.fixture.release_dir, relocated)
        second = relocated / first.name
        changed_iso = relocated / "game.iso"
        changed_iso.write_bytes(b"different-disc\n")
        document = json.loads(second.read_text(encoding="ascii"))
        document["outputs"]["iso"]["sha256"] = hashlib.sha256(
            changed_iso.read_bytes()
        ).hexdigest()
        document["outputs"]["iso"]["size"] = changed_iso.stat().st_size
        second.write_bytes(canonical_json_bytes(document))
        comparison = release_manifest.compare_release_manifests(first, second)
        self.assertFalse(comparison["identical"])
        self.assertIn("outputs.iso.sha256", comparison["differing_fields"])

    def test_compare_reports_identity_config_and_root_changes_deterministically(self) -> None:
        first = self.fixture.write()
        config_fixture = ReleaseFixture(self.fixture.root / "config-change")
        config_fixture.effective_values["object_pool_capacity"] = 209
        profile = json.loads(config_fixture.profile.read_text(encoding="ascii"))
        profile["effective_config"] = config_fixture.effective_values
        config_fixture._write_canonical(config_fixture.profile, profile)
        config_fixture.rebuild_identity(2)
        config_comparison = release_manifest.compare_release_manifests(
            first, config_fixture.write()
        )
        self.assertFalse(config_comparison["identical"])
        self.assertEqual(
            config_comparison["differing_fields"],
            sorted(config_comparison["differing_fields"]),
        )
        self.assertIn(
            "effective_config.object_pool_capacity",
            config_comparison["differing_fields"],
        )

        root_fixture = ReleaseFixture(self.fixture.root / "root-change")
        package_set = json.loads(root_fixture.package_set.read_text(encoding="ascii"))
        package_class = sorted(target_profile.PACKAGE_CLASSES)[0]
        package_set["package_class_hashes"][package_class] = "f" * 64
        root_fixture._write_canonical(root_fixture.package_set, package_set)
        root_fixture.rebuild_identity(2)
        root_comparison = release_manifest.compare_release_manifests(
            first, root_fixture.write()
        )
        self.assertFalse(root_comparison["identical"])
        self.assertEqual(
            root_comparison["differing_fields"],
            sorted(root_comparison["differing_fields"]),
        )
        self.assertIn("package_set_sha256", root_comparison["differing_fields"])


class ReleaseManifestMissingImplementationTests(unittest.TestCase):
    def test_release_manifest_module_exists(self) -> None:
        self.assertIsNotNone(release_manifest, "release_manifest.py is missing")


if __name__ == "__main__":
    unittest.main()
