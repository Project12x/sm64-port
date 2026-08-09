#!/usr/bin/env python3
"""RED/GREEN tests for the offline seam-aware-decimation IR<->OBJ shim.

Task 7 of the 2026-08-09 memory-residency-campaign plan (parallel, offline
prototype lane -- no build-system integration, no identity wiring). This
module proves the IR<->OBJ primitives the real decimation driver depends on:

  * Positions and UVs share a single index (1:1 by construction) both when
    writing OBJ and when reading it back -- SeamAwareDecimater is fed a v/vt
    stream where index i always names the same (position, uv) pair, so any
    face whose v and vt indices diverge is a shim bug, not a valid input.
  * `texture_tile` (a large, per-triangle Saturn render-state blob) has no
    home in Wavefront OBJ. It is constant per material in the real BOB mesh
    IR (verified against the generated file below), so it round-trips via a
    material-id side channel built from the template document, not through
    OBJ text.
  * `source` (original source-triangle id) also has no home in OBJ. For the
    disabled-decimation identity path, source order is preserved exactly
    (face order == triangle order, so `source[i]` still means what it meant
    before); for real decimation, source ids are intentionally reset to a
    new sequential 0..N-1 range because decimation invalidates the original
    provenance identity (merged/removed triangles have no single ancestor).

See tools/saturn/mesh_ir_obj_shim.py for the implementation and
docs/saturn/evidence/reports/memcamp-decimation-prototype-*.md for the real
SeamAwareDecimater run this shim drives.
"""
from __future__ import annotations

import copy
import json
from pathlib import Path

from mesh_ir_obj_shim import (
    build_material_texture_tile_table,
    mesh_ir_to_obj,
    material_submesh_obj,
    obj_to_mesh_ir,
    parse_obj,
    quantize_component,
    reassemble_decimated_mesh_ir,
)

try:
    from saturn_mesh_ir import validate_mesh_ir
except ImportError:  # pragma: no cover - saturn_mesh_ir.py always exists in this repo
    validate_mesh_ir = None


ROOT = Path(__file__).resolve().parents[2]
REAL_BOB_MESH_IR = (
    ROOT / "build" / "saturn" / "sourceboot" / "generated" / "bob_area1_mesh_ir_v2.json"
)


def _tiny_fixture() -> dict:
    """A hand-built, two-material v2 mesh IR document.

    Material 0 is a unit quad (2 triangles, 4 vertices); material 1 is a
    second unit quad translated in X (2 triangles, 4 vertices). No vertex is
    shared between materials, matching the real BOB mesh's invariant
    (verified separately: zero vertices are shared across >1 material there).
    UVs are deliberately distinct per vertex and include negative values, to
    exercise the same integer range the real generated UVs use.
    """
    positions = [
        [0, 0, 0], [10, 0, 0], [10, 10, 0], [0, 10, 0],
        [20, 0, 0], [30, 0, 0], [30, 10, 0], [20, 10, 0],
    ]
    uvs = [
        [0, 0], [100, 0], [100, 100], [0, 100],
        [-50, -25], [50, -25], [50, 75], [-50, 75],
    ]
    tile_a = {"texture": "generic_a", "state": {"format": "G_IM_FMT_RGBA", "uls": 0, "lrs": 31}}
    tile_b = {"texture": "generic_b", "state": {"format": "G_IM_FMT_RGBA", "uls": 0, "lrs": 63}}
    triangles = [
        {"source": 0, "material": 0, "indices": [0, 1, 2], "texture_tile": tile_a},
        {"source": 1, "material": 0, "indices": [0, 2, 3], "texture_tile": tile_a},
        {"source": 2, "material": 1, "indices": [4, 5, 6], "texture_tile": tile_b},
        {"source": 3, "material": 1, "indices": [4, 6, 7], "texture_tile": tile_b},
    ]
    return {
        "schema": "sm64-saturn-mesh-ir",
        "version": 2,
        "name": "shim_fixture",
        "source": {"path": "fixtures/shim", "intake_schema": "sm64-saturn-static-scene-intake", "intake_version": 3},
        "static_world_space": True,
        "positions": positions,
        "materials": [
            {"id": 0, "rgb555": [31, 31, 31], "texture": "generic_a"},
            {"id": 1, "rgb555": [31, 31, 31], "texture": "generic_b"},
        ],
        "triangles": triangles,
        "vertex_attributes": {"uv": uvs},
        "pairing_forbidden_triangles": [],
        "validation_poses": [],
    }


