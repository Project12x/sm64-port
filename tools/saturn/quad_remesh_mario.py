#!/usr/bin/env python3
"""Field-aligned quad remeshing of the generated Mario actor mesh.

This is a host-side analysis tool and the quad counterpart of
``decimate_mario_actor.py``.  It never writes into ``src/port/saturn/gfx``.

Why quads
---------
VDP1 draws a four-vertex primitive natively, so a triangle is a degenerate quad
paying a quad's price.  ``decimate_mario_actor.py`` runs a *triangle*
simplifier and then re-pairs the result with ``quad_pairing.py``; T2.21
measured that decimation makes pairing worse (quads fall from 22.4% of
primitives to 11.2%).  A quad remesher emits quads directly: one output face is
one VDP1 command, with no pairing pass and no degenerate-quad waste.

Remesher
--------
QuadriFlow (Huang, Zhou, Niessner, Shewchuk, Guibas, SGP 2018), pinned commit
``810b7a0967c35b0dc85b4464e3835e26a756c967``, BSD-3-Clause, driven headless as
``quadriflow -i in.obj -o out.obj -f <faces> -seed <n>``.  Nothing from
QuadriFlow is copied into this tree; ``work/`` is gitignored and the binary is
a build artefact.

The structural difference from meshoptimizer
--------------------------------------------
meshoptimizer collapses an edge onto one of its *existing* endpoints, so a
decimated index set stays a subset of the original 424 vertex identifiers and
the 107 baked pose frames stay valid untouched (T2.21 §4.2).  **A remesher
invents new vertices, so that property is gone.**  Poses are therefore
transferred here by barycentric projection: every new vertex is anchored to
(original triangle, barycentric weights) on the surface it was remeshed from,
and each pose frame is re-evaluated as the same barycentric combination of that
triangle's *deformed* corners.

That transfer is exact in a specific and useful sense.  The pose banks encode a
piecewise-linear deformation of the original surface, so a barycentric point of
a deformed original triangle lies *exactly* on the deformed original surface.
Every transferred vertex therefore has zero vertex-to-surface error in every
frame, and all residual error is the remesh's own surface approximation --
the same class of error meshoptimizer reports.  :func:`surface_deviation`
measures it, and it is measured across all 107 frames rather than on the
neutral pose alone.

Three structural constraints, as in the triangle tool
-----------------------------------------------------
1. Textured primitives are untouchable.  ``extract_mario_actor.py:911`` marks
   every textured source triangle ``pairing_forbidden`` and ``:945-950`` refuses
   to emit a tile start unless the primitive owns exactly one source triangle;
   T2.19a proved both of its commands reach the screen.  The 50 textured
   triangles are excluded from remeshing entirely and re-emitted unchanged.
2. Materials may not smear.  A quad carries exactly one material, and a vertex
   shared by two materials cannot move without opening a crack.  Remeshing is
   therefore run per (connected component, material) patch.
3. Meshlets are contiguous spans of *primitive index*, so the whole meshlet
   table is regenerated with the project's own ``mario_meshlets``.

What this tool will not do
--------------------------
QuadriFlow does not preserve an open patch's boundary: on Mario's five open
untextured patches it discards the boundary loop and in three of five cases
*closes the hole*, growing the patch over its neighbour.  Open patches are
therefore left as original triangles and sent through the project's own
``quad_pairing.py``, exactly as they are today.  :func:`classify_patches`
records which patches were remeshed and which were not, and the report carries
the count both ways.
"""

from __future__ import annotations

import argparse
import collections
import json
import math
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from decimate_mario_actor import (  # noqa: E402
    footprint,
    load_mesh,
    parse_array,
    parse_define,
    primitive_triangles,
    reshape,
)
from extract_mario_actor import mario_meshlets  # noqa: E402
from saturn_mesh_ir import compile_mesh_ir  # noqa: E402

NO_TILE = 0xFFFF
# QuadriFlow fails outright on very small closed patches; 6 quads is the
# measured floor below which no seed in 0..7 produces output.
MIN_TARGET_QUADS = 6
SEED_RETRIES = 8


# --------------------------------------------------------------------------
# mesh decomposition
# --------------------------------------------------------------------------


def source_triangles(mesh: dict) -> list[dict]:
    """Reconstruct the flat source-triangle stream from the generated bank."""
    triangles: list[dict] = []
    for index, primitive in enumerate(mesh["primitives"]):
        for corners in primitive_triangles(primitive):
            triangles.append(
                {
                    "indices": list(corners),
                    "material": primitive[0],
                    "cull_back": mesh["cull_back"][index],
                    "textured": mesh["tile_start"][index] != NO_TILE,
                    "tile": mesh["tile_start"][index],
                    "primitive": index,
                }
            )
    return triangles


