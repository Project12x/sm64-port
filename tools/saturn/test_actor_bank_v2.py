#!/usr/bin/env python3
"""Canonical S64B-v2 host writer/parser contract tests."""

from __future__ import annotations

import hashlib
import json
import struct
import sys
import unittest
from dataclasses import replace
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))

import actor_bank_v2 as actor_bank_v2_module  # noqa: E402
from actor_bank_format import validate_actor_bank  # noqa: E402
from actor_bank_v2 import (  # noqa: E402
    ActorBankResourcesV2,
    RenderBindingV2,
    TargetMaterialV2,
    TextureTileV2,
    pack_actor_bank_v2,
    source_identity_v2,
)
from actor_family_bundle import SourceRecord  # noqa: E402
from test_actor_family_bundle import tiny_bank  # noqa: E402


V2_EXTENSION = struct.Struct(">4H17I12s")
V2_BINDING = struct.Struct(">4H")
V2_MATERIAL = struct.Struct(">H4B H")
V2_TILE = struct.Struct(">IIHHHBB")


def palette(seed: int = 1) -> bytes:
    return struct.pack(">16H", 0, *(0x8000 | (seed + value) for value in range(15)))


def untextured_resources() -> ActorBankResourcesV2:
    return ActorBankResourcesV2(
        bindings=tuple(RenderBindingV2(0, 0xFFFF) for _ in range(3)),
        materials=(TargetMaterialV2(1, 0, 0),),
        tiles=(),
        bake_policy_id=0x12345678,
    )


def textured_resources() -> ActorBankResourcesV2:
    shared = palette()
    first = TextureTileV2(b"\x01\x23\x45\x67\x89\xab\xcd\xef", 8, 2, 1, shared)
    duplicate = replace(first)
    second = TextureTileV2(b"\x10\x32\x54\x76\x98\xba\xdc\xfe", 8, 2, 1, shared)
    return ActorBankResourcesV2(
        bindings=(RenderBindingV2(0, 1), RenderBindingV2(0, 0), RenderBindingV2(0, 2)),
        materials=(TargetMaterialV2(2, 1, 1),),
        tiles=(first, duplicate, second),
        bake_policy_id=7,
    )


def zero_draw_core(source_hash: bytes) -> bytes:
    """Build a hand-derived v1 bank whose required draw counts are zero."""
    header = struct.Struct(">4s9HI32sHH10IH")
    animation = struct.Struct(">IIHHHh")
    geometry_header = struct.Struct(">4s7H7I")
    payload = bytearray(104)
    records_offset = len(payload)
    payload.extend(bytes(animation.size))
    indices_offset = len(payload)
    payload.extend(struct.pack(">I12H", 12, 1, 0, 1, 0, 1, 0,
                               1, 0, 1, 0, 1, 0))
    indices_size = len(payload) - indices_offset
    values_offset = len(payload)
    payload.extend(struct.pack(">Ih", 1, 0))
    values_size = len(payload) - values_offset
    while len(payload) % 4:
        payload.append(0)
    vertices_offset = len(payload)
    payload.extend(struct.pack(">hhhHH", 0, 0, 0, 0, 0) * 3)
    vertices_size = len(payload) - vertices_offset
    while len(payload) % 4:
        payload.append(0)
    geometry_offset = len(payload)
    joint_offset = geometry_header.size
    part_offset = joint_offset + 12
    material_offset = part_offset + 4
    terminal = material_offset + 4
    payload.extend(geometry_header.pack(
        b"GEO1", 1, 1, 1, 0, 0, 0, 0, joint_offset, part_offset,
        material_offset, terminal, terminal, terminal, terminal))
    payload.extend(struct.pack(">hhhhhH", -1, 0, 0, 0, 0, 0))
    payload.extend(struct.pack(">HH", 0, 0))
    payload.extend(bytes(4))
    geometry_size = len(payload) - geometry_offset
    payload[records_offset:records_offset + animation.size] = animation.pack(
        values_offset + 4, indices_offset + 4, 1, 1, 0, 1)
    payload[:header.size] = header.pack(
        b"S64B", 1, 1, 7, 1, 1, 0, 0, 3, 1, 0x1F, source_hash,
        104, 16, records_offset, indices_offset, indices_size,
        values_offset, values_size, vertices_offset, vertices_size,
        geometry_offset, geometry_size, 203, 0)
    return bytes(payload)


