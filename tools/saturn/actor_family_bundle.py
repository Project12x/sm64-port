#!/usr/bin/env python3
"""Canonical deterministic S64F-v3 actor-family bundle boundary."""

from __future__ import annotations

import hashlib
import struct
from dataclasses import dataclass
from typing import Mapping, Sequence

S64F_V3_HEADER = struct.Struct(">4s6H12I32s")
S64F_V3_FAMILY = struct.Struct(">14I2HI")
S64F_V3_VARIANT = struct.Struct(">2H5I32s32s")

S64F_MAGIC = b"S64F"
S64F_VERSION = 3
S64F_MAX_FAMILIES = 64
S64F_MAX_VARIANTS = 128
FAMILY_FLAG_SUPPORTED = 1 << 0
FAMILY_FLAG_GEOMETRY = 1 << 1
FAMILY_FLAGS = FAMILY_FLAG_SUPPORTED | FAMILY_FLAG_GEOMETRY
CAPABILITY_MASK = (1 << 18) - 1
RUNTIME_CAPABILITY_MASK = (1 << 5) - 1
UINT32_MAX = (1 << 32) - 1

_S64B_HEADER = struct.Struct(">4s9HI32sHH10I")
_S64B_ANIMATION = struct.Struct(">IIHHHh")
_GEO_HEADER = struct.Struct(">4s7H7I")
_MESHLET = struct.Struct(">HHBB6h12I")


@dataclass(frozen=True, order=True)
class VariantKey:
    family_ordinal: int
    model_id: int


@dataclass(frozen=True)
class SourceRecord:
    path: str
    sha256: bytes


@dataclass(frozen=True)
class FamilyDocument:
    stable_family_hash: int
    capability_mask: int
    runtime_capability_mask: int
    maximum_live_instances: int
    source_actor_count: int
    flags: int
    name: bytes
    source: bytes
    unsupported: bytes
    metadata_blob: bytes
    model_ids: tuple[int, ...] = ()


@dataclass(frozen=True)
class BundleDocument:
    package_generation: int
    families: Sequence[FamilyDocument]


@dataclass(frozen=True)
class FamilyView:
    stable_family_hash: int
    capability_mask: int
    runtime_capability_mask: int
    maximum_live_instances: int
    source_actor_count: int
    flags: int
    variant_first: int
    variant_count: int


@dataclass(frozen=True)
class VariantView:
    family_ordinal: int
    model_id: int
    bank_offset: int
    bank_size: int
    bank_lane_bytes: int
    bank_maximum_scratch: int
    payload_sha256: bytes
    source_sha256: bytes


@dataclass(frozen=True)
class BundleView:
    payload: bytes
    family_count: int
    variant_count: int
    family_records_offset: int
    variant_records_offset: int
    metadata_offset: int
    metadata_size: int
    bank_payloads_offset: int
    bank_payloads_size: int
    workspace_lane_stride: int
    maximum_scratch: int
    package_generation: int
    content_sha256: bytes
    families: tuple[FamilyView, ...]
    variants: tuple[VariantView, ...]


@dataclass(frozen=True)
class _Bank:
    family_ordinal: int
    model_id: int
    source_sha256: bytes
    lane_bytes: int
    maximum_scratch: int


def _u16(value: int, name: str, *, nonzero: bool = False) -> int:
    if not isinstance(value, int) or value < (1 if nonzero else 0) or value > 0xFFFF:
        raise ValueError(f"invalid {name}")
    return value


def _u32(value: int, name: str, *, nonzero: bool = False) -> int:
    if not isinstance(value, int) or value < (1 if nonzero else 0) or value > UINT32_MAX:
        raise ValueError(f"invalid {name}")
    return value


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


def _blob(value: bytes, name: str) -> bytes:
    if not isinstance(value, bytes):
        raise ValueError(f"{name} must be bytes")
    return value


