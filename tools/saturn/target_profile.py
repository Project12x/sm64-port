"""Resolve checked-in Saturn target profiles into deterministic package roots."""

from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Literal, Mapping

from hermetic_manifest import (
    canonical_json_bytes,
    normalize_repo_path,
    reject_case_collisions,
    sha256_file,
    write_if_changed,
)


PACKAGE_CLASSES = (
    "route", "input", "camera", "cart", "level", "shared-data", "actor",
    "animation", "audio", "texture",
)
PROFILE_SCHEMA = "sm64-saturn-target-profile-v1"
DESCRIPTOR_SCHEMA = "sm64-saturn-package-descriptor-v1"


@dataclass(frozen=True)
class ResolvedTargetProfile:
    document: dict[str, Any]
    canonical: bytes
    sha256: str
    package_class_manifests: dict[str, Path]
    package_class_hashes: dict[str, str]
    package_set_document: dict[str, Any]
    package_set_canonical: bytes
    package_set_sha256: str


def _read_object(path: Path, label: str) -> dict[str, Any]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"invalid {label}: {path}") from error
    if not isinstance(document, dict):
        raise ValueError(f"{label} must be a JSON object: {path}")
    return document


def _require_keys(document: Mapping[str, Any], expected: set[str], label: str) -> None:
    actual = set(document)
    if actual != expected:
        missing = sorted(expected - actual)
        unknown = sorted(actual - expected)
        raise ValueError(f"{label} keys invalid; missing={missing}, unknown={unknown}")


def _config(value: Mapping[str, Any], label: str, *, allow_empty: bool = False) -> dict[str, int]:
    if not isinstance(value, Mapping) or (not value and not allow_empty):
        raise ValueError(f"{label} must be a non-empty object")
    if any(not isinstance(key, str) or type(item) is not int for key, item in value.items()):
        raise ValueError(f"{label} values must be integers")
    return dict(value)


def _profile(root: Path, profile_path: Path) -> tuple[dict[str, Any], str]:
    relative_profile = normalize_repo_path(root, profile_path)
    document = _read_object(root / relative_profile, "target profile")
    _require_keys(document, {
        "schema", "profile_id", "release_enabled", "release_config", "package_classes",
        "package_descriptors", "output_names",
    }, "target profile")
    if document["schema"] != PROFILE_SCHEMA or not isinstance(document["profile_id"], str) or not document["profile_id"]:
        raise ValueError("target profile schema or profile_id is invalid")
    if type(document["release_enabled"]) is not bool:
        raise ValueError("target profile release_enabled must be boolean")
    document["release_config"] = _config(document["release_config"], "release_config", allow_empty=True)
    classes = document["package_classes"]
    if not isinstance(classes, list) or tuple(classes) != PACKAGE_CLASSES:
        raise ValueError("target profile must declare the canonical package classes")
    descriptors = document["package_descriptors"]
    if not isinstance(descriptors, list) or any(not isinstance(item, str) for item in descriptors):
        raise ValueError("target profile package_descriptors must be strings")
    normalized = [normalize_repo_path(root, item) for item in descriptors]
    reject_case_collisions(normalized)
    if len(set(normalized)) != len(normalized):
        raise ValueError("target profile has duplicate manifest paths")
    document["package_descriptors"] = normalized
    names = document["output_names"]
    if not isinstance(names, dict) or set(names) != {"elf", "source_dat", "iso", "cue"}:
        raise ValueError("target profile output_names must name elf, source_dat, iso, and cue")
    if any(not isinstance(item, str) for item in names.values()):
        raise ValueError("target profile output names must be relative non-empty strings")
    normalized_names: dict[str, str] = {}
    for name, value in names.items():
        try:
            normalized_names[name] = normalize_repo_path(root, value)
        except ValueError as error:
            raise ValueError("target profile output names must be relative repository paths") from error
    document["output_names"] = normalized_names
    return document, relative_profile


def _descriptor(root: Path, descriptor_path: str) -> tuple[dict[str, Any], dict[str, Any], tuple[str, ...]]:
    path = root / descriptor_path
    document = _read_object(path, "package descriptor")
    _require_keys(document, {"schema", "package_class", "package_id", "inputs"}, "package descriptor")
    package_class = document["package_class"]
    package_id = document["package_id"]
    if document["schema"] != DESCRIPTOR_SCHEMA or package_class not in PACKAGE_CLASSES:
        raise ValueError("package descriptor schema or package_class is invalid")
    if not isinstance(package_id, str) or not package_id:
        raise ValueError("package descriptor package_id is invalid")
    inputs = document["inputs"]
    if not isinstance(inputs, list) or not inputs:
        raise ValueError("package descriptor has missing payloads")
    rendered_inputs: list[dict[str, str]] = []
    paths: list[str] = []
    requested_paths: list[str] = []
    for item in inputs:
        if not isinstance(item, dict) or set(item) != {"path"} or not isinstance(item["path"], str):
            raise ValueError("package descriptor input is invalid")
        relative = normalize_repo_path(root, item["path"])
        payload = root / relative
        if not payload.is_file():
            raise ValueError(f"package descriptor payload is missing: {relative}")
        paths.append(relative)
        requested_paths.append(item["path"].replace("\\", "/"))
        rendered_inputs.append({"path": relative, "sha256": sha256_file(payload)})
    reject_case_collisions(requested_paths)
    reject_case_collisions(paths)
    if len(set(paths)) != len(paths):
        raise ValueError("package descriptor has duplicate payload paths")
    manifest = {
        "schema": "sm64-saturn-package-manifest-v1",
        "package_class": package_class,
        "package_id": package_id,
        "source_descriptor_sha256": sha256_file(path),
        "inputs": sorted(rendered_inputs, key=lambda item: item["path"].encode("utf-8")),
    }
    return document, manifest, tuple(requested_paths)


