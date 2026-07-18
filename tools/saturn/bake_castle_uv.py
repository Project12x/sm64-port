#!/usr/bin/env python3
"""Bake a bounded real Castle Area 1 texture slice into VDP1 UV tiles.

Only the two dominant opaque materials are selected for this first M3 camera:
the policy keeps command count below the default Yaul VDP1 command partition.
ROM-derived pixels are emitted beneath build/ and must never be committed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from extract_mario_textures import mio0_decode, rom_bytes, saturn_rgb1555

TILE = 8
SELECTED = ("inside_09000000", "inside_09005000")
ASSETS = {
    "inside_09000000": "textures/inside/inside_castle_textures.00000.rgba16.png",
    "inside_09005000": "textures/inside/inside_castle_textures.05000.rgba16.png",
}


def rgba16(rom: bytes, entry: list[object]) -> tuple[int, int, list[int], str]:
    width, height, size, regions = entry
    base, relative = regions["us"]
    image = mio0_decode(rom, base)
    data = image[relative:relative + size]
    if len(data) != size:
        raise ValueError("texture range outside decoded US ROM bank")
    return int(width), int(height), [saturn_rgb1555(int.from_bytes(data[i:i + 2], "big")) for i in range(0, len(data), 2)], hashlib.sha256(data).hexdigest()


def midpoint(a: tuple[int, ...], b: tuple[int, ...]) -> tuple[int, ...]:
    return tuple((left + right) // 2 for left, right in zip(a, b))


def barycentric(x: int, y: int) -> tuple[float, float, float]:
    # VDP1's measured repeated-vertex distorted-sprite basis is C/B/A.
    b = (x + 0.5) / TILE
    c = (y + 0.5) / TILE
    return c, b, 1.0 - b - c


def sample(texture: tuple[int, int, list[int], str], uv: tuple[tuple[int, int], tuple[int, int], tuple[int, int]], x: int, y: int) -> int:
    a, b, c = barycentric(x, y)
    if c < 0:
        return 0
    u = int(a * uv[0][0] + b * uv[1][0] + c * uv[2][0]) // 32
    v = int(a * uv[0][1] + b * uv[1][1] + c * uv[2][1]) // 32
    width, height, pixels, _digest = texture
    return pixels[(v % height) * width + (u % width)]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--intake", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    rom, asset_map = rom_bytes(args.rom), json.loads(args.assets.read_text(encoding="utf-8"))
    scene = json.loads(args.intake.read_text(encoding="utf-8"))
    texture_data = {name: rgba16(rom, asset_map[path]) for name, path in ASSETS.items()}
    selected_indices = {scene["textures"].index(name) for name in SELECTED}
    starts = [0xFFFF] * int(scene["triangle_count"])
    positions: list[tuple[tuple[int, int, int], tuple[int, int, int], tuple[int, int, int]]] = []
    words: list[int] = []
    for index, texture_index in enumerate(scene["texture_indices"]):
        if texture_index not in selected_indices:
            continue
        starts[index] = len(positions)
        tri = scene["triangles"][index]
        original = [tuple(scene["positions"][vertex]) for vertex in tri]
        original_uv = [tuple(pair) for pair in scene["uv"][index]]
        ab, bc, ca = midpoint(original[0], original[1]), midpoint(original[1], original[2]), midpoint(original[2], original[0])
        uab, ubc, uca = midpoint(original_uv[0], original_uv[1]), midpoint(original_uv[1], original_uv[2]), midpoint(original_uv[2], original_uv[0])
        for position_tri, uv_tri in (((original[0], ab, ca), (original_uv[0], uab, uca)), ((ab, original[1], bc), (uab, original_uv[1], ubc)), ((ca, bc, original[2]), (uca, ubc, original_uv[2])), ((ab, bc, ca), (uab, ubc, uca))):
            positions.append(position_tri)
            texture = texture_data[scene["textures"][texture_index]]
            words.extend(sample(texture, uv_tri, x, y) for y in range(TILE) for x in range(TILE))
    lines = ["/* Local ROM-derived output: do not commit. */", "#pragma once", "#include <stdint.h>", f"#define SM64_CASTLE_UV_TILE_WIDTH {TILE}U", "#define SM64_CASTLE_UV_TILE_NONE 0xFFFFU", f"#define SM64_CASTLE_UV_TILE_COUNT {len(positions)}U", "static const uint16_t sm64_castle_uv_tile_start[SM64_CASTLE_AREA1_OPAQUE_TRIANGLE_COUNT] = {"]
    lines.extend("    " + ", ".join(f"{value}U" for value in starts[offset:offset + 16]) + "," for offset in range(0, len(starts), 16))
    lines.append("};")
    lines.append("static const int16_t sm64_castle_uv_positions[SM64_CASTLE_UV_TILE_COUNT][3][3] = {")
    lines.extend(f"    {{{{{a}, {b}, {c}}}, {{{d}, {e}, {f}}}, {{{g}, {h}, {i}}}}}," for (a, b, c), (d, e, f), (g, h, i) in positions)
    lines.append("};")
    lines.append("static const uint16_t sm64_castle_uv_tiles[SM64_CASTLE_UV_TILE_COUNT][SM64_CASTLE_UV_TILE_WIDTH * SM64_CASTLE_UV_TILE_WIDTH] = {")
    for offset in range(0, len(words), TILE * TILE): lines.append("    {" + ", ".join(f"0x{word:04X}" for word in words[offset:offset + TILE * TILE]) + "},")
    lines.append("};")
    args.output.parent.mkdir(parents=True, exist_ok=True); args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    report = {"source": scene["source"], "selected_textures": list(SELECTED), "selected_triangles": sum(value != 0xFFFF for value in starts), "tile": [TILE, TILE], "tile_count": len(positions), "texture_bytes": len(words) * 2, "command_estimate": 2 + (len(positions)) + (int(scene["triangle_count"]) - sum(value != 0xFFFF for value in starts)) + 1, "rom_sha256": hashlib.sha256(rom).hexdigest(), "texture_sha256": {name: data[3] for name, data in texture_data.items()}}
    args.report.parent.mkdir(parents=True, exist_ok=True); args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
