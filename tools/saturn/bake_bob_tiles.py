#!/usr/bin/env python3
"""Bake BOB's Fast3D texture triangles into deterministic VDP1 CLUT16 tiles.

The source is the checked-in BOB intake stream, not a second geometry parser.
Each eligible source triangle gets one fixed-size tile; the manifest keeps the
source identity and tile state beside the packed bank so a stale bank cannot be
silently interpreted as textured geometry.
"""
from __future__ import annotations

import argparse
import binascii
import hashlib
import json
import struct
import zlib
from pathlib import Path

from bake_castle_uv import quantize_clut16, pack_clut16, sample_triangle
from vdp1_texture import downsample_rgb1555

MAX_TILE_BYTES = 333_696
MAX_VDP1_TEXTURE_BYTES = 446_432
BOB_TEXTURES = {
    **{f"generic_0900{offset:04X}": f"textures/generic/bob_textures.{offset:05X}.rgba16.png"
       for offset in (0x1800, 0x2000, 0x2800, 0x3000, 0x3800, 0x4000,
                      0x4800, 0x5000, 0x5800, 0x6000, 0x7000, 0x7800,
                      0x8000, 0x8800, 0x9000, 0x9800, 0xA000, 0xA800)},
    "generic_0900B000": "textures/generic/bob_textures.0B000.ia16.png",
    "bob_seg7_texture_07000000": "levels/bob/0.rgba16.png",
    "bob_seg7_texture_07000800": "levels/bob/1.rgba16.png",
    "bob_seg7_texture_07001000": "levels/bob/2.rgba16.png",
    "bob_seg7_texture_07001800": "levels/bob/3.rgba16.png",
    "bob_seg7_texture_07002000": "levels/bob/4.rgba16.png",
}