def unchecked_zero_draw_v2(core: bytes, source_hash: bytes) -> bytes:
    """Promote the zero-draw core mechanically, bypassing the production packer."""
    header = struct.Struct(">4s9HI32sHH10IH")
    animation = struct.Struct(">IIHHHh")
    fields = list(header.unpack_from(core))
    delta = 88
    for index in (14, 15, 17, 19, 21):
        fields[index] += delta
    fields[1] = 2
    fields[11] = source_hash
    fields[12] = 192
    payload = bytearray(192)
    payload.extend(core[104:])
    payload[:header.size] = header.pack(*fields)
    values, indices, frames, joints, flags, divisor = animation.unpack_from(payload, 192)
    payload[192:208] = animation.pack(values + delta, indices + delta,
                                      frames, joints, flags, divisor)
    binding_offset = len(payload)
    material_offset = binding_offset
    payload.extend(V2_MATERIAL.pack(1, 0, 0, 0, 0, 0))
    tile_offset = len(payload)
    texture_offset = (tile_offset + 7) & ~7
    payload.extend(bytes(texture_offset - len(payload)))
    extension = V2_EXTENSION.pack(
        8, 8, 16, 0, binding_offset, 0, material_offset, 8,
        tile_offset, 0, texture_offset, 0, texture_offset, 0,
        0, 0, 0, 0, 0, len(payload), 1, bytes(12))
    payload[104:192] = extension
    return bytes(payload)