def source_identity(family_ordinal: int, model_id: int,
                    sources: Sequence[SourceRecord]) -> bytes:
    family_ordinal = _u16(family_ordinal, "family ordinal", nonzero=True)
    model_id = _u16(model_id, "model ID", nonzero=True)
    if not sources:
        raise ValueError("source set must not be empty")
    ordered = sorted(sources, key=lambda item: item.path)
    prior = None
    canonical = bytearray(b"S64B-VARIANT-SOURCES\x00\x01")
    canonical.extend(struct.pack(">HH", family_ordinal, model_id))
    for item in ordered:
        path = item.path
        if (not isinstance(path, str) or not path or path.startswith("/") or
                (len(path) >= 2 and path[1] == ":") or
                "\\" in path or "\x00" in path):
            raise ValueError("source path must be normalized and root-relative")
        parts = path.split("/")
        if any(part in ("", ".", "..") for part in parts):
            raise ValueError("source path contains invalid segment")
        try:
            encoded = path.encode("utf-8")
        except UnicodeEncodeError as exc:
            raise ValueError("source path is not UTF-8") from exc
        if prior == path:
            raise ValueError("duplicate source path")
        prior = path
        if not isinstance(item.sha256, bytes) or len(item.sha256) != 32:
            raise ValueError("source hash must be 32 raw bytes")
        canonical.extend(encoded + b"\x00" + item.sha256)
    return hashlib.sha256(canonical).digest()


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


def _validate_s64b(payload: bytes) -> _Bank:
    if not isinstance(payload, bytes) or len(payload) < 104 or len(payload) > UINT32_MAX:
        raise ValueError("malformed S64B size")
    fields = _S64B_HEADER.unpack_from(payload)
    (magic, version, family_id, model_id, joint_count, animation_count,
     meshlet_count, primitive_count, vertex_count, max_instances, feature_mask,
     source_hash, header_size, record_size, records_offset, indices_offset,
     indices_size, values_offset, values_size, vertices_offset, vertices_size,
     geometry_offset, geometry_size, maximum_scratch) = fields
    if (magic != b"S64B" or version != 1 or header_size != 104 or record_size != 16):
        raise ValueError("malformed S64B header")
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
        if not counts[0][1] or counts[1][1] != counts[0][1] or counts[1][3] != counts[0][3] or \
                source_ordinal != source_cursor or counts[0][4][0] != source_ordinal or \
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
    return _Bank(family_id, model_id, source_hash, lane, maximum_scratch)


def _document(document: BundleDocument) -> tuple[FamilyDocument, ...]:
    if not isinstance(document, BundleDocument):
        raise ValueError("invalid bundle document")
    _u32(document.package_generation, "package generation", nonzero=True)
    families = tuple(document.families)
    if not 1 <= len(families) <= S64F_MAX_FAMILIES:
        raise ValueError("family limit is 1..64")
    prior = 0
    variants = 0
    for ordinal, item in enumerate(families, 1):
        stable = _u32(item.stable_family_hash, "stable family hash", nonzero=True)
        if stable == prior:
            raise ValueError("duplicate family hash")
        if stable < prior:
            raise ValueError("family order must be stable-hash order")
        prior = stable
        _u32(item.capability_mask, "capability mask")
        _u32(item.runtime_capability_mask, "runtime capability mask")
        _u32(item.maximum_live_instances, "maximum live instances")
        _u32(item.source_actor_count, "source actor count")
        _u32(item.flags, "family flags")
        if item.capability_mask & ~CAPABILITY_MASK or item.runtime_capability_mask & ~RUNTIME_CAPABILITY_MASK or item.flags & ~FAMILY_FLAGS:
            raise ValueError("family mask/flags")
        models = tuple(item.model_ids)
        if models != tuple(sorted(models)) or len(set(models)) != len(models):
            raise ValueError("variant model order/duplicate")
        for model in models:
            _u16(model, "model ID", nonzero=True)
        supported = bool(item.flags & FAMILY_FLAG_SUPPORTED)
        if supported != bool(models) or bool(item.flags & FAMILY_FLAG_GEOMETRY) != supported:
            raise ValueError("supported family variant contract")
        if supported and (item.maximum_live_instances == 0 or item.source_actor_count == 0):
            raise ValueError("supported family counts")
        for name in ("name", "source", "unsupported", "metadata_blob"):
            _blob(getattr(item, name), name)
        variants += len(models)
        if variants > S64F_MAX_VARIANTS:
            raise ValueError("variant limit is 1..128")
    if not variants:
        raise ValueError("variant limit is 1..128")
    return families


