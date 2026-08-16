#!/usr/bin/env python3
"""Emit a small C view of the already-validated BOB Mesh IR v2 bank."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def c_array(values: list[object], width: int = 12) -> list[str]:
    lines: list[str] = []
    for offset in range(0, len(values), width):
        lines.append("    " + ", ".join(str(value) for value in values[offset:offset + width]) + ",")
    return lines


def build_render_clusters(mesh: dict[str, object],
                          manifest: dict[str, object]) -> list[dict[str, object]]:
    """Return deterministic one-material cluster metadata for the BOB bank."""
    entries = {int(entry["source_triangle"]): entry for entry in manifest["entries"]}
    positions = [list(map(int, point)) for point in mesh["positions"]]
    clusters: list[dict[str, object]] = []
    for ordinal, primitive in enumerate(mesh["primitives"]):
        indices = [int(value) for value in primitive["indices"]]
        sources = [int(value) for value in primitive["source_triangles"]]
        if len(indices) != 4 or not sources:
            raise ValueError("compiled primitive has no stable cluster identity")
        if any(index < 0 or index >= len(positions) for index in indices):
            raise ValueError("compiled primitive has an out-of-range position")
        source0 = sources[0]
        mandatory = source0 < 128
        far_enabled = mandatory or source0 % 8 != 0
        refs = sorted(set(indices))
        points = [positions[index] for index in refs]
        clusters.append({
            "source_ordinal": source0,
            "primitive_first": ordinal,
            "primitive_count": 1,
            "material_partition": int(primitive["material"]),
            "mandatory": mandatory,
            "bounds": {
                "min": [min(point[axis] for point in points) for axis in range(3)],
                "max": [max(point[axis] for point in points) for axis in range(3)],
            },
            "position_refs": [refs, refs, refs if far_enabled else []],
            "position_values": points,
            "tile_present": source0 in entries,
        })
    return clusters


SCENE_ADMISSION_LEAF_MAX = 8


def _admission_axis_bounds(clusters: list[dict[str, object]],
                           order: list[int], lo: int, hi: int) -> tuple[list[int], list[int]]:
    minimum = [min(int(clusters[order[i]]["bounds"]["min"][axis])
                   for i in range(lo, hi)) for axis in range(3)]
    maximum = [max(int(clusters[order[i]]["bounds"]["max"][axis])
                   for i in range(lo, hi)) for axis in range(3)]
    return minimum, maximum


def build_admission_hierarchy(
    clusters: list[dict[str, object]],
    leaf_max: int = SCENE_ADMISSION_LEAF_MAX,
) -> tuple[list[dict[str, object]], list[int]]:
    """Median-split binary hierarchy over cluster bounds.

    Sprint 2 T2.12. The runtime prunes an OUTSIDE subtree and admits an INSIDE
    subtree without testing it, which is only equivalent to the flat pass if a
    node's bounds bound its whole subtree; every node's bounds are therefore
    the exact union of the cluster bounds it owns, and a child's set is a
    subset of its parent's, so containment holds by construction and
    metadata_valid() re-proves it at bind time.

    Node indices are assigned breadth-first, so every child index is strictly
    greater than its parent's -- the acyclicity property the validator
    enforces. Each leaf owns a contiguous run of the cluster-ref array, and the
    runs partition it, so every cluster is referenced exactly once and the
    coverage check still holds.

    Leaf size 8 was chosen by measurement, not taste:
    tools/saturn/admission_hierarchy_test.c reports total frustum tests per
    pose over 964 poses x 5 frustum templates for leaf sizes 1..128, and 8 is
    the minimum (31.6% of the flat pass, against 36.0% at 4 and 33.2% at 16).
    """
    order = list(range(len(clusters)))
    nodes: list[dict[str, object]] = [{}]
    queue: list[tuple[int, int, int]] = [(0, 0, len(clusters))]
    head = 0
    while head < len(queue):
        node_index, lo, hi = queue[head]
        head += 1
        minimum, maximum = _admission_axis_bounds(clusters, order, lo, hi)
        node = {
            "bounds_min": minimum,
            "bounds_max": maximum,
            "cluster_ref_first": 0,
            "cluster_ref_count": 0,
            "portal_ref_first": 0,
            "portal_ref_count": 0,
            "child_first": 0,
            "child_count": 0,
        }
        nodes[node_index] = node
        span = hi - lo
        if span <= leaf_max:
            node["cluster_ref_first"] = lo
            node["cluster_ref_count"] = span
            continue
        axis = max(range(3), key=lambda a: (maximum[a] - minimum[a], -a))
        order[lo:hi] = sorted(
            order[lo:hi],
            key=lambda index: (int(clusters[index]["bounds"]["min"][axis]) +
                               int(clusters[index]["bounds"]["max"][axis]), index))
        node["child_first"] = len(nodes)
        node["child_count"] = 2
        nodes.extend([{}, {}])
        middle = lo + span // 2
        queue.append((node["child_first"], lo, middle))
        queue.append((node["child_first"] + 1, middle, hi))
    return nodes, order


def build_scene_admission_metadata(
    clusters: list[dict[str, object]],
    bsp: dict[str, object],
) -> dict[str, object]:
    """Adapt any validated cluster bank to the generic admission view.

    BOB publishes no portal windows, so the spatial index is the median-split
    hierarchy built above rather than a portal graph.  Callers can still
    replace it with validated node/portal arrays without changing the runtime
    API.
    """
    if not clusters:
        raise ValueError("scene admission requires at least one cluster")
    node_spans = bsp.get("node_spans", {})
    if not isinstance(node_spans, dict):
        raise ValueError("BSP node spans are malformed")
    nodes, refs = build_admission_hierarchy(clusters)
    if sorted(refs) != list(range(len(clusters))):
        raise ValueError("admission hierarchy must reference every cluster once")
    return {
        "schema": "sm64-saturn-scene-admission-v1",
        "metadata_valid": True,
        "root_node": 0,
        "nodes": nodes,
        "cluster_refs": refs,
        "portals": [],
        "portal_refs": [],
        "source_bsp_sha256": node_spans.get("sha256"),
        "cluster_count": len(clusters),
    }


def emit_scene(mesh: dict[str, object], manifest: dict[str, object],
               bsp: dict[str, object]) -> str:
    entries = {int(entry["source_triangle"]): entry for entry in manifest["entries"]}
    # VDP1 textured triangle tiles use the shared castleviewer A/B/C/C
    # lowering. The offline affine companion is not a runtime vertex.
    positions = [tuple(point) for point in mesh["positions"]]
    materials = {int(material["id"]): material for material in mesh["materials"]}
    primitives: list[dict[str, object]] = []
    for primitive in mesh["primitives"]:
        sources = [int(value) for value in primitive["source_triangles"]]
        tile = entries.get(sources[0])
        material = materials[int(primitive["material"])]
        indices = [int(value) for value in primitive["indices"]]
        if len(indices) != 4:
            raise ValueError("compiled primitive must have four VDP1 corners")
        primitives.append({
            "indices": indices,
            "source0": sources[0],
            "source1": sources[1] if len(sources) > 1 else 0xFFFF,
            "rgb": material["rgb555"],
            "tile_offset": int(tile["bank_offset"]) if tile else 0,
            "clut_offset": int(tile["clut_offset"]) if tile else 0,
            "tile_size": int(tile["tile_size"]) if tile else 0,
            "textured": 1 if tile else 0,
        })
    clusters = build_render_clusters(mesh, manifest)
    admission = build_scene_admission_metadata(clusters, bsp)
    cluster_position_refs: list[int] = []
    for cluster in clusters:
        firsts: list[int] = []
        counts: list[int] = []
        for refs in cluster["position_refs"]:
            firsts.append(len(cluster_position_refs))
            counts.append(len(refs))
            cluster_position_refs.extend(refs)
        cluster["position_ref_first"] = firsts
        cluster["position_ref_count"] = counts
    # Keep one shared position/normal identity while baking deterministic
    # material/primitive masks for runtime LOD selection. The mid tier keeps
    # every source primitive; the far tier drops one of every eight non-
    # route-critical source primitives. source0 remains the stable identity.
    lod_mid = [1] * len(primitives)
    lod_far = [0 if int(primitive["source0"]) >= 128 and
               int(primitive["source0"]) % 8 == 0 else 1
               for primitive in primitives]
    lod_masks = [[1] * len(primitives), lod_mid, lod_far]
    lod_position_refs: list[list[int]] = []
    for mask in lod_masks:
        lod_position_refs.append(sorted({index for primitive, enabled in
                                         zip(primitives, mask) if enabled
                                         for index in primitive["indices"]}))
    lod_position_ref_offsets = [0]
    for refs in lod_position_refs:
        lod_position_ref_offsets.append(lod_position_ref_offsets[-1] + len(refs))
    flat_lod_position_refs = [index for refs in lod_position_refs for index in refs]
    spans = bsp.get("node_spans")
    if not isinstance(spans, dict):
        raise ValueError("BSP report lacks compact node spans")
    first_ref = spans.get("node_first_ref")
    ref_count = spans.get("node_ref_count")
    primitive_refs = spans.get("primitive_refs")
    digest = spans.get("sha256")
    if not (isinstance(first_ref, list) and isinstance(ref_count, list) and
            isinstance(primitive_refs, list) and isinstance(digest, str) and
            len(first_ref) == len(ref_count) and len(digest) == 64):
        raise ValueError("BSP compact node spans are malformed")
    actual_digest = hashlib.sha256(json.dumps(
        [first_ref, ref_count, primitive_refs],
        separators=(",", ":")).encode()).hexdigest()
    if digest != actual_digest:
        raise ValueError("BSP compact node span digest mismatch")
    node_span_count = len(first_ref)
    primitive_ref_count = len(primitive_refs)
    lines = [
        "/* Generated by tools/saturn/emit_bob_scene.py; do not edit. */",
        "#ifndef SM64_SATURN_BOB_SCENE_H",
        "#define SM64_SATURN_BOB_SCENE_H",
        "#include <stdint.h>",
        "#include \"saturn_scene_admission.h\"",
        f"#define SM64_SATURN_BOB_POSITION_COUNT {len(positions)}U",
        f"#define SM64_SATURN_BOB_PRIMITIVE_COUNT {len(primitives)}U",
        "#define SM64_SATURN_BOB_LOD_TIER_COUNT 3U",
        f"#define SM64_SATURN_BOB_LOD_POSITION_REF_COUNT {len(flat_lod_position_refs)}U",
        f"#define SM64_SATURN_BOB_CLUSTER_COUNT {len(primitives)}U",
        f"#define SM64_SATURN_BOB_CLUSTER_POSITION_REF_COUNT {len(cluster_position_refs)}U",
        "/* bob_bsp.h owns the node-span arrays; identity must match exactly. */",
        f"#define SM64_SATURN_BOB_SCENE_NODE_SPAN_COUNT {node_span_count}U",
        f"#define SM64_SATURN_BOB_SCENE_PRIMITIVE_REF_COUNT {primitive_ref_count}U",
        f"#define SM64_SATURN_BOB_ADMISSION_NODE_COUNT {len(admission['nodes'])}U",
        f"#define SM64_SATURN_BOB_ADMISSION_CLUSTER_REF_COUNT {len(admission['cluster_refs'])}U",
        f"#define SM64_SATURN_BOB_SCENE_BSP_CONTENT_ID 0x{digest[:16]}ULL",
        f"#define SM64_SATURN_BOB_SCENE_BSP_CONTENT_SHA256 \"{digest}\"",
        "typedef struct sm64_saturn_bob_primitive {",
        "    uint16_t indices[4]; uint16_t source0; uint16_t source1;",
        "    uint8_t rgb[3]; uint8_t textured; uint8_t tile_size;",
        "    uint32_t tile_offset; uint32_t clut_offset;",
        "} sm64_saturn_bob_primitive_t;",
        "static const int32_t sm64_saturn_bob_positions[SM64_SATURN_BOB_POSITION_COUNT][3] = {",
    ]
    for point in positions:
        lines.append("    {%d, %d, %d}," % tuple(point))
    lines += [
        "};",
        "static const sm64_saturn_bob_primitive_t sm64_saturn_bob_primitives[SM64_SATURN_BOB_PRIMITIVE_COUNT] = {",
    ]
    for primitive in primitives:
        lines.append(
            "    {{%d, %d, %d, %d}, %d, %d, {%d, %d, %d}, %d, %d, %dU, %dU}," % (
                *primitive["indices"], primitive["source0"], primitive["source1"],
                *primitive["rgb"], primitive["textured"], primitive["tile_size"],
                primitive["tile_offset"], primitive["clut_offset"],
            )
        )
    lines += [
        "};",
        "static const uint16_t sm64_saturn_bob_cluster_position_refs[SM64_SATURN_BOB_CLUSTER_POSITION_REF_COUNT] = {",
    ]
    lines.extend(c_array(cluster_position_refs, width=16))
    lines += [
        "};",
        "static const sm64_saturn_render_cluster_t sm64_saturn_bob_render_clusters[SM64_SATURN_BOB_CLUSTER_COUNT] = {",
    ]
    for cluster in clusters:
        lines.append("    {{%s}, {%s}, %dU, %dU, {%dU, %dU, %dU}, {%dU, %dU, %dU}, %dU, %dU, %dU, {0U, 0U, 0U}, 0U, 0U}," % (
            ", ".join(str(value * 65536) for value in cluster["bounds"]["min"]),
            ", ".join(str(value * 65536) for value in cluster["bounds"]["max"]),
            cluster["primitive_first"], cluster["primitive_count"],
            *cluster["position_ref_first"], *cluster["position_ref_count"],
            cluster["material_partition"], cluster["source_ordinal"],
            1 if cluster["mandatory"] else 0))
    lines += [
        "};",
        "static const sm64_saturn_scene_admission_node_t sm64_saturn_bob_scene_admission_nodes[SM64_SATURN_BOB_ADMISSION_NODE_COUNT] = {",
    ]
    for node in admission["nodes"]:
        lines.append("    {{%s}, {%s}, %dU, %dU, %dU, %dU, %dU, %dU}," % (
            ", ".join(str(value * 65536) for value in node["bounds_min"]),
            ", ".join(str(value * 65536) for value in node["bounds_max"]),
            node["cluster_ref_first"], node["cluster_ref_count"],
            node["portal_ref_first"], node["portal_ref_count"],
            node.get("child_first", 0), node.get("child_count", 0)))
    lines += [
        "};",
        "static const uint16_t sm64_saturn_bob_scene_admission_cluster_refs[SM64_SATURN_BOB_ADMISSION_CLUSTER_REF_COUNT] = {",
    ]
    lines.extend(c_array(admission["cluster_refs"], width=16))
    lines += [
        "};",
        "/* Compact, sorted unique position references for near/mid/far. The",
        " * runtime selects one complete tier before workers transform vertices. */",
        "static const uint16_t sm64_saturn_bob_lod_position_ref_offsets[SM64_SATURN_BOB_LOD_TIER_COUNT + 1U] = {",
    ]
    lines.extend(c_array(lod_position_ref_offsets, width=12))
    lines += [
        "};",
        "static const uint16_t sm64_saturn_bob_lod_position_refs[SM64_SATURN_BOB_LOD_POSITION_REF_COUNT] = {",
    ]
    lines.extend(c_array(flat_lod_position_refs, width=16))
    lines += [
        "};",
        "static const uint8_t sm64_saturn_bob_lod_mid_mask[SM64_SATURN_BOB_PRIMITIVE_COUNT] = {",
    ]
    lines.extend(c_array(lod_mid, width=24))
    lines += [
        "};",
        "static const uint8_t sm64_saturn_bob_lod_far_mask[SM64_SATURN_BOB_PRIMITIVE_COUNT] = {",
    ]
    lines.extend(c_array(lod_far, width=24))
    lines += ["};", "#endif", ""]
    return "\n".join(lines)


def emit(mesh: dict[str, object], manifest: dict[str, object],
         bsp: dict[str, object]) -> str:
    """Compatibility wrapper for the existing BOB generated-header path."""
    return emit_scene(mesh, manifest, bsp)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mesh", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--bsp", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    bsp = json.loads(args.bsp.read_text(encoding="utf-8"))
    output = emit(
        json.loads(args.mesh.read_text(encoding="utf-8")),
        json.loads(args.manifest.read_text(encoding="utf-8")),
        bsp,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(output, encoding="utf-8")


if __name__ == "__main__":
    main()
