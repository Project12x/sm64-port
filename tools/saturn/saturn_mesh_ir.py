#!/usr/bin/env python3
"""Validate and compile the versioned SM64-to-Saturn mesh interchange format.

The source IR is intentionally boring JSON. It keeps source triangle identity,
independent vertex streams, optional variable-influence deformation data, and offline
deformation samples. The compiled form replaces safe triangle pairs with VDP1
quadrilaterals while preserving an explicit repeated-vertex fallback for every
unpaired source triangle.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from quad_pairing import RenderPrimitive, pair_triangles


SOURCE_SCHEMA = "sm64-saturn-mesh-ir"
COMPILED_SCHEMA = "sm64-saturn-compiled-mesh"
SCHEMA_VERSION = 1
SUPPORTED_SCHEMA_VERSIONS = (1, 2)


def _integer_vector(value: Any, length: int, label: str) -> list[int]:
    if (
        not isinstance(value, list)
        or len(value) != length
        or any(isinstance(component, bool) or not isinstance(component, int) for component in value)
    ):
        raise ValueError(f"{label} must contain exactly {length} integers")
    return value


def validate_mesh_ir(document: dict[str, Any]) -> None:
    """Reject malformed or target-incompatible source IR documents."""
    if document.get("schema") != SOURCE_SCHEMA or document.get("version") not in SUPPORTED_SCHEMA_VERSIONS:
        raise ValueError(
            f"expected {SOURCE_SCHEMA!r} schema version one of {SUPPORTED_SCHEMA_VERSIONS}"
        )
    version = int(document["version"])
    if version >= 2 and document.get("static_world_space") is not True:
        raise ValueError("version 2 meshes must declare static_world_space=true")
    if not isinstance(document.get("name"), str) or not document["name"]:
        raise ValueError("name must be a non-empty string")

    positions = document.get("positions")
    if not isinstance(positions, list) or not positions:
        raise ValueError("positions must be a non-empty list")
    for index, position in enumerate(positions):
        vector = _integer_vector(position, 3, f"positions[{index}]")
        if any(component < -32768 or component > 32767 for component in vector):
            raise ValueError(f"positions[{index}] exceeds signed 16-bit target range")

    materials = document.get("materials")
    if not isinstance(materials, list) or not materials:
        raise ValueError("materials must be a non-empty list")
    material_ids: set[int] = set()
    for index, material in enumerate(materials):
        if not isinstance(material, dict):
            raise ValueError(f"materials[{index}] must be an object")
        material_id = material.get("id")
        if isinstance(material_id, bool) or not isinstance(material_id, int) or material_id < 0:
            raise ValueError(f"materials[{index}].id must be a non-negative integer")
        if material_id in material_ids:
            raise ValueError(f"duplicate material id {material_id}")
        material_ids.add(material_id)
        rgb = _integer_vector(material.get("rgb555"), 3, f"materials[{index}].rgb555")
        if any(component < 0 or component > 31 for component in rgb):
            raise ValueError(f"materials[{index}].rgb555 component exceeds 0..31")

    triangles = document.get("triangles")
    if not isinstance(triangles, list) or not triangles:
        raise ValueError("triangles must be a non-empty list")
    source_ids: set[int] = set()
    for index, triangle in enumerate(triangles):
        if not isinstance(triangle, dict):
            raise ValueError(f"triangles[{index}] must be an object")
        indices = _integer_vector(triangle.get("indices"), 3, f"triangles[{index}].indices")
        if len(set(indices)) != 3 or any(vertex < 0 or vertex >= len(positions) for vertex in indices):
            raise ValueError(f"triangles[{index}] has degenerate or out-of-range indices")
        if triangle.get("material") not in material_ids:
            raise ValueError(f"triangles[{index}] references an unknown material")
        if "texture_tile" in triangle:
            if version < 2:
                raise ValueError("texture_tile is only supported by Mesh IR v2")
            texture_tile = triangle["texture_tile"]
            if not isinstance(texture_tile, dict):
                raise ValueError(f"triangles[{index}].texture_tile must be an object")
            texture = texture_tile.get("texture")
            if texture is not None and (not isinstance(texture, str) or not texture):
                raise ValueError(f"triangles[{index}].texture_tile.texture must be a string or null")
            if not isinstance(texture_tile.get("state"), dict):
                raise ValueError(f"triangles[{index}].texture_tile.state must be an object")
        source_id = triangle.get("source")
        if isinstance(source_id, bool) or not isinstance(source_id, int) or source_id < 0:
            raise ValueError(f"triangles[{index}].source must be a non-negative integer")
        if source_id in source_ids:
            raise ValueError(f"duplicate source triangle id {source_id}")
        source_ids.add(source_id)

    forbidden = document.get("pairing_forbidden_triangles", [])
    if not isinstance(forbidden, list) or any(
        isinstance(index, bool) or not isinstance(index, int) or index < 0 or index >= len(triangles)
        for index in forbidden
    ):
        raise ValueError("pairing_forbidden_triangles must contain source triangle indexes")

    attributes = document.get("vertex_attributes", {})
    if not isinstance(attributes, dict):
        raise ValueError("vertex_attributes must be an object")
    if "uv" in attributes:
        if not isinstance(attributes["uv"], list) or len(attributes["uv"]) != len(positions):
            raise ValueError("vertex_attributes.uv length must match positions")
        for index, uv in enumerate(attributes["uv"]):
            _integer_vector(uv, 2, f"vertex_attributes.uv[{index}]")

    deformation = document.get("deformation")
    if deformation is not None:
        if not isinstance(deformation, dict):
            raise ValueError("deformation must be an object")
        mode = deformation.get("mode")
        if mode not in ("linear_blend", "goddard_weighted_accumulation"):
            raise ValueError("deformation.mode is not supported")
        joint_count = deformation.get("joint_count")
        if isinstance(joint_count, bool) or not isinstance(joint_count, int) or joint_count <= 0:
            raise ValueError("deformation.joint_count must be positive")
        joint_names = deformation.get("joints")
        if joint_names is not None and (
            not isinstance(joint_names, list)
            or len(joint_names) != joint_count
            or any(not isinstance(name, str) or not name for name in joint_names)
        ):
            raise ValueError("deformation.joints must name every joint")
        influences = deformation.get("influences")
        if not isinstance(influences, list) or len(influences) != len(positions):
            raise ValueError("deformation.influences length must match positions")
        for vertex_index, vertex_influences in enumerate(influences):
            if not isinstance(vertex_influences, list):
                raise ValueError(f"deformation.influences[{vertex_index}] must be a list")
            seen_joints: set[int] = set()
            total_weight = 0
            for influence_index, influence in enumerate(vertex_influences):
                if not isinstance(influence, dict):
                    raise ValueError(
                        f"deformation.influences[{vertex_index}][{influence_index}] must be an object"
                    )
                joint = influence.get("joint")
                weight_q15 = influence.get("weight_q15")
                if isinstance(joint, bool) or not isinstance(joint, int) or not 0 <= joint < joint_count:
                    raise ValueError(
                        f"deformation.influences[{vertex_index}][{influence_index}] references an unknown joint"
                    )
                if joint in seen_joints:
                    raise ValueError(f"deformation.influences[{vertex_index}] repeats joint {joint}")
                seen_joints.add(joint)
                if (
                    isinstance(weight_q15, bool)
                    or not isinstance(weight_q15, int)
                    or not 0 <= weight_q15 <= 32768
                ):
                    raise ValueError(
                        f"deformation.influences[{vertex_index}][{influence_index}].weight_q15 exceeds 0..32768"
                    )
                total_weight += weight_q15
            if mode == "linear_blend" and total_weight != 32768:
                raise ValueError(
                    f"linear_blend influences for vertex {vertex_index} must sum to 32768"
                )

    poses = document.get("validation_poses", [])
    if not isinstance(poses, list):
        raise ValueError("validation_poses must be a list")
    pose_names: set[str] = set()
    for pose_index, pose in enumerate(poses):
        if not isinstance(pose, dict) or not isinstance(pose.get("name"), str) or not pose["name"]:
            raise ValueError(f"validation_poses[{pose_index}].name must be a non-empty string")
        if pose["name"] in pose_names:
            raise ValueError(f"duplicate validation pose name {pose['name']!r}")
        pose_names.add(pose["name"])
        pose_positions = pose.get("positions")
        if not isinstance(pose_positions, list) or len(pose_positions) != len(positions):
            raise ValueError(f"validation_poses[{pose_index}].positions length must match positions")
        for vertex_index, position in enumerate(pose_positions):
            vector = _integer_vector(
                position, 3, f"validation_poses[{pose_index}].positions[{vertex_index}]"
            )
            if any(component < -32768 or component > 32767 for component in vector):
                raise ValueError(
                    f"validation_poses[{pose_index}].positions[{vertex_index}] exceeds signed 16-bit target range"
                )


def compile_mesh_ir(
    document: dict[str, Any]
) -> tuple[dict[str, Any], list[RenderPrimitive], dict[str, object]]:
    """Compile a validated source document to the auditable Saturn render IR."""
    validate_mesh_ir(document)
    positions = [tuple(position) for position in document["positions"]]
    triangles = document["triangles"]
    faces = [
        (triangle["material"], *triangle["indices"])
        for triangle in triangles
    ]
    poses = [
        (pose["name"], [tuple(position) for position in pose["positions"]])
        for pose in document.get("validation_poses", [])
    ]
    explicit_forbidden_triangles = set(document.get("pairing_forbidden_triangles", []))
    texture_tiles = None
    vertex_uvs = None
    if document["version"] >= 2 and "uv" in document.get("vertex_attributes", {}):
        texture_tiles = [triangle.get("texture_tile") for triangle in triangles]
        vertex_uvs = [tuple(uv) for uv in document["vertex_attributes"]["uv"]]
    textured_triangles = (
        set(range(len(faces)))
        if "uv" in document.get("vertex_attributes", {}) and document["version"] < 2
        else set()
    )
    pairing_forbidden_triangles = textured_triangles | explicit_forbidden_triangles
    primitives, report = pair_triangles(
        positions,
        faces,
        deformation_poses=poses,
        pairing_forbidden_triangles=pairing_forbidden_triangles,
        texture_tiles=texture_tiles,
        vertex_uvs=vertex_uvs,
        projection_policy="planar" if document["version"] >= 2 else "sampled",
    )
    if pairing_forbidden_triangles:
        report["pairing_forbidden_triangle_count"] = len(pairing_forbidden_triangles)
    deformation = document.get("deformation")
    if deformation is not None:
        influence_counts = [len(items) for items in deformation["influences"]]
        influence_totals = [
            sum(influence["weight_q15"] for influence in items)
            for items in deformation["influences"]
        ]
        report.update(
            {
                "deformation_mode": deformation["mode"],
                "deformation_joint_count": deformation["joint_count"],
                "deformation_weighted_vertex_count": sum(
                    count > 0 for count in influence_counts
                ),
                "deformation_influence_count": sum(influence_counts),
                "deformation_max_influences_per_vertex": max(influence_counts, default=0),
                "deformation_vertices_over_q15_one": sum(
                    total > 32768 for total in influence_totals
                ),
            }
        )
    compiled_primitives = []
    for primitive in primitives:
        source_indices = [primitive.first_triangle]
        if primitive.second_triangle is not None:
            source_indices.append(primitive.second_triangle)
        compiled_primitives.append(
            {
                "representation": "quad" if primitive.second_triangle is not None else "triangle_fallback",
                "material": primitive.material,
                "indices": list(primitive.vertices),
                "source_triangles": [triangles[index]["source"] for index in source_indices],
            }
        )
        if document["version"] >= 2:
            compiled_primitives[-1]["texture_tiles"] = [
                triangles[index].get("texture_tile") for index in source_indices
            ]

    compiled = {
        "schema": COMPILED_SCHEMA,
        "version": document["version"],
        "name": document["name"],
        "source": document.get("source", {}),
        "positions": document["positions"],
        "materials": document["materials"],
        "vertex_attributes": document.get("vertex_attributes", {}),
        "static_world_space": document.get("static_world_space", False),
        "pairing_forbidden_triangles": document.get("pairing_forbidden_triangles", []),
        "deformation": document.get("deformation"),
        "primitives": compiled_primitives,
        "report": report,
    }
    return compiled, primitives, report


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    document = json.loads(args.input.read_text(encoding="utf-8"))
    compiled, _primitives, report = compile_mesh_ir(document)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(compiled, indent=2) + "\n", encoding="utf-8", newline="\n")
    if args.report is not None:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
