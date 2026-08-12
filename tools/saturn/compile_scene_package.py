#!/usr/bin/env python3
"""Compile deterministic, version-one Saturn scene root packages.

The S64P root is deliberately small and platform-shaped: inline scene records
are carried in ordinary sections while actor, animation, and audio data remain
external content-addressed payloads.  All integers are big-endian.
"""
from __future__ import annotations

import argparse
import contextlib
import hashlib
import json
import os
import tempfile
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Mapping, Sequence

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
    # Stable IDs, never caller-order bit positions. Bit N in the packed ABI is
    # assigned only after all descriptors reach canonical global order.
    dependencies: tuple[str, ...] = ()
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


def _canonical_dependencies(dependencies: Iterable[DependencyInput]) -> list[DependencyInput]:
    return sorted(dependencies, key=lambda item: (
        _enum(SECTION_KINDS, item.kind, "dependency kind"),
        _stable_id_bytes(item.stable_id), hashlib.sha256(bytes(item.data)).digest()))


def canonical_dependency_masks(dependencies: Iterable[DependencyInput]) -> dict[str, int]:
    """Resolve stable-ID references into the ABI's canonical ordinal bitset."""
    ordered = _canonical_dependencies(dependencies)
    if len(ordered) > 32:
        raise ValueError("S64P supports at most 32 external dependencies")
    indices: dict[str, int] = {}
    for index, dependency in enumerate(ordered):
        if dependency.kind not in DEPENDENCY_KINDS:
            raise ValueError(f"unknown dependency kind: {dependency.kind}")
        _stable_id_bytes(dependency.stable_id)
        if dependency.stable_id in indices:
            raise ValueError(f"duplicate dependency stable ID: {dependency.stable_id}")
        indices[dependency.stable_id] = index
    masks: dict[str, int] = {}
    for dependency in ordered:
        mask = 0
        if len(dependency.dependencies) != len(set(dependency.dependencies)):
            raise ValueError(f"duplicate dependency reference: {dependency.stable_id}")
        for stable_id in dependency.dependencies:
            if stable_id not in indices:
                raise ValueError(
                    f"unknown dependency reference: {dependency.stable_id} -> {stable_id}")
            mask |= 1 << indices[stable_id]
        masks[dependency.stable_id] = mask

    visiting: set[str] = set()
    visited: set[str] = set()

    def visit(stable_id: str) -> None:
        if stable_id in visiting:
            raise ValueError("payload dependency cycle")
        if stable_id in visited:
            return
        visiting.add(stable_id)
        dependency = ordered[indices[stable_id]]
        for referenced in dependency.dependencies:
            visit(referenced)
        visiting.remove(stable_id)
        visited.add(stable_id)

    for stable_id in indices:
        visit(stable_id)
    return masks