def _revalidate_measurements(root: Path, measurements: Mapping[str, str]) -> None:
    """Fail before publication if any bytes changed after their first hash."""
    for relative, expected in sorted(measurements.items(), key=lambda item: item[0].encode("utf-8")):
        path = root / relative
        if not path.is_file() or sha256_file(path) != expected:
            raise ValueError(f"package descriptor or payload changed during resolution: {relative}")


def resolve_target_profile(root: Path, profile_path: Path, effective_config: Mapping[str, int],
                           output_dir: Path, mode: Literal["development", "release"]) -> ResolvedTargetProfile:
    """Resolve exactly the selected descriptors, rejecting ambiguous input state."""
    if mode not in ("development", "release"):
        raise ValueError("target profile mode must be development or release")
    root = root.resolve()
    profile, _ = _profile(root, profile_path)
    configuration = _config(effective_config, "effective_config")
    if mode == "release":
        if not profile["release_enabled"]:
            raise ValueError(f"{profile['profile_id']} is not release-enabled")
        if configuration != profile["release_config"]:
            raise ValueError("release effective_config must exactly equal release_config")

    package_rows: list[dict[str, str]] = []
    per_class: dict[str, list[dict[str, Any]]] = {kind: [] for kind in PACKAGE_CLASSES}
    seen_tuples: set[tuple[str, str]] = set()
    payload_paths: list[str] = []
    requested_payload_paths: list[str] = []
    measurements: dict[str, str] = {}
    for descriptor_path in profile["package_descriptors"]:
        descriptor, manifest, requested_paths = _descriptor(root, descriptor_path)
        package_key = (descriptor["package_class"], descriptor["package_id"])
        if package_key in seen_tuples:
            raise ValueError(f"duplicate package class/id tuple: {package_key}")
        seen_tuples.add(package_key)
        measurements[descriptor_path] = manifest["source_descriptor_sha256"]
        requested_payload_paths.extend(requested_paths)
        for item in manifest["inputs"]:
            payload_paths.append(item["path"])
            measurements[item["path"]] = item["sha256"]
        manifest_canonical = canonical_json_bytes(manifest)
        manifest_sha256 = hashlib.sha256(manifest_canonical).hexdigest()
        package_rows.append({
            "package_class": descriptor["package_class"], "package_id": descriptor["package_id"],
            "manifest_sha256": manifest_sha256,
        })
        per_class[descriptor["package_class"]].append(manifest)

    reject_case_collisions(requested_payload_paths)
    reject_case_collisions(payload_paths)
    if len(set(payload_paths)) != len(payload_paths):
        raise ValueError("target profile has duplicate payload paths")

    output_dir = output_dir.resolve()
    package_class_manifests: dict[str, Path] = {}
    package_class_hashes: dict[str, str] = {}
    for package_class in PACKAGE_CLASSES:
        manifests = per_class[package_class]
        manifests.sort(key=lambda item: (item["package_id"].encode("utf-8"),
                                         canonical_json_bytes(item)))
        aggregate = {
            "schema": "sm64-saturn-package-class-manifest-v1",
            "package_class": package_class,
            "packages": manifests,
        }
        aggregate_canonical = canonical_json_bytes(aggregate)
        aggregate_sha256 = hashlib.sha256(aggregate_canonical).hexdigest()
        path = output_dir / f"{package_class}-packages.json"
        _revalidate_measurements(root, measurements)
        write_if_changed(path, aggregate_canonical)
        package_class_manifests[package_class] = path
        package_class_hashes[package_class] = aggregate_sha256

    package_rows.sort(key=lambda row: (
        row["package_class"].encode("utf-8"),
        row["package_id"].encode("utf-8"),
        row["manifest_sha256"].encode("ascii"),
    ))
    package_set_document = {
        "schema": "sm64-saturn-package-set-v2",
        "profile_id": profile["profile_id"],
        "packages": package_rows,
        "package_class_hashes": package_class_hashes,
    }
    package_set_canonical = canonical_json_bytes(package_set_document)
    package_set_sha256 = hashlib.sha256(package_set_canonical).hexdigest()
    document = {
        "schema": "sm64-saturn-resolved-target-profile-v1",
        "profile_id": profile["profile_id"],
        "release_enabled": profile["release_enabled"],
        "effective_config": configuration,
        "package_manifests": package_rows,
        "output_names": profile["output_names"],
    }
    canonical = canonical_json_bytes(document)
    return ResolvedTargetProfile(
        document=document, canonical=canonical,
        sha256=hashlib.sha256(canonical).hexdigest(),
        package_class_manifests=package_class_manifests,
        package_class_hashes=package_class_hashes,
        package_set_document=package_set_document,
        package_set_canonical=package_set_canonical,
        package_set_sha256=package_set_sha256,
    )