def connected_components(triangles: list[dict]) -> list[int]:
    """Return a component id per vertex, ordered by descending component size."""
    adjacency: dict[int, set[int]] = collections.defaultdict(set)
    for triangle in triangles:
        corners = triangle["indices"]
        for a, b in ((0, 1), (1, 2), (2, 0)):
            adjacency[corners[a]].add(corners[b])
            adjacency[corners[b]].add(corners[a])
    seen: set[int] = set()
    groups: list[set[int]] = []
    for vertex in sorted(adjacency):
        if vertex in seen:
            continue
        stack = [vertex]
        group: set[int] = set()
        while stack:
            current = stack.pop()
            if current in group:
                continue
            group.add(current)
            seen.add(current)
            stack.extend(adjacency[current] - group)
        groups.append(group)
    groups.sort(key=len, reverse=True)
    component_of: dict[int, int] = {}
    for identifier, group in enumerate(groups):
        for vertex in group:
            component_of[vertex] = identifier
    return component_of


def boundary_edge_count(triangles: list[dict]) -> tuple[int, int]:
    """Return (boundary edges, non-manifold edges) for a triangle set."""
    edges: collections.Counter = collections.Counter()
    for triangle in triangles:
        corners = triangle["indices"]
        for a, b in ((0, 1), (1, 2), (2, 0)):
            edges[tuple(sorted((corners[a], corners[b])))] += 1
    return (
        sum(1 for count in edges.values() if count == 1),
        sum(1 for count in edges.values() if count > 2),
    )


def classify_patches(triangles: list[dict]) -> dict:
    """Split the mesh into (component, material) patches and classify each.

    A patch is remeshable when it is untextured *and* closed.  QuadriFlow does
    not preserve an open patch's boundary loop, and a resampled boundary is a
    crack against the neighbouring material.
    """
    component_of = connected_components(triangles)
    patches: dict[tuple[int, int], list[int]] = collections.defaultdict(list)
    for index, triangle in enumerate(triangles):
        key = (component_of[triangle["indices"][0]], triangle["material"])
        patches[key].append(index)

    records = []
    for key in sorted(patches):
        members = patches[key]
        group = [triangles[index] for index in members]
        boundary, non_manifold = boundary_edge_count(group)
        textured = group[0]["textured"]
        if any(triangle["textured"] != textured for triangle in group):
            raise ValueError(f"patch {key} mixes textured and untextured triangles")
        cull_states = {triangle["cull_back"] for triangle in group}
        if len(cull_states) != 1:
            raise ValueError(f"patch {key} mixes G_CULL_BACK states")
        records.append(
            {
                "component": key[0],
                "material": key[1],
                "triangles": members,
                "triangle_count": len(members),
                "vertex_count": len({v for t in group for v in t["indices"]}),
                "textured": textured,
                "cull_back": cull_states.pop(),
                "boundary_edges": boundary,
                "non_manifold_edges": non_manifold,
                "closed": boundary == 0 and non_manifold == 0,
                "remeshable": (not textured) and boundary == 0 and non_manifold == 0,
            }
        )
    return {"patches": records, "component_of": component_of}


# --------------------------------------------------------------------------
# QuadriFlow driver
# --------------------------------------------------------------------------


def run_quadriflow(
    binary: Path,
    positions: list[list[int]],
    triangles: list[list[int]],
    target_faces: int,
    scratch: Path,
    tag: str,
) -> tuple[list[list[float]], list[list[int]], int] | None:
    """Remesh one patch.  Returns (vertices, faces, seed) or None on failure.

    QuadriFlow is not robust at these face counts -- roughly a quarter of the
    small closed patches produce no output at a given seed.  Seeds are tried in
    ascending order and the first success is taken, which keeps the result a
    deterministic function of the input.
    """
    scratch.mkdir(parents=True, exist_ok=True)
    used = sorted({vertex for triangle in triangles for vertex in triangle})
    local_of = {vertex: index + 1 for index, vertex in enumerate(used)}
    lines = [f"v {positions[v][0]} {positions[v][1]} {positions[v][2]}" for v in used]
    lines += [
        "f " + " ".join(str(local_of[vertex]) for vertex in triangle)
        for triangle in triangles
    ]
    request = scratch / f"{tag}.obj"
    request.write_text("\n".join(lines) + "\n")

    for seed in range(SEED_RETRIES):
        response = scratch / f"{tag}_f{target_faces}_s{seed}.obj"
        if response.exists():
            response.unlink()
        try:
            subprocess.run(
                [
                    str(binary.resolve()),
                    "-i", str(request.resolve()),
                    "-o", str(response.resolve()),
                    "-f", str(target_faces),
                    "-seed", str(seed),
                ],
                capture_output=True,
                text=True,
                timeout=120,
            )
        except subprocess.TimeoutExpired:
            continue
        if not response.exists():
            continue
        vertices: list[list[float]] = []
        faces: list[list[int]] = []
        for line in response.read_text().splitlines():
            if line.startswith("v "):
                vertices.append([float(value) for value in line.split()[1:4]])
            elif line.startswith("f "):
                faces.append([int(value) - 1 for value in line.split()[1:]])
        if not faces:
            continue
        if any(len(face) != 4 for face in faces):
            # A non-quad face would be a triangle fallback, which defeats the
            # entire premise; reject the seed rather than silently accept it.
            continue
        if max(max(face) for face in faces) >= len(vertices):
            continue
        return vertices, faces, seed
    return None


# --------------------------------------------------------------------------
# pose transfer
# --------------------------------------------------------------------------


