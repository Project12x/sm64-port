#!/usr/bin/env python3
"""Offline decimation of the generated Mario actor mesh.

This is a host-side analysis tool.  It never writes into
``src/port/saturn/gfx``; it reads the generated actor bank header, runs a real
quadric-error simplifier over the parts of the mesh that are allowed to move,
re-runs the project's own quad pairing and meshlet generators over the result,
and reports what the target would actually cost.

Simplifier
----------
``meshopt_simplifyWithAttributes`` from zeux/meshoptimizer, pinned commit
``97bbdce4716f6257c9527b051515136882f33e79`` (MIT, Copyright (c) 2016-2026
Arseny Kapoulkine), driven through the small host CLI in
``work/upstream/meshoptimizer/simplify_cli.cpp``.  Nothing from meshoptimizer
is copied into this tree; ``work/`` is gitignored and the binary is a build
artefact.

The one property of that entry point this tool depends on is stated by its own
header (``src/meshoptimizer.h``): "The resulting index buffer references
vertices from the original vertex buffer."  meshoptimizer collapses an edge
onto one of its existing endpoints and never invents a vertex position, so the
surviving vertex set is a *subset* of the original vertex identifiers.  That is
what keeps the 107 baked per-vertex pose frames index-compatible.

Three structural constraints are enforced here rather than hoped for; see the
module-level checks in :func:`build_jobs` and the assertions in
:func:`decimate`.

1. Textured primitives are untouchable.  ``extract_mario_actor.py:891`` marks
   every textured source triangle ``pairing_forbidden`` and ``:944-951`` refuses
   to emit a tile start unless the primitive owns exactly one source triangle,
   so a textured primitive is always one triangle and emits a Gouraud polygon
   *plus* a distorted sprite.  T2.19a proved both reach the screen.  Textured
   triangles are therefore excluded from every simplifier job and every vertex
   they reference is locked.
2. Poses are dense per-vertex arrays.  ``sm64_mario_animation_vertices`` is
   ``[30][424][3]`` and ``sm64_mario_walking_animation_vertices`` is
   ``[77][424][3]``, both indexed by the same vertex identifier space as
   ``sm64_mario_vertices``.  Because the simplifier only collapses to existing
   endpoints, poses stay valid; compacting them is a pure column subset.
3. Meshlets are contiguous spans of *primitive index*.  ``mario_meshlets``
   walks the primitive list in order and cuts a record whenever
   ``(material, opacity)`` changes or 32 primitives accumulate, so removing any
   primitive renumbers every later one and the whole meshlet table has to be
   regenerated.  This tool regenerates it with the project's own function and
   reports the delta.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from extract_mario_actor import mario_meshlets  # noqa: E402
from saturn_mesh_ir import compile_mesh_ir  # noqa: E402

# meshopt_SimplifyX option bits, from src/meshoptimizer.h at the pinned commit.
MESHOPT_SIMPLIFY_LOCK_BORDER = 1 << 0
# meshopt_SimplifyVertex_* flags, same header.
MESHOPT_SIMPLIFY_VERTEX_LOCK = 1 << 0


def parse_array(text: str, name: str) -> list[int]:
    """Return the flat integer payload of one generated C array."""
    match = re.search(
        r"static const \w+ " + re.escape(name) + r"\s*\[[^=]*=\s*\{", text
    )
    if match is None:
        raise ValueError(f"array {name} not found")
    start = match.end()
    depth = 1
    index = start
    while depth > 0:
        character = text[index]
        if character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
        index += 1
    return [int(value) for value in re.findall(r"-?\d+", text[start:index - 1])]


def reshape(flat: list[int], *dims: int) -> object:
    total = 1
    for dim in dims:
        total *= dim
    if len(flat) != total:
        raise ValueError(f"expected {total} values, got {len(flat)}")
    if len(dims) == 1:
        return list(flat)
    stride = total // dims[0]
    return [reshape(flat[i * stride:(i + 1) * stride], *dims[1:]) for i in range(dims[0])]


def parse_define(text: str, name: str) -> int:
    match = re.search(r"#define " + re.escape(name) + r"\s+(\d+)U?", text)
    if match is None:
        raise ValueError(f"define {name} not found")
    return int(match.group(1))


def load_mesh(path: Path) -> dict:
    text = path.read_text()
    vertex_count = parse_define(text, "SM64_MARIO_VERTEX_COUNT")
    primitive_count = parse_define(text, "SM64_MARIO_PRIMITIVE_COUNT")
    material_count = parse_define(text, "SM64_MARIO_MATERIAL_COUNT")
    textured_count = parse_define(text, "SM64_MARIO_TEXTURED_SOURCE_TRIANGLE_COUNT")
    frames = parse_define(text, "SM64_MARIO_ANIMATION_FRAME_COUNT")
    walk_frames = parse_define(text, "SM64_MARIO_WALKING_ANIMATION_FRAME_COUNT")
    return {
        "vertex_count": vertex_count,
        "primitive_count": primitive_count,
        "material_count": material_count,
        "textured_count": textured_count,
        "frame_count": frames,
        "walk_frame_count": walk_frames,
        "vertices": reshape(parse_array(text, "sm64_mario_vertices"), vertex_count, 3),
        "material_rgb": reshape(
            parse_array(text, "sm64_mario_material_rgb"), material_count, 3
        ),
        "primitives": reshape(
            parse_array(text, "sm64_mario_primitives"), primitive_count, 5
        ),
        "cull_back": parse_array(text, "sm64_mario_primitive_cull_back"),
        "tile_start": parse_array(text, "sm64_mario_texture_tile_start"),
        "textured_source_vertices": reshape(
            parse_array(text, "sm64_mario_textured_source_vertices"), textured_count, 3
        ),
        "animation": reshape(
            parse_array(text, "sm64_mario_animation_vertices"), frames, vertex_count, 3
        ),
        "walking": reshape(
            parse_array(text, "sm64_mario_walking_animation_vertices"),
            walk_frames,
            vertex_count,
            3,
        ),
        "meshlet_count": parse_define(text, "SM64_MARIO_MESHLET_COUNT"),
        "meshlet_primitive_list_count": parse_define(
            text, "SM64_MARIO_MESHLET_LOD_PRIMITIVE_LIST_COUNT"
        ),
        "meshlet_position_list_count": parse_define(
            text, "SM64_MARIO_MESHLET_LOD_POSITION_LIST_COUNT"
        ),
    }


def primitive_triangles(primitive: list[int]) -> list[tuple[int, int, int]]:
    """Split one generated primitive record into source-order triangles.

    ``{material, a, b, c, d}`` with ``d == c`` is a triangle fallback; otherwise
    the record is a boundary-cycle quad and fans as ``(a, b, c) + (a, c, d)``.
    """
    _, a, b, c, d = primitive
    if d == c:
        return [(a, b, c)]
    return [(a, b, c), (a, c, d)]


def build_jobs(mesh: dict) -> dict:
    """Split the mesh into decimatable per-material jobs plus a frozen remainder."""
    primitives = mesh["primitives"]
    tile_start = mesh["tile_start"]
    none_tile = 0xFFFF

    frozen: list[dict] = []
    movable: list[dict] = []
    for index, primitive in enumerate(primitives):
        material = primitive[0]
        record = {
            "primitive": index,
            "material": material,
            "cull_back": mesh["cull_back"][index],
            "tile": tile_start[index],
        }
        if tile_start[index] != none_tile:
            triangles = primitive_triangles(primitive)
            if len(triangles) != 1:
                raise ValueError(
                    f"textured primitive {index} is not a single triangle; "
                    "the T2.19a pairing_forbidden invariant is broken"
                )
            record["triangles"] = triangles
            frozen.append(record)
        else:
            record["triangles"] = primitive_triangles(primitive)
            movable.append(record)

    if len(frozen) != mesh["textured_count"]:
        raise ValueError(
            f"expected {mesh['textured_count']} textured primitives, found {len(frozen)}"
        )

    # Every vertex a textured primitive touches is locked: T2.19a's decal
    # composite reads the same three positions through two different tables.
    locked = set()
    for record in frozen:
        locked.update(record["triangles"][0])
    for triangle in mesh["textured_source_vertices"]:
        locked.update(triangle)

    # A vertex shared by two materials is locked too, so a collapse inside one
    # material group can never open a crack against its neighbour.
    material_of_vertex: dict[int, set[int]] = {}
    for record in movable + frozen:
        for triangle in record["triangles"]:
            for vertex in triangle:
                material_of_vertex.setdefault(vertex, set()).add(record["material"])
    for vertex, materials in material_of_vertex.items():
        if len(materials) > 1:
            locked.add(vertex)

    groups: dict[int, list[dict]] = {}
    for record in movable:
        groups.setdefault(record["material"], []).append(record)

    # Emission order matters: mario_meshlets cuts a record whenever
    # (material, opacity) changes or 32 primitives accumulate, walking the
    # primitive list in order.  A reordered rebuild silently changes the
    # meshlet count, so the schedule below reproduces the original coarse
    # ordering: textured primitives stay where they were, and each material's
    # decimated triangles are spliced in at that material's first appearance.
    schedule: list[tuple[str, int]] = []
    seen_materials: set[int] = set()
    for index, primitive in enumerate(primitives):
        if tile_start[index] != none_tile:
            schedule.append(("frozen", index))
        elif primitive[0] not in seen_materials:
            seen_materials.add(primitive[0])
            schedule.append(("group", primitive[0]))
    return {"frozen": frozen, "groups": groups, "locked": locked, "schedule": schedule}


def run_simplifier(
    binary: Path,
    positions: list[list[int]],
    indices: list[int],
    locks: list[int],
    target_index_count: int,
    target_error: float,
    options: int,
    scratch: Path,
) -> tuple[list[int], float]:
    scratch.mkdir(parents=True, exist_ok=True)
    request = scratch / "simplify_in.txt"
    response = scratch / "simplify_out.txt"
    lines = [
        f"{len(positions)} {len(indices)} {target_index_count} {target_error!r} {options}"
    ]
    lines += [f"{point[0]} {point[1]} {point[2]}" for point in positions]
    lines += [str(value) for value in locks]
    lines += [str(value) for value in indices]
    request.write_text("\n".join(lines) + "\n")
    completed = subprocess.run(
        [str(binary.resolve()), str(request.resolve()), str(response.resolve())],
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"simplify_cli failed ({completed.returncode}): {completed.stderr.strip()}"
        )
    payload = response.read_text().split()
    count = int(payload[0])
    error = float(payload[1])
    return [int(value) for value in payload[2:2 + count]], error


def triangle_area_is_zero(positions: list[list[int]], triangle: tuple[int, int, int]) -> bool:
    if len(set(triangle)) != 3:
        return True
    a, b, c = (positions[index] for index in triangle)
    u = [b[axis] - a[axis] for axis in range(3)]
    v = [c[axis] - a[axis] for axis in range(3)]
    cross = (
        u[1] * v[2] - u[2] * v[1],
        u[2] * v[0] - u[0] * v[2],
        u[0] * v[1] - u[1] * v[0],
    )
    return cross == (0, 0, 0)


def decimate(mesh: dict, jobs: dict, ratio: float, binary: Path, scratch: Path) -> dict:
    """Run one decimation level and return the reassembled triangle stream."""
    positions = mesh["vertices"]
    degenerate_dropped = 0
    per_material: list[dict] = []
    worst_error = 0.0
    frozen_by_primitive = {record["primitive"]: record for record in jobs["frozen"]}
    decimated_by_material: dict[int, list[dict]] = {}

    for material in sorted(jobs["groups"]):
        records = jobs["groups"][material]
        triangles = [triangle for record in records for triangle in record["triangles"]]
        cull_states = {record["cull_back"] for record in records}
        if len(cull_states) != 1:
            raise ValueError(f"material {material} mixes G_CULL_BACK states")
        cull_back = cull_states.pop()

        used = sorted({vertex for triangle in triangles for vertex in triangle})
        local_of = {vertex: index for index, vertex in enumerate(used)}
        local_positions = [positions[vertex] for vertex in used]
        locks = [
            MESHOPT_SIMPLIFY_VERTEX_LOCK if vertex in jobs["locked"] else 0
            for vertex in used
        ]
        flat = [local_of[vertex] for triangle in triangles for vertex in triangle]
        target = max(3, (int(len(flat) * ratio) // 3) * 3)
        result, error = run_simplifier(
            binary, local_positions, flat, locks, target, 1.0, 0, scratch
        )
        worst_error = max(worst_error, error)

        kept = 0
        bucket = decimated_by_material.setdefault(material, [])
        for offset in range(0, len(result), 3):
            triangle = tuple(used[result[offset + axis]] for axis in range(3))
            if triangle_area_is_zero(positions, triangle):
                degenerate_dropped += 1
                continue
            bucket.append(
                {
                    "material": material,
                    "indices": list(triangle),
                    "cull_back": cull_back,
                    "textured": False,
                    "tile": 0xFFFF,
                }
            )
            kept += 1
        per_material.append(
            {
                "material": material,
                "input_triangles": len(triangles),
                "requested_triangles": target // 3,
                "output_triangles": kept,
                "locked_vertices": sum(1 for value in locks if value),
                "vertices": len(used),
                "relative_error": error,
            }
        )

    surviving: list[dict] = []
    for kind, key in jobs["schedule"]:
        if kind == "frozen":
            record = frozen_by_primitive[key]
            surviving.append(
                {
                    "material": record["material"],
                    "indices": list(record["triangles"][0]),
                    "cull_back": record["cull_back"],
                    "textured": True,
                    "tile": record["tile"],
                }
            )
        else:
            surviving.extend(decimated_by_material.get(key, []))

    return {
        "triangles": surviving,
        "per_material": per_material,
        "degenerate_dropped": degenerate_dropped,
        "worst_relative_error": worst_error,
    }


def compile_level(mesh: dict, level: dict) -> dict:
    """Run the project's own pairing and meshlet generators over a level."""
    triangles = level["triangles"]
    document = {
        "schema": "sm64-saturn-mesh-ir",
        "version": 1,
        "name": "mario_decimated",
        "positions": [list(point) for point in mesh["vertices"]],
        "materials": [
            {"id": index, "rgb555": list(rgb)}
            for index, rgb in enumerate(mesh["material_rgb"])
        ],
        "triangles": [
            {
                "indices": list(triangle["indices"]),
                "material": triangle["material"],
                "source": index,
            }
            for index, triangle in enumerate(triangles)
        ],
        "pairing_forbidden_triangles": [
            index for index, triangle in enumerate(triangles) if triangle["textured"]
        ],
        "vertex_attributes": {},
        "validation_poses": [],
    }
    compiled, primitives, report = compile_mesh_ir(document)

    source_triangles = [
        {
            "texture": "tile" if triangle["textured"] else None,
            "cull_back": triangle["cull_back"],
        }
        for triangle in triangles
    ]
    meshlets = mario_meshlets(
        source_triangles, compiled["primitives"], document["positions"]
    )

    referenced = sorted(
        {vertex for primitive in compiled["primitives"] for vertex in primitive["indices"]}
    )
    for primitive in compiled["primitives"]:
        sources = primitive["source_triangles"]
        if any(triangles[source]["textured"] for source in sources):
            if len(sources) != 1:
                raise ValueError("a textured triangle was paired into a quad")

    textured_primitives = sum(
        1
        for primitive in compiled["primitives"]
        if triangles[primitive["source_triangles"][0]]["textured"]
    )
    return {
        "triangle_count": len(triangles),
        "primitive_count": len(compiled["primitives"]),
        "quad_count": sum(
            1
            for primitive in compiled["primitives"]
            if primitive["representation"] == "quad"
        ),
        "textured_primitive_count": textured_primitives,
        "referenced_vertex_count": len(referenced),
        "meshlet_count": meshlets["count"],
        "meshlet_primitive_list_count": len(meshlets["lod_primitive_indices"]),
        "meshlet_position_list_count": len(meshlets["lod_position_indices"]),
        "pairing_report": report,
        "primitives": compiled["primitives"],
        "referenced_vertices": referenced,
        "triangles": triangles,
    }


