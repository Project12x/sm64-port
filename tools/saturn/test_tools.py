#!/usr/bin/env python3
"""Small standard-library regression tests for Saturn host-side tools."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))

from asset_classifier import classify_primitives, source_scan  # noqa: E402
from capture_hwtest import has_cd_block_copy_limitation, input_pulse_request  # noqa: E402
from telemetry_decode import decode  # noqa: E402


class AssetClassifierTests(unittest.TestCase):
    def test_direct_quad_requires_shared_uv_agreement(self) -> None:
        primitives = [
            {"indices": [0, 1, 2], "uvs": [[0, 0], [16, 0], [16, 16]], "material": 7},
            {"indices": [0, 2, 3], "uvs": [[0, 0], [16, 16], [0, 16]], "material": 7},
        ]
        report = classify_primitives(primitives)
        self.assertEqual(report["direct_textured_quad_candidates"], 1)

        primitives[1]["uvs"][1] = [8, 8]
        report = classify_primitives(primitives)
        self.assertEqual(report["direct_textured_quad_candidates"], 0)
        self.assertEqual(report["rejection_reasons"]["shared_uv_mismatch"], 1)

    def test_source_scan_counts_quadrangle_macro(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "display.c").write_text(
                "gsSP1Triangle(0, 1, 2, 0); gsSP2Triangles(0,1,2,0, 0,2,3,0); "
                "gsSP1Quadrangle(0,1,2,3,0); gsSPVertex(v, 4, 0); "
                "gsDPSetTextureImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, tex);\n",
                encoding="utf-8",
            )
            report = source_scan(root)
        self.assertEqual(report["gsSP1Triangle"], 1)
        self.assertEqual(report["gsSP2Triangles"], 1)
        self.assertEqual(report["gsSP1Quadrangle"], 1)
        self.assertEqual(report["static_triangles"], 3)
        self.assertEqual(report["geometry_macro_counts"]["gsSPVertex"], 1)
        self.assertEqual(report["geometry_macro_counts"]["gsDPSetTextureImage"], 1)

    def test_six_way_representation_counts_are_conservative(self) -> None:
        primitives = [
            {"indices": [0, 1, 2], "material": 1},
            {"indices": [3, 4, 5], "uvs": [[0, 0], [8, 0], [0, 8]], "material": 2},
            {"indices": [6, 7, 8], "uvs": [[0, 0], [8, 0], [0, 8]], "split": True},
            {"indices": [9, 10, 11], "uvs": [[0, 0], [8, 0], [0, 8]], "bake": True},
            {"indices": [12, 13, 14], "uvs": [[0, 0], [8, 0], [0, 8]], "effect": "shadow"},
        ]
        counts = classify_primitives(primitives)["representation_counts"]
        self.assertEqual(counts["direct_untextured_triangle"], 1)
        self.assertEqual(counts["textured_degenerate_triangle"], 1)
        self.assertEqual(counts["split_cropped_surface"], 1)
        self.assertEqual(counts["baked_surface"], 1)
        self.assertEqual(counts["effect_fallback"], 1)

    def test_malformed_uvs_are_rejected_without_raising(self) -> None:
        primitives = [
            {"indices": [0, 1, 2], "uvs": [[0, 0], [8, 0]], "material": 1},
            {"indices": [0, 2, 3], "uvs": [[0, 0], [8, 8], [0, 8]], "material": 1},
        ]
        report = classify_primitives(primitives)
        self.assertEqual(report["direct_textured_quad_candidates"], 0)
        self.assertEqual(report["rejection_reasons"]["uvs_length_mismatch"], 1)


class YmirInputTests(unittest.TestCase):
    def test_pressed_mask_is_converted_to_active_low_pad_report(self) -> None:
        message = input_pulse_request(7, 0x0400)
        self.assertEqual(message["id"], 7)
        self.assertEqual(message["params"]["buttons"], 0xFBF8)
        self.assertEqual(message["params"]["frames"], 1)

    def test_hold_duration_is_forwarded(self) -> None:
        message = input_pulse_request(8, 0x8000, 12)
        self.assertEqual(message["params"], {"buttons": 0x7FF8, "frames": 12})


class TelemetryTests(unittest.TestCase):
    def test_complete_pass_status_decodes(self) -> None:
        words = [
            0x53415430, 1, 1, 0x8000001F, 0x5C, 0x400000, 0x400000,
            0xFFFFFFFF, 0, 0, 10, 20, 30, 40, 4, 15360,
        ]
        data = []
        for word in words:
            data.extend(word.to_bytes(4, "big"))
        data.extend((0x53415458).to_bytes(4, "big"))
        data.extend((1).to_bytes(4, "big"))
        data.extend([0] * 48)
        decoded = decode(data, require_complete=True)
        self.assertTrue(decoded["ok"])
        self.assertTrue(decoded["status_flags"]["complete"])
        self.assertEqual(decoded["cart_id"], 0x5C)
        self.assertEqual(decoded["extended"]["cpu_dmac_pass"], 0)
        self.assertEqual(decoded["extended"]["vdp1_textured_triangle_ticks"], 0)

    def test_wrong_cart_identity_is_not_ok(self) -> None:
        words = [
            0x53415430, 1, 1, 0x8000001F, 0x00, 0x400000, 0x400000,
            0xFFFFFFFF, 0, 0, 10, 20, 30, 40, 4, 15360,
        ]
        data = b"".join(word.to_bytes(4, "big") for word in words)
        decoded = decode(list(data), require_complete=True)
        self.assertFalse(decoded["ok"])

    def test_ymir_cd_block_diagnostic_is_recognized(self) -> None:
        self.assertTrue(has_cd_block_copy_limitation("info | CDBlock | Get copy error command is unimplemented"))
        self.assertTrue(has_cd_block_copy_limitation("CD-block copy operation unavailable"))
        self.assertFalse(has_cd_block_copy_limitation("Filesystem built successfully"))

    def test_bad_extended_magic_is_rejected(self) -> None:
        words = [
            0x53415430, 1, 1, 0x8000001F, 0x5C, 0x400000, 0x400000,
            0xFFFFFFFF, 0, 0, 10, 20, 30, 40, 4, 15360,
        ]
        data = bytearray(b"".join(word.to_bytes(4, "big") for word in words))
        data.extend((0).to_bytes(4, "big"))
        data.extend((1).to_bytes(4, "big"))
        data.extend(b"\0" * 48)
        with self.assertRaisesRegex(ValueError, "extended telemetry magic"):
            decode(list(data), require_complete=True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
