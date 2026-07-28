#!/usr/bin/env python3
"""Bake the bounded 16x16 texture tier for BSP-split BOB fragments.

The geometry/UVs come from the exact-rational ``static_bsp`` pass.  Every
convex fragment is fan-triangulated for VDP1, and every textured child gets a
fresh 16x16 CLUT tile sampled from its interpolated Fast3D UVs.  This tool is
host-side until the generated fragment scene is adopted by sourceboot.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

from bake_bob_tiles import BOB_TEXTURES, _png_pixels
from bake_castle_uv import (nearest_integer, pack_clut16, quantize_clut16,
                            sample_triangle, triangulate_polygon)
from compile_bob_bsp import _polygons, _runtime_plane
from static_bsp import build, iter_polygons


TILE = 16
MAX_RESIDENT_BYTES = 446432


def _fragment_bsp(root: object,
                  fragment_indices: dict[int, list[int]]) -> dict[str, object]:
    nodes: list[dict[str, object] | None] = []
    refs: list[int] = []

    def visit(node: object | None) -> int:
        if node is None:
            return -1
        index = len(nodes)
        nodes.append(None)
        front = visit(node.front)
        back = visit(node.back)
        start = len(refs)
        for polygon in node.coplanar:
            refs.extend(fragment_indices[id(polygon)])
        normal, distance = _runtime_plane(node.plane)
        nodes[index] = {
            "plane": normal,
            "distance": distance,
            "children": (front, back),
            "ref_range": (start, len(refs) - start),
        }
        return index

    visit(root)
    return {
        "node_count": len(nodes),
        "ref_count": len(refs),
        "nodes": nodes,
        "refs": refs,
    }


def _primitive_tile_allowed(scene: dict[str, object], primitive: dict[str, object]) -> bool:
    indices = [int(value) for value in primitive["indices"][:3]]
    uv = scene["vertex_attributes"]["uv"]
    points = [uv[index] for index in indices]
    state = primitive["texture_tiles"][0]["state"]
    span_s = max(point[0] for point in points) - min(point[0] for point in points)
    span_t = max(point[1] for point in points) - min(point[1] for point in points)
    tile_s = int(state["lrs"]) - int(state["uls"]) + 4
    tile_t = int(state["lrt"]) - int(state["ult"]) + 4
    k = max(span_s / tile_s, span_t / tile_t) / 8.0
    return k <= 16


def bake(scene: dict[str, object], asset_root: Path) -> tuple[bytes, bytes, dict[str, object], dict[str, object]]:
    textures: dict[str, tuple[int, int, list[int], str, int]] = {}
    names = {
        str(scene["materials"][int(primitive["material"])] ["texture"])
        for primitive in scene["primitives"]
        if scene["materials"][int(primitive["material"])] ["texture"] is not None
    }
    for name in sorted(names):
        relative = BOB_TEXTURES.get(name)
        if relative is None:
            raise ValueError(f"no exported BOB texture for {name}")
        width, height, pixels = _png_pixels(asset_root / relative)
        source = (asset_root / relative).read_bytes()
        textures[name] = (width, height, pixels,
                          hashlib.sha256(source).hexdigest(), len(source))

    root, stats = build(_polygons(scene))
    bank = bytearray()
    clut = bytearray()
    entries: list[dict[str, object]] = []
    fragments: list[dict[str, object]] = []
    fragment_indices: dict[int, list[int]] = {}
    for polygon_index, polygon in enumerate(iter_polygons(root)):
        primitive = scene["primitives"][polygon.source]
        material = scene["materials"][int(primitive["material"])]
        texture_name = material["texture"] if _primitive_tile_allowed(
            scene, primitive) else None
        children = triangulate_polygon(polygon)
        polygon_fragments: list[int] = []
        for child_index, child in enumerate(children):
            points = [[nearest_integer(vertex.position[axis]) for axis in range(3)]
                      for vertex in child.vertices]
            uvs = [[int(vertex.attributes[axis]) for axis in range(2)]
                   for vertex in child.vertices]
            uv = tuple(tuple(vertex.attributes[axis] for axis in range(2))
                       for vertex in child.vertices)
            fragment = {
                "source_primitive": polygon.source,
                "source_triangles": primitive["source_triangles"],
                "polygon_index": polygon_index,
                "child_index": child_index,
                "positions": points,
                "uv": uvs,
                "textured": texture_name is not None,
                "tile_offset": 0,
                "clut_offset": 0,
            }
            if texture_name is not None:
                state = primitive["texture_tiles"][0]["state"]
                texture = textures[str(texture_name)]
                # Transparent source texels may retain RGB bits in exported
                # PNGs.  VDP1's reserved CLUT entry is zero; canonicalize
                # those samples before the shared quantizer so a split UV
                # footprint cannot manufacture an unmapped palette key.
                pixels = [sample_triangle(texture, uv, state, x, y, TILE, 1)
                          for y in range(TILE) for x in range(TILE)]
                pixels = [value if value & 0x8000 else 0 for value in pixels]
                palette, mapping = quantize_clut16(pixels)
                fragment["tile_offset"] = len(bank)
                fragment["clut_offset"] = len(clut)
                bank.extend(pack_clut16([mapping[value] for value in pixels]))
                clut.extend(struct.pack(">16H", *palette))
                entries.append({
                    "fragment": len(fragments),
                    "source_primitive": polygon.source,
                    "tile_size": TILE,
                    "tile_offset": fragment["tile_offset"],
                    "clut_offset": fragment["clut_offset"],
                })
            polygon_fragments.append(len(fragments))
            fragments.append(fragment)
        fragment_indices[id(polygon)] = polygon_fragments

    resident = len(bank) + len(clut)
    if resident > MAX_RESIDENT_BYTES:
        raise ValueError(f"16x16 BSP tier exceeds VDP1 budget: {resident}")
    manifest = {
        "schema": "sm64-saturn-bob-bsp-fragment-bank",
        "version": 1,
        "tile_size": TILE,
        "fragment_count": len(fragments),
        "textured_fragment_count": len(entries),
        "texture_bytes": len(bank),
        "clut_bytes": len(clut),
        "resident_bytes": resident,
        "max_resident_bytes": MAX_RESIDENT_BYTES,
        "bsp_sha256": stats.digest,
        "entries": entries,
    }
    scene_out = {
        "schema": "sm64-saturn-bob-bsp-fragment-scene",
        "version": 1,
        "bsp_sha256": stats.digest,
        "uv_mapping": {
            "tile_size": TILE,
            "source_scale": 1,
            "weights": "vdp1_repeated_c",
            "corner_order": "A/B/C/C",
            "uv_interpolation": "exact_rational_before_source_tile_state",
        },
        "fragments": fragments,
        "bsp": _fragment_bsp(root, fragment_indices),
    }
    return bytes(bank), bytes(clut), manifest, scene_out


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--asset-root", type=Path, required=True)
    parser.add_argument("--bank", type=Path, required=True)
    parser.add_argument("--clut", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--scene", type=Path, required=True)
    args = parser.parse_args()
    bank, clut, manifest, scene = bake(
        json.loads(args.input.read_text(encoding="utf-8")), args.asset_root)
    args.bank.parent.mkdir(parents=True, exist_ok=True)
    args.bank.write_bytes(bank)
    args.clut.write_bytes(clut)
    manifest["bank_sha256"] = hashlib.sha256(bank).hexdigest()
    manifest["clut_sha256"] = hashlib.sha256(clut).hexdigest()
    args.manifest.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    args.scene.write_text(json.dumps(scene, indent=2, sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
