#!/usr/bin/env python3
"""Bake a bounded real Castle Area 1 texture slice into VDP1 UV tiles.

The caller selects only the materials visible to its M3 camera.  Tile size and
source scale are explicit residency/fidelity controls; the policy keeps command
count below the default Yaul VDP1 command partition.
ROM-derived pixels are emitted beneath build/ and must never be committed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from extract_mario_textures import mio0_decode, rom_bytes, saturn_rgb1555
from vdp1_texture import repeated_vertex_weights

DEFAULT_TILE = 16
DEFAULT_SELECTED = (
    "inside_09000000", "inside_09001000", "inside_09003800",
    "inside_09004000", "inside_09005000", "inside_09008000",
)
ASSETS = {
    "inside_09000000": "textures/inside/inside_castle_textures.00000.rgba16.png",
    "inside_09001000": "textures/inside/inside_castle_textures.01000.rgba16.png",
    "inside_09003800": "textures/inside/inside_castle_textures.03800.rgba16.png",
    "inside_09004000": "textures/inside/inside_castle_textures.04000.rgba16.png",
    "inside_09005000": "textures/inside/inside_castle_textures.05000.rgba16.png",
    "inside_09008000": "textures/inside/inside_castle_textures.08000.rgba16.png",
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


def sample(texture: tuple[int, int, list[int], str], uv: tuple[tuple[int, int], tuple[int, int], tuple[int, int]], tile_state: dict[str, object], x: int, y: int, tile: int, source_scale: int) -> int:
    a, b, c = repeated_vertex_weights(x, y, tile, tile)
    # Match Mario's proven s10.5 truncation, then apply the source display
    # list's render-tile extent/mode instead of blindly wrapping image bounds.
    u = int((a * uv[0][0] + b * uv[1][0] + c * uv[2][0]) / (32.0 * source_scale))
    v = int((a * uv[0][1] + b * uv[1][1] + c * uv[2][1]) / (32.0 * source_scale))
    width, height, pixels, _digest = texture
    tile_width, tile_height = int(tile_state["width"]) // source_scale, int(tile_state["height"]) // source_scale
    u = max(0, min(tile_width - 1, u)) if tile_state["clamp_s"] else u % tile_width
    v = max(0, min(tile_height - 1, v)) if tile_state["clamp_t"] else v % tile_height
    width, height = width // source_scale, height // source_scale
    if tile_width > width or tile_height > height:
        raise ValueError("render tile extent exceeds ROM texture dimensions")
    return pixels[(v * source_scale) * (width * source_scale) + (u * source_scale)]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--intake", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--texture", action="append", dest="textures")
    parser.add_argument("--tile", type=int, default=DEFAULT_TILE)
    parser.add_argument("--source-scale", type=int, default=1)
    parser.add_argument("--subdivision", type=int, choices=(1, 4), default=1)
    args = parser.parse_args()
    rom, asset_map = rom_bytes(args.rom), json.loads(args.assets.read_text(encoding="utf-8"))
    scene = json.loads(args.intake.read_text(encoding="utf-8"))
    selected = tuple(args.textures or DEFAULT_SELECTED)
    if args.tile < 8 or args.tile % 8 != 0:
        raise ValueError("--tile must be an eight-pixel multiple")
    if args.source_scale < 1 or args.source_scale not in (1, 2, 4):
        raise ValueError("--source-scale must be 1, 2, or 4")
    unknown = set(selected) - set(ASSETS)
    if unknown:
        raise ValueError(f"unsupported Castle texture(s): {sorted(unknown)}")
    texture_data = {name: rgba16(rom, asset_map[ASSETS[name]]) for name in selected}
    selected_indices = {scene["textures"].index(name) for name in selected}
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
        tile_state = scene["tile_state"][index]
        if tile_state is None or "width" not in tile_state or "height" not in tile_state:
            raise ValueError(f"triangle {index} has no complete Fast3D render-tile state")
        if args.subdivision == 4:
            ab, bc, ca = midpoint(original[0], original[1]), midpoint(original[1], original[2]), midpoint(original[2], original[0])
            uab, ubc, uca = midpoint(original_uv[0], original_uv[1]), midpoint(original_uv[1], original_uv[2]), midpoint(original_uv[2], original_uv[0])
            parts = (((original[0], ab, ca), (original_uv[0], uab, uca)), ((ab, original[1], bc), (uab, original_uv[1], ubc)), ((ca, bc, original[2]), (uca, ubc, original_uv[2])), ((ab, bc, ca), (uab, ubc, uca)))
        else:
            parts = ((tuple(original), tuple(original_uv)),)
        for position_tri, uv_tri in parts:
            positions.append(position_tri)
            texture = texture_data[scene["textures"][texture_index]]
            words.extend(sample(texture, uv_tri, tile_state, x, y, args.tile, args.source_scale) for y in range(args.tile) for x in range(args.tile))
    lines = ["/* Local ROM-derived output: do not commit. */", "#pragma once", "#include <stdint.h>", f"#define SM64_CASTLE_UV_TILE_WIDTH {args.tile}U", f"#define SM64_CASTLE_UV_TILES_PER_TRIANGLE {args.subdivision}U", "#define SM64_CASTLE_UV_TILE_NONE 0xFFFFU", f"#define SM64_CASTLE_UV_TILE_COUNT {len(positions)}U", "static const uint16_t sm64_castle_uv_tile_start[SM64_CASTLE_AREA1_OPAQUE_TRIANGLE_COUNT] = {"]
    lines.extend("    " + ", ".join(f"{value}U" for value in starts[offset:offset + 16]) + "," for offset in range(0, len(starts), 16))
    lines.append("};")
    lines.append("static const int16_t sm64_castle_uv_positions[SM64_CASTLE_UV_TILE_COUNT][3][3] = {")
    lines.extend(f"    {{{{{a}, {b}, {c}}}, {{{d}, {e}, {f}}}, {{{g}, {h}, {i}}}}}," for (a, b, c), (d, e, f), (g, h, i) in positions)
    lines.append("};")
    lines.append("static const uint16_t sm64_castle_uv_tiles[SM64_CASTLE_UV_TILE_COUNT][SM64_CASTLE_UV_TILE_WIDTH * SM64_CASTLE_UV_TILE_WIDTH] = {")
    for offset in range(0, len(words), args.tile * args.tile): lines.append("    {" + ", ".join(f"0x{word:04X}" for word in words[offset:offset + args.tile * args.tile]) + "},")
    lines.append("};")
    args.output.parent.mkdir(parents=True, exist_ok=True); args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    report = {"source": scene["source"], "selected_textures": list(selected), "selected_triangles": sum(value != 0xFFFF for value in starts), "tile": [args.tile, args.tile], "source_scale": args.source_scale, "subdivision": args.subdivision, "tile_count": len(positions), "texture_bytes": len(words) * 2, "command_estimate": 2 + (len(positions)) + (int(scene["triangle_count"]) - sum(value != 0xFFFF for value in starts)) + 1, "uv_sampling": "Fast3D s10.5 truncation plus extracted render-tile size and clamp/wrap mode; complete VDP1 C/B/A/C repeated-vertex tile", "rom_sha256": hashlib.sha256(rom).hexdigest(), "texture_sha256": {name: data[3] for name, data in texture_data.items()}}
    args.report.parent.mkdir(parents=True, exist_ok=True); args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
