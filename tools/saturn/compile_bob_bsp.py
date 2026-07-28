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
from fractions import Fraction
from pathlib import Path

from static_bsp import Node, Polygon, Vertex, build, painter_order


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


def _flatten(root: Node) -> tuple[list[Node], list[int], list[tuple[int, int]], list[tuple[int, int]]]:
    nodes: list[Node] = []
    refs: list[int] = []
    ranges: list[tuple[int, int]] = []

    def visit(node: Node | None) -> int:
        if node is None:
            return -1
        index = len(nodes)
        nodes.append(node)
        ranges.append((len(refs), len(node.coplanar)))
        refs.extend(polygon.source for polygon in node.coplanar)
        visit(node.front)
        visit(node.back)
        return index

    visit(root)
    # Revisit using the same preorder so child indices are stable and there is
    # no pointer-shaped data in the generated C artifact.
    cursor = 0
    children: list[tuple[int, int]] = []

    def wire(node: Node | None) -> None:
        nonlocal cursor
        if node is None:
            return
        index = cursor
        cursor += 1
        front = cursor if node.front is not None else -1
        if node.front is not None:
            wire(node.front)
        back = cursor if node.back is not None else -1
        if node.back is not None:
            wire(node.back)
        if len(children) <= index:
            children.extend([(-1, -1)] * (index + 1 - len(children)))
        children[index] = (front, back)

    wire(root)
    return nodes, refs, children, ranges


def _runtime_plane(plane: tuple[int, int, int, int]) -> tuple[tuple[int, int, int], int]:
    """Quantize an exact plane for bounded SH-2 sign tests.

    The split compiler can produce enormous canonical coefficients after
    repeated rational intersections.  Plane sign is scale-invariant, so keep
    the coefficient vector in a fixed 20-bit envelope and round the offset at
    the same scale.  The resulting products fit signed 64-bit for BOB's world
    coordinates while the exact plane remains in the host digest/report.
    """
    maximum = max(abs(value) for value in plane[:3])
    if maximum == 0:
        raise ValueError("BSP plane has no spatial normal")
    target = 1 << 20
    scale = Fraction(maximum, target)

    def rounded(value: Fraction) -> int:
        if value >= 0:
            return int(value + Fraction(1, 2))
        return -int(-value + Fraction(1, 2))

    normal = tuple(rounded(Fraction(value) / scale) for value in plane[:3])
    distance = rounded(Fraction(plane[3]) / scale)
    if normal == (0, 0, 0):
        raise ValueError("BSP plane quantized to a zero normal")
    return normal, distance


def header_text(scene: dict[str, object], candidate_limit: int = 32,
                split_weight: int = 8) -> str:
    polygons = _polygons(scene)
    root, _stats = build(polygons, candidate_limit=candidate_limit,
                         split_weight=split_weight)
    nodes, refs, children, ranges = _flatten(root)
    lines = [
        "/* Generated by tools/saturn/compile_bob_bsp.py; do not edit. */",
        "#ifndef SM64_SATURN_BOB_BSP_H",
        "#define SM64_SATURN_BOB_BSP_H",
        "#include <stdint.h>",
        f"#define SM64_SATURN_BOB_BSP_NODE_COUNT {len(nodes)}U",
        f"#define SM64_SATURN_BOB_BSP_REF_COUNT {len(refs)}U",
        "static const int32_t sm64_saturn_bob_bsp_planes[SM64_SATURN_BOB_BSP_NODE_COUNT][3] = {",
    ]
    lines.extend("    {%d, %d, %d}," % _runtime_plane(node.plane)[0]
                 for node in nodes)
    lines += [
        "};",
        "static const int64_t sm64_saturn_bob_bsp_distances[SM64_SATURN_BOB_BSP_NODE_COUNT] = {",
    ]
    lines.extend("    INT64_C(%d)," % _runtime_plane(node.plane)[1]
                 for node in nodes)
    lines += [
        "};",
        "static const int16_t sm64_saturn_bob_bsp_children[SM64_SATURN_BOB_BSP_NODE_COUNT][2] = {",
    ]
    lines.extend("    {%d, %d}," % pair for pair in children)
    lines += [
        "};",
        "static const uint16_t sm64_saturn_bob_bsp_ref_ranges[SM64_SATURN_BOB_BSP_NODE_COUNT][2] = {",
    ]
    lines.extend("    {%dU, %dU}," % pair for pair in ranges)
    lines += [
        "};",
        "static const uint16_t sm64_saturn_bob_bsp_refs[SM64_SATURN_BOB_BSP_REF_COUNT] = {",
    ]
    lines.extend("    %dU," % value for value in refs)
    lines += ["};", "#endif", ""]
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--header", type=Path)
    parser.add_argument("--candidate-limit", type=int, default=32)
    parser.add_argument("--split-weight", type=int, default=8)
    args = parser.parse_args()
    scene = json.loads(args.input.read_text(encoding="utf-8"))
    report = compile_bsp(scene, args.candidate_limit, args.split_weight)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if args.header is not None:
        args.header.parent.mkdir(parents=True, exist_ok=True)
        args.header.write_text(header_text(scene, args.candidate_limit,
                                           args.split_weight), encoding="utf-8")


if __name__ == "__main__":
    main()
