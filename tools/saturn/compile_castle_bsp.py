#!/usr/bin/env python3
"""Prototype a camera-independent BSP over source-derived Castle VDP1 tiles."""
from __future__ import annotations

import argparse
from fractions import Fraction
import json
from pathlib import Path

from static_bsp import Polygon, Vertex, build, painter_order


def midpoint(a: tuple[int, ...], b: tuple[int, ...]) -> tuple[int, ...]:
    return tuple((left + right) // 2 for left, right in zip(a, b))


def _polygon(vertices: list[tuple[int, int, int]], uv: list[tuple[int, int]],
             primitive_index: int, primitive: dict[str, object]) -> Polygon:
    return Polygon(
        tuple(Vertex.make(position, attributes) for position, attributes
              in zip(vertices, uv)),
        source=primitive_index, root=int(primitive["root"]),
        layer=int(primitive["layer"]), texture=int(primitive["texture_index"]),
    )


def expand_render_polygons(scene: dict[str, object], subdivision: int) -> list[Polygon]:
    """Reproduce the current camera-independent quad/triangle subdivision."""
    if subdivision not in (1, 4):
        raise ValueError("subdivision must be 1 or 4")
    output: list[Polygon] = []
    positions = scene["positions"]
    triangles = scene["triangles"]
    all_uv = scene["uv"]
    for primitive_index, primitive in enumerate(scene["primitives"]):
        first = int(primitive["first_triangle"])
        triangle = triangles[first]
        original = [tuple(positions[index]) for index in triangle]
        original_uv = [tuple(value) for value in all_uv[first]]
        second = primitive["second_triangle"]
        if second is not None:
            vertex_uv: dict[int, tuple[int, int]] = {}
            for triangle_index in (first, int(second)):
                for vertex, value in zip(triangles[triangle_index], all_uv[triangle_index]):
                    vertex_uv[int(vertex)] = tuple(value)
            indices = [int(value) for value in primitive["vertices"]]
            output.append(_polygon([tuple(positions[index]) for index in indices],
                                   [vertex_uv[index] for index in indices],
                                   primitive_index, primitive))
            continue
        if subdivision == 1:
            output.append(_polygon(original, original_uv, primitive_index, primitive))
            continue
        ab, bc, ca = midpoint(original[0], original[1]), midpoint(original[1], original[2]), midpoint(original[2], original[0])
        uab, ubc, uca = midpoint(original_uv[0], original_uv[1]), midpoint(original_uv[1], original_uv[2]), midpoint(original_uv[2], original_uv[0])
        for vertices, uv in (
            ((original[0], ab, ca), (original_uv[0], uab, uca)),
            ((ab, original[1], bc), (uab, original_uv[1], ubc)),
            ((ca, bc, original[2]), (uca, ubc, original_uv[2])),
            ((ab, bc, ca), (uab, ubc, uca)),
        ):
            output.append(_polygon(list(vertices), list(uv), primitive_index, primitive))
    return output


def _triangulated_count(polygons: list[Polygon]) -> int:
    return sum(len(polygon.vertices) - 2 for polygon in polygons)


def compile_bsp(scene: dict[str, object], subdivision: int = 4,
                candidate_limit: int = 32, split_weight: int = 8) -> dict[str, object]:
    render_polygons = expand_render_polygons(scene, subdivision)
    # The RDP alpha layer is binary cutout and belongs in the opaque BSP.
    # True translucent decals remain a later, independently sorted pass.
    static = [polygon for polygon in render_polygons if polygon.layer != 1]
    decals = [polygon for polygon in render_polygons if polygon.layer == 1]
    root, stats = build(static, candidate_limit=candidate_limit,
                        split_weight=split_weight)
    output = list(painter_order(root, (0, 0, 0)))
    triangulated = _triangulated_count(output)
    return {
        "schema": "sm64-saturn-static-bsp-prototype",
        "version": 1,
        "source": scene["source"],
        "source_ir_version": scene["version"],
        "policy": "exact rational convex splits; deterministic balanced plane heuristic",
        "dynamic_insertion": "traverse Mario position through the same BSP and emit it in the containing leaf",
        "translucency": "LAYER_TRANSPARENT_DECAL excluded for independent late pass",
        "subdivision": subdivision,
        "candidate_limit": candidate_limit,
        "split_weight": split_weight,
        "input_render_polygons": len(render_polygons),
        "input_static_polygons": len(static),
        "decal_polygons": len(decals),
        "output_convex_polygons": stats.output_polygons,
        "output_vdp1_triangle_estimate": triangulated,
        "additional_convex_polygons": stats.output_polygons - stats.input_polygons,
        "additional_vdp1_triangle_estimate": triangulated - _triangulated_count(static),
        "split_events": stats.split_events,
        "node_count": stats.node_count,
        "max_depth": stats.max_depth,
        "max_fragment_vertices": stats.max_vertices,
        "deterministic_sha256": stats.digest,
        "limits": [
            "Prototype report only; split fragments are not emitted to VDP1 yet",
            "Texture tiles must be rebaked from interpolated Fast3D UVs before runtime integration",
            "Node planes require bounded fixed-point quantization validation for SH-2 traversal",
        ],
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--intake", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--subdivision", type=int, choices=(1, 4), default=4)
    parser.add_argument("--candidate-limit", type=int, default=32)
    parser.add_argument("--split-weight", type=int, default=8)
    args = parser.parse_args()
    scene = json.loads(args.intake.read_text(encoding="utf-8"))
    report = compile_bsp(scene, args.subdivision, args.candidate_limit,
                         args.split_weight)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
