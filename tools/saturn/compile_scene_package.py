#!/usr/bin/env python3
"""Compile deterministic, version-one Saturn scene root packages.

The S64P root is deliberately small and platform-shaped: inline scene records
are carried in ordinary sections while actor, animation, and audio data remain
external content-addressed payloads.  All integers are big-endian.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Mapping

MAGIC = b"S64P"
VERSION = 1
FLAG_PROVISIONAL = 0x0001
KNOWN_FLAGS = FLAG_PROVISIONAL

SECTION_KINDS = {
    "WORLD_STATIC": 1,
    "COLLISION": 2,
    "SKY_BACKGROUND": 3,
    "BSP_PORTAL": 4,
    "ACTOR_DEPENDENCIES": 5,
    "ANIMATION_DEPENDENCIES": 6,
    "AUDIO_DEPENDENCIES": 7,
    "RESIDENCY_PLAN": 8,
}
SECTION_NAMES = {value: key for key, value in SECTION_KINDS.items()}
DEPENDENCY_KINDS = frozenset({
    "ACTOR_DEPENDENCIES", "ANIMATION_DEPENDENCIES", "AUDIO_DEPENDENCIES"})
DESTINATION_CLASSES = {"NONE": 0, "HWRAM": 1, "LWRAM": 2, "CART": 3,
                       "VRAM": 4, "SOUND_RAM": 5}
DESTINATION_NAMES = {value: key for key, value in DESTINATION_CLASSES.items()}
LIFETIMES = {"BOOT": 1, "SCENE": 2, "AREA": 3, "FRAME": 4, "STREAM": 5}
LIFETIME_NAMES = {value: key for key, value in LIFETIMES.items()}

HEADER_STRUCT = struct.Struct(">4sHHIHHHH32s32s")
SECTION_STRUCT = struct.Struct(">HBBHHIIIII32sI")
DEPENDENCY_STRUCT = struct.Struct(">HBB32sIIII32sIII")
HEADER_SIZE = HEADER_STRUCT.size
SECTION_DESCRIPTOR_SIZE = SECTION_STRUCT.size
DEPENDENCY_DESCRIPTOR_SIZE = DEPENDENCY_STRUCT.size
PACKAGE_SHA256_OFFSET = 20
DEPENDENCY_SET_SHA256_OFFSET = 52


@dataclass(frozen=True)
class SectionInput:
    kind: str
    data: bytes
    alignment: int = 4
    destination_class: str = "NONE"
    lifetime: str = "SCENE"
    dependency_mask: int = 0
    max_scratch: int = 0


@dataclass(frozen=True)
class DependencyInput:
    kind: str
    stable_id: str
    data: bytes
    destination_class: str = "CART"
    lifetime: str = "SCENE"
    dependency_mask: int = 0
    max_scratch: int = 0
    generation: int = 1
    alignment: int = 4


def _u32(value: int, name: str) -> int:
    if not isinstance(value, int) or value < 0 or value > 0xFFFFFFFF:
        raise ValueError(f"{name} is outside uint32")
    return value


def _alignment(value: int) -> int:
    if not isinstance(value, int) or value < 1 or value > 4096 or value & (value - 1):
        raise ValueError("alignment must be a power of two from 1 through 4096")
    return value


def _enum(mapping: Mapping[str, int], value: str, name: str) -> int:
    try:
        return mapping[value]
    except KeyError as error:
        raise ValueError(f"unknown {name}: {value}") from error


def _stable_id_bytes(stable_id: str) -> bytes:
    encoded = stable_id.encode("utf-8")
    if not encoded or len(encoded) > 31 or b"\0" in encoded:
        raise ValueError("stable ID must be 1..31 non-NUL UTF-8 bytes")
    return encoded + bytes(32 - len(encoded))


def _align(value: int, alignment: int) -> int:
    return (value + alignment - 1) & -alignment


def dependency_set_canonical_bytes(dependencies: Iterable[DependencyInput]) -> bytes:
    """Return the versioned byte sequence covered by dependency_set_sha256."""
    records = []
    for dependency in dependencies:
        kind = _enum(SECTION_KINDS, dependency.kind, "dependency kind")
        digest = hashlib.sha256(bytes(dependency.data)).digest()
        records.append((kind, _stable_id_bytes(dependency.stable_id), dependency.generation, digest))
    records.sort(key=lambda record: (record[0], record[1], record[3]))
    return b"S64P-DEPS\x00\x01" + b"".join(
        struct.pack(">H32sI32s", kind, stable_id, generation, digest)
        for kind, stable_id, generation, digest in records)


def _dependency_section(kind: str, dependencies: list[DependencyInput]) -> bytes:
    ordered = sorted(dependencies, key=lambda item: (
        item.stable_id.encode("utf-8"), hashlib.sha256(bytes(item.data)).digest()))
    records = []
    for dependency in ordered:
        if dependency.kind != kind:
            raise ValueError("dependency grouped under the wrong section kind")
        destination = _enum(DESTINATION_CLASSES, dependency.destination_class, "destination")
        if destination == DESTINATION_CLASSES["NONE"]:
            raise ValueError("external dependency destination cannot be NONE")
        records.append(DEPENDENCY_STRUCT.pack(
            SECTION_KINDS[kind], destination,
            _enum(LIFETIMES, dependency.lifetime, "lifetime"),
            _stable_id_bytes(dependency.stable_id),
            _u32(len(dependency.data), "dependency byte count"),
            _alignment(dependency.alignment),
            _u32(dependency.dependency_mask, "dependency mask"),
            _u32(dependency.max_scratch, "maximum scratch"),
            hashlib.sha256(bytes(dependency.data)).digest(),
            _u32(dependency.generation, "generation"), 0, 0))
    return struct.pack(">I", len(records)) + b"".join(records)


def compile_package(level_id: int, area_id: int,
                    sections: Iterable[SectionInput],
                    dependencies: Iterable[DependencyInput] = (), *,
                    provisional: bool = False) -> bytes:
    """Pack an S64P root. Input ordering never affects the output bytes."""
    if not 0 <= level_id <= 0xFFFF or not 1 <= area_id <= 0xFFFF:
        raise ValueError("level/area IDs are outside the S64P header range")
    supplied: dict[str, SectionInput] = {}
    for section in sections:
        _enum(SECTION_KINDS, section.kind, "section kind")
        if section.kind in DEPENDENCY_KINDS:
            raise ValueError("dependency sections are compiler-owned")
        if section.kind in supplied:
            raise ValueError(f"duplicate section: {section.kind}")
        supplied[section.kind] = section

    dependency_list = list(dependencies)
    seen_dependencies: set[tuple[str, str]] = set()
    grouped = {kind: [] for kind in DEPENDENCY_KINDS}
    for dependency in dependency_list:
        if dependency.kind not in DEPENDENCY_KINDS:
            raise ValueError(f"unknown dependency kind: {dependency.kind}")
        identity = (dependency.kind, dependency.stable_id)
        if identity in seen_dependencies:
            raise ValueError(f"duplicate dependency: {dependency.kind}/{dependency.stable_id}")
        seen_dependencies.add(identity)
        grouped[dependency.kind].append(dependency)

    normalized: list[SectionInput] = []
    for kind in SECTION_KINDS:
        if kind in DEPENDENCY_KINDS:
            normalized.append(SectionInput(kind, _dependency_section(kind, grouped[kind]),
                                           alignment=16))
        else:
            normalized.append(supplied.get(kind, SectionInput(kind, b"", alignment=4)))

    table_end = HEADER_SIZE + len(normalized) * SECTION_DESCRIPTOR_SIZE
    cursor = table_end
    descriptors: list[bytes] = []
    payload = bytearray()
    for section in normalized:
        alignment = _alignment(section.alignment)
        offset = _align(cursor, alignment)
        payload.extend(bytes(offset - cursor))
        data = bytes(section.data)
        descriptors.append(SECTION_STRUCT.pack(
            SECTION_KINDS[section.kind],
            _enum(DESTINATION_CLASSES, section.destination_class, "destination"),
            _enum(LIFETIMES, section.lifetime, "lifetime"),
            1, 0, offset, _u32(len(data), "section byte size"), alignment,
            _u32(section.dependency_mask, "section dependency mask"),
            _u32(section.max_scratch, "section maximum scratch"),
            hashlib.sha256(data).digest(), 0))
        payload.extend(data)
        cursor = offset + len(data)

    package_size = HEADER_SIZE + len(descriptors) * SECTION_DESCRIPTOR_SIZE + len(payload)
    _u32(package_size, "package size")
    dependency_digest = hashlib.sha256(
        dependency_set_canonical_bytes(dependency_list)).digest()
    flags = FLAG_PROVISIONAL if provisional else 0
    header = HEADER_STRUCT.pack(MAGIC, VERSION, HEADER_SIZE, package_size,
                                level_id, area_id, len(descriptors), flags,
                                bytes(32), dependency_digest)
    package = bytearray(header + b"".join(descriptors) + payload)
    package_digest = hashlib.sha256(package).digest()
    package[PACKAGE_SHA256_OFFSET:PACKAGE_SHA256_OFFSET + 32] = package_digest
    return bytes(package)


def parse_package(package: bytes) -> dict[str, object]:
    """Decode package fields without claiming validation."""
    if len(package) < HEADER_SIZE:
        raise ValueError("truncated S64P header")
    (magic, version, header_size, package_size, level_id, area_id,
     section_count, flags, package_sha, dependency_sha) = HEADER_STRUCT.unpack_from(package)
    sections = []
    for index in range(section_count):
        descriptor_offset = header_size + index * SECTION_DESCRIPTOR_SIZE
        if descriptor_offset + SECTION_DESCRIPTOR_SIZE > len(package):
            raise ValueError("truncated S64P section table")
        (kind, destination, lifetime, section_version, section_flags, offset,
         size, alignment, dependency_mask, max_scratch, digest,
         reserved) = SECTION_STRUCT.unpack_from(package, descriptor_offset)
        sections.append({
            "kind_id": kind, "kind": SECTION_NAMES.get(kind, f"UNKNOWN_{kind}"),
            "destination_id": destination,
            "destination_class": DESTINATION_NAMES.get(destination, f"UNKNOWN_{destination}"),
            "lifetime_id": lifetime,
            "lifetime": LIFETIME_NAMES.get(lifetime, f"UNKNOWN_{lifetime}"),
            "section_version": section_version, "flags": section_flags,
            "offset": offset, "size": size, "alignment": alignment,
            "dependency_mask": dependency_mask, "max_scratch": max_scratch,
            "sha256": digest.hex(), "reserved": reserved,
            "descriptor_offset": descriptor_offset,
        })
    return {
        "magic": magic.decode("ascii", errors="replace"), "version": version,
        "header_size": header_size, "package_size": package_size,
        "level_id": level_id, "area_id": area_id, "section_count": section_count,
        "flags": flags, "provisional": bool(flags & FLAG_PROVISIONAL),
        "package_sha256": package_sha.hex(),
        "dependency_set_sha256": dependency_sha.hex(), "sections": sections,
    }


def _parse_section_argument(value: str) -> tuple[str, Path]:
    if "=" not in value:
        raise argparse.ArgumentTypeError("section must be KIND=PATH")
    kind, path = value.split("=", 1)
    if kind not in SECTION_KINDS or kind in DEPENDENCY_KINDS:
        raise argparse.ArgumentTypeError(f"invalid inline section kind: {kind}")
    return kind, Path(path)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--level-id", type=int, required=True)
    parser.add_argument("--area-id", type=int, required=True)
    parser.add_argument("--section", action="append", default=[], type=_parse_section_argument)
    parser.add_argument("--provisional", action="store_true")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--metadata-output", type=Path)
    args = parser.parse_args()
    inputs = [SectionInput(kind, path.read_bytes(), alignment=16)
              for kind, path in args.section]
    package = compile_package(args.level_id, args.area_id, inputs,
                              provisional=args.provisional)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(package)
    if args.metadata_output is not None:
        metadata = parse_package(package)
        metadata["schema"] = "sm64-saturn-scene-package-report-v1"
        args.metadata_output.parent.mkdir(parents=True, exist_ok=True)
        args.metadata_output.write_text(
            json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
