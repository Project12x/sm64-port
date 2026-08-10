#!/usr/bin/env python3
"""Compose sourceboot identity v2 from sealed hermetic build inputs."""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Literal, Mapping, Sequence

import gen_build_identity as identity
import gen_source_closure
import gen_toolchain_attestation
from hermetic_manifest import (
    canonical_json_bytes,
    normalize_repo_path,
    write_if_changed,
)
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


@dataclass(frozen=True)
class _Snapshot:
    path: Path
    raw: bytes
    sha256: str
    document: dict[str, Any]


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


def _digest(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()


def _capture_canonical(path: Path, label: str) -> _Snapshot:
    if not path.is_file():
        raise ValueError(f"{label} is not a file: {path}")
    raw = path.read_bytes()
    try:
        document = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(f"{label} is stale or invalid") from error
    if not isinstance(document, dict) or canonical_json_bytes(document) != raw:
        raise ValueError(f"{label} is stale or invalid")
    return _Snapshot(path.resolve(), raw, _digest(raw), document)


def _source_closure_snapshot(
    root: Path, path: Path
) -> tuple[_Snapshot, dict[Path, str]]:
    snapshot = _capture_canonical(path, "source closure")
    try:
        rows = gen_source_closure._sealed_rows(root, snapshot.document)
    except (TypeError, ValueError) as error:
        raise ValueError(f"source closure is stale or invalid: {error}") from error
    selected: dict[Path, str] = {snapshot.path: snapshot.sha256}
    expected_by_path: dict[Path, str] = {}
    for row in rows.values():
        source = (root / row["path"]).resolve()
        existing = expected_by_path.get(source)
        if existing is not None and existing != row["sha256"]:
            raise ValueError(f"source closure has conflicting hashes: {row['path']}")
        expected_by_path[source] = row["sha256"]
    for source, expected in expected_by_path.items():
        if not source.is_file() or _digest(source.read_bytes()) != expected:
            raise ValueError(
                f"source closure input is stale: {source.relative_to(root).as_posix()}"
            )
        selected[source] = expected
    return snapshot, selected


def _toolchain_snapshot(path: Path) -> _Snapshot:
    snapshot = _capture_canonical(path, "toolchain attestation")
    try:
        gen_toolchain_attestation._validate_attestation_document(snapshot.document)
    except ValueError as error:
        raise ValueError(f"toolchain attestation is stale or invalid: {error}") from error
    return snapshot


def _validate_resolved_profile(
    snapshot: _Snapshot, expected: bytes, values: Mapping[str, int]
) -> None:
    if snapshot.raw != expected:
        raise ValueError("resolved target profile is stale")
    expected_keys = {
        "schema", "profile_id", "release_enabled", "effective_config",
        "package_manifests", "output_names",
    }
    document = snapshot.document
    if (set(document) != expected_keys
            or document.get("schema") != "sm64-saturn-resolved-target-profile-v1"
            or document.get("effective_config") != dict(values)):
        raise ValueError("resolved target profile is stale or invalid")


def _validate_class_manifest(
    snapshot: _Snapshot, package_class: str, expected_sha256: str
) -> None:
    document = snapshot.document
    if (set(document) != {"schema", "package_class", "packages"}
            or document.get("schema") != "sm64-saturn-package-class-manifest-v1"
            or document.get("package_class") != package_class
            or not isinstance(document.get("packages"), list)):
        raise ValueError(f"{package_class} package class manifest is stale or invalid")
    if snapshot.sha256 != expected_sha256:
        raise ValueError(f"{package_class} package class manifest is stale")


def _validate_package_set(
    snapshot: _Snapshot,
    expected: bytes,
    class_snapshots: Mapping[str, _Snapshot],
) -> None:
    if snapshot.raw != expected:
        raise ValueError("package set is stale")
    document = snapshot.document
    hashes = document.get("package_class_hashes")
    if (set(document) != {"schema", "profile_id", "packages", "package_class_hashes"}
            or document.get("schema") != "sm64-saturn-package-set-v2"
            or not isinstance(document.get("packages"), list)
            or not isinstance(hashes, dict)
            or set(hashes) != set(target_profile.PACKAGE_CLASSES)):
        raise ValueError("package set is stale or invalid")
    for package_class, class_snapshot in class_snapshots.items():
        if hashes[package_class] != class_snapshot.sha256:
            raise ValueError(f"package set {package_class} class hash is stale")


def _descriptor(root: Path, path: Path, digest: str) -> dict[str, str]:
    return {"path": normalize_repo_path(root, path), "sha256": digest}


def _spec_descriptors(spec: Mapping[str, Any]) -> list[dict[str, str]]:
    descriptors = list(spec["artifacts"].values())
    descriptors.extend(spec[field] for field in identity.V2_ROOT_DESCRIPTOR_FIELDS)
    return descriptors


def _validation_spec(
    spec: Mapping[str, Any], staged_paths: Mapping[str, Path]
) -> dict[str, Any]:
    rendered = copy.deepcopy(spec)
    for descriptor in _spec_descriptors(rendered):
        descriptor["path"] = str(staged_paths[descriptor["path"]])
    return rendered


def _verify_digest(path: Path, expected: str, label: str) -> None:
    if not path.is_file() or _digest(path.read_bytes()) != expected:
        raise ValueError(f"{label} changed during identity composition: {path}")


def _verify_package_inputs(
    root: Path,
    profile_document: Mapping[str, Any],
    class_snapshots: Mapping[str, _Snapshot],
) -> None:
    expected: dict[tuple[str, str], Mapping[str, Any]] = {}
    for package_class, snapshot in class_snapshots.items():
        for package in snapshot.document["packages"]:
            if not isinstance(package, dict):
                raise ValueError(f"{package_class} package class manifest is invalid")
            expected[(package_class, package.get("package_id"))] = package

    observed: set[tuple[str, str]] = set()
    for relative in profile_document["package_descriptors"]:
        descriptor_path = (root / relative).resolve()
        if not descriptor_path.is_file():
            raise ValueError(
                f"package descriptor changed during identity composition: {relative}"
            )
        raw = descriptor_path.read_bytes()
        try:
            descriptor = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise ValueError(
                f"package descriptor changed during identity composition: {relative}"
            ) from error
        if not isinstance(descriptor, dict):
            raise ValueError(
                f"package descriptor changed during identity composition: {relative}"
            )
        key = (descriptor.get("package_class"), descriptor.get("package_id"))
        package = expected.get(key)
        if package is None or _digest(raw) != package.get("source_descriptor_sha256"):
            raise ValueError(
                f"package descriptor changed during identity composition: {relative}"
            )
        observed.add(key)
        for item in package.get("inputs", ()):
            payload = (root / item["path"]).resolve()
            _verify_digest(payload, item["sha256"], "package payload")
    if observed != set(expected):
        raise ValueError("selected package descriptors changed during identity composition")


def _verify_snapshots(snapshots: Sequence[_Snapshot], label: str) -> None:
    for snapshot in snapshots:
        _verify_digest(snapshot.path, snapshot.sha256, label)


def _publish_transaction(publications: Sequence[tuple[Path, bytes]]) -> None:
    paths = [path.resolve() for path, _data in publications]
    if len(set(paths)) != len(paths):
        raise ValueError("identity publication has duplicate output paths")
    prior = {
        path: path.read_bytes() if path.is_file() else None
        for path in paths
    }
    try:
        for path, data in publications:
            write_if_changed(path, data)
    except BaseException as error:
        rollback_errors: list[BaseException] = []
        for path in reversed(paths):
            try:
                previous = prior[path]
                if previous is None:
                    path.unlink(missing_ok=True)
                else:
                    write_if_changed(path, previous)
            except BaseException as rollback_error:
                rollback_errors.append(rollback_error)
        if rollback_errors:
            raise RuntimeError(
                f"identity publication failed and rollback had {len(rollback_errors)} errors"
            ) from error
        raise


def write_spec(
    root: Path,
    output: Path,
    config: Mapping[str, int],
    profile_path: Path,
    source_closure_path: Path,
    toolchain_attestation_path: Path,
    mode: Literal["development", "release"],
) -> None:
    """Transactionally publish identity v2 from immutable validated snapshots."""
    root = root.resolve()
    output = output.resolve()
    normalize_repo_path(root, output)
    values = _integer_config(config)
    profile_path = _input_path(root, profile_path)
    source_closure_path = _input_path(root, source_closure_path)
    toolchain_attestation_path = _input_path(root, toolchain_attestation_path)

    source_snapshot, selected_source_inputs = _source_closure_snapshot(
        root, source_closure_path
    )
    toolchain_snapshot = _toolchain_snapshot(toolchain_attestation_path)
    profile_snapshot = _capture_canonical(profile_path, "target profile")

    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix=".saturn-identity-stage-", dir=output.parent
    ) as temporary:
        stage = Path(temporary)
        staged_profile_source = stage / "selected-target-profile.json"
        staged_source_closure = stage / "selected-source-closure.json"
        staged_toolchain = stage / "selected-toolchain-attestation.json"
        staged_profile_source.write_bytes(profile_snapshot.raw)
        staged_source_closure.write_bytes(source_snapshot.raw)
        staged_toolchain.write_bytes(toolchain_snapshot.raw)

        profile_document, _relative = target_profile._profile(
            root, staged_profile_source
        )
        if mode == "release" and not profile_document["release_enabled"]:
            raise ValueError(f"{profile_document['profile_id']} is not release-enabled")
        if profile_document["release_config"] != values:
            raise ValueError(
                "Make-provided config must exactly match target profile release_config"
            )

        staged_manifest_dir = stage / "saturn-package-manifests"
        resolved = target_profile.resolve_target_profile(
            root, staged_profile_source, values, staged_manifest_dir, mode
        )
        staged_resolved_profile = stage / "saturn-target-profile-v1.json"
        staged_package_set = stage / "saturn-package-set-v1.json"
        staged_resolved_profile.write_bytes(resolved.canonical)
        staged_package_set.write_bytes(resolved.package_set_canonical)

        class_snapshots: dict[str, _Snapshot] = {}
        for package_class in target_profile.PACKAGE_CLASSES:
            snapshot = _capture_canonical(
                resolved.package_class_manifests[package_class],
                f"{package_class} package class manifest",
            )
            _validate_class_manifest(
                snapshot, package_class, resolved.package_class_hashes[package_class]
            )
            class_snapshots[package_class] = snapshot

        resolved_profile_snapshot = _capture_canonical(
            staged_resolved_profile, "resolved target profile"
        )
        package_set_snapshot = _capture_canonical(staged_package_set, "package set")
        _validate_resolved_profile(
            resolved_profile_snapshot, resolved.canonical, values
        )
        _validate_package_set(
            package_set_snapshot, resolved.package_set_canonical, class_snapshots
        )

        final_manifest_dir = output.parent / "saturn-package-manifests"
        final_resolved_profile = output.parent / "saturn-target-profile-v1.json"
        final_package_set = output.parent / "saturn-package-set-v1.json"
        staged_paths: dict[str, Path] = {}

        source_descriptor = _descriptor(
            root, source_closure_path, source_snapshot.sha256
        )
        toolchain_descriptor = _descriptor(
            root, toolchain_attestation_path, toolchain_snapshot.sha256
        )
        staged_paths[source_descriptor["path"]] = staged_source_closure
        staged_paths[toolchain_descriptor["path"]] = staged_toolchain

        class_descriptors: dict[str, dict[str, str]] = {}
        for package_class, snapshot in class_snapshots.items():
            final_path = final_manifest_dir / snapshot.path.name
            descriptor = _descriptor(root, final_path, snapshot.sha256)
            class_descriptors[package_class] = descriptor
            staged_paths[descriptor["path"]] = snapshot.path

        target_profile_descriptor = _descriptor(
            root, final_resolved_profile, resolved_profile_snapshot.sha256
        )
        package_set_descriptor = _descriptor(
            root, final_package_set, package_set_snapshot.sha256
        )
        staged_paths[target_profile_descriptor["path"]] = staged_resolved_profile
        staged_paths[package_set_descriptor["path"]] = staged_package_set

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
            "target_profile": target_profile_descriptor,
            "package_set": package_set_descriptor,
            "toolchain_attestation": toolchain_descriptor,
        }
        spec_bytes = canonical_json_bytes(spec)
        identity.build_identity(_validation_spec(spec, staged_paths))

        _verify_package_inputs(root, profile_document, class_snapshots)
        for path, expected in selected_source_inputs.items():
            _verify_digest(path, expected, "source closure input")
        _verify_digest(
            toolchain_snapshot.path, toolchain_snapshot.sha256,
            "toolchain attestation",
        )
        _verify_digest(profile_snapshot.path, profile_snapshot.sha256, "target profile")
        _verify_snapshots(
            (
                source_snapshot,
                toolchain_snapshot,
                resolved_profile_snapshot,
                package_set_snapshot,
                *class_snapshots.values(),
            ),
            "staged identity input",
        )

        publications: list[tuple[Path, bytes]] = [
            *(
                (final_manifest_dir / snapshot.path.name, snapshot.raw)
                for snapshot in class_snapshots.values()
            ),
            (final_resolved_profile, resolved_profile_snapshot.raw),
            (final_package_set, package_set_snapshot.raw),
            (output, spec_bytes),
        ]
        _publish_transaction(publications)


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
