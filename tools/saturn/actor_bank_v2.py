#!/usr/bin/env python3
"""Deterministic pointer-free S64B-v2 promotion and resource packing."""

from __future__ import annotations

import hashlib
import json
import struct
from dataclasses import dataclass
from typing import Mapping, Sequence

from actor_bank_format import (
    ActorMaterialRecipeV2,
    ActorTileFormatV2,
    S64B_V1_HEADER_SIZE,
    S64B_V2_HEADER_SIZE,
    validate_actor_bank,
)
from actor_family_bundle import SourceRecord


UINT32_MAX = (1 << 32) - 1
_PACK_SIZE_LIMIT = UINT32_MAX
_COMMON_HEADER = struct.Struct(">4s9HI32sHH10IH")
_ANIMATION = struct.Struct(">IIHHHh")
_EXTENSION = struct.Struct(">4H17I12s")
_BINDING = struct.Struct(">4H")
_MATERIAL = struct.Struct(">H4BH")
_TILE = struct.Struct(">IIHHHBB")


@dataclass(frozen=True)
class RenderBindingV2:
    material_id: int
    tile_id: int


@dataclass(frozen=True)
class TargetMaterialV2:
    recipe: int
    layer: int
    alpha_mode: int
    selector_kind: int = 0


@dataclass(frozen=True)
class TextureTileV2:
    pixels: bytes
    width: int
    height: int
    format: int
    clut: bytes | None


@dataclass(frozen=True)
class ActorBankResourcesV2:
    bindings: tuple[RenderBindingV2, ...]
    materials: tuple[TargetMaterialV2, ...]
    tiles: tuple[TextureTileV2, ...]
    bake_policy_id: int


