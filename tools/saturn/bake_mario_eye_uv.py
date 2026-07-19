#!/usr/bin/env python3
"""Bake SM64's normal-Mario UV triangles into VDP1-safe direct-color tiles.

The N64 display list supplies per-vertex UVs; VDP1 distorted sprites do not.
This local-only stage samples each source triangle into a complete 16x16 VDP1
distorted-sprite texture. Runtime maps the complete tile to the matching
repeated-vertex triangle. ROM-derived output stays below build/.
"""
from __future__ import annotations

import argparse, json
from collections import Counter
from pathlib import Path

from extract_mario_textures import mio0_decode, rom_bytes, saturn_rgb1555
from vdp1_texture import downsample_rgb1555, repeated_vertex_weights

# One 16×16 RGB1555 tile per source triangle keeps the current source-actor
# path at 25,600 bytes.  Optional four-way subdivision remains a diagnostic
# quality tier; a 32×32 trial consumed four times the residency without fixing
# the then-unresolved target mapping/mode defect.
TILE = 16
TEXTURE_ASSETS = {
    "mario_texture_eyes_front": "mario_eyes_center",
    "mario_texture_m_logo": "mario_logo",
    "mario_texture_hair_sideburn": "mario_sideburn",
    "mario_texture_mustache": "mario_mustache",
    "mario_texture_yellow_button": "mario_overalls_button",
}

def bilinear_weights(x: int, y: int, tile: int = TILE) -> tuple[float, float, float]:
    """Map VDP1's repeated-vertex sprite texel corners to Fast3D A/B/C.

    The patterned BIOS-backed HWTEST probe establishes that a command emitted
    as (A, B, C, C) maps its source tile corners as C, B, A, [collapsed].
    This is not conventional image-space triangle order.  Keep the transform
    here beside the offline bake so the output remains one target-native tile
    per subtriangle rather than a software framebuffer workaround.
    """
    return repeated_vertex_weights(x, y, tile, tile)

def pixel(texture: list[int], width: int, height: int, u: float, v: float) -> int:
    # Coordinates have already been adjusted for source downsampling.
    x = max(0, min(width - 1, int(u / 32.0)))
    y = max(0, min(height - 1, int(v / 32.0)))
    return texture[y * width + x]


def blend(vertices: list[list[int]], weights: tuple[float, float, float]) -> list[int]:
    return [round(sum(weights[index] * vertices[index][axis] for index in range(3))) for axis in range(len(vertices[0]))]

