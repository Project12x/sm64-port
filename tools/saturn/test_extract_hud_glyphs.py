import hashlib
import unittest

from extract_hud_glyphs import GLYPH_MANIFEST, build_header

# Minimal hand-built MIO0 blob (see extract_mario_textures.mio0_decode for the format):
# a 16-byte header, one all-literal flags byte, and 4 raw bytes. Decodes to exactly
# b"\xAB\xCD\x00\x00" -- pixel 0 = 0xABCD big-endian (alpha bit set -> opaque; red=21,
# green=15, blue=6 -> RGB1555 0x99F5), pixel 1 = 0x0000 (alpha bit clear -> VDP1's
# transparent code 0x0000). Reading the same two bytes little-endian instead (0xCDAB)
# decodes to a different RGB1555 word (0xD6D9), so a byte-order regression in the
# RGB1555 conversion changes pixel 0's value and is caught below.
_FIXTURE_MIO0_ROM = (
    b"MIO0"
    + (4).to_bytes(4, "big")  # decompressed size
    + (17).to_bytes(4, "big")  # LZ pair-stream offset (unused: every symbol is literal)
    + (17).to_bytes(4, "big")  # raw literal-byte stream offset
    + bytes([0xF0])  # flags byte: next 4 symbols are literal bytes
    + bytes([0xAB, 0xCD, 0x00, 0x00])
)

# Every real glyph key maps to the same tiny synthetic pixel pair above, so
# build_header can run its normal loop over the full GLYPH_MANIFEST without needing
# the real ROM. width != height so a swapped tuple-unpack regression
# (`height, width, size, regions = entry`) is caught by the width/height assertions
# below instead of passing silently.
FIXTURE_ASSETS = {
    glyph.manifest_key: [2, 1, 4, {"us": [0, 0]}] for glyph in GLYPH_MANIFEST
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

    def test_build_header_extracts_correct_pixels_and_metadata(self):
        header, manifest = build_header(FIXTURE_ASSETS, rom=_FIXTURE_MIO0_ROM)

        glyph_info = manifest["glyphs"]["digit_0"]
        self.assertEqual(glyph_info["width"], 2)
        self.assertEqual(glyph_info["height"], 1)
        self.assertEqual(glyph_info["offset"], 0)
        self.assertEqual(glyph_info["bytes"], 4)
        self.assertEqual(
            glyph_info["sha256"],
            hashlib.sha256(bytes([0xAB, 0xCD, 0x00, 0x00])).hexdigest(),
        )

        self.assertIn("#define SM64_SATURN_HUD_DIGIT_0_WIDTH 2U", header)
        self.assertIn("#define SM64_SATURN_HUD_DIGIT_0_HEIGHT 1U", header)
        self.assertIn(
            "static const uint16_t sm64_saturn_hud_digit_0[2] = {\n    0x99F5, 0x0000,",
            header,
        )


if __name__ == "__main__":
    unittest.main()
