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
from collections import Counter
from pathlib import Path

from quad_pairing import RenderPrimitive
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

MARIO_LIGHT_BAKE_POLICY = (
    "per-frame source-pose face-normal accumulation; light=(-2,4,5), "
    "ambient=8/31, diffuse=23/31, degenerate=20/31; one uint8 intensity "
    "per shared vertex"
)


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

def scale_matrix(scale: float) -> Matrix:
    return (scale, 0.0, 0.0, 0.0, scale, 0.0, 0.0, 0.0, scale, 0.0, 0.0, 0.0)

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

def animation_streams(source: str) -> tuple[list[int], list[int]]:
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
    return values("indices"), values("values")


def animation_translation(source: str, frame: int) -> tuple[int, int, int]:
    """Read the source root x/y/z translation channels."""
    indices, samples = animation_streams(source)
    return tuple(
        samples[indices[axis * 2 + 1] + min(frame, indices[axis * 2] - 1)]
        for axis in range(3)
    )


def animation_rotations(source: str, frame: int) -> list[tuple[int, int, int]]:
    """Read a source Animation's index/value stream like retrieve_animation_index()."""
    indices, samples = animation_streams(source)
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


def animation_frame_count(source: str) -> int:
    """Return the source Animation frame count from its header."""
    header = re.search(r"struct Animation anim_\w+\[\]\s*=\s*\{(.*?)\};", source, re.DOTALL)
    if header is None:
        raise ValueError("missing source Animation header")
    fields = [field.strip() for field in header.group(1).split(",")]
    if len(fields) < 5:
        raise ValueError("incomplete source Animation header")
    return int(fields[4], 0)

def geo_layout_parts(geo_source: str, rotations: list[tuple[int, int, int]], root_translation: tuple[int, int, int] = (0, 0, 0)) -> list[tuple[str, Matrix, str]]:
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
                translation = tuple(int(fields[axis + 1]) for axis in range(3))
                if animated_part_index == 0:
                    translation = tuple(translation[axis] + root_translation[axis] for axis in range(3))
                animated_part_index += 1
                last = matrix_mul(source_matrix(translation, rotation), parent)
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

    # mario_geo is the LevelScript entry and wraps every body branch in the
    # original 0.25 GEO_SCALE. Entering mario_geo_body directly without this
    # wrapper makes the actor four times too large in SM64 world coordinates.
    wrapper = layouts.get("mario_geo", "")
    scale_match = re.search(r"GEO_SCALE\s*\([^,]+,\s*(\d+)\s*\)", wrapper)
    root = scale_matrix(int(scale_match.group(1)) / 65536.0) if scale_match else identity_matrix()
    # Root translation remains zero (object/world placement is the game
    # runtime's responsibility), while the source rotations and global scale
    # are consumed in original graph order.
    walk(0, root)
    if not result:
        raise ValueError("mario_geo_body did not produce any display lists")
    return result


def vertex_groups(source: str) -> dict[str, list[tuple[int, int, int, int, int]]]:
    result: dict[str, list[tuple[int, int, int, int, int]]] = {}
    for name, body in blocks(source, "Vtx").items():
        rows = re.findall(r"\{\{\{\s*(-?\d+),\s*(-?\d+),\s*(-?\d+)\s*\},\s*\d+,\s*\{\s*(-?\d+),\s*(-?\d+)", body)
        result[name] = [tuple(map(int, row)) for row in rows]
    return result


#: Fields in one complete Vtx row: {{{x,y,z}, flag, {u,v}, {n0,n1,n2,n3}}}.
VTX_ROW_FIELDS = 10

_VTX_ROW = re.compile(
    r"\{\{\s*\{\s*(-?\w+),\s*(-?\w+),\s*(-?\w+)\s*\}\s*,\s*(-?\w+)\s*,"
    r"\s*\{\s*(-?\w+),\s*(-?\w+)\s*\}\s*,"
    r"\s*\{\s*(-?\w+),\s*(-?\w+),\s*(-?\w+),\s*(-?\w+)\s*\}"
)


def _vtx_value(text: str) -> int:
    # The normal/colour field is written in hex, the rest in decimal. int(x, 0)
    # would reject a decimal with a leading zero as a bad octal literal.
    negative, digits = text.startswith("-"), text.lstrip("-")
    value = int(digits, 16) if digits.lower().startswith("0x") else int(digits, 10)
    return -value if negative else value