def pack_bundle(document: BundleDocument,
                bank_payloads: Mapping[VariantKey, bytes]) -> bytes:
    families = _document(document)
    expected_keys = tuple(VariantKey(ordinal, model) for ordinal, item in enumerate(families, 1)
                          for model in item.model_ids)
    if set(bank_payloads) != set(expected_keys) or len(bank_payloads) != len(expected_keys):
        raise ValueError("bank mapping must exactly match variant keys")
    family_offset = S64F_V3_HEADER.size
    variant_offset = _add(family_offset, _mul(len(families), S64F_V3_FAMILY.size, "family table"), "family table")
    metadata_offset = _add(variant_offset, _mul(len(expected_keys), S64F_V3_VARIANT.size, "variant table"), "variant table")
    metadata = bytearray()
    interned: dict[bytes, tuple[int, int]] = {b"": (0, 0)}
    spans: list[tuple[tuple[int, int], ...]] = []
    for item in families:
        item_spans = []
        for value in (item.name, item.source, item.unsupported, item.metadata_blob):
            if value not in interned:
                interned[value] = (len(metadata), len(value)); metadata.extend(value)
            item_spans.append(interned[value])
        spans.append(tuple(item_spans))
    bank_root = _align(_add(metadata_offset, len(metadata), "metadata"), "metadata alignment")
    banks = bytearray()
    variants = []
    bank_ids: dict[int, VariantKey] = {}
    maximum_lane = 0
    for key in expected_keys:
        while len(banks) & 3:
            banks.append(0)
        bank_offset = len(banks)
        raw = bank_payloads[key]
        bank = _validate_s64b(raw)
        if (bank.family_ordinal, bank.model_id) != (key.family_ordinal, key.model_id):
            raise ValueError("S64B family/model mismatch")
        bank_id = int.from_bytes(bank.source_sha256[:4], "big")
        if bank_id == 0:
            raise ValueError("zero bank ID")
        if bank_id in bank_ids and bank_ids[bank_id] != key:
            raise ValueError("bank ID collision")
        bank_ids[bank_id] = key
        variants.append((key, bank_offset, len(raw), bank.lane_bytes,
                         bank.maximum_scratch, hashlib.sha256(raw).digest(), bank.source_sha256))
        banks.extend(raw)
        maximum_lane = max(maximum_lane, bank.lane_bytes)
    while len(banks) & 3:
        banks.append(0)
    total_size = _add(bank_root, len(banks), "bundle total")
    maximum_scratch = _add(3, _mul(2, maximum_lane, "bundle scratch"), "bundle scratch")
    payload = bytearray(total_size)
    variant_cursor = 0
    for ordinal, item in enumerate(families, 1):
        item_variants = tuple(v for v in variants if v[0].family_ordinal == ordinal)
        flat_spans = tuple(value for pair in spans[ordinal - 1] for value in pair)
        S64F_V3_FAMILY.pack_into(payload, family_offset + (ordinal - 1) * S64F_V3_FAMILY.size,
            item.stable_family_hash, item.capability_mask, item.runtime_capability_mask,
            item.maximum_live_instances, item.source_actor_count, item.flags, *flat_spans,
            variant_cursor, len(item_variants), 0)
        variant_cursor += len(item_variants)
    for index, (key, offset, size, lane, scratch, digest, source) in enumerate(variants):
        S64F_V3_VARIANT.pack_into(payload, variant_offset + index * S64F_V3_VARIANT.size,
            key.family_ordinal, key.model_id, 0, offset, size, lane, scratch, digest, source)
    payload[metadata_offset:metadata_offset + len(metadata)] = metadata
    payload[bank_root:bank_root + len(banks)] = banks
    S64F_V3_HEADER.pack_into(payload, 0, S64F_MAGIC, S64F_VERSION, S64F_V3_HEADER.size,
        len(families), len(variants), S64F_V3_FAMILY.size, S64F_V3_VARIANT.size,
        family_offset, variant_offset, metadata_offset, len(metadata), bank_root, len(banks),
        maximum_lane, maximum_scratch, document.package_generation, 0, total_size, 0, bytes(32))
    payload[64:96] = hashlib.sha256(payload).digest()
    sealed = bytes(payload)
    validate_bundle(sealed)
    return sealed


