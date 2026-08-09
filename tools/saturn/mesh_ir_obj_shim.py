#!/usr/bin/env python3
"""IR<->OBJ shim for the offline SeamAwareDecimater prototype (Task 7 of the
2026-08-09 memory-residency-campaign plan).

This is deliberately an OFFLINE, PROTOTYPE-ONLY tool: it has no build-system
integration and is not wired into the sourceboot identity pipeline. It exists
to drive a pinned, MIT-licensed external binary (SeamAwareDecimater, built
from work/upstream/seam-aware-decimater/) against the real BOB terrain mesh
IR and hand its output back to the existing offline compilers
(saturn_mesh_ir.py, compile_bob_bsp.py) unmodified, so the fidelity/size
tradeoff of terrain decimation can be measured with real numbers.

Design notes (see also test_mesh_ir_obj_shim.py's module docstring):

  * ``sm64-saturn-mesh-ir`` v2 documents store one welded vertex stream:
    ``positions[i]`` and ``vertex_attributes.uv[i]`` describe the same
    logical vertex. This shim preserves that "UVs 1:1 by construction"
    property by always writing a face's ``v`` and ``vt`` OBJ indices
    identically (``f i/i j/j k/k``), and by refusing to parse OBJ text where
    they diverge -- SeamAwareDecimater is a genuinely seam-*aware* decimater
    (it protects UV-topology seams from collapsing), but this project's mesh
    IR does not encode UV seams as index splits, so feeding it a 1:1 stream
    means the only "seams" it can discover are true mesh/material boundaries
    (see ``material_submesh_obj`` below).

  * ``texture_tile`` (a large per-triangle Saturn render-state blob) and
    ``source`` (an original source-triangle id) have no representation in
    Wavefront OBJ. Verified against the real generated BOB mesh IR:
    every material's ``texture_tile`` is byte-identical across all of that
    material's triangles, and not one vertex is referenced by more than one
    material. Both facts hold structurally (this shim raises rather than
    silently guessing if they ever stop holding for some input), and both
    are exploited here:
      - ``texture_tile`` round-trips via a material-id side channel
        (``build_material_texture_tile_table``), not through OBJ text.
      - Because materials never share a vertex, decimating each material's
        submesh independently (``material_submesh_obj``) can never merge
        geometry across a material boundary -- SeamAwareDecimater sees each
        material's open boundary edges and protects them like any other
        mesh boundary, and the caller always knows which material produced
        a given decimated OBJ, so material/texture_tile reattachment after
        real decimation is exact, not inferred.

  * SeamAwareDecimater's own ``igl::readOBJ`` call (see decimater.cpp) uses
    the argument-free overload that never returns face-material groups, so
    running it on a *combined* multi-material OBJ would silently discard
    material identity from the output. This shim never does that: real
    decimation always goes through the one-material-at-a-time path.

  * Positions and UVs are re-quantized deterministically on read
    (``quantize_component``: round-half-away-from-zero, a pure function of
    its input) so that decimated float vertex positions become the mesh
    IR's required integers, and so that an OBJ round trip with decimation
    disabled reproduces the exact original integers (doubles represent every
    integer in the schema's signed-16-bit position range exactly, so no
    precision is lost in that path).
"""
from __future__ import annotations

import argparse
import json
import math
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


MATERIAL_PREFIX = "material_"


# --------------------------------------------------------------------------
# Deterministic numeric re-quantization
# --------------------------------------------------------------------------

def quantize_component(value: float) -> int:
    """Round-half-away-from-zero to the nearest integer.

    A pure function of ``value`` -- the same input always produces the same
    output, on any machine, any run. This is the only place float precision
    from OBJ text (or from a real decimated vertex position) becomes an
    integer for the mesh IR schema.
    """
    if value >= 0:
        return int(math.floor(value + 0.5))
    return -int(math.floor(-value + 0.5))