def vertex_rows(source: str) -> dict[str, list[tuple[int, ...]]]:
    """Every Vtx row in full: (x, y, z, flag, u, v, n0, n1, n2, n3).

    :func:`vertex_groups` keeps only position and uv, which is all the mesh
    exporters need. Proving two source vertices are *interchangeable* needs
    the dropped fields too: quad merging welds two rows into one vertex only
    when the whole row matches, because a differing normal shades differently
    under Gouraud even at an identical position.

    Additive on purpose -- :func:`vertex_groups` and its callers are unchanged.
    """
    return {
        name: [
            tuple(_vtx_value(value) for value in row)
            for row in _VTX_ROW.findall(body)
        ]
        for name, body in blocks(source, "Vtx").items()
    }


def ints(command: str) -> list[int]:
    return [int(value, 0) for value in re.findall(r"(?<![A-Za-z_])(?:0x[0-9A-Fa-f]+|\d+)", command)]


def flatten(display_lists: dict[str, str], vertices: dict[str, list[tuple[int, int, int, int, int]]],
            name: str, matrix: Matrix, light: str,
            out: list[dict[str, object]], texture: str | None = None,
            combine_mode: str | None = None,
            cull_back: bool = True,
            stack: tuple[str, ...] = ()) -> tuple[str, str | None, str | None, bool]:
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
    current_combine_mode = combine_mode
    current_cull_back = cull_back
    for macro, args in re.findall(r"(gs\w+)\(([^;]*?)\)", body, re.DOTALL):
        if macro == "gsSPLight":
            found = re.search(r"&(mario_\w+_lights_group)\.", args)
            if found:
                current_light = found.group(1)
        elif macro == "gsDPSetTextureImage":
            found = re.search(r"\b(mario_texture_\w+)\b", args)
            if found:
                current_texture = found.group(1)
        elif macro == "gsDPSetCombineMode":
            current_combine_mode = args.split(",", 1)[0].strip()
        elif macro == "gsSPSetGeometryMode" and "G_CULL_BACK" in args:
            current_cull_back = True
        elif macro == "gsSPClearGeometryMode" and "G_CULL_BACK" in args:
            current_cull_back = False
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
                            "display_list": name, "texture": current_texture,
                            "combine_mode": current_combine_mode,
                            "cull_back": current_cull_back})
        elif macro == "gsSPDisplayList":
            child = re.match(r"\s*(\w+)", args)
            if child:
                (current_light, current_texture, current_combine_mode,
                 current_cull_back) = flatten(
                    display_lists, vertices, child.group(1), matrix, current_light,
                    out, current_texture, current_combine_mode,
                    current_cull_back, stack + (name,)
                )
    return current_light, current_texture, current_combine_mode, current_cull_back


def flatten_parts(
    display_lists: dict[str, str],
    vertices: dict[str, list[tuple[int, int, int, int, int]]],
    parts: list[tuple[str, Matrix, str]],
    out: list[dict[str, object]],
) -> tuple[str, str | None, str | None, bool]:
    """Flatten GeoLayout display lists with one persistent Fast3D state.

    SM64 intentionally lets lights, textures, combine modes, and geometry
    modes established by one sibling display list feed the following sibling.
    Resetting every animated part to blue changed Mario's red forearms and the
    red backing beneath his cap logo.  The game globally enables G_CULL_BACK;
    selected display lists may explicitly clear or restore it.
    """
    if not parts:
        raise ValueError("Mario GeoLayout contains no render parts")
    current_light = parts[0][2]
    current_texture: str | None = None
    current_combine_mode: str | None = None
    current_cull_back = True  # src/game/game_init.c enables G_CULL_BACK.
    for name, matrix, _initial_light in parts:
        (current_light, current_texture, current_combine_mode,
         current_cull_back) = flatten(
            display_lists, vertices, name, matrix, current_light, out,
            current_texture, current_combine_mode, current_cull_back,
        )
    return current_light, current_texture, current_combine_mode, current_cull_back