def _canonical(document: dict) -> str:
    return json.dumps(document, indent=2, sort_keys=True) + "\n"


def test_quantize_component_is_deterministic_round_half_away_from_zero() -> None:
    assert quantize_component(4864.0) == 4864
    assert quantize_component(-6504.0) == -6504
    assert quantize_component(0.4999999999) == 0
    assert quantize_component(0.5) == 1
    assert quantize_component(-0.5) == -1
    assert quantize_component(2.5) == 3
    assert quantize_component(-2.5) == -3
    # Determinism: repeated calls with the same input never disagree.
    for _ in range(3):
        assert quantize_component(1234.0000001) == 1234


def test_build_material_texture_tile_table_matches_constant_per_material() -> None:
    document = _tiny_fixture()
    table = build_material_texture_tile_table(document)
    assert table[0] == document["triangles"][0]["texture_tile"]
    assert table[1] == document["triangles"][2]["texture_tile"]


def test_build_material_texture_tile_table_rejects_inconsistent_material() -> None:
    document = _tiny_fixture()
    document["triangles"][1]["texture_tile"] = {"texture": "different", "state": {}}
    try:
        build_material_texture_tile_table(document)
    except ValueError as error:
        assert "material 0" in str(error)
    else:
        raise AssertionError("expected ValueError for a material with >1 distinct texture_tile")


def test_parse_obj_rejects_v_vt_index_mismatch() -> None:
    text = "v 0 0 0\nv 1 0 0\nv 1 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nf 1/2 2/1 3/3\n"
    try:
        parse_obj(text)
    except ValueError as error:
        assert "1:1" in str(error) or "invariant" in str(error)
    else:
        raise AssertionError("expected ValueError when a face's v and vt indices diverge")


def test_parse_obj_rejects_non_triangle_face() -> None:
    text = (
        "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
        "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
        "f 1/1 2/2 3/3 4/4\n"
    )
    try:
        parse_obj(text)
    except ValueError as error:
        assert "triangulated" in str(error)
    else:
        raise AssertionError("expected ValueError for a non-triangulated (quad) face")


def test_parse_obj_ignores_usemtl_mtllib_and_comments() -> None:
    text = (
        "# a comment\n"
        "mtllib fixture.mtl\n"
        "v 0 0 0\nv 1 0 0\nv 1 1 0\n"
        "vt 0 0\nvt 1 0\nvt 1 1\n"
        "usemtl material_5\n"
        "f 1/1 2/2 3/3\n"
    )
    parsed = parse_obj(text)
    assert len(parsed.positions) == 3
    assert len(parsed.faces) == 1
    assert parsed.faces[0].material == "material_5"


def test_material_submesh_obj_local_reindexing_isolates_one_material() -> None:
    document = _tiny_fixture()
    obj_text, vertex_count = material_submesh_obj(document, 1)
    assert vertex_count == 4
    parsed = parse_obj(obj_text)
    assert len(parsed.positions) == 4
    assert parsed.positions[0] == document["positions"][4]
    assert len(parsed.faces) == 2
    for face in parsed.faces:
        assert all(0 <= index < 4 for index in face.indices)


def test_round_trip_identity_is_byte_identical_on_tiny_fixture() -> None:
    document = _tiny_fixture()
    original = copy.deepcopy(document)
    obj_text = mesh_ir_to_obj(document)
    round_tripped = obj_to_mesh_ir(obj_text, template_document=document, reset_source_ids=False)
    assert _canonical(round_tripped) == _canonical(original), (
        "IR->OBJ->IR with decimation disabled must be byte-identical"
    )