def _integer(value: object, minimum: int, maximum: int, name: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not minimum <= value <= maximum:
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


def _align8(value: int, name: str) -> int:
    return _add(value, 7, name) & ~7


def _pack_add(a: int, b: int, name: str) -> int:
    limit = _PACK_SIZE_LIMIT
    if (not isinstance(limit, int) or isinstance(limit, bool) or
            limit < 0 or limit > UINT32_MAX):
        raise ValueError("S64B v2 pack size limit")
    if a < 0 or b < 0 or a > limit or b > limit - a:
        raise ValueError(f"{name} limit")
    return a + b


def _pack_mul(a: int, b: int, name: str) -> int:
    limit = _PACK_SIZE_LIMIT
    if (not isinstance(limit, int) or isinstance(limit, bool) or
            limit < 0 or limit > UINT32_MAX):
        raise ValueError("S64B v2 pack size limit")
    if a < 0 or b < 0 or (a and b > limit // a):
        raise ValueError(f"{name} limit")
    return a * b


def _pack_align8(value: int, name: str) -> int:
    return _pack_add(value, (-value) & 7, name)


def _pack_record_spans(base: int, count: int, size: int,
                       name: str) -> tuple[tuple[int, int], ...]:
    spans = []
    for index in range(count):
        offset = _pack_add(base, _pack_mul(index, size, name), name)
        spans.append((offset, _pack_add(offset, size, name)))
    return tuple(spans)


def _policy_value(value: object, name: str) -> object:
    if value is None or isinstance(value, bool):
        return value
    if isinstance(value, str):
        if (value.startswith(("/", "\\")) or "\\" in value or
                (len(value) >= 2 and value[0].isalpha() and value[1] == ":")):
            raise ValueError(f"host path is forbidden in {name}")
        return value
    if isinstance(value, int) and not isinstance(value, bool):
        _integer(value, -(1 << 63), (1 << 63) - 1, name)
        return value
    if isinstance(value, (list, tuple)):
        return [_policy_value(item, f"{name} item") for item in value]
    if isinstance(value, Mapping):
        result: dict[str, object] = {}
        for key, item in value.items():
            if not isinstance(key, str):
                raise ValueError("policy keys must be strings")
            result[key] = _policy_value(item, f"{name}.{key}")
        return result
    raise ValueError(f"noncanonical {name}")


def source_identity_v2(family_ordinal: int, model_id: int,
                       sources: Sequence[SourceRecord], bake_policy_id: int,
                       policy_document: Mapping[str, object]) -> bytes:
    """Hash canonical root-relative sources and the complete reviewed bake policy."""
    family_ordinal = _integer(family_ordinal, 1, 0xFFFF, "family ordinal")
    model_id = _integer(model_id, 1, 0xFFFF, "model ID")
    bake_policy_id = _integer(bake_policy_id, 1, UINT32_MAX, "bake policy ID")
    if not isinstance(policy_document, Mapping):
        raise ValueError("policy document must be a mapping")
    sources = tuple(sources)
    if not sources:
        raise ValueError("source set must not be empty")
    ordered = sorted(sources, key=lambda item: item.path)
    canonical = bytearray(b"S64B-V2-SOURCE-IDENTITY\x00\x01")
    canonical.extend(struct.pack(">HHII", family_ordinal, model_id,
                                 bake_policy_id, len(ordered)))
    prior_path: str | None = None
    folded_paths: set[str] = set()
    for item in ordered:
        path = item.path
        if (not isinstance(path, str) or not path or path.startswith("/") or
                ":" in path or "\\" in path or "\x00" in path):
            raise ValueError("source path must be normalized and root-relative")
        parts = path.split("/")
        if any(part in ("", ".", "..") for part in parts):
            raise ValueError("source path contains invalid segment")
        try:
            encoded = path.encode("utf-8")
        except UnicodeEncodeError as exc:
            raise ValueError("source path is not UTF-8") from exc
        if len(encoded) > 0xFFFF:
            raise ValueError("source path exceeds uint16")
        folded = path.casefold()
        if path == prior_path:
            raise ValueError("duplicate source path")
        if folded in folded_paths:
            raise ValueError("casefold-colliding source path")
        prior_path = path
        folded_paths.add(folded)
        if not isinstance(item.sha256, bytes) or len(item.sha256) != 32:
            raise ValueError("source hash must be 32 raw bytes")
        canonical.extend(struct.pack(">H", len(encoded)) + encoded + item.sha256)
    normalized_policy = _policy_value(policy_document, "policy document")
    policy = json.dumps(normalized_policy, sort_keys=True, separators=(",", ":"),
                        ensure_ascii=False, allow_nan=False).encode("utf-8")
    canonical.extend(struct.pack(">I", len(policy)) + policy)
    return hashlib.sha256(canonical).digest()


def _validate_tile(tile: TextureTileV2, index: int) -> None:
    if not isinstance(tile, TextureTileV2) or not isinstance(tile.pixels, bytes):
        raise ValueError(f"invalid tile {index}")
    width = _integer(tile.width, 8, 504, f"tile {index} width")
    height = _integer(tile.height, 1, 255, f"tile {index} height")
    if width % 8:
        raise ValueError(f"tile {index} width must be a multiple of eight")
    tile_format = _integer(tile.format, 1, 2, f"tile {index} format")
    pixels = _mul(width, height, f"tile {index} pixels")
    expected = pixels // 2 if tile_format == ActorTileFormatV2.CLUT16 else \
        _mul(pixels, 2, f"tile {index} RGB1555 bytes")
    if len(tile.pixels) != expected:
        raise ValueError(f"tile {index} payload equation")
    if tile_format == ActorTileFormatV2.CLUT16:
        if not isinstance(tile.clut, bytes) or len(tile.clut) != 32:
            raise ValueError(f"tile {index} CLUT")
    elif tile.clut is not None:
        raise ValueError(f"tile {index} RGB1555 CLUT")


def pack_actor_bank_v2(core_v1: bytes, source_sha256: bytes,
                       resources: ActorBankResourcesV2) -> tuple[bytes, dict[str, object]]:
    """Promote a validated v1 core and append canonical target-ready resources."""
    if not isinstance(core_v1, bytes):
        raise ValueError("S64B v2 core must be bytes")
    core_view = validate_actor_bank(core_v1)
    if core_view.version != 1:
        raise ValueError("S64B v2 promotion requires a v1 core")
    common = _COMMON_HEADER.unpack_from(core_v1)
    if not common[6] or not common[7]:
        raise ValueError("S64B v2 meshlet/primitive count")
    if not isinstance(source_sha256, bytes) or len(source_sha256) != 32 or not any(source_sha256):
        raise ValueError("S64B v2 source SHA-256")
    if not isinstance(resources, ActorBankResourcesV2):
        raise ValueError("invalid S64B v2 resources")
    bindings = tuple(resources.bindings)
    materials = tuple(resources.materials)
    source_tiles = tuple(resources.tiles)
    bake_policy_id = _integer(resources.bake_policy_id, 1, UINT32_MAX,
                              "bake policy ID")
    if len(bindings) != core_view.primitive_count:
        raise ValueError("one render binding per primitive is required")
    if len(materials) != core_view.material_count:
        raise ValueError("one target material per GEO1 material is required")
    if len(source_tiles) > 0xFFFF:
        raise ValueError("source tile count exceeds uint16")
    for index, tile in enumerate(source_tiles):
        _validate_tile(tile, index)
    for index, material in enumerate(materials):
        if not isinstance(material, TargetMaterialV2):
            raise ValueError(f"invalid material {index}")
        _integer(material.recipe, 1, 7, f"material {index} recipe")
        _integer(material.layer, 0, 2, f"material {index} layer")
        _integer(material.alpha_mode, 0, 2, f"material {index} alpha")
        _integer(material.selector_kind, 0, 0, f"material {index} selector")

    canonical_tiles: list[TextureTileV2] = []
    tile_ids: dict[TextureTileV2, int] = {}
    referenced_inputs: set[int] = set()
    packed_bindings: list[RenderBindingV2] = []
    for index, binding in enumerate(bindings):
        if not isinstance(binding, RenderBindingV2):
            raise ValueError(f"invalid binding {index}")
        material_id = _integer(binding.material_id, 0, 0xFFFF,
                               f"binding {index} material")
        if material_id >= len(materials):
            raise ValueError(f"binding {index} material is outside input")
        tile_id = _integer(binding.tile_id, 0, 0xFFFF, f"binding {index} tile")
        if tile_id == 0xFFFF:
            packed_bindings.append(RenderBindingV2(material_id, tile_id))
            continue
        if tile_id >= len(source_tiles):
            raise ValueError(f"binding {index} tile is outside input")
        referenced_inputs.add(tile_id)
        tile = source_tiles[tile_id]
        if tile not in tile_ids:
            tile_ids[tile] = len(canonical_tiles)
            canonical_tiles.append(tile)
        packed_bindings.append(RenderBindingV2(material_id, tile_ids[tile]))
    if referenced_inputs != set(range(len(source_tiles))):
        raise ValueError("unused source tile")

    cluts: list[bytes] = []
    clut_ids: dict[bytes, int] = {}
    tile_layouts: list[tuple[int, int]] = []
    texture_size = 0
    for index, tile in enumerate(canonical_tiles):
        relative = _pack_align8(texture_size,
                                "S64B v2 texture payload accumulation")
        texture_size = _pack_add(relative, len(tile.pixels),
                                 "S64B v2 texture payload accumulation")
        if tile.format == ActorTileFormatV2.CLUT16:
            clut = tile.clut
            if clut is None:
                raise ValueError(f"tile {index} CLUT")
            if clut not in clut_ids:
                clut_ids[clut] = len(cluts)
                cluts.append(clut)
            clut_id = clut_ids[clut]
        else:
            clut_id = 0xFFFF
        tile_layouts.append((relative, clut_id))
    texture_size = _pack_align8(texture_size,
                                "S64B v2 texture payload accumulation")
    clut_size = _pack_mul(len(cluts), 32,
                          "S64B v2 CLUT payload accumulation")

    (magic, _version, family_id, model_id, joint_count, animation_count,
     meshlet_count, primitive_count, vertex_count, max_instances, feature_mask,
     _old_source, _header_size, record_size, records_offset, indices_offset,
     indices_size, values_offset, values_size, vertices_offset, vertices_size,
     geometry_offset, geometry_size, maximum_scratch, header_padding) = common
    if header_padding:
        raise ValueError("S64B v1 core header padding")
    delta = S64B_V2_HEADER_SIZE - S64B_V1_HEADER_SIZE
    common_end = _pack_add(len(core_v1), delta, "S64B v2 total size")
    binding_size = _pack_mul(len(packed_bindings), _BINDING.size,
                             "S64B v2 binding table")
    material_size = _pack_mul(len(materials), _MATERIAL.size,
                              "S64B v2 material table")
    tile_size = _pack_mul(len(canonical_tiles), _TILE.size,
                          "S64B v2 tile table")
    binding_offset = common_end
    material_offset = _pack_add(binding_offset, binding_size,
                                "S64B v2 total size")
    tile_offset = _pack_add(material_offset, material_size,
                            "S64B v2 total size")
    tile_end = _pack_add(tile_offset, tile_size, "S64B v2 total size")
    texture_offset = _pack_align8(tile_end, "S64B v2 total size")
    texture_end = _pack_add(texture_offset, texture_size, "S64B v2 total size")
    clut_offset = _pack_align8(texture_end, "S64B v2 total size")
    total_size = _pack_add(clut_offset, clut_size, "S64B v2 total size")

    new_records_offset = _pack_add(records_offset, delta, "S64B v2 records")
    new_indices_offset = _pack_add(indices_offset, delta, "S64B v2 indices")
    new_values_offset = _pack_add(values_offset, delta, "S64B v2 values")
    new_vertices_offset = _pack_add(vertices_offset, delta, "S64B v2 vertices")
    new_geometry_offset = _pack_add(geometry_offset, delta, "S64B v2 geometry")
    rebased_animations: list[tuple[int, int, bytes]] = []
    for index in range(animation_count):
        record_delta = _pack_mul(index, _ANIMATION.size,
                                 "S64B v2 animation record")
        old_record = _pack_add(records_offset, record_delta,
                               "S64B v2 animation record")
        values, indices, frames, joints, flags, divisor = _ANIMATION.unpack_from(
            core_v1, old_record)
        encoded = _ANIMATION.pack(
            _pack_add(values, delta, "S64B v2 pose values"),
            _pack_add(indices, delta, "S64B v2 pose indices"),
            frames, joints, flags, divisor)
        target = _pack_add(new_records_offset, record_delta,
                           "S64B v2 animation record")
        target_end = _pack_add(target, _ANIMATION.size,
                               "S64B v2 animation record")
        rebased_animations.append((target, target_end, encoded))
    binding_spans = _pack_record_spans(
        binding_offset, len(packed_bindings), _BINDING.size,
        "S64B v2 binding record")
    material_spans = _pack_record_spans(
        material_offset, len(materials), _MATERIAL.size,
        "S64B v2 material record")
    tile_spans = _pack_record_spans(
        tile_offset, len(canonical_tiles), _TILE.size,
        "S64B v2 tile record")
    texture_spans = []
    for tile, (relative, _clut_id) in zip(canonical_tiles, tile_layouts):
        start = _pack_add(texture_offset, relative,
                          "S64B v2 texture payload copy")
        texture_spans.append((start, _pack_add(
            start, len(tile.pixels), "S64B v2 texture payload copy")))
    clut_spans = _pack_record_spans(
        clut_offset, len(cluts), 32, "S64B v2 CLUT payload copy")

    # All output span/table/payload arithmetic is complete and bounded before
    # this sole allocation. Copies below use only the preflighted offsets.
    payload = bytearray(total_size)
    payload[S64B_V2_HEADER_SIZE:binding_offset] = core_v1[S64B_V1_HEADER_SIZE:]
    for target, target_end, encoded in rebased_animations:
        payload[target:target_end] = encoded
    header = _COMMON_HEADER.pack(
        magic, 2, family_id, model_id, joint_count, animation_count,
        meshlet_count, primitive_count, vertex_count, max_instances,
        feature_mask, source_sha256, S64B_V2_HEADER_SIZE, record_size,
        new_records_offset, new_indices_offset, indices_size,
        new_values_offset, values_size, new_vertices_offset, vertices_size,
        new_geometry_offset, geometry_size,
        maximum_scratch, 0)
    payload[:len(header)] = header

    for binding, (offset, end) in zip(packed_bindings, binding_spans):
        payload[offset:end] = _BINDING.pack(
            binding.material_id, binding.tile_id, 0, 0)
    for material, (offset, end) in zip(materials, material_spans):
        payload[offset:end] = _MATERIAL.pack(
            material.recipe, material.layer, material.alpha_mode,
            material.selector_kind, 0, 0)
    for tile, layout, (offset, end), (pixels, pixels_end) in zip(
            canonical_tiles, tile_layouts, tile_spans, texture_spans):
        relative, clut_id = layout
        payload[offset:end] = _TILE.pack(
            relative, len(tile.pixels), tile.width, tile.height,
            clut_id, tile.format, 0)
        payload[pixels:pixels_end] = tile.pixels
    for clut, (offset, end) in zip(cluts, clut_spans):
        payload[offset:end] = clut

    texture_commands = sum(binding.tile_id != 0xFFFF for binding in packed_bindings)
    gouraud_recipes = {
        ActorMaterialRecipeV2.FLAT_GOURAUD,
        ActorMaterialRecipeV2.CLUT16_GOURAUD,
        ActorMaterialRecipeV2.RGB1555_GOURAUD,
    }
    gouraud_tables = sum(materials[binding.material_id].recipe in gouraud_recipes
                         for binding in packed_bindings
                         if binding.material_id < len(materials))
    extension = _EXTENSION.pack(
        _BINDING.size, _MATERIAL.size, _TILE.size, 0,
        binding_offset, binding_size, material_offset, material_size,
        tile_offset, tile_size, texture_offset, texture_size,
        clut_offset, clut_size, texture_size, clut_size,
        primitive_count, texture_commands, gouraud_tables, total_size,
        bake_policy_id, bytes(12))
    payload[S64B_V1_HEADER_SIZE:S64B_V2_HEADER_SIZE] = extension
    packed = bytes(payload)
    view = validate_actor_bank(packed)
    if view.source_sha256 != source_sha256:
        raise ValueError("S64B v2 source validation mismatch")
    report: dict[str, object] = {
        "schema": "sm64-saturn-actor-bank-v2",
        "version": 2,
        "source_sha256": source_sha256.hex(),
        "payload_sha256": hashlib.sha256(packed).hexdigest(),
        "payload_size": len(packed),
        "bake_policy_id": bake_policy_id,
        "primitive_count": primitive_count,
        "material_count": len(materials),
        "tile_count": len(canonical_tiles),
        "clut_count": len(cluts),
        "texture_resident_bytes": texture_size,
        "clut_resident_bytes": clut_size,
        "draw_records_per_instance": primitive_count,
        "texture_commands_per_instance": texture_commands,
        "gouraud_tables_per_instance": gouraud_tables,
        "format": {
            "header_size": S64B_V2_HEADER_SIZE,
            "render_binding_record_size": _BINDING.size,
            "target_material_record_size": _MATERIAL.size,
            "texture_tile_record_size": _TILE.size,
            "render_bindings_offset": binding_offset,
            "target_materials_offset": material_offset,
            "texture_tiles_offset": tile_offset,
            "texture_payload_offset": texture_offset,
            "clut_payload_offset": clut_offset,
        },
    }
    return packed, report