def validate_bundle(payload: bytes) -> BundleView:
    if not isinstance(payload, bytes) or len(payload) < S64F_V3_HEADER.size or len(payload) > UINT32_MAX:
        raise ValueError("invalid bundle size")
    fields = S64F_V3_HEADER.unpack_from(payload)
    (magic, version, header_size, family_count, variant_count, family_size, variant_size,
     family_offset, variant_offset, metadata_offset, metadata_size, bank_root, bank_size,
     lane_stride, maximum_scratch, generation, flags, total_size, reserved, digest) = fields
    if magic != S64F_MAGIC:
        raise ValueError("S64F magic")
    if version != S64F_VERSION:
        raise ValueError("S64F version must be 3")
    if header_size != 96 or family_size != 64 or variant_size != 88:
        raise ValueError("S64F record size")
    if not 1 <= family_count <= 64:
        raise ValueError("family limit")
    if not 1 <= variant_count <= 128:
        raise ValueError("variant limit")
    expected_variant = _add(96, _mul(family_count, 64, "family table"), "family table")
    expected_metadata = _add(expected_variant, _mul(variant_count, 88, "variant table"), "variant table")
    expected_bank = _align(_add(expected_metadata, metadata_size, "metadata span"), "metadata span")
    if family_offset != 96 or variant_offset != expected_variant or metadata_offset != expected_metadata or bank_root != expected_bank:
        raise ValueError("noncanonical table/span offset")
    if total_size != len(payload) or _add(bank_root, bank_size, "bank span") != total_size or total_size & 3:
        raise ValueError("noncanonical total/bank span")
    if not generation or flags or reserved:
        raise ValueError("generation/flags/reserved")
    actual_digest = hashlib.sha256(payload[:64] + bytes(32) + payload[96:]).digest()
    if not any(digest) or digest != actual_digest:
        raise ValueError("content hash mismatch")
    if any(payload[metadata_offset + metadata_size:bank_root]):
        raise ValueError("nonzero metadata padding")
    families = []
    reconstructed = bytearray()
    interned: dict[bytes, tuple[int, int]] = {b"": (0, 0)}
    previous_hash = 0
    variant_cursor = 0
    for index in range(family_count):
        record = S64F_V3_FAMILY.unpack_from(payload, family_offset + index * 64)
        stable, capability, runtime, maximum_live, source_count, family_flags = record[:6]
        span_fields = record[6:14]
        first, count, family_reserved = record[14:]
        if not stable or stable <= previous_hash:
            raise ValueError("family order/duplicate/zero")
        previous_hash = stable
        if capability & ~CAPABILITY_MASK or runtime & ~RUNTIME_CAPABILITY_MASK or family_flags & ~FAMILY_FLAGS or family_reserved:
            raise ValueError("family mask/flags/reserved")
        supported = bool(family_flags & FAMILY_FLAG_SUPPORTED)
        if supported != bool(count) or bool(family_flags & FAMILY_FLAG_GEOMETRY) != supported or \
                (supported and (not maximum_live or not source_count)):
            raise ValueError("family supported/count contract")
        if first != variant_cursor or count > variant_count - first:
            raise ValueError("family variant span")
        variant_cursor += count
        for offset, size in zip(span_fields[0::2], span_fields[1::2]):
            _span(offset, size, metadata_size, "family metadata span")
            value = payload[metadata_offset + offset:metadata_offset + offset + size]
            if value not in interned:
                interned[value] = (len(reconstructed), len(value)); reconstructed.extend(value)
            if (offset, size) != interned[value]:
                raise ValueError("noncanonical metadata overlap/order")
        families.append(FamilyView(stable, capability, runtime, maximum_live,
                                   source_count, family_flags, first, count))
    if variant_cursor != variant_count or reconstructed != payload[metadata_offset:metadata_offset + metadata_size]:
        raise ValueError("family variant/metadata coverage")
    variants = []
    previous_key = VariantKey(0, 0)
    bank_cursor = 0
    bank_ids: dict[int, VariantKey] = {}
    maximum_lane = 0
    for index in range(variant_count):
        record = S64F_V3_VARIANT.unpack_from(payload, variant_offset + index * 88)
        family_ordinal, model_id, variant_flags, offset, size, lane, scratch, payload_hash, source_hash = record
        key = VariantKey(family_ordinal, model_id)
        if not family_ordinal or family_ordinal > family_count or not model_id or key <= previous_key:
            raise ValueError("variant order/duplicate/identity")
        previous_key = key
        family = families[family_ordinal - 1]
        if not family.variant_first <= index < family.variant_first + family.variant_count:
            raise ValueError("variant outside family span")
        if variant_flags or offset & 3 or offset != _align(bank_cursor, "bank padding"):
            raise ValueError("variant flags/noncanonical bank offset")
        if any(payload[bank_root + bank_cursor:bank_root + offset]):
            raise ValueError("nonzero bank padding")
        _span(offset, size, bank_size, "variant bank span")
        raw = payload[bank_root + offset:bank_root + offset + size]
        if not size or hashlib.sha256(raw).digest() != payload_hash:
            raise ValueError("bank payload hash mismatch")
        bank = _validate_s64b(raw)
        if (bank.family_ordinal, bank.model_id) != (family_ordinal, model_id):
            raise ValueError("S64B family/model mismatch")
        if bank.source_sha256 != source_hash:
            raise ValueError("S64B source mismatch")
        if lane != bank.lane_bytes or scratch != bank.maximum_scratch or scratch != 3 + 2 * lane:
            raise ValueError("S64B scratch mismatch")
        bank_id = int.from_bytes(source_hash[:4], "big")
        if not bank_id:
            raise ValueError("zero bank ID")
        if bank_id in bank_ids and bank_ids[bank_id] != key:
            raise ValueError("bank ID collision")
        bank_ids[bank_id] = key
        variants.append(VariantView(family_ordinal, model_id, offset, size, lane,
                                    scratch, payload_hash, source_hash))
        maximum_lane = max(maximum_lane, lane)
        bank_cursor = offset + size
    final_cursor = _align(bank_cursor, "bank payload padding")
    if final_cursor != bank_size or any(payload[bank_root + bank_cursor:bank_root + bank_size]):
        raise ValueError("bank payload coverage/padding")
    if lane_stride != maximum_lane or maximum_scratch != 3 + 2 * lane_stride:
        raise ValueError("bundle workspace mismatch")
    return BundleView(payload, family_count, variant_count, family_offset, variant_offset,
                      metadata_offset, metadata_size, bank_root, bank_size, lane_stride,
                      maximum_scratch, generation, digest, tuple(families), tuple(variants))
