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
from math import ceil, floor
from pathlib import Path

from static_bsp import (Node, Polygon, Vertex, build, iter_polygons,
                         painter_order, plane_distance)


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
                split_weight: int = 8,
                manifest: dict[str, object] | None = None) -> dict[str, object]:
    polygons = _polygons(scene)
    root, stats = build(polygons, candidate_limit=candidate_limit,
                        split_weight=split_weight)
    nodes, refs, children, ranges, subtree_ranges, octant_orders, leaf_ranges = _flatten(root)
    node_spans = _node_spans(ranges, refs)
    leaf_nodes = [index for index, (start, count) in enumerate(leaf_ranges)
                  if start >= 0 and count >= 0]
    origin_order = painter_order(root, (0, 0, 0))
    report = {
        "schema": "sm64-saturn-bob-static-bsp",
        "version": 2,
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
        "runtime_plane_policy": "scale-invariant sign test; normal max 2^20; rounded offset at same scale",
        "runtime_normal_limit": 1048576,
        "origin_order_sha256": hashlib.sha256(
            json.dumps([
                {"source": p.source, "vertices": [
                    [[v.numerator, v.denominator] for v in vertex.position]
                    for vertex in p.vertices
                ]} for p in origin_order
            ], sort_keys=True, separators=(",", ":")).encode()
        ).hexdigest(),
        "bounds": _bounds_report(root),
        "flattened_ref_count": len(refs),
        "node_spans": node_spans,
        "leaf_node_count": len(leaf_nodes),
        "leaf_fragment_count": sum(count for _start, count in leaf_ranges
                                    if count >= 0),
        "leaf_range_sha256": hashlib.sha256(
            json.dumps(leaf_ranges, separators=(",", ":")).encode()
        ).hexdigest(),
        "subtree_range_sha256": hashlib.sha256(
            json.dumps(subtree_ranges, separators=(",", ":")).encode()
        ).hexdigest(),
        "camera_octant_order": {
            "octants": 8,
            "policy": "quantized node extent probe; runtime validates actual plane sign",
            "sha256": hashlib.sha256(
                json.dumps(octant_orders, separators=(",", ":")).encode()
            ).hexdigest(),
        },
        "limits": [
            "Bounds are conservative floor/ceil quantized exact-rational fragment bounds",
            "Runtime traversal must use the camera position, not the origin order",
            "UV interpolation remains exact rational until final scene-header quantization",
            "Preorder fragment refs are contiguous per subtree; source0 identity is preserved",
        ],
    }
    if manifest is not None:
        entries = {int(entry["source_triangle"]): entry
                   for entry in manifest["entries"]}
        fragment_classes = {"16x16": 0, "32x32": 0, "flat": 0}
        triangle_classes = {"16x16": 0, "32x32": 0, "flat": 0}
        texture_bytes = 0
        clut_bytes = 0
        for polygon in iter_polygons(root):
            primitive = scene["primitives"][polygon.source]
            source = int(primitive["source_triangles"][0])
            entry = entries.get(source)
            triangle_count = len(polygon.vertices) - 2
            if entry is None:
                fragment_classes["flat"] += 1
                triangle_classes["flat"] += triangle_count
                continue
            size = int(entry["tile_size"])
            key = f"{size}x{size}"
            fragment_classes[key] = fragment_classes.get(key, 0) + 1
            triangle_classes[key] = triangle_classes.get(key, 0) + triangle_count
            texture_bytes += triangle_count * (size * size // 2)
            clut_bytes += triangle_count * 32
        report["fragment_tile_classes"] = fragment_classes
        report["triangulated_fragment_tile_classes"] = triangle_classes
        report["triangulated_fragment_count"] = sum(triangle_classes.values())
        report["estimated_fragment_texture_bytes"] = texture_bytes
        report["estimated_fragment_clut_bytes"] = clut_bytes
        report["estimated_fragment_resident_bytes"] = texture_bytes + clut_bytes
        report["vdp1_texture_budget_bytes"] = 446432
        low_tier_texture = report["triangulated_fragment_count"] * (16 * 16 // 2)
        low_tier_clut = report["triangulated_fragment_count"] * 32
        report["all_16x16_fragment_resident_bytes"] = low_tier_texture + low_tier_clut
    return report


def compile_bob_bsp(scene: dict[str, object], candidate_limit: int = 32,
                    split_weight: int = 8,
                    manifest: dict[str, object] | None = None) -> dict[str, object]:
    """Compatibility wrapper; package callers should use ``compile_bsp``."""
    return compile_bsp(scene, candidate_limit, split_weight, manifest)


def _node_metrics(node: Node | None) -> tuple[tuple[Fraction, Fraction, Fraction],
                                                tuple[Fraction, Fraction, Fraction],
                                                int, int, int, int]:
    """Return conservative bounds and deterministic work metadata for a node.

    Bounds are derived from final split fragments, not source primitive boxes.
    The work weight is versioned by this module's policy: one unit per
    fragment, two for each extra vertex introduced by a split, and one for
    each adjacent material transition in the flattened subtree.
    """
    if node is None:
        zero = (Fraction(0), Fraction(0), Fraction(0))
        return zero, zero, 0, 0, 0, 0
    polygons = list(iter_polygons(node))
    own_polygons = list(node.coplanar)
    if not polygons:
        zero = (Fraction(0), Fraction(0), Fraction(0))
        return zero, zero, 0, 0, 0, 0
    minimum = tuple(min(vertex.position[axis] for polygon in polygons
                        for vertex in polygon.vertices)
                    for axis in range(3))
    maximum = tuple(max(vertex.position[axis] for polygon in polygons
                        for vertex in polygon.vertices)
                    for axis in range(3))
    material_changes = sum(
        1 for left, right in zip(own_polygons, own_polygons[1:])
        if left.texture != right.texture)
    weight = sum(1 + max(0, len(polygon.vertices) - 4) * 2
                 for polygon in own_polygons) + material_changes
    child_metrics = [_node_metrics(child) for child in (node.front, node.back)
                     if child is not None]
    leaf_count = 1 if node.front is None and node.back is None else 0
    max_depth = 1
    for child_min, child_max, child_weight, _child_count, child_leaves, child_depth in child_metrics:
        minimum = tuple(min(minimum[axis], child_min[axis]) for axis in range(3))
        maximum = tuple(max(maximum[axis], child_max[axis]) for axis in range(3))
        weight += child_weight
        leaf_count += child_leaves
        max_depth = max(max_depth, child_depth + 1)
    return minimum, maximum, weight, len(polygons), leaf_count, max_depth


def _quantized_bounds(node: Node | None) -> tuple[tuple[int, int, int], tuple[int, int, int]]:
    minimum, maximum, _weight, _count, _leaves, _depth = _node_metrics(node)
    return tuple(floor(value) for value in minimum), tuple(ceil(value) for value in maximum)


def _bounds_report(root: Node) -> dict[str, object]:
    minimum, maximum, weight, fragments, leaves, depth = _node_metrics(root)
    quantized_min, quantized_max = _quantized_bounds(root)
    return {
        "quantization": "floor minimum / ceil maximum; exact Fraction source",
        "root_min": list(quantized_min),
        "root_max": list(quantized_max),
        "root_exact_min": [[value.numerator, value.denominator] for value in minimum],
        "root_exact_max": [[value.numerator, value.denominator] for value in maximum],
        "root_work_weight": weight,
        "root_fragment_count": fragments,
        "root_leaf_count": leaves,
        "max_depth": depth,
    }


def _flatten(root: Node) -> tuple[
        list[Node], list[int], list[tuple[int, int]], list[tuple[int, int]],
        list[tuple[int, int]], list[list[tuple[int, int]]],
        list[tuple[int, int]]]:
    """Flatten nodes and their immutable work ranges in preorder.

    ``refs`` is a single preorder stream of final BSP fragments.  Every
    fragment is appended exactly once, so a subtree's range is contiguous and
    can be split between the SH-2s without rebuilding Mesh IR identity.  The
    octant table is a deterministic admission hint: it chooses the child
    order using a point one quantized node extent outside the node in each
    camera-octant direction.  Runtime still validates the actual plane sign
    for painter order; the table only avoids pointer-shaped policy data in the
    generated artifact.
    """
    nodes: list[Node] = []
    refs: list[int] = []
    ranges: list[tuple[int, int]] = []
    subtree_ranges: list[tuple[int, int]] = []
    leaf_ranges: list[tuple[int, int]] = []
    children: list[tuple[int, int]] = []
    octant_orders: list[list[tuple[int, int]]] = []

    def visit(node: Node | None) -> int:
        if node is None:
            return -1
        index = len(nodes)
        nodes.append(node)
        own_start = len(refs)
        ranges.append((own_start, len(node.coplanar)))
        subtree_ranges.append((own_start, own_start))
        leaf_ranges.append((-1, 0))
        children.append((-1, -1))
        octant_orders.append([(-1, -1)] * 8)
        refs.extend(polygon.source for polygon in node.coplanar)
        front = visit(node.front)
        back = visit(node.back)
        children[index] = (front, back)
        subtree_end = len(refs)
        subtree_ranges[index] = (own_start, subtree_end - own_start)
        if front < 0 and back < 0:
            leaf_ranges[index] = (own_start, len(node.coplanar))
        if front >= 0 and back >= 0:
            minimum, maximum = _quantized_bounds(node)
            center = tuple(Fraction(minimum[axis] + maximum[axis], 2)
                           for axis in range(3))
            extent = tuple(Fraction(maximum[axis] - minimum[axis], 2) + 1
                           for axis in range(3))
            for octant in range(8):
                point = tuple(
                    center[axis] + (extent[axis]
                                    if octant & (1 << axis) else -extent[axis])
                    for axis in range(3))
                if plane_distance(node.plane, point) >= 0:
                    octant_orders[index][octant] = (front, back)
                else:
                    octant_orders[index][octant] = (back, front)
        return index

    visit(root)
    return nodes, refs, children, ranges, subtree_ranges, octant_orders, leaf_ranges


def _node_spans(ranges: list[tuple[int, int]], refs: list[int]) -> dict[str, object]:
    """Pack deterministic node-local primitive spans for runtime admission.

    Exact BSP splitting can place a source primitive in more than one node.
    That conservative cross-node coverage remains, while duplicate source IDs
    within one local span are removed. The runtime keeps the old traversal's
    first-reference-wins policy as it appends only accepted spans.
    """
    first_ref: list[int] = []
    ref_count: list[int] = []
    packed: list[int] = []
    for start, count in ranges:
        first_ref.append(len(packed))
        seen: set[int] = set()
        for primitive in refs[start:start + count]:
            if primitive not in seen:
                seen.add(primitive)
                packed.append(primitive)
        ref_count.append(len(packed) - first_ref[-1])
    digest = hashlib.sha256(json.dumps(
        [first_ref, ref_count, packed], separators=(",", ":")).encode()).hexdigest()
    return {
        "policy": "node-local conservative spans; stable first reference; unique within span",
        "node_first_ref": first_ref,
        "node_ref_count": ref_count,
        "primitive_ref_count": len(packed),
        "primitive_refs": packed,
        "sha256": digest,
        "content_id": int(digest[:16], 16),
    }


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
    nodes, refs, children, ranges, subtree_ranges, octant_orders, leaf_ranges = _flatten(root)
    node_spans = _node_spans(ranges, refs)
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
        "/* Packed deterministic work spans; one local span per BSP node. */",
        "#define SM64_SATURN_BOB_NODE_SPAN_COUNT SM64_SATURN_BOB_BSP_NODE_COUNT",
        f"#define SM64_SATURN_BOB_PRIMITIVE_REF_COUNT {node_spans['primitive_ref_count']}U",
        f"#define SM64_SATURN_BOB_BSP_CONTENT_ID 0x{node_spans['sha256'][:16]}ULL",
        f"#define SM64_SATURN_BOB_BSP_CONTENT_SHA256 \"{node_spans['sha256']}\"",
        "static const uint16_t sm64_saturn_bob_node_first_ref[SM64_SATURN_BOB_NODE_SPAN_COUNT] = {",
    ]
    lines.extend("    %dU," % value for value in node_spans["node_first_ref"])
    lines += [
        "};",
        "static const uint16_t sm64_saturn_bob_node_ref_count[SM64_SATURN_BOB_NODE_SPAN_COUNT] = {",
    ]
    lines.extend("    %dU," % value for value in node_spans["node_ref_count"])
    lines += [
        "};",
        "static const uint16_t sm64_saturn_bob_primitive_refs[SM64_SATURN_BOB_PRIMITIVE_REF_COUNT] = {",
    ]
    lines.extend(
        "    " + ", ".join(str(value) + "U" for value in
                            node_spans["primitive_refs"][offset:offset + 16]) + ","
        for offset in range(0, node_spans["primitive_ref_count"], 16))
    lines += [
        "};",
        "/* Contiguous preorder ranges for a complete subtree and its leaf. */",
        "static const uint16_t sm64_saturn_bob_bsp_subtree_ranges[SM64_SATURN_BOB_BSP_NODE_COUNT][2] = {",
    ]
    lines.extend("    {%dU, %dU}," % pair for pair in subtree_ranges)
    lines += [
        "};",
        "static const int16_t sm64_saturn_bob_bsp_leaf_ranges[SM64_SATURN_BOB_BSP_NODE_COUNT][2] = {",
    ]
    lines.extend("    {%d, %d}," % pair for pair in leaf_ranges)
    lines += [
        "};",
        "/* Stable camera-octant admission order; -1 denotes an absent child. */",
        "static const int16_t sm64_saturn_bob_bsp_octant_child_order[SM64_SATURN_BOB_BSP_NODE_COUNT][8][2] = {",
    ]
    lines.extend(
        "    {" + ", ".join("{%d, %d}" % pair for pair in orders) + "},"
        for orders in octant_orders)
    lines += [
        "};",
        "/* Conservative final-fragment bounds; minima are floored and maxima ceiled. */",
        "static const int32_t sm64_saturn_bob_bsp_bounds_min[SM64_SATURN_BOB_BSP_NODE_COUNT][3] = {",
    ]
    lines.extend(
        "    {%d, %d, %d}," % _quantized_bounds(node)[0] for node in nodes)
    lines += [
        "};",
        "static const int32_t sm64_saturn_bob_bsp_bounds_max[SM64_SATURN_BOB_BSP_NODE_COUNT][3] = {",
    ]
    lines.extend(
        "    {%d, %d, %d}," % _quantized_bounds(node)[1] for node in nodes)
    lines += [
        "};",
        "static const uint16_t sm64_saturn_bob_bsp_work_weight[SM64_SATURN_BOB_BSP_NODE_COUNT] = {",
    ]
    lines.extend("    %dU," % _node_metrics(node)[2] for node in nodes)
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
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--candidate-limit", type=int, default=32)
    parser.add_argument("--split-weight", type=int, default=8)
    args = parser.parse_args()
    scene = json.loads(args.input.read_text(encoding="utf-8"))
    manifest = None if args.manifest is None else json.loads(
        args.manifest.read_text(encoding="utf-8"))
    report = compile_bsp(scene, args.candidate_limit, args.split_weight,
                         manifest)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if args.header is not None:
        args.header.parent.mkdir(parents=True, exist_ok=True)
        args.header.write_text(header_text(scene, args.candidate_limit,
                                           args.split_weight), encoding="utf-8")


if __name__ == "__main__":
    main()
