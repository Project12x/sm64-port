#!/usr/bin/env python3
"""Compose sourceboot identity v2 from sealed hermetic build inputs."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Literal, Mapping

import gen_build_identity as identity
import gen_source_closure
import gen_toolchain_attestation
from hermetic_manifest import canonical_json_bytes, sha256_file, write_if_changed
import target_profile


# Identity v1's nine package-named fields remain ABI-visible in v2. Texture has
# no legacy field and is deliberately represented only by the package-set root.
PACKAGE_IDENTITY_FIELDS = {
    "route": "route_artifact_hash",
    "input": "input_artifact_hash",
    "camera": "camera_artifact_hash",
    "cart": "cart_profile_hash",
    "level": "scene_package_hash",
    "shared-data": "scene_dependency_set_hash",
    "actor": "actor_package_hash",
    "animation": "animation_package_hash",
    "audio": "audio_package_hash",
}


def _integer_config(config: Mapping[str, int]) -> dict[str, int]:
    required = set(identity.SCALAR_FIELDS) | set(identity.COMPILER_CONFIG_FIELDS)
    required.update("features." + feature for feature in identity.FEATURE_BITS)
    if set(config) != required:
        missing = sorted(required - set(config))
        extra = sorted(set(config) - required)
        raise ValueError(f"identity bootstrap config mismatch; missing={missing}, extra={extra}")
    if any(type(value) is not int for value in config.values()):
        raise ValueError("identity bootstrap config values must be integers")
    return dict(config)


def _input_path(root: Path, path: Path) -> Path:
    return path.resolve() if path.is_absolute() else (root / path).resolve()


def _descriptor(path: Path, label: str, expected_sha256: str | None = None) -> dict[str, str]:
    if not path.is_file():
        raise ValueError(f"{label} is not a file: {path}")
    digest = sha256_file(path)
    if expected_sha256 is not None and digest != expected_sha256:
        raise ValueError(f"{label} is stale")
    return {"path": str(path.resolve()), "sha256": digest}


def _source_closure_descriptor(root: Path, path: Path) -> dict[str, str]:
    try:
        document = gen_source_closure._load_sealed_closure(path)
        rows = gen_source_closure._sealed_rows(root, document)
    except ValueError as error:
        raise ValueError(f"source closure is stale or invalid: {error}") from error
    for row in rows.values():
        source = root / row["path"]
        if not source.is_file() or sha256_file(source) != row["sha256"]:
            raise ValueError(f"source closure input is stale: {row['path']}")
    return _descriptor(path, "source closure")


def _toolchain_descriptor(path: Path) -> dict[str, str]:
    try:
        _raw, document = gen_toolchain_attestation._load_sealed_attestation(path)
        gen_toolchain_attestation._validate_attestation_document(document)
    except ValueError as error:
        raise ValueError(f"toolchain attestation is stale or invalid: {error}") from error
    return _descriptor(path, "toolchain attestation")


def _canonical_object(path: Path, label: str) -> tuple[bytes, dict[str, Any]]:
    if not path.is_file():
        raise ValueError(f"{label} is not a file: {path}")
    raw = path.read_bytes()
    try:
        document = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(f"{label} is stale or invalid") from error
    if not isinstance(document, dict) or canonical_json_bytes(document) != raw:
        raise ValueError(f"{label} is stale or invalid")
    return raw, document


def _validate_resolved_profile(path: Path, expected: bytes, values: Mapping[str, int]) -> None:
    raw, document = _canonical_object(path, "resolved target profile")
    if raw != expected:
        raise ValueError("resolved target profile is stale")
    expected_keys = {
        "schema", "profile_id", "release_enabled", "effective_config",
        "package_manifests", "output_names",
    }
    if (set(document) != expected_keys
            or document.get("schema") != "sm64-saturn-resolved-target-profile-v1"
            or document.get("effective_config") != dict(values)):
        raise ValueError("resolved target profile is stale or invalid")


def _validate_class_manifest(path: Path, package_class: str, expected_sha256: str) -> None:
    _raw, document = _canonical_object(path, f"{package_class} package class manifest")
    if (set(document) != {"schema", "package_class", "packages"}
            or document.get("schema") != "sm64-saturn-package-class-manifest-v1"
            or document.get("package_class") != package_class
            or not isinstance(document.get("packages"), list)):
        raise ValueError(f"{package_class} package class manifest is stale or invalid")
    _descriptor(path, f"{package_class} package class manifest", expected_sha256)


def _validate_package_set(
    path: Path,
    expected: bytes,
    class_descriptors: Mapping[str, Mapping[str, str]],
) -> None:
    raw, document = _canonical_object(path, "package set")
    if raw != expected:
        raise ValueError("package set is stale")
    hashes = document.get("package_class_hashes")
    if (set(document) != {"schema", "profile_id", "packages", "package_class_hashes"}
            or document.get("schema") != "sm64-saturn-package-set-v2"
            or not isinstance(document.get("packages"), list)
            or not isinstance(hashes, dict)
            or set(hashes) != set(target_profile.PACKAGE_CLASSES)):
        raise ValueError("package set is stale or invalid")
    for package_class, descriptor in class_descriptors.items():
        if hashes[package_class] != descriptor["sha256"]:
            raise ValueError(f"package set {package_class} class hash is stale")


def _revalidate_descriptors(spec: Mapping[str, Any]) -> None:
    descriptors = list(spec["artifacts"].items())
    descriptors.extend((field, spec[field]) for field in identity.V2_ROOT_DESCRIPTOR_FIELDS)
    for label, descriptor in descriptors:
        current = _descriptor(Path(descriptor["path"]), label)
        if current["sha256"] != descriptor["sha256"]:
            raise ValueError(f"{label} descriptor is stale")


def write_spec(
    root: Path,
    output: Path,
    config: Mapping[str, int],
    profile_path: Path,
    source_closure_path: Path,
    toolchain_attestation_path: Path,
    mode: Literal["development", "release"],
) -> None:
    """Atomically publish identity v2 after every consumed descriptor validates."""
    root = root.resolve()
    output = output.resolve()
    values = _integer_config(config)
    source_closure_path = _input_path(root, source_closure_path)
    toolchain_attestation_path = _input_path(root, toolchain_attestation_path)

    source_descriptor = _source_closure_descriptor(root, source_closure_path)
    toolchain_descriptor = _toolchain_descriptor(toolchain_attestation_path)

    profile_document, relative_profile = target_profile._profile(root, profile_path)
    if mode == "release" and not profile_document["release_enabled"]:
        raise ValueError(f"{profile_document['profile_id']} is not release-enabled")
    if profile_document["release_config"] != values:
        raise ValueError("Make-provided config must exactly match target profile release_config")
    profile_source = root / relative_profile
    profile_source_sha256 = sha256_file(profile_source)

    manifest_dir = output.parent / "saturn-package-manifests"
    resolved = target_profile.resolve_target_profile(
        root, profile_source, values, manifest_dir, mode
    )
    if sha256_file(profile_source) != profile_source_sha256:
        raise ValueError("target profile changed during identity composition")

    resolved_profile_path = output.parent / "saturn-target-profile-v1.json"
    package_set_path = output.parent / "saturn-package-set-v1.json"
    write_if_changed(resolved_profile_path, resolved.canonical)
    write_if_changed(package_set_path, resolved.package_set_canonical)

    class_descriptors: dict[str, dict[str, str]] = {}
    for package_class in target_profile.PACKAGE_CLASSES:
        path = resolved.package_class_manifests[package_class]
        expected = resolved.package_class_hashes[package_class]
        _validate_class_manifest(path, package_class, expected)
        class_descriptors[package_class] = _descriptor(
            path, f"{package_class} package class manifest", expected
        )
    _validate_resolved_profile(resolved_profile_path, resolved.canonical, values)
    _validate_package_set(package_set_path, resolved.package_set_canonical, class_descriptors)

    artifacts = {"source_hash": source_descriptor}
    artifacts.update({
        artifact_field: class_descriptors[package_class]
        for package_class, artifact_field in PACKAGE_IDENTITY_FIELDS.items()
    })
    spec = {
        "identity_version": identity.IDENTITY_V2_VERSION,
        "features": {
            name: values[f"features.{name}"] for name in identity.FEATURE_BITS
        },
        **{field: values[field] for field in identity.SCALAR_FIELDS},
        **{field: values[field] for field in identity.COMPILER_CONFIG_FIELDS},
        "artifacts": artifacts,
        "target_profile": _descriptor(
            resolved_profile_path, "resolved target profile", resolved.sha256
        ),
        "package_set": _descriptor(
            package_set_path, "package set", resolved.package_set_sha256
        ),
        "toolchain_attestation": toolchain_descriptor,
    }
    identity.build_identity(spec)
    _revalidate_descriptors(spec)
    write_if_changed(
        output,
        (json.dumps(spec, sort_keys=True, indent=2, ensure_ascii=True) + "\n").encode("ascii"),
    )


def _parse_settings(items: list[str]) -> dict[str, int]:
    values: dict[str, int] = {}
    for item in items:
        name, separator, raw = item.partition("=")
        if not separator or not name:
            raise ValueError(f"invalid --set value: {item}")
        try:
            values[name] = int(raw, 10)
        except ValueError as error:
            raise ValueError(f"invalid integer for {name}: {raw}") from error
    return values


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--profile", required=True, type=Path)
    parser.add_argument("--source-closure", required=True, type=Path)
    parser.add_argument("--toolchain-attestation", required=True, type=Path)
    parser.add_argument("--mode", choices=("development", "release"), required=True)
    parser.add_argument("--set", action="append", default=[], metavar="NAME=INTEGER")
    args = parser.parse_args()
    write_spec(
        args.root, args.output, _parse_settings(args.set), args.profile,
        args.source_closure, args.toolchain_attestation, args.mode,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
