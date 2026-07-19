#!/usr/bin/env python3
"""Bake a bounded real Castle Area 1 texture slice into VDP1 UV tiles.

The caller selects only the materials visible to its M3 camera.  Tile size and
source scale are explicit residency/fidelity controls; the policy keeps command
count below the default Yaul VDP1 command partition.
ROM-derived pixels are emitted beneath build/ and must never be committed.
"""
from __future__ import annotations

import argparse
from collections import Counter
from fractions import Fraction
import hashlib
import json
import math
from pathlib import Path

from compile_castle_bsp import expand_render_polygons
from extract_mario_textures import mio0_decode, rom_bytes, saturn_rgb1555
from static_bsp import Node, Polygon, Vertex, build
from vdp1_texture import (
    distorted_sprite_weights,
    downsample_rgb1555,
    repeated_vertex_weights,
)

DEFAULT_TILE = 16
DEFAULT_SELECTED = (
    "inside_09000000", "inside_09001000", "inside_09003800",
    "inside_09004000", "inside_09005000", "inside_09008000",
    "inside_09008800", "inside_castle_seg7_texture_07000800",
    "inside_castle_seg7_texture_07002000",
)
ASSETS = {
    "inside_09000000": "textures/inside/inside_castle_textures.00000.rgba16.png",
    "inside_09001000": "textures/inside/inside_castle_textures.01000.rgba16.png",
    "inside_09003800": "textures/inside/inside_castle_textures.03800.rgba16.png",
    "inside_09004000": "textures/inside/inside_castle_textures.04000.rgba16.png",
    "inside_09005000": "textures/inside/inside_castle_textures.05000.rgba16.png",
    "inside_09008000": "textures/inside/inside_castle_textures.08000.rgba16.png",
    "inside_09008800": "textures/inside/inside_castle_textures.08800.rgba16.png",
    "inside_castle_seg7_texture_07000800": "levels/castle_inside/1.rgba16.png",
    "inside_castle_seg7_texture_07002000": "levels/castle_inside/3.rgba16.png",
}


def rgb555_components(value: int) -> tuple[int, int, int]:
    return value & 0x1F, (value >> 5) & 0x1F, (value >> 10) & 0x1F