def test_round_trip_identity_is_byte_identical_on_real_bob_mesh() -> None:
    if not REAL_BOB_MESH_IR.exists():
        print(f"SKIP: real BOB mesh IR not present at {REAL_BOB_MESH_IR} (offline prototype; no build dependency)")
        return
    document = json.loads(REAL_BOB_MESH_IR.read_text(encoding="utf-8"))
    original = copy.deepcopy(document)
    obj_text = mesh_ir_to_obj(document)
    round_tripped = obj_to_mesh_ir(obj_text, template_document=document, reset_source_ids=False)
    assert _canonical(round_tripped) == _canonical(original), (
        "IR->OBJ->IR with decimation disabled must be byte-identical on the real BOB mesh"
    )
    if validate_mesh_ir is not None:
        validate_mesh_ir(round_tripped)


def test_reassemble_decimated_mesh_ir_reattaches_texture_tile_and_resets_source() -> None:
    document = _tiny_fixture()
    per_material_obj = {
        material_id: material_submesh_obj(document, material_id)[0]
        for material_id in (0, 1)
    }
    reassembled = reassemble_decimated_mesh_ir(document, per_material_obj)
    assert len(reassembled["positions"]) == 8
    assert len(reassembled["triangles"]) == 4
    sources = [triangle["source"] for triangle in reassembled["triangles"]]
    assert sources == list(range(len(sources))), "decimated output must reset source ids to a dense sequential range"
    for triangle in reassembled["triangles"]:
        expected_tile = document["materials"][triangle["material"]]["texture"]
        assert triangle["texture_tile"]["texture"] == expected_tile
    if validate_mesh_ir is not None:
        validate_mesh_ir(reassembled)


def test_reassemble_drops_triangles_degenerate_after_quantization() -> None:
    """Real-decimation finding (BOB material 8, strict 2): a decimated triangle
    can be valid at full double precision but become a zero-area sliver once
    its positions are quantized to the schema's required integers. The
    reassembler must drop such triangles (downstream BSP tooling rejects
    degenerate polygons outright) rather than hand one through.
    """
    document = _tiny_fixture()
    # Two vertices at 0.4 apart in float space quantize to the same integer.
    obj_text = (
        "# single-material submesh with one collapsed-by-quantization triangle\n"
        "v 0 0 0\nv 10 0 0\nv 10.4 0 0\n"
        "vt 0 0\nvt 100 0\nvt 100 0\n"
        "f 1/1 2/2 3/3\n"
    )
    dropped: list[dict] = []
    reassembled = reassemble_decimated_mesh_ir(
        document, {0: obj_text}, degenerate_report=dropped
    )
    assert len(reassembled["triangles"]) == 0
    assert len(dropped) == 1
    assert dropped[0]["material"] == 0


def test_reassemble_rejects_texture_tile_inconsistency_even_after_decimation() -> None:
    document = _tiny_fixture()
    document["triangles"][3]["texture_tile"] = {"texture": "different", "state": {}}
    per_material_obj = {
        material_id: material_submesh_obj(document, material_id)[0]
        for material_id in (0, 1)
    }
    try:
        reassemble_decimated_mesh_ir(document, per_material_obj)
    except ValueError:
        pass
    else:
        raise AssertionError("expected ValueError: material 1 now has two distinct texture_tile blobs")


def main() -> None:
    test_quantize_component_is_deterministic_round_half_away_from_zero()
    test_build_material_texture_tile_table_matches_constant_per_material()
    test_build_material_texture_tile_table_rejects_inconsistent_material()
    test_parse_obj_rejects_v_vt_index_mismatch()
    test_parse_obj_rejects_non_triangle_face()
    test_parse_obj_ignores_usemtl_mtllib_and_comments()
    test_material_submesh_obj_local_reindexing_isolates_one_material()
    test_round_trip_identity_is_byte_identical_on_tiny_fixture()
    test_round_trip_identity_is_byte_identical_on_real_bob_mesh()
    test_reassemble_decimated_mesh_ir_reattaches_texture_tile_and_resets_source()
    test_reassemble_drops_triangles_degenerate_after_quantization()
    test_reassemble_rejects_texture_tile_inconsistency_even_after_decimation()
    print("mesh_ir_obj_shim: PASS")


if __name__ == "__main__":
    main()
