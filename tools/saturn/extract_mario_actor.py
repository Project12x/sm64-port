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
import math
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


Matrix = tuple[float, float, float, float, float, float, float, float, float, float, float, float]

def identity_matrix() -> Matrix:
    return (1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0)

def source_matrix(translation: tuple[int, int, int], rotation: tuple[int, int, int]) -> Matrix:
    """Close port mtxf_rotate_xyz_and_translate() from math_util.c."""
    sx, sy, sz = (math.sin(value * math.tau / 65536.0) for value in rotation)
    cx, cy, cz = (math.cos(value * math.tau / 65536.0) for value in rotation)
    return (cy * cz, cy * sz, -sy,
            sx * sy * cz - cx * sz, sx * sy * sz + cx * cz, sx * cy,
            cx * sy * cz + sx * sz, cx * sy * sz - sx * cz, cx * cy,
            float(translation[0]), float(translation[1]), float(translation[2]))

def matrix_mul(local: Matrix, parent: Matrix) -> Matrix:
    rows = tuple(sum(local[row * 3 + axis] * parent[axis * 3 + col] for axis in range(3)) for row in range(3) for col in range(3))
    translation = tuple(sum(local[9 + axis] * parent[axis * 3 + col] for axis in range(3)) + parent[9 + col] for col in range(3))
    return rows + translation

def matrix_apply(matrix: Matrix, point: tuple[int, int, int]) -> tuple[int, int, int]:
    return tuple(round(sum(point[axis] * matrix[axis * 3 + col] for axis in range(3)) + matrix[9 + col]) for col in range(3))

def animation_rotations(source: str, frame: int) -> list[tuple[int, int, int]]:
    """Read a source Animation's index/value stream like retrieve_animation_index()."""
    name = re.search(r"struct Animation (anim_\w+)\[\]", source)
    if name is None:
        raise ValueError("missing source Animation header")
    stem = name.group(1)
    def values(suffix: str) -> list[int]:
        body = re.search(rf"{stem}_{suffix}\[\]\s*=\s*\{{(.*?)\}};", source, re.DOTALL)
        if body is None:
            raise ValueError(f"missing {stem}_{suffix}")
        raw = [int(value, 0) for value in re.findall(r"(?:0x[0-9A-Fa-f]+|-?\d+)", body.group(1))]
        return [value - 0x10000 if suffix == "values" and value & 0x8000 else value for value in raw]
    indices, samples = values("indices"), values("values")
    if len(indices) < 6 or len(indices) % 2:
        raise ValueError("invalid source Animation index stream")
    cursor = 6  # source root consumes translation x/y/z before part rotations.
    rotations: list[tuple[int, int, int]] = []
    while cursor + 5 < len(indices):
        triplet = []
        for axis in range(3):
            count, offset = indices[cursor], indices[cursor + 1]
            cursor += 2
            triplet.append(samples[offset + min(frame, count - 1)])
        rotations.append(tuple(triplet))
    return rotations