def quantize_clut16(pixels: list[int]) -> tuple[list[int], dict[int, int]]:
    """Build a deterministic transparent + 15-color RGB555 material CLUT."""
    histogram = Counter(value for value in pixels if value & 0x8000)
    if not histogram:
        return [0] * 16, {0: 0}
    boxes: list[list[int]] = [sorted(histogram)]
    while len(boxes) < 15:
        candidates = []
        for index, box in enumerate(boxes):
            if len(box) < 2:
                continue
            channels = tuple(zip(*(rgb555_components(value) for value in box)))
            ranges = tuple(max(channel) - min(channel) for channel in channels)
            channel = max(range(3), key=lambda axis: (ranges[axis], -axis))
            population = sum(histogram[value] for value in box)
            candidates.append((ranges[channel] * population, population,
                               -index, channel, index))
        if not candidates:
            break
        _score, _population, _stable, channel, box_index = max(candidates)
        box = sorted(boxes[box_index],
                     key=lambda value: (rgb555_components(value)[channel], value))
        total = sum(histogram[value] for value in box)
        running = 0
        split = 1
        for split, value in enumerate(box, 1):
            running += histogram[value]
            if running * 2 >= total:
                break
        split = min(max(1, split), len(box) - 1)
        boxes[box_index:box_index + 1] = [box[:split], box[split:]]

    palette = [0]
    for box in boxes:
        population = sum(histogram[value] for value in box)
        channels = [rgb555_components(value) for value in box]
        averaged = tuple((sum(component[axis] * histogram[value]
                              for value, component in zip(box, channels)) +
                          population // 2) // population for axis in range(3))
        palette.append(0x8000 | averaged[0] | (averaged[1] << 5) |
                       (averaged[2] << 10))
    palette.extend([palette[-1]] * (16 - len(palette)))

    mapping = {value: min(
        range(1, 16),
        key=lambda index: (sum(
            (left - right) ** 2 for left, right in zip(
                rgb555_components(value), rgb555_components(palette[index]))),
            index),
    ) for value in histogram}
    mapping[0] = 0
    return palette, mapping


def pack_clut16(indices: list[int]) -> list[int]:
    if len(indices) % 2:
        raise ValueError("4-bit VDP1 texture requires an even texel count")
    if any(index < 0 or index > 15 for index in indices):
        raise ValueError("CLUT index outside four-bit domain")
    return [(indices[offset] << 4) | indices[offset + 1]
            for offset in range(0, len(indices), 2)]


def rgba16(rom: bytes, entry: list[object], source_scale: int) -> tuple[int, int, list[int], str, int]:
    width, height, size, regions = entry
    base, relative = regions["us"]
    image = mio0_decode(rom, base)
    data = image[relative:relative + size]
    if len(data) != size:
        raise ValueError("texture range outside decoded US ROM bank")
    pixels = [saturn_rgb1555(int.from_bytes(data[i:i + 2], "big")) for i in range(0, len(data), 2)]
    scaled_width, scaled_height, scaled = downsample_rgb1555(pixels, int(width), int(height), source_scale)
    return scaled_width, scaled_height, scaled, hashlib.sha256(data).hexdigest(), len(data)


def midpoint(a: tuple[int, ...], b: tuple[int, ...]) -> tuple[int, ...]:
    return tuple((left + right) // 2 for left, right in zip(a, b))


def should_subdivide(
        positions: list[tuple[int, int, int]], subdivision: int,
        threshold: int) -> bool:
    """Select the PS1-style large-polygon split without camera-specific data."""
    if subdivision == 4:
        return True
    if threshold <= 0:
        return False
    diagonal_squared = sum(
        (max(point[axis] for point in positions) -
         min(point[axis] for point in positions)) ** 2
        for axis in range(3)
    )
    return diagonal_squared > threshold * threshold


def texture_coordinate(
        raw: float, scale: int, lower: int, extent: int, mask: int,
        shift: int, clamp: bool, mirror: bool) -> int:
    """Apply SM64's Fast3D tile-coordinate state in source-texel space."""
    coordinate = math.floor(raw * scale / (32 * 65536)) - lower // 4
    if 0 < shift <= 10:
        coordinate >>= shift
    elif shift > 10:
        coordinate <<= 16 - shift
    if clamp:
        return max(0, min(extent - 1, coordinate))
    period = 1 << mask if mask else extent
    if period <= 0:
        raise ValueError("texture wrap period must be positive")
    if mirror:
        wrapped = coordinate % (period * 2)
        return period * 2 - 1 - wrapped if wrapped >= period else wrapped
    return coordinate % period


def sample_raw(
        texture: tuple[int, int, list[int], str, int], raw_u: float,
        raw_v: float, tile_state: dict[str, object], source_scale: int) -> int:
    # Resolve the complete render tile before downscaling.  The previous
    # prototype divided first and inferred both axes' clamp state from the
    # whole gsDPSetTile macro; that turned Castle's T-clamp/S-wrap walls into
    # horizontally clamped streaks.
    width, height, pixels, _digest, _source_bytes = texture
    tile_width, tile_height = int(tile_state["width"]), int(tile_state["height"])
    source_u = texture_coordinate(
        raw_u, int(tile_state["sp_scale_s"]), int(tile_state["uls"]), tile_width,
        int(tile_state["mask_s"]), int(tile_state["shift_s"]),
        bool(tile_state["clamp_s"]), bool(tile_state["mirror_s"]),
    )
    source_v = texture_coordinate(
        raw_v, int(tile_state["sp_scale_t"]), int(tile_state["ult"]), tile_height,
        int(tile_state["mask_t"]), int(tile_state["shift_t"]),
        bool(tile_state["clamp_t"]), bool(tile_state["mirror_t"]),
    )
    u, v = source_u // source_scale, source_v // source_scale
    if tile_width // source_scale > width or tile_height // source_scale > height:
        raise ValueError("render tile extent exceeds ROM texture dimensions")
    return pixels[v * width + u]


def sample_triangle(
        texture: tuple[int, int, list[int], str, int],
        uv: tuple[tuple[int, int], tuple[int, int], tuple[int, int]],
        tile_state: dict[str, object], x: int, y: int, tile: int,
        source_scale: int) -> int:
    # VDP1 textured commands are quads. Match the source-Mario lowering:
    # bake the complete affine repeated-C tile.  The destination command is
    # already a collapsed triangle (A/B/C/C); masking a diagonal in texture
    # space discards valid source coverage and creates view-dependent seams
    # when the triangle is projected by VDP1.
    a, b, c = repeated_vertex_weights(x, y, tile, tile)
    return sample_raw(
        texture,
        a * uv[0][0] + b * uv[1][0] + c * uv[2][0],
        a * uv[0][1] + b * uv[1][1] + c * uv[2][1],
        tile_state, source_scale,
    )


def sample_quad(
        texture: tuple[int, int, list[int], str, int],
        uv: tuple[tuple[int, int], tuple[int, int], tuple[int, int], tuple[int, int]],
        tile_state: dict[str, object], x: int, y: int, tile: int,
        source_scale: int) -> int:
    a, b, c, d = distorted_sprite_weights(x, y, tile, tile)
    return sample_raw(
        texture,
        a * uv[0][0] + b * uv[1][0] + c * uv[2][0] + d * uv[3][0],
        a * uv[0][1] + b * uv[1][1] + c * uv[2][1] + d * uv[3][1],
        tile_state, source_scale,
    )


def triangulate_polygon(polygon: Polygon) -> list[Polygon]:
    """Lower an arbitrary convex BSP fragment to VDP1 repeated-vertex tris."""
    if len(polygon.vertices) == 3:
        return [polygon]
    output = []
    for index in range(1, len(polygon.vertices) - 1):
        output.append(Polygon(
            (polygon.vertices[0], polygon.vertices[index], polygon.vertices[index + 1]),
            source=polygon.source, root=polygon.root, layer=polygon.layer,
            texture=polygon.texture,
        ))
    return output


def adaptive_subdivide_triangle(polygon: Polygon, threshold: int) -> list[Polygon]:
    """Bisect a VDP1 triangle's longest world-space edge until it fits.

    This deliberately runs after BSP construction: every child remains coplanar
    and in the same BSP node, while Vertex.between() interpolates the original
    Fast3D attributes exactly.  It is the Saturn-shaped counterpart to the PS1
    port's static longest-edge preprocessor, not camera-authored replacement
    geometry.
    """
    if len(polygon.vertices) != 3:
        raise ValueError("adaptive subdivision requires a triangle")
    if threshold <= 0:
        return [polygon]

    vertices = polygon.vertices
    edges = ((0, 1), (1, 2), (2, 0))
    lengths = tuple(sum(
        (vertices[right].position[axis] - vertices[left].position[axis]) ** 2
        for axis in range(3)
    ) for left, right in edges)
    edge_index = max(range(3), key=lambda index: (lengths[index], -index))
    if lengths[edge_index] <= threshold * threshold:
        return [polygon]

    midpoint_vertex = vertices[edges[edge_index][0]].between(
        vertices[edges[edge_index][1]], Fraction(1, 2))
    if edge_index == 0:
        children = (
            (vertices[0], midpoint_vertex, vertices[2]),
            (midpoint_vertex, vertices[1], vertices[2]),
        )
    elif edge_index == 1:
        children = (
            (vertices[0], vertices[1], midpoint_vertex),
            (vertices[0], midpoint_vertex, vertices[2]),
        )
    else:
        children = (
            (vertices[0], vertices[1], midpoint_vertex),
            (midpoint_vertex, vertices[1], vertices[2]),
        )

    output: list[Polygon] = []
    for child_vertices in children:
        child = Polygon(
            child_vertices, source=polygon.source, root=polygon.root,
            layer=polygon.layer, texture=polygon.texture,
        )
        output.extend(adaptive_subdivide_triangle(child, threshold))
    return output


def adaptive_subdivide_quad(polygon: Polygon, threshold: int) -> list[Polygon]:
    """Split a large convex source quad into four attribute-preserving quads."""
    if len(polygon.vertices) != 4:
        raise ValueError("adaptive quad subdivision requires a quad")
    if threshold <= 0:
        return [polygon]
    vertices = polygon.vertices
    edges = ((0, 1), (1, 2), (2, 3), (3, 0))
    lengths = tuple(sum(
        (vertices[right].position[axis] - vertices[left].position[axis]) ** 2
        for axis in range(3)
    ) for left, right in edges)
    diagonal = max(
        sum((vertices[2].position[axis] - vertices[0].position[axis]) ** 2
            for axis in range(3)),
        sum((vertices[3].position[axis] - vertices[1].position[axis]) ** 2
            for axis in range(3)),
    )
    if max(max(lengths), diagonal) <= threshold * threshold:
        return [polygon]

    def between(left: Vertex, right: Vertex) -> Vertex:
        return left.between(right, Fraction(1, 2))

    mid01, mid12 = between(vertices[0], vertices[1]), between(vertices[1], vertices[2])
    mid23, mid30 = between(vertices[2], vertices[3]), between(vertices[3], vertices[0])
    center = between(mid01, mid23)
    children = (
        (vertices[0], mid01, center, mid30),
        (mid01, vertices[1], mid12, center),
        (center, mid12, vertices[2], mid23),
        (mid30, center, mid23, vertices[3]),
    )
    output: list[Polygon] = []
    for child_vertices in children:
        child = Polygon(
            child_vertices, source=polygon.source, root=polygon.root,
            layer=polygon.layer, texture=polygon.texture,
        )
        output.extend(adaptive_subdivide_quad(child, threshold))
    return output


def lower_polygon(polygon: Polygon, threshold: int) -> list[Polygon]:
    # VDP1 has a native distorted-sprite quad. Preserve source/BSP convex
    # quads so their four Fast3D attributes remain one affine primitive; the
    # old unconditional triangulation turned every wall/floor quad into a
    # degenerate textured triangle and multiplied both command count and UV
    # distortion.
    if len(polygon.vertices) == 4:
        return adaptive_subdivide_quad(polygon, threshold)
    output: list[Polygon] = []
    for triangle in triangulate_polygon(polygon):
        output.extend(adaptive_subdivide_triangle(triangle, threshold))
    return output


def flatten_bsp(
        root: Node, subdivision_threshold: int = 0,
) -> tuple[list[dict[str, object]], list[Polygon], int, int]:
    """Serialize nodes preorder and group their VDP1 triangles contiguously."""
    nodes: list[dict[str, object] | None] = []
    polygons: list[Polygon] = []
    triangle_count = 0
    polygon_count = 0

    def visit(node: Node | None) -> int:
        nonlocal triangle_count, polygon_count
        if node is None:
            return -1
        index = len(nodes)
        nodes.append(None)
        start = len(polygons)
        for polygon in node.coplanar:
            polygon_count += 1
            triangle_count += len(polygon.vertices) - 2
            polygons.extend(lower_polygon(polygon, subdivision_threshold))
        count = len(polygons) - start
        front, back = visit(node.front), visit(node.back)
        nodes[index] = {
            "plane": node.plane,
            "front": front,
            "back": back,
            "tile_start": start,
            "tile_count": count,
        }
        return index

    visit(root)
    return [node for node in nodes if node is not None], polygons, triangle_count, polygon_count


def nearest_integer(value: Fraction) -> int:
    """Round a rational symmetrically for the source's int16 world domain."""
    if value >= 0:
        return (value.numerator * 2 + value.denominator) // (2 * value.denominator)
    positive = -value
    return -((positive.numerator * 2 + positive.denominator) //
             (2 * positive.denominator))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--intake", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--texture", action="append", dest="textures")
    parser.add_argument("--tile", type=int, default=DEFAULT_TILE)
    parser.add_argument("--source-scale", type=int, default=1)
    parser.add_argument("--texture-format", choices=("rgb1555", "clut16"),
                        default="clut16")
    parser.add_argument("--subdivision", type=int, choices=(1, 4), default=1)
    parser.add_argument("--subdivision-threshold", type=int, default=0,
                        help="split triangles whose source-space diagonal exceeds this value")
    parser.add_argument("--max-tiles", type=int, default=2400,
                        help="reject a generated bank larger than the measured VDP1 budget")
    parser.add_argument("--ordering", choices=("depth", "bsp"), default="bsp",
                        help="compile a camera-independent static BSP or retain depth-sort input")
    parser.add_argument("--bsp-candidate-limit", type=int, default=32)
    parser.add_argument("--bsp-split-weight", type=int, default=8)
    args = parser.parse_args()
    rom, asset_map = rom_bytes(args.rom), json.loads(args.assets.read_text(encoding="utf-8"))
    scene = json.loads(args.intake.read_text(encoding="utf-8"))
    selected = tuple(args.textures or DEFAULT_SELECTED)
    if args.tile < 8 or args.tile % 8 != 0:
        raise ValueError("--tile must be an eight-pixel multiple")
    if args.source_scale < 1 or args.source_scale not in (1, 2, 4):
        raise ValueError("--source-scale must be 1, 2, or 4")
    unknown = set(selected) - set(ASSETS)
    if unknown:
        raise ValueError(f"unsupported Castle texture(s): {sorted(unknown)}")
    texture_data = {name: rgba16(rom, asset_map[ASSETS[name]], args.source_scale) for name in selected}
    cluts: list[list[int]] = []
    clut_maps: list[dict[int, int]] = []
    if args.texture_format == "clut16":
        for name in selected:
            palette, mapping = quantize_clut16(texture_data[name][2])
            cluts.append(palette)
            clut_maps.append(mapping)
    clut_indices = {name: index for index, name in enumerate(selected)}
    selected_indices = {scene["textures"].index(name) for name in selected}
    starts = [0xFFFF] * int(scene["primitive_count"])
    counts = [0] * int(scene["primitive_count"])
    positions: list[tuple[tuple[int, int, int], ...]] = []
    tile_primitives: list[int] = []
    tile_cluts: list[int] = []
    texels: list[int] = []
    render_polygons = expand_render_polygons(scene, args.subdivision)
    bsp_nodes: list[dict[str, object]] = []
    bsp_stats = None
    if args.ordering == "bsp":
        static = [polygon for polygon in render_polygons if polygon.layer != 1]
        decals = [polygon for polygon in render_polygons if polygon.layer == 1]
        bsp_root, bsp_stats = build(
            static, candidate_limit=args.bsp_candidate_limit,
            split_weight=args.bsp_split_weight,
        )
        bsp_nodes, output_polygons, post_bsp_triangle_count, post_bsp_polygon_count = flatten_bsp(
            bsp_root, args.subdivision_threshold)
        decal_start = len(output_polygons)
        for polygon in decals:
            post_bsp_polygon_count += 1
            post_bsp_triangle_count += len(polygon.vertices) - 2
            output_polygons.extend(lower_polygon(
                polygon, args.subdivision_threshold))
        decal_count = len(output_polygons) - decal_start
    else:
        output_polygons = [fragment for polygon in render_polygons
                           for fragment in lower_polygon(
                               polygon, args.subdivision_threshold)]
        post_bsp_triangle_count = sum(
            len(polygon.vertices) - 2 for polygon in render_polygons)
        post_bsp_polygon_count = len(render_polygons)
        decal_start = 0
        decal_count = 0

    if args.max_tiles > 0 and len(output_polygons) > args.max_tiles:
        raise ValueError(
            f"adaptive Castle bank needs {len(output_polygons)} tiles; "
            f"measured VDP1 budget is {args.max_tiles}")

    paired_quads = 0
    maximum_position_error = Fraction(0)
    for polygon in output_polygons:
        index = polygon.source
        primitive = scene["primitives"][index]
        texture_index = int(primitive["texture_index"])
        if texture_index not in selected_indices:
            raise ValueError(f"BSP output primitive {index} has an unselected texture")
        if starts[index] == 0xFFFF:
            starts[index] = len(positions)
        counts[index] += 1
        source_index = int(primitive["first_triangle"])
        tile_state = scene["tile_state"][source_index]
        if tile_state is None or "width" not in tile_state or "height" not in tile_state:
            raise ValueError(f"primitive {index} has no complete Fast3D render-tile state")
        texture = texture_data[scene["textures"][texture_index]]
        texture_name = scene["textures"][texture_index]
        clut_index = clut_indices[texture_name]
        rounded: list[tuple[int, int, int]] = []
        for vertex in polygon.vertices:
            point = tuple(nearest_integer(value) for value in vertex.position)
            maximum_position_error = max(maximum_position_error, *(
                abs(value - rounded_value)
                for value, rounded_value in zip(vertex.position, point)
            ))
            if any(value < -32768 or value > 32767 for value in point):
                raise ValueError("BSP fragment left the int16 Castle world domain")
            rounded.append(point)
        uv = tuple(tuple(value for value in vertex.attributes) for vertex in polygon.vertices)
        tile_primitives.append(index)
        tile_cluts.append(clut_index)
        if len(polygon.vertices) == 4:
            positions.append(tuple(rounded))
            sampled = [sample_quad(texture, uv, tile_state, x, y, args.tile,
                                   args.source_scale)
                       for y in range(args.tile) for x in range(args.tile)]
            paired_quads += 1
        elif len(polygon.vertices) == 3:
            extrapolated = tuple(
                rounded[0][axis] + rounded[2][axis] - rounded[1][axis]
                for axis in range(3))
            if any(value < -32768 or value > 32767 for value in extrapolated):
                raise ValueError("triangle affine companion left int16 Castle world domain")
            positions.append((*rounded, extrapolated))
            sampled = [sample_triangle(texture, uv, tile_state, x, y,
                                       args.tile, args.source_scale)
                       for y in range(args.tile) for x in range(args.tile)]
        else:
            raise ValueError("BSP lowering must produce only triangles or quads")
        if args.texture_format == "clut16":
            mapping = clut_maps[clut_index]
            texels.extend(0 if not (value & 0x8000) else mapping[value]
                          for value in sampled)
        else:
            texels.extend(sampled)
    textured_count = sum(value != 0xFFFF for value in starts)
    bytes_per_tile = (args.tile * args.tile // 2 if args.texture_format == "clut16"
                      else args.tile * args.tile * 2)
    lines = ["/* Local ROM-derived output: do not commit. */", "#pragma once", "#include <stdint.h>", f"#define SM64_CASTLE_UV_TILE_WIDTH {args.tile}U", f"#define SM64_CASTLE_UV_TILE_BYTES {bytes_per_tile}U", f"#define SM64_CASTLE_UV_TEXTURE_FORMAT_CLUT16 {1 if args.texture_format == 'clut16' else 0}U", f"#define SM64_CASTLE_UV_CLUT_COUNT {len(cluts)}U", "#define SM64_CASTLE_UV_TILE_NONE 0xFFFFU", f"#define SM64_CASTLE_UV_TEXTURED_PRIMITIVE_COUNT {textured_count}U", f"#define SM64_CASTLE_UV_PAIRED_QUAD_COUNT {paired_quads}U", f"#define SM64_CASTLE_UV_TILE_COUNT {len(positions)}U", f"#define SM64_CASTLE_BSP_NODE_COUNT {len(bsp_nodes)}U", f"#define SM64_CASTLE_BSP_DECAL_START {decal_start}U", f"#define SM64_CASTLE_BSP_DECAL_COUNT {decal_count}U", "static const uint16_t sm64_castle_uv_tile_start[SM64_CASTLE_AREA1_PRIMITIVE_COUNT] = {"]
    lines.extend("    " + ", ".join(f"{value}U" for value in starts[offset:offset + 16]) + "," for offset in range(0, len(starts), 16))
    lines.append("};")
    if bsp_nodes:
        lines.append("static const int32_t sm64_castle_bsp_normal[SM64_CASTLE_BSP_NODE_COUNT][3] = {")
        lines.extend("    {" + ", ".join(str(value) for value in node["plane"][:3]) + "}," for node in bsp_nodes)
        lines.append("};")
        lines.append("static const int64_t sm64_castle_bsp_distance[SM64_CASTLE_BSP_NODE_COUNT] = {")
        lines.extend("    INT64_C(" + str(node["plane"][3]) + ")," for node in bsp_nodes)
        lines.append("};")
        lines.append("static const int16_t sm64_castle_bsp_children[SM64_CASTLE_BSP_NODE_COUNT][2] = {")
        lines.extend(f"    {{{node['front']}, {node['back']}}}," for node in bsp_nodes)
        lines.append("};")
        lines.append("static const uint16_t sm64_castle_bsp_tile_range[SM64_CASTLE_BSP_NODE_COUNT][2] = {")
        lines.extend(f"    {{{node['tile_start']}U, {node['tile_count']}U}}," for node in bsp_nodes)
        lines.append("};")
    lines.append("static const uint16_t sm64_castle_uv_tile_primitive[SM64_CASTLE_UV_TILE_COUNT] = {")
    lines.extend("    " + ", ".join(f"{value}U" for value in tile_primitives[offset:offset + 16]) + "," for offset in range(0, len(tile_primitives), 16))
    lines.append("};")
    lines.append("static const uint8_t sm64_castle_uv_tile_clut[SM64_CASTLE_UV_TILE_COUNT] = {")
    lines.extend("    " + ", ".join(f"{value}U" for value in tile_cluts[offset:offset + 32]) + "," for offset in range(0, len(tile_cluts), 32))
    lines.append("};")
    lines.append("static const uint16_t sm64_castle_uv_tile_count[SM64_CASTLE_AREA1_PRIMITIVE_COUNT] = {")
    lines.extend("    " + ", ".join(f"{value}U" for value in counts[offset:offset + 16]) + "," for offset in range(0, len(counts), 16))
    lines.append("};")
    lines.append("static const int16_t sm64_castle_uv_positions[SM64_CASTLE_UV_TILE_COUNT][4][3] = {")
    lines.extend("    {" + ", ".join(f"{{{x}, {y}, {z}}}" for x, y, z in quad) + "}," for quad in positions)
    lines.append("};")
    if args.texture_format == "clut16":
        packed = pack_clut16(texels)
        lines.append("static const uint16_t sm64_castle_uv_cluts[SM64_CASTLE_UV_CLUT_COUNT][16] = {")
        lines.extend("    {" + ", ".join(f"0x{value:04X}" for value in palette) + "}," for palette in cluts)
        lines.append("};")
        lines.append("static const uint8_t sm64_castle_uv_tiles[SM64_CASTLE_UV_TILE_COUNT][SM64_CASTLE_UV_TILE_BYTES] = {")
        for offset in range(0, len(packed), bytes_per_tile): lines.append("    {" + ", ".join(f"0x{value:02X}" for value in packed[offset:offset + bytes_per_tile]) + "},")
        emitted_texture_bytes = len(packed)
    else:
        lines.append("static const uint16_t sm64_castle_uv_tiles[SM64_CASTLE_UV_TILE_COUNT][SM64_CASTLE_UV_TILE_WIDTH * SM64_CASTLE_UV_TILE_WIDTH] = {")
        for offset in range(0, len(texels), args.tile * args.tile): lines.append("    {" + ", ".join(f"0x{value:04X}" for value in texels[offset:offset + args.tile * args.tile]) + "},")
        emitted_texture_bytes = len(texels) * 2
    lines.append("};")
    args.output.parent.mkdir(parents=True, exist_ok=True); args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    report = {"source": scene["source"], "selected_textures": list(selected), "selected_primitives": textured_count, "source_triangles": int(scene["triangle_count"]), "render_primitives": int(scene["primitive_count"]), "paired_textured_quads": paired_quads, "tile": [args.tile, args.tile], "source_scale": args.source_scale, "source_filter": "RGB1555 box filter with majority alpha", "texture_format": args.texture_format, "clut_count": len(cluts), "source_texture_bytes": sum(data[4] for data in texture_data.values()), "resampled_source_bytes": sum(data[0] * data[1] * 2 for data in texture_data.values()), "ordering": args.ordering, "subdivision": args.subdivision, "subdivision_threshold": args.subdivision_threshold, "subdivision_policy": "post-BSP recursive source-space quad/triangle subdivision with exact Fast3D attribute interpolation", "tile_budget": args.max_tiles, "post_bsp_triangles_before_adaptive_split": post_bsp_triangle_count, "post_bsp_polygons_before_adaptive_split": post_bsp_polygon_count, "adaptive_split_events": max(0, len(positions) - post_bsp_polygon_count), "tile_count": len(positions), "texture_bytes": emitted_texture_bytes, "command_estimate": 2 + len(positions) + (int(scene["primitive_count"]) - textured_count) + 1, "texture_state": "Fast3D image/load-tile/TMEM/render-tile v2: independent S/T clamp, mirror, mask, shift, tile origin/extent, SP scale, and retained LOD bindings", "uv_sampling": "Fast3D s10.5 sampling resolved in source-texel space; VDP1 character corners use A/B/C/D, with source triangles using the complete affine repeated-C tile (no diagonal coverage mask)", "bsp": None if bsp_stats is None else {"policy": "exact rational offline splits before VDP1 lowering", "node_count": bsp_stats.node_count, "split_events": bsp_stats.split_events, "input_polygons": bsp_stats.input_polygons, "output_convex_polygons": bsp_stats.output_polygons, "max_depth": bsp_stats.max_depth, "max_fragment_vertices": bsp_stats.max_vertices, "decal_start": decal_start, "decal_count": decal_count, "position_quantization": "nearest source world unit", "maximum_position_error": [maximum_position_error.numerator, maximum_position_error.denominator], "deterministic_sha256": bsp_stats.digest}, "rom_sha256": hashlib.sha256(rom).hexdigest(), "texture_sha256": {name: data[3] for name, data in texture_data.items()}}
    args.report.parent.mkdir(parents=True, exist_ok=True); args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
