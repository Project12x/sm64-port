#!/usr/bin/env python3
"""Version-owned validation for canonical Saturn S64B actor banks."""

from __future__ import annotations

import struct
from dataclasses import dataclass
from enum import IntEnum


UINT32_MAX = (1 << 32) - 1
S64B_V1_HEADER_SIZE = 104
S64B_V2_HEADER_SIZE = 192

_S64B_HEADER = struct.Struct(">4s9HI32sHH10IH")
_S64B_ANIMATION = struct.Struct(">IIHHHh")
_GEO_HEADER = struct.Struct(">4s7H7I")
_MESHLET = struct.Struct(">HHBB6h12I")
_S64B_V2_EXTENSION = struct.Struct(">4H17I12s")
_S64B_V2_BINDING = struct.Struct(">4H")
_S64B_V2_MATERIAL = struct.Struct(">H4BH")
_S64B_V2_TILE = struct.Struct(">IIHHHBB")


class ActorMaterialRecipeV2(IntEnum):
    FLAT_GOURAUD = 1
    CLUT16_REPLACE = 2
    CLUT16_GOURAUD = 3
    RGB1555_REPLACE = 4
    RGB1555_GOURAUD = 5
    CLUT16_HALF_TRANSPARENT = 6
    RGB1555_HALF_TRANSPARENT = 7


class ActorTargetLayerV2(IntEnum):
    OPAQUE = 0
    CUTOUT = 1
    TRANSLUCENT = 2


class ActorAlphaModeV2(IntEnum):
    OPAQUE = 0
    BINARY_ZERO_TRANSPARENT = 1
    HALF_TRANSPARENT = 2


class ActorTileFormatV2(IntEnum):
    CLUT16 = 1
    RGB1555 = 2


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


def _align8(value: int, name: str = "alignment") -> int:
    return _add(value, 7, name) & ~7


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


def _zero_gap(payload: bytes, start: int, end: int, name: str) -> None:
    if start > end or any(payload[start:end]):
        raise ValueError(f"nonzero or reversed S64B v2 {name} padding")


def _v2_material(recipe: int, layer: int, alpha: int, selector: int,
                 flags: int, reserved: int) -> None:
    if selector:
        raise ValueError("S64B v2 material selector")
    if flags or reserved:
        raise ValueError("S64B v2 material reserved")
    if recipe == ActorMaterialRecipeV2.FLAT_GOURAUD:
        allowed = (layer, alpha) == (ActorTargetLayerV2.OPAQUE, ActorAlphaModeV2.OPAQUE)
    elif recipe in (ActorMaterialRecipeV2.CLUT16_REPLACE,
                    ActorMaterialRecipeV2.CLUT16_GOURAUD,
                    ActorMaterialRecipeV2.RGB1555_REPLACE,
                    ActorMaterialRecipeV2.RGB1555_GOURAUD):
        allowed = (layer, alpha) in (
            (ActorTargetLayerV2.OPAQUE, ActorAlphaModeV2.OPAQUE),
            (ActorTargetLayerV2.CUTOUT, ActorAlphaModeV2.BINARY_ZERO_TRANSPARENT),
        )
    elif recipe in (ActorMaterialRecipeV2.CLUT16_HALF_TRANSPARENT,
                    ActorMaterialRecipeV2.RGB1555_HALF_TRANSPARENT):
        allowed = (layer, alpha) == (
            ActorTargetLayerV2.TRANSLUCENT, ActorAlphaModeV2.HALF_TRANSPARENT)
    else:
        raise ValueError("S64B v2 material recipe")
    if not allowed:
        raise ValueError("S64B v2 material layer/alpha")