def mario_vertex_light_intensities(
    pose_vertices: list[list[int]], primitives: list[RenderPrimitive]
) -> list[int]:
    """Mirror castleviewer's build_mario_gouraud() intensity pass exactly.

    The target accumulates the normal made by primitive corners a/b/c into
    each unique primitive corner.  Explicit triangle fallbacks repeat c in d,
    so that repeated fourth corner must not receive the normal twice.  The
    resulting 0..31 intensity is material-independent and can therefore be
    stored once per shared vertex instead of once per VDP1 Gouraud corner.
    """
    normals = [[0, 0, 0] for _ in pose_vertices]
    for primitive in primitives:
        indices = primitive.vertices
        if any(index < 0 or index >= len(pose_vertices) for index in indices):
            raise ValueError("Mario lighting primitive references an invalid vertex")
        a, b, c = (pose_vertices[index] for index in indices[:3])
        ux, uy, uz = (b[axis] - a[axis] for axis in range(3))
        vx, vy, vz = (c[axis] - a[axis] for axis in range(3))
        nx = uy * vz - uz * vy
        ny = uz * vx - ux * vz
        nz = ux * vy - uy * vx
        for corner, vertex in enumerate(indices):
            if corner == 3 and vertex == indices[2]:
                continue
            normals[vertex][0] += nx
            normals[vertex][1] += ny
            normals[vertex][2] += nz

    intensities: list[int] = []
    for normal_x, normal_y, normal_z in normals:
        maximum = 6 * (abs(normal_x) + abs(normal_y) + abs(normal_z))
        dot = (-2 * normal_x) + (4 * normal_y) + (5 * normal_z)
        intensity = 20 if maximum == 0 else 8 + (max(dot, 0) * 23) // maximum
        if not 0 <= intensity <= 31:
            raise ValueError(f"Mario light intensity {intensity} exceeds RGB555 range")
        intensities.append(intensity)
    return intensities


def c_u8_light_bank(
    symbol: str, frame_count_macro: str, frames: list[list[int]]
) -> list[str]:
    """Format a compact frame/vertex uint8_t bank for the generated header."""
    if not frames:
        return []
    width = len(frames[0])
    if width == 0 or any(len(frame) != width for frame in frames):
        raise ValueError("Mario light intensity frames must be non-empty and rectangular")
    if any(not 0 <= value <= 31 for frame in frames for value in frame):
        raise ValueError("Mario light intensity bank exceeds RGB555 range")
    lines = [
        f"static const uint8_t {symbol}[{frame_count_macro}][SM64_MARIO_VERTEX_COUNT] = {{"
    ]
    for frame in frames:
        lines.append("    {")
        lines += [
            "        " + ", ".join(map(str, frame[offset:offset + 24])) + ","
            for offset in range(0, len(frame), 24)
        ]
        lines.append("    },")
    lines.append("};")
    return lines


