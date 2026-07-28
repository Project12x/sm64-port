#!/usr/bin/env python3
"""Build the reference-backed static BSP for the BOB Mesh IR v2 bank.

This is deliberately a host-side artifact first.  The runtime can only use a
BSP after its planes, node ranges, and split UVs are carried through the scene
header, so this pass measures the exact fragment/dependency cost before that
ABI is widened.  It reuses ``static_bsp`` (the same exact-rational compiler
used by castleviewer) rather than introducing a second ordering algorithm.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from static_bsp import Polygon, Vertex, build, painter_order


def _polygons(scene: dict[str, object]) -> list[Polygon]:
    positions = scene["positions"]
    uv = scene["vertex_attributes"]["uv"]
    output: list[Polygon] = []
    for primitive_index, primitive in enumerate(scene["primitives"]):
        indices = [int(value) for value in primitive["indices"]]
        if indices[3] == indices[2]:
            indices = indices[:3]
        vertices = tuple(
            Vertex.make(positions[index], uv[index]) for index in indices
        )
        sources = primitive["source_triangles"]
        output.append(Polygon(
            vertices,
            source=primitive_index,
            texture=int(primitive["material"]),
            root=int(sources[0]),
        ))
    return output


def compile_bsp(scene: dict[str, object], candidate_limit: int = 32,
                split_weight: int = 8) -> dict[str, object]:
    polygons = _polygons(scene)
    root, stats = build(polygons, candidate_limit=candidate_limit,
                        split_weight=split_weight)
    origin_order = painter_order(root, (0, 0, 0))
    return {
        "schema": "sm64-saturn-bob-static-bsp",
        "version": 1,
        "source": scene["source"],
        "source_ir_version": scene["version"],
        "policy": "exact rational convex splits; deterministic balanced plane heuristic",
        "input_render_polygons": len(polygons),
        "output_convex_polygons": stats.output_polygons,
        "additional_polygons": stats.output_polygons - stats.input_polygons,
        "split_events": stats.split_events,
        "node_count": stats.node_count,
        "max_depth": stats.max_depth,
        "max_fragment_vertices": stats.max_vertices,
        "candidate_limit": candidate_limit,
        "split_weight": split_weight,
        "deterministic_sha256": stats.digest,
        "origin_order_sha256": hashlib.sha256(
            json.dumps([
                {"source": p.source, "vertices": [
                    [[v.numerator, v.denominator] for v in vertex.position]
                    for vertex in p.vertices
                ]} for p in origin_order
            ], sort_keys=True, separators=(",", ":")).encode()
        ).hexdigest(),
        "limits": [
            "Report-only until split fragments and node ranges are emitted into bob_scene.h",
            "Runtime traversal must use the camera position, not the origin order",
            "UV interpolation remains exact rational until final scene-header quantization",
        ],
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--candidate-limit", type=int, default=32)
    parser.add_argument("--split-weight", type=int, default=8)
    args = parser.parse_args()
    scene = json.loads(args.input.read_text(encoding="utf-8"))
    report = compile_bsp(scene, args.candidate_limit, args.split_weight)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
