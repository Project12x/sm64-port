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
import math
from pathlib import Path

from extract_mario_textures import mio0_decode, rom_bytes, saturn_rgb1555
from vdp1_texture import (
    distorted_sprite_weights,
    downsample_rgb1555,
    repeated_vertex_weights,
)

DEFAULT_TILE = 16
DEFAULT_SELECTED = (
    "inside_09000000", "inside_09001000", "inside_09003800",
    "inside_09004000", "inside_09005000", "inside_09008000",
    "inside_09008800", "inside_castle_seg7_texture_07000800",
    "inside_castle_seg7_texture_07002000",
)
ASSETS = {
    "inside_09000000": "textures/inside/inside_castle_textures.00000.rgba16.png",
    "inside_09001000": "textures/inside/inside_castle_textures.01000.rgba16.png",
    "inside_09003800": "textures/inside/inside_castle_textures.03800.rgba16.png",
    "inside_09004000": "textures/inside/inside_castle_textures.04000.rgba16.png",
    "inside_09005000": "textures/inside/inside_castle_textures.05000.rgba16.png",
    "inside_09008000": "textures/inside/inside_castle_textures.08000.rgba16.png",
    "inside_09008800": "textures/inside/inside_castle_textures.08800.rgba16.png",
    "inside_castle_seg7_texture_07000800": "levels/castle_inside/1.rgba16.png",
    "inside_castle_seg7_texture_07002000": "levels/castle_inside/3.rgba16.png",
}


def rgba16(rom: bytes, entry: list[object], source_scale: int) -> tuple[int, int, list[int], str, int]:
    width, height, size, regions = entry
    base, relative = regions["us"]
    image = mio0_decode(rom, base)
    data = image[relative:relative + size]
    if len(data) != size:
        raise ValueError("texture range outside decoded US ROM bank")
    pixels = [saturn_rgb1555(int.from_bytes(data[i:i + 2], "big")) for i in range(0, len(data), 2)]
    scaled_width, scaled_height, scaled = downsample_rgb1555(pixels, int(width), int(height), source_scale)
    return scaled_width, scaled_height, scaled, hashlib.sha256(data).hexdigest(), len(data)