def quantize_vector(values: list[float]) -> list[int]:
    return [quantize_component(value) for value in values]


# --------------------------------------------------------------------------
# Material / texture_tile side channel
# --------------------------------------------------------------------------

def build_material_texture_tile_table(document: dict[str, Any]) -> dict[int, Any]:
    """Map material id -> its single, constant ``texture_tile`` blob.

    Raises ValueError if any material has more than one distinct
    ``texture_tile`` across its triangles -- that would mean OBJ's
    material-id side channel is lossy for this document, and this shim must
    fail closed rather than silently pick one.
    """
    table: dict[int, Any] = {}
    canonical: dict[int, str] = {}
    for triangle in document["triangles"]:
        material_id = triangle["material"]
        tile = triangle.get("texture_tile")
        key = json.dumps(tile, sort_keys=True)
        if material_id in table:
            if canonical[material_id] != key:
                raise ValueError(
                    f"material {material_id} has more than one distinct texture_tile; "
                    "the material-id side channel this shim relies on is lossy for this document"
                )
        else:
            table[material_id] = tile
            canonical[material_id] = key
    return table


# --------------------------------------------------------------------------
# OBJ parsing
# --------------------------------------------------------------------------

@dataclass
class ParsedObjFace:
    material: str | None
    indices: tuple[int, int, int]  # 0-based, into positions/uvs


@dataclass
class ParsedObj:
    positions: list[list[int]] = field(default_factory=list)
    uvs: list[list[int]] = field(default_factory=list)
    faces: list[ParsedObjFace] = field(default_factory=list)


