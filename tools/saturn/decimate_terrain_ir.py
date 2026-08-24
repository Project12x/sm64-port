"""Rewrite a level's mesh IR through the meshoptimizer decimator (S7.1).

The C++ tool (decimate_terrain_mesh.cpp) owns the geometry decision and returns
a surviving triangle list; this owns the document, because material/tile/source
metadata is the bake's to understand, not the decimator's.

Every vertex index belongs to exactly one material, because the source display
lists duplicate their vertices at material boundaries. That makes a surviving
triangle's material readable from its vertices -- but ONLY as long as an edge
collapse cannot move a triangle from one material's island onto another's.
Index-disjointness does not give that guarantee: meshoptimizer welds coincident
positions to build its edge graph, so the duplicated seam vertices are one
vertex to the simplifier and a cross-material collapse costs zero geometric
error. The decimator's position-keyed vertex_lock is what actually prevents it.

This script therefore verifies the outcome rather than trusting the input
property: retention per material must stay near the requested ratio. Wholesale
migration of triangles onto a neighbour's texture is invisible in the geometric
error (measured 0.0169 while it was happening) and invisible in the triangle
count, but it is obvious in the per-material histogram.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from collections import defaultdict
from pathlib import Path


# A material below this share of terrain area may vanish without anyone
# noticing; above it, losing the material is a visible hole.
LOST_MATERIAL_AREA_LIMIT = 0.001  # 0.1%

# Seam locking legitimately holds some materials well ABOVE the requested
# ratio (a small material ringed by locked seams may not decimate at all), so
# only shortfalls are interesting. These bound how far below the request a
# material may sit before it reads as migration rather than decimation.
RETENTION_TOLERANCE = 0.10
MIGRATION_RATIO_FLOOR = 0.5
MIGRATION_MIN_TRIANGLES = 8


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--tool", type=Path, required=True,
                        help="compiled decimate_terrain_mesh executable")
    parser.add_argument("--ratio", type=float, default=0.75)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--work", type=Path, required=True,
                        help="directory for the decimator's intermediate output")
    args = parser.parse_args()

    # `compile-bob-area` is phony and make invokes it several times
    # concurrently. Decimating in place over the extractor's own output made
    # those invocations race: one re-read an already-decimated mesh and
    # decimated it a second time (1101 -> 825 -> 705, dropping materials 12 and
    # 13 entirely), another read a half-written intermediate. Refuse the
    # in-place case outright -- kept a pure function of an input the extractor
    # owns, this step is idempotent however many times make runs it.
    if args.input.resolve() == args.output.resolve():
        print("decimate: --input and --output are the same file. Decimating in "
              "place compounds across make's concurrent invocations of this "
              "step. Write to a separate path.", file=sys.stderr)
        return 1

    document = json.loads(args.input.read_bytes())
    triangles = document["triangles"]

    vertex_material: dict[int, set[int]] = defaultdict(set)
    material_template: dict[int, dict] = {}
    for triangle in triangles:
        material = triangle["material"]
        material_template.setdefault(material, triangle)
        for index in triangle["indices"]:
            vertex_material[index].add(material)

    shared = [v for v, materials in vertex_material.items() if len(materials) > 1]
    if shared:
        print(f"decimate: {len(shared)} vertices span multiple materials; "
              "material reassignment would be ambiguous. Refusing rather than "
              "guessing a texture for a decimated triangle.", file=sys.stderr)
        return 1

    # Concurrent invocations must not share an intermediate path either, or one
    # reads the other's half-written file (seen as a JSON decode error at 0.75).
    stamp = os.getpid()
    args.work.mkdir(parents=True, exist_ok=True)
    indices_path = args.work / ("decimated_indices.%d.json" % stamp)
    report_path = (args.report.with_name("%s.%d.tmp" % (args.report.name, stamp))
                   if args.report else None)
    command = [
        str(args.tool),
        "--input", str(args.input),
        "--output", str(indices_path),
        "--ratio", str(args.ratio),
    ]
    if report_path:
        command += ["--report", str(report_path)]
    result = subprocess.run(command, capture_output=True, text=True)
    sys.stderr.write(result.stderr)
    if result.returncode != 0:
        return result.returncode

    kept = json.loads(indices_path.read_bytes())["triangles"]
    indices_path.unlink()
    if report_path:
        os.replace(report_path, args.report)

    rebuilt = []
    for new_index, corners in enumerate(kept):
        materials = {next(iter(vertex_material[index])) for index in corners}
        if len(materials) != 1:
            print(f"decimate: triangle {corners} spans materials {materials}; "
                  "the disjoint-island property was violated mid-run",
                  file=sys.stderr)
            return 1
        material = materials.pop()
        template = material_template[material]
        entry = {"material": material, "indices": list(corners)}
        if "texture_tile" in template:
            entry["texture_tile"] = template["texture_tile"]
        # `source` must be a unique non-negative id (saturn_mesh_ir.py:98-102).
        # A decimated triangle is new geometry formed by edge collapses and has
        # no single originating source triangle, so it is renumbered rather than
        # inheriting one -- copying the material template's id gave every
        # triangle of a material the same value and tripped the uniqueness
        # check immediately.
        entry["source"] = new_index
        rebuilt.append(entry)

    # A material that loses every triangle means a textured surface vanishes
    # from the level. On BOB at 0.75 that is material 16 -- 4 triangles, 0.005%
    # of terrain area, invisible. On another level it could be a wall. Report
    # every loss, and refuse when a lost material carried enough area to be
    # seen, rather than discovering it by looking at the game later.
    def triangle_area(corners: list[int]) -> float:
        a, b, c = (positions[i] for i in corners)
        u = [b[k] - a[k] for k in range(3)]
        v = [c[k] - a[k] for k in range(3)]
        cross = (u[1] * v[2] - u[2] * v[1],
                 u[2] * v[0] - u[0] * v[2],
                 u[0] * v[1] - u[1] * v[0])
        return sum(x * x for x in cross) ** 0.5 / 2.0

    positions = document["positions"]
    area_before: dict[int, float] = defaultdict(float)
    for triangle in triangles:
        area_before[triangle["material"]] += triangle_area(triangle["indices"])
    total_area = sum(area_before.values()) or 1.0
    surviving = {entry["material"] for entry in rebuilt}
    lost = sorted(set(area_before) - surviving)
    significant = [m for m in lost
                   if area_before[m] / total_area > LOST_MATERIAL_AREA_LIMIT]
    for material in lost:
        share = 100.0 * area_before[material] / total_area
        print(f"decimate: material {material} eliminated "
              f"({share:.4f}% of terrain area)", file=sys.stderr)
    if significant:
        print(f"decimate: materials {significant} carried more than "
              f"{100 * LOST_MATERIAL_AREA_LIMIT:.2f}% of terrain area and were "
              "eliminated entirely -- a visible surface would disappear. "
              "Raise --ratio or exclude those materials.", file=sys.stderr)
        return 1

    # Per-material retention is the test that actually catches cross-material
    # migration, and it is the reason this guard exists at all: when seam
    # locking failed, BOB kept 100% of six materials and 18% of material 12
    # while the geometric error read a reassuring 0.0169 and the total triangle
    # count hit its target exactly. Neither the error nor the count can see a
    # triangle that was relabelled onto a neighbour's texture; the histogram
    # can. With seams locked, decimation can only ever REMOVE triangles from a
    # material, so retention below a fraction of the requested ratio means
    # triangles left the material by some other route.
    counts_before: dict[int, int] = defaultdict(int)
    for triangle in triangles:
        counts_before[triangle["material"]] += 1
    counts_after: dict[int, int] = defaultdict(int)
    for entry in rebuilt:
        counts_after[entry["material"]] += 1

    migrated = []
    for material in sorted(counts_before):
        before = counts_before[material]
        after = counts_after.get(material, 0)
        retention = after / before
        if retention < args.ratio - RETENTION_TOLERANCE:
            print(f"decimate: material {material} retained {after}/{before} "
                  f"({100.0 * retention:.0f}%) against a requested "
                  f"{100.0 * args.ratio:.0f}%", file=sys.stderr)
            if before >= MIGRATION_MIN_TRIANGLES and \
                    retention < args.ratio * MIGRATION_RATIO_FLOOR:
                migrated.append(material)
    if migrated:
        print(f"decimate: materials {migrated} lost far more triangles than "
              "the requested ratio. Seam locking is not holding, so triangles "
              "are being relabelled onto a neighbouring material's texture -- "
              "geometry stays correct while surfaces draw with the wrong "
              "texture. Refusing.", file=sys.stderr)
        return 1

    document["triangles"] = rebuilt
    # Indices into the old triangle list cannot survive a decimation that
    # produces a different triangle set; drop rather than silently mis-map.
    if document.get("pairing_forbidden_triangles"):
        print("decimate: input declares pairing_forbidden_triangles, which "
              "index the pre-decimation triangle list and cannot be remapped. "
              "Refusing.", file=sys.stderr)
        return 1
    document["pairing_forbidden_triangles"] = []
    # Publish the finished document by rename, so a concurrent run of this step
    # never observes a partially written mesh IR.
    staged = args.output.with_name("%s.%d.tmp" % (args.output.name, stamp))
    staged.write_bytes(
        json.dumps(document, separators=(",", ":"), sort_keys=True).encode())
    os.replace(staged, args.output)
    print(f"decimate: {len(triangles)} -> {len(rebuilt)} triangles written to "
          f"{args.output.name}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
