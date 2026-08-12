#!/usr/bin/env python3
"""Version-owned validation for canonical Saturn S64B actor banks."""

from __future__ import annotations

import struct
from dataclasses import dataclass


UINT32_MAX = (1 << 32) - 1

_S64B_HEADER = struct.Struct(">4s9HI32sHH10IH")
_S64B_ANIMATION = struct.Struct(">IIHHHh")
_GEO_HEADER = struct.Struct(">4s7H7I")
_MESHLET = struct.Struct(">HHBB6h12I")


@dataclass(frozen=True)
class ActorBankView:
    payload: bytes
    version: int
    family_ordinal: int
    model_id: int
    source_sha256: bytes
    lane_bytes: int
    maximum_scratch: int
    primitive_count: int
    material_count: int
    hot_end: int
    texture_payload_offset: int
    texture_payload_size: int
    clut_payload_offset: int
    clut_payload_size: int


def _add(a: int, b: int, name: str) -> int:
    if a < 0 or b < 0 or a > UINT32_MAX or b > UINT32_MAX - a:
        raise ValueError(f"{name} overflow")
    return a + b


def _mul(a: int, b: int, name: str) -> int:
    if a < 0 or b < 0 or (a and b > UINT32_MAX // a):
        raise ValueError(f"{name} overflow")
    return a * b


def _align(value: int, name: str = "alignment") -> int:
    return _add(value, 3, name) & ~3


def _span(offset: int, size: int, limit: int, name: str) -> tuple[int, int]:
    end = _add(offset, size, name)
    if end > limit:
        raise ValueError(f"{name} outside payload")
    return offset, end


def _read_u16(payload: bytes, offset: int, name: str) -> int:
    if not isinstance(payload, bytes) or offset < 0 or offset + 2 > len(payload):
        raise ValueError(f"malformed {name}")
    return struct.unpack_from(">H", payload, offset)[0]


def _workspace(vertex_count: int, joint_count: int) -> tuple[int, int]:
    if vertex_count == 0 or joint_count == 0:
        raise ValueError("S64B workspace count")
    cursor = vertex_count * 6 + vertex_count
    cursor = _align(cursor, "S64B workspace")
    cursor = _add(cursor, joint_count * 64, "S64B workspace")
    cursor = _add(cursor, vertex_count * 2, "S64B workspace")
    cursor = _add(cursor, vertex_count, "S64B workspace")
    lane = _align(cursor, "S64B workspace")
    return lane, _add(3, _mul(2, lane, "S64B scratch"), "S64B scratch")


def _validate_v1(payload: bytes) -> ActorBankView:
    if not isinstance(payload, bytes) or len(payload) < 104 or len(payload) > UINT32_MAX:
        raise ValueError("malformed S64B size")
    fields = _S64B_HEADER.unpack_from(payload)
    (magic, version, family_id, model_id, joint_count, animation_count,
     meshlet_count, primitive_count, vertex_count, max_instances, feature_mask,
     source_hash, header_size, record_size, records_offset, indices_offset,
     indices_size, values_offset, values_size, vertices_offset, vertices_size,
     geometry_offset, geometry_size, maximum_scratch, header_padding) = fields
    if (magic != b"S64B" or version != 1 or header_size != 104 or record_size != 16):
        raise ValueError("malformed S64B header")
    if header_padding:
        raise ValueError("S64B header padding")
    if not family_id or not model_id or not joint_count or not animation_count or not vertex_count or not max_instances:
        raise ValueError("malformed S64B identity/count")
    if not any(source_hash):
        raise ValueError("malformed S64B source/features")
    records_size = _mul(animation_count, 16, "S64B animation table")
    for offset, size, name in ((records_offset, records_size, "S64B animation span"),
                               (indices_offset, indices_size, "S64B indices span"),
                               (values_offset, values_size, "S64B values span"),
                               (vertices_offset, vertices_size, "S64B vertices span"),
                               (geometry_offset, geometry_size, "S64B geometry span")):
        _span(offset, size, len(payload), name)
    if (records_offset < 104 or indices_offset < records_offset + records_size or
            values_offset < indices_offset + indices_size or
            vertices_offset < values_offset + values_size or
            geometry_offset < vertices_offset + vertices_size or
            geometry_offset + geometry_size != len(payload) or
            vertices_size != vertex_count * 10):
        raise ValueError("noncanonical S64B spans")
    lane, required_scratch = _workspace(vertex_count, joint_count)
    if maximum_scratch != required_scratch:
        raise ValueError("S64B scratch mismatch")
    if geometry_size < _GEO_HEADER.size:
        raise ValueError("S64B geometry header")
    geo = _GEO_HEADER.unpack_from(payload, geometry_offset)
    (geo_magic, part_count, geo_joint_count, material_count, geo_meshlet_count,
     geo_primitive_count, primitive_ref_count, vertex_ref_count, joint_offset,
     part_offset, material_offset, meshlet_offset, primitive_offset,
     primitive_ref_offset, vertex_ref_offset) = geo
    if (geo_magic != b"GEO1" or not part_count or not material_count or
            geo_joint_count != joint_count or geo_meshlet_count != meshlet_count or
            geo_primitive_count != primitive_count):
        raise ValueError("S64B geometry identity/count")
    expected = _GEO_HEADER.size
    for actual, count, size, name in (
            (joint_offset, geo_joint_count, 12, "joint"),
            (part_offset, part_count, 4, "part"),
            (material_offset, material_count, 4, "material"),
            (meshlet_offset, geo_meshlet_count, _MESHLET.size, "meshlet"),
            (primitive_offset, geo_primitive_count, 10, "primitive"),
            (primitive_ref_offset, primitive_ref_count, 2, "primitive ref"),
            (vertex_ref_offset, vertex_ref_count, 2, "vertex ref")):
        if actual != expected:
            raise ValueError(f"S64B geometry {name} offset")
        expected = _add(expected, _mul(count, size, f"S64B {name}"), f"S64B {name}")
    if expected != geometry_size:
        raise ValueError("S64B geometry size")
    previous_node = -1
    for index in range(geo_joint_count):
        parent, _x, _y, _z, node, branch = struct.unpack_from(">hhhhhH", payload,
            geometry_offset + joint_offset + index * 12)
        if ((index == 0 and parent != -1) or
                (index and not 0 <= parent < index) or node < 0 or
                (index and node <= previous_node) or branch not in (0xFFFF, node)):
            raise ValueError("S64B joint hierarchy")
        previous_node = node
    for index in range(part_count):
        joint, ordinal = struct.unpack_from(">HH", payload,
            geometry_offset + part_offset + index * 4)
        if joint >= joint_count or ordinal != index:
            raise ValueError("S64B part identity")
    for index in range(material_count):
        material = payload[geometry_offset + material_offset + index * 4:
                           geometry_offset + material_offset + index * 4 + 4]
        if any(value > 31 for value in material[:3]) or material[3] != 0:
            raise ValueError("S64B material")
    primitive_cursor = vertex_cursor = source_cursor = 0
    meshlets = []
    for index in range(meshlet_count):
        record = _MESHLET.unpack_from(payload, geometry_offset + meshlet_offset + index * _MESHLET.size)
        material, source_ordinal, translucency, reserved = record[:4]
        bounds = record[4:10]
        tiers = record[10:]
        if material >= material_count or source_ordinal >= primitive_count or translucency > 1 or reserved:
            raise ValueError("S64B meshlet identity")
        if any(bounds[axis] > bounds[axis + 3] for axis in range(3)):
            raise ValueError("S64B meshlet bounds")
        for tier in range(3):
            po, pc, vo, vc = tiers[tier * 4:tier * 4 + 4]
            if po != primitive_cursor or vo != vertex_cursor or pc > 32 or \
                    pc > primitive_ref_count - primitive_cursor or vc > vertex_ref_count - vertex_cursor:
                raise ValueError("S64B meshlet tier span")
            primitive_cursor += pc; vertex_cursor += vc
        meshlets.append((material, source_ordinal, tiers))
    if primitive_cursor != primitive_ref_count or vertex_cursor != vertex_ref_count:
        raise ValueError("S64B meshlet coverage")
    primitives = []
    for index in range(primitive_count):
        record = struct.unpack_from(">5H", payload, geometry_offset + primitive_offset + index * 10)
        material, a, b, c, d = record
        if material >= material_count or any(v >= vertex_count for v in (a, b, c, d)) or \
                a == b or a == c or b == c or (d != c and d in (a, b)):
            raise ValueError("S64B primitive")
        primitives.append(record)
    primitive_refs = struct.unpack_from(f">{primitive_ref_count}H", payload,
                                        geometry_offset + primitive_ref_offset)
    vertex_refs = struct.unpack_from(f">{vertex_ref_count}H", payload,
                                     geometry_offset + vertex_ref_offset)
    if any(v >= primitive_count for v in primitive_refs) or any(v >= vertex_count for v in vertex_refs):
        raise ValueError("S64B geometry reference")
    for material, source_ordinal, tiers in meshlets:
        counts = []
        for tier in range(3):
            po, pc, vo, vc = tiers[tier * 4:tier * 4 + 4]
            refs = primitive_refs[po:po + pc]
            emitted = []
            for primitive_index in refs:
                if primitives[primitive_index][0] != material:
                    raise ValueError("S64B meshlet material")
                for vertex in primitives[primitive_index][1:]:
                    if vertex not in emitted:
                        emitted.append(vertex)
            if tuple(emitted) != vertex_refs[vo:vo + vc]:
                raise ValueError("S64B meshlet vertex closure")
            counts.append((po, pc, vo, vc, refs))
        expected_tier0 = tuple(range(source_ordinal,
                                     source_ordinal + counts[0][1]))
        if not counts[0][1] or counts[1][1] != counts[0][1] or counts[1][3] != counts[0][3] or \
                source_ordinal != source_cursor or counts[0][4] != expected_tier0 or \
                counts[0][4] != counts[1][4]:
            raise ValueError("S64B meshlet source order")
        expected_tier2 = tuple(v for v in counts[0][4] if v % 8 == 1)
        if counts[2][4] != expected_tier2:
            raise ValueError("S64B meshlet tier2")
        source_cursor += counts[0][1]
    if source_cursor != primitive_count:
        raise ValueError("S64B primitive coverage")
    for index in range(animation_count):
        values, indices, frames, joints, flags, _divisor = _S64B_ANIMATION.unpack_from(
            payload, records_offset + index * 16)
        if not frames or joints != joint_count or indices < indices_offset + 4 or \
                values < values_offset + 4 or indices & 1 or values & 1:
            raise ValueError("S64B pose record")
        _span(indices, 0, indices_offset + indices_size, "S64B pose indices")
        _span(values, 0, values_offset + values_size, "S64B pose values")
        index_words = struct.unpack_from(">I", payload, indices - 4)[0]
        value_words = struct.unpack_from(">I", payload, values - 4)[0]
        if index_words != (joint_count + 1) * 6:
            raise ValueError("S64B pose channel count")
        _span(indices, index_words * 2, indices_offset + indices_size, "S64B pose indices")
        _span(values, value_words * 2, values_offset + values_size, "S64B pose values")
        channel = struct.unpack_from(f">{index_words}H", payload, indices)
        if any(count == 0 or offset > value_words or count > value_words - offset
               for count, offset in zip(channel[0::2], channel[1::2])):
            raise ValueError("S64B pose channel span")
    for index in range(vertex_count):
        joint, branch = struct.unpack_from(">HH", payload, vertices_offset + index * 10 + 6)
        owner = struct.unpack_from(">H", payload, geometry_offset + part_offset + branch * 4)[0] \
            if branch < part_count else 0xFFFF
        if joint >= joint_count or branch >= part_count or owner != joint:
            raise ValueError("S64B vertex owner")
    return ActorBankView(
        payload=payload, version=1, family_ordinal=family_id, model_id=model_id,
        source_sha256=source_hash, lane_bytes=lane, maximum_scratch=maximum_scratch,
        primitive_count=primitive_count, material_count=material_count, hot_end=len(payload),
        texture_payload_offset=0, texture_payload_size=0,
        clut_payload_offset=0, clut_payload_size=0)


def validate_actor_bank(payload: bytes) -> ActorBankView:
    """Validate a canonical S64B bank through its version-owned contract."""
    if not isinstance(payload, bytes) or len(payload) < 104 or len(payload) > UINT32_MAX:
        raise ValueError("malformed S64B size")
    if payload[:4] != b"S64B":
        raise ValueError("malformed S64B header")
    version = _read_u16(payload, 4, "S64B version")
    if version == 1:
        return _validate_v1(payload)
    if version == 2:
        raise ValueError("S64B v2 contract is not implemented")
    raise ValueError("unsupported S64B version")


def validate_actor_bank_expected(payload: bytes, source_sha256: bytes) -> ActorBankView:
    """Validate a bank and bind it to the caller's exact source identity."""
    if not isinstance(source_sha256, bytes) or len(source_sha256) != 32:
        raise ValueError("S64B expected source SHA-256")
    view = validate_actor_bank(payload)
    if view.source_sha256 != source_sha256:
        raise ValueError("S64B source mismatch")
    return view
