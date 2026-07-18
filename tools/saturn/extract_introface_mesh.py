#!/usr/bin/env python3
"""Extract the original Goddard Mario face topology for the Saturn study.

This is a source-to-source data conversion only.  It deliberately preserves
the original vertex positions, triangle order, material IDs, and source
indices from dynlist_mario_face.c; VDP1 projection and material conversion
remain in the Saturn renderer.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
from pathlib import Path

from saturn_mesh_ir import compile_mesh_ir


def goddard_deformation(source: str, vertex_count: int) -> dict[str, object]:
    """Extract Goddard's ordered joint-weight accumulation without normalizing it."""
    token_pattern = re.compile(
        r"MakeAttachedJoint\(([^)]+)\)|SetSkinWeight\((\d+),\s*([0-9.]+)\)"
    )
    joints: list[str] = []
    influences: list[list[dict[str, int]]] = [[] for _ in range(vertex_count)]
    current_joint: int | None = None
    for match in token_pattern.finditer(source):
        if match.group(1) is not None:
            joints.append(match.group(1).strip())
            current_joint = len(joints) - 1
            continue
        if current_joint is None:
            raise ValueError("SetSkinWeight appears before MakeAttachedJoint")
        vertex = int(match.group(2))
        if vertex < 0 or vertex >= vertex_count:
            raise ValueError(f"Goddard skin weight references vertex {vertex} outside the face")
        weight_q15 = round(float(match.group(3)) * 32768.0 / 100.0)
        influences[vertex].append({"joint": current_joint, "weight_q15": weight_q15})
    if not joints:
        raise ValueError("no Goddard face joints found")
    return {
        "mode": "goddard_weighted_accumulation",
        "joint_count": len(joints),
        "joints": joints,
        "influences": influences,
    }


def initializer_body(source: str, name: str, dimensions: str) -> str:
    match = re.search(
        rf"static\s+{re.escape(dimensions)}\s+{re.escape(name)}\[\]\[\d+\]\s*=\s*\{{(.*?)\}};",
        source,
        re.DOTALL,
    )
    if match is None:
        raise ValueError(f"could not locate {name}")
    return match.group(1)


def rows(body: str, count: int) -> list[tuple[int, ...]]:
    pattern = r"\{\s*" + r"\s*,\s*".join([r"(-?\d+)"] * count) + r"\s*\}"
    return [tuple(map(int, match)) for match in re.findall(pattern, body)]


def materials(source: str) -> list[tuple[int, int, int]]:
    """Convert the source material ambient RGB floats to Saturn's 5-bit RGB."""
    matches = re.findall(
        r"SetId\((\d+)\),\s*SetAmbient\(([0-9.]+),\s*([0-9.]+),\s*([0-9.]+)\)",
        source,
    )
    result: dict[int, tuple[int, int, int]] = {}
    for material, red, green, blue in matches:
        result[int(material)] = tuple(round(float(value) * 31) for value in (red, green, blue))
    if sorted(result) != list(range(8)):
        raise ValueError("expected material IDs 0 through 7")
    return [result[index] for index in range(8)]


def named_rows(source: str, name: str, count: int) -> list[tuple[int, ...]]:
    match = re.search(rf"static\s+\w+\s+{re.escape(name)}\[\]\[\d+\]\s*=\s*\{{(.*?)\}};", source, re.DOTALL)
    if match is None:
        raise ValueError(f"could not locate {name}")
    return rows(match.group(1), count)


def group_materials(source: str, group: str, expected_count: int = 4) -> list[tuple[int, int, int]]:
    match = re.search(rf"StartGroup\({re.escape(group)}\),(.*?)EndGroup", source, re.DOTALL)
    if match is None:
        raise ValueError(f"could not locate {group}")
    entries = re.findall(r"SetId\((\d+)\),\s*SetAmbient\(([0-9.]+),\s*([0-9.]+),\s*([0-9.]+)\)", match.group(1))
    result = {int(material): tuple(round(float(value) * 31) for value in (red, green, blue)) for material, red, green, blue in entries}
    if sorted(result) != list(range(expected_count)):
        raise ValueError(f"expected material IDs 0 through {expected_count - 1} in {group}")
    return [result[index] for index in range(expected_count)]


