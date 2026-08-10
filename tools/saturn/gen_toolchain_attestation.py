#!/usr/bin/env python3
"""Seal the byte identity of the external Saturn compiler toolchain."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence

from hermetic_manifest import (
    canonical_json_bytes,
    normalize_repo_path,
    reject_case_collisions,
    sha256_file,
    write_if_changed,
)


SCHEMA = "sm64-saturn-toolchain-attestation-v1"
TARGET_ABI = "sh2eb-none-elf"
YAUL_VERSION = "0.3.1"
YAUL_COMMIT = "6012f79f237773378c8014e70d8998ad95a38d98"
_TOOL_ARGUMENTS = (
    ("gcc", "compiler"), ("as", "assembler"), ("ld", "linker driver"),
    ("nm", "nm"), ("objcopy", "objcopy"), ("objdump", "objdump"),
    ("readelf", "readelf"), ("addr2line", "addr2line"),
)


@dataclass(frozen=True)
class ToolchainComponent:
    """One diagnostically rooted toolchain component with invoked binaries."""

    component_id: str
    version: str
    root: Path
    binaries: tuple[Path, ...]


@dataclass(frozen=True)
class AttestationBuild:
    """The complete canonical result and ownership keys for external inputs."""

    document: dict[str, Any]
    canonical: bytes
    sha256: str
    external_dependency_keys: frozenset[tuple[str, str]]


def build_toolchain_attestation(
    components: Sequence[ToolchainComponent], external_dependencies: Sequence[Path]
) -> AttestationBuild:
    """Measure all invoked tools and externally discovered headers deterministically."""
    normalized_components = _validate_components(components)
    dependency_paths = _validate_external_paths(external_dependencies)
    dependencies_by_component: dict[str, list[dict[str, str]]] = {
        component.component_id: [] for component in normalized_components
    }
    measurements: dict[Path, str] = {}
    external_keys: set[tuple[str, str]] = set()

    component_rows: list[dict[str, Any]] = []
    for component in normalized_components:
        binary_records = _records_for_paths(
            component.root, component.binaries, "toolchain binary", "binary", measurements
        )
        component_rows.append({
            "id": component.component_id,
            "version": component.version,
            "binaries": binary_records,
            "dependencies": dependencies_by_component[component.component_id],
        })

    rows_by_id = {row["id"]: row for row in component_rows}
    for dependency in dependency_paths:
        matches: list[tuple[ToolchainComponent, str]] = []
        for component in normalized_components:
            try:
                relative = normalize_repo_path(component.root, dependency)
            except ValueError:
                continue
            matches.append((component, relative))
        if not matches:
            raise ValueError(f"unclassified external dependency: {dependency}")
        if len(matches) != 1:
            raise ValueError(f"external dependency matches multiple toolchain components: {dependency}")
        component, relative = matches[0]
        digest = _measure_file(dependency, "external dependency", measurements)
        rows_by_id[component.component_id]["dependencies"].append({
            "path": relative,
            "sha256": digest,
        })
        external_keys.add((component.component_id, relative))

    for row in component_rows:
        row["dependencies"] = _sorted_records(row["dependencies"])

    document = {
        "schema": SCHEMA,
        "target_abi": TARGET_ABI,
        "components": component_rows,
    }
    canonical = canonical_json_bytes(document)
    _revalidate_measurements(measurements)
    return AttestationBuild(
        document=document,
        canonical=canonical,
        sha256=hashlib.sha256(canonical).hexdigest(),
        external_dependency_keys=frozenset(external_keys),
    )


def verify_toolchain_attestation(
    path: Path, components: Sequence[ToolchainComponent], external_dependencies: Sequence[Path]
) -> dict[str, Any]:
    """Require a valid sealed file to have the exact current canonical bytes."""
    built = build_toolchain_attestation(components, external_dependencies)
    raw, sealed = _load_sealed_attestation(path)
    _validate_attestation_document(sealed)
    if raw != built.canonical:
        raise ValueError("toolchain attestation differs from current inputs")
    return sealed


def write_toolchain_attestation(
    path: Path, components: Sequence[ToolchainComponent], external_dependencies: Sequence[Path]
) -> AttestationBuild:
    """Validate every current input before atomically publishing new sealed bytes."""
    built = build_toolchain_attestation(components, external_dependencies)
    write_if_changed(path, built.canonical)
    return built


def _validate_components(components: Sequence[ToolchainComponent]) -> tuple[ToolchainComponent, ...]:
    values = tuple(components)
    if not values:
        raise ValueError("toolchain attestation requires at least one component")
    component_ids: list[str] = []
    for component in values:
        if not isinstance(component, ToolchainComponent):
            raise ValueError("toolchain component is invalid")
        if not isinstance(component.component_id, str) or not component.component_id:
            raise ValueError("toolchain component id is invalid")
        if not isinstance(component.version, str) or not component.version:
            raise ValueError("toolchain component version is invalid")
        if not Path(component.root).is_dir():
            raise ValueError(f"toolchain component root is not a directory: {component.root}")
        component_ids.append(component.component_id)
    _reject_duplicate_or_case_colliding(component_ids, "component id")
    return tuple(sorted(values, key=lambda item: item.component_id.encode("utf-8")))


def _validate_external_paths(paths: Sequence[Path]) -> tuple[Path, ...]:
    requested = [_path_spelling(path) for path in paths]
    _reject_duplicate_or_case_colliding(requested, "external dependency")
    resolved = tuple(Path(path).resolve() for path in paths)
    if len(set(resolved)) != len(resolved):
        raise ValueError("duplicate external dependency records")
    return tuple(sorted(resolved, key=_path_sort_key))


def _records_for_paths(
    root: Path, paths: Sequence[Path], file_kind: str, record_kind: str,
    measurements: dict[Path, str],
) -> list[dict[str, str]]:
    requested = [_requested_component_relative(root, path) for path in paths]
    _reject_duplicate_or_case_colliding(requested, record_kind)
    records: list[dict[str, str]] = []
    for path in paths:
        relative = normalize_repo_path(root, path)
        resolved = (root / path).resolve() if not Path(path).is_absolute() else Path(path).resolve()
        records.append({
            "path": relative,
            "sha256": _measure_file(resolved, file_kind, measurements),
        })
    normalized = [record["path"] for record in records]
    _reject_duplicate_or_case_colliding(normalized, record_kind)
    return _sorted_records(records)


def _requested_component_relative(root: Path, path: Path) -> str:
    root = Path(root).absolute()
    candidate = Path(path)
    candidate = candidate if candidate.is_absolute() else root / candidate
    try:
        return candidate.relative_to(root).as_posix()
    except ValueError:
        return candidate.as_posix()


def _path_spelling(path: Path) -> str:
    return Path(path).absolute().as_posix()


def _reject_duplicate_or_case_colliding(values: Iterable[str], kind: str) -> None:
    rendered = list(values)
    if len(set(rendered)) != len(rendered):
        if kind == "component id":
            raise ValueError("duplicate component ids")
        raise ValueError(f"duplicate {kind} records")
    try:
        reject_case_collisions(rendered)
    except ValueError as error:
        raise ValueError(str(error).replace("repository paths", f"{kind} paths")) from error


def _measure_file(path: Path, kind: str, measurements: dict[Path, str]) -> str:
    resolved = Path(path).resolve()
    if not resolved.is_file():
        raise ValueError(f"{kind} is not a file: {resolved}")
    digest = sha256_file(resolved)
    existing = measurements.get(resolved)
    if existing is not None and existing != digest:
        raise ValueError(f"{kind} changed during toolchain attestation: {resolved}")
    measurements[resolved] = digest
    return digest


def _revalidate_measurements(measurements: Mapping[Path, str]) -> None:
    for path, expected in sorted(measurements.items(), key=lambda item: _path_sort_key(item[0])):
        if not path.is_file() or sha256_file(path) != expected:
            raise ValueError(f"toolchain input changed during attestation: {path}")


def _sorted_records(records: Sequence[dict[str, str]]) -> list[dict[str, str]]:
    return sorted(records, key=lambda row: row["path"].encode("utf-8"))


def _load_sealed_attestation(path: Path) -> tuple[bytes, dict[str, Any]]:
    path = Path(path)
    if not path.is_file():
        raise ValueError(f"sealed toolchain attestation is not a file: {path}")
    raw = path.read_bytes()
    try:
        document = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError("sealed toolchain attestation is not canonical JSON") from error
    if not isinstance(document, dict) or canonical_json_bytes(document) != raw:
        raise ValueError("sealed toolchain attestation is not canonical JSON")
    return raw, document


def _validate_attestation_document(document: Mapping[str, Any]) -> None:
    expected = {"schema", "target_abi", "components"}
    actual = set(document)
    if actual != expected:
        raise ValueError(f"toolchain attestation keys invalid; missing={sorted(expected - actual)}, "
                         f"unknown={sorted(actual - expected)}")
    if document.get("schema") != SCHEMA or document.get("target_abi") != TARGET_ABI:
        raise ValueError("toolchain attestation schema is invalid")
    components = document.get("components")
    if not isinstance(components, list) or not components:
        raise ValueError("toolchain attestation components are invalid")
    ids: list[str] = []
    for component in components:
        if not isinstance(component, dict) or set(component) != {"id", "version", "binaries", "dependencies"}:
            raise ValueError("toolchain attestation component is invalid")
        component_id, version = component["id"], component["version"]
        if not isinstance(component_id, str) or not component_id or not isinstance(version, str) or not version:
            raise ValueError("toolchain attestation component is invalid")
        ids.append(component_id)
        _validate_records(component["binaries"], "binary")
        _validate_records(component["dependencies"], "dependency")
    _reject_duplicate_or_case_colliding(ids, "component id")
    if components != sorted(components, key=lambda item: item["id"].encode("utf-8")):
        raise ValueError("toolchain attestation components are not sorted")


def _validate_records(records: Any, kind: str) -> None:
    if not isinstance(records, list):
        raise ValueError(f"toolchain attestation {kind} records are invalid")
    paths: list[str] = []
    for record in records:
        if not isinstance(record, dict) or set(record) != {"path", "sha256"}:
            raise ValueError("toolchain attestation record is invalid")
        path, digest = record["path"], record["sha256"]
        if (not isinstance(path, str) or not path or path != normalize_repo_path(Path.cwd(), path)
                or not _is_lowercase_sha256(digest)):
            raise ValueError("toolchain attestation record is invalid")
        paths.append(path)
    _reject_duplicate_or_case_colliding(paths, kind)
    if records != _sorted_records(records):
        raise ValueError(f"toolchain attestation {kind} records are not sorted")


def _is_lowercase_sha256(value: Any) -> bool:
    return (isinstance(value, str) and len(value) == 64
            and all(character in "0123456789abcdef" for character in value))


def _path_sort_key(path: Path) -> tuple[bytes, ...]:
    return tuple(part.encode("utf-8") for part in path.as_posix().split("/"))


def _gcc_version(path: Path) -> str:
    result = subprocess.run([str(path), "--version"], check=False, capture_output=True, text=True)
    if result.returncode != 0 or not result.stdout:
        raise ValueError(f"compiler --version failed: {path}")
    return result.stdout


def _sourceboot_component(args: argparse.Namespace) -> ToolchainComponent:
    if args.yaul_version != YAUL_VERSION or args.yaul_commit != YAUL_COMMIT:
        raise ValueError("sourceboot toolchain must use Yaul 0.3.1 commit " + YAUL_COMMIT)
    binaries = tuple(Path(getattr(args, argument)) for argument, _label in _TOOL_ARGUMENTS)
    version = (f"yaul-{args.yaul_version} commit-{args.yaul_commit}; "
               f"gcc --version:\n{_gcc_version(binaries[0])}")
    return ToolchainComponent("yaul-sh-sdk", version, Path(args.yaul_root), binaries)


def _parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--yaul-root", type=Path, required=True)
    parser.add_argument("--yaul-version", required=True)
    parser.add_argument("--yaul-commit", required=True)
    for argument, label in _TOOL_ARGUMENTS:
        parser.add_argument(f"--{argument}", type=Path, required=True, help=f"invoked {label} binary")
    parser.add_argument("--external-dependency", type=Path, action="append", default=[])
    parser.add_argument("--output", type=Path)
    parser.add_argument("--verify", type=Path)
    args = parser.parse_args(argv)
    if bool(args.output) == bool(args.verify):
        parser.error("provide exactly one of --output or --verify")
    return args


def main(argv: Sequence[str] | None = None) -> None:
    args = _parse_args(argv)
    component = _sourceboot_component(args)
    if args.verify:
        verify_toolchain_attestation(args.verify, [component], args.external_dependency)
    else:
        write_toolchain_attestation(args.output, [component], args.external_dependency)


if __name__ == "__main__":
    main()
