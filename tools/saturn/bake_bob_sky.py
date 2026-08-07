#!/usr/bin/env python3
"""Bake BOB's source skybox PNG into a VDP2 RGB1555 bitmap.

The Saturn has no 256-pixel bitmap mode.  The source 248x248 sky image is
therefore placed in a 512x256 RGB1555 NBG1 surface with edge replication.
This is an offline asset step; camera scrolling and layer priority remain
runtime responsibilities.
"""

from __future__ import annotations

import argparse
import binascii
import json
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bake_castle_uv import pack_clut16, quantize_clut16  # noqa: E402


def _png_rows(path: Path) -> tuple[int, int, list[bytes]]:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("sky input is not a PNG")
    pos = 8
    width = height = depth = color = None
    compressed = bytearray()
    while pos < len(data):
        size = struct.unpack(">I", data[pos:pos + 4])[0]
        kind = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + size]
        pos += 12 + size
        if kind == b"IHDR":
            width, height, depth, color, compression, filt, interlace = struct.unpack(
                ">IIBBBBB", chunk)
            if (depth, color, compression, filt, interlace) != (8, 6, 0, 0, 0):
                raise ValueError("sky PNG must be noninterlaced RGBA8")
        elif kind == b"IDAT":
            compressed.extend(chunk)
        elif kind == b"IEND":
            break
    if width is None or height is None:
        raise ValueError("sky PNG lacks IHDR")
    raw = zlib.decompress(bytes(compressed))
    stride = width * 4
    if len(raw) != height * (stride + 1):
        raise ValueError("sky PNG scanline payload has unexpected size")
    rows: list[bytes] = []
    prior = bytearray(stride)
    offset = 0
    for _ in range(height):
        filter_type = raw[offset]
        encoded = raw[offset + 1:offset + 1 + stride]
        offset += stride + 1
        row = bytearray(stride)
        for i, value in enumerate(encoded):
            left = row[i - 4] if i >= 4 else 0
            up = prior[i]
            up_left = prior[i - 4] if i >= 4 else 0
            if filter_type == 0:
                prediction = 0
            elif filter_type == 1:
                prediction = left
            elif filter_type == 2:
                prediction = up
            elif filter_type == 3:
                prediction = (left + up) // 2
            elif filter_type == 4:
                p = left + up - up_left
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - up_left)
                prediction = left if pa <= pb and pa <= pc else up if pb <= pc else up_left
            else:
                raise ValueError(f"unsupported PNG filter {filter_type}")
            row[i] = (value + prediction) & 0xFF
        rows.append(bytes(row))
        prior = row
    return width, height, rows


def _replicate_canvas(source: Path, output_width: int, output_height: int) -> tuple[int, int, list[int]]:
    """Decode source and edge-replicate it into a row-major RGB1555 canvas.

    Shared by bake() and bake_clut16() so a geometry fix applied to one path
    is mechanically applied to both -- there is exactly one dimension check,
    one x/y offset computation, and one edge-clamp loop in this file. Alpha
    is discarded and every sample is unconditionally tagged opaque (bit 15
    set); this canvas can never contain a transparent sample.
    """
    width, height, rows = _png_rows(source)
    if width > output_width or height > output_height:
        raise ValueError("sky source exceeds VDP2 bitmap dimensions")
    x_offset = (output_width - width) // 2
    y_offset = (output_height - height) // 2
    raw_rgb1555: list[int] = []
    for y in range(output_height):
        source_y = min(max(y - y_offset, 0), height - 1)
        row = rows[source_y]
        for x in range(output_width):
            source_x = min(max(x - x_offset, 0), width - 1)
            r, g, b = row[source_x * 4:source_x * 4 + 3]
            value = 0x8000 | ((r * 31 // 255) << 10) | ((g * 31 // 255) << 5) | (b * 31 // 255)
            raw_rgb1555.append(value)
    return width, height, raw_rgb1555


def bake(source: Path, output_width: int = 512, output_height: int = 256) -> tuple[bytes, dict[str, object]]:
    width, height, raw_rgb1555 = _replicate_canvas(source, output_width, output_height)
    pixels = bytearray(output_width * output_height * 2)
    for offset, value in enumerate(raw_rgb1555):
        struct.pack_into(">H", pixels, offset * 2, value)
    manifest = {
        "schema": "sm64-saturn-vdp2-sky",
        "source": source.as_posix(),
        "source_dimensions": [width, height],
        "bitmap_dimensions": [output_width, output_height],
        "format": "RGB1555",
        "bytes": len(pixels),
        "sha256": __import__("hashlib").sha256(pixels).hexdigest(),
        "edge_replication": True,
    }
    return bytes(pixels), manifest


def bake_clut16(source: Path, output_width: int = 512, output_height: int = 256) -> tuple[list[int], list[int], dict[str, object]]:
    """Same edge-replicated canvas as bake(), quantized to a 16-color CLUT.

    Returns (packed_nibble_indices, palette_16_rgb1555_words, manifest).
    Calls the same _replicate_canvas() helper bake() uses, so the two paths
    only diverge at the final per-pixel quantization step.
    """
    width, height, raw_rgb1555 = _replicate_canvas(source, output_width, output_height)
    palette, mapping = quantize_clut16(raw_rgb1555)
    # _replicate_canvas() unconditionally tags every sample opaque (bit 15
    # set), so every value here is a key quantize_clut16() populated into
    # its histogram/mapping -- a strict subscript matches the pattern used
    # at every other quantize_clut16 call site in this codebase (see
    # bake_bob_tiles.py, bake_bob_bsp_fragments.py) and fails loudly instead
    # of silently mis-mapping if that invariant is ever broken.
    indices = [mapping[value] for value in raw_rgb1555]
    packed = pack_clut16(indices)
    manifest = {
        "schema": "sm64-saturn-vdp2-sky",
        "source": source.as_posix(),
        "source_dimensions": [width, height],
        "bitmap_dimensions": [output_width, output_height],
        "format": "CLUT16",
        "palette_entries": len(palette),
        "bytes": len(packed),
        "sha256": __import__("hashlib").sha256(bytes(packed)).hexdigest(),
        "edge_replication": True,
    }
    return packed, palette, manifest


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--texture-format", choices=["rgb1555", "clut16"], default="rgb1555")
    parser.add_argument("--palette-output", type=Path, help="required when --texture-format=clut16")
    args = parser.parse_args(argv)
    if args.texture_format == "clut16":
        if args.palette_output is None:
            parser.error("--palette-output is required with --texture-format clut16")
        packed, palette, manifest = bake_clut16(args.input)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(bytes(packed))
        args.palette_output.parent.mkdir(parents=True, exist_ok=True)
        args.palette_output.write_bytes(b"".join(struct.pack(">H", c) for c in palette))
    else:
        pixels, manifest = bake(args.input)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(pixels)
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