def mario_render_clusters(
    triangles: list[dict[str, object]],
    compiled_primitives: list[dict[str, object]],
    positions: list[list[int]],
) -> dict[str, object]:
    """Group actor work by the leaf display list that emitted each triangle.

    A GeoLayout part can call several nested Fast3D lists.  ``flatten`` records
    the list that contains the triangle command, which is the smallest stable
    source unit we can classify against a scene BSP.  A paired VDP1 quad must
    never span two of those units: doing so would make its visibility and
    painter ownership ambiguous.

    The flattened index streams deliberately retain primitive order and first
    vertex use inside each (lexicographically ordered) cluster.  This gives the
    target compact caller-owned work lists without coupling Mario to any one
    level or BSP implementation.
    """
    source_leaf = {
        source: str(triangle["display_list"])
        for source, triangle in enumerate(triangles)
    }
    grouped: dict[str, list[int]] = {}
    for primitive_index, primitive in enumerate(compiled_primitives):
        sources = primitive.get("source_triangles")
        if not isinstance(sources, list) or not sources:
            raise ValueError(
                f"Mario primitive {primitive_index} has no source triangles"
            )
        try:
            leaves = {source_leaf[int(source)] for source in sources}
        except (KeyError, TypeError, ValueError) as error:
            raise ValueError(
                f"Mario primitive {primitive_index} references an unknown source triangle"
            ) from error
        if len(leaves) != 1:
            raise ValueError(
                "paired Mario source triangles cross render clusters: "
                f"primitive {primitive_index}, sources {sources}, leaves {sorted(leaves)}"
            )
        grouped.setdefault(next(iter(leaves)), []).append(primitive_index)

    primitive_offsets = [0]
    primitive_indices: list[int] = []
    vertex_offsets = [0]
    vertex_indices: list[int] = []
    clusters: list[dict[str, object]] = []
    for cluster_id, name in enumerate(sorted(grouped)):
        cluster_primitives = grouped[name]
        primitive_indices.extend(cluster_primitives)
        primitive_offsets.append(len(primitive_indices))

        seen_vertices: set[int] = set()
        cluster_vertices: list[int] = []
        for primitive_index in cluster_primitives:
            indices = compiled_primitives[primitive_index].get("indices")
            if not isinstance(indices, list) or len(indices) != 4:
                raise ValueError(
                    f"Mario primitive {primitive_index} must contain four target indices"
                )
            for vertex in indices:
                if (
                    isinstance(vertex, bool)
                    or not isinstance(vertex, int)
                    or vertex < 0
                    or vertex >= len(positions)
                ):
                    raise ValueError(
                        f"Mario primitive {primitive_index} references an invalid vertex"
                    )
                if vertex not in seen_vertices:
                    seen_vertices.add(vertex)
                    cluster_vertices.append(vertex)
        if not cluster_vertices:
            raise ValueError(f"Mario render cluster {name} contains no vertices")
        vertex_indices.extend(cluster_vertices)
        vertex_offsets.append(len(vertex_indices))
        minimum = [min(positions[index][axis] for index in cluster_vertices) for axis in range(3)]
        maximum = [max(positions[index][axis] for index in cluster_vertices) for axis in range(3)]
        clusters.append({
            "id": cluster_id,
            "name": name,
            "primitive_offset": primitive_offsets[-2],
            "primitive_count": len(cluster_primitives),
            "unique_vertex_offset": vertex_offsets[-2],
            "unique_vertex_count": len(cluster_vertices),
            "bounds": {"min": minimum, "max": maximum},
        })

    if len(primitive_indices) != len(compiled_primitives):
        raise ValueError("Mario render clusters do not cover every compiled primitive")
    if len(primitive_indices) > 0xFFFF or len(vertex_indices) > 0xFFFF:
        raise ValueError("Mario render-cluster index stream exceeds uint16_t")
    # The actor currently has no authored proxy mesh. Near and mid preserve
    # every cluster reference; far deterministically keeps terrain's source
    # primitive subset. This is a compact pre-transform stream, not another
    # pose bank, so the target retains one canonical actor position set.
    lod_vertex_indices: list[int] = []
    lod_vertex_offsets = [0]
    for tier in range(3):
        seen_vertices: set[int] = set()
        for primitive_index in primitive_indices:
            if tier == 2 and primitive_index % 8 == 0:
                continue
            for vertex in compiled_primitives[primitive_index]["indices"]:
                if vertex not in seen_vertices:
                    seen_vertices.add(vertex)
                    lod_vertex_indices.append(vertex)
        lod_vertex_offsets.append(len(lod_vertex_indices))
    if len(lod_vertex_indices) > 0xFFFF:
        raise ValueError("Mario render-cluster LOD vertex stream exceeds uint16_t")
    return {
        "policy": "originating leaf Fast3D display list",
        "bounds_space": (
            "neutral extracted actor pose; animated runtimes rebuild each AABB "
            "from its unique-vertex list"
        ),
        "count": len(clusters),
        "primitive_index_count": len(primitive_indices),
        "unique_vertex_reference_count": len(vertex_indices),
        "primitive_offsets": primitive_offsets,
        "primitive_indices": primitive_indices,
        "unique_vertex_offsets": vertex_offsets,
        "unique_vertex_indices": vertex_indices,
        "lod_unique_vertex_offsets": lod_vertex_offsets,
        "lod_unique_vertex_indices": lod_vertex_indices,
        "clusters": clusters,
    }


