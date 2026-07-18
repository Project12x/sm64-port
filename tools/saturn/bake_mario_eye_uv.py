#!/usr/bin/env python3
"""Bake SM64's source eye UV triangles into VDP1-safe direct-color tiles.

The N64 display list supplies per-vertex UVs; VDP1 distorted sprites do not.
This local-only stage samples each source triangle into a 16x16 texture with a
transparent exterior.  Runtime then maps one complete tile to the matching
degenerate VDP1 triangle.  ROM-derived output stays below build/.
"""
from __future__ import annotations

import argparse, json
from pathlib import Path

from extract_mario_textures import mio0_decode, rom_bytes, saturn_rgb1555

TILE = 16

def bilinear_weights(x: int, y: int) -> tuple[float, float, float]:
    b = (x + 0.5) / TILE
    c = (y + 0.5) / TILE
    return 1.0 - b - c, b, c

def pixel(texture: bytes, width: int, height: int, u: float, v: float) -> int:
    # Fast3D's source coordinates are s10.5-style values for this 32x32 asset.
    x = max(0, min(width - 1, int(u / 32.0)))
    y = max(0, min(height - 1, int(v / 32.0)))
    value = int.from_bytes(texture[(y * width + x) * 2:(y * width + x + 1) * 2], "big")
    return saturn_rgb1555(value)

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--intake", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    assets = json.loads(args.assets.read_text(encoding="utf-8"))
    intake = json.loads(args.intake.read_text(encoding="utf-8"))
    entry = assets["actors/mario/mario_eyes_center.rgba16.png"]
    width, height, size, regions = entry
    base, offset = regions["us"]
    source = mio0_decode(rom_bytes(args.rom), base)[offset:offset + size]
    if len(source) != size:
        raise ValueError("eye texture range outside decompressed MIO0 segment")
    triangles = intake["textured_eye_triangles"]
    tiles: list[list[int]] = []
    for triangle in triangles:
        uv = triangle["uv"]
        tile: list[int] = []
        for y in range(TILE):
            for x in range(TILE):
                a, b, c = bilinear_weights(x, y)
                if a < 0.0:
                    tile.append(0)
                    continue
                u = a * uv[0][0] + b * uv[1][0] + c * uv[2][0]
                v = a * uv[0][1] + b * uv[1][1] + c * uv[2][1]
                tile.append(pixel(source, width, height, u, v))
        tiles.append(tile)
    lines = ["/* Local ROM-derived output: do not commit. */", "#pragma once",
             f"#define SM64_MARIO_EYE_UV_TRIANGLE_COUNT {len(triangles)}U",
             f"#define SM64_MARIO_EYE_UV_TILE_WIDTH {TILE}U",
             "static const int16_t sm64_mario_eye_uv_positions[SM64_MARIO_EYE_UV_TRIANGLE_COUNT][3][3] = {"]
    for triangle in triangles:
        lines.append("    {" + ", ".join("{" + ", ".join(str(value) for value in vertex) + "}" for vertex in triangle["positions"]) + "},")
    lines += ["};", "static const uint16_t sm64_mario_eye_uv_tiles[SM64_MARIO_EYE_UV_TRIANGLE_COUNT][SM64_MARIO_EYE_UV_TILE_WIDTH * SM64_MARIO_EYE_UV_TILE_WIDTH] = {"]
    for tile in tiles:
        lines.append("    {")
        lines.extend("        " + ", ".join(f"0x{word:04X}" for word in tile[index:index + 8]) + "," for index in range(0, len(tile), 8))
        lines.append("    },")
    lines += ["};"]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps({"source": "mario_eyes_cap_on_dl", "triangle_count": len(triangles), "tile": [TILE, TILE], "uv_space": "Fast3D source UV / 32", "mapping": "per-source-triangle UV bake with transparent exterior"}, indent=2) + "\n", encoding="utf-8")

if __name__ == "__main__":
    main()
