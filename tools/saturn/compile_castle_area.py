#!/usr/bin/env python3
"""Compile the source-selected Castle Area 1 roots into a compact Saturn IR.

The original GeoLayout remains authoritative at runtime.  This host pass only
serializes its static Fast3D payloads and preserves each triangle's root and
render layer so the SH-2 graph walker can select target-native VDP1 commands.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from extract_castle_area import extract
from quad_pairing import candidates, maximum_weight_matching


LAYERS = {
    "LAYER_OPAQUE": 0,
    "LAYER_TRANSPARENT_DECAL": 1,
    "LAYER_ALPHA": 2,
}


def _build_primitives(
    positions: list[tuple[int, int, int]],
    triangles: list[tuple[int, int, int]],
    sources: list[str],
    texture_indices: list[int],
    uv: list[tuple[tuple[int, int], tuple[int, int], tuple[int, int]]],
    tile_state: list[dict[str, object] | None],
    roots: list[int],
    layers: list[int],
) -> tuple[list[dict[str, object]], dict[str, object]]:
    """Pair only source-identical, rectangular-UV triangles into VDP1 quads.

    Geometry safety and exact matching come from quad_pairing.  Castle adds the
    Fast3D contract: a pair may not cross a display-list, root, layer, texture,
    tile-state, or UV seam.  Rejected triangles remain explicit four-corner
    repeated-vertex primitives, so this optimization cannot remove source data.
    """
    material_keys = [
        (roots[index], layers[index], texture_indices[index], sources[index],
         json.dumps(tile_state[index], sort_keys=True))
        for index in range(len(triangles))
    ]
    material_ids: dict[tuple[object, ...], int] = {}
    for key in material_keys:
        material_ids.setdefault(key, len(material_ids))
    faces = [
        (material_ids[material_keys[index]], *triangle)
        for index, triangle in enumerate(triangles)
    ]
    geometry_options, geometry_rejections = candidates(positions, faces)
    safe_options = []
    uv_rejections: dict[str, int] = {}

    def reject(reason: str) -> None:
        uv_rejections[reason] = uv_rejections.get(reason, 0) + 1

    for option in geometry_options:
        vertex_uv: dict[int, tuple[int, int]] = {}
        compatible = True
        for triangle_index in (option.first, option.second):
            for vertex, pair in zip(triangles[triangle_index], uv[triangle_index]):
                if vertex in vertex_uv and vertex_uv[vertex] != pair:
                    compatible = False
                vertex_uv[vertex] = pair
        if not compatible:
            reject("shared_uv_mismatch")
            continue
        domain = set(vertex_uv.values())
        if (len(domain) != 4 or
                len({pair[0] for pair in domain}) != 2 or
                len({pair[1] for pair in domain}) != 2):
            reject("uv_domain_not_rectangle")
            continue
        safe_options.append(option)

    matched = maximum_weight_matching(safe_options)
    primitives: list[dict[str, object]] = []
    emitted: set[int] = set()
    for triangle_index, triangle in enumerate(triangles):
        if triangle_index in emitted:
            continue
        option = matched.get(triangle_index)
        second = None if option is None else option.second
        vertices = (*triangle, triangle[2]) if option is None else option.vertices
        primitives.append({
            "vertices": vertices,
            "first_triangle": triangle_index,
            "second_triangle": second,
            "root": roots[triangle_index],
            "layer": layers[triangle_index],
            "texture_index": texture_indices[triangle_index],
        })
        emitted.add(triangle_index)
        if option is not None:
            emitted.update((option.first, option.second))

    quad_count = sum(primitive["second_triangle"] is not None for primitive in primitives)
    return primitives, {
        "policy": "source-identical view-stable rectangular-UV VDP1 quads",
        "projection_policy": "multi-view convexity; large painter-coarse walls remain subdivided triangles",
        "matcher": "networkx.max_weight_matching",
        "source_triangle_count": len(triangles),
        "geometry_candidate_count": len(geometry_options),
        "textured_quad_candidate_count": len(safe_options),
        "quad_count": quad_count,
        "standalone_triangle_count": len(primitives) - quad_count,
        "render_primitive_count": len(primitives),
        "commands_saved": len(triangles) - len(primitives),
        "geometry_rejection_reasons": geometry_rejections,
        "texture_rejection_reasons": dict(sorted(uv_rejections.items())),
    }


def compile_scene(area: Path, selected_layers: set[str] | None = None) -> dict[str, object]:
    """Return a deterministic indexed representation of selected source roots."""
    scene = extract(area)
    roots = [str(root["display_list"]) for root in scene["roots"]]
    positions: list[tuple[int, int, int]] = []
    position_index: dict[tuple[int, int, int], int] = {}
    triangles: list[tuple[int, int, int]] = []
    sources: list[str] = []
    triangle_textures: list[str | None] = []
    triangle_uv: list[tuple[tuple[int, int], tuple[int, int], tuple[int, int]]] = []
    triangle_tiles: list[dict[str, object] | None] = []
    triangle_roots: list[int] = []
    triangle_layers: list[int] = []
    for triangle in scene["triangles"]:
        layer = str(triangle["layer"])
        if selected_layers is not None and layer not in selected_layers:
            continue
        indices: list[int] = []
        for raw in triangle["positions"]:
            position = tuple(int(value) for value in raw)
            index = position_index.get(position)
            if index is None:
                index = len(positions)
                position_index[position] = index
                positions.append(position)
            indices.append(index)
        triangles.append(tuple(indices))
        sources.append(str(triangle["source_display_list"]))
        triangle_textures.append(None if triangle["texture"] is None else str(triangle["texture"]))
        triangle_uv.append(tuple(tuple(int(value) for value in uv) for uv in triangle["uv"]))
        triangle_tiles.append(triangle["tile"])
        triangle_roots.append(roots.index(str(triangle["root_display_list"])))
        triangle_layers.append(LAYERS[layer])
    textures = sorted({texture for texture in triangle_textures if texture is not None})
    texture_indices = [0xFF if texture is None else textures.index(texture) for texture in triangle_textures]
    primitives, pairing = _build_primitives(
        positions, triangles, sources, texture_indices, triangle_uv,
        triangle_tiles, triangle_roots, triangle_layers,
    )
    return {
        "schema": "sm64-saturn-castle-static-ir",
        "version": 3,
        "name": "castle_inside_area_1_source_roots",
        "source": "levels/castle_inside/areas/1",
        "layers": {name: value for name, value in LAYERS.items()
                   if selected_layers is None or name in selected_layers},
        "roots": roots,
        "vertex_count": len(positions),
        "triangle_count": len(triangles),
        "source_display_lists": sorted(set(sources)),
        "triangle_sources": sources,
        "textures": textures,
        "texture_indices": texture_indices,
        "triangle_roots": triangle_roots,
        "triangle_layers": triangle_layers,
        "positions": positions,
        "triangles": triangles,
        "uv": triangle_uv,
        "tile_state": triangle_tiles,
        "primitive_count": len(primitives),
        "primitives": primitives,
        "pairing": pairing,
        "texture_state_version": 2,
        "limits": ["Static geometry payloads only", "Runtime graph selection retained", "No visibility or clipping"],
    }


def compile_opaque(area: Path) -> dict[str, object]:
    """Compatibility view used by established camera-planning tests/tools."""
    bank = compile_scene(area, {"LAYER_OPAQUE"})
    bank["name"] = "castle_inside_area_1_opaque_root"
    bank["layer"] = "LAYER_OPAQUE"
    return bank


def write_header(bank: dict[str, object], output: Path) -> None:
    positions = bank["positions"]
    triangles = bank["triangles"]
    texture_indices = bank["texture_indices"]
    uv = bank["uv"]
    primitives = bank["primitives"]
    lines = [
        "/* Generated by tools/saturn/compile_castle_area.py; do not edit. */",
        "#pragma once",
        "#include <stdint.h>",
        f"#define SM64_CASTLE_AREA1_VERTEX_COUNT {len(positions)}U",
        f"#define SM64_CASTLE_AREA1_TRIANGLE_COUNT {len(triangles)}U",
        f"#define SM64_CASTLE_AREA1_PRIMITIVE_COUNT {len(primitives)}U",
        f"#define SM64_CASTLE_AREA1_TEXTURE_COUNT {len(bank['textures'])}U",
        f"#define SM64_CASTLE_AREA1_ROOT_COUNT {len(bank['roots'])}U",
        "#define SM64_CASTLE_LAYER_OPAQUE 0U",
        "#define SM64_CASTLE_LAYER_TRANSPARENT_DECAL 1U",
        "#define SM64_CASTLE_LAYER_ALPHA 2U",
        "#define SM64_CASTLE_AREA1_TEXTURE_NONE 0xFFU",
        "static const int16_t sm64_castle_area1_vertices[SM64_CASTLE_AREA1_VERTEX_COUNT][3] = {",
    ]
    lines.extend(f"    {{{x}, {y}, {z}}}," for x, y, z in positions)
    lines.append("};")
    lines.append("static const uint16_t sm64_castle_area1_primitives[SM64_CASTLE_AREA1_PRIMITIVE_COUNT][4] = {")
    lines.extend("    {" + ", ".join(str(value) for value in primitive["vertices"]) + "}," for primitive in primitives)
    lines.append("};")
    lines.append("static const uint16_t sm64_castle_area1_primitive_source[SM64_CASTLE_AREA1_PRIMITIVE_COUNT] = {")
    lines.extend("    " + ", ".join(f"{primitive['first_triangle']}U" for primitive in primitives[offset:offset + 16]) + "," for offset in range(0, len(primitives), 16))
    lines.append("};")
    lines.append("static const uint8_t sm64_castle_area1_primitive_root[SM64_CASTLE_AREA1_PRIMITIVE_COUNT] = {")
    lines.extend("    " + ", ".join(f"{primitive['root']}U" for primitive in primitives[offset:offset + 16]) + "," for offset in range(0, len(primitives), 16))
    lines.append("};")
    lines.append("static const uint8_t sm64_castle_area1_primitive_layer[SM64_CASTLE_AREA1_PRIMITIVE_COUNT] = {")
    lines.extend("    " + ", ".join(f"{primitive['layer']}U" for primitive in primitives[offset:offset + 16]) + "," for offset in range(0, len(primitives), 16))
    lines.append("};")
    lines.append("static const uint16_t sm64_castle_area1_triangles[SM64_CASTLE_AREA1_TRIANGLE_COUNT][3] = {")
    lines.extend(f"    {{{a}, {b}, {c}}}," for a, b, c in triangles)
    lines.append("};")
    lines.append("static const uint8_t sm64_castle_area1_triangle_texture[SM64_CASTLE_AREA1_TRIANGLE_COUNT] = {")
    lines.extend("    " + ", ".join(f"{value}U" for value in texture_indices[offset:offset + 16]) + "," for offset in range(0, len(texture_indices), 16))
    lines.append("};")
    lines.append("static const uint8_t sm64_castle_area1_triangle_root[SM64_CASTLE_AREA1_TRIANGLE_COUNT] = {")
    lines.extend("    " + ", ".join(f"{value}U" for value in bank["triangle_roots"][offset:offset + 16]) + "," for offset in range(0, len(triangles), 16))
    lines.append("};")
    lines.append("static const uint8_t sm64_castle_area1_triangle_layer[SM64_CASTLE_AREA1_TRIANGLE_COUNT] = {")
    lines.extend("    " + ", ".join(f"{value}U" for value in bank["triangle_layers"][offset:offset + 16]) + "," for offset in range(0, len(triangles), 16))
    lines.append("};")
    lines.append("static const int16_t sm64_castle_area1_triangle_uv[SM64_CASTLE_AREA1_TRIANGLE_COUNT][3][2] = {")
    lines.extend(f"    {{{{{a}, {b}}}, {{{c}, {d}}}, {{{e}, {f}}}}}," for (a, b), (c, d), (e, f) in uv)
    lines.append("};")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--area", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    bank = compile_scene(args.area)
    write_header(bank, args.output)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(bank, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
