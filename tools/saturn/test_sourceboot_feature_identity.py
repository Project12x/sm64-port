#!/usr/bin/env python3
"""Capture-side mutation contracts for the compiled sourceboot identity."""

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

import capture_sourceboot_throughput as capture
import gen_build_identity as identity


class SourcebootFeatureIdentityTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        artifacts = {}
        for index, field in enumerate(identity.ARTIFACT_HASH_FIELDS):
            path = self.root / f"{field}.bin"
            path.write_bytes(bytes([index + 1]) * (index + 3))
            artifacts[field] = {
                "path": str(path),
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            }
        self.spec = {
            "features": {"complete_mario_animation": 1,
                         "dynamic_actor_closure": 1,
                         "semantic_audio": 1},
            "renderer_pipeline": 4, "level_id": 9, "area_id": 1,
            "route_id": 7, "route_replay_mode": 1, "live_input_mode": 1,
            "camera_route": 0, "camera_variant": 3, "diagnostic_mode": 0,
            "bootstrap_ticks": 600, "cart_mbit": 32,
            "cart_stage_sectors": 8, "hot_promotion": 1, "near_clip": 1,
            "bsp_order": 1, "polygon_tier": 2, "fragment_mode": 0,
            "artifacts": artifacts,
        }
        self.raw = identity.build_identity(self.spec).raw
        self.label = identity.identity_label(self.raw)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_capture_accepts_only_matching_loaded_elf_tuple_and_label(self) -> None:
        result = capture.validate_build_identity(
            self.raw, self.raw, expected_label=self.label
        )
        self.assertTrue(result["match"])
        self.assertEqual(result["label"], self.label)
        mismatch = bytearray(self.raw)
        mismatch[-1] ^= 1
        with self.assertRaisesRegex(ValueError, "loaded.*ELF|tuple"):
            capture.validate_build_identity(
                self.raw, bytes(mismatch), expected_label=self.label
            )
        with self.assertRaisesRegex(ValueError, "label"):
            capture.validate_build_identity(
                self.raw, self.raw, expected_label=self.label + "-drift"
            )

    def test_every_identity_field_mutation_is_rejected(self) -> None:
        parsed = identity.parse_identity(self.raw)
        scalar_fields = (
            "magic", "version", "size", "feature_bits", "renderer_pipeline",
            "level_id", "area_id", "route_id", "route_replay_mode",
            "live_input_mode", "camera_route", "camera_variant",
            "diagnostic_mode", "reserved0", "bootstrap_ticks", "cart_mbit",
            "cart_stage_sectors", "hot_promotion", "near_clip", "bsp_order",
            "polygon_tier", "fragment_mode", "reserved1",
        )
        hash_fields = (
            "source_hash", "effective_config_hash", "route_artifact_hash",
            "input_artifact_hash", "camera_artifact_hash", "cart_profile_hash",
            "scene_package_hash", "scene_dependency_set_hash",
            "actor_package_hash", "animation_package_hash", "audio_package_hash",
        )
        for field in scalar_fields:
            with self.subTest(field=field):
                mutant = copy.deepcopy(parsed)
                mutant[field] ^= 1
                with self.assertRaises(ValueError):
                    capture.validate_build_identity(
                        self.raw, identity.pack_identity(mutant),
                        expected_label=self.label,
                    )
        for field in hash_fields:
            with self.subTest(field=field):
                mutant = copy.deepcopy(parsed)
                mutant[field] = ("00" if mutant[field][:2] != "00" else "ff") + mutant[field][2:]
                with self.assertRaises(ValueError):
                    capture.validate_build_identity(
                        self.raw, identity.pack_identity(mutant),
                        expected_label=self.label,
                    )
        for bit in (1, 2, 4):
            with self.subTest(feature_bit=bit):
                mutant = copy.deepcopy(parsed)
                mutant["feature_bits"] ^= bit
                with self.assertRaises(ValueError):
                    capture.validate_build_identity(
                        self.raw, identity.pack_identity(mutant),
                        expected_label=self.label,
                    )

    def test_resolver_rejects_absent_or_wrong_sized_elf_identity(self) -> None:
        with self.assertRaisesRegex(ValueError, "missing.*saturn_build_identity"):
            capture.resolve_build_identity_symbol({})
        with self.assertRaisesRegex(ValueError, "wrong size"):
            capture.resolve_build_identity_symbol(
                {"saturn_build_identity": {"address": 0x06020000, "size": 403}}
            )
        self.assertEqual(
            capture.resolve_build_identity_symbol(
                {"saturn_build_identity": {"address": 0x06020000, "size": 404}}
            )["address"],
            0x06020000,
        )

    def test_reserved_and_unknown_feature_bits_are_invalid_even_without_expected_tuple(self) -> None:
        for field in ("reserved0", "reserved1"):
            parsed = identity.parse_identity(self.raw)
            parsed[field] = 1
            with self.assertRaisesRegex(ValueError, "reserved"):
                identity.validate_identity(identity.pack_identity(parsed))
        parsed = identity.parse_identity(self.raw)
        parsed["feature_bits"] |= 1 << 3
        with self.assertRaisesRegex(ValueError, "feature"):
            identity.validate_identity(identity.pack_identity(parsed))


if __name__ == "__main__":
    unittest.main()