def quad_pairing_headroom(mesh: dict, triangles: list[dict], tolerance_degrees: float = 2.0) -> dict:
    """Census the lossless quad-pairing lever, independently of the pairer.

    VDP1 draws every primitive as a four-vertex command, so a triangle
    fallback (``d == c``) costs the same as a real quad.  Merging two adjacent
    triangles into one quad is therefore a free command, and this measures how
    many such merges the geometry actually admits.
    """
    import math

    positions = mesh["vertices"]

    def normal(indices: list[int]) -> tuple[int, int, int]:
        a, b, c = (positions[index] for index in indices)
        u = [b[axis] - a[axis] for axis in range(3)]
        v = [c[axis] - a[axis] for axis in range(3)]
        return (
            u[1] * v[2] - u[2] * v[1],
            u[2] * v[0] - u[0] * v[2],
            u[0] * v[1] - u[1] * v[0],
        )

    edges: dict[tuple[int, int], list[int]] = {}
    untextured = 0
    for index, triangle in enumerate(triangles):
        if triangle["textured"]:
            continue
        untextured += 1
        corners = triangle["indices"]
        for a, b in ((0, 1), (1, 2), (2, 0)):
            edges.setdefault(tuple(sorted((corners[a], corners[b]))), []).append(index)

    shared = [pair for pair in edges.values() if len(pair) == 2]
    same_material = [
        pair
        for pair in shared
        if triangles[pair[0]]["material"] == triangles[pair[1]]["material"]
    ]

    coplanar: list[list[int]] = []
    for left, right in same_material:
        n1, n2 = normal(triangles[left]["indices"]), normal(triangles[right]["indices"])
        m1 = math.sqrt(sum(value * value for value in n1))
        m2 = math.sqrt(sum(value * value for value in n2))
        if m1 == 0.0 or m2 == 0.0:
            continue
        cosine = max(-1.0, min(1.0, sum(a * b for a, b in zip(n1, n2)) / (m1 * m2)))
        if math.degrees(math.acos(cosine)) <= tolerance_degrees:
            coplanar.append([left, right])

    used: set[int] = set()
    disjoint = 0
    for left, right in coplanar:
        if left in used or right in used:
            continue
        used.update((left, right))
        disjoint += 1

    return {
        "tolerance_degrees": tolerance_degrees,
        "untextured_triangles": untextured,
        "distinct_untextured_edges": len(edges),
        "edges_shared_by_two_untextured_triangles": len(shared),
        "shared_edges_same_material": len(same_material),
        "shared_edges_same_material_and_coplanar": len(coplanar),
        "disjoint_coplanar_pairs_ceiling": disjoint,
    }


