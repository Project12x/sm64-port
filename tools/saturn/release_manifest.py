#!/usr/bin/env python3
"""Seal, verify, and compare exact Saturn release artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Literal, Mapping

import gen_build_identity as identity
from hermetic_manifest import canonical_json_bytes, write_if_changed


SCHEMA = "sm64-saturn-release-manifest-v1"
COMPARISON_SCHEMA = "sm64-saturn-release-comparison-v1"
MANIFEST_NAME = "saturn-release-manifest-v1.json"
OUTPUT_NAMES = ("elf", "source_dat", "iso", "cue")
_SHA256 = re.compile(r"[0-9a-f]{64}\Z")
_FILE_LINE = re.compile(
    r'^\s*FILE\s+"([^"]+)"\s+\S+\s*$', re.IGNORECASE | re.MULTILINE
)
_ANY_FILE_LINE = re.compile(r"^\s*FILE\b", re.IGNORECASE | re.MULTILINE)


@dataclass(frozen=True)
class ReleaseManifestVerification:
    document: dict[str, Any]
    manifest_sha256: str
    outputs: dict[str, Path]


def _strict_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    document: dict[str, Any] = {}
    for key, value in pairs:
        if key in document:
            raise ValueError(f"duplicate JSON object key: {key}")
        document[key] = value
    return document


def _load_canonical(path: Path, label: str) -> tuple[bytes, dict[str, Any]]:
    if not path.is_file():
        raise ValueError(f"{label} is not a file: {path}")
    raw = path.read_bytes()
    try:
        document = json.loads(raw.decode("ascii"), object_pairs_hook=_strict_object)
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as error:
        raise ValueError(f"{label} is not canonical JSON: {path}") from error
    if not isinstance(document, dict) or canonical_json_bytes(document) != raw:
        raise ValueError(f"{label} is not canonical JSON: {path}")
    return raw, document


def _require_sha256(value: Any, label: str) -> str:
    if not isinstance(value, str) or _SHA256.fullmatch(value) is None:
        raise ValueError(f"{label} is not a lowercase SHA-256")
    return value


def _measure(path: Path, label: str) -> tuple[int, str]:
    if not path.is_file():
        raise ValueError(f"{label} is not a file: {path}")
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            size += len(chunk)
            digest.update(chunk)
    if size <= 0:
        raise ValueError(f"{label} is empty: {path}")
    return size, digest.hexdigest()


def _release_relative(base: Path, path: Path, label: str) -> str:
    resolved = path.resolve()
    try:
        relative = resolved.relative_to(base.resolve())
    except ValueError as error:
        raise ValueError(f"{label} path escapes release directory: {path}") from error
    rendered = relative.as_posix()
    if not rendered or rendered == ".":
        raise ValueError(f"{label} path is empty")
    return rendered


def _resolve_release_path(base: Path, value: Any, label: str) -> Path:
    if not isinstance(value, str) or not value or "\\" in value:
        raise ValueError(f"{label} release path is invalid")
    requested = Path(value)
    if requested.is_absolute() or any(part in ("", ".", "..") for part in requested.parts):
        raise ValueError(f"{label} release path is invalid")
    resolved = (base / requested).resolve()
    try:
        resolved.relative_to(base.resolve())
    except ValueError as error:
        raise ValueError(f"{label} release path escapes manifest directory") from error
    if requested.as_posix() != value:
        raise ValueError(f"{label} release path is not canonical")
    return resolved


def _elf_sections(data: bytes) -> tuple[str, list[dict[str, int]]]:
    if len(data) < 52 or data[:4] != b"\x7fELF" or data[4] != 1:
        raise ValueError("ELF output is not ELF32")
    endian = "big" if data[5] == 2 else "little" if data[5] == 1 else None
    if endian is None:
        raise ValueError("ELF output has unknown byte order")
    table = int.from_bytes(data[32:36], endian)
    entry_size = int.from_bytes(data[46:48], endian)
    count = int.from_bytes(data[48:50], endian)
    if entry_size < 40 or count == 0:
        raise ValueError("ELF section headers are malformed")
    sections: list[dict[str, int]] = []
    for index in range(count):
        offset = table + index * entry_size
        if offset + 40 > len(data):
            raise ValueError("ELF section header is outside file")
        row = {
            "type": int.from_bytes(data[offset + 4 : offset + 8], endian),
            "flags": int.from_bytes(data[offset + 8 : offset + 12], endian),
            "address": int.from_bytes(data[offset + 12 : offset + 16], endian),
            "offset": int.from_bytes(data[offset + 16 : offset + 20], endian),
            "size": int.from_bytes(data[offset + 20 : offset + 24], endian),
            "link": int.from_bytes(data[offset + 24 : offset + 28], endian),
            "entry_size": int.from_bytes(data[offset + 36 : offset + 40], endian),
        }
        if row["offset"] + row["size"] > len(data):
            raise ValueError("ELF section contents are outside file")
        sections.append(row)
    return endian, sections


def _extract_build_identity(elf: Path) -> tuple[bytes, dict[str, Any]]:
    """Extract exactly one validated saturn_build_identity from one ELF snapshot."""
    data = elf.read_bytes()
    endian, sections = _elf_sections(data)
    matches: list[tuple[int, int, int]] = []
    for section in sections:
        if section["type"] != 2:
            continue
        entry_size = section["entry_size"]
        if entry_size < 16 or section["size"] % entry_size:
            raise ValueError("ELF symbol table is malformed")
        if section["link"] >= len(sections) or sections[section["link"]]["type"] != 3:
            raise ValueError("ELF symbol string table is malformed")
        strings = sections[section["link"]]
        string_data = data[strings["offset"] : strings["offset"] + strings["size"]]
        for relative in range(0, section["size"], entry_size):
            entry = section["offset"] + relative
            name_offset = int.from_bytes(data[entry : entry + 4], endian)
            if name_offset >= len(string_data):
                continue
            end = string_data.find(b"\0", name_offset)
            if end < 0:
                raise ValueError("ELF symbol name is unterminated")
            name = string_data[name_offset:end].decode("ascii", errors="strict")
            if name.lstrip("_") != "saturn_build_identity":
                continue
            address = int.from_bytes(data[entry + 4 : entry + 8], endian)
            size = int.from_bytes(data[entry + 8 : entry + 12], endian)
            section_index = int.from_bytes(data[entry + 14 : entry + 16], endian)
            matches.append((address, size, section_index))
    if len(matches) != 1:
        raise ValueError(
            "ELF output must contain exactly one saturn_build_identity symbol"
        )
    address, size, section_index = matches[0]
    if size not in identity.SUPPORTED_IDENTITY_SIZES:
        raise ValueError(
            f"ELF saturn_build_identity has unsupported size {size}"
        )
    if section_index <= 0 or section_index >= len(sections):
        raise ValueError("ELF saturn_build_identity section is invalid")
    section = sections[section_index]
    relative = address - section["address"]
    if relative < 0 or relative + size > section["size"]:
        raise ValueError("ELF saturn_build_identity is outside its section")
    start = section["offset"] + relative
    raw = data[start : start + size]
    return raw, identity.validate_identity(raw)


def _validate_cue(cue: Path, iso: Path) -> None:
    try:
        text = cue.read_text(encoding="ascii")
    except (OSError, UnicodeDecodeError) as error:
        raise ValueError("CUE output is not ASCII text") from error
    file_lines = _ANY_FILE_LINE.findall(text)
    directives = _FILE_LINE.findall(text)
    if len(file_lines) != 1 or len(directives) != 1:
        raise ValueError("CUE must contain exactly one FILE directive")
    reference = Path(directives[0])
    if reference.is_absolute() or any(part == ".." for part in reference.parts):
        raise ValueError("CUE ISO reference is unsafe")
    if reference.name != iso.name or (cue.parent / reference).resolve() != iso.resolve():
        raise ValueError("CUE referenced ISO differs from release manifest ISO")


def _validate_identity_binding(
    raw: bytes,
    values: Mapping[str, Any],
    identity_document: Mapping[str, Any],
    effective_config: Mapping[str, Any],
) -> None:
    version = values["version"]
    expected_keys = {"schema", "label", "identity_sha256", "identity"}
    if version == identity.IDENTITY_V2_VERSION:
        expected_keys.add("effective_config")
    if set(identity_document) != expected_keys:
        raise ValueError("identity JSON keys differ from its declared version")
    if identity_document.get("schema") != f"sm64-saturn-build-identity-v{version}":
        raise ValueError("identity JSON schema differs from ELF identity")
    if identity_document.get("identity") != dict(values):
        raise ValueError("identity JSON values differ from ELF identity")
    digest = hashlib.sha256(raw).hexdigest()
    if identity_document.get("identity_sha256") != digest:
        raise ValueError("identity JSON SHA-256 differs from ELF identity")
    canonical_config = identity.canonical_effective_config(effective_config)
    if hashlib.sha256(canonical_config).hexdigest() != values["effective_config_hash"]:
        raise ValueError("effective config does not match ELF identity")


def _validate_input_documents(
    profile: Mapping[str, Any],
    source_closure: Mapping[str, Any],
    package_set: Mapping[str, Any],
    toolchain: Mapping[str, Any],
) -> None:
    if (
        set(profile) != {
            "schema", "profile_id", "release_enabled", "effective_config",
            "package_manifests", "output_names",
        }
        or profile.get("schema") != "sm64-saturn-resolved-target-profile-v1"
    ):
        raise ValueError("resolved target profile schema is invalid")
    if (
        not isinstance(profile.get("profile_id"), str)
        or not profile["profile_id"]
        or type(profile.get("release_enabled")) is not bool
        or not isinstance(profile.get("effective_config"), dict)
        or not isinstance(profile.get("package_manifests"), list)
        or not isinstance(profile.get("output_names"), dict)
        or set(profile["output_names"]) != set(OUTPUT_NAMES)
    ):
        raise ValueError("resolved target profile id is invalid")
    if (
        set(source_closure) != {"schema", "inputs"}
        or source_closure.get("schema") != "sm64-saturn-source-closure-v2"
        or not isinstance(source_closure.get("inputs"), list)
    ):
        raise ValueError("source closure schema is invalid")
    closure_paths: set[str] = set()
    for row in source_closure["inputs"]:
        if (
            not isinstance(row, dict)
            or set(row) != {"path", "sha256", "class", "owners"}
            or not isinstance(row.get("path"), str)
            or not row["path"]
            or "\\" in row["path"]
            or Path(row["path"]).is_absolute()
            or any(part in ("", ".", "..") for part in Path(row["path"]).parts)
            or _SHA256.fullmatch(str(row.get("sha256"))) is None
            or not isinstance(row.get("class"), str)
            or not isinstance(row.get("owners"), list)
            or not row["owners"]
            or any(not isinstance(owner, str) or not owner for owner in row["owners"])
            or row["owners"] != sorted(row["owners"])
            or row["path"] in closure_paths
        ):
            raise ValueError("source closure record schema is invalid")
        closure_paths.add(row["path"])
    if (
        set(package_set) != {
            "schema", "profile_id", "packages", "package_class_hashes"
        }
        or package_set.get("schema") != "sm64-saturn-package-set-v2"
        or package_set.get("profile_id") != profile["profile_id"]
        or not isinstance(package_set.get("packages"), list)
        or package_set["packages"] != profile["package_manifests"]
        or not isinstance(package_set.get("package_class_hashes"), dict)
    ):
        raise ValueError("package set differs from resolved target profile")
    for row in package_set["packages"]:
        if (
            not isinstance(row, dict)
            or set(row) != {"package_class", "package_id", "manifest_sha256"}
            or not isinstance(row.get("package_class"), str)
            or not isinstance(row.get("package_id"), str)
            or _SHA256.fullmatch(str(row.get("manifest_sha256"))) is None
        ):
            raise ValueError("package set record schema is invalid")
    if any(
        not isinstance(name, str) or _SHA256.fullmatch(str(digest)) is None
        for name, digest in package_set["package_class_hashes"].items()
    ):
        raise ValueError("package set class hashes are invalid")
    if (
        set(toolchain) != {"schema", "target_abi", "components"}
        or toolchain.get("schema") != "sm64-saturn-toolchain-attestation-v1"
        or not isinstance(toolchain.get("target_abi"), str)
        or not isinstance(toolchain.get("components"), list)
    ):
        raise ValueError("toolchain attestation schema is invalid")


def _git_provenance(
    root: Path, source_closure: Mapping[str, Any], mode: str
) -> dict[str, Any]:
    """Record revision and one closure-clean fact without serializing dirty paths."""
    revision = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=root, check=False,
        capture_output=True, text=True,
    )
    if revision.returncode != 0 or re.fullmatch(r"[0-9a-fA-F]{40}\s*", revision.stdout) is None:
        raise ValueError("repository Git revision is unavailable")
    paths = [
        row.get("path") for row in source_closure["inputs"]
        if isinstance(row, dict)
        and isinstance(row.get("path"), str)
        and not (
            row.get("class") == "generated-input"
            and row["path"].startswith("build/")
        )
    ]
    if len(paths) != len(set(paths)) or any(not path for path in paths):
        raise ValueError("source closure provenance paths are invalid")
    status = subprocess.run(
        ["git", "status", "--porcelain=v1", "--untracked-files=all", "--", *paths],
        cwd=root, check=False, capture_output=True, text=True,
    )
    if status.returncode != 0:
        raise ValueError("source closure cleanliness is unavailable")
    clean = not bool(status.stdout)
    if mode == "release" and not clean:
        raise ValueError("release source closure is not clean")
    return {
        "git_revision": revision.stdout.strip().lower(),
        "closure_clean": clean,
    }


def build_release_manifest(
    root: Path,
    profile_path: Path,
    identity_json: Path,
    source_closure: Path,
    package_set: Path,
    toolchain_attestation: Path,
    outputs: Mapping[str, Path],
    mode: Literal["development", "release"],
) -> bytes:
    """Return canonical bytes only after every input and output is sealed."""
    if mode not in ("development", "release"):
        raise ValueError("release manifest mode must be development or release")
    if set(outputs) != set(OUTPUT_NAMES):
        raise ValueError("release outputs must name elf, source_dat, iso, and cue")
    root = root.resolve()
    profile_raw, profile = _load_canonical(profile_path, "resolved target profile")
    identity_raw, identity_document = _load_canonical(identity_json, "identity JSON")
    closure_raw, closure = _load_canonical(source_closure, "source closure")
    package_raw, packages = _load_canonical(package_set, "package set")
    toolchain_raw, toolchain = _load_canonical(
        toolchain_attestation, "toolchain attestation"
    )
    _validate_input_documents(profile, closure, packages, toolchain)
    effective_config = identity_document.get("effective_config")
    if not isinstance(effective_config, dict):
        raise ValueError("identity JSON effective config is missing")

    resolved_outputs = {name: Path(outputs[name]).resolve() for name in OUTPUT_NAMES}
    release_dir = resolved_outputs["cue"].parent
    records: dict[str, dict[str, Any]] = {}
    measurements: dict[str, tuple[int, str]] = {}
    for name in OUTPUT_NAMES:
        size, digest = _measure(resolved_outputs[name], f"{name} output")
        measurements[name] = (size, digest)
        records[name] = {
            "path": _release_relative(release_dir, resolved_outputs[name], name),
            "size": size,
            "sha256": digest,
        }
    raw_embedded, values = _extract_build_identity(resolved_outputs["elf"])
    _validate_identity_binding(
        raw_embedded, values, identity_document, effective_config
    )
    profile_sha = hashlib.sha256(profile_raw).hexdigest()
    closure_sha = hashlib.sha256(closure_raw).hexdigest()
    package_sha = hashlib.sha256(package_raw).hexdigest()
    toolchain_sha = hashlib.sha256(toolchain_raw).hexdigest()
    if values["source_hash"] != closure_sha:
        raise ValueError("source closure SHA-256 differs from ELF identity")
    if values.get("target_profile_hash") != profile_sha:
        raise ValueError("target profile SHA-256 differs from ELF identity")
    if values.get("package_set_root_hash") != package_sha:
        raise ValueError("package set SHA-256 differs from ELF identity")
    if values.get("toolchain_attestation_hash") != toolchain_sha:
        raise ValueError("toolchain attestation SHA-256 differs from ELF identity")
    _validate_cue(resolved_outputs["cue"], resolved_outputs["iso"])

    provenance = _git_provenance(root, closure, mode)
    document = {
        "schema": SCHEMA,
        "profile_id": profile["profile_id"],
        "target_profile_sha256": profile_sha,
        "identity_version": values["version"],
        "identity_sha256": hashlib.sha256(raw_embedded).hexdigest(),
        "effective_config_sha256": values["effective_config_hash"],
        "effective_config": effective_config,
        "source_closure_sha256": closure_sha,
        "package_set_sha256": package_sha,
        "toolchain_attestation_sha256": toolchain_sha,
        "mode": mode,
        "reproducibility": "uncompared",
        "provenance": provenance,
        "outputs": records,
    }
    for path, expected, label in (
        (profile_path, profile_raw, "resolved target profile"),
        (identity_json, identity_raw, "identity JSON"),
        (source_closure, closure_raw, "source closure"),
        (package_set, package_raw, "package set"),
        (toolchain_attestation, toolchain_raw, "toolchain attestation"),
    ):
        if path.read_bytes() != expected:
            raise ValueError(f"{label} changed during release sealing")
    for name, path in resolved_outputs.items():
        if _measure(path, f"{name} output") != measurements[name]:
            raise ValueError(f"{name} output changed during release sealing")
    return canonical_json_bytes(document)


def _validate_manifest_shape(document: Mapping[str, Any]) -> None:
    expected = {
        "schema", "profile_id", "target_profile_sha256", "identity_version",
        "identity_sha256", "effective_config_sha256", "effective_config",
        "source_closure_sha256", "package_set_sha256",
        "toolchain_attestation_sha256", "mode", "reproducibility",
        "provenance", "outputs",
    }
    if set(document) != expected or document.get("schema") != SCHEMA:
        raise ValueError("release manifest schema or keys are invalid")
    if not isinstance(document.get("profile_id"), str) or not document["profile_id"]:
        raise ValueError("release manifest profile id is invalid")
    for field in (
        "target_profile_sha256", "identity_sha256", "effective_config_sha256",
        "source_closure_sha256", "package_set_sha256",
        "toolchain_attestation_sha256",
    ):
        _require_sha256(document.get(field), f"release manifest {field}")
    if document.get("identity_version") not in (
        identity.IDENTITY_V1_VERSION, identity.IDENTITY_V2_VERSION
    ):
        raise ValueError("release manifest identity version is invalid")
    if not isinstance(document.get("effective_config"), dict):
        raise ValueError("release manifest effective config is invalid")
    if document.get("mode") not in ("development", "release"):
        raise ValueError("release manifest mode is invalid")
    if document.get("reproducibility") != "uncompared":
        raise ValueError("release manifest reproducibility state is invalid")
    provenance = document.get("provenance")
    if (
        not isinstance(provenance, dict)
        or set(provenance) != {"git_revision", "closure_clean"}
        or not isinstance(provenance.get("git_revision"), str)
        or re.fullmatch(r"[0-9a-f]{40}", provenance["git_revision"]) is None
        or type(provenance.get("closure_clean")) is not bool
    ):
        raise ValueError("release manifest provenance is invalid")
    if document["mode"] == "release" and not provenance["closure_clean"]:
        raise ValueError("release manifest release closure is not clean")
    if not isinstance(document.get("outputs"), dict) or set(document["outputs"]) != set(
        OUTPUT_NAMES
    ):
        raise ValueError("release manifest outputs are invalid")


def verify_release_manifest(
    path: Path, *, required_profile: str | None = None
) -> ReleaseManifestVerification:
    """Verify canonical manifest bytes, every output, CUE binding, and identity."""
    raw, document = _load_canonical(path, "release manifest")
    _validate_manifest_shape(document)
    if required_profile is not None and document["profile_id"] != required_profile:
        raise ValueError(
            f"release manifest required profile is {required_profile}, "
            f"got {document['profile_id']}"
        )
    base = path.resolve().parent
    outputs: dict[str, Path] = {}
    seen: set[Path] = set()
    for name in OUTPUT_NAMES:
        record = document["outputs"][name]
        if not isinstance(record, dict) or set(record) != {"path", "size", "sha256"}:
            raise ValueError(f"release manifest {name} output record is invalid")
        output = _resolve_release_path(base, record["path"], name)
        if output in seen:
            raise ValueError("release manifest output paths are not unique")
        seen.add(output)
        if type(record["size"]) is not int or record["size"] <= 0:
            raise ValueError(f"release manifest {name} output size is invalid")
        expected_sha = _require_sha256(record["sha256"], f"{name} output")
        size, digest = _measure(output, f"{name} output")
        if digest != expected_sha:
            raise ValueError(
                f"{name} output SHA-256 mismatch: expected {expected_sha}, got {digest}"
            )
        if size != record["size"]:
            raise ValueError(
                f"{name} output size mismatch: expected {record['size']}, got {size}"
            )
        outputs[name] = output

    embedded, values = _extract_build_identity(outputs["elf"])
    if hashlib.sha256(embedded).hexdigest() != document["identity_sha256"]:
        raise ValueError("ELF identity SHA-256 differs from release manifest")
    if values["version"] != document["identity_version"]:
        raise ValueError("ELF identity version differs from release manifest")
    if values["effective_config_hash"] != document["effective_config_sha256"]:
        raise ValueError("ELF effective config digest differs from release manifest")
    canonical_config = identity.canonical_effective_config(document["effective_config"])
    if hashlib.sha256(canonical_config).hexdigest() != values["effective_config_hash"]:
        raise ValueError("effective config does not match ELF identity")
    if values["source_hash"] != document["source_closure_sha256"]:
        raise ValueError("source closure digest differs from ELF identity")
    if values["version"] == identity.IDENTITY_V2_VERSION:
        bindings = {
            "target_profile_hash": "target_profile_sha256",
            "package_set_root_hash": "package_set_sha256",
            "toolchain_attestation_hash": "toolchain_attestation_sha256",
        }
        for identity_field, manifest_field in bindings.items():
            if values[identity_field] != document[manifest_field]:
                raise ValueError(
                    f"{manifest_field} differs from ELF identity"
                )
    _validate_cue(outputs["cue"], outputs["iso"])
    return ReleaseManifestVerification(
        document=dict(document),
        manifest_sha256=hashlib.sha256(raw).hexdigest(),
        outputs=outputs,
    )


def _differences(first: Any, second: Any, prefix: str = "") -> list[str]:
    if type(first) is not type(second):
        return [prefix or "document"]
    if isinstance(first, dict):
        differences: list[str] = []
        for key in sorted(set(first) | set(second)):
            field = f"{prefix}.{key}" if prefix else key
            if key not in first or key not in second:
                differences.append(field)
            else:
                differences.extend(_differences(first[key], second[key], field))
        return differences
    if isinstance(first, list):
        if len(first) != len(second):
            return [prefix]
        differences = []
        for index, (left, right) in enumerate(zip(first, second)):
            differences.extend(_differences(left, right, f"{prefix}[{index}]"))
        return differences
    return [] if first == second else [prefix]


def compare_release_manifests(first: Path, second: Path) -> dict[str, Any]:
    """Compare verified canonical inputs and bytes, never their host locations."""
    first_verified = verify_release_manifest(first)
    second_verified = verify_release_manifest(second)
    differing = _differences(first_verified.document, second_verified.document)
    return {
        "schema": COMPARISON_SCHEMA,
        "identical": not differing,
        "differing_fields": differing,
        "first_manifest_sha256": first_verified.manifest_sha256,
        "second_manifest_sha256": second_verified.manifest_sha256,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    write = subparsers.add_parser("write", help="seal exact release outputs")
    write.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    write.add_argument("--profile", type=Path, required=True)
    write.add_argument("--identity-json", type=Path, required=True)
    write.add_argument("--source-closure", type=Path, required=True)
    write.add_argument("--package-set", type=Path, required=True)
    write.add_argument("--toolchain-attestation", type=Path, required=True)
    write.add_argument("--mode", choices=("development", "release"), required=True)
    for name in OUTPUT_NAMES:
        write.add_argument("--" + name.replace("_", "-"), type=Path, required=True)
    write.add_argument("--output", type=Path, required=True)
    verify = subparsers.add_parser("verify", help="verify one sealed release")
    verify.add_argument("--manifest", type=Path, required=True)
    verify.add_argument("--required-profile")
    compare = subparsers.add_parser("compare", help="compare two sealed releases")
    compare.add_argument("--first", type=Path, required=True)
    compare.add_argument("--second", type=Path, required=True)
    compare.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    if args.command == "write":
        if args.output.resolve().parent != args.cue.resolve().parent:
            parser.error("release manifest output must be beside the CUE")
        raw = build_release_manifest(
            args.root, args.profile, args.identity_json, args.source_closure,
            args.package_set, args.toolchain_attestation,
            {name: getattr(args, name) for name in OUTPUT_NAMES}, args.mode,
        )
        write_if_changed(args.output.resolve(), raw)
        print(hashlib.sha256(raw).hexdigest())
        return 0
    if args.command == "verify":
        verified = verify_release_manifest(
            args.manifest, required_profile=args.required_profile
        )
        print(verified.manifest_sha256)
        return 0
    comparison = compare_release_manifests(args.first, args.second)
    raw = canonical_json_bytes(comparison)
    if args.output is not None:
        write_if_changed(args.output.resolve(), raw)
    print(raw.decode("ascii"), end="")
    return 0 if comparison["identical"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
