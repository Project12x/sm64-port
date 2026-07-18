#!/usr/bin/env python3
"""Export user-ROM RGBA16 assets as local PNGs for renderer diagnostics."""
from __future__ import annotations

import argparse
import binascii
import json
import struct
import zlib
from pathlib import Path

from extract_mario_textures import mio0_decode, rom_bytes


def png_chunk(kind: bytes, data: bytes) -> bytes:
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", binascii.crc32(body) & 0xFFFFFFFF)


def write_rgba16_png(path: Path, source: bytes, width: int, height: int) -> None:
    rows = bytearray()
    for y in range(height):
        rows.append(0)  # PNG filter: none
        for x in range(width):
            value = int.from_bytes(source[(y * width + x) * 2:(y * width + x + 1) * 2], "big")
            rows.extend((((value >> 11) & 31) * 255 // 31,
                         ((value >> 6) & 31) * 255 // 31,
                         ((value >> 1) & 31) * 255 // 31))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" +
                     png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
                     png_chunk(b"IDAT", zlib.compress(bytes(rows), 9)) +
                     png_chunk(b"IEND", b""))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--prefix", required=True, help="asset path prefix to export")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    rom = rom_bytes(args.rom)
    assets = json.loads(args.assets.read_text(encoding="utf-8"))
    decoded: dict[int, bytes] = {}
    for name, entry in sorted(assets.items()):
        if not name.startswith(args.prefix):
            continue
        width, height, size, regions = entry
        base, offset = regions["us"]
        image = decoded.setdefault(base, mio0_decode(rom, base))
        source = image[offset:offset + size]
        if len(source) != size:
            raise ValueError(f"{name}: range outside decoded US ROM bank")
        write_rgba16_png(args.output / (Path(name).stem + ".png"), source, width, height)


if __name__ == "__main__":
    main()