def closest_point_on_triangle(
    point: list[float], a: list[float], b: list[float], c: list[float]
) -> tuple[float, tuple[float, float, float]]:
    """Squared distance to a triangle, plus the barycentric weights of the foot.

    Ericson, *Real-Time Collision Detection*, the standard Voronoi-region form.
    """
    ab = [b[i] - a[i] for i in range(3)]
    ac = [c[i] - a[i] for i in range(3)]
    ap = [point[i] - a[i] for i in range(3)]
    d1 = sum(ab[i] * ap[i] for i in range(3))
    d2 = sum(ac[i] * ap[i] for i in range(3))
    if d1 <= 0.0 and d2 <= 0.0:
        return sum((point[i] - a[i]) ** 2 for i in range(3)), (1.0, 0.0, 0.0)
    bp = [point[i] - b[i] for i in range(3)]
    d3 = sum(ab[i] * bp[i] for i in range(3))
    d4 = sum(ac[i] * bp[i] for i in range(3))
    if d3 >= 0.0 and d4 <= d3:
        return sum((point[i] - b[i]) ** 2 for i in range(3)), (0.0, 1.0, 0.0)
    vc = d1 * d4 - d3 * d2
    if vc <= 0.0 and d1 >= 0.0 and d3 <= 0.0:
        v = d1 / (d1 - d3) if d1 != d3 else 0.0
        foot = [a[i] + v * ab[i] for i in range(3)]
        return sum((point[i] - foot[i]) ** 2 for i in range(3)), (1.0 - v, v, 0.0)
    cp = [point[i] - c[i] for i in range(3)]
    d5 = sum(ab[i] * cp[i] for i in range(3))
    d6 = sum(ac[i] * cp[i] for i in range(3))
    if d6 >= 0.0 and d5 <= d6:
        return sum((point[i] - c[i]) ** 2 for i in range(3)), (0.0, 0.0, 1.0)
    vb = d5 * d2 - d1 * d6
    if vb <= 0.0 and d2 >= 0.0 and d6 <= 0.0:
        w = d2 / (d2 - d6) if d2 != d6 else 0.0
        foot = [a[i] + w * ac[i] for i in range(3)]
        return sum((point[i] - foot[i]) ** 2 for i in range(3)), (1.0 - w, 0.0, w)
    va = d3 * d6 - d5 * d4
    if va <= 0.0 and (d4 - d3) >= 0.0 and (d5 - d6) >= 0.0:
        denominator = (d4 - d3) + (d5 - d6)
        w = (d4 - d3) / denominator if denominator != 0.0 else 0.0
        foot = [b[i] + w * (c[i] - b[i]) for i in range(3)]
        return sum((point[i] - foot[i]) ** 2 for i in range(3)), (0.0, 1.0 - w, w)
    denominator = va + vb + vc
    v = vb / denominator
    w = vc / denominator
    foot = [a[i] + v * ab[i] + w * ac[i] for i in range(3)]
    return sum((point[i] - foot[i]) ** 2 for i in range(3)), (1.0 - v - w, v, w)


def anchor_to_surface(
    point: list[float], candidates: list[list[int]], positions: list[list[int]]
) -> dict:
    """Anchor one new vertex to (triangle, barycentric weights) on the original."""
    best_distance = float("inf")
    best: dict | None = None
    for triangle in candidates:
        a, b, c = (positions[index] for index in triangle)
        distance, weights = closest_point_on_triangle(
            point, [float(v) for v in a], [float(v) for v in b], [float(v) for v in c]
        )
        if distance < best_distance:
            best_distance = distance
            best = {"triangle": list(triangle), "weights": list(weights)}
    if best is None:
        raise ValueError("no candidate triangle for anchoring")
    best["snap_distance"] = math.sqrt(best_distance)
    return best


def evaluate_anchor(anchor: dict, frame: list[list[int]]) -> list[float]:
    """Evaluate one anchored vertex against one deformed pose frame."""
    a, b, c = (frame[index] for index in anchor["triangle"])
    wa, wb, wc = anchor["weights"]
    return [wa * a[i] + wb * b[i] + wc * c[i] for i in range(3)]


def evaluate_anchor_scalar(anchor: dict, frame: list[int]) -> float:
    a, b, c = (frame[index] for index in anchor["triangle"])
    wa, wb, wc = anchor["weights"]
    return wa * a + wb * b + wc * c


# --------------------------------------------------------------------------
# error measurement
# --------------------------------------------------------------------------


def surface_deviation(
    samples: list[tuple[list[float], list[list[int]]]],
    positions: list[list[int]],
) -> tuple[float, float]:
    """Max and RMS distance from sample points to the original surface."""
    if not samples:
        return 0.0, 0.0
    distances = []
    for point, candidates in samples:
        best = min(
            closest_point_on_triangle(
                point,
                [float(v) for v in positions[t[0]]],
                [float(v) for v in positions[t[1]]],
                [float(v) for v in positions[t[2]]],
            )[0]
            for t in candidates
        )
        distances.append(math.sqrt(best))
    return max(distances), math.sqrt(sum(d * d for d in distances) / len(distances))


def axis_extent(points: list[list[float]]) -> float:
    if not points:
        return 0.0
    return max(
        max(point[axis] for point in points) - min(point[axis] for point in points)
        for axis in range(3)
    )