def geo_layout_parts(geo_source: str, rotations: list[tuple[int, int, int]]) -> list[tuple[str, Matrix, str]]:
    """Evaluate the neutral mario_geo_body hierarchy from the original source.

    This first evaluator intentionally handles the bind pose: animated-part
    rotations are zero and ASM callbacks are runtime animation hooks. The
    renderer will consume those callbacks once the source animation path is
    brought over. The hierarchy and translations are read, not copied.
    """
    layouts = blocks(geo_source, "GeoLayout")
    tokens = geo_tokens(layouts["mario_geo_body"])
    result: list[tuple[str, Matrix, str]] = []
    rotation_index = 0
    animated_part_index = 0

    def next_rotation() -> tuple[int, int, int]:
        nonlocal rotation_index
        rotation = rotations[rotation_index] if rotation_index < len(rotations) else (0, 0, 0)
        rotation_index += 1
        return rotation

    def walk(index: int, parent: Matrix) -> int:
        last = parent
        while index < len(tokens):
            macro, args = tokens[index]
            index += 1
            if macro == "GEO_CLOSE_NODE":
                return index
            if macro == "GEO_OPEN_NODE":
                # GeoLayout sibling nodes all begin at this walk's parent
                # matrix.  `last` is only the matrix of the immediately
                # preceding node whose child list follows the OPEN_NODE.
                # Match geo_process_animated_part's push / recurse / pop:
                # when that child list returns, its local joint transform
                # must not leak into a later arm, leg, or head sibling.
                index = walk(index, last)
                last = parent
                continue
            if macro == "GEO_RETURN":
                return index
            if macro == "GEO_ANIMATED_PART":
                nonlocal animated_part_index
                fields = [field.strip() for field in args.split(",")]
                if len(fields) != 5:
                    raise ValueError(f"unexpected GEO_ANIMATED_PART: {args}")
                # The root first consumes its translation channels, then the
                # same geo_process_animated_part invocation immediately sees
                # ANIM_TYPE_ROTATION and consumes its rotation triplet too.
                # Skipping that triplet shifts every later limb channel.
                rotation = next_rotation()
                animated_part_index += 1
                last = matrix_mul(source_matrix(tuple(int(fields[axis + 1]) for axis in range(3)), rotation), parent)
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
                    branch_matrix = parent if fields[1] == "mario_geo_face_and_wings" else matrix_mul(source_matrix(offset, next_rotation()), parent)
                    result.append((display_list, branch_matrix, "mario_blue_lights_group"))
                continue
            # GEO_ASM, GEO_ROTATION_NODE and GEO_SCALE are retained source
            # controls. Their neutral Mario bind-pose values are identity.
        return index

    # The preview intentionally leaves root *translation* at zero (turntable
    # placement), but consumes root rotation and every later joint triplet in
    # the same order as geo_process_animated_part's attribute cursor.
    walk(0, identity_matrix())
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
            name: str, matrix: Matrix, light: str,
            out: list[dict[str, object]], texture: str | None = None,
            stack: tuple[str, ...] = ()) -> tuple[str, str | None]:
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
    current_texture = texture
    for macro, args in re.findall(r"(gs\w+)\(([^;]*?)\)", body, re.DOTALL):
        if macro == "gsSPLight":
            found = re.search(r"&(mario_\w+_lights_group)\.", args)
            if found:
                current_light = found.group(1)
        elif macro == "gsDPSetTextureImage":
            found = re.search(r"\b(mario_texture_\w+)\b", args)
            if found:
                current_texture = found.group(1)
        elif macro == "gsSPTexture" and "G_OFF" in args:
            current_texture = None
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
                    list(matrix_apply(matrix, tuple(cache[index][0:3])))
                    for index in triangle
                ]
                out.append({"rgb": LIGHTS[current_light], "positions": positions,
                            "uv": [[cache[index][3], cache[index][4]] for index in triangle],
                            "display_list": name, "texture": current_texture})
        elif macro == "gsSPDisplayList":
            child = re.match(r"\s*(\w+)", args)
            if child:
                current_light, current_texture = flatten(
                    display_lists, vertices, child.group(1), matrix, current_light,
                    out, current_texture, stack + (name,)
                )
    return current_light, current_texture


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--geo", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--mesh-ir-output", type=Path)
    parser.add_argument("--animation", type=Path, help="source Mario Animation table for the fixed preview pose")
    parser.add_argument("--animation-frame", type=int, default=0)
    args = parser.parse_args()
    model = args.model.read_text(encoding="utf-8")
    geo = args.geo.read_text(encoding="utf-8")
    display_lists, vertices = blocks(model, "Gfx"), vertex_groups(model)
    triangles: list[dict[str, object]] = []
    rotations = animation_rotations(args.animation.read_text(encoding="utf-8"), args.animation_frame) if args.animation else []
    parts = geo_layout_parts(geo, rotations)
    for name, matrix, light in parts:
        flatten(display_lists, vertices, name, matrix, light, triangles)
    model_hash, geo_hash = hashlib.sha256(model.encode()).hexdigest(), hashlib.sha256(geo.encode()).hexdigest()
    # Build the same versioned interchange document used by the intro face.
    # Position sharing is preserved only when the complete source-space point
    # agrees; material boundaries remain explicit primitive boundaries.
    positions: list[list[int]] = []
    position_index: dict[tuple[int, int, int], int] = {}
    materials: list[dict[str, object]] = []
    material_index: dict[tuple[tuple[int, int, int], str | None], int] = {}
    ir_triangles: list[dict[str, object]] = []
    for source, triangle in enumerate(triangles):
        rgb = tuple(triangle["rgb"])
        texture = triangle["texture"]
        material_key = (rgb, texture if isinstance(texture, str) else None)
        if material_key not in material_index:
            material_index[material_key] = len(materials)
            materials.append({"id": material_index[material_key], "rgb555": list(rgb), "texture": material_key[1]})
        indices: list[int] = []
        for point in triangle["positions"]:
            key = tuple(point)
            if key not in position_index:
                position_index[key] = len(positions)
                positions.append(list(key))
            indices.append(position_index[key])
        ir_triangles.append({"source": source, "material": material_index[material_key], "indices": indices})
    source_ir = {
        "schema": "sm64-saturn-mesh-ir", "version": 1,
        "name": "normal_mario_neutral_turntable", "positions": positions,
        "materials": materials, "triangles": ir_triangles,
        "pairing_forbidden_triangles": [index for index, triangle in enumerate(triangles) if triangle["texture"] is not None],
        "vertex_attributes": {}, "validation_poses": [],
        "source": {"model": {"path": str(args.model).replace("\\\\", "/"), "sha256": hashlib.sha256(model.encode()).hexdigest()},
                   "geo": {"path": str(args.geo).replace("\\\\", "/"), "sha256": hashlib.sha256(geo.encode()).hexdigest()}},
    }
    compiled_ir, primitives, quad_report = compile_mesh_ir(source_ir)
    textured_source_ids = [index for index, triangle in enumerate(triangles) if triangle["texture"] is not None]
    textured_source_rank = {source: rank for rank, source in enumerate(textured_source_ids)}
    texture_tile_start = [0xFFFF] * len(primitives)
    for primitive_index, primitive in enumerate(compiled_ir["primitives"]):
        sources = primitive["source_triangles"]
        if any(source in textured_source_rank for source in sources):
            if len(sources) != 1 or sources[0] not in textured_source_rank:
                raise ValueError("textured source triangles must remain individual VDP1 primitives")
            texture_tile_start[primitive_index] = textured_source_rank[sources[0]] * 4
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
        f"#define SM64_MARIO_TEXTURED_SOURCE_TRIANGLE_COUNT {len(textured_source_ids)}U",
        "#define SM64_MARIO_TEXTURE_TILE_NONE 0xFFFFU",
        "/* Source-space vertices and RGB555 material table. */",
        "static const int16_t sm64_mario_vertices[SM64_MARIO_VERTEX_COUNT][3] = {",
    ]
    lines += ["    {" + ", ".join(map(str, point)) + "}," for point in positions]
    lines += ["};", f"#define SM64_MARIO_MATERIAL_COUNT {len(materials)}U", "static const uint8_t sm64_mario_material_rgb[SM64_MARIO_MATERIAL_COUNT][3] = {"]
    lines += ["    {" + ", ".join(map(str, material["rgb555"])) + "}," for material in materials]
    lines += ["};", "/* material,a,b,c,d; d repeats c for explicit triangle fallbacks. */", "static const uint16_t sm64_mario_primitives[SM64_MARIO_PRIMITIVE_COUNT][5] = {"]
    lines += ["    {%d, %d, %d, %d, %d}," % (primitive.material, *primitive.vertices) for primitive in primitives]
    lines += ["};", "/* First UV subtile for a compiled primitive, or TEXTURE_TILE_NONE. */", "static const uint16_t sm64_mario_texture_tile_start[SM64_MARIO_PRIMITIVE_COUNT] = {"]
    lines += ["    " + ", ".join(f"{value}U" for value in texture_tile_start[index:index + 12]) + "," for index in range(0, len(texture_tile_start), 12)]
    lines += ["};", "#endif"]
    args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps({
        "name": "normal_mario_neutral_turntable",
        "reuse": "direct data conversion",
        "source": {"model": {"path": str(args.model).replace("\\\\", "/"), "sha256": model_hash}, "geo": {"path": str(args.geo).replace("\\\\", "/"), "sha256": geo_hash}},
        "geo_layout": "mario_geo_body",
        "parts": [{"display_list": name, "matrix": [round(value, 6) for value in matrix]} for name, matrix, _light in parts],
        "triangle_count": len(triangles), "vertex_count": len(positions),
        "primitive_count": len(primitives), "quad_count": quad_report["quad_count"],
        "triangle_fallback_count": quad_report["standalone_triangle_count"],
        "triangle_display_lists": sorted({str(item["display_list"]) for item in triangles}),
        "textured_triangles": [
            {"source": index, "texture": triangle["texture"], "positions": triangle["positions"], "uv": triangle["uv"]}
            for index, triangle in enumerate(triangles) if index in textured_source_rank
        ],
        "textured_primitive_count": len(textured_source_ids),
        "geo_evaluator": {
            "layout": "mario_geo_body",
            "implemented": ["GEO_ANIMATED_PART", "GEO_OPEN_NODE", "GEO_CLOSE_NODE", "GEO_BRANCH", "GEO_DISPLAY_LIST", "Animation index/value rotations", "source xyz matrices"],
            "branch_selections": BRANCH_SELECTIONS,
        },
        "animation": {"path": str(args.animation).replace("\\\\", "/"), "frame": args.animation_frame} if args.animation else None,
        "limits": ["source Animation rotations are fixed to one pose", "ASM head/torso callbacks remain identity", "RGBA16 texture commands use source light colors until VDP1 texture conversion"],
        "mesh_ir": compiled_ir,
    }, indent=2) + "\n", encoding="utf-8")
    if args.mesh_ir_output is not None:
        args.mesh_ir_output.parent.mkdir(parents=True, exist_ok=True)
        args.mesh_ir_output.write_text(json.dumps(compiled_ir, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