def _validate_v2(payload: bytes) -> ActorBankView:
    if not isinstance(payload, bytes) or len(payload) < S64B_V2_HEADER_SIZE or len(payload) > UINT32_MAX:
        raise ValueError("malformed S64B v2 size")
    common = _S64B_HEADER.unpack_from(payload)
    (magic, version, family_id, model_id, joint_count, animation_count,
     meshlet_count, primitive_count, vertex_count, max_instances, feature_mask,
     source_hash, header_size, record_size, records_offset, indices_offset,
     indices_size, values_offset, values_size, vertices_offset, vertices_size,
     geometry_offset, geometry_size, maximum_scratch, header_padding) = common
    extension = _S64B_V2_EXTENSION.unpack_from(payload, S64B_V1_HEADER_SIZE)
    (binding_record_size, material_record_size, tile_record_size, v2_flags,
     binding_offset, binding_size, material_offset, material_size,
     tile_offset, tile_size, texture_offset, texture_size, clut_offset, clut_size,
     texture_resident, clut_resident, draw_count, texture_commands,
     gouraud_tables, total_size, bake_policy_id, extension_reserved) = extension
    if (magic != b"S64B" or version != 2 or header_size != S64B_V2_HEADER_SIZE or
            record_size != _S64B_ANIMATION.size or header_padding):
        raise ValueError("malformed S64B v2 header")
    if (binding_record_size != _S64B_V2_BINDING.size or
            material_record_size != _S64B_V2_MATERIAL.size or
            tile_record_size != _S64B_V2_TILE.size or v2_flags or
            any(extension_reserved)):
        raise ValueError("S64B v2 extension record/reserved")
    if not bake_policy_id:
        raise ValueError("S64B v2 bake policy")

    records_size = _mul(animation_count, _S64B_ANIMATION.size,
                        "S64B v2 animation table")
    records_end = _add(records_offset, records_size, "S64B v2 records")
    indices_end = _add(indices_offset, indices_size, "S64B v2 indices")
    values_end = _add(values_offset, values_size, "S64B v2 values")
    vertices_end = _add(vertices_offset, vertices_size, "S64B v2 vertices")
    geometry_end = _add(geometry_offset, geometry_size, "S64B v2 geometry")
    if (records_offset != S64B_V2_HEADER_SIZE or indices_offset != records_end or
            values_offset != _align(indices_end, "S64B v2 values alignment") or
            vertices_offset != _align(values_end, "S64B v2 vertices alignment") or
            geometry_offset != _align(vertices_end, "S64B v2 geometry alignment") or
            binding_offset != geometry_end):
        raise ValueError("noncanonical S64B v2 common spans")
    for offset, size, name in (
            (records_offset, records_size, "records"),
            (indices_offset, indices_size, "indices"),
            (values_offset, values_size, "values"),
            (vertices_offset, vertices_size, "vertices"),
            (geometry_offset, geometry_size, "geometry")):
        _span(offset, size, binding_offset, f"S64B v2 {name}")
    _span(0, binding_offset, len(payload), "S64B v2 common region")
    _zero_gap(payload, indices_end, values_offset, "indices")
    _zero_gap(payload, values_end, vertices_offset, "values")
    _zero_gap(payload, vertices_end, geometry_offset, "vertices")

    # Reconstruct only the canonical v1 common core and use the established
    # parser as the single authority for pose/GEO1 semantics. GEO1-relative
    # offsets remain untouched; only bank-absolute offsets move by 88 bytes.
    delta = S64B_V2_HEADER_SIZE - S64B_V1_HEADER_SIZE
    absolute_offsets = (records_offset, indices_offset, values_offset,
                        vertices_offset, geometry_offset)
    if any(offset < delta for offset in absolute_offsets):
        raise ValueError("S64B v2 common offset underflow")
    core = bytearray(S64B_V1_HEADER_SIZE)
    core.extend(payload[S64B_V2_HEADER_SIZE:binding_offset])
    core_header = _S64B_HEADER.pack(
        magic, 1, family_id, model_id, joint_count, animation_count,
        meshlet_count, primitive_count, vertex_count, max_instances,
        feature_mask, source_hash, S64B_V1_HEADER_SIZE, record_size,
        records_offset - delta, indices_offset - delta, indices_size,
        values_offset - delta, values_size, vertices_offset - delta,
        vertices_size, geometry_offset - delta, geometry_size,
        maximum_scratch, 0)
    core[:len(core_header)] = core_header
    for index in range(animation_count):
        original = _add(records_offset,
                        _mul(index, _S64B_ANIMATION.size, "S64B v2 pose record"),
                        "S64B v2 pose record")
        values, indices, frames, joints, flags, divisor = _S64B_ANIMATION.unpack_from(
            payload, original)
        if values < delta or indices < delta:
            raise ValueError("S64B v2 pose offset underflow")
        rebased = _S64B_ANIMATION.pack(values - delta, indices - delta, frames,
                                       joints, flags, divisor)
        target = _add(S64B_V1_HEADER_SIZE,
                      _mul(index, _S64B_ANIMATION.size, "S64B v2 core pose record"),
                      "S64B v2 core pose record")
        core[target:target + _S64B_ANIMATION.size] = rebased
    common_view = _validate_v1(bytes(core))

    expected_binding_size = _mul(primitive_count, _S64B_V2_BINDING.size,
                                 "S64B v2 binding table")
    expected_material_size = _mul(common_view.material_count, _S64B_V2_MATERIAL.size,
                                  "S64B v2 material table")
    binding_end = _add(binding_offset, binding_size, "S64B v2 bindings")
    material_end = _add(material_offset, material_size, "S64B v2 materials")
    tile_end = _add(tile_offset, tile_size, "S64B v2 tiles")
    texture_end = _add(texture_offset, texture_size, "S64B v2 texture payload")
    clut_end = _add(clut_offset, clut_size, "S64B v2 CLUT payload")
    if (binding_size != expected_binding_size or material_offset != binding_end or
            material_size != expected_material_size or tile_offset != material_end or
            tile_size % _S64B_V2_TILE.size or
            texture_offset != _align8(tile_end, "S64B v2 texture alignment") or
            texture_offset & 7 or texture_size & 7 or
            clut_offset != _align8(texture_end, "S64B v2 CLUT alignment") or
            clut_offset & 7 or clut_size % 32 or clut_end != total_size or
            total_size != len(payload)):
        raise ValueError("noncanonical S64B v2 resource spans/total size")
    if texture_resident != texture_size or clut_resident != clut_size:
        raise ValueError("S64B v2 resident aggregate")
    if draw_count != primitive_count:
        raise ValueError("S64B v2 draw aggregate")
    _zero_gap(payload, tile_end, texture_offset, "tile directory")
    for offset, size, name in (
            (binding_offset, binding_size, "bindings"),
            (material_offset, material_size, "materials"),
            (tile_offset, tile_size, "tiles"),
            (texture_offset, texture_size, "texture payload"),
            (clut_offset, clut_size, "CLUT payload")):
        _span(offset, size, total_size, f"S64B v2 {name}")

    materials: list[int] = []
    for index in range(common_view.material_count):
        record_offset = _add(material_offset,
            _mul(index, _S64B_V2_MATERIAL.size, "S64B v2 material record"),
            "S64B v2 material record")
        record = _S64B_V2_MATERIAL.unpack_from(
            payload, record_offset)
        recipe, layer, alpha, selector, flags, reserved = record
        _v2_material(recipe, layer, alpha, selector, flags, reserved)
        materials.append(recipe)

    tile_count = tile_size // _S64B_V2_TILE.size
    if tile_count > 0xFFFF:
        raise ValueError("S64B v2 tile count")
    tiles: list[tuple[int, int, int, int, int]] = []
    texture_cursor = 0
    first_cluts: list[int] = []
    seen_cluts: set[int] = set()
    for index in range(tile_count):
        record_offset = _add(tile_offset,
            _mul(index, _S64B_V2_TILE.size, "S64B v2 tile record"),
            "S64B v2 tile record")
        record = _S64B_V2_TILE.unpack_from(
            payload, record_offset)
        relative, payload_size, width, height, clut_id, tile_format, flags = record
        if flags or width < 8 or width > 504 or width % 8 or not 1 <= height <= 255:
            raise ValueError("S64B v2 tile dimensions/flags")
        expected_relative = _align8(texture_cursor, "S64B v2 tile payload")
        if relative != expected_relative:
            raise ValueError("S64B v2 tile payload order")
        gap_start = _add(texture_offset, texture_cursor, "S64B v2 tile payload")
        gap_end = _add(texture_offset, relative, "S64B v2 tile payload")
        _zero_gap(payload, gap_start, gap_end, "tile payload")
        if tile_format == ActorTileFormatV2.CLUT16:
            expected_size = _mul(width, height, "S64B v2 CLUT16 pixels") // 2
            if clut_id == 0xFFFF:
                raise ValueError("S64B v2 CLUT tile ID")
            if clut_id not in seen_cluts:
                if clut_id != len(first_cluts):
                    raise ValueError("S64B v2 CLUT first-use ordinal")
                seen_cluts.add(clut_id)
                first_cluts.append(clut_id)
        elif tile_format == ActorTileFormatV2.RGB1555:
            expected_size = _mul(_mul(width, height, "S64B v2 RGB1555 pixels"),
                                 2, "S64B v2 RGB1555 pixels")
            if clut_id != 0xFFFF:
                raise ValueError("S64B v2 RGB1555 CLUT ID")
        else:
            raise ValueError("S64B v2 tile format")
        if payload_size != expected_size:
            raise ValueError("S64B v2 tile payload equation")
        _span(relative, payload_size, texture_size, "S64B v2 tile payload")
        if tile_format == ActorTileFormatV2.RGB1555:
            pixels_start = _add(texture_offset, relative, "S64B v2 RGB1555 payload")
            pixels_end = _add(pixels_start, payload_size, "S64B v2 RGB1555 payload")
            pixels = payload[pixels_start:pixels_end]
            words = struct.unpack(f">{payload_size // 2}H", pixels)
            if any(word and not word & 0x8000 for word in words):
                raise ValueError("S64B v2 RGB1555 transparency word")
        texture_cursor = _add(relative, payload_size, "S64B v2 tile payload")
        tiles.append((relative, payload_size, width, height, tile_format))
    expected_texture_size = _align8(texture_cursor, "S64B v2 texture padding")
    if texture_size != expected_texture_size:
        raise ValueError("S64B v2 texture payload coverage")
    texture_used_end = _add(texture_offset, texture_cursor,
                            "S64B v2 texture payload")
    _zero_gap(payload, texture_used_end, texture_end, "texture payload")

    clut_count = clut_size // 32
    if first_cluts != list(range(clut_count)):
        raise ValueError("S64B v2 CLUT dense ordinals")
    for index in range(clut_count):
        palette_offset = _add(clut_offset, _mul(index, 32, "S64B v2 CLUT record"),
                              "S64B v2 CLUT record")
        words = struct.unpack_from(">16H", payload, palette_offset)
        if words[0] != 0 or any(word and not word & 0x8000 for word in words[1:]):
            raise ValueError("S64B v2 CLUT transparency words")

    geo = _GEO_HEADER.unpack_from(payload, geometry_offset)
    primitive_relative = geo[12]
    first_tiles: list[int] = []
    seen_tiles: set[int] = set()
    counted_textures = 0
    counted_gouraud = 0
    for index in range(primitive_count):
        binding_record = _add(binding_offset,
            _mul(index, _S64B_V2_BINDING.size, "S64B v2 binding record"),
            "S64B v2 binding record")
        material_id, tile_id, flags, reserved = _S64B_V2_BINDING.unpack_from(
            payload, binding_record)
        primitive_record = _add(
            _add(geometry_offset, primitive_relative, "S64B v2 primitive table"),
            _mul(index, 10, "S64B v2 primitive record"),
            "S64B v2 primitive record")
        primitive_material = struct.unpack_from(">H", payload, primitive_record)[0]
        if material_id != primitive_material or material_id >= len(materials) or flags or reserved:
            raise ValueError("S64B v2 binding material/reserved")
        recipe = materials[material_id]
        if recipe == ActorMaterialRecipeV2.FLAT_GOURAUD:
            if tile_id != 0xFFFF:
                raise ValueError("S64B v2 untextured binding")
        else:
            if tile_id >= tile_count:
                raise ValueError("S64B v2 textured binding tile")
            counted_textures += 1
            if tile_id not in seen_tiles:
                if tile_id != len(first_tiles):
                    raise ValueError("S64B v2 tile first-use ordinal")
                seen_tiles.add(tile_id)
                first_tiles.append(tile_id)
            tile_format = tiles[tile_id][4]
            wants_clut = recipe in (
                ActorMaterialRecipeV2.CLUT16_REPLACE,
                ActorMaterialRecipeV2.CLUT16_GOURAUD,
                ActorMaterialRecipeV2.CLUT16_HALF_TRANSPARENT)
            if (wants_clut and tile_format != ActorTileFormatV2.CLUT16) or \
                    (not wants_clut and tile_format != ActorTileFormatV2.RGB1555):
                raise ValueError("S64B v2 material recipe/tile format")
        if recipe in (ActorMaterialRecipeV2.FLAT_GOURAUD,
                      ActorMaterialRecipeV2.CLUT16_GOURAUD,
                      ActorMaterialRecipeV2.RGB1555_GOURAUD):
            counted_gouraud += 1
    if first_tiles != list(range(tile_count)):
        raise ValueError("S64B v2 tile dense ordinals")
    if texture_commands != counted_textures:
        raise ValueError("S64B v2 texture command aggregate")
    if gouraud_tables != counted_gouraud:
        raise ValueError("S64B v2 Gouraud aggregate")

    return ActorBankView(
        payload=payload, version=2, family_ordinal=family_id, model_id=model_id,
        source_sha256=source_hash, lane_bytes=common_view.lane_bytes,
        maximum_scratch=maximum_scratch, primitive_count=primitive_count,
        material_count=common_view.material_count, hot_end=texture_offset,
        texture_payload_offset=texture_offset, texture_payload_size=texture_size,
        clut_payload_offset=clut_offset, clut_payload_size=clut_size)


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
        return _validate_v2(payload)
    raise ValueError("unsupported S64B version")


def validate_actor_bank_expected(payload: bytes, source_sha256: bytes) -> ActorBankView:
    """Validate a bank and bind it to the caller's exact source identity."""
    if not isinstance(source_sha256, bytes) or len(source_sha256) != 32:
        raise ValueError("S64B expected source SHA-256")
    view = validate_actor_bank(payload)
    if view.source_sha256 != source_sha256:
        raise ValueError("S64B source mismatch")
    return view
