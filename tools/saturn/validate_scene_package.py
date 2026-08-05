#!/usr/bin/env python3
"""Fail-closed validation for version-one S64P root packages."""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path
from typing import Mapping

from compile_scene_package import (
    DEPENDENCY_DESCRIPTOR_SIZE, DEPENDENCY_KINDS,
    DEPENDENCY_STRUCT, DESTINATION_CLASSES, DESTINATION_NAMES, FLAG_PROVISIONAL,
    HEADER_SIZE, HEADER_STRUCT, KNOWN_FLAGS, LIFETIMES, LIFETIME_NAMES, MAGIC,
    PACKAGE_SHA256_OFFSET, SECTION_DESCRIPTOR_SIZE, SECTION_KINDS, SECTION_NAMES,
    SECTION_STRUCT, VERSION,
)

DEFAULT_BUDGETS = {
    "HWRAM": 1024 * 1024,
    "LWRAM": 1024 * 1024,
    "CART": 4 * 1024 * 1024,
    "VRAM": 512 * 1024,
    "SOUND_RAM": 512 * 1024,
}


class PackageValidationError(ValueError):
    pass


def _fail(message: str) -> None:
    raise PackageValidationError(message)


def _acyclic(masks: list[int], label: str) -> None:
    limit_mask = (1 << len(masks)) - 1 if masks else 0
    if any(mask & ~limit_mask for mask in masks):
        _fail(f"{label} dependency mask references an absent record")
    visiting: set[int] = set()
    visited: set[int] = set()

    def visit(index: int) -> None:
        if index in visiting:
            _fail(f"{label} dependency cycle")
        if index in visited:
            return
        visiting.add(index)
        mask = masks[index]
        for dependency in range(len(masks)):
            if mask & (1 << dependency):
                visit(dependency)
        visiting.remove(index)
        visited.add(index)

    for index in range(len(masks)):
        visit(index)


def _decode_stable_id(raw: bytes) -> str:
    if b"\0" not in raw:
        _fail("dependency stable ID is not NUL terminated")
    value, padding = raw.split(b"\0", 1)
    if not value or any(padding):
        _fail("dependency stable ID has invalid padding")
    try:
        return value.decode("utf-8")
    except UnicodeDecodeError:
        _fail("dependency stable ID is not UTF-8")
    raise AssertionError("unreachable")