def quad_planarity(indices: list[int], positions: list[list[float]]) -> float:
    """Out-of-plane deviation of a quad's fourth corner, as a fraction of its size.

    VDP1 draws a non-planar quad as a bilinear warp, so this is the quantity
    that decides whether a quad reads as a surface or as a visible crease.
    """
    a, b, c, d = (positions[index] for index in indices)
    u = [b[i] - a[i] for i in range(3)]
    v = [c[i] - a[i] for i in range(3)]
    normal = [
        u[1] * v[2] - u[2] * v[1],
        u[2] * v[0] - u[0] * v[2],
        u[0] * v[1] - u[1] * v[0],
    ]
    length = math.sqrt(sum(value * value for value in normal))
    if length == 0.0:
        return 0.0
    offset = abs(sum(normal[i] * (d[i] - a[i]) for i in range(3))) / length
    diagonal = math.sqrt(sum((c[i] - a[i]) ** 2 for i in range(3)))
    return offset / diagonal if diagonal > 0.0 else 0.0


# --------------------------------------------------------------------------
# level construction
# --------------------------------------------------------------------------


def build_level(
    mesh: dict,
    triangles: list[dict],
    classification: dict,
    ratio: float,
    binary: Path,
    scratch: Path,
) -> dict:
    """Remesh every remeshable patch at one density and reassemble the actor."""
    positions = mesh["vertices"]
    patches = classification["patches"]

    new_positions: list[list[int]] = [list(point) for point in positions]
    anchors: dict[int, dict] = {}
    for index in range(len(positions)):
        # An original vertex is its own anchor with weight 1, which makes the
        # pose transfer exact for every vertex that survives untouched.
        anchors[index] = {
            "triangle": [index, index, index],
            "weights": [1.0, 0.0, 0.0],
            "snap_distance": 0.0,
        }

    remeshed_faces: dict[tuple[int, int], list[dict]] = {}
    untouched: list[int] = []
    per_patch: list[dict] = []
    failures = 0

    for patch in patches:
        key = (patch["component"], patch["material"])
        members = [triangles[index] for index in patch["triangles"]]
        if not patch["remeshable"]:
            untouched.extend(patch["triangles"])
            per_patch.append(
                {
                    "component": patch["component"],
                    "material": patch["material"],
                    "input_triangles": patch["triangle_count"],
                    "remeshed": False,
                    "reason": "textured" if patch["textured"] else "open_boundary",
                    "boundary_edges": patch["boundary_edges"],
                }
            )
            continue

        target = max(MIN_TARGET_QUADS, int(round(patch["triangle_count"] * ratio / 2.0)))
        result = run_quadriflow(
            binary,
            positions,
            [triangle["indices"] for triangle in members],
            target,
            scratch,
            f"c{patch['component']:02d}m{patch['material']}",
        )
        if result is None:
            failures += 1
            untouched.extend(patch["triangles"])
            per_patch.append(
                {
                    "component": patch["component"],
                    "material": patch["material"],
                    "input_triangles": patch["triangle_count"],
                    "remeshed": False,
                    "reason": "quadriflow_failed",
                    "requested_quads": target,
                }
            )
            continue

        vertices, faces, seed = result
        candidates = [triangle["indices"] for triangle in members]
        remap: list[int] = []
        snap_distances: list[float] = []
        projected: list[list[float]] = []
        for point in vertices:
            anchor = anchor_to_surface(point, candidates, positions)
            identifier = len(new_positions)
            # Store the anchor's foot, not QuadriFlow's raw output.  The raw
            # position sits wherever the position field put it, which on these
            # small closed shells is measurably *inside* the surface; the foot
            # is on the original surface by construction.  Storing the foot
            # also makes the base vertex table agree with the transferred pose
            # banks, which are barycentric evaluations of the same anchor.
            foot = evaluate_anchor(anchor, positions)
            projected.append(foot)
            new_positions.append([int(round(value)) for value in foot])
            anchors[identifier] = anchor
            snap_distances.append(anchor["snap_distance"])
            remap.append(identifier)

        records = [
            {
                "material": patch["material"],
                "indices": [remap[index] for index in face],
                "cull_back": patch["cull_back"],
                "textured": False,
                "tile": NO_TILE,
                "raw": [vertices[index] for index in face],
                "local": [projected[index] for index in face],
            }
            for face in faces
        ]
        remeshed_faces[key] = records

        local_extent = axis_extent([[float(v) for v in positions[i]]
                                    for t in candidates for i in t])
        patch_points = [[float(v) for v in positions[i]] for t in candidates for i in t]
        original_diagonal = math.sqrt(
            sum(
                (max(p[axis] for p in patch_points) - min(p[axis] for p in patch_points)) ** 2
                for axis in range(3)
            )
        )

        def diagonal(points: list[list[float]]) -> float:
            return math.sqrt(
                sum(
                    (max(p[axis] for p in points) - min(p[axis] for p in points)) ** 2
                    for axis in range(3)
                )
            )

        samples = []
        for face in faces:
            corners = [projected[index] for index in face]
            centroid = [sum(c[i] for c in corners) / 4.0 for i in range(3)]
            samples.append((centroid, candidates))
            for i in range(4):
                mid = [(corners[i][k] + corners[(i + 1) % 4][k]) / 2.0 for k in range(3)]
                samples.append((mid, candidates))
        max_deviation, rms_deviation = surface_deviation(samples, positions)
        per_patch.append(
            {
                "component": patch["component"],
                "material": patch["material"],
                "input_triangles": patch["triangle_count"],
                "remeshed": True,
                "requested_quads": target,
                "output_quads": len(faces),
                "output_vertices": len(vertices),
                "seed": seed,
                "patch_axis_extent": local_extent,
                "max_surface_deviation": max_deviation,
                "rms_surface_deviation": rms_deviation,
                "relative_error_patch_extent": (
                    max_deviation / local_extent if local_extent else 0.0
                ),
                # Shrinkage is the failure mode that actually shows on screen:
                # Mario is 15 separate abutting closed shells, so a patch that
                # contracts pulls away from its neighbour and opens a joint gap.
                # Both the raw remesher output and the surface-projected result
                # are reported, because projection is what fixes it.
                "original_bbox_diagonal": original_diagonal,
                "raw_bbox_diagonal": diagonal(vertices),
                "projected_bbox_diagonal": diagonal(projected),
                "raw_diagonal_retained": (
                    diagonal(vertices) / original_diagonal if original_diagonal else 0.0
                ),
                "projected_diagonal_retained": (
                    diagonal(projected) / original_diagonal if original_diagonal else 0.0
                ),
                "max_snap_distance": max(snap_distances) if snap_distances else 0.0,
                "mean_snap_distance": (
                    sum(snap_distances) / len(snap_distances) if snap_distances else 0.0
                ),
            }
        )

    return {
        "positions": new_positions,
        "anchors": anchors,
        "remeshed": remeshed_faces,
        "untouched": untouched,
        "per_patch": per_patch,
        "quadriflow_failures": failures,
    }