def _png_pixels(path: Path) -> tuple[int, int, list[int]]:
    """Read the small exporter PNGs without adding a runtime dependency."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    offset, idat, width = 8, bytearray(), 0
    height = color_type = bit_depth = 0
    while offset < len(data):
        size = struct.unpack(">I", data[offset:offset + 4])[0]
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + size]
        offset += 12 + size
        if kind == b"IHDR":
            width, height, bit_depth, color_type = struct.unpack(">IIBB", payload[:10])
        elif kind == b"IDAT":
            idat.extend(payload)
    if bit_depth != 8 or color_type not in (4, 6):
        raise ValueError(f"{path}: unsupported PNG format")
    channels = 2 if color_type == 4 else 4
    row_bytes = width * channels
    raw = zlib.decompress(bytes(idat))
    rows: list[bytes] = []
    cursor = 0
    previous = bytearray(row_bytes)
    for _ in range(height):
        filt = raw[cursor]
        cursor += 1
        row = bytearray(raw[cursor:cursor + row_bytes])
        cursor += row_bytes
        for i in range(row_bytes):
            left = row[i - channels] if i >= channels else 0
            up = previous[i]
            up_left = previous[i - channels] if i >= channels else 0
            if filt == 1:
                row[i] = (row[i] + left) & 255
            elif filt == 2:
                row[i] = (row[i] + up) & 255
            elif filt == 3:
                row[i] = (row[i] + ((left + up) // 2)) & 255
            elif filt == 4:
                p = left + up - up_left
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - up_left)
                row[i] = (row[i] + (left if pa <= pb and pa <= pc else up if pb <= pc else up_left)) & 255
            elif filt != 0:
                raise ValueError(f"{path}: unsupported PNG filter {filt}")
        rows.append(bytes(row))
        previous = row
    pixels: list[int] = []
    for row in rows:
        for x in range(width):
            if channels == 2:
                red = green = blue = row[x * 2]
                alpha = row[x * 2 + 1]
            else:
                red, green, blue, alpha = row[x * 4: x * 4 + 4]
            # exporter PNGs use 8-bit-expanded RGB1555 channels.
            value = ((red * 31 + 127) // 255) | (((green * 31 + 127) // 255) << 5) | (((blue * 31 + 127) // 255) << 10)
            pixels.append(value | (0x8000 if alpha >= 128 else 0))
    return width, height, pixels


def texture_coordinate_key(state: dict[str, object]) -> str:
    return json.dumps(state, sort_keys=True, separators=(",", ":"))


def triangle_k(triangle: dict[str, object]) -> float:
    state = triangle["tile"]
    uv = triangle["uv"]
    span_s = max(point[0] for point in uv) - min(point[0] for point in uv)
    span_t = max(point[1] for point in uv) - min(point[1] for point in uv)
    tile_s = int(state["lrs"]) - int(state["uls"]) + 4
    tile_t = int(state["lrt"]) - int(state["ult"]) + 4
    # Fast3D UVs are s10.5 values while the tile extents above are in
    # quarter-texel units.  Eight of these extent ratios is one source-tile
    # period (the same normalization used by the texture specification).
    return max(span_s / tile_s, span_t / tile_t) / 8.0


def bake_bob(intake: dict[str, object], asset_root: Path) -> tuple[bytes, bytes, dict[str, object]]:
    textures: dict[str, tuple[int, int, list[int], str, int]] = {}
    for name in sorted({str(t["texture"]) for t in intake["triangles"]}):
        relative = BOB_TEXTURES.get(name)
        if relative is None:
            raise ValueError(f"no exported BOB texture for {name}")
        width, height, pixels = _png_pixels(asset_root / relative)
        pixels = downsample_rgb1555(pixels, width, height, 1)[2]
        source = (asset_root / relative).read_bytes()
        textures[name] = (width, height, pixels, hashlib.sha256(source).hexdigest(), len(source))

    bank = bytearray()
    clut = bytearray()
    entries: list[dict[str, object]] = []
    class_counts = {"16x16": 0, "32x32": 0, "gouraud": 0}
    for source_index, triangle in enumerate(intake["triangles"]):
        k = triangle_k(triangle)
        if k > 16:
            class_counts["gouraud"] += 1
            continue
        tile_size = 16 if k <= 2 else 32
        class_name = f"{tile_size}x{tile_size}"
        class_counts[class_name] += 1
        state = dict(triangle["tile"])
        texture = textures[str(triangle["texture"])]
        uv = tuple(tuple(int(value) for value in point) for point in triangle["uv"])
        pixels = [sample_triangle(texture, uv, state, x, y, tile_size, 1)
                  for y in range(tile_size) for x in range(tile_size)]
        palette, mapping = quantize_clut16(pixels)
        packed = pack_clut16([mapping[value] for value in pixels])
        tile_offset = len(bank)
        clut_offset = len(clut)
        bank.extend(packed)
        clut.extend(struct.pack(">16H", *palette))
        entries.append({
            "source_triangle": source_index,
            "source_display_list": triangle["source_display_list"],
            "display_list_ordinal": source_index,
            "texture": triangle["texture"],
            "tile_state_sha256": hashlib.sha256(texture_coordinate_key(state).encode()).hexdigest(),
            "k": k,
            "tile_size": tile_size,
            "bank_offset": tile_offset,
            "bank_bytes": len(packed),
            "clut_offset": clut_offset,
            "clut_bytes": 32,
        })
    if class_counts != {"16x16": 567, "32x32": 510, "gouraud": 24}:
        raise ValueError(f"unexpected BOB tile classes: {class_counts}")
    if len(bank) > MAX_TILE_BYTES:
        raise ValueError(f"tile bank exceeds budget: {len(bank)} > {MAX_TILE_BYTES}")
    if len(bank) + len(clut) > MAX_VDP1_TEXTURE_BYTES:
        raise ValueError("tile plus CLUT bank exceeds free VDP1 texture VRAM")
    manifest = {
        "schema": "sm64-saturn-bob-texture-bank",
        "version": 1,
        "source_schema": intake["schema"],
        "source_version": intake["version"],
        "tile_format": "VDP1_CLUT16_4BPP",
        "coverage": class_counts,
        "source_triangles": len(intake["triangles"]),
        "texture_bytes": len(bank),
        "clut_bytes": len(clut),
        "resident_bytes": len(bank) + len(clut),
        "max_texture_bytes": MAX_TILE_BYTES,
        "max_resident_bytes": MAX_VDP1_TEXTURE_BYTES,
        "entries": entries,
        "source_texture_sha256": {name: value[3] for name, value in sorted(textures.items())},
    }
    return bytes(bank), bytes(clut), manifest


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--intake", type=Path, required=True)
    parser.add_argument("--asset-root", type=Path, default=Path("."))
    parser.add_argument("--bank", type=Path, required=True)
    parser.add_argument("--clut", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()
    intake = json.loads(args.intake.read_text(encoding="utf-8"))
    bank, clut, manifest = bake_bob(intake, args.asset_root)
    for path, payload in ((args.bank, bank), (args.clut, clut)):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)
    manifest["bank_sha256"] = hashlib.sha256(bank).hexdigest()
    manifest["clut_sha256"] = hashlib.sha256(clut).hexdigest()
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
