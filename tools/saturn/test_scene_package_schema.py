#!/usr/bin/env python3
"""Mutation tests for the version-one, big-endian S64P package ABI."""
from __future__ import annotations

import hashlib
import json
import struct
import subprocess
import sys
import tempfile
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from unittest import mock

from compile_scene_package import (
    HEADER_SIZE,
    DependencyInput,
    SectionInput,
    canonical_dependency_masks,
    compile_package,
    parse_package,
)
from validate_scene_package import (
    PackageValidationError,
    load_payloads,
    validate_scene_package,
)
from emit_scene_package_header import emit_abi_header, emit_header


def fixture(*, provisional: bool = False) -> tuple[bytes, dict[str, tuple[bytes, int]]]:
    payloads = {
        "goomba": (b"actor-goomba-v1", 7),
        "mario-walk": (b"animation-walk-v1", 7),
        "bob-music": (b"audio-bob-v1", 7),
    }
    sections = [
        SectionInput("WORLD_STATIC", b"world", alignment=16, destination_class="LWRAM"),
        SectionInput("COLLISION", b"collision", alignment=4, destination_class="HWRAM"),
        SectionInput("SKY_BACKGROUND", b"sky", alignment=8, destination_class="VRAM"),
        SectionInput("BSP_PORTAL", b"bsp", alignment=4, destination_class="HWRAM",
                     dependency_mask=1 << 0),
    ]
    dependencies = [
        DependencyInput("ACTOR_DEPENDENCIES", "goomba", payloads["goomba"][0],
                        destination_class="CART", lifetime="AREA", generation=7),
        DependencyInput("ANIMATION_DEPENDENCIES", "mario-walk", payloads["mario-walk"][0],
                        destination_class="CART", lifetime="SCENE", generation=7),
        DependencyInput("AUDIO_DEPENDENCIES", "bob-music", payloads["bob-music"][0],
                        destination_class="SOUND_RAM", lifetime="SCENE", generation=7),
    ]
    return compile_package(9, 1, sections, dependencies, provisional=provisional), payloads


def mutate_dependencies(package: bytes,
                        changes: list[tuple[str, int, bytes]]) -> bytes:
    """Mutate dependency bytes and reseal their containing sections/root."""
    damaged = bytearray(package)
    parsed = parse_package(package)
    by_kind = {section["kind"]: section for section in parsed["sections"]}
    changed_kinds: set[str] = set()
    for kind, relative_offset, replacement in changes:
        section = by_kind[kind]
        offset = section["offset"] + 4 + relative_offset
        damaged[offset:offset + len(replacement)] = replacement
        changed_kinds.add(kind)
    for kind in changed_kinds:
        section = by_kind[kind]
        content = damaged[section["offset"]:section["offset"] + section["size"]]
        digest = hashlib.sha256(content).digest()
        descriptor_hash = section["descriptor_offset"] + 28
        damaged[descriptor_hash:descriptor_hash + 32] = digest
    damaged[20:52] = bytes(32)
    damaged[20:52] = hashlib.sha256(damaged).digest()
    return bytes(damaged)