def assemble(mesh: dict, triangles: list[dict], level: dict) -> dict:
    """Combine remeshed quads and untouched triangles into a primitive list.

    Remeshed quads become VDP1 primitives directly -- one output face is one
    command, with no pairing pass.  Untouched triangles go through the
    project's own ``compile_mesh_ir`` so they get exactly the pairing they get
    today.
    """
    positions = level["positions"]
    # Original triangle order, not patch order.  mario_meshlets cuts a record
    # whenever (material, opacity) changes or 32 primitives accumulate, walking
    # the primitive list in order, so a reordered rebuild silently changes the
    # meshlet count -- the defect T2.21 §4.4 caught.  Keeping the untouched
    # triangles in their original order is what makes the ratio-1.0 baseline
    # reproduce the shipped header's 31 meshlets.
    untouched = sorted(level["untouched"])

    document = {
        "schema": "sm64-saturn-mesh-ir",
        "version": 1,
        "name": "mario_quad_remesh_untouched",
        "positions": [list(point) for point in positions],
        "materials": [
            {"id": index, "rgb555": list(rgb)}
            for index, rgb in enumerate(mesh["material_rgb"])
        ],
        "triangles": [
            {
                "indices": list(triangles[index]["indices"]),
                "material": triangles[index]["material"],
                "source": order,
            }
            for order, index in enumerate(untouched)
        ],
        "pairing_forbidden_triangles": [
            order for order, index in enumerate(untouched)
            if triangles[index]["textured"]
        ],
        "vertex_attributes": {},
        "validation_poses": [],
    }
    compiled, _primitives, report = compile_mesh_ir(document)

    paired: list[dict] = []
    for primitive in compiled["primitives"]:
        sources = primitive["source_triangles"]
        origin = triangles[untouched[sources[0]]]
        if origin["textured"] and len(sources) != 1:
            raise ValueError("a textured triangle was paired into a quad")
        paired.append(
            {
                "material": primitive["material"],
                "indices": list(primitive["indices"]),
                "representation": primitive["representation"],
                "textured": origin["textured"],
                "tile": origin["tile"],
                "cull_back": origin["cull_back"],
                "source": "paired",
            }
        )

    remeshed_by_material: dict[int, list[dict]] = collections.defaultdict(list)
    for (_component, material), records in sorted(level["remeshed"].items()):
        for record in records:
            remeshed_by_material[material].append(
                {
                    "material": material,
                    "indices": list(record["indices"]),
                    "representation": "quad",
                    "textured": False,
                    "tile": NO_TILE,
                    "cull_back": record["cull_back"],
                    "source": "remeshed",
                }
            )

    # The paired primitives are already in original triangle order.  A
    # material's remeshed quads are spliced in immediately before that
    # material's first surviving paired primitive, which keeps every material
    # run contiguous and reproduces the original ordering exactly when nothing
    # was remeshed.
    emitted: list[dict] = []
    seen_materials: set[int] = set()
    for record in paired:
        material = record["material"]
        if material not in seen_materials:
            seen_materials.add(material)
            emitted.extend(remeshed_by_material.get(material, []))
        emitted.append(record)
    # A material whose every patch was remeshed has no paired primitive to
    # anchor against; emit it in material order after the rest.
    for material in sorted(remeshed_by_material):
        if material not in seen_materials:
            seen_materials.add(material)
            emitted.extend(remeshed_by_material[material])

    compiled_primitives = [
        {
            "representation": record["representation"],
            "material": record["material"],
            "indices": record["indices"],
            "source_triangles": [index],
        }
        for index, record in enumerate(emitted)
    ]
    meshlet_sources = [
        {"texture": "tile" if record["textured"] else None,
         "cull_back": record["cull_back"]}
        for record in emitted
    ]
    meshlets = mario_meshlets(meshlet_sources, compiled_primitives, positions)

    referenced = sorted({v for record in emitted for v in record["indices"]})
    textured_count = sum(1 for record in emitted if record["textured"])
    quad_count = sum(1 for record in emitted if record["representation"] == "quad")
    return {
        "primitives": emitted,
        "primitive_count": len(emitted),
        "quad_count": quad_count,
        "remeshed_primitive_count": sum(
            1 for record in emitted if record["source"] == "remeshed"
        ),
        "textured_primitive_count": textured_count,
        "vdp1_command_count": len(emitted) + textured_count,
        "referenced_vertices": referenced,
        "referenced_vertex_count": len(referenced),
        "meshlet_count": meshlets["count"],
        "meshlet_primitive_list_count": len(meshlets["lod_primitive_indices"]),
        "meshlet_position_list_count": len(meshlets["lod_position_indices"]),
        "pairing_report": report,
    }


