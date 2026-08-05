#!/usr/bin/env python3
"""Compile the complete source Mario actor into a compact Saturn S64B bank."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path
from typing import Iterable

from actor_source import (load_animation_inventory, parse_mario_skeleton,
                          validate_geo_node_vocabulary)
from extract_mario_actor import (
    animation_rotations,
    animation_translation,
    blocks,
    flatten,
    flatten_parts,
    geo_layout_parts,
    mario_meshlets,
    mario_vertex_light_intensities,
    matrix_apply,
    vertex_groups,
)
from saturn_mesh_ir import compile_mesh_ir


MAGIC = b"S64B"
VERSION = 1
FEATURE_COMPACT_CHANNELS = 1 << 0
FEATURE_JOINT_LOCAL_VERTICES = 1 << 1
FEATURE_MESHLETS = 1 << 2
FEATURE_MATERIALS = 1 << 3
FEATURE_SOURCE_HASHES = 1 << 4
FEATURE_MASK = (FEATURE_COMPACT_CHANNELS | FEATURE_JOINT_LOCAL_VERTICES |
                FEATURE_MESHLETS | FEATURE_MATERIALS | FEATURE_SOURCE_HASHES)

# Required public prefix, followed by offsets needed to validate the payload.
HEADER_STRUCT = struct.Struct(">4s9HI32sHH10I")
HEADER_SIZE = (HEADER_STRUCT.size + 3) & ~3
ANIMATION_RECORD_STRUCT = struct.Struct(">IIHHHh")
VERTEX_RECORD_STRUCT = struct.Struct(">hhhHH")
GEOMETRY_HEADER_STRUCT = struct.Struct(">4s7H7I")
JOINT_RECORD_STRUCT = struct.Struct(">hhhhhH")
PART_RECORD_STRUCT = struct.Struct(">HH")
MATERIAL_RECORD_STRUCT = struct.Struct(">BBBB")
MESHLET_RECORD_STRUCT = struct.Struct(">HHBB6h12I")
PRIMITIVE_RECORD_STRUCT = struct.Struct(">5H")


def _align(data: bytearray, alignment: int = 4) -> int:
    while len(data) % alignment:
        data.append(0)
    return len(data)


def _canonical_json(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8")


def _canonical_source_hash(path: Path) -> str:
    """Hash UTF-8 source with universal newlines for checkout-independent IDs."""
    return hashlib.sha256(path.read_text(encoding="utf-8").encode("utf-8")).hexdigest()


def _source_digest(sources: Iterable[dict[str, object]]) -> bytes:
    canonical = b"S64B-SOURCES\x00\x01" + b"".join(
        str(item["path"]).encode("utf-8") + b"\x00" + bytes.fromhex(str(item["sha256"]))
        for item in sources
    )
    return hashlib.sha256(canonical).digest()


def _joint_owners(geo_source: str, part_names: list[str]) -> list[int]:
    zero = [(0, 0, 0)] * 20
    base = geo_layout_parts(geo_source, zero)
    if [part[0] for part in base] != part_names:
        raise ValueError("Mario branch order changed while deriving joint ownership")
    owners: list[int] = []
    for part_index, (_name, matrix, _light) in enumerate(base):
        affected: list[int] = []
        for joint in range(20):
            rotations = list(zero)
            rotations[joint] = (257, 0, 0)
            perturbed = geo_layout_parts(geo_source, rotations)
            if any(abs(a - b) > 1e-7 for a, b in zip(matrix, perturbed[part_index][1])):
                affected.append(joint)
        if not affected:
            raise ValueError(f"Mario render branch {part_index} has no joint owner")
        owners.append(max(affected))
    return owners


def _compile_geometry(root: Path, manifest: dict[str, object]) -> dict[str, object]:
    model_path = root / str(manifest["model"])
    geo_path = root / str(manifest["geo"])
    base_path = root / str(manifest["legacy_validation"]["idle_source"])
    model_source = model_path.read_text(encoding="utf-8")
    geo_source = geo_path.read_text(encoding="utf-8")
    validate_geo_node_vocabulary(geo_source)
    base_source = base_path.read_text(encoding="utf-8")
    parts = geo_layout_parts(
        geo_source, animation_rotations(base_source, 0),
        animation_translation(base_source, 0))
    display_lists = blocks(model_source, "Gfx")
    source_vertices = vertex_groups(model_source)
    triangles: list[dict[str, object]] = []
    current_light = parts[0][2]
    current_texture: str | None = None
    current_combine: str | None = None
    current_cull = True
    for branch_ordinal, (name, matrix, _light) in enumerate(parts):
        current_light, current_texture, current_combine, current_cull = flatten(
            display_lists, source_vertices, name, matrix, current_light, triangles,
            current_texture, current_combine, current_cull,
            capture_local_positions=True, branch_ordinal=branch_ordinal)

    positions: list[list[int]] = []
    vertex_keys: dict[tuple[int, int, int], int] = {}
    vertex_metadata: dict[int, tuple[tuple[int, int, int], int]] = {}
    materials: list[dict[str, object]] = []
    material_ids: dict[tuple[tuple[int, int, int], str | None, str | None], int] = {}
    ir_triangles: list[dict[str, object]] = []
    references: dict[tuple[int, int, int], list[tuple[int, int]]] = {}
    for source_ordinal, triangle in enumerate(triangles):
        material_key = (
            tuple(triangle["rgb"]),
            triangle["texture"] if isinstance(triangle["texture"], str) else None,
            triangle["combine_mode"] if isinstance(triangle["combine_mode"], str) else None,
        )
        if material_key not in material_ids:
            material_ids[material_key] = len(materials)
            materials.append({"material_id": len(materials), "rgb": list(material_key[0]),
                              "texture": material_key[1], "combine_mode": material_key[2]})
        indices: list[int] = []
        for corner, point in enumerate(triangle["positions"]):
            key = tuple(point)
            if key not in vertex_keys:
                vertex_keys[key] = len(positions)
                positions.append(list(key))
            vertex_index = vertex_keys[key]
            metadata = (tuple(triangle["local_positions"][corner]), int(triangle["branch_ordinal"]))
            prior = vertex_metadata.setdefault(vertex_index, metadata)
            if prior != metadata:
                raise ValueError("neutral shared vertex crosses joint ownership")
            indices.append(vertex_index)
            references.setdefault(key, []).append((source_ordinal, corner))
        ir_triangles.append({"source": source_ordinal,
                             "material": material_ids[material_key], "indices": indices})
    source_ir = {
        "schema": "sm64-saturn-mesh-ir", "version": 1, "name": "mario_actor_bank",
        "positions": positions,
        "materials": [{"id": item["material_id"], "rgb555": item["rgb"],
                       "texture": item["texture"], "combine_mode": item["combine_mode"]}
                      for item in materials],
        "triangles": ir_triangles,
        "pairing_forbidden_triangles": [index for index, item in enumerate(triangles)
                                        if item["texture"] is not None],
        "vertex_attributes": {}, "validation_poses": [],
        "source": {"model": {"path": str(manifest["model"]),
                              "sha256": hashlib.sha256(model_source.encode()).hexdigest()},
                   "geo": {"path": str(manifest["geo"]),
                            "sha256": hashlib.sha256(geo_source.encode()).hexdigest()}},
    }
    compiled_ir, primitives, _pairing = compile_mesh_ir(source_ir)
    meshlets = mario_meshlets(triangles, compiled_ir["primitives"], positions)
    joint_owners = _joint_owners(geo_source, [part[0] for part in parts])
    skeleton = parse_mario_skeleton(geo_source)
    vertices = []
    for index in range(len(positions)):
        local, branch = vertex_metadata[index]
        vertices.append({"local": list(local), "joint_ordinal": joint_owners[branch],
                         "branch_ordinal": branch})
    return {
        "model_source": model_source, "geo_source": geo_source,
        "positions": positions, "vertices": vertices, "materials": materials,
        "primitives": [{"material": primitive.material, "indices": list(primitive.vertices)}
                       for primitive in primitives],
        "primitive_objects": primitives,
        "meshlets": meshlets,
        "joints": [{"joint_ordinal": joint.joint_ordinal,
                    "parent_ordinal": joint.parent_ordinal,
                    "translation": list(joint.translation),
                    "node_ordinal": joint.node_ordinal,
                    "branch_ordinal": joint.branch_ordinal}
                   for joint in skeleton],
        "parts": [{"branch_ordinal": index, "joint_ordinal": joint_owners[index],
                   "display_list": part[0]} for index, part in enumerate(parts)],
        "triangles": triangles, "references": references,
    }


def _pack_geometry(geometry: dict[str, object]) -> bytes:
    joints = geometry["joints"]
    parts = geometry["parts"]
    materials = geometry["materials"]
    meshlets = geometry["meshlets"]["meshlets"]
    primitives = geometry["primitives"]
    primitive_refs: list[int] = []
    vertex_refs: list[int] = []
    meshlet_records: list[bytes] = []
    for meshlet in meshlets:
        tier_fields: list[int] = []
        for tier_primitives, tier_vertices in meshlet["tiers"]:
            tier_fields.extend((len(primitive_refs), len(tier_primitives),
                                len(vertex_refs), len(tier_vertices)))
            primitive_refs.extend(tier_primitives)
            vertex_refs.extend(tier_vertices)
        bounds = meshlet["bounds"]
        meshlet_records.append(MESHLET_RECORD_STRUCT.pack(
            int(meshlet["material"]), int(meshlet["source_ordinal"]),
            int(meshlet["opacity"]), 0, *bounds["min"], *bounds["max"], *tier_fields))
    header_size = GEOMETRY_HEADER_STRUCT.size
    joint_offset = header_size
    part_offset = joint_offset + len(joints) * JOINT_RECORD_STRUCT.size
    material_offset = part_offset + len(parts) * PART_RECORD_STRUCT.size
    meshlet_offset = material_offset + len(materials) * MATERIAL_RECORD_STRUCT.size
    primitive_offset = meshlet_offset + len(meshlet_records) * MESHLET_RECORD_STRUCT.size
    primitive_ref_offset = primitive_offset + len(primitives) * PRIMITIVE_RECORD_STRUCT.size
    vertex_ref_offset = primitive_ref_offset + len(primitive_refs) * 2
    output = bytearray(GEOMETRY_HEADER_STRUCT.pack(
        b"GEO1", len(parts), len(joints), len(materials), len(meshlets), len(primitives),
        len(primitive_refs), len(vertex_refs), joint_offset, part_offset, material_offset,
        meshlet_offset, primitive_offset, primitive_ref_offset, vertex_ref_offset))
    for joint in joints:
        output.extend(JOINT_RECORD_STRUCT.pack(
            joint["parent_ordinal"], *joint["translation"],
            joint["node_ordinal"], joint["branch_ordinal"]))
    for part in parts:
        output.extend(PART_RECORD_STRUCT.pack(part["joint_ordinal"], part["branch_ordinal"]))
    for material in materials:
        output.extend(MATERIAL_RECORD_STRUCT.pack(*material["rgb"], 0))
    for record in meshlet_records:
        output.extend(record)
    for primitive in primitives:
        output.extend(PRIMITIVE_RECORD_STRUCT.pack(primitive["material"], *primitive["indices"]))
    output.extend(struct.pack(f">{len(primitive_refs)}H", *primitive_refs))
    output.extend(struct.pack(f">{len(vertex_refs)}H", *vertex_refs))
    return bytes(output)


def compile_mario_actor_bank(root: Path, manifest_path: Path) -> tuple[dict[str, object], bytes]:
    root = root.resolve()
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    inventory = load_animation_inventory(root)
    geometry = _compile_geometry(root, manifest)
    joint_counts = {record.joint_count for record in inventory.records}
    if joint_counts != {20}:
        raise ValueError(f"Mario animation joint counts changed: {sorted(joint_counts)}")

    source_entries = [
        {"path": "include/mario_animation_ids.h",
         "sha256": _canonical_source_hash(root / "include/mario_animation_ids.h")},
        {"path": str(manifest["geo"]),
         "sha256": _canonical_source_hash(root / str(manifest["geo"]))},
        {"path": str(manifest["model"]),
         "sha256": _canonical_source_hash(root / str(manifest["model"]))},
    ] + [{"path": item.path, "sha256": item.sha256} for item in inventory.source_files]
    source_entries.sort(key=lambda item: item["path"])
    source_digest = _source_digest(source_entries)

    payload = bytearray(bytes(HEADER_SIZE))
    records_offset = _align(payload)
    payload.extend(bytes(len(inventory.records) * ANIMATION_RECORD_STRUCT.size))

    index_blobs: dict[bytes, int] = {}
    value_blobs: dict[bytes, int] = {}
    packed_records: list[dict[str, object]] = []
    indices_offset = _align(payload)
    for record in inventory.records:
        blob = struct.pack(f">{len(record.indices)}H", *record.indices)
        if blob not in index_blobs:
            payload.extend(struct.pack(">I", len(record.indices)))
            index_blobs[blob] = len(payload)
            payload.extend(blob)
        packed_records.append({"record": record, "indices_offset": index_blobs[blob],
                               "indices_size": len(blob)})
    indices_size = len(payload) - indices_offset
    values_offset = _align(payload)
    for item in packed_records:
        record = item["record"]
        blob = struct.pack(f">{len(record.values)}h", *record.values)
        if blob not in value_blobs:
            payload.extend(struct.pack(">I", len(record.values)))
            value_blobs[blob] = len(payload)
            payload.extend(blob)
        item["values_offset"] = value_blobs[blob]
        item["values_size"] = len(blob)
    values_size = len(payload) - values_offset
    vertices_offset = _align(payload)
    for vertex in geometry["vertices"]:
        payload.extend(VERTEX_RECORD_STRUCT.pack(
            *vertex["local"], vertex["joint_ordinal"], vertex["branch_ordinal"]))
    vertices_size = len(payload) - vertices_offset
    meshlets_offset = _align(payload)
    geometry_blob = _pack_geometry(geometry)
    payload.extend(geometry_blob)
    meshlets_size = len(geometry_blob)

    animation_documents: list[dict[str, object]] = []
    for item in packed_records:
        record = item["record"]
        record_offset = records_offset + record.animation_id * ANIMATION_RECORD_STRUCT.size
        encoded = ANIMATION_RECORD_STRUCT.pack(
            item["values_offset"], item["indices_offset"], record.frame_count,
            record.joint_count, record.flags, record.y_translation_divisor)
        payload[record_offset:record_offset + len(encoded)] = encoded
        last_samples = [
            record.values[offset + min(record.frame_count - 1, count - 1)]
            for count, offset in zip(record.indices[0::2], record.indices[1::2])
        ]
        animation_documents.append({
            "animation_id": record.animation_id, "enum_name": record.enum_name,
            "symbol": record.symbol, "source_path": record.source_path,
            "source_sha256": record.source_sha256, "values_offset": item["values_offset"],
            "values_size": item["values_size"], "indices_offset": item["indices_offset"],
            "indices_size": item["indices_size"], "frame_count": record.frame_count,
            "joint_count": record.joint_count, "channel_count": len(record.indices) // 2,
            "flags": record.flags, "y_translation_divisor": record.y_translation_divisor,
            "start_frame": record.start_frame, "loop_start": record.loop_start,
            "last_channel_samples": last_samples,
        })

    max_scratch = (len(geometry["vertices"]) * 3 * 2 +
                   len(geometry["vertices"]) + 20 * 12 * 4)
    header = HEADER_STRUCT.pack(
        MAGIC, VERSION, int(manifest["family_id"]), int(manifest["model_id"]), 20,
        len(inventory.records), len(geometry["meshlets"]["meshlets"]),
        len(geometry["primitives"]), len(geometry["vertices"]),
        int(manifest["max_instances"]), FEATURE_MASK, source_digest,
        HEADER_SIZE, ANIMATION_RECORD_STRUCT.size, records_offset,
        indices_offset, indices_size, values_offset, values_size,
        vertices_offset, vertices_size, meshlets_offset, meshlets_size, max_scratch)
    payload[:len(header)] = header

    payload_sha256 = hashlib.sha256(payload).hexdigest()
    full_prebake_bytes = sum(record.frame_count for record in inventory.records) * len(geometry["vertices"]) * 7
    document: dict[str, object] = {
        "schema": "sm64-saturn-actor-bank-v1", "version": VERSION,
        "magic": MAGIC.decode("ascii"), "family_id": int(manifest["family_id"]),
        "model_id": int(manifest["model_id"]), "joint_count": 20,
        "animation_count": len(animation_documents),
        "source_file_count": len(inventory.source_files),
        "meshlet_count": len(geometry["meshlets"]["meshlets"]),
        "primitive_count": len(geometry["primitives"]),
        "vertex_count": len(geometry["vertices"]),
        "max_instances": int(manifest["max_instances"]), "feature_mask": FEATURE_MASK,
        "source_sha256": source_digest.hex(), "max_scratch": max_scratch,
        "geometry_scope": manifest["geometry_scope"],
        "switch_variant_geometry_complete": False,
        "switch_variant_runtime_owner": "Task 10",
        "sources": source_entries, "animations": animation_documents,
        "vertices": geometry["vertices"], "joints": geometry["joints"],
        "parts": geometry["parts"],
        "materials": geometry["materials"], "meshlets": geometry["meshlets"]["meshlets"],
        "primitives": geometry["primitives"],
        "payload_sha256": payload_sha256, "payload_size": len(payload),
        "compact_channel_bytes": indices_size + values_size,
        "full_prebake_vertex_light_bytes": full_prebake_bytes,
        "s64p_dependency": {
            "kind": "ANIMATION_DEPENDENCIES", "stable_id": "mario-animation-bank-v1",
            "generation": 1, "destination_class": "CART", "lifetime": "BOOT",
            "alignment": 4, "max_scratch": max_scratch, "byte_count": len(payload),
            "sha256": payload_sha256,
        },
        "format": {"header_size": HEADER_SIZE, "animation_record_size": ANIMATION_RECORD_STRUCT.size,
                   "records_offset": records_offset, "indices_offset": indices_offset,
                   "indices_size": indices_size, "values_offset": values_offset,
                   "values_size": values_size, "vertices_offset": vertices_offset,
                   "vertices_size": vertices_size, "meshlets_offset": meshlets_offset,
                   "meshlets_size": meshlets_size},
        "legacy_validation": manifest["legacy_validation"],
    }
    validate_actor_bank_document(document, bytes(payload))
    return document, bytes(payload)


def validate_actor_bank_document(document: dict[str, object], payload: bytes) -> None:
    if payload[:4] != MAGIC or document.get("magic") != "S64B" or document.get("version") != VERSION:
        raise ValueError("invalid actor bank identity")
    if hashlib.sha256(payload).hexdigest() != document.get("payload_sha256"):
        raise ValueError("actor bank payload hash mismatch")
    sources = document.get("sources")
    if not isinstance(sources, list):
        raise ValueError("missing actor bank sources")
    paths = [str(item.get("path", "")) for item in sources]
    if paths != sorted(paths):
        raise ValueError("canonical source order is required")
    for source in sources:
        digest = source.get("sha256")
        if not isinstance(digest, str) or len(digest) != 64:
            raise ValueError("missing source hash")
    animations = document.get("animations")
    if not isinstance(animations, list):
        raise ValueError("missing animation records")
    ids = [item.get("animation_id") for item in animations]
    if len(ids) != len(set(ids)):
        raise ValueError("duplicate animation ID")
    if ids != list(range(209)):
        raise ValueError("complete animation ID set is required")
    for item in animations:
        for offset_name, size_name in (("values_offset", "values_size"),
                                       ("indices_offset", "indices_size")):
            offset, size = item.get(offset_name), item.get(size_name)
            if not isinstance(offset, int) or not isinstance(size, int) or offset < HEADER_SIZE or size <= 0 or offset + size > len(payload):
                raise ValueError("animation stream span is outside payload")
        indices_offset = item["indices_offset"]
        values_offset = item["values_offset"]
        if indices_offset < 4 or values_offset < 4:
            raise ValueError("animation stream span is outside payload")
        index_words = struct.unpack_from(">I", payload, indices_offset - 4)[0]
        value_words = struct.unpack_from(">I", payload, values_offset - 4)[0]
        if index_words * 2 != item["indices_size"]:
            raise ValueError("index stream word count mismatch")
        if value_words * 2 != item["values_size"]:
            raise ValueError("value stream word count mismatch")
        indices = struct.unpack_from(f">{index_words}H", payload, indices_offset)
        for count, offset in zip(indices[0::2], indices[1::2]):
            if count == 0 or offset + count > value_words:
                raise ValueError("animation stream span crosses value stream")
    joint_count = int(document.get("joint_count", 0))
    vertices = document.get("vertices")
    if not isinstance(vertices, list) or len(vertices) != document.get("vertex_count"):
        raise ValueError("actor vertex table is incomplete")
    for vertex in vertices:
        joint = vertex.get("joint_ordinal")
        branch = vertex.get("branch_ordinal")
        if not isinstance(joint, int) or not 0 <= joint < joint_count:
            raise ValueError("bad joint ordinal")
        if not isinstance(branch, int) or not 0 <= branch < len(document.get("parts", [])):
            raise ValueError("bad branch ordinal")
    joints = document.get("joints")
    if not isinstance(joints, list) or len(joints) != joint_count:
        raise ValueError("actor joint table is incomplete")
    for ordinal, joint in enumerate(joints):
        if joint.get("joint_ordinal") != ordinal or not -1 <= joint.get("parent_ordinal", -2) < ordinal:
            raise ValueError("bad joint hierarchy ordinal")


def decode_animation_channels(document: dict[str, object], payload: bytes,
                              animation_id: int, frame: int) -> list[int]:
    if not 0 <= animation_id < len(document["animations"]):
        raise ValueError("animation ID is outside bank")
    record = document["animations"][animation_id]
    indices = struct.unpack_from(f">{record['indices_size'] // 2}H", payload,
                                 record["indices_offset"])
    values = struct.unpack_from(f">{record['values_size'] // 2}h", payload,
                                record["values_offset"])
    sample_frame = max(0, frame)
    return [values[offset + min(sample_frame, count - 1)]
            for count, offset in zip(indices[0::2], indices[1::2])]


def verify_legacy_pose_differential(root: Path, document: dict[str, object],
                                    payload: bytes) -> dict[str, object]:
    manifest = document["legacy_validation"]
    model_source = (root / "actors/mario/model.inc.c").read_text(encoding="utf-8")
    geo_source = (root / "actors/mario/geo.inc.c").read_text(encoding="utf-8")
    display_lists, source_vertices = blocks(model_source, "Gfx"), vertex_groups(model_source)
    base_source = (root / manifest["idle_source"]).read_text(encoding="utf-8")
    base_parts = geo_layout_parts(geo_source, animation_rotations(base_source, 0),
                                  animation_translation(base_source, 0))
    base_triangles: list[dict[str, object]] = []
    flatten_parts(display_lists, source_vertices, base_parts, base_triangles)
    references: dict[tuple[int, int, int], list[tuple[int, int]]] = {}
    base_positions = [[0, 0, 0] for _ in document["vertices"]]
    position_index: dict[tuple[int, int, int], int] = {}
    for triangle_index, triangle in enumerate(base_triangles):
        for corner, point in enumerate(triangle["positions"]):
            key = tuple(point)
            if key not in position_index:
                position_index[key] = len(position_index)
                base_positions[position_index[key]] = list(point)
            references.setdefault(key, []).append((triangle_index, corner))

    primitive_objects = [
        type("Primitive", (), {"vertices": tuple(item["indices"])})()
        for item in document["primitives"]
    ]
    pose_mismatches = 0
    light_mismatches = 0
    counts: dict[str, int] = {}
    for label, source_key, animation_key in (
        ("idle", "idle_source", "idle_animation_id"),
        ("walking", "walking_source", "walking_animation_id"),
    ):
        source = (root / manifest[source_key]).read_text(encoding="utf-8")
        record = document["animations"][int(manifest[animation_key])]
        counts[label] = record["frame_count"]
        for frame in range(record["frame_count"]):
            compact = decode_animation_channels(document, payload, record["animation_id"], frame)
            translation = tuple(compact[:3])
            rotations = [tuple(compact[offset:offset + 3])
                         for offset in range(3, len(compact), 3)]
            compact_parts = geo_layout_parts(geo_source, rotations, translation)
            actual = [
                list(matrix_apply(compact_parts[vertex["branch_ordinal"]][1],
                                  tuple(vertex["local"])))
                for vertex in document["vertices"]
            ]
            legacy_parts = geo_layout_parts(geo_source, animation_rotations(source, frame),
                                            animation_translation(source, frame))
            legacy_triangles: list[dict[str, object]] = []
            flatten_parts(display_lists, source_vertices, legacy_parts, legacy_triangles)
            expected: list[list[int]] = []
            for point in base_positions:
                candidates = {
                    tuple(legacy_triangles[triangle]["positions"][corner])
                    for triangle, corner in references[tuple(point)]
                }
                if len(candidates) != 1:
                    raise ValueError("legacy animation split a shared source vertex")
                expected.append(list(candidates.pop()))
            if actual != expected:
                pose_mismatches += sum(a != b for a, b in zip(actual, expected))
            actual_light = mario_vertex_light_intensities(actual, primitive_objects)
            expected_light = mario_vertex_light_intensities(expected, primitive_objects)
            light_mismatches += sum(a != b for a, b in zip(actual_light, expected_light))
    return {"idle_frames": counts["idle"], "walking_frames": counts["walking"],
            "posed_vertex_mismatches": pose_mismatches,
            "light_input_mismatches": light_mismatches,
            "exact": pose_mismatches == 0 and light_mismatches == 0}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    document, payload = compile_mario_actor_bank(args.root, args.manifest)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(payload)
    proof = verify_legacy_pose_differential(args.root.resolve(), document, payload)
    if not proof["exact"]:
        raise ValueError("compact actor bank failed legacy pose differential")
    document["legacy_differential"] = proof
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