def c_render_cluster_metadata(metadata: dict[str, object]) -> list[str]:
    """Format scene-neutral, compact actor cluster tables for the target."""
    count = int(metadata["count"])
    primitive_indices = list(metadata["primitive_indices"])
    vertex_indices = list(metadata["unique_vertex_indices"])
    primitive_offsets = list(metadata["primitive_offsets"])
    vertex_offsets = list(metadata["unique_vertex_offsets"])
    lod_vertex_offsets = list(metadata["lod_unique_vertex_offsets"])
    lod_vertex_indices = list(metadata["lod_unique_vertex_indices"])
    clusters = list(metadata["clusters"])
    if len(primitive_offsets) != count + 1 or len(vertex_offsets) != count + 1:
        raise ValueError("Mario render-cluster offsets must contain count + 1 entries")

    def rows(values: list[int], width: int = 16) -> list[str]:
        return [
            "    " + ", ".join(f"{value}U" for value in values[offset:offset + width]) + ","
            for offset in range(0, len(values), width)
        ]

    lines = [
        "/* Leaf-display-list render clusters; source names remain in the JSON report. */",
        f"#define SM64_MARIO_RENDER_CLUSTER_COUNT {count}U",
        f"#define SM64_MARIO_RENDER_CLUSTER_PRIMITIVE_LIST_COUNT {len(primitive_indices)}U",
        f"#define SM64_MARIO_RENDER_CLUSTER_VERTEX_LIST_COUNT {len(vertex_indices)}U",
        "#define SM64_MARIO_RENDER_CLUSTER_LOD_TIER_COUNT 3U",
        f"#define SM64_MARIO_RENDER_CLUSTER_LOD_VERTEX_LIST_COUNT {len(lod_vertex_indices)}U",
        "static const uint16_t sm64_mario_render_cluster_primitive_offsets[SM64_MARIO_RENDER_CLUSTER_COUNT + 1U] = {",
    ]
    lines += rows(primitive_offsets)
    lines += [
        "};",
        "static const uint16_t sm64_mario_render_cluster_primitive_list[SM64_MARIO_RENDER_CLUSTER_PRIMITIVE_LIST_COUNT] = {",
    ]
    lines += rows(primitive_indices)
    lines += [
        "};",
        "static const uint16_t sm64_mario_render_cluster_vertex_offsets[SM64_MARIO_RENDER_CLUSTER_COUNT + 1U] = {",
    ]
    lines += rows(vertex_offsets)
    lines += [
        "};",
        "static const uint16_t sm64_mario_render_cluster_vertex_list[SM64_MARIO_RENDER_CLUSTER_VERTEX_LIST_COUNT] = {",
    ]
    lines += rows(vertex_indices)
    lines += [
        "};",
        "/* Exact per-tier actor position references; near/mid retain full detail. */",
        "static const uint16_t sm64_mario_render_cluster_lod_vertex_offsets[SM64_MARIO_RENDER_CLUSTER_LOD_TIER_COUNT + 1U] = {",
    ]
    lines += rows(lod_vertex_offsets)
    lines += [
        "};",
        "static const uint16_t sm64_mario_render_cluster_lod_vertex_list[SM64_MARIO_RENDER_CLUSTER_LOD_VERTEX_LIST_COUNT] = {",
    ]
    lines += rows(lod_vertex_indices)
    lines += [
        "};",
        "/* Neutral-pose AABBs. Rebuild animated bounds from each cluster vertex list. */",
        "static const int16_t sm64_mario_render_cluster_bounds[SM64_MARIO_RENDER_CLUSTER_COUNT][2][3] = {",
    ]
    for cluster in clusters:
        bounds = cluster["bounds"]
        lines.append(
            "    {{%s}, {%s}},"
            % (", ".join(map(str, bounds["min"])), ", ".join(map(str, bounds["max"])))
        )
    lines.append("};")
    return lines


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--geo", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--mesh-ir-output", type=Path)
    parser.add_argument("--animation", type=Path, help="source Mario Animation table for the fixed preview pose")
    parser.add_argument("--walking-animation", type=Path,
                        help="second source Animation table selected while the actor has movement intent")
    parser.add_argument("--animation-frame", type=int, default=0)
    args = parser.parse_args()
    model = args.model.read_text(encoding="utf-8")
    geo = args.geo.read_text(encoding="utf-8")
    display_lists, vertices = blocks(model, "Gfx"), vertex_groups(model)
    triangles: list[dict[str, object]] = []
    animation_source = args.animation.read_text(encoding="utf-8") if args.animation else None
    walking_animation_source = (args.walking_animation.read_text(encoding="utf-8")
                                if args.walking_animation else None)
    rotations = animation_rotations(animation_source, args.animation_frame) if animation_source else []
    parts = geo_layout_parts(geo, rotations, animation_translation(animation_source, args.animation_frame) if animation_source else (0, 0, 0))
    flatten_parts(display_lists, vertices, parts, triangles)
    textured_combine_modes = {
        str(triangle["combine_mode"])
        for triangle in triangles
        if triangle["texture"] is not None
    }
    if textured_combine_modes != {"G_CC_BLENDRGBFADEA"}:
        raise ValueError(
            "unsupported Mario textured combine modes: "
            f"{sorted(textured_combine_modes)}"
        )
    model_hash, geo_hash = hashlib.sha256(model.encode()).hexdigest(), hashlib.sha256(geo.encode()).hexdigest()
    # Build the same versioned interchange document used by the intro face.
    # Position sharing is preserved only when the complete source-space point
    # agrees; material boundaries remain explicit primitive boundaries.
    positions: list[list[int]] = []
    position_index: dict[tuple[int, int, int], int] = {}
    materials: list[dict[str, object]] = []
    material_index: dict[
        tuple[tuple[int, int, int], str | None, str | None], int
    ] = {}
    ir_triangles: list[dict[str, object]] = []
    for source, triangle in enumerate(triangles):
        rgb = tuple(triangle["rgb"])
        texture = triangle["texture"]
        combine_mode = triangle["combine_mode"]
        material_key = (
            rgb,
            texture if isinstance(texture, str) else None,
            combine_mode if isinstance(combine_mode, str) else None,
        )
        if material_key not in material_index:
            material_index[material_key] = len(materials)
            materials.append({
                "id": material_index[material_key],
                "rgb555": list(rgb),
                "texture": material_key[1],
                "combine_mode": material_key[2],
            })
        indices: list[int] = []
        for point in triangle["positions"]:
            key = tuple(point)
            if key not in position_index:
                position_index[key] = len(positions)
                positions.append(list(key))
            indices.append(position_index[key])
        ir_triangles.append({"source": source, "material": material_index[material_key], "indices": indices})

    # Evaluate every frame through the original Animation index/value stream
    # and GeoLayout hierarchy. This compact vertex pose bank is a transitional
    # Saturn representation of source animation data, not a replacement
    # animation or action system.
    references: dict[tuple[int, int, int], list[tuple[int, int]]] = {}
    for triangle_index, triangle in enumerate(triangles):
        for corner, point in enumerate(triangle["positions"]):
            references.setdefault(tuple(point), []).append((triangle_index, corner))

    def evaluate_animation(source: str | None) -> list[list[list[int]]]:
        if source is None:
            return []
        output: list[list[list[int]]] = []
        for frame in range(animation_frame_count(source)):
            frame_triangles: list[dict[str, object]] = []
            frame_parts = geo_layout_parts(geo, animation_rotations(source, frame),
                                            animation_translation(source, frame))
            flatten_parts(display_lists, vertices, frame_parts, frame_triangles)
            if len(frame_triangles) != len(triangles):
                raise ValueError(f"animation frame {frame} changed source triangle count")
            if any(
                (current["display_list"], current["texture"],
                 current["combine_mode"], current["cull_back"])
                != (neutral["display_list"], neutral["texture"],
                    neutral["combine_mode"], neutral["cull_back"])
                for current, neutral in zip(frame_triangles, triangles)
            ):
                raise ValueError(f"animation frame {frame} changed source topology/material state")
            frame_vertices: list[list[int]] = []
            for point in positions:
                values = {
                    tuple(frame_triangles[triangle_index]["positions"][corner])
                    for triangle_index, corner in references[tuple(point)]
                }
                if len(values) != 1:
                    raise ValueError(
                        f"animation frame {frame} splits a neutral shared vertex at {point}"
                    )
                frame_vertices.append(list(values.pop()))
            output.append(frame_vertices)
        return output

    animation_positions = evaluate_animation(animation_source)
    walking_animation_positions = evaluate_animation(walking_animation_source)
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
    primitive_cull_back: list[bool] = []
    for primitive_index, primitive in enumerate(compiled_ir["primitives"]):
        states = {
            bool(triangles[int(source)]["cull_back"])
            for source in primitive["source_triangles"]
        }
        if len(states) != 1:
            raise ValueError(
                "paired Mario triangles cross G_CULL_BACK state: "
                f"primitive {primitive_index}"
            )
        primitive_cull_back.append(states.pop())
    render_clusters = mario_render_clusters(
        triangles, compiled_ir["primitives"], positions
    )
    compiled_ir["render_clusters"] = render_clusters
    animation_light_intensities = [
        mario_vertex_light_intensities(frame, primitives)
        for frame in animation_positions
    ]
    walking_animation_light_intensities = [
        mario_vertex_light_intensities(frame, primitives)
        for frame in walking_animation_positions
    ]
    textured_source_ids = [index for index, triangle in enumerate(triangles) if triangle["texture"] is not None]
    textured_source_rank = {source: rank for rank, source in enumerate(textured_source_ids)}
    texture_tile_start = [0xFFFF] * len(primitives)
    for primitive_index, primitive in enumerate(compiled_ir["primitives"]):
        sources = primitive["source_triangles"]
        if any(source in textured_source_rank for source in sources):
            if len(sources) != 1 or sources[0] not in textured_source_rank:
                raise ValueError("textured source triangles must remain individual VDP1 primitives")
            texture_tile_start[primitive_index] = textured_source_rank[sources[0]] * 4
    textured_source_vertices = [
        [position_index[tuple(point)] for point in triangles[source]["positions"]]
        for source in textured_source_ids
    ]
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
    lines.append("};")
    if animation_positions:
        lines += [
            f"#define SM64_MARIO_ANIMATION_FRAME_COUNT {len(animation_positions)}U",
            "static const int16_t sm64_mario_animation_vertices[SM64_MARIO_ANIMATION_FRAME_COUNT][SM64_MARIO_VERTEX_COUNT][3] = {",
        ]
        for frame_vertices in animation_positions:
            lines.append("    {")
            lines += ["        {" + ", ".join(map(str, point)) + "}," for point in frame_vertices]
            lines.append("    },")
        lines.append("};")
    if walking_animation_positions:
        lines += [
            f"#define SM64_MARIO_WALKING_ANIMATION_FRAME_COUNT {len(walking_animation_positions)}U",
            "static const int16_t sm64_mario_walking_animation_vertices[SM64_MARIO_WALKING_ANIMATION_FRAME_COUNT][SM64_MARIO_VERTEX_COUNT][3] = {",
        ]
        for frame_vertices in walking_animation_positions:
            lines.append("    {")
            lines += ["        {" + ", ".join(map(str, point)) + "}," for point in frame_vertices]
            lines.append("    },")
        lines.append("};")
    if animation_light_intensities:
        lines.append(
            "/* Offline equivalent of build_mario_gouraud(): one 0..31 intensity per shared vertex. */"
        )
        lines += c_u8_light_bank(
            "sm64_mario_animation_light_intensity",
            "SM64_MARIO_ANIMATION_FRAME_COUNT",
            animation_light_intensities,
        )
    if walking_animation_light_intensities:
        lines.append(
            "/* Walking-pose vertex light intensities use the identical source-pose policy. */"
        )
        lines += c_u8_light_bank(
            "sm64_mario_walking_animation_light_intensity",
            "SM64_MARIO_WALKING_ANIMATION_FRAME_COUNT",
            walking_animation_light_intensities,
        )
    lines += [f"#define SM64_MARIO_MATERIAL_COUNT {len(materials)}U", "static const uint8_t sm64_mario_material_rgb[SM64_MARIO_MATERIAL_COUNT][3] = {"]
    lines += ["    {" + ", ".join(map(str, material["rgb555"])) + "}," for material in materials]
    lines += ["};", "/* material,a,b,c,d; d repeats c for explicit triangle fallbacks. */", "static const uint16_t sm64_mario_primitives[SM64_MARIO_PRIMITIVE_COUNT][5] = {"]
    lines += ["    {%d, %d, %d, %d, %d}," % (primitive.material, *primitive.vertices) for primitive in primitives]
    lines += [
        "};",
        "/* Source Fast3D geometry mode after persistent display-list state. */",
        "static const uint8_t sm64_mario_primitive_cull_back[SM64_MARIO_PRIMITIVE_COUNT] = {",
    ]
    lines += [
        "    " + ", ".join(
            "1" if value else "0"
            for value in primitive_cull_back[index:index + 24]
        ) + ","
        for index in range(0, len(primitive_cull_back), 24)
    ]
    lines.append("};")
    lines += c_render_cluster_metadata(render_clusters)
    lines += ["/* First UV subtile for a compiled primitive, or TEXTURE_TILE_NONE. */", "static const uint16_t sm64_mario_texture_tile_start[SM64_MARIO_PRIMITIVE_COUNT] = {"]
    lines += ["    " + ", ".join(f"{value}U" for value in texture_tile_start[index:index + 12]) + "," for index in range(0, len(texture_tile_start), 12)]
    lines += [
        "};",
        "/* Stable source vertex indices for each textured Fast3D triangle. */",
        "static const uint16_t sm64_mario_textured_source_vertices[SM64_MARIO_TEXTURED_SOURCE_TRIANGLE_COUNT][3] = {",
    ]
    lines += ["    {" + ", ".join(map(str, indices)) + "}," for indices in textured_source_vertices]
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
        "fast3d_state_scope": "persistent across source-ordered GeoLayout display lists",
        "cull_back_primitive_count": sum(primitive_cull_back),
        "render_clusters": render_clusters,
        "triangle_display_lists": sorted({str(item["display_list"]) for item in triangles}),
        "textured_combine_modes": dict(Counter(
            str(item["combine_mode"])
            for item in triangles if item["texture"] is not None
        )),
        "textured_triangles": [
            {"source": index, "texture": triangle["texture"],
             "rgb": triangle["rgb"], "combine_mode": triangle["combine_mode"],
             "cull_back": triangle["cull_back"],
             "positions": triangle["positions"],
             "uv": triangle["uv"]}
            for index, triangle in enumerate(triangles) if index in textured_source_rank
        ],
        "textured_primitive_count": len(textured_source_ids),
        "geo_evaluator": {
            "layout": "mario_geo_body",
            "implemented": ["GEO_ANIMATED_PART", "GEO_OPEN_NODE", "GEO_CLOSE_NODE", "GEO_BRANCH", "GEO_DISPLAY_LIST", "mario_geo global GEO_SCALE", "Animation index/value rotations", "source xyz matrices"],
            "branch_selections": BRANCH_SELECTIONS,
        },
        "animation": {"path": str(args.animation).replace("\\\\", "/"), "preview_frame": args.animation_frame, "frame_count": len(animation_positions), "vertex_pose_bytes": len(animation_positions) * len(positions) * 3 * 2, "vertex_light_intensity_bytes": sum(map(len, animation_light_intensities))} if args.animation else None,
        "walking_animation": {"path": str(args.walking_animation).replace("\\\\", "/"), "frame_count": len(walking_animation_positions), "vertex_pose_bytes": len(walking_animation_positions) * len(positions) * 3 * 2, "vertex_light_intensity_bytes": sum(map(len, walking_animation_light_intensities))} if args.walking_animation else None,
        "offline_vertex_lighting": {
            "policy": MARIO_LIGHT_BAKE_POLICY,
            "runtime_reference": "src/port/saturn/castleviewer/main.c:build_mario_gouraud",
            "encoding": "uint8 intensity in RGB555 range 0..31 per shared vertex per frame",
            "idle": {
                "frame_count": len(animation_light_intensities),
                "vertex_count": len(positions),
                "bytes": sum(map(len, animation_light_intensities)),
            },
            "walking": {
                "frame_count": len(walking_animation_light_intensities),
                "vertex_count": len(positions),
                "bytes": sum(map(len, walking_animation_light_intensities)),
            },
            "total_bytes": (
                sum(map(len, animation_light_intensities))
                + sum(map(len, walking_animation_light_intensities))
            ),
        },
        "limits": ["ASM head/torso callbacks remain identity", "Animation poses are pre-evaluated offline until the full runtime GeoLayout evaluator is linked"],
        "mesh_ir": compiled_ir,
    }, indent=2) + "\n", encoding="utf-8")
    if args.mesh_ir_output is not None:
        args.mesh_ir_output.parent.mkdir(parents=True, exist_ok=True)
        args.mesh_ir_output.write_text(json.dumps(compiled_ir, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
