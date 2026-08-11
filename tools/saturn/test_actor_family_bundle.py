#!/usr/bin/env python3
"""Canonical S64F-v3 host writer/parser tests and target fixture builder."""

from __future__ import annotations

import hashlib
import struct
import sys
import unittest
from dataclasses import replace
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from actor_family_bundle import (  # noqa: E402
    FAMILY_FLAG_GEOMETRY,
    FAMILY_FLAG_SUPPORTED,
    BundleDocument,
    FamilyDocument,
    S64F_V3_FAMILY,
    S64F_V3_HEADER,
    S64F_V3_VARIANT,
    SourceRecord,
    VariantKey,
    pack_bundle,
    source_identity,
    validate_bundle,
)


def _align(data: bytearray, alignment: int = 4) -> int:
    while len(data) % alignment:
        data.append(0)
    return len(data)


def tiny_bank(family_ordinal: int, model_id: int, source_hash: bytes) -> bytes:
    """Build a hand-derived minimal S64B-v1 accepted by the real target parser."""
    header = struct.Struct(">4s9HI32sHH10I")
    animation = struct.Struct(">IIHHHh")
    geometry_header = struct.Struct(">4s7H7I")
    meshlet = struct.Struct(">HHBB6h12I")
    payload = bytearray(104)
    records_offset = len(payload)
    payload.extend(bytes(animation.size))
    indices_offset = _align(payload)
    payload.extend(struct.pack(">I12H", 12, 1, 0, 1, 0, 1, 0,
                               1, 0, 1, 0, 1, 0))
    indices_size = len(payload) - indices_offset
    values_offset = _align(payload)
    payload.extend(struct.pack(">Ih", 1, 0))
    values_size = len(payload) - values_offset
    vertices_offset = _align(payload)
    payload.extend(struct.pack(">hhhHH", 0, 0, 0, 0, 0) * 3)
    vertices_size = len(payload) - vertices_offset
    meshlets_offset = _align(payload)
    joint_offset = geometry_header.size
    part_offset = joint_offset + 12
    material_offset = part_offset + 4
    meshlet_offset = material_offset + 4
    primitive_offset = meshlet_offset + meshlet.size
    primitive_ref_offset = primitive_offset + 10
    vertex_ref_offset = primitive_ref_offset + 4
    geometry = bytearray(geometry_header.pack(
        b"GEO1", 1, 1, 1, 1, 1, 2, 6, joint_offset, part_offset,
        material_offset, meshlet_offset, primitive_offset,
        primitive_ref_offset, vertex_ref_offset))
    geometry.extend(struct.pack(">hhhhhH", -1, 0, 0, 0, 0, 0))
    geometry.extend(struct.pack(">HH", 0, 0))
    geometry.extend(bytes(4))
    geometry.extend(meshlet.pack(
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 1, 0, 3, 1, 1, 3, 3, 2, 0, 6, 0))
    geometry.extend(struct.pack(">5H", 0, 0, 1, 2, 2))
    geometry.extend(struct.pack(">2H", 0, 0))
    geometry.extend(struct.pack(">6H", 0, 1, 2, 0, 1, 2))
    payload.extend(geometry)
    meshlets_size = len(geometry)
    payload[records_offset:records_offset + animation.size] = animation.pack(
        values_offset + 4, indices_offset + 4, 1, 1, 0, 1)
    payload[:header.size] = header.pack(
        b"S64B", 1, family_ordinal, model_id, 1, 1, 1, 1, 3, 1,
        0x1F, source_hash, 104, 16, records_offset,
        indices_offset, indices_size, values_offset, values_size,
        vertices_offset, vertices_size, meshlets_offset, meshlets_size, 203)
    return bytes(payload)


def family(stable_hash: int, *, supported: bool = True,
           models: tuple[int, ...] = (1,)) -> FamilyDocument:
    flags = FAMILY_FLAG_SUPPORTED | FAMILY_FLAG_GEOMETRY if supported else 0
    return FamilyDocument(
        stable_family_hash=stable_hash,
        capability_mask=0x4000 if supported else 0,
        runtime_capability_mask=0x11 if supported else 0,
        maximum_live_instances=4 if supported else 0,
        source_actor_count=2,
        flags=flags,
        name=f"family-{stable_hash}".encode(),
        source=b"actors/example/geo.inc.c",
        unsupported=b"" if supported else b"unsupported",
        metadata_blob=b"{",
        model_ids=models,
    )


def document_and_banks(families: tuple[FamilyDocument, ...] | None = None):
    families = families or (family(0x1001, models=(7, 9)), family(0x2002, models=(3,)))
    document = BundleDocument(package_generation=0x12345678, families=families)
    banks = {}
    for ordinal, item in enumerate(families, 1):
        for model_id in item.model_ids:
            digest = hashlib.sha256(f"source:{ordinal}:{model_id}".encode()).digest()
            banks[VariantKey(ordinal, model_id)] = tiny_bank(ordinal, model_id, digest)
    return document, banks