def midpoint(a: tuple[int, ...], b: tuple[int, ...]) -> tuple[int, ...]:
    return tuple((left + right) // 2 for left, right in zip(a, b))


def should_subdivide(
        positions: list[tuple[int, int, int]], subdivision: int,
        threshold: int) -> bool:
    """Select the PS1-style large-polygon split without camera-specific data."""
    if subdivision == 4:
        return True
    if threshold <= 0:
        return False
    diagonal_squared = sum(
        (max(point[axis] for point in positions) -
         min(point[axis] for point in positions)) ** 2
        for axis in range(3)
    )
    return diagonal_squared > threshold * threshold


def texture_coordinate(
        raw: float, scale: int, lower: int, extent: int, mask: int,
        shift: int, clamp: bool, mirror: bool) -> int:
    """Apply SM64's Fast3D tile-coordinate state in source-texel space."""
    coordinate = math.floor(raw * scale / (32 * 65536)) - lower // 4
    if 0 < shift <= 10:
        coordinate >>= shift
    elif shift > 10:
        coordinate <<= 16 - shift
    if clamp:
        return max(0, min(extent - 1, coordinate))
    period = 1 << mask if mask else extent
    if period <= 0:
        raise ValueError("texture wrap period must be positive")
    if mirror:
        wrapped = coordinate % (period * 2)
        return period * 2 - 1 - wrapped if wrapped >= period else wrapped
    return coordinate % period


def sample_raw(
        texture: tuple[int, int, list[int], str, int], raw_u: float,
        raw_v: float, tile_state: dict[str, object], source_scale: int) -> int:
    # Resolve the complete render tile before downscaling.  The previous
    # prototype divided first and inferred both axes' clamp state from the
    # whole gsDPSetTile macro; that turned Castle's T-clamp/S-wrap walls into
    # horizontally clamped streaks.
    width, height, pixels, _digest, _source_bytes = texture
    tile_width, tile_height = int(tile_state["width"]), int(tile_state["height"])
    source_u = texture_coordinate(
        raw_u, int(tile_state["sp_scale_s"]), int(tile_state["uls"]), tile_width,
        int(tile_state["mask_s"]), int(tile_state["shift_s"]),
        bool(tile_state["clamp_s"]), bool(tile_state["mirror_s"]),
    )
    source_v = texture_coordinate(
        raw_v, int(tile_state["sp_scale_t"]), int(tile_state["ult"]), tile_height,
        int(tile_state["mask_t"]), int(tile_state["shift_t"]),
        bool(tile_state["clamp_t"]), bool(tile_state["mirror_t"]),
    )
    u, v = source_u // source_scale, source_v // source_scale
    if tile_width // source_scale > width or tile_height // source_scale > height:
        raise ValueError("render tile extent exceeds ROM texture dimensions")
    return pixels[v * width + u]


def sample_triangle(
        texture: tuple[int, int, list[int], str, int],
        uv: tuple[tuple[int, int], tuple[int, int], tuple[int, int]],
        tile_state: dict[str, object], x: int, y: int, tile: int,
        source_scale: int) -> int:
    a, b, c = repeated_vertex_weights(x, y, tile, tile)
    return sample_raw(
        texture,
        a * uv[0][0] + b * uv[1][0] + c * uv[2][0],
        a * uv[0][1] + b * uv[1][1] + c * uv[2][1],
        tile_state, source_scale,
    )


def sample_quad(
        texture: tuple[int, int, list[int], str, int],
        uv: tuple[tuple[int, int], tuple[int, int], tuple[int, int], tuple[int, int]],
        tile_state: dict[str, object], x: int, y: int, tile: int,
        source_scale: int) -> int:
    a, b, c, d = distorted_sprite_weights(x, y, tile, tile)
    return sample_raw(
        texture,
        a * uv[0][0] + b * uv[1][0] + c * uv[2][0] + d * uv[3][0],
        a * uv[0][1] + b * uv[1][1] + c * uv[2][1] + d * uv[3][1],
        tile_state, source_scale,
    )


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
    parser.add_argument("--subdivision-threshold", type=int, default=0,
                        help="split triangles whose source-space diagonal exceeds this value")
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
    texture_data = {name: rgba16(rom, asset_map[ASSETS[name]], args.source_scale) for name in selected}
    selected_indices = {scene["textures"].index(name) for name in selected}
    starts = [0xFFFF] * int(scene["primitive_count"])
    counts = [0] * int(scene["primitive_count"])
    positions: list[tuple[tuple[int, int, int], ...]] = []
    tile_primitives: list[int] = []
    words: list[int] = []
    paired_quads = 0
    for index, primitive in enumerate(scene["primitives"]):
        texture_index = int(primitive["texture_index"])
        if texture_index not in selected_indices:
            continue
        starts[index] = len(positions)
        source_index = int(primitive["first_triangle"])
        tri = scene["triangles"][source_index]
        original = [tuple(scene["positions"][vertex]) for vertex in tri]
        original_uv = [tuple(pair) for pair in scene["uv"][source_index]]
        tile_state = scene["tile_state"][source_index]
        if tile_state is None or "width" not in tile_state or "height" not in tile_state:
            raise ValueError(f"primitive {index} has no complete Fast3D render-tile state")
        texture = texture_data[scene["textures"][texture_index]]
        second_index = primitive["second_triangle"]
        if second_index is not None:
            vertex_uv: dict[int, tuple[int, int]] = {}
            for triangle_index in (source_index, int(second_index)):
                for vertex, pair in zip(scene["triangles"][triangle_index], scene["uv"][triangle_index]):
                    value = tuple(pair)
                    if vertex in vertex_uv and vertex_uv[vertex] != value:
                        raise ValueError(f"primitive {index} crossed a source UV seam")
                    vertex_uv[vertex] = value
            quad_vertices = tuple(int(value) for value in primitive["vertices"])
            position_quad = tuple(tuple(scene["positions"][vertex]) for vertex in quad_vertices)
            uv_quad = tuple(vertex_uv[vertex] for vertex in quad_vertices)
            positions.append(position_quad)
            tile_primitives.append(index)
            words.extend(sample_quad(texture, uv_quad, tile_state, x, y, args.tile,
                                     args.source_scale)
                         for y in range(args.tile) for x in range(args.tile))
            counts[index] = 1
            paired_quads += 1
            continue
        if should_subdivide(original, args.subdivision, args.subdivision_threshold):
            ab, bc, ca = midpoint(original[0], original[1]), midpoint(original[1], original[2]), midpoint(original[2], original[0])
            uab, ubc, uca = midpoint(original_uv[0], original_uv[1]), midpoint(original_uv[1], original_uv[2]), midpoint(original_uv[2], original_uv[0])
            parts = (((original[0], ab, ca), (original_uv[0], uab, uca)), ((ab, original[1], bc), (uab, original_uv[1], ubc)), ((ca, bc, original[2]), (uca, ubc, original_uv[2])), ((ab, bc, ca), (uab, ubc, uca)))
        else:
            parts = ((tuple(original), tuple(original_uv)),)
        counts[index] = len(parts)
        for position_tri, uv_tri in parts:
            positions.append((*position_tri, position_tri[2]))
            tile_primitives.append(index)
            words.extend(sample_triangle(texture, uv_tri, tile_state, x, y,
                                         args.tile, args.source_scale)
                         for y in range(args.tile) for x in range(args.tile))
    textured_count = sum(value != 0xFFFF for value in starts)
    lines = ["/* Local ROM-derived output: do not commit. */", "#pragma once", "#include <stdint.h>", f"#define SM64_CASTLE_UV_TILE_WIDTH {args.tile}U", "#define SM64_CASTLE_UV_TILE_NONE 0xFFFFU", f"#define SM64_CASTLE_UV_TEXTURED_PRIMITIVE_COUNT {textured_count}U", f"#define SM64_CASTLE_UV_PAIRED_QUAD_COUNT {paired_quads}U", f"#define SM64_CASTLE_UV_TILE_COUNT {len(positions)}U", "static const uint16_t sm64_castle_uv_tile_start[SM64_CASTLE_AREA1_PRIMITIVE_COUNT] = {"]
    lines.extend("    " + ", ".join(f"{value}U" for value in starts[offset:offset + 16]) + "," for offset in range(0, len(starts), 16))
    lines.append("};")
    lines.append("static const uint16_t sm64_castle_uv_tile_primitive[SM64_CASTLE_UV_TILE_COUNT] = {")
    lines.extend("    " + ", ".join(f"{value}U" for value in tile_primitives[offset:offset + 16]) + "," for offset in range(0, len(tile_primitives), 16))
    lines.append("};")
    lines.append("static const uint8_t sm64_castle_uv_tile_count[SM64_CASTLE_AREA1_PRIMITIVE_COUNT] = {")
    lines.extend("    " + ", ".join(f"{value}U" for value in counts[offset:offset + 16]) + "," for offset in range(0, len(counts), 16))
    lines.append("};")
    lines.append("static const int16_t sm64_castle_uv_positions[SM64_CASTLE_UV_TILE_COUNT][4][3] = {")
    lines.extend("    {" + ", ".join(f"{{{x}, {y}, {z}}}" for x, y, z in quad) + "}," for quad in positions)
    lines.append("};")
    lines.append("static const uint16_t sm64_castle_uv_tiles[SM64_CASTLE_UV_TILE_COUNT][SM64_CASTLE_UV_TILE_WIDTH * SM64_CASTLE_UV_TILE_WIDTH] = {")
    for offset in range(0, len(words), args.tile * args.tile): lines.append("    {" + ", ".join(f"0x{word:04X}" for word in words[offset:offset + args.tile * args.tile]) + "},")
    lines.append("};")
    args.output.parent.mkdir(parents=True, exist_ok=True); args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    report = {"source": scene["source"], "selected_textures": list(selected), "selected_primitives": textured_count, "source_triangles": int(scene["triangle_count"]), "render_primitives": int(scene["primitive_count"]), "paired_textured_quads": paired_quads, "tile": [args.tile, args.tile], "source_scale": args.source_scale, "source_filter": "RGB1555 box filter with majority alpha", "source_texture_bytes": sum(data[4] for data in texture_data.values()), "resampled_source_bytes": sum(data[0] * data[1] * 2 for data in texture_data.values()), "subdivision": args.subdivision, "subdivision_threshold": args.subdivision_threshold, "subdivided_triangles": sum(value == 4 for value in counts), "tile_count": len(positions), "texture_bytes": len(words) * 2, "command_estimate": 2 + len(positions) + (int(scene["primitive_count"]) - textured_count) + 1, "texture_state": "Fast3D image/load-tile/TMEM/render-tile v2: independent S/T clamp, mirror, mask, shift, tile origin/extent, SP scale, and retained LOD bindings", "uv_sampling": "Fast3D s10.5 sampling resolved in source-texel space; native VDP1 quads use measured C/B/A/D character-corner order, fallbacks retain C/B/A/C repeated-vertex tiles", "rom_sha256": hashlib.sha256(rom).hexdigest(), "texture_sha256": {name: data[3] for name, data in texture_data.items()}}
    args.report.parent.mkdir(parents=True, exist_ok=True); args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