def validate_scene_package(package: bytes,
                           payloads: Mapping[str, tuple[bytes, int]] | None = None,
                           *, allow_provisional: bool = False,
                           budgets: Mapping[str, int] | None = None) -> dict[str, object]:
    if len(package) < HEADER_SIZE:
        _fail("truncated S64P header")
    (magic, version, header_size, package_size, level_id, area_id,
     section_count, flags, package_sha, dependency_set_sha) = HEADER_STRUCT.unpack_from(package)
    if magic != MAGIC:
        _fail("wrong S64P magic")
    if version != VERSION:
        _fail("unsupported S64P version")
    if header_size != HEADER_SIZE:
        _fail("wrong S64P header size")
    if package_size != len(package):
        _fail("S64P package size mismatch")
    if section_count != len(SECTION_KINDS):
        _fail("S64P section count must cover the closed enum")
    if flags & ~KNOWN_FLAGS:
        _fail("unknown S64P flags")
    if flags & FLAG_PROVISIONAL and not allow_provisional:
        _fail("provisional S64P root cannot satisfy a target/final gate")
    table_end = header_size + section_count * SECTION_DESCRIPTOR_SIZE
    if table_end > len(package):
        _fail("truncated S64P section descriptor table")

    sections: list[dict[str, object]] = []
    previous_end = table_end
    expected_kinds = list(SECTION_KINDS.values())
    section_masks: list[int] = []
    usage = {name: 0 for name in DEFAULT_BUDGETS}
    for index, expected_kind in enumerate(expected_kinds):
        descriptor_offset = header_size + index * SECTION_DESCRIPTOR_SIZE
        (kind, destination, lifetime, section_version, section_flags, offset,
         size, alignment, dependency_mask, max_scratch, digest,
         reserved) = SECTION_STRUCT.unpack_from(package, descriptor_offset)
        if kind not in SECTION_NAMES:
            _fail("unknown S64P section kind")
        if kind != expected_kind:
            _fail("S64P descriptor order is not canonical")
        if destination not in DESTINATION_NAMES:
            _fail("unknown S64P destination class")
        if lifetime not in LIFETIME_NAMES:
            _fail("unknown S64P lifetime")
        if section_version != 1 or section_flags != 0 or reserved != 0:
            _fail("unsupported S64P section descriptor metadata")
        if alignment < 1 or alignment > 4096 or alignment & (alignment - 1):
            _fail("bad S64P section alignment")
        if offset % alignment:
            _fail("misaligned S64P section offset")
        if offset < previous_end:
            _fail("S64P section overlap or out-of-order offset")
        if offset + size < offset or offset + size > len(package):
            _fail("S64P section range overflow")
        if any(package[previous_end:offset]):
            _fail("S64P alignment gap is not zero-filled")
        content = package[offset:offset + size]
        if hashlib.sha256(content).digest() != digest:
            _fail("S64P section content hash mismatch")
        destination_name = DESTINATION_NAMES[destination]
        if destination_name != "NONE":
            usage[destination_name] += size + max_scratch
        section_masks.append(dependency_mask)
        sections.append({
            "kind": SECTION_NAMES[kind], "kind_id": kind,
            "destination_class": destination_name, "lifetime": LIFETIME_NAMES[lifetime],
            "offset": offset, "size": size, "alignment": alignment,
            "dependency_mask": dependency_mask, "max_scratch": max_scratch,
            "sha256": digest.hex(), "descriptor_offset": descriptor_offset,
        })
        previous_end = offset + size
    if previous_end != len(package):
        _fail("unclaimed trailing bytes in S64P package")
    _acyclic(section_masks, "section")

    dependencies: list[dict[str, object]] = []
    canonical_records: list[tuple[int, bytes, int, bytes]] = []
    for section in sections:
        if section["kind"] not in DEPENDENCY_KINDS:
            continue
        content = package[int(section["offset"]):int(section["offset"]) + int(section["size"])]
        if len(content) < 4:
            _fail("truncated dependency section")
        count = struct.unpack_from(">I", content)[0]
        if len(content) != 4 + count * DEPENDENCY_DESCRIPTOR_SIZE:
            _fail("dependency section size/count mismatch")
        local_order: list[tuple[bytes, bytes]] = []
        for index in range(count):
            values = DEPENDENCY_STRUCT.unpack_from(content, 4 + index * DEPENDENCY_DESCRIPTOR_SIZE)
            (kind, destination, lifetime, stable_raw, byte_count, alignment,
             dependency_mask, max_scratch, digest, generation, dep_flags,
             reserved) = values
            if kind != section["kind_id"] or kind not in (
                    SECTION_KINDS["ACTOR_DEPENDENCIES"],
                    SECTION_KINDS["ANIMATION_DEPENDENCIES"],
                    SECTION_KINDS["AUDIO_DEPENDENCIES"]):
                _fail("unknown or mismatched dependency payload kind")
            if destination not in DESTINATION_NAMES or destination == DESTINATION_CLASSES["NONE"]:
                _fail("unknown dependency destination class")
            if lifetime not in LIFETIME_NAMES:
                _fail("unknown dependency lifetime")
            if alignment < 1 or alignment > 4096 or alignment & (alignment - 1):
                _fail("bad dependency alignment")
            if dep_flags != 0 or reserved != 0:
                _fail("unsupported dependency descriptor metadata")
            stable_id = _decode_stable_id(stable_raw)
            local_order.append((stable_raw, digest))
            canonical_records.append((kind, stable_raw, generation, digest))
            destination_name = DESTINATION_NAMES[destination]
            usage[destination_name] += byte_count + max_scratch
            dependencies.append({
                "kind": SECTION_NAMES[kind], "stable_id": stable_id,
                "byte_count": byte_count, "alignment": alignment,
                "destination_class": destination_name,
                "lifetime": LIFETIME_NAMES[lifetime],
                "dependency_mask": dependency_mask, "max_scratch": max_scratch,
                "sha256": digest.hex(), "generation": generation,
            })
        if local_order != sorted(local_order):
            _fail("dependency descriptor order is not canonical")

    identities = [dependency["stable_id"] for dependency in dependencies]
    if len(identities) != len(set(identities)):
        _fail("duplicate dependency stable ID")
    _acyclic([int(dependency["dependency_mask"]) for dependency in dependencies], "payload")
    canonical_records.sort(key=lambda record: (record[0], record[1], record[3]))
    canonical = b"S64P-DEPS\x00\x01" + b"".join(
        struct.pack(">H32sI32s", *record) for record in canonical_records)
    if hashlib.sha256(canonical).digest() != dependency_set_sha:
        _fail("dependency-set SHA-256 mismatch")

    zeroed = bytearray(package)
    zeroed[PACKAGE_SHA256_OFFSET:PACKAGE_SHA256_OFFSET + 32] = bytes(32)
    if hashlib.sha256(zeroed).digest() != package_sha:
        _fail("package SHA-256 mismatch")

    supplied = {} if payloads is None else dict(payloads)
    required = set(identities)
    missing = required - set(supplied)
    extra = set(supplied) - required
    if missing:
        _fail(f"missing payload: {sorted(missing)[0]}")
    if extra:
        _fail(f"extra payload: {sorted(extra)[0]}")
    by_id = {str(dependency["stable_id"]): dependency for dependency in dependencies}
    for stable_id, (data, generation) in supplied.items():
        dependency = by_id[stable_id]
        if generation != dependency["generation"]:
            _fail(f"wrong payload generation: {stable_id}")
        if len(data) != dependency["byte_count"] or hashlib.sha256(data).hexdigest() != dependency["sha256"]:
            _fail(f"payload hash/size mismatch: {stable_id}")

    limits = dict(DEFAULT_BUDGETS)
    if budgets is not None:
        limits.update(budgets)
    for destination, used in usage.items():
        limit = limits.get(destination)
        if not isinstance(limit, int) or limit < 0:
            _fail(f"invalid budget for {destination}")
        if used > limit:
            _fail(f"{destination} budget overflow: {used} > {limit}")

    return {
        "schema": "sm64-saturn-scene-package-validation-v1",
        "level_id": level_id, "area_id": area_id, "flags": flags,
        "provisional": bool(flags & FLAG_PROVISIONAL),
        "package_size": package_size, "package_sha256": package_sha.hex(),
        "dependency_set_sha256": dependency_set_sha.hex(),
        "sections": sections, "dependencies": dependencies, "budget_usage": usage,
    }


def _load_payloads(path: Path | None) -> dict[str, tuple[bytes, int]]:
    if path is None:
        return {}
    document = json.loads(path.read_text(encoding="utf-8"))
    entries = document.get("payloads")
    if not isinstance(entries, list):
        _fail("payload manifest requires a payloads list")
    result: dict[str, tuple[bytes, int]] = {}
    for entry in entries:
        stable_id = entry["stable_id"]
        if stable_id in result:
            _fail(f"duplicate payload manifest ID: {stable_id}")
        result[stable_id] = (Path(entry["path"]).read_bytes(), int(entry["generation"]))
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--payload-manifest", type=Path)
    parser.add_argument("--allow-provisional", action="store_true")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    report = validate_scene_package(args.input.read_bytes(),
                                    _load_payloads(args.payload_manifest),
                                    allow_provisional=args.allow_provisional)
    if args.report is not None:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                               encoding="utf-8")


if __name__ == "__main__":
    main()
