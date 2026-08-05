#!/usr/bin/env python3
"""Static ownership checks for the generic S64P runtime boundary."""
from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RUNTIME = ROOT / "src/port/saturn/runtime"
SNAPSHOT = ROOT / "src/port/saturn/gfx/saturn_render_snapshot.h"
SOURCE_CART_C = ROOT / "src/port/saturn/sourceboot/source_cart.c"
SOURCE_CART_MAKE = ROOT / "src/port/saturn/sourceboot/Makefile"
SOURCEBOOT_MAIN = ROOT / "src/port/saturn/sourceboot/main.c"


class SceneRuntimeNeutralityTest(unittest.TestCase):
    def test_generic_runtime_has_no_level_names(self) -> None:
        text = "\n".join(path.read_text(encoding="utf-8") for path in (
            RUNTIME / "saturn_scene_package.h", RUNTIME / "saturn_scene_package.c",
            RUNTIME / "saturn_scene_residency.h", RUNTIME / "saturn_scene_residency.c"))
        self.assertIsNone(re.search(r"\b(?:bob|wf)\b", text, re.IGNORECASE))

    def test_peer_snapshot_identity_is_pointer_free(self) -> None:
        text = SNAPSHOT.read_text(encoding="utf-8")
        body = re.search(r"typedef struct sm64_saturn_render_snapshot \{(.*?)\n\} sm64_saturn_render_snapshot_t;", text, re.DOTALL)
        self.assertIsNotNone(body)
        self.assertNotIn("*", body.group(1))
        for field in ("scene_package_id", "active_feature_mask", "scene_package_sha256",
                      "scene_dependency_set_sha256", "actor_bank_identity",
                      "animation_bank_identity", "audio_bank_identity"):
            self.assertIn(field, body.group(1))

    def test_target_boundary_rejects_provisional_roots(self) -> None:
        text = SOURCE_CART_C.read_text(encoding="utf-8")
        function = re.search(r"bool sm64_saturn_source_cart_scene_package_validate\(.*?\n\}", text, re.DOTALL)
        self.assertIsNotNone(function)
        self.assertIn("sm64_saturn_scene_package_validate", function.group(0))
        self.assertIn("sm64_saturn_scene_package_validate_target", function.group(0))
        self.assertIn("runtime/saturn_scene_package.c", SOURCE_CART_MAKE.read_text(encoding="utf-8"))
        self.assertIn("runtime/saturn_scene_residency.c", SOURCE_CART_MAKE.read_text(encoding="utf-8"))
        self.assertIn("sm64_saturn_source_cart_boot_scene_package_validate", SOURCEBOOT_MAIN.read_text(encoding="utf-8"))

    def test_source_cart_exposes_only_aligned_post_source_capacity(self) -> None:
        text = SOURCE_CART_C.read_text(encoding="utf-8")
        function = re.search(r"bool sm64_saturn_source_cart_residency_span\(.*?\n\}", text, re.DOTALL)
        self.assertIsNotNone(function)
        self.assertIn("__sourceboot_cart_rodata_end", function.group(0))
        self.assertIn("source_prefix_bytes", function.group(0))
        self.assertIn("cart_bytes - high_water", function.group(0))

    def test_runtime_decodes_media_without_struct_casts(self) -> None:
        text = (RUNTIME / "saturn_scene_package.c").read_text(encoding="utf-8")
        self.assertIn("read_u16", text)
        self.assertIn("read_u32", text)
        self.assertNotRegex(text, r"\(\s*(?:const\s+)?sm64_saturn_scene_(?:package_header|section_descriptor|dependency_descriptor)_t\s*\*\s*\)")

    def test_cart_capacity_is_injected_not_assumed(self) -> None:
        text = (RUNTIME / "saturn_scene_residency.c").read_text(encoding="utf-8")
        self.assertNotIn("dram_cart_area_get", text)
        self.assertNotIn("4 * 1024", text)
        self.assertIn("state->capacity", text)


if __name__ == "__main__":
    unittest.main()