def rotate_xyz(vertex: tuple[int, int, int], rotation: tuple[float, float, float]) -> tuple[float, float, float]:
    x, y, z = vertex
    for axis, degrees in zip("xyz", rotation):
        cosine, sine = math.cos(math.radians(degrees)), math.sin(math.radians(degrees))
        if axis == "x":
            y, z = y * cosine - z * sine, y * sine + z * cosine
        elif axis == "y":
            x, z = x * cosine + z * sine, -x * sine + z * cosine
        else:
            x, y = x * cosine - y * sine, x * sine + y * cosine
    return x, y, z


def eye_world_vertices(vertices: list[tuple[int, int, int]], joint_rotation: tuple[float, float, float], net_rotation: tuple[float, float, float], net_offset: tuple[float, float, float]) -> list[tuple[int, int, int]]:
    converted = []
    for vertex in vertices:
        x, y, z = rotate_xyz(rotate_xyz(vertex, joint_rotation), net_rotation)
        converted.append((round(x + net_offset[0]), round(y + net_offset[1]), round(z + net_offset[2])))
    return converted


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--eyes-input", type=Path, required=True)
    parser.add_argument("--features-input", type=Path, required=True)
    parser.add_argument("--master-input", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--quad-report", type=Path)
    parser.add_argument("--mesh-ir-output", type=Path)
    args = parser.parse_args()
    text = args.input.read_text(encoding="utf-8")
    eyes_text = args.eyes_input.read_text(encoding="utf-8")
    features_text = args.features_input.read_text(encoding="utf-8")
    master_text = args.master_input.read_text(encoding="utf-8") if args.master_input else None
    vertices = rows(initializer_body(text, "mario_Face_VtxData", "s16"), 3)
    faces = rows(initializer_body(text, "mario_Face_FaceData", "u16"), 4)
    face_materials = materials(text)
    digest = hashlib.sha256(text.encode("utf-8")).hexdigest()
    source_ir = {
        "schema": "sm64-saturn-mesh-ir",
        "version": 1,
        "name": "mario_intro_face",
        "source": {
            "path": "src/goddard/dynlists/dynlist_mario_face.c",
            "sha256": digest,
        },
        "positions": [list(vertex) for vertex in vertices],
        "materials": [
            {"id": material_id, "rgb555": list(rgb)}
            for material_id, rgb in enumerate(face_materials)
        ],
        "triangles": [
            {"source": source, "material": material, "indices": [a, b, c]}
            for source, (material, a, b, c) in enumerate(faces)
        ],
        "vertex_attributes": {},
        "deformation": goddard_deformation(master_text, len(vertices)) if master_text else None,
        "validation_poses": [],
    }
    compiled_ir, face_primitives, quad_report = compile_mesh_ir(source_ir)
    right_eye = eye_world_vertices(named_rows(eyes_text, "verts_mario_eye_right", 3), (184.483, -178.885, 82.485), (90.0, 180.0, 0.0), (29.7, 192.4, -3.0))
    left_eye = eye_world_vertices(named_rows(eyes_text, "verts_mario_eye_left", 3), (-6.873, 0.206, -97.461), (-90.0, 0.0, 0.0), (-29.0, 192.3, -2.0))
    right_eye_faces = named_rows(eyes_text, "facedata_mario_eye_right", 4)
    left_eye_faces = named_rows(eyes_text, "facedata_mario_eye_left", 4)
    right_eye_materials = group_materials(eyes_text, "DYNOBJ_MARIO_RIGHT_EYE_MTL_GROUP")
    left_eye_materials = group_materials(eyes_text, "DYNOBJ_MARIO_LEFT_EYE_MTL_GROUP")
    right_eyebrow = named_rows(features_text, "verts_mario_eyebrow_right", 3)
    right_eyebrow_faces = named_rows(features_text, "facedata_mario_eyebrow_right", 4)
    right_eyebrow_materials = group_materials(features_text, "DYNOBJ_MARIO_RIGHT_EYEBROW_MTL_GROUP", 1)
    left_eyebrow = named_rows(features_text, "verts_mario_eyebrow_left", 3)
    left_eyebrow_faces = named_rows(features_text, "facedata_mario_eyebrow_left", 4)
    left_eyebrow_materials = group_materials(features_text, "DYNOBJ_MARIO_LEFT_EYEBROW_MTL_GROUP", 1)
    mustache = named_rows(features_text, "verts_mario_mustache", 3)
    mustache_faces = named_rows(features_text, "facedata_mario_mustache", 4)
    mustache_materials = group_materials(features_text, "DYNOBJ_MARIO_MUSTACHE_MTL_GROUP", 1)
    lines = [
        "/* Generated by tools/saturn/extract_introface_mesh.py; do not edit. */",
        "/* Source: src/goddard/dynlists/dynlist_mario_face.c",
        f" * SHA-256: {digest}",
        f" * Eye source: src/goddard/dynlists/dynlists_mario_eyes.c SHA-256: {hashlib.sha256(eyes_text.encode('utf-8')).hexdigest()}",
        f" * Feature source: src/goddard/dynlists/dynlists_mario_eyebrows_mustache.c SHA-256: {hashlib.sha256(features_text.encode('utf-8')).hexdigest()}",
        " * Reuse: direct data conversion; vertices and triangles are unchanged. */",
        "#ifndef SM64_SATURN_INTROFACE_MARIO_FACE_MESH_H",
        "#define SM64_SATURN_INTROFACE_MARIO_FACE_MESH_H",
        "",
        f"#define SM64_FACE_VERTEX_COUNT {len(vertices)}U",
        f"#define SM64_FACE_TRIANGLE_COUNT {len(faces)}U",
        f"#define SM64_FACE_QUAD_COUNT {quad_report['quad_count']}U",
        f"#define SM64_FACE_PRIMITIVE_COUNT {len(face_primitives)}U",
        "static const int16_t sm64_face_vertices[SM64_FACE_VERTEX_COUNT][3] = {",
    ]
    lines += [f"    {{{x}, {y}, {z}}}," for x, y, z in vertices]
    lines += ["};", "", "/* material, source vertex indices a/b/c */", "static const uint16_t sm64_face_triangles[SM64_FACE_TRIANGLE_COUNT][4] = {"]
    lines += [f"    {{{material}, {a}, {b}, {c}}}," for material, a, b, c in faces]
    lines += [
        "};",
        "",
        "/* Saturn render IR: material, ordered vertices a/b/c/d, source triangles.",
        " * A source1 value of 0xFFFF marks a repeated-vertex triangle fallback. */",
        "static const uint16_t sm64_face_primitives[SM64_FACE_PRIMITIVE_COUNT][7] = {",
    ]
    lines += [
        "    {%d, %d, %d, %d, %d, %d, %s},"
        % (
            primitive.material,
            *primitive.vertices,
            primitive.first_triangle,
            "0xFFFF" if primitive.second_triangle is None else str(primitive.second_triangle),
        )
        for primitive in face_primitives
    ]
    lines += ["};", "", "/* RGB555 values converted from the original SetAmbient material values. */", "static const uint8_t sm64_face_material_rgb[8][3] = {"]
    lines += [f"    {{{red}, {green}, {blue}}}," for red, green, blue in face_materials]
    lines += ["};", "", f"#define SM64_RIGHT_EYE_VERTEX_COUNT {len(right_eye)}U", f"#define SM64_RIGHT_EYE_TRIANGLE_COUNT {len(right_eye_faces)}U", f"#define SM64_LEFT_EYE_VERTEX_COUNT {len(left_eye)}U", f"#define SM64_LEFT_EYE_TRIANGLE_COUNT {len(left_eye_faces)}U", "/* World-space eye vertices: source local data transformed using the static", " * joint/net rotations and offsets in dynlist_mario_master.c (XYZ Euler). */", "static const int16_t sm64_right_eye_vertices[SM64_RIGHT_EYE_VERTEX_COUNT][3] = {"]
    lines += [f"    {{{x}, {y}, {z}}}," for x, y, z in right_eye]
    lines += ["};", "static const uint16_t sm64_right_eye_triangles[SM64_RIGHT_EYE_TRIANGLE_COUNT][4] = {"]
    lines += [f"    {{{material}, {a}, {b}, {c}}}," for material, a, b, c in right_eye_faces]
    lines += ["};", "static const uint8_t sm64_right_eye_material_rgb[4][3] = {"]
    lines += [f"    {{{red}, {green}, {blue}}}," for red, green, blue in right_eye_materials]
    lines += ["};", "static const int16_t sm64_left_eye_vertices[SM64_LEFT_EYE_VERTEX_COUNT][3] = {"]
    lines += [f"    {{{x}, {y}, {z}}}," for x, y, z in left_eye]
    lines += ["};", "static const uint16_t sm64_left_eye_triangles[SM64_LEFT_EYE_TRIANGLE_COUNT][4] = {"]
    lines += [f"    {{{material}, {a}, {b}, {c}}}," for material, a, b, c in left_eye_faces]
    lines += ["};", "static const uint8_t sm64_left_eye_material_rgb[4][3] = {"]
    lines += [f"    {{{red}, {green}, {blue}}}," for red, green, blue in left_eye_materials]
    lines += ["};", "", f"#define SM64_RIGHT_EYEBROW_VERTEX_COUNT {len(right_eyebrow)}U", f"#define SM64_RIGHT_EYEBROW_TRIANGLE_COUNT {len(right_eyebrow_faces)}U", "static const int16_t sm64_right_eyebrow_vertices[SM64_RIGHT_EYEBROW_VERTEX_COUNT][3] = {"]
    lines += [f"    {{{x}, {y}, {z}}}," for x, y, z in right_eyebrow]
    lines += ["};", "static const uint16_t sm64_right_eyebrow_triangles[SM64_RIGHT_EYEBROW_TRIANGLE_COUNT][4] = {"]
    lines += [f"    {{{material}, {a}, {b}, {c}}}," for material, a, b, c in right_eyebrow_faces]
    lines += ["};", "static const uint8_t sm64_right_eyebrow_material_rgb[1][3] = {"]
    lines += [f"    {{{red}, {green}, {blue}}}," for red, green, blue in right_eyebrow_materials]
    lines += ["};", f"#define SM64_LEFT_EYEBROW_VERTEX_COUNT {len(left_eyebrow)}U", f"#define SM64_LEFT_EYEBROW_TRIANGLE_COUNT {len(left_eyebrow_faces)}U", "static const int16_t sm64_left_eyebrow_vertices[SM64_LEFT_EYEBROW_VERTEX_COUNT][3] = {"]
    lines += [f"    {{{x}, {y}, {z}}}," for x, y, z in left_eyebrow]
    lines += ["};", "static const uint16_t sm64_left_eyebrow_triangles[SM64_LEFT_EYEBROW_TRIANGLE_COUNT][4] = {"]
    lines += [f"    {{{material}, {a}, {b}, {c}}}," for material, a, b, c in left_eyebrow_faces]
    lines += ["};", "static const uint8_t sm64_left_eyebrow_material_rgb[1][3] = {"]
    lines += [f"    {{{red}, {green}, {blue}}}," for red, green, blue in left_eyebrow_materials]
    lines += ["};", f"#define SM64_MUSTACHE_VERTEX_COUNT {len(mustache)}U", f"#define SM64_MUSTACHE_TRIANGLE_COUNT {len(mustache_faces)}U", "static const int16_t sm64_mustache_vertices[SM64_MUSTACHE_VERTEX_COUNT][3] = {"]
    lines += [f"    {{{x}, {y}, {z}}}," for x, y, z in mustache]
    lines += ["};", "static const uint16_t sm64_mustache_triangles[SM64_MUSTACHE_TRIANGLE_COUNT][4] = {"]
    lines += [f"    {{{material}, {a}, {b}, {c}}}," for material, a, b, c in mustache_faces]
    lines += ["};", "static const uint8_t sm64_mustache_material_rgb[1][3] = {"]
    lines += [f"    {{{red}, {green}, {blue}}}," for red, green, blue in mustache_materials]
    lines += ["};", "", "#endif", ""]
    args.output.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    if args.quad_report is not None:
        report = {
            "schema": 1,
            "source": "src/goddard/dynlists/dynlist_mario_face.c",
            "source_sha256": digest,
            "algorithm": "exact constrained maximum-cardinality, maximum-integer-quality matching",
            "minimum_normal_alignment": 0.80,
            "camera_samples": {"yaw_degrees": [-45, -22, 0, 22, 45], "pitch_degrees": [-30, 0, 30]},
            "prior_art": {
                "repository": "https://github.com/Rulesobeyer/Optimized-Tris-to-Quads-Converter",
                "commit": "1e1cdb1aaf55bb3e222cd8ecf7233f9065af392c",
                "license": "Apache-2.0",
                "reuse_mode": "pattern-only",
            },
            **quad_report,
        }
        args.quad_report.parent.mkdir(parents=True, exist_ok=True)
        args.quad_report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")
    if args.mesh_ir_output is not None:
        args.mesh_ir_output.parent.mkdir(parents=True, exist_ok=True)
        args.mesh_ir_output.write_text(
            json.dumps(compiled_ir, indent=2) + "\n", encoding="utf-8", newline="\n"
        )


if __name__ == "__main__":
    main()