def parse_obj(text: str) -> ParsedObj:
    """Parse a small, deterministic subset of Wavefront OBJ.

    Understands ``v``, ``vt``, ``usemtl``, and triangulated ``f`` lines with
    ``v/vt`` tokens (no normals). Everything else (``vn``, ``mtllib``, ``g``,
    ``o``, ``s``, comments, blank lines) is ignored, matching what
    SeamAwareDecimater's own ``igl::readOBJ`` call tolerates.

    Enforces the "UVs 1:1 by construction" invariant: every face vertex's
    ``v`` and ``vt`` index must be identical, and every face must be a
    triangle. Both are real correctness properties this shim depends on, not
    style preferences, so both raise ValueError rather than silently
    coercing bad input.
    """
    positions: list[list[int]] = []
    uvs: list[list[int]] = []
    faces: list[ParsedObjFace] = []
    current_material: str | None = None

    for line_no, raw_line in enumerate(text.splitlines(), start=1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        tag = parts[0]
        if tag == "v":
            if len(parts) < 4:
                raise ValueError(f"line {line_no}: 'v' needs at least 3 components")
            positions.append(quantize_vector([float(component) for component in parts[1:4]]))
        elif tag == "vt":
            if len(parts) < 3:
                raise ValueError(f"line {line_no}: 'vt' needs at least 2 components")
            uvs.append(quantize_vector([float(component) for component in parts[1:3]]))
        elif tag == "usemtl":
            if len(parts) != 2:
                raise ValueError(f"line {line_no}: 'usemtl' needs exactly one material name")
            current_material = parts[1]
        elif tag == "f":
            if len(parts) != 4:
                raise ValueError(
                    f"line {line_no}: only triangulated faces are supported "
                    f"(got {len(parts) - 1} vertices)"
                )
            local_indices = []
            for token in parts[1:]:
                components = token.split("/")
                if len(components) < 2 or not components[1]:
                    raise ValueError(
                        f"line {line_no}: face vertex {token!r} is missing its vt index "
                        "(shim requires the 1:1 v/vt-by-construction invariant)"
                    )
                v_index = int(components[0])
                vt_index = int(components[1])
                if v_index != vt_index:
                    raise ValueError(
                        f"line {line_no}: face vertex {token!r} breaks the 1:1 v/vt "
                        f"invariant (v={v_index}, vt={vt_index})"
                    )
                local_indices.append(v_index - 1)
            faces.append(ParsedObjFace(material=current_material, indices=tuple(local_indices)))
        # else: vn / mtllib / g / o / s and anything unrecognized are ignored.

    if len(positions) != len(uvs):
        raise ValueError(
            f"positions/uvs length mismatch after parsing ({len(positions)} vs {len(uvs)}); "
            "expected a 1:1 vertex_attributes.uv stream"
        )
    return ParsedObj(positions=positions, uvs=uvs, faces=faces)


# --------------------------------------------------------------------------
# Whole-document (identity round trip) encode/decode
# --------------------------------------------------------------------------

def mesh_ir_to_obj(document: dict[str, Any]) -> str:
    """Render a whole v2 mesh IR document as OBJ text.

    Vertex/face order is preserved exactly (no reordering), which is what
    lets the disabled-decimation round trip be byte-identical: OBJ line N
    always corresponds to IR array index N-1.
    """
    positions = document["positions"]
    uvs = document["vertex_attributes"]["uv"]
    if len(positions) != len(uvs):
        raise ValueError(
            f"mesh IR positions/uv length mismatch ({len(positions)} vs {len(uvs)}); "
            "this shim requires a 1:1 vertex_attributes.uv stream"
        )

    lines = [
        "# Generated by tools/saturn/mesh_ir_obj_shim.py -- offline decimation prototype.",
        "# v/vt share an index (1:1 by construction); texture_tile/source travel out-of-band.",
    ]
    for position in positions:
        lines.append("v %d %d %d" % tuple(position))
    for uv in uvs:
        lines.append("vt %d %d" % tuple(uv))

    current_material: int | None = None
    for triangle in document["triangles"]:
        material_id = triangle["material"]
        if material_id != current_material:
            lines.append(f"usemtl {MATERIAL_PREFIX}{material_id}")
            current_material = material_id
        i0, i1, i2 = (index + 1 for index in triangle["indices"])
        lines.append(f"f {i0}/{i0} {i1}/{i1} {i2}/{i2}")

    return "\n".join(lines) + "\n"


def obj_to_mesh_ir(obj_text: str, *, template_document: dict[str, Any], reset_source_ids: bool) -> dict[str, Any]:
    """Parse whole-document OBJ text back into a v2 mesh IR document.

    ``template_document`` supplies everything OBJ cannot carry: schema/
    version/name/source metadata, the materials table, the texture_tile-by-
    material side channel, and (when ``reset_source_ids`` is False) the
    original per-face ``source`` id, valid only when face order exactly
    matches the template's triangle order -- true for the disabled-
    decimation identity path this function exists for.
    """
    parsed = parse_obj(obj_text)
    texture_tile_table = build_material_texture_tile_table(template_document)
    template_triangles = template_document["triangles"]

    triangles: list[dict[str, Any]] = []
    for index, face in enumerate(parsed.faces):
        if face.material is None or not face.material.startswith(MATERIAL_PREFIX):
            raise ValueError(f"face {index} has no usemtl {MATERIAL_PREFIX}<id> group")
        material_id = int(face.material[len(MATERIAL_PREFIX):])
        if material_id not in texture_tile_table:
            raise ValueError(f"face {index} references unknown material {material_id}")
        if reset_source_ids:
            source_id = index
        else:
            if index >= len(template_triangles):
                raise ValueError(
                    "reset_source_ids=False requires face order to match the template "
                    "document's triangle order exactly, but the OBJ has more faces"
                )
            source_id = template_triangles[index]["source"]
        triangles.append(
            {
                "source": source_id,
                "material": material_id,
                "indices": list(face.indices),
                "texture_tile": texture_tile_table[material_id],
            }
        )

    return {
        "schema": template_document["schema"],
        "version": template_document["version"],
        "name": template_document["name"],
        "source": template_document.get("source", {}),
        "static_world_space": template_document.get("static_world_space", False),
        "positions": parsed.positions,
        "materials": template_document["materials"],
        "triangles": triangles,
        "vertex_attributes": {"uv": parsed.uvs},
        "pairing_forbidden_triangles": template_document.get("pairing_forbidden_triangles", []),
        "validation_poses": template_document.get("validation_poses", []),
    }


# --------------------------------------------------------------------------
# Per-material encode/decode (real decimation driver)
# --------------------------------------------------------------------------

def _split_bowtie_vertices(
    positions: list[list[int]], uvs: list[list[int]], faces: list[tuple[int, int, int]]
) -> tuple[list[list[int]], list[list[int]], list[tuple[int, int, int]]]:
    """Split "bowtie" vertices so every vertex has a single triangle fan.

    Discovered empirically while driving real decimation on the BOB terrain
    mesh: a handful of vertices are shared, by exact welded position, between
    otherwise-disconnected micro-islands of the same material (e.g. two
    separate floor slabs that happen to touch at one point). Their incident
    triangles do not form one edge-connected fan around the vertex -- a
    classic non-manifold "bowtie". SeamAwareDecimater's edge_flaps/
    circulation machinery (``ext/libigl/include/igl/circulation.cpp``)
    assumes a single fan per vertex and hits a real, reproducible assertion
    (``"e should touch ff"``) on bowtie input; it is not a bug in this
    shim's OBJ encoding, and not something ``--strict`` controls.

    The fix is a standard, geometry-preserving mesh preprocessing step:
    duplicate the vertex once per extra fan component, so each copy sits at
    the identical (position, uv) but is referenced only by its own fan's
    triangles. This changes no triangle's shape or texture mapping and adds
    no new boundary that was not already there (the fans were already
    disconnected -- they just could not correctly share one index in an
    edge-flap-based algorithm). The split is a pure function of face order
    (via deterministic union-find, no hashing/set-iteration-order
    dependence in its output), so it does not put determinism at risk.
    """
    vertex_faces: dict[int, list[int]] = {}
    for face_index, face in enumerate(faces):
        for vertex in face:
            vertex_faces.setdefault(vertex, []).append(face_index)

    positions = list(positions)
    uvs = list(uvs)
    faces = [list(face) for face in faces]

    for vertex, incident in vertex_faces.items():
        if len(incident) <= 1:
            continue
        parent = {face_index: face_index for face_index in incident}

        def find(face_index: int) -> int:
            while parent[face_index] != face_index:
                parent[face_index] = parent[parent[face_index]]
                face_index = parent[face_index]
            return face_index

        def union(a: int, b: int) -> None:
            root_a, root_b = find(a), find(b)
            if root_a != root_b:
                parent[root_a] = root_b

        for i in range(len(incident)):
            for j in range(i + 1, len(incident)):
                face_i, face_j = incident[i], incident[j]
                shares_edge = bool(
                    (set(faces[face_i]) - {vertex}) & (set(faces[face_j]) - {vertex})
                )
                if shares_edge:
                    union(face_i, face_j)

        components: dict[int, list[int]] = {}
        for face_index in incident:
            components.setdefault(find(face_index), []).append(face_index)
        if len(components) <= 1:
            continue

        # Leave the first-discovered fan on the original vertex index;
        # duplicate the vertex once per additional fan component.
        for component_faces in list(components.values())[1:]:
            new_index = len(positions)
            positions.append(positions[vertex])
            uvs.append(uvs[vertex])
            for face_index in component_faces:
                faces[face_index] = [
                    new_index if index == vertex else index for index in faces[face_index]
                ]

    return positions, uvs, [tuple(face) for face in faces]


def material_submesh_obj(document: dict[str, Any], material_id: int) -> tuple[str, int]:
    """Render one material's triangles as a self-contained, locally-indexed OBJ.

    Because no vertex in the real BOB mesh IR is shared across materials,
    this partition can never let decimation merge geometry across a
    material boundary -- SeamAwareDecimater sees each material's true open
    edges as mesh boundaries and protects them, same as it would for a
    single free-standing mesh. Bowtie vertices (see
    ``_split_bowtie_vertices``) are split before emitting OBJ text.

    Returns ``(obj_text, vertex_count)``.
    """
    positions = document["positions"]
    uvs = document["vertex_attributes"]["uv"]
    global_to_local: dict[int, int] = {}
    local_positions: list[list[int]] = []
    local_uvs: list[list[int]] = []
    local_faces: list[tuple[int, int, int]] = []

    for triangle in document["triangles"]:
        if triangle["material"] != material_id:
            continue
        local_indices = []
        for global_index in triangle["indices"]:
            local_index = global_to_local.get(global_index)
            if local_index is None:
                local_index = len(local_positions)
                global_to_local[global_index] = local_index
                local_positions.append(positions[global_index])
                local_uvs.append(uvs[global_index])
            local_indices.append(local_index)
        local_faces.append(tuple(local_indices))

    if not local_positions:
        raise ValueError(f"material {material_id} has no triangles in this document")

    local_positions, local_uvs, local_faces = _split_bowtie_vertices(
        local_positions, local_uvs, local_faces
    )

    lines = ["# Generated by tools/saturn/mesh_ir_obj_shim.py -- single-material submesh."]
    lines.extend("v %d %d %d" % tuple(position) for position in local_positions)
    lines.extend("vt %d %d" % tuple(uv) for uv in local_uvs)
    for i0, i1, i2 in local_faces:
        lines.append(f"f {i0 + 1}/{i0 + 1} {i1 + 1}/{i1 + 1} {i2 + 1}/{i2 + 1}")
    return "\n".join(lines) + "\n", len(local_positions)


def _is_degenerate_triangle(p0: list[int], p1: list[int], p2: list[int]) -> bool:
    """True if the three (already-quantized-to-integer) positions are collinear/coincident."""
    ax, ay, az = (p1[i] - p0[i] for i in range(3))
    bx, by, bz = (p2[i] - p0[i] for i in range(3))
    cross = (ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx)
    return cross == (0, 0, 0)


def reassemble_decimated_mesh_ir(
    document: dict[str, Any],
    decimated_obj_by_material: dict[int, str],
    *,
    degenerate_report: list[dict[str, Any]] | None = None,
) -> dict[str, Any]:
    """Merge per-material (possibly decimated) OBJ text back into one v2 mesh IR document.

    ``source`` ids are reset to a dense sequential 0..N-1 range: real
    decimation merges and removes triangles, so no single ancestor id is
    correct any more. ``texture_tile`` is reattached from ``document``'s
    material-id side channel (verbatim, per material) -- exact, not
    inferred, because the caller already knows which material produced each
    OBJ text. ``pairing_forbidden_triangles`` and ``validation_poses`` are
    dropped: both index into the pre-decimation triangle/position arrays and
    would silently mean the wrong thing if carried through unchanged.

    Real-decimation finding (BOB material 8, strict 2): SeamAwareDecimater's
    edge-collapse/foldover guards protect each individual edge collapse, but
    two *different* surviving vertex indices can still end up at the exact
    same position -- observed directly in decimater.exe's own OBJ output,
    not introduced by quantize_component's rounding. Two boundary-protected
    vertices (a shared corner split into separate fans by
    ``_split_bowtie_vertices``, or two independently-preserved boundary
    vertices) can both survive decimation unmoved at an identical position;
    if a later stage (quad/triangle-fallback pairing) then picks two of them
    as corners of the same output primitive, the result is a zero-area
    sliver. ``static_bsp.py``'s ``plane_for_polygon`` correctly rejects
    degenerate polygons outright, so this reassembly step must not hand it
    one. Such triangles are dropped here (their existence is expected at the
    margin; their exact source data is recorded for transparency -- see the
    evidence report); when ``degenerate_report`` is given, one dict is
    appended per dropped triangle.
    """
    texture_tile_table = build_material_texture_tile_table(document)
    positions: list[list[int]] = []
    uvs: list[list[int]] = []
    triangles: list[dict[str, Any]] = []

    for material_id in sorted(decimated_obj_by_material):
        if material_id not in texture_tile_table:
            raise ValueError(f"decimated material {material_id} is not in the source document")
        parsed = parse_obj(decimated_obj_by_material[material_id])
        offset = len(positions)
        positions.extend(parsed.positions)
        uvs.extend(parsed.uvs)
        for face in parsed.faces:
            global_indices = [offset + index for index in face.indices]
            p0, p1, p2 = (positions[index] for index in global_indices)
            if _is_degenerate_triangle(p0, p1, p2):
                if degenerate_report is not None:
                    degenerate_report.append(
                        {
                            "material": material_id,
                            "indices": global_indices,
                            "positions": [p0, p1, p2],
                        }
                    )
                continue
            triangles.append(
                {
                    "source": len(triangles),
                    "material": material_id,
                    "indices": global_indices,
                    "texture_tile": texture_tile_table[material_id],
                }
            )

    return {
        "schema": document["schema"],
        "version": document["version"],
        "name": document["name"],
        "source": document.get("source", {}),
        "static_world_space": document.get("static_world_space", False),
        "positions": positions,
        "materials": document["materials"],
        "triangles": triangles,
        "vertex_attributes": {"uv": uvs},
        "pairing_forbidden_triangles": [],
        "validation_poses": [],
    }


# --------------------------------------------------------------------------
# Real decimation driver (invokes the pinned SeamAwareDecimater binary)
# --------------------------------------------------------------------------

# The MinGW-built decimater.exe dynamically links libstdc++-6.dll /
# libgcc_s_seh-1.dll / libwinpthread-1.dll from its MSYS2 toolchain
# directory. Invoked with a bare PATH (e.g. from this repo's
# .venv-saturn-tools interpreter, outside an MSYS2 login shell), the process
# does not fail to start cleanly -- it starts and crashes with
# STATUS_ACCESS_VIOLATION before main() runs any user code. Confirmed by
# direct comparison: identical invocation, only difference is this
# directory on PATH, crash vs. exit 0. Fold it in defensively rather than
# require every caller to remember an MSYS2 shell wrapper.
_MINGW64_BIN = Path(r"C:\msys64\mingw64\bin")


def _decimater_subprocess_env() -> dict[str, str]:
    import os

    env = dict(os.environ)
    if _MINGW64_BIN.is_dir():
        env["PATH"] = str(_MINGW64_BIN) + os.pathsep + env.get("PATH", "")
    return env


def run_decimater_binary(
    binary: Path, input_obj: Path, output_obj: Path, *, percent_vertices: int, strict: int
) -> subprocess.CompletedProcess[str]:
    # .venv-saturn-tools' subprocess.Popen has a known, separately-chipped
    # defect where relative executable paths fail with
    # FileNotFoundError/WinError 2 even when the file exists relative to the
    # current working directory. Resolve to an absolute path defensively so
    # this driver works regardless of which interpreter invokes it.
    return subprocess.run(
        [
            str(Path(binary).resolve()),
            str(Path(input_obj).resolve()),
            "percent-vertices",
            str(percent_vertices),
            "--strict",
            str(strict),
            str(Path(output_obj).resolve()),
        ],
        capture_output=True,
        text=True,
        check=True,
        env=_decimater_subprocess_env(),
    )


def decimate_mesh_ir(
    document: dict[str, Any],
    *,
    binary: Path,
    percent_vertices: int,
    strict: int,
    workdir: Path,
) -> tuple[dict[str, Any], dict[str, Any]]:
    """Decimate every material's submesh independently via the pinned binary.

    Materials too small for the requested percentage (target vertex count
    would be 0 or >= the material's own vertex count -- decimater.exe
    requires ``0 < target < input``) are kept at full resolution rather than
    invoking the binary, and flagged ``skipped`` in the returned report.
    """
    workdir.mkdir(parents=True, exist_ok=True)
    decimated_obj_by_material: dict[int, str] = {}
    per_material_report: list[dict[str, Any]] = []

    material_ids = sorted({triangle["material"] for triangle in document["triangles"]})
    for material_id in material_ids:
        obj_text, vertex_count = material_submesh_obj(document, material_id)
        target = round(percent_vertices * vertex_count / 100)
        if target <= 0 or target >= vertex_count:
            decimated_obj_by_material[material_id] = obj_text
            per_material_report.append(
                {
                    "material": material_id,
                    "input_vertices": vertex_count,
                    "output_vertices": vertex_count,
                    "skipped": True,
                    "reason": f"target {target} out of (0, {vertex_count}) at {percent_vertices}%",
                }
            )
            continue

        input_path = workdir / f"material_{material_id:02d}_in.obj"
        output_path = workdir / f"material_{material_id:02d}_out.obj"
        input_path.write_text(obj_text, encoding="utf-8", newline="\n")
        if output_path.exists():
            output_path.unlink()
        result = run_decimater_binary(
            binary, input_path, output_path, percent_vertices=percent_vertices, strict=strict
        )
        decimated_text = output_path.read_text(encoding="utf-8")
        decimated_obj_by_material[material_id] = decimated_text
        parsed_out = parse_obj(decimated_text)
        per_material_report.append(
            {
                "material": material_id,
                "input_vertices": vertex_count,
                "output_vertices": len(parsed_out.positions),
                "target_vertices": target,
                "skipped": False,
                "returncode": result.returncode,
            }
        )

    degenerate_report: list[dict[str, Any]] = []
    reassembled = reassemble_decimated_mesh_ir(
        document, decimated_obj_by_material, degenerate_report=degenerate_report
    )
    report = {
        "percent_vertices": percent_vertices,
        "strict": strict,
        "input_positions": len(document["positions"]),
        "input_triangles": len(document["triangles"]),
        "output_positions": len(reassembled["positions"]),
        "output_triangles": len(reassembled["triangles"]),
        "degenerate_triangles_dropped": len(degenerate_report),
        "degenerate_triangles": degenerate_report,
        "materials": per_material_report,
    }
    return reassembled, report


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def _canonical_json(document: dict[str, Any]) -> str:
    return json.dumps(document, indent=2, sort_keys=True) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mode", choices=("identity", "decimate"), required=True)
    parser.add_argument("--decimater-binary", type=Path, help="required for --mode decimate")
    parser.add_argument("--percent-vertices", type=int, default=50)
    parser.add_argument("--strict", type=int, default=2)
    parser.add_argument("--workdir", type=Path, default=Path("mesh_ir_obj_shim_work"))
    parser.add_argument("--report", type=Path)
    args = parser.parse_args(argv)

    document = json.loads(args.input.read_text(encoding="utf-8"))

    if args.mode == "identity":
        obj_text = mesh_ir_to_obj(document)
        output_document = obj_to_mesh_ir(obj_text, template_document=document, reset_source_ids=False)
        report: dict[str, Any] = {
            "mode": "identity",
            "byte_identical": _canonical_json(output_document) == _canonical_json(document),
        }
    else:
        if args.decimater_binary is None:
            parser.error("--mode decimate requires --decimater-binary")
        output_document, report = decimate_mesh_ir(
            document,
            binary=args.decimater_binary,
            percent_vertices=args.percent_vertices,
            strict=args.strict,
            workdir=args.workdir,
        )
        report["mode"] = "decimate"

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(_canonical_json(output_document), encoding="utf-8", newline="\n")
    if args.report is not None:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")
    else:
        print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
