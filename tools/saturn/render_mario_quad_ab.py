#!/usr/bin/env python3
"""Render A/B contact sheets of the quad-remeshed Mario actor.

The quad counterpart of ``render_mario_ab.py``, and deliberately the same
instrument: same cell size, same camera yaws, same fitted bounds, same
dependency-free PNG encoder and z-buffered rasteriser, so a sheet from this
tool can be laid beside ``mario-decimation-neutral-solid.png`` and compared
directly.

Two differences from the triangle tool, both forced by what remeshing does:

1. **Each level has its own vertex table and its own pose bank.** A remesher
   invents vertices, so the 107 shipped pose frames do not apply to a remeshed
   level; ``quad_remesh_mario.py`` transfers them by barycentric projection and
   writes the transferred banks into the levels document. The walking sheets
   here are therefore rendered from *transferred* poses, which is exactly the
   claim under test.
2. **Every level is framed with the original level's fitted bounds** for a
   given yaw, rather than its own, so a silhouette difference on the page is a
   silhouette difference in the mesh.

What these renders are not
--------------------------
They are host renders of the *geometry*, flat-shaded per primitive from the
baked RGB555 material table with a fixed light. **A quad is drawn here as two
flat triangles. VDP1 does not do that** -- it rasterises a four-vertex command
as a bilinear patch, so a non-planar quad warps on hardware in a way this
renderer cannot show. ``quad_remesh_mario.py`` reports the planarity
distribution precisely because these pictures cannot. Colour, Gouraud banding
and texture are absent here as they were in T2.21.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from render_mario_ab import (  # noqa: E402
    BACKGROUND,
    CELL_HEIGHT,
    CELL_WIDTH,
    INK,
    LABEL_HEIGHT,
    MARGIN,
    PANEL,
    WIRE,
    Image,
    draw_line,
    draw_triangle,
    fit,
    rotate,
    shade,
    text_width,
)

REMESHED_TINT = (90, 210, 160)
UNTOUCHED_TINT = (235, 150, 90)


def render_cell(level: dict, positions: list[list[int]], material_rgb: list[list[int]],
                yaw_degrees: float, pitch_degrees: float, transform,
                wireframe: bool, mode: str) -> Image:
    image = Image(CELL_WIDTH, CELL_HEIGHT - LABEL_HEIGHT, PANEL)
    depth = [float("inf")] * (image.width * image.height)
    yaw = math.radians(yaw_degrees)
    pitch = math.radians(pitch_degrees)
    scale, centre_x, centre_y = transform
    origin_x = image.width * 0.5
    origin_y = image.height * 0.5

    view = []
    for point in positions:
        x, y, z = rotate(point, yaw, pitch)
        view.append((origin_x + (x - centre_x) * scale,
                     origin_y - (y - centre_y) * scale, z))

    order = []
    for primitive in level["primitives"]:
        indices = primitive["indices"]
        corners = indices if primitive["quad"] and indices[3] != indices[2] else indices[:3]
        order.append((primitive, corners))

    for primitive, corners in order:
        a, b, c = (view[index] for index in corners[:3])
        normal = (
            (b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1]),
            (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]),
            (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]),
        )
        colour = shade(material_rgb[primitive["material"]], normal)
        if mode == "provenance":
            tint = REMESHED_TINT if primitive["remeshed"] else UNTOUCHED_TINT
            colour = shade([tint[0] // 8, tint[1] // 8, tint[2] // 8], normal)
        elif mode == "textured":
            if primitive["textured"]:
                colour = shade([31, 0, 26], normal)
            else:
                grey = sum(colour) // 5
                colour = (grey, grey, grey)
        elif primitive["textured"]:
            colour = tuple(min(255, int(value * 0.72 + 60)) for value in colour)
        fans = [(0, 1, 2)] if len(corners) == 3 else [(0, 1, 2), (0, 2, 3)]
        for fan in fans:
            draw_triangle(image, depth, [view[corners[i]] for i in fan], colour)

    if wireframe:
        for primitive, corners in order:
            for i in range(len(corners)):
                draw_line(image, view[corners[i]],
                          view[corners[(i + 1) % len(corners)]], WIRE)
    return image


def build_sheet(document: dict, bank: str, frame: int, pose_name: str,
                angles: list[float], pitch: float, wireframe: bool,
                stats: dict, output: Path, mode: str = "solid") -> None:
    levels = document["levels"]
    material_rgb = document["material_rgb"]
    reference_positions = levels[0][bank][frame]

    transforms = [
        fit([rotate(point, math.radians(angle), math.radians(pitch))
             for point in reference_positions])
        for angle in angles
    ]

    columns = len(angles)
    rows = len(levels)
    header = 34
    width = MARGIN + columns * (CELL_WIDTH + MARGIN)
    height = header + rows * (CELL_HEIGHT + MARGIN) + MARGIN
    sheet = Image(width, height, BACKGROUND)
    title = f"MARIO ACTOR QUAD REMESH AB - POSE {pose_name}"
    if wireframe:
        title += " - WIREFRAME"
    if mode == "provenance":
        title = f"GREEN = REMESHED QUADS, ORANGE = UNTOUCHED - POSE {pose_name}"
    elif mode == "textured":
        title = f"THE 50 TEXTURED PRIMITIVES ARE FIXED COST - POSE {pose_name}"
    sheet.text(MARGIN, 8, title, INK, 2)

    for column, angle in enumerate(angles):
        label = f"YAW {int(angle)}"
        x = MARGIN + column * (CELL_WIDTH + MARGIN)
        sheet.text(x + (CELL_WIDTH - text_width(label)) // 2, 24, label, INK, 1)

    for row, level in enumerate(levels):
        y = header + row * (CELL_HEIGHT + MARGIN)
        positions = level[bank][frame]
        for column, angle in enumerate(angles):
            x = MARGIN + column * (CELL_WIDTH + MARGIN)
            cell = render_cell(level, positions, material_rgb, angle, pitch,
                               transforms[column], wireframe, mode)
            sheet.blit(cell, x, y)
        entry = stats[level["name"]]
        caption = (
            f"{level['name'].replace('_', ' ')}  "
            f"PRIM {entry['primitive_count']}  CMD {entry['vdp1_command_count']}  "
            f"QUAD {entry['quad_count']}  VTX {entry['referenced_vertex_count']}  "
            f"{entry['bytes']['total'] // 1024}KB"
        )
        sheet.text(MARGIN + 2, y + CELL_HEIGHT - LABEL_HEIGHT + 2, caption, INK, 1)

    sheet.write_png(output)
    print(f"wrote {output}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--levels", type=Path, required=True)
    parser.add_argument("--stats", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    document = json.loads(args.levels.read_text())
    report = json.loads(args.stats.read_text())
    stats = {entry["name"]: entry for entry in report["levels"]}

    angles = [0.0, 45.0, 90.0, 200.0]
    poses = [
        ("NEUTRAL", "animation", 0),
        ("WALK F20", "walking", 20),
        ("WALK F48", "walking", 48),
    ]
    for pose_name, bank, frame in poses:
        slug = pose_name.lower().replace(" ", "-")
        build_sheet(document, bank, frame, pose_name, angles, 12.0, False, stats,
                    args.output_dir / f"mario-quadremesh-{slug}-solid.png")
    build_sheet(document, "animation", 0, "NEUTRAL", angles, 12.0, True, stats,
                args.output_dir / "mario-quadremesh-neutral-wireframe.png")
    build_sheet(document, "walking", 20, "WALK F20", angles, 12.0, True, stats,
                args.output_dir / "mario-quadremesh-walk-f20-wireframe.png")
    build_sheet(document, "animation", 0, "NEUTRAL", angles, 12.0, False, stats,
                args.output_dir / "mario-quadremesh-provenance.png", mode="provenance")
    build_sheet(document, "animation", 0, "NEUTRAL", angles, 12.0, False, stats,
                args.output_dir / "mario-quadremesh-textured-fixed-cost.png",
                mode="textured")
    print("done")


if __name__ == "__main__":
    main()
