#!/usr/bin/env python3
"""Subdivide large terrain triangles in the mesh IR, before quad pairing.

WHY THIS EXISTS
---------------
VDP1 draws distorted sprites: it interpolates a texture linearly along a quad's
edges with no per-pixel perspective divide. A large surface at a grazing angle
therefore warps its texture no matter how correct its vertices are. That is a
hardware property, not a bug in the transform, and no amount of clipping or
clamping in the renderer can fix it -- both were tried and neither worked.

Every Saturn renderer that draws textured floors solves this the same way, by
making the polygons small:
  * SlaveDriver-Engine emits one command per TILE-sized face
    (`WALLS.C:1145`, `EZ_specialDistSpr2(mapPic(level_face[f].tile), ...)`).
  * This project's own castle demo passes `--subdivision 1
    --subdivision-threshold 1024` to `bake_castle_uv.py`.
BOB never got it because its geometry and its tiles are produced by different
tools, so nothing subdivided the mesh itself.

This step is placed exactly where `decimate_terrain_ir.py` sits: after
extraction, before `saturn_mesh_ir.py`, so pairing, clustering, BSP, admission
and VDP1 emission all see the refined mesh. Render geometry only -- collision
is a separate source and never passes through here.

T-JUNCTIONS ARE THE WHOLE DIFFICULTY
------------------------------------
Splitting a triangle without splitting its neighbour leaves a hanging vertex on
the shared edge, and the neighbour's straight edge no longer meets the split
one exactly. That is a visible crack, the same defect a runtime side-plane clip
produced in this renderer. Two rules prevent it here:

  1. Midpoints are SHARED. A midpoint is keyed by its sorted endpoint index
     pair, so both triangles owning an edge get the identical new vertex index
     and the identical position -- not merely equal coordinates.
  2. Refinement is closed under adjacency (red-green). A triangle with all
     three edges split becomes four ("red"). One left holding one or two
     hanging nodes is split to use them ("green") rather than left with a
     T-junction. Green triangles are re-examined, so the process terminates
     only when no edge is split on one side and straight on the other.
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path


def _edge_key(a: int, b: int) -> tuple[int, int]:
    return (a, b) if a < b else (b, a)


class Mesh:
    """Positions plus parallel UVs, with memoised shared edge midpoints."""

    def __init__(self, positions: list, uvs: list) -> None:
        self.positions = [list(p) for p in positions]
        self.uvs = [list(uv) for uv in uvs]
        self._midpoints: dict[tuple[int, int], int] = {}

    def midpoint(self, a: int, b: int) -> int:
        """Index of the midpoint of edge (a, b), created once and shared.

        Sharing is what keeps neighbouring triangles welded: both sides look up
        the same key and receive the same index, so there is no possibility of
        two midpoints that merely round to the same place.
        """
        key = _edge_key(a, b)
        existing = self._midpoints.get(key)
        if existing is not None:
            return existing
        pa, pb = self.positions[a], self.positions[b]
        ua, ub = self.uvs[a], self.uvs[b]
        # Integer midpoint with floor division: the IR carries world units and
        # s10.5 texture coordinates as integers, and introducing fractions here
        # would not survive the downstream quantisation anyway.
        self.positions.append([(pa[i] + pb[i]) // 2 for i in range(3)])
        self.uvs.append([(ua[i] + ub[i]) // 2 for i in range(2)])
        index = len(self.positions) - 1
        self._midpoints[key] = index
        return index

    def longest_edge(self, indices: list) -> float:
        longest = 0.0
        for i in range(3):
            a = self.positions[indices[i]]
            b = self.positions[indices[(i + 1) % 3]]
            length = math.sqrt(sum((a[k] - b[k]) ** 2 for k in range(3)))
            longest = max(longest, length)
        return longest


def _split_red(mesh: Mesh, tri: dict) -> list:
    """Four-way split. The classic refinement: every edge halved, the three
    midpoints forming the middle triangle."""
    a, b, c = tri["indices"]
    ab = mesh.midpoint(a, b)
    bc = mesh.midpoint(b, c)
    ca = mesh.midpoint(c, a)
    return [[a, ab, ca], [ab, b, bc], [ca, bc, c], [ab, bc, ca]]


def _split_green(mesh: Mesh, indices: list, hanging: dict) -> list:
    """Close a triangle over the hanging nodes its neighbours created.

    One hanging node bisects the triangle; two produce three triangles. Three
    is handled as a red split by the caller. Without this a neighbour's split
    would leave a crack along the shared edge."""
    a, b, c = indices
    edges = [(a, b), (b, c), (c, a)]
    present = [i for i, e in enumerate(edges) if _edge_key(*e) in hanging]
    if len(present) == 1:
        i = present[0]
        m = hanging[_edge_key(*edges[i])]
        opposite = indices[(i + 2) % 3]
        return [[edges[i][0], m, opposite], [m, edges[i][1], opposite]]
    if len(present) == 2:
        i, j = present
        mi = hanging[_edge_key(*edges[i])]
        mj = hanging[_edge_key(*edges[j])]
        # Fan from the vertex shared by both split edges.
        shared = set(edges[i]) & set(edges[j])
        pivot = shared.pop()
        far_i = edges[i][0] if edges[i][1] == pivot else edges[i][1]
        far_j = edges[j][0] if edges[j][1] == pivot else edges[j][1]
        return [[pivot, mi, mj], [mi, far_i, far_j], [mi, far_j, mj]]
    return [list(indices)]


def subdivide(document: dict, threshold: float, max_passes: int) -> dict:
    positions = document["positions"]
    uvs = document["vertex_attributes"]["uv"]
    if len(uvs) != len(positions):
        raise SystemExit(
            "subdivide: uv count %d does not match position count %d; the IR "
            "contract is one uv per position" % (len(uvs), len(positions)))
    mesh = Mesh(positions, uvs)
    triangles = [dict(t) for t in document["triangles"]]

    # `source` must be unique per triangle (saturn_mesh_ir.py rejects
    # duplicates) and it is NOT merely provenance: the runtime LOD policy keys
    # a mandatory-route prefix off it -- saturn_lod_can_suppress() takes
    # primitive->source0 against DEMO_LOD_MANDATORY_ROUTE_PREFIX. Renumbering
    # existing triangles would therefore silently move which geometry is
    # protected from suppression. So every original id is left alone: the first
    # child of a split inherits the parent's id, and siblings are allocated
    # above the current maximum, where they fall outside the prefix and are
    # ordinary suppressible geometry.
    next_source = max((t.get("source", 0) for t in triangles), default=-1) + 1

    # child source id -> the original intake row it was split from. The tile
    # baker indexes intake["triangles"] BY source id
    # (bake_bob_tiles.py: `not 0 <= source0 < len(intake["triangles"])`), so a
    # new id is only legal once the intake has a row at that index. Children
    # inherit their parent's material verbatim, so the row is a straight copy.
    provenance: dict[int, int] = {}

    def _children(tri: dict, pieces: list) -> list:
        nonlocal next_source
        out = []
        parent = tri.get("source", 0)
        depth = int(tri.get("subdivision_depth", 0)) + 1
        for position, piece in enumerate(pieces):
            child = dict(tri)
            child["indices"] = piece
            child["subdivision_depth"] = depth
            if position > 0:
                child["source"] = next_source
                provenance[next_source] = provenance.get(parent, parent)
                next_source += 1
            out.append(child)
        return out

    for _ in range(max_passes):
        # Pass 1: mark every triangle over threshold for a red split, creating
        # its midpoints. Creating them here is what lets neighbours discover
        # the hanging nodes in pass 2.
        red: list[int] = []
        for index, tri in enumerate(triangles):
            if mesh.longest_edge(tri["indices"]) > threshold:
                red.append(index)
        if not red:
            break
        for index in red:
            a, b, c = triangles[index]["indices"]
            mesh.midpoint(a, b)
            mesh.midpoint(b, c)
            mesh.midpoint(c, a)

        # Pass 2: rebuild. A triangle is red if all three of its edges now have
        # midpoints, green if some do, unchanged if none do.
        rebuilt: list[dict] = []
        for tri in triangles:
            a, b, c = tri["indices"]
            edges = [_edge_key(a, b), _edge_key(b, c), _edge_key(c, a)]
            split = {e: mesh._midpoints[e] for e in edges
                     if e in mesh._midpoints}
            if len(split) == 3:
                pieces = _split_red(mesh, tri)
            elif split:
                pieces = _split_green(mesh, [a, b, c], split)
            else:
                rebuilt.append(tri)
                continue
            rebuilt.extend(_children(tri, pieces))
        triangles = rebuilt

    document = dict(document)
    document["positions"] = mesh.positions
    attributes = dict(document["vertex_attributes"])
    attributes["uv"] = mesh.uvs
    document["vertex_attributes"] = attributes
    document["triangles"] = triangles
    # Indices into the old triangle list cannot survive refinement.
    if document.get("pairing_forbidden_triangles"):
        document["pairing_forbidden_triangles"] = []
    return document, provenance


def retexture_intake(intake: dict, triangles: list, uvs: list) -> dict:
    """Point every intake row at its own triangle's UVs.

    The tile baker samples from `intake["triangles"][source]["uv"]`, NOT from
    the mesh IR. A child that inherited its parent's row would therefore be
    baked over the parent's ENTIRE texture span: the tile would not shrink, the
    bank would grow for nothing, and the affine warp subdivision exists to fix
    would survive untouched. Rewriting each row from the refined geometry is
    what actually makes a child a smaller face. Rows for untouched triangles
    are rewritten with the values they already had."""
    intake = dict(intake)
    rows = list(intake["triangles"])
    for tri in triangles:
        source_id = tri.get("source")
        if source_id is None or not 0 <= source_id < len(rows):
            continue
        corner_uvs = [list(uvs[index]) for index in tri["indices"]]
        row = dict(rows[source_id])
        row["uv"] = corner_uvs
        # How many times this face has been halved. The baker sizes a tile from
        # the UV span alone, which would give a child that covers a QUARTER of
        # the parent's area the same 32x32 tile -- four times the texture for
        # the same surface. Halving the tile dimension per level keeps texel
        # density identical (4 children x 16x16 = one 32x32) and makes
        # subdivision texture-neutral instead of quadratic.
        row["subdivision_depth"] = int(tri.get("subdivision_depth", 0))
        rows[source_id] = row
    intake["triangles"] = rows
    return intake


def extend_intake(intake: dict, provenance: dict) -> dict:
    """Give every new source id a row at its own index in the intake.

    The tile baker treats a source id as an index into intake["triangles"], so
    refinement has to grow that list in lockstep or the ids it mints are out of
    range. A child's material is its parent's, so each added row is a copy."""
    if not provenance:
        return intake
    intake = dict(intake)
    triangles = list(intake["triangles"])
    for source_id in sorted(provenance):
        origin = provenance[source_id]
        if not 0 <= origin < len(triangles):
            raise SystemExit(
                "subdivide: provenance %d -> %d is out of intake range"
                % (source_id, origin))
        if source_id != len(triangles):
            raise SystemExit(
                "subdivide: source ids must be contiguous from the original "
                "count; expected %d, got %d" % (len(triangles), source_id))
        triangles.append(triangles[origin])
    intake["triangles"] = triangles
    intake["triangle_count"] = len(triangles)
    if "textured_triangle_count" in intake:
        intake["textured_triangle_count"] = sum(
            1 for t in triangles if t.get("texture"))
    return intake


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--threshold", type=float, required=True,
                        help="split any triangle whose longest edge exceeds "
                             "this many world units")
    parser.add_argument("--max-passes", type=int, default=4,
                        help="refinement passes; each can quadruple a "
                             "triangle, so this bounds the blow-up")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--intake", type=Path,
                        help="intake document to read; required because the "
                             "tile baker indexes it by source id")
    parser.add_argument("--intake-output", type=Path,
                        help="where the extended intake is written. A separate "
                             "file, never the input: rewriting the extractor's "
                             "output makes this step non-idempotent, and a "
                             "second invocation would mint ids that already "
                             "exist. Decimation avoids the same trap.")
    args = parser.parse_args()

    document = json.loads(args.input.read_bytes())
    before_tris = len(document["triangles"])
    before_pos = len(document["positions"])
    provenance: dict[int, int] = {}
    if args.threshold <= 0.0:
        result = document
    else:
        result, provenance = subdivide(
            document, args.threshold, args.max_passes)
        if provenance:
            if args.intake is None or args.intake_output is None:
                raise SystemExit(
                    "subdivide: refinement minted %d new source ids but "
                    "--intake/--intake-output were not both given; the tile "
                    "baker would reject them" % len(provenance))
            if args.intake_output.resolve() == args.intake.resolve():
                raise SystemExit(
                    "subdivide: --intake-output must differ from --intake; "
                    "rewriting the source in place is not idempotent")
            intake = extend_intake(
                json.loads(args.intake.read_bytes()), provenance)
            intake = retexture_intake(
                intake, result["triangles"],
                result["vertex_attributes"]["uv"])
            args.intake_output.parent.mkdir(parents=True, exist_ok=True)
            args.intake_output.write_bytes(json.dumps(
                intake, separators=(",", ":"), sort_keys=True).encode())
    after_tris = len(result["triangles"])
    after_pos = len(result["positions"])

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(
        json.dumps(result, separators=(",", ":"), sort_keys=True).encode())
    if args.report is not None:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_bytes(json.dumps({
            "threshold": args.threshold,
            "max_passes": args.max_passes,
            "triangles_before": before_tris,
            "triangles_after": after_tris,
            "positions_before": before_pos,
            "positions_after": after_pos,
            "policy": "red-green refinement with shared edge midpoints; "
                      "no T-junctions by construction",
        }, indent=2, sort_keys=True).encode())
    print("subdivide: triangles %d -> %d, positions %d -> %d (threshold %g)"
          % (before_tris, after_tris, before_pos, after_pos, args.threshold))
    return 0


if __name__ == "__main__":
    sys.exit(main())