def transfer_poses(mesh: dict, level: dict, assembled: dict) -> dict:
    """Rebake all 107 pose frames and both light banks onto the new vertices."""
    referenced = assembled["referenced_vertices"]
    anchors = level["anchors"]
    banks = {}
    for name, frames in (("animation", mesh["animation"]), ("walking", mesh["walking"])):
        banks[name] = [
            [
                [int(round(value)) for value in evaluate_anchor(anchors[vertex], frame)]
                for vertex in referenced
            ]
            for frame in frames
        ]
    lights = {}
    for name, frames in (
        ("animation_light", mesh.get("animation_light")),
        ("walking_light", mesh.get("walking_light")),
    ):
        if frames is None:
            continue
        lights[name] = [
            [
                int(round(evaluate_anchor_scalar(anchors[vertex], frame)))
                for vertex in referenced
            ]
            for frame in frames
        ]
    return {"vertices": banks, "light": lights}


def pose_sweep(
    mesh: dict, triangles: list[dict], level: dict, assembled: dict, transferred: dict
) -> dict:
    """Measure remesh surface deviation across every one of the 107 pose frames.

    T2.21 §4.2 recorded that its collapse decision was made on the neutral pose
    only and that nothing proved it across 107 frames.  Barycentric transfer is
    cheap enough to close that gap: each frame's deformed original surface is
    reconstructed from the shipped pose bank, the remeshed quads are evaluated
    against it, and the worst frame is reported.
    """
    referenced = assembled["referenced_vertices"]
    slot_of = {vertex: index for index, vertex in enumerate(referenced)}
    remeshed = [
        record for record in assembled["primitives"] if record["source"] == "remeshed"
    ]
    if not remeshed:
        return {"frames": 0}

    candidates_by_material: dict[int, list[list[int]]] = collections.defaultdict(list)
    for triangle in triangles:
        if not triangle["textured"]:
            candidates_by_material[triangle["material"]].append(triangle["indices"])

    results = []
    for bank_name, frames in (
        ("animation", mesh["animation"]),
        ("walking", mesh["walking"]),
    ):
        for frame_index, frame in enumerate(frames):
            deformed = [list(point) for point in frame]
            extent = axis_extent([[float(v) for v in point] for point in deformed])
            samples = []
            for record in remeshed:
                corners = [
                    transferred["vertices"][bank_name][frame_index][slot_of[v]]
                    for v in record["indices"]
                ]
                centroid = [sum(float(c[i]) for c in corners) / 4.0 for i in range(3)]
                samples.append((centroid, candidates_by_material[record["material"]]))
            max_deviation, rms_deviation = surface_deviation(samples, deformed)
            results.append(
                {
                    "bank": bank_name,
                    "frame": frame_index,
                    "extent": extent,
                    "max_deviation": max_deviation,
                    "rms_deviation": rms_deviation,
                    "relative": max_deviation / extent if extent else 0.0,
                }
            )
    worst = max(results, key=lambda row: row["relative"])
    return {
        "frames": len(results),
        "worst_frame": worst,
        "worst_relative": worst["relative"],
        "mean_relative": sum(row["relative"] for row in results) / len(results),
        "max_rms": max(row["rms_deviation"] for row in results),
    }


