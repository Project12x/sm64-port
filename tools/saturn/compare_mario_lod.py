#!/usr/bin/env python3
"""Measure the triangle-decimation and quad-remesh Mario levels on one metric.

T2.21 quoted meshoptimizer's own ``result_error``, which is normalised by the
*per-job subset* extent and is not produced at all by a remesher.  Comparing
the two approaches therefore needs a metric that is computed the same way for
both, from geometry alone.

The metric here is surface deviation: every output face is sampled at its
centroid and its edge midpoints, and each sample's distance to the **original**
Mario surface is measured with an exact point-triangle test.  It is reported in
mesh units and as a fraction of the whole-mesh 150-unit axis extent, so the two
approaches, and the shipped mesh, sit in one table.

Sampling only the output faces measures one direction of the Hausdorff
distance.  That is the direction that matters here -- it answers "how far does
the simplified surface stray from the original" -- but it is not symmetric and
will not by itself detect a feature of the original that vanished entirely.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))

from decimate_mario_actor import load_mesh, primitive_triangles  # noqa: E402
from quad_remesh_mario import closest_point_on_triangle  # noqa: E402


def original_surface(mesh: dict) -> tuple[list[list[float]], list[list[int]], float]:
    positions = [[float(value) for value in point] for point in mesh["vertices"]]
    triangles = [
        list(corners)
        for primitive in mesh["primitives"]
        for corners in primitive_triangles(primitive)
    ]
    extent = max(
        max(point[axis] for point in positions) - min(point[axis] for point in positions)
        for axis in range(3)
    )
    return positions, triangles, extent


def deviation(samples: list[list[float]], positions: list[list[float]],
              triangles: list[list[int]]) -> tuple[float, float]:
    if not samples:
        return 0.0, 0.0
    distances = []
    for point in samples:
        best = min(
            closest_point_on_triangle(
                point, positions[t[0]], positions[t[1]], positions[t[2]]
            )[0]
            for t in triangles
        )
        distances.append(math.sqrt(best))
    return max(distances), math.sqrt(sum(d * d for d in distances) / len(distances))


def face_samples(corners: list[list[float]]) -> list[list[float]]:
    count = len(corners)
    centroid = [sum(c[axis] for c in corners) / count for axis in range(3)]
    samples = [centroid]
    for index in range(count):
        following = corners[(index + 1) % count]
        samples.append(
            [(corners[index][axis] + following[axis]) / 2.0 for axis in range(3)]
        )
    return samples


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--header", type=Path,
        default=Path("src/port/saturn/gfx/saturn_mario_actor_mesh.h"),
    )
    parser.add_argument("--decimation-levels", type=Path, required=True)
    parser.add_argument("--quad-levels", type=Path, required=True)
    parser.add_argument("--decimation-report", type=Path, required=True)
    parser.add_argument("--quad-report", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    mesh = load_mesh(args.header)
    positions, triangles, extent = original_surface(mesh)

    rows = []

    decimation = json.loads(args.decimation_levels.read_text())
    decimation_stats = {
        entry["name"]: entry
        for entry in json.loads(args.decimation_report.read_text())["levels"]
    }
    base = [[float(v) for v in point] for point in decimation["vertices"]]
    for level in decimation["levels"]:
        samples = []
        for primitive in level["primitives"]:
            indices = primitive["indices"]
            corners = indices if indices[3] != indices[2] else indices[:3]
            samples.extend(face_samples([base[index] for index in corners]))
        maximum, rms = deviation(samples, positions, triangles)
        stats = decimation_stats[level["name"]]
        rows.append(
            {
                "approach": "triangle_decimation",
                "level": level["name"],
                "primitive_count": stats["primitive_count"],
                "quad_count": stats["quad_count"],
                "vdp1_command_count": stats["vdp1_command_count"],
                "referenced_vertex_count": stats["referenced_vertex_count"],
                "bytes": stats["bytes"]["total"],
                "sample_count": len(samples),
                "max_deviation": maximum,
                "rms_deviation": rms,
                "max_relative_whole_extent": maximum / extent,
                "rms_relative_whole_extent": rms / extent,
            }
        )

    quad = json.loads(args.quad_levels.read_text())
    quad_stats = {
        entry["name"]: entry
        for entry in json.loads(args.quad_report.read_text())["levels"]
    }
    for level in quad["levels"]:
        neutral = [[float(v) for v in point] for point in level["animation"][0]]
        samples = []
        for primitive in level["primitives"]:
            indices = primitive["indices"]
            corners = indices if primitive["quad"] and indices[3] != indices[2] else indices[:3]
            samples.extend(face_samples([neutral[index] for index in corners]))
        maximum, rms = deviation(samples, positions, triangles)
        stats = quad_stats[level["name"]]
        rows.append(
            {
                "approach": "quad_remesh",
                "level": level["name"],
                "primitive_count": stats["primitive_count"],
                "quad_count": stats["quad_count"],
                "vdp1_command_count": stats["vdp1_command_count"],
                "referenced_vertex_count": stats["referenced_vertex_count"],
                "bytes": stats["bytes"]["total"],
                "sample_count": len(samples),
                "max_deviation": maximum,
                "rms_deviation": rms,
                "max_relative_whole_extent": maximum / extent,
                "rms_relative_whole_extent": rms / extent,
            }
        )

    payload = {
        "schema": "sm64-saturn-mario-lod-comparison",
        "version": 1,
        "metric": (
            "one-sided surface deviation: face centroids and edge midpoints of "
            "each level, measured to the original 788-triangle Mario surface"
        ),
        "whole_mesh_axis_extent": extent,
        "rows": rows,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")

    print(f"{'approach':<20} {'level':<14} {'cmd':>5} {'vtx':>5} {'KB':>6} "
          f"{'maxdev':>7} {'max%':>6} {'rms':>6} {'rms%':>6}")
    for row in rows:
        print(
            f"{row['approach']:<20} {row['level']:<14} "
            f"{row['vdp1_command_count']:>5} {row['referenced_vertex_count']:>5} "
            f"{row['bytes'] // 1024:>6} "
            f"{row['max_deviation']:>7.2f} {100 * row['max_relative_whole_extent']:>5.2f}% "
            f"{row['rms_deviation']:>6.2f} {100 * row['rms_relative_whole_extent']:>5.2f}%"
        )


if __name__ == "__main__":
    main()