def split_four(triangle: dict[str, object]) -> list[dict[str, object]]:
    """Split one Fast3D triangle into four source-space affine subtriangles."""
    positions, uv = triangle["positions"], triangle["uv"]
    a, b, c = (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)
    ab, bc, ca = (0.5, 0.5, 0.0), (0.0, 0.5, 0.5), (0.5, 0.0, 0.5)
    point = lambda weights: {"position": blend(positions, weights), "uv": blend(uv, weights)}
    corners = {"a": point(a), "b": point(b), "c": point(c), "ab": point(ab), "bc": point(bc), "ca": point(ca)}
    return [{"positions": [corners[key]["position"] for key in keys], "uv": [corners[key]["uv"] for key in keys]}
            for keys in (("a", "ab", "ca"), ("ab", "b", "bc"), ("ca", "bc", "c"), ("ab", "bc", "ca"))]

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--intake", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--tile", type=int, choices=(8, 16, 32), default=TILE)
    parser.add_argument("--source-scale", type=int, choices=(1, 2, 4), default=1)
    parser.add_argument("--subdivision", type=int, choices=(1, 4), default=1)
    args = parser.parse_args()
    assets = json.loads(args.assets.read_text(encoding="utf-8"))
    intake = json.loads(args.intake.read_text(encoding="utf-8"))
    rom = rom_bytes(args.rom)
    decoded: dict[int, bytes] = {}
    textures: dict[str, tuple[list[int], int, int]] = {}
    source_texture_bytes = 0
    source_triangles = intake["textured_triangles"]
    for texture_name in sorted({str(item["texture"]) for item in source_triangles}):
        asset_name = TEXTURE_ASSETS.get(texture_name)
        if asset_name is None:
            raise ValueError(f"no local ROM asset mapping for {texture_name}")
        width, height, size, regions = assets[f"actors/mario/{asset_name}.rgba16.png"]
        base, offset = regions["us"]
        image = decoded.setdefault(base, mio0_decode(rom, base))
        source = image[offset:offset + size]
        if len(source) != size:
            raise ValueError(f"{texture_name}: range outside decompressed MIO0 segment")
        source_texture_bytes += len(source)
        converted = [saturn_rgb1555(int.from_bytes(source[index:index + 2], "big")) for index in range(0, len(source), 2)]
        scaled_width, scaled_height, scaled = downsample_rgb1555(converted, width, height, args.source_scale)
        textures[texture_name] = (scaled, scaled_width, scaled_height)
    triangles = ([
        {**subtriangle, "texture": source_triangle["texture"],
         "combine_mode": source_triangle["combine_mode"]}
        for source_triangle in source_triangles
        for subtriangle in split_four(source_triangle)
    ] if args.subdivision == 4 else source_triangles)
    tiles: list[list[int]] = []
    combine_modes = {str(triangle["combine_mode"]) for triangle in triangles}
    if combine_modes != {"G_CC_BLENDRGBFADEA"}:
        raise ValueError(f"unsupported Mario texture combine modes: {sorted(combine_modes)}")
    for triangle in triangles:
        uv = triangle["uv"]
        source, width, height = textures[str(triangle["texture"])]
        tile: list[int] = []
        for y in range(args.tile):
            for x in range(args.tile):
                a, b, c = bilinear_weights(x, y, args.tile)
                u = (a * uv[0][0] + b * uv[1][0] + c * uv[2][0]) / args.source_scale
                v = (a * uv[0][1] + b * uv[1][1] + c * uv[2][1]) / args.source_scale
                tile.append(pixel(source, width, height, u, v))
        tiles.append(tile)
    transparent_word_count = sum(word == 0 for tile in tiles for word in tile)
    opaque_word_count = sum(bool(word & 0x8000) for tile in tiles for word in tile)
    invalid_words = [
        word for tile in tiles for word in tile
        if word != 0 and not (word & 0x8000)
    ]
    if invalid_words:
        raise ValueError(
            "VDP1 direct-color bake emitted nonzero bit-15-clear words: "
            f"{len(invalid_words)}"
        )
    lines = ["/* Local ROM-derived output: do not commit. */", "#pragma once",
             f"#define SM64_MARIO_TEXTURE_UV_TRIANGLE_COUNT {len(triangles)}U",
             f"#define SM64_MARIO_TEXTURE_TILES_PER_SOURCE {args.subdivision}U",
             f"#define SM64_MARIO_TEXTURE_UV_TILE_WIDTH {args.tile}U",
             "static const int16_t sm64_mario_texture_uv_positions[SM64_MARIO_TEXTURE_UV_TRIANGLE_COUNT][3][3] = {"]
    for triangle in triangles:
        lines.append("    {" + ", ".join("{" + ", ".join(str(value) for value in vertex) + "}" for vertex in triangle["positions"]) + "},")
    lines += ["};", "static const uint16_t sm64_mario_texture_uv_tiles[SM64_MARIO_TEXTURE_UV_TRIANGLE_COUNT][SM64_MARIO_TEXTURE_UV_TILE_WIDTH * SM64_MARIO_TEXTURE_UV_TILE_WIDTH] = {"]
    for tile in tiles:
        lines.append("    {")
        lines.extend("        " + ", ".join(f"0x{word:04X}" for word in tile[index:index + 8]) + "," for index in range(0, len(tile), 8))
        lines.append("    },")
    lines += ["};"]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps({"source": "mario_geo_body normal-cap/front branch", "source_triangle_count": len(source_triangles), "triangle_count": len(triangles), "textures": dict(Counter(str(item["texture"]) for item in source_triangles)), "subdivision": f"{args.subdivision} affine tile(s) per source triangle", "tile": [args.tile, args.tile], "source_scale": args.source_scale, "source_filter": "RGB1555 box filter with majority alpha; A1=0 canonicalized to VDP1 0x0000", "source_texture_bytes": source_texture_bytes, "resampled_source_bytes": sum(width * height * 2 for _pixels, width, height in textures.values()), "texture_bytes": len(tiles) * args.tile * args.tile * 2, "vdp1_default_texture_partition_bytes": 0x0006BFE0, "uv_space": "Fast3D source UV / 32", "combine_mode": "G_CC_BLENDRGBFADEA", "lowering": "atomic neutral-base Gouraud polygon plus alpha-keyed CC_REPLACE texture detail", "vdp1_output_alpha": "A1=0 emits exact VDP1 transparent word 0x0000; opaque detail words have RGB1555 bit 15 set", "vdp1_end_code_policy": "disabled for fixed-size source tiles; emitted texture words are constrained to 0x0000 or MSB-set opaque values", "vdp1_transparent_word_count": transparent_word_count, "vdp1_opaque_word_count": opaque_word_count, "vdp1_nonzero_msb_clear_word_count": len(invalid_words), "mapping": "complete per-subtriangle UV bake; VDP1 A/B/C/D character corners with repeated destination C receiving both source C and D"}, indent=2) + "\n", encoding="utf-8")

if __name__ == "__main__":
    main()
