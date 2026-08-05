#!/usr/bin/env python3
"""Determinism and descriptor-order tests for the generic S64P compiler."""
from __future__ import annotations

import random
import unittest

from compile_scene_package import DependencyInput, SectionInput, compile_package
from validate_scene_package import PackageValidationError, validate_scene_package


class ScenePackageDeterminismTest(unittest.TestCase):
    def test_shuffled_inputs_produce_byte_identical_packages(self) -> None:
        sections = [
            SectionInput("WORLD_STATIC", b"world", alignment=16),
            SectionInput("COLLISION", b"collision"),
            SectionInput("SKY_BACKGROUND", b"sky"),
            SectionInput("BSP_PORTAL", b"bsp"),
        ]
        dependencies = [
            DependencyInput("AUDIO_DEPENDENCIES", "z-audio", b"z"),
            DependencyInput("ACTOR_DEPENDENCIES", "b-actor", b"b"),
            DependencyInput("ACTOR_DEPENDENCIES", "a-actor", b"a"),
            DependencyInput("ANIMATION_DEPENDENCIES", "walk", b"walk"),
        ]
        expected = compile_package(9, 1, sections, dependencies, provisional=True)
        random.Random(42).shuffle(sections)
        random.Random(84).shuffle(dependencies)
        actual = compile_package(9, 1, sections, dependencies, provisional=True)
        self.assertEqual(actual, expected)

    def test_validator_rejects_nondeterministic_section_order(self) -> None:
        package = compile_package(9, 1, [
            SectionInput("WORLD_STATIC", b"world"),
            SectionInput("COLLISION", b"collision"),
            SectionInput("SKY_BACKGROUND", b"sky"),
            SectionInput("BSP_PORTAL", b"bsp"),
        ], provisional=True)
        damaged = bytearray(package)
        # Swap only the first two kind IDs; descriptor order is no longer canonical.
        damaged[84:86], damaged[148:150] = damaged[148:150], damaged[84:86]
        with self.assertRaisesRegex(PackageValidationError, "descriptor order"):
            validate_scene_package(bytes(damaged), {}, allow_provisional=True)

    def test_duplicate_stable_ids_and_section_kinds_fail_closed(self) -> None:
        with self.assertRaisesRegex(ValueError, "duplicate section"):
            compile_package(9, 1, [
                SectionInput("WORLD_STATIC", b"a"), SectionInput("WORLD_STATIC", b"b")])
        with self.assertRaisesRegex(ValueError, "duplicate dependency"):
            compile_package(9, 1, [], [
                DependencyInput("ACTOR_DEPENDENCIES", "same", b"a"),
                DependencyInput("ACTOR_DEPENDENCIES", "same", b"b"),
            ])


if __name__ == "__main__":
    unittest.main()
