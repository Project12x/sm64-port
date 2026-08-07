import json
import unittest
from pathlib import Path

from extract_hud_glyphs import GLYPH_MANIFEST, build_header

FIXTURE_ASSETS = {
    "textures/segment2/segment2.00000.rgba16.png": [16, 16, 512, {"us": [1083968, 0]}],
    "textures/segment2/segment2.03200.rgba16.png": [16, 16, 512, {"us": [1083968, 12288]}],
    "actors/power_meter/power_meter_eight_segments.rgba16.png": [
        32, 32, 2048, {"us": [2102288, 0]},
    ],
}


class TestGlyphManifestCoverage(unittest.TestCase):
    def test_manifest_lists_exactly_the_needed_glyphs(self):
        names = {entry.asset_name for entry in GLYPH_MANIFEST}
        self.assertIn("digit_0", names)
        self.assertIn("digit_9", names)
        self.assertIn("glyph_multiply", names)
        self.assertIn("glyph_coin", names)
        self.assertIn("glyph_mario_head", names)
        self.assertIn("glyph_star", names)
        self.assertIn("glyph_apostrophe", names)
        self.assertIn("glyph_double_quote", names)
        self.assertIn("cam_camera", names)
        self.assertIn("cam_arrow_up", names)
        self.assertIn("power_meter_1", names)
        self.assertIn("power_meter_8", names)
        self.assertNotIn("glyph_beta_key", names)

    def test_build_header_rejects_missing_asset_entry(self):
        with self.assertRaises(KeyError):
            build_header({}, rom_sha256="0" * 64)


if __name__ == "__main__":
    unittest.main()