def write_c_fixture(path: Path) -> None:
    document, banks = document_and_banks()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(pack_bundle(document, banks))


class ActorFamilyBundleTest(unittest.TestCase):
    def test_exact_record_sizes_and_canonical_source_identity(self) -> None:
        self.assertEqual(S64F_V3_HEADER.size, 96)
        self.assertEqual(S64F_V3_FAMILY.size, 64)
        self.assertEqual(S64F_V3_VARIANT.size, 88)
        sources = (
            SourceRecord("actors/foo/model.inc.c", bytes(range(32))),
            SourceRecord("actors/foo/anims/a.inc.c", bytes(reversed(range(32)))),
        )
        expected = hashlib.sha256(
            b"S64B-VARIANT-SOURCES\x00\x01\x00\x02\x00\x07"
            b"actors/foo/anims/a.inc.c\x00" + bytes(reversed(range(32))) +
            b"actors/foo/model.inc.c\x00" + bytes(range(32))).digest()
        self.assertEqual(source_identity(2, 7, tuple(reversed(sources))), expected)
        for bad in ("", "/absolute", "C:/absolute", "a\\b", "a/./b", "a/../b", "a//b"):
            with self.subTest(path=bad), self.assertRaises(ValueError):
                source_identity(2, 7, (SourceRecord(bad, bytes(32)),))
        with self.assertRaisesRegex(ValueError, "duplicate source path"):
            source_identity(2, 7, (sources[0], sources[0]))
        with self.assertRaisesRegex(ValueError, "source set"):
            source_identity(2, 7, ())

    def test_deterministic_bytes_layout_metadata_alias_and_zero_padding(self) -> None:
        doc, banks = document_and_banks()
        payload = pack_bundle(doc, dict(reversed(tuple(banks.items()))))
        self.assertEqual(payload, pack_bundle(doc, banks))
        self.assertEqual(len(payload), 1580)
        self.assertEqual(hashlib.sha256(payload).hexdigest(),
                         "fa47d3342b2111e946778a1f67bb479f235ecb7be7bcaeac5e2d0d0a8fcf42e4")
        view = validate_bundle(payload)
        self.assertEqual(view.package_generation, 0x12345678)
        self.assertEqual([(v.family_ordinal, v.model_id) for v in view.variants],
                         [(1, 7), (1, 9), (2, 3)])
        header = S64F_V3_HEADER.unpack_from(payload)
        self.assertEqual(header[:7], (b"S64F", 3, 96, 2, 3, 64, 88))
        self.assertEqual(header[7:10], (96, 224, 488))
        self.assertEqual(header[15], 0x12345678)
        self.assertEqual(header[16], 0)
        self.assertEqual(header[18], 0)
        metadata_offset, metadata_size, bank_offset = header[9], header[10], header[11]
        self.assertEqual(payload[metadata_offset + metadata_size:bank_offset],
                         bytes(bank_offset - metadata_offset - metadata_size))
        self.assertEqual(len(payload) % 4, 0)
        self.assertEqual(payload[64:96], hashlib.sha256(payload[:64] + bytes(32) + payload[96:]).digest())

    def test_limits_order_duplicates_zero_and_colliding_bank_ids(self) -> None:
        families = tuple(family(0x1000 + index, models=(index * 2 + 1, index * 2 + 2))
                         for index in range(64))
        doc, banks = document_and_banks(families)
        self.assertEqual(validate_bundle(pack_bundle(doc, banks)).variant_count, 128)
        with self.assertRaisesRegex(ValueError, "family limit"):
            pack_bundle(BundleDocument(1, families + (family(0x9000),)), banks)
        too_many = (family(1, models=tuple(range(1, 130))),)
        doc129, banks129 = document_and_banks(too_many)
        with self.assertRaisesRegex(ValueError, "variant limit"):
            pack_bundle(doc129, banks129)
        with self.assertRaisesRegex(ValueError, "family order"):
            pack_bundle(BundleDocument(1, tuple(reversed(families[:2]))), {})
        with self.assertRaisesRegex(ValueError, "duplicate family"):
            pack_bundle(BundleDocument(1, (family(1), family(1))), {})
        with self.assertRaisesRegex(ValueError, "variant model order/duplicate"):
            pack_bundle(BundleDocument(1, (family(1, models=(1, 1)),)), {})
        one, one_banks = document_and_banks((family(1),))
        key = next(iter(one_banks))
        zero_id = bytearray(one_banks[key]); zero_id[26:30] = bytes(4)
        with self.assertRaisesRegex(ValueError, "bank ID"):
            pack_bundle(one, {key: bytes(zero_id)})
        collision_doc, collision_banks = document_and_banks((family(1, models=(1, 2)),))
        keys = tuple(collision_banks)
        collision = bytearray(collision_banks[keys[1]])
        collision[26:30] = collision_banks[keys[0]][26:30]
        with self.assertRaisesRegex(ValueError, "bank ID collision"):
            pack_bundle(collision_doc, {keys[0]: collision_banks[keys[0]], keys[1]: bytes(collision)})

    def test_validator_rejects_header_table_span_hash_and_padding_mutations(self) -> None:
        doc, banks = document_and_banks()
        valid = pack_bundle(doc, banks)
        mutations = {
            "version": (4, b"\x00\x02"),
            "family offset": (16, b"\xff\xff\xff\xfc"),
            "variant offset": (20, b"\xff\xff\xff\xfc"),
            "metadata span": (28, b"\xff\xff\xff\xff"),
            "bank span": (36, b"\xff\xff\xff\xff"),
            "total size": (56, b"\xff\xff\xff\xff"),
            "reserved": (60, b"\x00\x00\x00\x01"),
        }
        for name, (offset, replacement) in mutations.items():
            corrupt = bytearray(valid); corrupt[offset:offset + len(replacement)] = replacement
            with self.subTest(name=name), self.assertRaises(ValueError):
                validate_bundle(bytes(corrupt))
        corrupt = bytearray(valid); corrupt[-1] ^= 1
        with self.assertRaisesRegex(ValueError, "content hash"):
            validate_bundle(bytes(corrupt))
        header = S64F_V3_HEADER.unpack_from(valid)
        padding = header[9] + header[10]
        if padding < header[11]:
            corrupt = bytearray(valid); corrupt[padding] = 1
            corrupt[64:96] = hashlib.sha256(corrupt[:64] + bytes(32) + corrupt[96:]).digest()
            with self.assertRaisesRegex(ValueError, "padding"):
                validate_bundle(bytes(corrupt))

    def test_validator_rejects_variant_and_embedded_bank_identity_mismatches(self) -> None:
        doc, banks = document_and_banks()
        valid = pack_bundle(doc, banks)
        header = S64F_V3_HEADER.unpack_from(valid)
        variant_offset = header[8]
        bank_root = header[11]
        bank_rel = struct.unpack_from(">I", valid, variant_offset + 8)[0]
        bank = bank_root + bank_rel
        cases = {
            "family": (bank + 6, b"\x00\x02"),
            "model": (bank + 8, b"\x00\x08"),
            "source": (bank + 26, b"\x7f"),
            "scratch": (bank + 98, b"\x00\x00\x00\xcc"),
            "pose": (bank + struct.unpack_from(">I", valid, bank + 62)[0] + 8,
                     b"\x00\x00"),
            "geometry": (bank + struct.unpack_from(">I", valid, bank + 90)[0], b"BAD!"),
        }
        for name, (offset, replacement) in cases.items():
            corrupt = bytearray(valid); corrupt[offset:offset + len(replacement)] = replacement
            embedded_size = struct.unpack_from(">I", corrupt, variant_offset + 12)[0]
            digest = hashlib.sha256(corrupt[bank:bank + embedded_size]).digest()
            corrupt[variant_offset + 24:variant_offset + 56] = digest
            corrupt[64:96] = hashlib.sha256(corrupt[:64] + bytes(32) + corrupt[96:]).digest()
            with self.subTest(name=name), self.assertRaises(ValueError):
                validate_bundle(bytes(corrupt))

    def test_validator_rejects_every_family_metadata_and_variant_bank_span_class(self) -> None:
        doc, banks = document_and_banks()
        valid = pack_bundle(doc, banks)
        header = S64F_V3_HEADER.unpack_from(valid)
        family_offset, variant_offset = header[7], header[8]
        for offset in (24, 28, 32, 36, 40, 44, 48, 52, 56, 58):
            corrupt = bytearray(valid)
            width = 2 if offset in (56, 58) else 4
            corrupt[family_offset + offset:family_offset + offset + width] = bytes([0xFF]) * width
            corrupt[64:96] = hashlib.sha256(corrupt[:64] + bytes(32) + corrupt[96:]).digest()
            with self.subTest(family_field=offset), self.assertRaises(ValueError):
                validate_bundle(bytes(corrupt))
        for offset in (8, 12):
            corrupt = bytearray(valid)
            corrupt[variant_offset + offset:variant_offset + offset + 4] = b"\xff\xff\xff\xfc"
            corrupt[64:96] = hashlib.sha256(corrupt[:64] + bytes(32) + corrupt[96:]).digest()
            with self.subTest(variant_field=offset), self.assertRaises(ValueError):
                validate_bundle(bytes(corrupt))

    def test_noncanonical_input_and_v2_are_rejected_without_partial_view(self) -> None:
        doc, banks = document_and_banks()
        with self.assertRaisesRegex(ValueError, "package generation"):
            pack_bundle(replace(doc, package_generation=0), banks)
        with self.assertRaisesRegex(ValueError, "bank mapping"):
            pack_bundle(doc, {next(iter(banks)): next(iter(banks.values()))})
        v2 = struct.pack(">4sHHIIII32s", b"S64F", 2, 1, 56, 56, 112, 0, bytes(range(32))) + bytes(56)
        with self.assertRaisesRegex(ValueError, "version"):
            validate_bundle(v2)


if __name__ == "__main__":
    unittest.main()