def _dependency_section(kind: str, dependencies: list[DependencyInput],
                        masks: Mapping[str, int]) -> bytes:
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
            masks[dependency.stable_id],
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
    masks = canonical_dependency_masks(dependency_list)
    grouped = {kind: [] for kind in DEPENDENCY_KINDS}
    for dependency in dependency_list:
        grouped[dependency.kind].append(dependency)

    normalized: list[SectionInput] = []
    for kind in SECTION_KINDS:
        if kind in DEPENDENCY_KINDS:
            normalized.append(SectionInput(kind, _dependency_section(kind, grouped[kind], masks),
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


def _dependency_from_manifest(manifest_path: Path,
                              payload_path: Path) -> DependencyInput:
    expected_keys = {
        "alignment", "byte_count", "destination_class", "generation",
        "kind", "lifetime", "max_scratch", "sha256", "stable_id",
    }
    document = json.loads(manifest_path.read_text(encoding="utf-8"))
    if not isinstance(document, dict) or set(document) != expected_keys:
        raise ValueError("dependency manifest fields are noncanonical")
    data = payload_path.read_bytes()
    byte_count = _u32(document["byte_count"], "dependency byte count")
    if byte_count != len(data):
        raise ValueError("dependency byte count mismatch")
    digest = hashlib.sha256(data).hexdigest()
    if document["sha256"] != digest:
        raise ValueError("dependency SHA-256 mismatch")
    kind = document["kind"]
    if kind not in DEPENDENCY_KINDS:
        raise ValueError(f"unknown dependency kind: {kind}")
    generation = _u32(document["generation"], "dependency generation")
    if generation == 0:
        raise ValueError("dependency generation must be nonzero")
    return DependencyInput(
        kind=kind,
        stable_id=document["stable_id"],
        data=data,
        destination_class=document["destination_class"],
        lifetime=document["lifetime"],
        max_scratch=_u32(document["max_scratch"], "maximum scratch"),
        generation=generation,
        alignment=_alignment(document["alignment"]),
    )


def _input_within_root(path: Path, root: Path, label: str) -> Path:
    try:
        resolved_root = root.resolve(strict=True)
        resolved = path.resolve(strict=True)
        resolved.relative_to(resolved_root)
    except (FileNotFoundError, RuntimeError, ValueError) as exc:
        raise ValueError(f"{label} escapes payload root: {path}") from exc
    if not resolved.is_file():
        raise ValueError(f"{label} is not a file: {path}")
    return resolved


def _published_bytes_match(path: Path, raw: bytes) -> bool:
    return (os.path.lexists(path) and not path.is_symlink() and
            path.is_file() and path.read_bytes() == raw)


@contextlib.contextmanager
def _publication_lock(paths: Sequence[Path]):
    key = hashlib.sha256(b"\0".join(
        os.path.normcase(str(path)).encode("utf-8")
        for path in paths)).hexdigest()[:24]
    lock_path = Path(tempfile.gettempdir()) / f"sm64-saturn-s64p-{key}.lock"
    with lock_path.open("a+b") as stream:
        stream.seek(0, os.SEEK_END)
        if stream.tell() == 0:
            stream.write(b"\0")
            stream.flush()
        stream.seek(0)
        if os.name == "nt":
            import msvcrt
            msvcrt.locking(stream.fileno(), msvcrt.LK_LOCK, 1)
        else:
            import fcntl
            fcntl.flock(stream.fileno(), fcntl.LOCK_EX)
        try:
            yield
        finally:
            stream.seek(0)
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                import fcntl
                fcntl.flock(stream.fileno(), fcntl.LOCK_UN)


def publish_or_verify_set(
        files: Sequence[tuple[Path, bytes]] | Iterable[tuple[Path, bytes]]) -> None:
    """Publish one generation set atomically or verify identical prior bytes.

    All existing targets are checked before the first link. Private files are
    then hard-linked in caller order (the ready/report marker belongs last).
    A late conflict rolls back only links whose file identity still matches
    this transaction; identical concurrent winners are accepted.
    """
    normalized = tuple((Path(path).absolute(), bytes(raw))
                       for path, raw in files)
    keys = tuple(os.path.normcase(str(path)) for path, _ in normalized)
    if len(keys) != len(set(keys)):
        raise ValueError("duplicate publication target")
    with _publication_lock(tuple(path for path, _ in normalized)):
        _publish_or_verify_set_locked(normalized)


def _publish_or_verify_set_locked(
        normalized: tuple[tuple[Path, bytes], ...]) -> None:
    present = tuple(os.path.lexists(path) for path, _ in normalized)
    for path, raw in normalized:
        path.parent.mkdir(parents=True, exist_ok=True)
        if os.path.lexists(path) and not _published_bytes_match(path, raw):
            raise ValueError(f"publication target exists: {path}")
    if present and present[-1] and not all(present):
        raise ValueError("publication ready marker exists for an incomplete set")

    private_paths: list[Path] = []
    private_files: list[tuple[Path, Path, bytes, tuple[int, int]]] = []
    published: list[tuple[Path, bytes, tuple[int, int]]] = []
    try:
        for path, raw in normalized:
            descriptor, private_name = tempfile.mkstemp(
                prefix=f".{path.name}.private-", dir=path.parent)
            private = Path(private_name)
            private_paths.append(private)
            with os.fdopen(descriptor, "wb") as stream:
                written = stream.write(raw)
                if written != len(raw):
                    raise OSError(
                        f"short private write: {written}/{len(raw)}")
                stream.flush()
                os.fsync(stream.fileno())
            private_stat = private.stat()
            identity = (private_stat.st_dev, private_stat.st_ino)
            private_files.append((private, path, raw, identity))
        for private, path, raw, identity in private_files:
            if os.path.lexists(path):
                if not _published_bytes_match(path, raw):
                    raise ValueError(f"publication target exists: {path}")
                continue
            try:
                os.link(private, path)
            except FileExistsError as exc:
                if not _published_bytes_match(path, raw):
                    raise ValueError(
                        f"publication target exists: {path}") from exc
            else:
                published.append((path, raw, identity))
    except Exception:
        for path, raw, identity in reversed(published):
            try:
                target_stat = path.lstat()
                target_identity = (target_stat.st_dev, target_stat.st_ino)
                if (target_identity == identity and
                        _published_bytes_match(path, raw)):
                    path.unlink()
            except FileNotFoundError:
                pass
        raise
    finally:
        for private in private_paths:
            private.unlink(missing_ok=True)


def publish_or_verify(path: Path, raw: bytes) -> None:
    """Publish or verify one file through the generation-set primitive."""
    publish_or_verify_set(((path, raw),))


def _assembly_bytes(root_package: Path, actor_bundle: Path,
                    assembly_base: Path) -> bytes:
    try:
        base = assembly_base.resolve(strict=True)
        root_relative = Path(os.path.relpath(
            root_package.absolute(), base)).as_posix()
        bundle_relative = Path(os.path.relpath(
            actor_bundle.resolve(strict=True), base)).as_posix()
    except (FileNotFoundError, RuntimeError, ValueError) as exc:
        raise ValueError("assembly inputs cannot be made relative") from exc
    lines = (
        ".section .rodata",
        ".align 4",
        ".global _sm64_saturn_sourceboot_scene_package_root",
        "_sm64_saturn_sourceboot_scene_package_root:",
        f'.incbin "{root_relative}"',
        "_sm64_saturn_sourceboot_scene_package_root_end:",
        ".align 4",
        ".global _sm64_saturn_sourceboot_scene_package_root_size",
        "_sm64_saturn_sourceboot_scene_package_root_size:",
        ".long _sm64_saturn_sourceboot_scene_package_root_end - _sm64_saturn_sourceboot_scene_package_root",
        ".align 4",
        ".global _sm64_saturn_sourceboot_actor_bundle",
        "_sm64_saturn_sourceboot_actor_bundle:",
        f'.incbin "{bundle_relative}"',
        "_sm64_saturn_sourceboot_actor_bundle_end:",
        ".align 4",
        ".global _sm64_saturn_sourceboot_actor_bundle_size",
        "_sm64_saturn_sourceboot_actor_bundle_size:",
        ".long _sm64_saturn_sourceboot_actor_bundle_end - _sm64_saturn_sourceboot_actor_bundle",
        "",
    )
    return "\n".join(lines).encode("ascii")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--level-id", type=int, required=True)
    parser.add_argument("--area-id", type=int, required=True)
    parser.add_argument("--section", action="append", default=[], type=_parse_section_argument)
    parser.add_argument("--dependency-manifest", action="append", default=[],
                        type=Path)
    parser.add_argument("--dependency-payload", action="append", default=[],
                        type=Path)
    parser.add_argument("--payload-root", type=Path)
    parser.add_argument("--provisional", action="store_true")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--metadata-output", type=Path)
    parser.add_argument("--payload-manifest-output", type=Path)
    parser.add_argument("--assembly-output", type=Path)
    parser.add_argument("--assembly-base", type=Path)
    parser.add_argument("--validation-output", type=Path)
    parser.add_argument("--header-output", type=Path)
    parser.add_argument("--abi-output", type=Path)
    parser.add_argument("--symbol-prefix", default="sm64_saturn_scene_package")
    args = parser.parse_args()
    if len(args.dependency_manifest) != len(args.dependency_payload):
        parser.error("each dependency manifest requires one dependency payload")
    if args.dependency_manifest and args.payload_root is None:
        parser.error("external dependencies require --payload-root")
    if (args.assembly_output is None) != (args.assembly_base is None):
        parser.error("--assembly-output and --assembly-base are required together")
    inputs = [SectionInput(kind, path.read_bytes(), alignment=16)
              for kind, path in args.section]
    dependency_manifests = [_input_within_root(
        manifest, args.payload_root, "dependency manifest")
        for manifest in args.dependency_manifest]
    dependency_payloads = [_input_within_root(
        payload, args.payload_root, "dependency payload")
        for payload in args.dependency_payload]
    dependencies = [_dependency_from_manifest(manifest, payload)
                    for manifest, payload in zip(
                        dependency_manifests, dependency_payloads)]
    package = compile_package(args.level_id, args.area_id, inputs, dependencies,
                              provisional=args.provisional)
    payload_manifest_raw = None
    if args.payload_manifest_output is not None:
        output = args.payload_manifest_output
        payloads = [{
            "stable_id": dependency.stable_id,
            "path": Path(os.path.relpath(payload_path, output.parent)).as_posix(),
            "generation": dependency.generation,
        } for dependency, payload_path in zip(
            dependencies, dependency_payloads)]
        payload_manifest_raw = (
            json.dumps({"payloads": payloads}, separators=(",", ":"),
                       sort_keys=True) + "\n").encode("utf-8")
    metadata_raw = None
    if args.metadata_output is not None:
        metadata = parse_package(package)
        metadata["schema"] = "sm64-saturn-scene-package-report-v1"
        metadata_raw = (json.dumps(
            metadata, indent=2, sort_keys=True) + "\n").encode("utf-8")
    assembly_raw = None
    if args.assembly_output is not None:
        if len(dependency_payloads) != 1:
            raise ValueError("sourceboot assembly requires exactly one dependency")
        assembly_raw = _assembly_bytes(
            args.output, dependency_payloads[0], args.assembly_base)

    validation_raw = None
    if args.validation_output is not None:
        from validate_scene_package import validate_scene_package
        payloads = {dependency.stable_id: (
            bytes(dependency.data), dependency.generation)
            for dependency in dependencies}
        validation = validate_scene_package(package, payloads)
        validation_raw = (json.dumps(
            validation, indent=2, sort_keys=True) + "\n").encode("utf-8")
    header_raw = None
    abi_raw = None
    if args.header_output is not None or args.abi_output is not None:
        from emit_scene_package_header import emit_abi_header, emit_header
        if args.header_output is not None:
            header_raw = emit_header(package, args.symbol_prefix).encode("utf-8")
        if args.abi_output is not None:
            abi_raw = emit_abi_header().encode("utf-8")

    publications: list[tuple[Path, bytes]] = [(args.output, package)]
    if payload_manifest_raw is not None:
        publications.append((args.payload_manifest_output, payload_manifest_raw))
    if assembly_raw is not None:
        publications.append((args.assembly_output, assembly_raw))
    if validation_raw is not None:
        publications.append((args.validation_output, validation_raw))
    if header_raw is not None:
        publications.append((args.header_output, header_raw))
    if abi_raw is not None:
        publications.append((args.abi_output, abi_raw))
    if metadata_raw is not None:  # Consumer-ready marker publishes last.
        publications.append((args.metadata_output, metadata_raw))
    publish_or_verify_set(publications)


if __name__ == "__main__":
    main()
