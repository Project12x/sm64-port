import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from bake_bob_sky import bake, bake_clut16  # noqa: E402
from bake_castle_uv import quantize_clut16  # noqa: E402


def _rgba8_png(width: int, height: int, pixel_at) -> bytes:
    """Build a minimal noninterlaced RGBA8 PNG. pixel_at(x, y) -> (r, g, b, a)."""
    import zlib
    rows = []
    for y in range(height):
        row = bytearray()
        for x in range(width):
            row.extend(pixel_at(x, y))
        rows.append(bytes(row))
    raw = b"".join(b"\x00" + row for row in rows)
    compressed = zlib.compress(raw, 9)

    def chunk(kind: bytes, payload: bytes) -> bytes:
        data = kind + payload
        return struct.pack(">I", len(payload)) + data + struct.pack(">I", zlib.crc32(data))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", compressed) + chunk(b"IEND", b"")


def _solid_rgba8_png(width: int, height: int, rgba: tuple[int, int, int, int]) -> bytes:
    return _rgba8_png(width, height, lambda x, y: rgba)


def _rgb1555_of(rgb: tuple[int, int, int]) -> int:
    r, g, b = rgb
    return 0x8000 | ((r * 31 // 255) << 10) | ((g * 31 // 255) << 5) | (b * 31 // 255)


class TestBakeClut16(unittest.TestCase):
    def test_palette_has_sixteen_entries_index_zero_transparent(self):
        png = _solid_rgba8_png(4, 4, (255, 0, 0, 255))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "sky.png"
            path.write_bytes(png)
            packed, palette, manifest = bake_clut16(path, output_width=8, output_height=8)
            self.assertEqual(len(palette), 16)
            self.assertEqual(palette[0], 0x0000)
            self.assertEqual(manifest["format"], "CLUT16")
            self.assertEqual(manifest["palette_entries"], 16)

    def test_packed_indices_are_nibble_packed_two_per_byte(self):
        png = _solid_rgba8_png(4, 4, (0, 255, 0, 255))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "sky.png"
            path.write_bytes(png)
            packed, palette, manifest = bake_clut16(path, output_width=8, output_height=8)
            self.assertEqual(len(packed), (8 * 8) // 2)
            self.assertEqual(manifest["bytes"], (8 * 8) // 2)
            # A solid-color source produces exactly one CLUT index for every
            # pixel; quantize_clut16's tie-break (equal distance -> smallest
            # index) makes that index deterministically 1, pinning every
            # packed byte to 0x11. This doesn't distinguish a nibble-order
            # bug (both nibbles are equal here) -- see the multi-color test
            # below for that -- but it is a real, deterministic value worth
            # protecting against regressions in the single-color path.
            self.assertEqual(set(packed), {0x11})

    def test_rgb1555_path_unchanged_by_new_option(self):
        png = _solid_rgba8_png(4, 4, (10, 20, 30, 255))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "sky.png"
            path.write_bytes(png)
            pixels, manifest = bake(path, output_width=8, output_height=8)
            self.assertEqual(manifest["format"], "RGB1555")
            self.assertEqual(len(pixels), 8 * 8 * 2)

    def test_packed_bytes_and_palette_match_real_quantization_of_a_multicolor_image(self):
        """Exercises quantize_clut16's box-splitting loop beyond one
        iteration (four distinct colors, not one) and pins pack_clut16's
        nibble order against an independently hand-derived expectation --
        the exact mutation (flipping high/low nibble order in pack_clut16)
        that a solid-color fixture cannot detect, because a flip only
        changes the packed byte when the two indices in a pair differ.
        """
        stripe_colors = [
            (255, 0, 0, 255),  # red
            (0, 255, 0, 255),  # green
            (0, 0, 255, 255),  # blue
            (255, 255, 0, 255),  # yellow
        ]
        width = height = 4  # one solid-color column per stripe, no padding

        def pixel_at(x, y):
            return stripe_colors[x]

        png = _rgba8_png(width, height, pixel_at)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "sky.png"
            path.write_bytes(png)
            packed, palette, manifest = bake_clut16(path, output_width=width, output_height=height)

        # Independently derived ground truth: same conversion formula the
        # production code uses, fed through the real quantizer directly
        # (not through bake_clut16 or pack_clut16), so this does not just
        # retest the implementation against itself.
        raw_row = [_rgb1555_of(rgba[:3]) for rgba in stripe_colors]
        ground_truth_palette, mapping = quantize_clut16(raw_row * height)

        # Four distinct opaque colors must land on four distinct indices --
        # otherwise adjacent nibbles could tie and this test would no longer
        # be able to detect a nibble-order mutation.
        self.assertEqual(len({mapping[value] for value in raw_row}), 4)

        self.assertEqual(palette, ground_truth_palette)
        self.assertEqual(sorted(palette[1:5]), sorted(raw_row))

        expected_indices = [mapping[value] for value in raw_row] * height
        expected_packed = [
            (expected_indices[i] << 4) | expected_indices[i + 1]
            for i in range(0, len(expected_indices), 2)
        ]
        self.assertEqual(list(packed), expected_packed)
        self.assertEqual(manifest["bytes"], len(expected_packed))

    def test_odd_total_texel_count_raises_value_error(self):
        # Empirically confirmed case: a 3x3 source baked to a 9x9 (81,
        # odd) canvas trips pack_clut16's "4-bit VDP1 texture requires an
        # even texel count" guard. Dimensions stay within bounds (3 <= 9)
        # so this exercises the odd-count guard specifically, not the
        # source-exceeds-output guard below.
        png = _solid_rgba8_png(3, 3, (255, 0, 0, 255))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "sky.png"
            path.write_bytes(png)
            with self.assertRaises(ValueError):
                bake_clut16(path, output_width=9, output_height=9)

    def test_source_larger_than_output_raises_value_error(self):
        png = _solid_rgba8_png(4, 4, (255, 0, 0, 255))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "sky.png"
            path.write_bytes(png)
            with self.assertRaises(ValueError):
                bake_clut16(path, output_width=2, output_height=2)


if __name__ == "__main__":
    unittest.main()
