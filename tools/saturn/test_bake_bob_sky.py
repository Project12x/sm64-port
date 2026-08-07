import struct
import unittest

from bake_bob_sky import bake, bake_clut16


def _solid_rgba8_png(width: int, height: int, rgba: tuple[int, int, int, int]) -> bytes:
    import zlib
    row = bytes(rgba) * width
    raw = b"".join(b"\x00" + row for _ in range(height))
    compressed = zlib.compress(raw, 9)

    def chunk(kind: bytes, payload: bytes) -> bytes:
        data = kind + payload
        return struct.pack(">I", len(payload)) + data + struct.pack(">I", zlib.crc32(data))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", compressed) + chunk(b"IEND", b"")


class TestBakeClut16(unittest.TestCase):
    def test_palette_has_sixteen_entries_index_zero_transparent(self):
        from pathlib import Path
        import tempfile
        png = _solid_rgba8_png(4, 4, (255, 0, 0, 255))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "sky.png"
            path.write_bytes(png)
            indices, palette, manifest = bake_clut16(path, output_width=8, output_height=8)
            self.assertEqual(len(palette), 16)
            self.assertEqual(palette[0], 0x0000)
            self.assertEqual(manifest["format"], "CLUT16")
            self.assertEqual(manifest["palette_entries"], 16)

    def test_packed_indices_are_nibble_packed_two_per_byte(self):
        from pathlib import Path
        import tempfile
        png = _solid_rgba8_png(4, 4, (0, 255, 0, 255))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "sky.png"
            path.write_bytes(png)
            indices, palette, manifest = bake_clut16(path, output_width=8, output_height=8)
            self.assertEqual(len(indices), (8 * 8) // 2)
            self.assertEqual(manifest["bytes"], (8 * 8) // 2)

    def test_rgb1555_path_unchanged_by_new_option(self):
        from pathlib import Path
        import tempfile
        png = _solid_rgba8_png(4, 4, (10, 20, 30, 255))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "sky.png"
            path.write_bytes(png)
            pixels, manifest = bake(path, output_width=8, output_height=8)
            self.assertEqual(manifest["format"], "RGB1555")
            self.assertEqual(len(pixels), 8 * 8 * 2)


if __name__ == "__main__":
    unittest.main()