def planarity_census(assembled: dict, positions: list[list[int]]) -> dict:
    """Compare remeshed-quad planarity against the shipped pairer's quads."""
    buckets: dict[str, list[float]] = {"remeshed": [], "paired": []}
    for record in assembled["primitives"]:
        if record["representation"] != "quad":
            continue
        value = quad_planarity(
            record["indices"], [[float(v) for v in p] for p in positions]
        )
        buckets["remeshed" if record["source"] == "remeshed" else "paired"].append(value)
    summary = {}
    for name, values in buckets.items():
        if not values:
            summary[name] = {"count": 0}
            continue
        ordered = sorted(values)
        summary[name] = {
            "count": len(ordered),
            "max": ordered[-1],
            "median": ordered[len(ordered) // 2],
            "p90": ordered[int(len(ordered) * 0.9)],
            "mean": sum(ordered) / len(ordered),
        }
    return summary


# --------------------------------------------------------------------------
# entry point
# --------------------------------------------------------------------------


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--header",
        type=Path,
        default=Path("src/port/saturn/gfx/saturn_mario_actor_mesh.h"),
    )
    parser.add_argument(
        "--quadriflow",
        type=Path,
        default=Path("work/upstream/quadriflow/build/quadriflow.exe"),
    )
    parser.add_argument("--ratios", default="1.00,0.75,0.50")
    parser.add_argument("--scratch", type=Path, default=Path("build/saturn/tmp/quadremesh"))
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--levels-output", type=Path)
    parser.add_argument("--skip-pose-sweep", action="store_true")
    args = parser.parse_args()

    header_text = args.header.read_text()
    mesh = load_mesh(args.header)
    vertex_count = mesh["vertex_count"]
    mesh["animation_light"] = reshape(
        parse_array(header_text, "sm64_mario_animation_light_intensity"),
        mesh["frame_count"], vertex_count,
    )
    mesh["walking_light"] = reshape(
        parse_array(header_text, "sm64_mario_walking_animation_light_intensity"),
        mesh["walk_frame_count"], vertex_count,
    )

    triangles = source_triangles(mesh)
    classification = classify_patches(triangles)
    whole_extent = axis_extent([[float(v) for v in p] for p in mesh["vertices"]])

    baseline_untouched = list(range(len(triangles)))
    baseline_level = {
        "positions": [list(point) for point in mesh["vertices"]],
        "anchors": {
            index: {"triangle": [index, index, index], "weights": [1.0, 0.0, 0.0],
                    "snap_distance": 0.0}
            for index in range(vertex_count)
        },
        "remeshed": {},
        "untouched": baseline_untouched,
        "per_patch": [],
        "quadriflow_failures": 0,
    }
    baseline = assemble(mesh, triangles, baseline_level)
    baseline["name"] = "original"
    baseline["ratio"] = 1.0
    baseline["level"] = baseline_level
    baseline["pose_sweep"] = {"frames": 0}
    baseline["planarity"] = planarity_census(baseline, mesh["vertices"])

    levels = [baseline]
    for text in args.ratios.split(","):
        ratio = float(text)
        level = build_level(mesh, triangles, classification, ratio,
                            args.quadriflow, args.scratch)
        assembled = assemble(mesh, triangles, level)
        assembled["name"] = f"quad_{int(round(ratio * 100))}"
        assembled["ratio"] = ratio
        assembled["level"] = level
        assembled["planarity"] = planarity_census(assembled, level["positions"])
        if args.skip_pose_sweep:
            assembled["pose_sweep"] = {"frames": 0}
        else:
            transferred = transfer_poses(mesh, level, assembled)
            assembled["transferred"] = transferred
            assembled["pose_sweep"] = pose_sweep(
                mesh, triangles, level, assembled, transferred
            )
        levels.append(assembled)

    summary = []
    for entry in levels:
        level = entry["level"]
        bytes_ = footprint(
            mesh,
            entry["primitive_count"],
            entry["referenced_vertex_count"],
            entry["meshlet_count"],
            entry["meshlet_primitive_list_count"],
            entry["meshlet_position_list_count"],
        )
        neutral_max = max(
            (row.get("max_surface_deviation", 0.0) for row in level["per_patch"]),
            default=0.0,
        )
        neutral_rel_patch = max(
            (row.get("relative_error_patch_extent", 0.0) for row in level["per_patch"]),
            default=0.0,
        )
        remeshed_rows = [row for row in level["per_patch"] if row.get("remeshed")]
        shrink = {}
        if remeshed_rows:
            raw = [row["raw_diagonal_retained"] for row in remeshed_rows]
            projected_retained = [
                row["projected_diagonal_retained"] for row in remeshed_rows
            ]
            shrink = {
                "remeshed_patches": len(remeshed_rows),
                "raw_mean_retained": sum(raw) / len(raw),
                "raw_worst_retained": min(raw),
                "projected_mean_retained": (
                    sum(projected_retained) / len(projected_retained)
                ),
                "projected_worst_retained": min(projected_retained),
                "max_snap_distance": max(
                    row["max_snap_distance"] for row in remeshed_rows
                ),
                "mean_snap_distance": sum(
                    row["mean_snap_distance"] for row in remeshed_rows
                ) / len(remeshed_rows),
            }
        summary.append(
            {
                "name": entry["name"],
                "ratio": entry["ratio"],
                "primitive_count": entry["primitive_count"],
                "quad_count": entry["quad_count"],
                "remeshed_primitive_count": entry["remeshed_primitive_count"],
                "textured_primitive_count": entry["textured_primitive_count"],
                "vdp1_command_count": entry["vdp1_command_count"],
                "referenced_vertex_count": entry["referenced_vertex_count"],
                "meshlet_count": entry["meshlet_count"],
                "meshlet_primitive_list_count": entry["meshlet_primitive_list_count"],
                "meshlet_position_list_count": entry["meshlet_position_list_count"],
                "quadriflow_failures": level["quadriflow_failures"],
                "neutral_max_surface_deviation": neutral_max,
                "neutral_relative_whole_extent": (
                    neutral_max / whole_extent if whole_extent else 0.0
                ),
                "neutral_relative_patch_extent": neutral_rel_patch,
                "shrinkage": shrink,
                "pose_sweep": entry["pose_sweep"],
                "planarity": entry["planarity"],
                "bytes": bytes_,
                "per_patch": level["per_patch"],
                "pairing_report": {
                    key: value
                    for key, value in entry["pairing_report"].items()
                    if key in {
                        "source_triangle_count", "quad_count",
                        "standalone_triangle_count", "render_primitive_count",
                        "commands_saved", "pairing_forbidden_triangle_count",
                    }
                },
            }
        )

    report = {
        "schema": "sm64-saturn-mario-quad-remesh",
        "version": 1,
        "remesher": {
            "repo": "https://github.com/hjwdzh/QuadriFlow",
            "commit": "810b7a0967c35b0dc85b4464e3835e26a756c967",
            "license": "BSD-3-Clause",
            "entry_point": "quadriflow -i in.obj -o out.obj -f <faces> -seed <n>",
            "reuse_mode": "dependency (host tool, not vendored)",
            "local_build_patch": (
                "src/dedge.cpp: guard the MSVC _InterlockedCompareExchange branch "
                "with !defined(__GNUC__) so MinGW takes __sync_bool_compare_and_swap"
            ),
        },
        "header": str(args.header).replace("\\", "/"),
        "whole_mesh_axis_extent": whole_extent,
        "source_counts": {
            "vertices": mesh["vertex_count"],
            "primitives": mesh["primitive_count"],
            "textured_primitives": mesh["textured_count"],
            "materials": mesh["material_count"],
            "animation_frames": mesh["frame_count"],
            "walking_animation_frames": mesh["walk_frame_count"],
            "meshlets": mesh["meshlet_count"],
            "source_triangles": len(triangles),
        },
        "shipped_header_counts": {
            "primitives": mesh["primitive_count"],
            "quads": parse_define(header_text, "SM64_MARIO_QUAD_COUNT"),
            "meshlets": mesh["meshlet_count"],
            "meshlet_primitive_list": mesh["meshlet_primitive_list_count"],
            "meshlet_position_list": mesh["meshlet_position_list_count"],
        },
        "patches": classification["patches"],
        "patch_summary": {
            "total": len(classification["patches"]),
            "remeshable": sum(1 for p in classification["patches"] if p["remeshable"]),
            "textured": sum(1 for p in classification["patches"] if p["textured"]),
            "open_untextured": sum(
                1 for p in classification["patches"]
                if not p["textured"] and not p["closed"]
            ),
            "remeshable_triangles": sum(
                p["triangle_count"] for p in classification["patches"] if p["remeshable"]
            ),
            "open_untextured_triangles": sum(
                p["triangle_count"] for p in classification["patches"]
                if not p["textured"] and not p["closed"]
            ),
            "textured_triangles": sum(
                p["triangle_count"] for p in classification["patches"] if p["textured"]
            ),
        },
        "levels": summary,
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")

    if args.levels_output is not None:
        args.levels_output.parent.mkdir(parents=True, exist_ok=True)
        payload = {
            "material_rgb": mesh["material_rgb"],
            "levels": [],
        }
        for entry in levels:
            referenced = entry["referenced_vertices"]
            slot_of = {vertex: index for index, vertex in enumerate(referenced)}
            level = entry["level"]
            if "transferred" in entry:
                animation = entry["transferred"]["vertices"]["animation"]
                walking = entry["transferred"]["vertices"]["walking"]
            else:
                animation = [
                    [list(frame[vertex]) for vertex in referenced]
                    for frame in mesh["animation"]
                ]
                walking = [
                    [list(frame[vertex]) for vertex in referenced]
                    for frame in mesh["walking"]
                ]
            payload["levels"].append(
                {
                    "name": entry["name"],
                    "animation": animation,
                    "walking": walking,
                    "primitives": [
                        {
                            "material": record["material"],
                            "indices": [slot_of[v] for v in record["indices"]],
                            "textured": record["textured"],
                            "quad": record["representation"] == "quad",
                            "remeshed": record["source"] == "remeshed",
                        }
                        for record in entry["primitives"]
                    ],
                }
            )
        args.levels_output.write_text(json.dumps(payload) + "\n")

    for row in summary:
        sweep = row["pose_sweep"]
        worst = sweep.get("worst_relative", 0.0)
        print(
            f"{row['name']:>12}  prim {row['primitive_count']:>4}  "
            f"quad {row['quad_count']:>4}  cmd {row['vdp1_command_count']:>4}  "
            f"vtx {row['referenced_vertex_count']:>4}  "
            f"meshlets {row['meshlet_count']:>3}  "
            f"bytes {row['bytes']['total']:>7}  "
            f"neutral_rel {row['neutral_relative_whole_extent']:.4f}  "
            f"pose_worst {worst:.4f}  qf_fail {row['quadriflow_failures']}"
        )


if __name__ == "__main__":
    main()