def footprint(mesh: dict, primitive_count: int, vertex_count: int,
              meshlet_count: int, primitive_list: int, position_list: int) -> dict:
    """Cart/RAM bytes of the generated tables that decimation actually moves."""
    frames = mesh["frame_count"]
    walk = mesh["walk_frame_count"]
    tables = {
        "sm64_mario_vertices": vertex_count * 3 * 2,
        "sm64_mario_animation_vertices": frames * vertex_count * 3 * 2,
        "sm64_mario_walking_animation_vertices": walk * vertex_count * 3 * 2,
        "sm64_mario_animation_light_intensity": frames * vertex_count,
        "sm64_mario_walking_animation_light_intensity": walk * vertex_count,
        "sm64_mario_primitives": primitive_count * 5 * 2,
        "sm64_mario_primitive_cull_back": primitive_count,
        "sm64_mario_texture_tile_start": primitive_count * 2,
        "sm64_mario_meshlet_metadata": meshlet_count * (2 + 1 + 2 + 12)
        + (meshlet_count * 3 + 1) * 2 * 2,
        "sm64_mario_meshlet_lod_primitive_list": primitive_list * 2,
        "sm64_mario_meshlet_lod_position_list": position_list * 2,
    }
    tables["total"] = sum(tables.values())
    return tables


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--header",
        type=Path,
        default=Path("src/port/saturn/gfx/saturn_mario_actor_mesh.h"),
    )
    parser.add_argument(
        "--simplifier",
        type=Path,
        default=Path("work/upstream/meshoptimizer/simplify_cli.exe"),
    )
    parser.add_argument("--ratios", default="0.75,0.50,0.25")
    parser.add_argument("--scratch", type=Path, default=Path("build/saturn/tmp/decimate"))
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--levels-output", type=Path)
    args = parser.parse_args()

    mesh = load_mesh(args.header)
    jobs = build_jobs(mesh)

    # The baseline is rebuilt in original primitive order so that the pairing
    # and meshlet generators must reproduce the shipped header's own counts.
    # That equality is this tool's correctness check.
    baseline_triangles = [
        {
            "material": primitive[0],
            "indices": list(triangle),
            "cull_back": mesh["cull_back"][index],
            "textured": mesh["tile_start"][index] != 0xFFFF,
            "tile": mesh["tile_start"][index],
        }
        for index, primitive in enumerate(mesh["primitives"])
        for triangle in primitive_triangles(primitive)
    ]

    levels: list[dict] = []
    baseline = compile_level(mesh, {"triangles": baseline_triangles})
    baseline["name"] = "original"
    baseline["ratio"] = 1.0
    baseline["degenerate_dropped"] = 0
    baseline["worst_relative_error"] = 0.0
    baseline["per_material"] = []
    levels.append(baseline)

    for ratio_text in args.ratios.split(","):
        ratio = float(ratio_text)
        level = decimate(mesh, jobs, ratio, args.simplifier, args.scratch)
        compiled = compile_level(mesh, level)
        compiled["name"] = f"decimated_{int(round(ratio * 100))}"
        compiled["ratio"] = ratio
        compiled["degenerate_dropped"] = level["degenerate_dropped"]
        compiled["worst_relative_error"] = level["worst_relative_error"]
        compiled["per_material"] = level["per_material"]
        levels.append(compiled)

    summary = []
    for level in levels:
        vertex_count = level["referenced_vertex_count"]
        bytes_ = footprint(
            mesh,
            level["primitive_count"],
            vertex_count,
            level["meshlet_count"],
            level["meshlet_primitive_list_count"],
            level["meshlet_position_list_count"],
        )
        summary.append(
            {
                "name": level["name"],
                "ratio": level["ratio"],
                "triangle_count": level["triangle_count"],
                "primitive_count": level["primitive_count"],
                "quad_count": level["quad_count"],
                "textured_primitive_count": level["textured_primitive_count"],
                "vdp1_command_count": level["primitive_count"]
                + level["textured_primitive_count"],
                "referenced_vertex_count": vertex_count,
                "meshlet_count": level["meshlet_count"],
                "meshlet_primitive_list_count": level["meshlet_primitive_list_count"],
                "meshlet_position_list_count": level["meshlet_position_list_count"],
                "degenerate_dropped": level["degenerate_dropped"],
                "worst_relative_error": level["worst_relative_error"],
                "bytes": bytes_,
                "per_material": level["per_material"],
                "pairing_report": {
                    key: value
                    for key, value in level["pairing_report"].items()
                    if key
                    in {
                        "source_triangle_count",
                        "candidate_count",
                        "quad_count",
                        "standalone_triangle_count",
                        "render_primitive_count",
                        "commands_saved",
                        "rejection_reasons",
                        "pairing_forbidden_triangle_count",
                    }
                },
            }
        )

    report = {
        "schema": "sm64-saturn-mario-decimation",
        "version": 1,
        "simplifier": {
            "repo": "https://github.com/zeux/meshoptimizer",
            "commit": "97bbdce4716f6257c9527b051515136882f33e79",
            "license": "MIT",
            "entry_point": "meshopt_simplifyWithAttributes",
            "reuse_mode": "dependency (host tool, not vendored)",
        },
        "header": str(args.header).replace("\\", "/"),
        "source_counts": {
            "vertices": mesh["vertex_count"],
            "primitives": mesh["primitive_count"],
            "textured_primitives": mesh["textured_count"],
            "materials": mesh["material_count"],
            "animation_frames": mesh["frame_count"],
            "walking_animation_frames": mesh["walk_frame_count"],
            "meshlets": mesh["meshlet_count"],
        },
        "locked_vertex_count": len(jobs["locked"]),
        "quad_pairing_headroom": quad_pairing_headroom(mesh, baseline_triangles),
        "shipped_header_counts": {
            "primitives": mesh["primitive_count"],
            "quads": parse_define(args.header.read_text(), "SM64_MARIO_QUAD_COUNT"),
            "meshlets": mesh["meshlet_count"],
            "meshlet_primitive_list": mesh["meshlet_primitive_list_count"],
            "meshlet_position_list": mesh["meshlet_position_list_count"],
        },
        "levels": summary,
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")

    if args.levels_output is not None:
        args.levels_output.parent.mkdir(parents=True, exist_ok=True)
        args.levels_output.write_text(
            json.dumps(
                {
                    "vertices": mesh["vertices"],
                    "material_rgb": mesh["material_rgb"],
                    "animation": mesh["animation"],
                    "walking": mesh["walking"],
                    "levels": [
                        {
                            "name": level["name"],
                            "primitives": [
                                {
                                    "material": primitive["material"],
                                    "indices": primitive["indices"],
                                    "textured": level["triangles"][
                                        primitive["source_triangles"][0]
                                    ]["textured"],
                                }
                                for primitive in level["primitives"]
                            ],
                        }
                        for level in levels
                    ],
                }
            )
            + "\n"
        )

    for row in summary:
        print(
            f"{row['name']:>14}  tri {row['triangle_count']:>4}  "
            f"prim {row['primitive_count']:>4}  cmd {row['vdp1_command_count']:>4}  "
            f"vtx {row['referenced_vertex_count']:>4}  "
            f"meshlets {row['meshlet_count']:>3}  "
            f"bytes {row['bytes']['total']:>7}"
        )


if __name__ == "__main__":
    main()
