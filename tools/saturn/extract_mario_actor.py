#!/usr/bin/env python3
"""Flatten the normal in-game Mario actor from the SM64 source display lists.

This deliberately implements only the small Fast3D subset exercised by the
normal Mario actor: vertex loads, one/two triangle commands, nested display
lists, and light selection.  It emits a neutral-pose, source-provenanced
triangle stream for the first Saturn turntable.  Geometry is never invented:
every emitted triangle names its originating display list in the JSON report.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path

from saturn_mesh_ir import compile_mesh_ir


LIGHTS = {
    "mario_blue_lights_group": (0, 0, 31),
    "mario_red_lights_group": (31, 0, 0),
    "mario_white_lights_group": (31, 31, 31),
    "mario_brown1_lights_group": (14, 4, 2),
    "mario_beige_lights_group": (31, 24, 15),
    "mario_brown2_lights_group": (14, 1, 0),
}

BRANCH_SELECTIONS = {
    # These are the normal-cap/front-eye/open-hand source switch states used
    # by SM64's GeoLayout callbacks at boot. They select existing display
    # lists; no actor geometry is recreated here.
    "mario_geo_face_and_wings": ("mario_cap_on_eyes_front", (0, 0, 0)),
    "mario_geo_left_hand": ("mario_left_hand_open", (60, 0, 0)),
    "mario_geo_right_hand": ("mario_right_hand_open", (60, 0, 0)),
}


def blocks(source: str, kind: str) -> dict[str, str]:
    return {
        name: body
        for name, body in re.findall(
            rf"(?:static\s+)?const\s+{kind}\s+(\w+)\[\]\s*=\s*\{{(.*?)\}};",
            source,
            re.DOTALL,
        )
    }


def geo_tokens(body: str) -> list[tuple[str, str]]:
    """Tokenize the limited GeoLayout vocabulary used by mario_geo_body."""
    return [
        (match.group(1), match.group(2) or "")
        for match in re.finditer(r"(GEO_(?:OPEN_NODE|CLOSE_NODE)|GEO_\w+)\s*(?:\(([^)]*)\))?", body)
    ]


def geo_layout_parts(geo_source: str) -> list[tuple[str, tuple[int, int, int], str]]:
    """Evaluate the neutral mario_geo_body hierarchy from the original source.

    This first evaluator intentionally handles the bind pose: animated-part
    rotations are zero and ASM callbacks are runtime animation hooks. The
    renderer will consume those callbacks once the source animation path is
    brought over. The hierarchy and translations are read, not copied.
    """
    layouts = blocks(geo_source, "GeoLayout")
    tokens = geo_tokens(layouts["mario_geo_body"])
    result: list[tuple[str, tuple[int, int, int], str]] = []

    def walk(index: int, parent: tuple[int, int, int]) -> int:
        last = parent
        while index < len(tokens):
            macro, args = tokens[index]
            index += 1
            if macro == "GEO_CLOSE_NODE":
                return index
            if macro == "GEO_OPEN_NODE":
                index = walk(index, last)
                continue
            if macro == "GEO_RETURN":
                return index
            if macro == "GEO_ANIMATED_PART":
                fields = [field.strip() for field in args.split(",")]
                if len(fields) != 5:
                    raise ValueError(f"unexpected GEO_ANIMATED_PART: {args}")
                last = tuple(parent[axis] + int(fields[axis + 1]) for axis in range(3))
                display_list = fields[4]
                if display_list != "NULL":
                    result.append((display_list, last, "mario_blue_lights_group"))
                continue
            if macro == "GEO_DISPLAY_LIST":
                fields = [field.strip() for field in args.split(",")]
                if len(fields) == 2:
                    result.append((fields[1], parent, "mario_blue_lights_group"))
                continue
            if macro == "GEO_BRANCH":
                fields = [field.strip() for field in args.split(",")]
                if len(fields) == 2 and fields[1] in BRANCH_SELECTIONS:
                    display_list, offset = BRANCH_SELECTIONS[fields[1]]
                    result.append((display_list,
                      tuple(parent[axis] + offset[axis] for axis in range(3)),
                      "mario_blue_lights_group"))
                continue
            # GEO_ASM, GEO_ROTATION_NODE and GEO_SCALE are retained source
            # controls. Their neutral Mario bind-pose values are identity.
        return index

    walk(0, (0, 0, 0))
    if not result:
        raise ValueError("mario_geo_body did not produce any display lists")
    return result


def vertex_groups(source: str) -> dict[str, list[tuple[int, int, int, int, int]]]:
    result: dict[str, list[tuple[int, int, int, int, int]]] = {}
    for name, body in blocks(source, "Vtx").items():
        rows = re.findall(r"\{\{\{\s*(-?\d+),\s*(-?\d+),\s*(-?\d+)\s*\},\s*\d+,\s*\{\s*(-?\d+),\s*(-?\d+)", body)
        result[name] = [tuple(map(int, row)) for row in rows]
    return result


def ints(command: str) -> list[int]:
    return [int(value, 0) for value in re.findall(r"(?<![A-Za-z_])(?:0x[0-9A-Fa-f]+|\d+)", command)]


def flatten(display_lists: dict[str, str], vertices: dict[str, list[tuple[int, int, int, int, int]]],
            name: str, offset: tuple[int, int, int], light: str,
            out: list[dict[str, object]], stack: tuple[str, ...] = ()) -> str:
    if name in stack:
        raise ValueError(f"recursive display list: {' -> '.join(stack + (name,))}")
    body = display_lists.get(name)
    if body is None:
        raise ValueError(f"missing display list {name}")
    # Fast3D's vertex cache persists across gsSPVertex calls and each load
    # specifies a destination slot. Replacing the whole cache (the earlier
    # prototype) turns valid triangles that reference retained slots into an
    # exploded actor.
    cache: list[tuple[int, int, int, int, int] | None] = [None] * 32
    current_light = light
    for macro, args in re.findall(r"(gs\w+)\(([^;]*?)\)", body, re.DOTALL):
        if macro == "gsSPLight":
            found = re.search(r"&(mario_\w+_lights_group)\.", args)
            if found:
                current_light = found.group(1)
        elif macro == "gsSPVertex":
            group = re.match(r"\s*(\w+)", args)
            if group is None or group.group(1) not in vertices:
                raise ValueError(f"unknown vertex group in {name}: {args}")
            values = ints(args[group.end():])
            if len(values) != 2:
                raise ValueError(f"unexpected gsSPVertex arguments in {name}: {args}")
            count, destination = values
            loaded = vertices[group.group(1)]
            if count != len(loaded) or destination + count > len(cache):
                raise ValueError(f"invalid vertex cache load in {name}: {args}")
            cache[destination:destination + count] = loaded
        elif macro in ("gsSP1Triangle", "gsSP2Triangles"):
            values = ints(args)
            triples = (values[:3], values[4:7]) if macro == "gsSP2Triangles" else (values[:3],)
            for triangle in triples:
                if len(triangle) != 3 or any(index >= len(cache) or cache[index] is None for index in triangle):
                    raise ValueError(f"invalid triangle in {name}: {args}")
                positions = [
                    [cache[index][axis] + offset[axis] for axis in range(3)]
                    for index in triangle
                ]
                out.append({"rgb": LIGHTS[current_light], "positions": positions,
                            "uv": [[cache[index][3], cache[index][4]] for index in triangle],
                            "display_list": name})
        elif macro == "gsSPDisplayList":
            child = re.match(r"\s*(\w+)", args)
            if child:
                current_light = flatten(display_lists, vertices, child.group(1), offset, current_light, out, stack + (name,))
    return current_light


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--geo", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--mesh-ir-output", type=Path)
    args = parser.parse_args()
    model = args.model.read_text(encoding="utf-8")
    geo = args.geo.read_text(encoding="utf-8")
    display_lists, vertices = blocks(model, "Gfx"), vertex_groups(model)
    triangles: list[dict[str, object]] = []
    parts = geo_layout_parts(geo)
    for name, offset, light in parts:
        flatten(display_lists, vertices, name, offset, light, triangles)
    model_hash, geo_hash = hashlib.sha256(model.encode()).hexdigest(), hashlib.sha256(geo.encode()).hexdigest()
    # Build the same versioned interchange document used by the intro face.
    # Position sharing is preserved only when the complete source-space point
    # agrees; material boundaries remain explicit primitive boundaries.
    positions: list[list[int]] = []
    position_index: dict[tuple[int, int, int], int] = {}
    materials: list[dict[str, object]] = []
    material_index: dict[tuple[int, int, int], int] = {}
    ir_triangles: list[dict[str, object]] = []
    for source, triangle in enumerate(triangles):
        rgb = tuple(triangle["rgb"])
        if rgb not in material_index:
            material_index[rgb] = len(materials)
            materials.append({"id": material_index[rgb], "rgb555": list(rgb)})
        indices: list[int] = []
        for point in triangle["positions"]:
            key = tuple(point)
            if key not in position_index:
                position_index[key] = len(positions)
                positions.append(list(key))
            indices.append(position_index[key])
        ir_triangles.append({"source": source, "material": material_index[rgb], "indices": indices})
    source_ir = {
        "schema": "sm64-saturn-mesh-ir", "version": 1,
        "name": "normal_mario_neutral_turntable", "positions": positions,
        "materials": materials, "triangles": ir_triangles,
        "vertex_attributes": {}, "validation_poses": [],
        "source": {"model": {"path": str(args.model).replace("\\\\", "/"), "sha256": hashlib.sha256(model.encode()).hexdigest()},
                   "geo": {"path": str(args.geo).replace("\\\\", "/"), "sha256": hashlib.sha256(geo.encode()).hexdigest()}},
    }
    compiled_ir, primitives, quad_report = compile_mesh_ir(source_ir)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "/* Generated by tools/saturn/extract_mario_actor.py; do not edit. */",
        "/* Direct data conversion from actors/mario/model.inc.c and geo.inc.c. */",
        f"/* model SHA-256: {model_hash}; geo SHA-256: {geo_hash} */",
        "#ifndef SM64_SATURN_MARIO_ACTOR_MESH_H",
        "#define SM64_SATURN_MARIO_ACTOR_MESH_H",
        f"#define SM64_MARIO_TRIANGLE_COUNT {len(triangles)}U",
        f"#define SM64_MARIO_VERTEX_COUNT {len(positions)}U",
        f"#define SM64_MARIO_PRIMITIVE_COUNT {len(primitives)}U",
        f"#define SM64_MARIO_QUAD_COUNT {quad_report['quad_count']}U",
        "/* Source-space vertices and RGB555 material table. */",
        "static const int16_t sm64_mario_vertices[SM64_MARIO_VERTEX_COUNT][3] = {",
    ]
    lines += ["    {" + ", ".join(map(str, point)) + "}," for point in positions]
    lines += ["};", f"#define SM64_MARIO_MATERIAL_COUNT {len(materials)}U", "static const uint8_t sm64_mario_material_rgb[SM64_MARIO_MATERIAL_COUNT][3] = {"]
    lines += ["    {" + ", ".join(map(str, material["rgb555"])) + "}," for material in materials]
    lines += ["};", "/* material,a,b,c,d; d repeats c for explicit triangle fallbacks. */", "static const uint16_t sm64_mario_primitives[SM64_MARIO_PRIMITIVE_COUNT][5] = {"]
    lines += ["    {%d, %d, %d, %d, %d}," % (primitive.material, *primitive.vertices) for primitive in primitives]
    # The selected normal-cap/front-eye branch supplies this exact source
    # patch. It remains separate because VDP1 textures are rectangular; M2's
    # full UV tessellation follows after this verified first textured surface.
    eye_vertices = [
        (point[0] + 155, point[1], point[2])
        for point in vertices["mario_eyes_cap_on_dl_vertex"]
    ]
    lines += ["};", f"#define SM64_MARIO_EYE_TEXTURE_VERTEX_COUNT {len(eye_vertices)}U", "static const int16_t sm64_mario_eye_texture_vertices[SM64_MARIO_EYE_TEXTURE_VERTEX_COUNT][3] = {"]
    lines += ["    {" + ", ".join(map(str, point)) + "}," for point in eye_vertices]
    lines += ["};", "#endif"]
    args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps({
        "name": "normal_mario_neutral_turntable",
        "reuse": "direct data conversion",
        "source": {"model": {"path": str(args.model).replace("\\\\", "/"), "sha256": model_hash}, "geo": {"path": str(args.geo).replace("\\\\", "/"), "sha256": geo_hash}},
        "geo_layout": "mario_geo_body",
        "parts": [{"display_list": name, "offset": offset} for name, offset, _light in parts],
        "triangle_count": len(triangles), "vertex_count": len(positions),
        "primitive_count": len(primitives), "quad_count": quad_report["quad_count"],
        "triangle_fallback_count": quad_report["standalone_triangle_count"],
        "triangle_display_lists": sorted({str(item["display_list"]) for item in triangles}),
        "textured_eye_triangles": [
            {"source": index, "positions": triangle["positions"], "uv": triangle["uv"]}
            for index, triangle in enumerate(triangles)
            if triangle["display_list"] == "mario_eyes_cap_on_dl"
        ],
        "geo_evaluator": {
            "layout": "mario_geo_body",
            "implemented": ["GEO_ANIMATED_PART", "GEO_OPEN_NODE", "GEO_CLOSE_NODE", "GEO_BRANCH", "GEO_DISPLAY_LIST"],
            "branch_selections": BRANCH_SELECTIONS,
        },
        "limits": ["neutral bind pose only", "GeoLayout rotation and ASM animation callbacks are identity until the source animation path is adapted", "RGBA16 texture commands use source light colors until VDP1 texture conversion"],
        "mesh_ir": compiled_ir,
    }, indent=2) + "\n", encoding="utf-8")
    if args.mesh_ir_output is not None:
        args.mesh_ir_output.parent.mkdir(parents=True, exist_ok=True)
        args.mesh_ir_output.write_text(json.dumps(compiled_ir, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
