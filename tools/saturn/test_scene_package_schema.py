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
from pathlib import Path

from compile_scene_package import (
    HEADER_SIZE,
    DependencyInput,
    SectionInput,
    canonical_dependency_masks,
    compile_package,
    parse_package,
)
from validate_scene_package import PackageValidationError, validate_scene_package
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
                "--payload-manifest", str(manifest), "--symbol-prefix", "fixture",
            ], text=True, capture_output=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("FIXTURE_S64P_PACKAGE_SHA256",
                          (root / "scene.h").read_text(encoding="utf-8"))
            self.assertIn("sm64_saturn_scene_dependency_descriptor",
                          (root / "saturn_scene_package_abi.h").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
