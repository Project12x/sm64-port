#!/usr/bin/env python3
"""Host coverage for identity-aware route-view capture artifacts."""
from __future__ import annotations

import unittest
from pathlib import Path
from unittest import mock

import capture_route_views as capture


class CaptureRouteViewsTest(unittest.TestCase):
    def test_role3_pipeline2_identity_uses_elf_markers_and_truthful_filename(self) -> None:
        elf = Path("role3.elf")
        markers = {
            "sourceboot_camera_idle_capture": 0x00210000,
            "sourceboot_route_checkpoint": 0x06010200,
            "g_sm64_saturn_source_cart_probe": 0x06010400,
            "sm64_saturn_camera_variant_marker": 3,
            "sm64_saturn_camera_route_marker": 1,
        }
        with mock.patch("capture_route_views.newest_sibling_elf", return_value=elf), \
                mock.patch("capture_camera_idle.resolve_symbols", return_value=markers):
            identity = capture.resolve_capture_identity(
                Path("role3.cue"), renderer_pipeline=2,
                camera_role="camera-fixed-candidate",
            )

        self.assertEqual(
            identity,
            {
                "renderer_pipeline": 2,
                "camera_role": "camera-fixed-candidate",
                "camera_variant": 3,
                "camera_route": 1,
                "elf": elf,
            },
        )
        self.assertEqual(
            capture.screenshot_filename("overview", 2000, "role3", identity),
            "ymir-bob-pipe2-overview-tick2000-role3.png",
        )
        mismatched_markers = {**markers, "sm64_saturn_camera_variant_marker": 1}
        with mock.patch("capture_route_views.newest_sibling_elf", return_value=elf), \
                mock.patch("capture_camera_idle.resolve_symbols", return_value=mismatched_markers):
            with self.assertRaisesRegex(
                ValueError, "ELF camera variant marker disagrees with capture role"
            ):
                capture.resolve_capture_identity(
                    Path("role3.cue"), renderer_pipeline=2,
                    camera_role="camera-fixed-candidate",
                )


if __name__ == "__main__":
    unittest.main()