class ActorBankV2Test(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.core_source = hashlib.sha256(b"v1 core source").digest()
        cls.v2_source = hashlib.sha256(b"v2 source and policy").digest()
        cls.core = tiny_bank(1, 7, cls.core_source)
        cls.untextured, cls.untextured_report = pack_actor_bank_v2(
            cls.core, cls.v2_source, untextured_resources())
        cls.textured, cls.textured_report = pack_actor_bank_v2(
            cls.core, cls.v2_source, textured_resources())

    def test_source_identity_is_domain_separated_canonical_and_host_independent(self) -> None:
        """Catches source-order, policy-order, or checkout-root leakage into identity."""
        sources = (
            SourceRecord("actors/cannon/model.inc.c", bytes(range(32))),
            SourceRecord("actors/cannon/geo.inc.c", bytes(reversed(range(32)))),
        )
        policy = {"tile": [16, 32], "quantizer": "clut16", "transparent": 0}
        canonical_json = b'{"quantizer":"clut16","tile":[16,32],"transparent":0}'
        canonical = bytearray(b"S64B-V2-SOURCE-IDENTITY\x00\x01")
        canonical.extend(struct.pack(">HHII", 29, 0x80, 5, 2))
        for item in reversed(sources):
            encoded = item.path.encode()
            canonical.extend(struct.pack(">H", len(encoded)) + encoded + item.sha256)
        canonical.extend(struct.pack(">I", len(canonical_json)) + canonical_json)
        expected = hashlib.sha256(canonical).digest()
        self.assertEqual(source_identity_v2(29, 0x80, sources, 5, policy), expected)
        self.assertEqual(source_identity_v2(
            29, 0x80, tuple(reversed(sources)), 5,
            {"transparent": 0, "quantizer": "clut16", "tile": [16, 32]}), expected)
        self.assertNotEqual(source_identity_v2(29, 0x80, sources, 6, policy), expected)

    def test_source_identity_rejects_noncanonical_or_ambiguous_inputs(self) -> None:
        """Catches absolute/colliding paths and non-portable policy values."""
        digest = bytes(range(32))
        for bad in ("", "/absolute", "C:/absolute", "actors/C:/absolute",
                    "a\\b", "a/./b", "a/../b", "a//b", "a\x00b"):
            with self.subTest(path=bad), self.assertRaises(ValueError):
                source_identity_v2(1, 1, (SourceRecord(bad, digest),), 1, {})
        with self.assertRaisesRegex(ValueError, "casefold"):
            source_identity_v2(1, 1, (
                SourceRecord("actors/A.c", digest), SourceRecord("actors/B.c", digest),
                SourceRecord("actors/a.c", digest)), 1, {})
        for invalid_policy in ({"float": 1.5}, {"bytes": b"host"}, {1: "key"},
                               {"checkout": "D:/Code/sm64"},
                               {"checkout": "D:\\Code\\sm64"}):
            with self.subTest(policy=invalid_policy), self.assertRaises(ValueError):
                source_identity_v2(1, 1, (SourceRecord("actors/a.c", digest),), 1,
                                   invalid_policy)
        with self.assertRaises(ValueError):
            source_identity_v2(0, 1, (SourceRecord("actors/a.c", digest),), 1, {})
        with self.assertRaises(ValueError):
            source_identity_v2(1, 1, (SourceRecord("actors/a.c", digest),), 0, {})

    def test_untextured_v2_has_exact_pointer_free_192_byte_layout(self) -> None:
        """Catches wrong header size, record sizes, span order, or v1-core rebasing."""
        self.assertEqual((V2_EXTENSION.size, V2_BINDING.size,
                          V2_MATERIAL.size, V2_TILE.size), (88, 8, 8, 16))
        view = validate_actor_bank(self.untextured)
        self.assertEqual((view.version, view.family_ordinal, view.model_id), (2, 1, 7))
        self.assertEqual(view.source_sha256, self.v2_source)
        self.assertEqual(self.untextured[4:6], b"\x00\x02")
        self.assertEqual(struct.unpack_from(">H", self.untextured, 58)[0], 192)
        self.assertEqual(struct.unpack_from(">I", self.untextured, 62)[0], 192)
        old_offsets = struct.unpack_from(">9I", self.core, 62)
        new_offsets = struct.unpack_from(">9I", self.untextured, 62)
        for index in (0, 1, 3, 5, 7):
            self.assertEqual(new_offsets[index], old_offsets[index] + 88)
        for index in (2, 4, 6, 8):
            self.assertEqual(new_offsets[index], old_offsets[index])
        old_values, old_indices = struct.unpack_from(">II", self.core, old_offsets[0])
        new_values, new_indices = struct.unpack_from(">II", self.untextured, new_offsets[0])
        self.assertEqual((new_values, new_indices), (old_values + 88, old_indices + 88))
        old_geo = old_offsets[7]
        new_geo = new_offsets[7]
        self.assertEqual(self.untextured[new_geo:new_geo + old_offsets[8]],
                         self.core[old_geo:old_geo + old_offsets[8]])
        ext = V2_EXTENSION.unpack_from(self.untextured, 104)
        self.assertEqual(ext[:4], (8, 8, 16, 0))
        binding_offset = len(self.core) + 88
        self.assertEqual(ext[4:10], (binding_offset, 24, binding_offset + 24, 8,
                                     binding_offset + 32, 0))
        self.assertEqual(ext[10] % 8, 0)
        self.assertEqual(ext[11], 0)
        self.assertEqual(ext[12], ext[10])
        self.assertEqual(ext[13:20], (0, 0, 0, 3, 0, 3, len(self.untextured)))
        self.assertEqual(ext[20], 0x12345678)
        self.assertEqual(ext[21], bytes(12))
        self.assertEqual(view.hot_end, ext[10])
        self.assertEqual(json.dumps(self.untextured_report, sort_keys=True).find("D:\\"), -1)

    def test_clut16_fixture_deduplicates_tiles_and_palettes_by_first_use(self) -> None:
        """Catches input-order ordinals, missed exact dedup, and palette duplication."""
        view = validate_actor_bank(self.textured)
        ext = V2_EXTENSION.unpack_from(self.textured, 104)
        binding_offset, material_offset, tile_offset = ext[4], ext[6], ext[8]
        self.assertEqual(ext[9], 32)
        self.assertEqual(ext[11], 16)
        self.assertEqual(ext[13], 32)
        self.assertEqual(ext[14:19], (16, 32, 3, 3, 0))
        self.assertEqual(
            [V2_BINDING.unpack_from(self.textured, binding_offset + index * 8) for index in range(3)],
            [(0, 0, 0, 0), (0, 0, 0, 0), (0, 1, 0, 0)])
        self.assertEqual(V2_MATERIAL.unpack_from(self.textured, material_offset),
                         (2, 1, 1, 0, 0, 0))
        self.assertEqual(V2_TILE.unpack_from(self.textured, tile_offset),
                         (0, 8, 8, 2, 0, 1, 0))
        self.assertEqual(V2_TILE.unpack_from(self.textured, tile_offset + 16),
                         (8, 8, 8, 2, 0, 1, 0))
        self.assertEqual(self.textured[ext[10]:ext[10] + 16],
                         b"\x01\x23\x45\x67\x89\xab\xcd\xef"
                         b"\x10\x32\x54\x76\x98\xba\xdc\xfe")
        self.assertEqual(self.textured[ext[12]:ext[12] + 32], palette())
        self.assertEqual((view.texture_payload_size, view.clut_payload_size), (16, 32))

    def test_extension_fields_and_reserved_bytes_are_structurally_owned(self) -> None:
        """Catches an unvalidated field or padding byte in offsets 104..191."""
        valid = self.textured
        mutations = {
            "binding record size": (104, b"\x00\x07"),
            "material record size": (106, b"\x00\x07"),
            "tile record size": (108, b"\x00\x0f"),
            "flags": (110, b"\x00\x01"),
            "binding offset": (112, b"\xff\xff\xff\xfc"),
            "binding size": (116, b"\xff\xff\xff\xff"),
            "material offset": (120, b"\xff\xff\xff\xfc"),
            "material size": (124, b"\xff\xff\xff\xff"),
            "tile offset": (128, b"\xff\xff\xff\xfc"),
            "tile size": (132, b"\xff\xff\xff\xff"),
            "texture offset": (136, b"\xff\xff\xff\xf8"),
            "texture size": (140, b"\xff\xff\xff\xff"),
            "clut offset": (144, b"\xff\xff\xff\xf8"),
            "clut size": (148, b"\xff\xff\xff\xff"),
            "texture resident": (152, b"\x00\x00\x00\x00"),
            "clut resident": (156, b"\x00\x00\x00\x00"),
            "draw aggregate": (160, b"\x00\x00\x00\x00"),
            "texture aggregate": (164, b"\x00\x00\x00\x00"),
            "gouraud aggregate": (168, b"\x00\x00\x00\x01"),
            "total size": (172, b"\xff\xff\xff\xff"),
            "bake policy": (176, b"\x00\x00\x00\x00"),
        }
        for name, (offset, replacement) in mutations.items():
            corrupt = bytearray(valid)
            corrupt[offset:offset + len(replacement)] = replacement
            with self.subTest(name=name), self.assertRaises(ValueError):
                validate_actor_bank(bytes(corrupt))
        for offset in range(180, 192):
            corrupt = bytearray(valid)
            corrupt[offset] = 1
            with self.subTest(reserved_offset=offset), self.assertRaises(ValueError):
                validate_actor_bank(bytes(corrupt))

    def test_validator_rejects_uint32_span_overflow_before_access(self) -> None:
        """Catches unchecked offset-plus-size arithmetic in v2 resource spans."""
        corrupt = bytearray(self.textured)
        corrupt[116:120] = b"\xff\xff\xff\xff"
        with self.assertRaisesRegex(ValueError, "overflow"):
            validate_actor_bank(bytes(corrupt))

    def test_validator_rejects_noncanonical_spans_padding_ordinals_and_trailing_bytes(self) -> None:
        """Catches gaps, aliases, reordered first use, nonzero padding, and suffixes."""
        valid = self.textured
        ext = V2_EXTENSION.unpack_from(valid, 104)
        binding_offset, tile_offset, texture_offset = ext[4], ext[8], ext[10]
        cases: list[tuple[str, int, bytes, str]] = [
            ("binding flags", binding_offset + 4, b"\x00\x01", "binding"),
            ("binding reserved", binding_offset + 6, b"\x00\x01", "binding"),
            ("first-use tile ordinal", binding_offset + 2, b"\x00\x01", "ordinal"),
            ("tile payload gap", tile_offset + 16, b"\x00\x00\x00\x10", "payload"),
            ("tile flags", tile_offset + 15, b"\x01", "tile"),
        ]
        for name, offset, replacement, message in cases:
            corrupt = bytearray(valid)
            corrupt[offset:offset + len(replacement)] = replacement
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, message):
                validate_actor_bank(bytes(corrupt))
        padded_resources = ActorBankResourcesV2(
            bindings=tuple(RenderBindingV2(0, 0) for _ in range(3)),
            materials=(TargetMaterialV2(2, 1, 1),),
            tiles=(TextureTileV2(b"\x12\x34\x56\x78", 8, 1, 1, palette()),),
            bake_policy_id=1,
        )
        padded, _ = pack_actor_bank_v2(self.core, self.v2_source, padded_resources)
        padded_ext = V2_EXTENSION.unpack_from(padded, 104)
        corrupt = bytearray(padded)
        corrupt[padded_ext[10] + 4] = 1
        with self.assertRaisesRegex(ValueError, "padding"):
            validate_actor_bank(bytes(corrupt))
        with self.assertRaisesRegex(ValueError, "total size|trailing"):
            validate_actor_bank(valid + b"\x00")
        self.assertLess(texture_offset, len(valid))

    def test_validator_checks_material_recipe_tile_format_and_transparency_words(self) -> None:
        """Catches recipe/alpha mismatches and invalid transparent target encodings."""
        valid = self.textured
        ext = V2_EXTENSION.unpack_from(valid, 104)
        binding_offset, material_offset, tile_offset = ext[4], ext[6], ext[8]
        texture_offset, clut_offset = ext[10], ext[12]
        cases = [
            ("material identity", binding_offset, b"\x00\x01", "material"),
            ("recipe", material_offset, b"\x00\x04", "format|recipe"),
            ("layer alpha", material_offset + 2, b"\x00\x01", "layer|alpha"),
            ("selector", material_offset + 4, b"\x01", "selector"),
            ("material flags", material_offset + 5, b"\x01", "material"),
            ("tile format", tile_offset + 14, b"\x02", "format|CLUT"),
            ("CLUT transparent zero", clut_offset, b"\x80\x00", "CLUT"),
            ("CLUT opaque marker", clut_offset + 2, b"\x00\x01", "CLUT"),
        ]
        for name, offset, replacement, message in cases:
            corrupt = bytearray(valid)
            corrupt[offset:offset + len(replacement)] = replacement
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, message):
                validate_actor_bank(bytes(corrupt))
        rgb = ActorBankResourcesV2(
            bindings=tuple(RenderBindingV2(0, 0) for _ in range(3)),
            materials=(TargetMaterialV2(4, 0, 0),),
            tiles=(TextureTileV2(struct.pack(">8H", 0, *([0x8001] * 7)), 8, 1, 2, None),),
            bake_policy_id=2,
        )
        rgb_payload, _ = pack_actor_bank_v2(self.core, self.v2_source, rgb)
        rgb_ext = V2_EXTENSION.unpack_from(rgb_payload, 104)
        corrupt = bytearray(rgb_payload)
        corrupt[rgb_ext[10] + 2:rgb_ext[10] + 4] = b"\x00\x01"
        with self.assertRaisesRegex(ValueError, "RGB1555"):
            validate_actor_bank(bytes(corrupt))

    def test_validator_accepts_distinct_nonoverlapping_tiles_with_identical_bytes(self) -> None:
        """Catches quadratic target-side content dedup rejection of legal distinct records."""
        ext = V2_EXTENSION.unpack_from(self.textured, 104)
        corrupt = bytearray(self.textured)
        corrupt[ext[10] + 8:ext[10] + 16] = corrupt[ext[10]:ext[10] + 8]
        self.assertEqual(validate_actor_bank(bytes(corrupt)).version, 2)

    def test_packer_rejects_invalid_counts_ranges_payloads_and_unused_tiles(self) -> None:
        """Catches host integer truncation, malformed resources, and nonsemantic payloads."""
        base = untextured_resources()
        invalid = [
            replace(base, bindings=base.bindings[:2]),
            replace(base, materials=()),
            replace(base, bake_policy_id=0),
            replace(base, bindings=tuple(RenderBindingV2(0, 0) for _ in range(3)),
                    tiles=(TextureTileV2(b"\0" * 4, 8, 1, 1, palette()),
                           TextureTileV2(b"\0" * 4, 8, 1, 1, palette(2)))),
            replace(base, bindings=tuple(RenderBindingV2(0, 0) for _ in range(3)),
                    materials=(TargetMaterialV2(2, 1, 1),),
                    tiles=(TextureTileV2(b"\0" * 3, 8, 1, 1, palette()),)),
            replace(base, bindings=tuple(RenderBindingV2(0, 0) for _ in range(3)),
                    materials=(TargetMaterialV2(4, 0, 0),),
                    tiles=(TextureTileV2(b"\0" * 16, 7, 1, 2, None),)),
        ]
        for resources in invalid:
            with self.subTest(resources=resources), self.assertRaises(ValueError):
                pack_actor_bank_v2(self.core, self.v2_source, resources)
        with self.assertRaises(ValueError):
            pack_actor_bank_v2(self.core, bytes(31), base)
        with self.assertRaises(ValueError):
            pack_actor_bank_v2(self.core + b"\0", self.v2_source, base)

    def test_v2_rejects_structurally_valid_zero_required_draw_counts(self) -> None:
        """Catches v2 accepting a bank with no meshlet and no primitive."""
        core = zero_draw_core(self.core_source)
        self.assertEqual(validate_actor_bank(core).version, 1)
        raw_v2 = unchecked_zero_draw_v2(core, self.v2_source)
        with self.assertRaisesRegex(ValueError, "meshlet/primitive count"):
            validate_actor_bank(raw_v2)
        resources = ActorBankResourcesV2(
            bindings=(), materials=(TargetMaterialV2(1, 0, 0),), tiles=(),
            bake_policy_id=1)
        with self.assertRaisesRegex(ValueError, "meshlet/primitive count"):
            pack_actor_bank_v2(core, self.v2_source, resources)

    def test_packer_preflights_final_bound_before_any_allocation(self) -> None:
        """Catches output growth before aggregate checked-bound validation."""
        cases = (
            (len(self.core) + 87, untextured_resources(), "S64B v2 total size limit"),
            (7, textured_resources(), "texture payload accumulation limit"),
            (16, textured_resources(), "CLUT payload accumulation limit"),
        )
        for reduced_limit, resources, message in cases:
            with self.subTest(limit=reduced_limit, message=message), \
                    patch.object(actor_bank_v2_module, "_PACK_SIZE_LIMIT", reduced_limit), \
                    patch.object(actor_bank_v2_module, "bytearray",
                                 side_effect=AssertionError("allocation preceded preflight"),
                                 create=True), \
                    self.assertRaisesRegex(ValueError, message):
                pack_actor_bank_v2(self.core, self.v2_source, resources)


if __name__ == "__main__":
    unittest.main()