class ScenePackageSchemaTest(unittest.TestCase):
    def test_bob_header_is_big_endian_and_has_exact_fields(self) -> None:
        package, payloads = fixture(provisional=True)
        parsed = validate_scene_package(package, payloads, allow_provisional=True)
        fields = struct.unpack_from(">IHHIHHHH32s32s", package)
        self.assertEqual(fields[:8], (0x53363450, 1, HEADER_SIZE, len(package), 9, 1, 8, 1))
        self.assertEqual(fields[8].hex(), parsed["package_sha256"])
        self.assertEqual(fields[9].hex(), parsed["dependency_set_sha256"])
        self.assertEqual([section["kind"] for section in parsed["sections"]], [
            "WORLD_STATIC", "COLLISION", "SKY_BACKGROUND", "BSP_PORTAL",
            "ACTOR_DEPENDENCIES", "ANIMATION_DEPENDENCIES",
            "AUDIO_DEPENDENCIES", "RESIDENCY_PLAN",
        ])

    def test_rejects_wrong_magic_version_root_and_dependency_set_hash(self) -> None:
        package, payloads = fixture()
        mutations = [(0, b"BAD!", "magic"), (5, b"\x02", "version"),
                     (20, bytes([package[20] ^ 1]), "package SHA-256"),
                     (52, bytes([package[52] ^ 1]), "dependency-set SHA-256")]
        for offset, replacement, message in mutations:
            with self.subTest(message=message):
                damaged = bytearray(package)
                damaged[offset:offset + len(replacement)] = replacement
                with self.assertRaisesRegex(PackageValidationError, message):
                    validate_scene_package(bytes(damaged), payloads)

    def test_rejects_missing_extra_wrong_generation_and_wrong_hash_payloads(self) -> None:
        package, payloads = fixture()
        cases: list[tuple[dict[str, tuple[bytes, int]], str]] = [
            ({key: value for key, value in payloads.items() if key != "goomba"}, "missing payload"),
            ({**payloads, "extra": (b"extra", 7)}, "extra payload"),
        ]
        for stable_id in ("goomba", "mario-walk", "bob-music"):
            cases.append(({**payloads, stable_id: (payloads[stable_id][0], 8)}, "generation"))
            cases.append(({**payloads, stable_id: (b"wrong", 7)}, "payload hash"))
        for evidence, message in cases:
            with self.subTest(message=message):
                with self.assertRaisesRegex(PackageValidationError, message):
                    validate_scene_package(package, evidence)

    def test_rejects_unknown_kind_lifetime_destination_and_flags(self) -> None:
        package, payloads = fixture()
        parsed = parse_package(package)
        descriptor = parsed["sections"][0]["descriptor_offset"]
        mutations = [
            (descriptor, struct.pack(">H", 99), "section kind"),
            (descriptor + 2, b"\x7f", "destination"),
            (descriptor + 3, b"\x7f", "lifetime"),
            (18, struct.pack(">H", 0x8000), "flags"),
        ]
        for offset, replacement, message in mutations:
            damaged = bytearray(package)
            damaged[offset:offset + len(replacement)] = replacement
            with self.subTest(message=message):
                with self.assertRaisesRegex(PackageValidationError, message):
                    validate_scene_package(bytes(damaged), payloads)

    def test_rejects_overlap_out_of_order_bad_alignment_and_dependency_cycle(self) -> None:
        package, payloads = fixture()
        parsed = parse_package(package)
        first = parsed["sections"][0]
        second = parsed["sections"][1]
        cases: list[tuple[bytes, str]] = []
        damaged = bytearray(package)
        struct.pack_into(">I", damaged, second["descriptor_offset"] + 8, first["offset"])
        cases.append((bytes(damaged), "overlap|order"))
        damaged = bytearray(package)
        struct.pack_into(">I", damaged, first["descriptor_offset"] + 16, 3)
        cases.append((bytes(damaged), "alignment"))
        damaged = bytearray(package)
        struct.pack_into(">I", damaged, first["descriptor_offset"] + 20, 1 << 3)
        struct.pack_into(">I", damaged, parsed["sections"][3]["descriptor_offset"] + 20, 1 << 0)
        cases.append((bytes(damaged), "cycle"))
        for blob, message in cases:
            with self.subTest(message=message):
                with self.assertRaisesRegex(PackageValidationError, message):
                    validate_scene_package(blob, payloads)

    def test_rejects_budget_overflow_and_provisional_target_use(self) -> None:
        package, payloads = fixture()
        with self.assertRaisesRegex(PackageValidationError, "budget"):
            validate_scene_package(package, payloads, budgets={"HWRAM": 1})
        provisional, provisional_payloads = fixture(provisional=True)
        with self.assertRaisesRegex(PackageValidationError, "provisional"):
            validate_scene_package(provisional, provisional_payloads)

    def test_dependency_set_is_bound_to_sorted_payload_hashes(self) -> None:
        package, payloads = fixture()
        parsed = validate_scene_package(package, payloads)
        self.assertNotEqual(parsed["dependency_set_sha256"], hashlib.sha256(b"").hexdigest())

    def test_dependency_masks_use_canonical_stable_id_references(self) -> None:
        dependencies = [
            DependencyInput("AUDIO_DEPENDENCIES", "audio", b"audio"),
            DependencyInput("ACTOR_DEPENDENCIES", "actor", b"actor",
                            dependencies=("audio",)),
            DependencyInput("ANIMATION_DEPENDENCIES", "animation", b"animation",
                            dependencies=("actor",)),
        ]
        masks = canonical_dependency_masks(dependencies)
        self.assertEqual(masks, {"actor": 1 << 2, "animation": 1 << 0, "audio": 0})
        package = compile_package(9, 1, [], dependencies)
        payloads = {item.stable_id: (item.data, 1) for item in dependencies}
        report = validate_scene_package(package, payloads)
        self.assertEqual({item["stable_id"]: item["dependency_mask"]
                          for item in report["dependencies"]}, masks)

    def test_compiler_rejects_absent_cycles_and_more_than_32_dependencies(self) -> None:
        with self.assertRaisesRegex(ValueError, "unknown dependency reference"):
            compile_package(9, 1, [], [
                DependencyInput("ACTOR_DEPENDENCIES", "actor", b"actor",
                                dependencies=("absent",))])
        with self.assertRaisesRegex(ValueError, "dependency cycle"):
            compile_package(9, 1, [], [
                DependencyInput("ACTOR_DEPENDENCIES", "actor", b"actor",
                                dependencies=("audio",)),
                DependencyInput("AUDIO_DEPENDENCIES", "audio", b"audio",
                                dependencies=("actor",)),
            ])
        with self.assertRaisesRegex(ValueError, "at most 32"):
            compile_package(9, 1, [], [
                DependencyInput("ACTOR_DEPENDENCIES", f"actor-{index}", bytes([index]))
                for index in range(33)])

    def test_rejects_mutated_payload_cycles_absent_bits_and_descriptor_enums(self) -> None:
        package, payloads = fixture()
        cases = [
            ([("ACTOR_DEPENDENCIES", 44, struct.pack(">I", 1 << 1)),
              ("ANIMATION_DEPENDENCIES", 44, struct.pack(">I", 1 << 0))], "payload dependency cycle"),
            ([("ACTOR_DEPENDENCIES", 44, struct.pack(">I", 1 << 31))], "absent record"),
            ([("ACTOR_DEPENDENCIES", 0, struct.pack(">H", 99))], "payload kind"),
            ([("ACTOR_DEPENDENCIES", 2, b"\x7f")], "destination"),
            ([("ACTOR_DEPENDENCIES", 3, b"\x7f")], "lifetime"),
            ([("ACTOR_DEPENDENCIES", 40, struct.pack(">I", 3))], "alignment"),
            ([("ACTOR_DEPENDENCIES", 88, struct.pack(">I", 1))], "metadata"),
        ]
        for changes, message in cases:
            with self.subTest(message=message):
                with self.assertRaisesRegex(PackageValidationError, message):
                    validate_scene_package(mutate_dependencies(package, changes), payloads)

    def test_emitted_header_freezes_the_binary_abi_and_package_identity(self) -> None:
        package, payloads = fixture(provisional=True)
        parsed = validate_scene_package(package, payloads, allow_provisional=True)
        header = emit_header(package, "bob_area1")
        self.assertIn("#define BOB_AREA1_S64P_HEADER_SIZE 84U", header)
        self.assertIn("#define BOB_AREA1_S64P_SECTION_DESCRIPTOR_SIZE 64U", header)
        self.assertIn("#define BOB_AREA1_S64P_DEPENDENCY_DESCRIPTOR_SIZE 96U", header)
        self.assertIn(parsed["package_sha256"], header)
        self.assertIn('#include "saturn_scene_package_abi.h"', header)
        self.assertNotIn("typedef struct", header)
        abi = emit_abi_header()
        self.assertIn("SM64_SATURN_S64P_WORLD_STATIC = 1", abi)
        self.assertIn("typedef struct sm64_saturn_scene_package_header", abi)

    def test_dependency_bearing_header_cli_validates_three_payloads(self) -> None:
        package, payloads = fixture()
        with tempfile.TemporaryDirectory(prefix="s64p-header-") as temporary:
            root = Path(temporary)
            package_path = root / "scene.s64p"
            package_path.write_bytes(package)
            entries = []
            for stable_id, (data, generation) in payloads.items():
                payload_path = root / f"{stable_id}.bin"
                payload_path.write_bytes(data)
                entries.append({"stable_id": stable_id,
                                "path": payload_path.name,
                                "generation": generation})
            manifest = root / "payloads.json"
            manifest.write_text(json.dumps({"payloads": entries}), encoding="utf-8")
            result = subprocess.run([
                sys.executable, str(Path(__file__).with_name("emit_scene_package_header.py")),
                "--input", str(package_path), "--output", str(root / "scene.h"),
                "--abi-output", str(root / "saturn_scene_package_abi.h"),
                "--payload-manifest", str(manifest),
                "--payload-root", str(root), "--symbol-prefix", "fixture",
            ], text=True, capture_output=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("FIXTURE_S64P_PACKAGE_SHA256",
                          (root / "scene.h").read_text(encoding="utf-8"))
            self.assertIn("sm64_saturn_scene_dependency_descriptor",
                          (root / "saturn_scene_package_abi.h").read_text(encoding="utf-8"))

    def test_compiler_cli_consumes_canonical_dependency_sidecar(self) -> None:
        payload = b"S64F-v3-actor-bundle"
        with tempfile.TemporaryDirectory(prefix="s64p-dependency-cli-") as temporary:
            root = Path(temporary)
            payload_path = root / "actors.s64f"
            manifest_path = root / "actors-dependency.json"
            output_path = root / "scene.s64p"
            report_path = root / "scene.json"
            payload_manifest_path = root / "payloads.json"
            payload_path.write_bytes(payload)
            manifest = {
                "alignment": 4,
                "byte_count": len(payload),
                "destination_class": "CART",
                "generation": 11,
                "kind": "ACTOR_DEPENDENCIES",
                "lifetime": "SCENE",
                "max_scratch": 0,
                "sha256": hashlib.sha256(payload).hexdigest(),
                "stable_id": "bob-area1-actors-v3",
            }
            manifest_path.write_text(
                json.dumps(manifest, sort_keys=True), encoding="utf-8")
            result = subprocess.run([
                sys.executable,
                str(Path(__file__).with_name("compile_scene_package.py")),
                "--level-id", "9", "--area-id", "1",
                "--dependency-manifest", str(manifest_path),
                "--dependency-payload", str(payload_path),
                "--payload-root", str(root),
                "--output", str(output_path),
                "--metadata-output", str(report_path),
                "--payload-manifest-output", str(payload_manifest_path),
            ], text=True, capture_output=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(payload_manifest_path.read_text(
                encoding="utf-8")), {"payloads": [{
                    "generation": 11,
                    "path": "actors.s64f",
                    "stable_id": "bob-area1-actors-v3",
                }]})
            parsed = validate_scene_package(
                output_path.read_bytes(), {"bob-area1-actors-v3": (payload, 11)})
            self.assertFalse(parsed["provisional"])
            self.assertEqual(parsed["dependencies"], [{
                "kind": "ACTOR_DEPENDENCIES",
                "stable_id": "bob-area1-actors-v3",
                "byte_count": len(payload),
                "alignment": 4,
                "destination_class": "CART",
                "lifetime": "SCENE",
                "dependency_mask": 0,
                "max_scratch": 0,
                "sha256": hashlib.sha256(payload).hexdigest(),
                "generation": 11,
            }])

    def test_compiler_cli_rejects_dependency_sidecar_byte_drift(self) -> None:
        payload = b"S64F-v3-actor-bundle"
        with tempfile.TemporaryDirectory(prefix="s64p-dependency-drift-") as temporary:
            root = Path(temporary)
            payload_path = root / "actors.s64f"
            manifest_path = root / "actors-dependency.json"
            payload_path.write_bytes(payload)
            manifest_path.write_text(json.dumps({
                "alignment": 4,
                "byte_count": len(payload),
                "destination_class": "CART",
                "generation": 11,
                "kind": "ACTOR_DEPENDENCIES",
                "lifetime": "SCENE",
                "max_scratch": 0,
                "sha256": "00" * 32,
                "stable_id": "bob-area1-actors-v3",
            }, sort_keys=True), encoding="utf-8")
            result = subprocess.run([
                sys.executable,
                str(Path(__file__).with_name("compile_scene_package.py")),
                "--level-id", "9", "--area-id", "1",
                "--dependency-manifest", str(manifest_path),
                "--dependency-payload", str(payload_path),
                "--payload-root", str(root),
                "--output", str(root / "scene.s64p"),
            ], text=True, capture_output=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("dependency SHA-256 mismatch", result.stderr)

    def test_external_payload_paths_cannot_escape_declared_root(self) -> None:
        with tempfile.TemporaryDirectory(prefix="s64p-payload-root-") as temporary:
            root = Path(temporary)
            package_root = root / "package"
            package_root.mkdir()
            outside = root / "outside.s64f"
            outside.write_bytes(b"outside")
            manifest = package_root / "dependency.json"
            manifest.write_text(json.dumps({
                "alignment": 4,
                "byte_count": len(outside.read_bytes()),
                "destination_class": "CART",
                "generation": 11,
                "kind": "ACTOR_DEPENDENCIES",
                "lifetime": "SCENE",
                "max_scratch": 0,
                "sha256": hashlib.sha256(outside.read_bytes()).hexdigest(),
                "stable_id": "bob-area1-actors-v3",
            }, sort_keys=True), encoding="utf-8")
            result = subprocess.run([
                sys.executable,
                str(Path(__file__).with_name("compile_scene_package.py")),
                "--level-id", "9", "--area-id", "1",
                "--dependency-manifest", str(manifest),
                "--dependency-payload", str(outside),
                "--payload-root", str(package_root),
                "--output", str(package_root / "scene.s64p"),
            ], text=True, capture_output=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("dependency payload escapes payload root", result.stderr)
            payload_manifest = package_root / "payloads.json"
            payload_manifest.write_text(json.dumps({"payloads": [{
                "generation": 11,
                "path": "../outside.s64f",
                "stable_id": "bob-area1-actors-v3",
            }]}), encoding="utf-8")
            with self.assertRaisesRegex(
                    PackageValidationError,
                    "payload manifest path escapes payload root"):
                load_payloads(payload_manifest, package_root)

    def test_make_compiles_minimal_final_actor_scene_package(self) -> None:
        makefile = Path(__file__).resolve().parents[2].joinpath(
            "Makefile.saturn.mk").read_text(encoding="utf-8")
        self.assertIn("compile-actor-scene-package:", makefile)
        start = makefile.index("compile-actor-scene-package:")
        end = makefile.index("\n\n", start)
        recipe = makefile[start:end]
        self.assertIn(
            "compile-actor-scene-package: verify-scene-package-schema", recipe)
        self.assertIn("--verify-publication", recipe)
        self.assertIn("compile-actor-banks", recipe)
        self.assertIn("compile-actor-family-bundle", recipe)
        self.assertIn("$(ACTOR_FAMILY_BUNDLE_DEPENDENCY)", recipe)
        self.assertIn("$(ACTOR_FAMILY_BUNDLE_PAYLOAD)", recipe)
        self.assertIn("--dependency-manifest", recipe)
        self.assertIn("--dependency-payload", recipe)
        self.assertIn("--metadata-output", recipe)
        self.assertIn("--payload-manifest-output", recipe)
        self.assertIn("$(SCENE_PACKAGE_FINAL_ASM)", recipe)
        self.assertIn("$(SCENE_PACKAGE_FINAL_ROOT)", recipe)
        self.assertIn("$(ACTOR_FAMILY_BUNDLE_PAYLOAD)", recipe)
        self.assertIn("--assembly-output", recipe)
        self.assertIn("--assembly-base", recipe)
        self.assertIn("--validation-output", recipe)
        self.assertIn("--header-output", recipe)
        self.assertIn("--abi-output", recipe)
        self.assertNotIn('> "$(SCENE_PACKAGE_FINAL_ASM)"', recipe)
        self.assertNotIn("validate_scene_package.py", recipe)
        self.assertNotIn("emit_scene_package_header.py", recipe)
        self.assertNotIn("--provisional", recipe)
        self.assertNotIn("--section", recipe)
        repo_root = Path(__file__).resolve().parents[2]
        compiler = (repo_root / "tools" / "saturn" /
                    "compile_scene_package.py").read_text(encoding="utf-8")
        self.assertIn("_sm64_saturn_sourceboot_scene_package_root", compiler)
        self.assertIn("_sm64_saturn_sourceboot_actor_bundle", compiler)

    def test_final_publication_is_verify_only_and_never_clobbers(self) -> None:
        payload = b"S64F-v3-actor-bundle"
        with tempfile.TemporaryDirectory(prefix="s64p-no-clobber-") as temporary:
            root = Path(temporary)
            payload_path = root / "actors.s64f"
            manifest_path = root / "actors-dependency.json"
            output_path = root / "scene.s64p"
            report_path = root / "scene.json"
            payload_manifest_path = root / "payloads.json"
            assembly_path = root / "actor_scene_bundle.sx"
            validation_path = root / "validation.json"
            header_path = root / "scene.h"
            abi_path = root / "abi.h"
            payload_path.write_bytes(payload)
            manifest_path.write_text(json.dumps({
                "alignment": 4,
                "byte_count": len(payload),
                "destination_class": "CART",
                "generation": 14,
                "kind": "ACTOR_DEPENDENCIES",
                "lifetime": "SCENE",
                "max_scratch": 0,
                "sha256": hashlib.sha256(payload).hexdigest(),
                "stable_id": "bob-area1-actors-v3",
            }, sort_keys=True), encoding="utf-8")
            command = [
                sys.executable,
                str(Path(__file__).with_name("compile_scene_package.py")),
                "--level-id", "9", "--area-id", "1",
                "--dependency-manifest", str(manifest_path),
                "--dependency-payload", str(payload_path),
                "--payload-root", str(root),
                "--output", str(output_path),
                "--metadata-output", str(report_path),
                "--payload-manifest-output", str(payload_manifest_path),
                "--assembly-output", str(assembly_path),
                "--assembly-base", str(root),
                "--validation-output", str(validation_path),
                "--header-output", str(header_path),
                "--abi-output", str(abi_path),
                "--symbol-prefix", "fixture",
            ]
            first = subprocess.run(
                command, text=True, capture_output=True, check=False)
            self.assertEqual(first.returncode, 0, first.stderr)
            published = {
                path: path.read_bytes() for path in (
                    output_path, report_path, payload_manifest_path,
                    assembly_path, validation_path, header_path, abi_path)
            }
            repeat = subprocess.run(
                command, text=True, capture_output=True, check=False)
            self.assertEqual(repeat.returncode, 0, repeat.stderr)
            self.assertEqual(published, {
                path: path.read_bytes() for path in published
            })
            damaged = bytearray(output_path.read_bytes())
            damaged[-1] ^= 1
            output_path.write_bytes(damaged)
            before_failed_repeat = {
                path: path.read_bytes() for path in published
            }
            failed = subprocess.run(
                command, text=True, capture_output=True, check=False)
            self.assertNotEqual(failed.returncode, 0)
            self.assertIn("publication target exists", failed.stderr)
            self.assertEqual(before_failed_repeat, {
                path: path.read_bytes() for path in published
            })

    def test_final_publication_rolls_back_every_late_conflict(self) -> None:
        payload = b"S64F-v3-actor-bundle"
        output_names = (
            "scene.s64p", "payloads.json", "actor_scene_bundle.sx",
            "validation.json", "scene.h", "abi.h", "scene.json")
        with tempfile.TemporaryDirectory(prefix="s64p-set-rollback-") as temporary:
            base = Path(temporary)
            for conflict_name in output_names:
                root = base / conflict_name.replace(".", "-")
                root.mkdir()
                payload_path = root / "actors.s64f"
                manifest_path = root / "actors-dependency.json"
                payload_path.write_bytes(payload)
                manifest_path.write_text(json.dumps({
                    "alignment": 4,
                    "byte_count": len(payload),
                    "destination_class": "CART",
                    "generation": 14,
                    "kind": "ACTOR_DEPENDENCIES",
                    "lifetime": "SCENE",
                    "max_scratch": 0,
                    "sha256": hashlib.sha256(payload).hexdigest(),
                    "stable_id": "bob-area1-actors-v3",
                }, sort_keys=True), encoding="utf-8")
                outputs = {name: root / name for name in output_names}
                outputs[conflict_name].write_bytes(b"foreign\n")
                command = [
                    sys.executable,
                    str(Path(__file__).with_name("compile_scene_package.py")),
                    "--level-id", "9", "--area-id", "1",
                    "--dependency-manifest", str(manifest_path),
                    "--dependency-payload", str(payload_path),
                    "--payload-root", str(root),
                    "--output", str(outputs["scene.s64p"]),
                    "--payload-manifest-output", str(outputs["payloads.json"]),
                    "--assembly-output", str(outputs["actor_scene_bundle.sx"]),
                    "--assembly-base", str(root),
                    "--validation-output", str(outputs["validation.json"]),
                    "--header-output", str(outputs["scene.h"]),
                    "--abi-output", str(outputs["abi.h"]),
                    "--metadata-output", str(outputs["scene.json"]),
                    "--symbol-prefix", "fixture",
                ]
                failed = subprocess.run(
                    command, text=True, capture_output=True, check=False)
                self.assertNotEqual(failed.returncode, 0, conflict_name)
                self.assertIn("publication target exists", failed.stderr)
                for name, path in outputs.items():
                    if name == conflict_name:
                        self.assertEqual(path.read_bytes(), b"foreign\n")
                    else:
                        self.assertFalse(path.exists(),
                                         f"partial publication at {name}")

    def test_publication_set_concurrent_producers_never_mix(self) -> None:
        from compile_scene_package import publish_or_verify_set

        with tempfile.TemporaryDirectory(prefix="s64p-set-concurrent-") as temporary:
            root = Path(temporary)
            paths = tuple(root / f"output-{index}.bin" for index in range(4))
            first = tuple((path, f"first-{index}".encode())
                          for index, path in enumerate(paths))
            second = tuple((path, f"second-{index}".encode())
                           for index, path in enumerate(paths))
            with ThreadPoolExecutor(max_workers=8) as pool:
                identical = [pool.submit(publish_or_verify_set, first)
                             for _ in range(8)]
                for future in identical:
                    future.result()
            self.assertEqual(tuple(path.read_bytes() for path in paths),
                             tuple(raw for _, raw in first))
            for path in paths:
                path.unlink()
            errors = []
            with ThreadPoolExecutor(max_workers=8) as pool:
                contenders = [pool.submit(
                    publish_or_verify_set, first if index % 2 == 0 else second)
                    for index in range(8)]
                for future in contenders:
                    try:
                        future.result()
                    except ValueError as exc:
                        errors.append(str(exc))
            final = tuple(path.read_bytes() for path in paths)
            self.assertIn(final, (tuple(raw for _, raw in first),
                                  tuple(raw for _, raw in second)))
            self.assertGreater(len(errors), 0)

    def test_publication_set_rolls_back_owned_links_at_every_position(self) -> None:
        from compile_scene_package import publish_or_verify_set
        import compile_scene_package

        with tempfile.TemporaryDirectory(prefix="s64p-set-link-fault-") as temporary:
            base = Path(temporary)
            real_link = compile_scene_package.os.link
            for fail_at in range(4):
                root = base / str(fail_at)
                files = tuple((root / f"output-{index}.bin",
                               f"bytes-{index}".encode())
                              for index in range(4))
                calls = 0

                def conflicting_link(source, target):
                    nonlocal calls
                    position = calls
                    calls += 1
                    if position == fail_at:
                        Path(target).write_bytes(b"foreign\n")
                        raise FileExistsError("injected late conflict")
                    return real_link(source, target)

                with mock.patch.object(
                        compile_scene_package.os, "link",
                        side_effect=conflicting_link):
                    with self.assertRaisesRegex(
                            ValueError, "publication target exists"):
                        publish_or_verify_set(files)
                for index, (path, _) in enumerate(files):
                    if index == fail_at:
                        self.assertEqual(path.read_bytes(), b"foreign\n")
                    else:
                        self.assertFalse(
                            path.exists(), f"owned link {index} was not rolled back")

    def test_validator_and_header_reports_never_clobber_drift(self) -> None:
        package = compile_package(9, 1, [], [])
        with tempfile.TemporaryDirectory(prefix="s64p-sidecar-no-clobber-") as temporary:
            root = Path(temporary)
            package_path = root / "scene.s64p"
            validation_path = root / "validation.json"
            header_path = root / "scene.h"
            abi_path = root / "abi.h"
            package_path.write_bytes(package)
            validator = [
                sys.executable,
                str(Path(__file__).with_name("validate_scene_package.py")),
                "--input", str(package_path), "--report", str(validation_path),
            ]
            header = [
                sys.executable,
                str(Path(__file__).with_name("emit_scene_package_header.py")),
                "--input", str(package_path), "--output", str(header_path),
                "--abi-output", str(abi_path), "--symbol-prefix", "fixture",
            ]
            self.assertEqual(subprocess.run(
                validator, text=True, capture_output=True,
                check=False).returncode, 0)
            self.assertEqual(subprocess.run(
                header, text=True, capture_output=True,
                check=False).returncode, 0)
            validation_path.write_bytes(b"foreign validation\n")
            result = subprocess.run(
                validator, text=True, capture_output=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("publication target exists", result.stderr)
            self.assertEqual(validation_path.read_bytes(), b"foreign validation\n")
            header_path.unlink()
            abi_path.write_bytes(b"foreign abi\n")
            result = subprocess.run(
                header, text=True, capture_output=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("publication target exists", result.stderr)
            self.assertFalse(header_path.exists(),
                             "late ABI conflict left a partial header")
            self.assertEqual(abi_path.read_bytes(), b"foreign abi\n")

    def test_sourceboot_restores_command_prefix_after_cold_stage(self) -> None:
        source = Path(__file__).resolve().parents[2].joinpath(
            "src/port/saturn/sourceboot/main.c").read_text(encoding="utf-8")
        activation = source.index("sm64_saturn_source_scene_bundle_init(")
        initializers = []
        cursor = 0
        needle = "sm64_saturn_vdp1_backend_init_with_storage("
        while (position := source.find(needle, cursor)) >= 0:
            initializers.append(position)
            cursor = position + len(needle)
        self.assertEqual(len(initializers), 2)
        self.assertTrue(all(position > activation for position in initializers),
                        "command prefixes must be initialized after the borrowed cold stage retires")


if __name__ == "__main__":
    unittest.main()
