#!/usr/bin/env python3
"""Seal one immutable native-math audit-v4 contract from unsealed evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import stat
from pathlib import Path
from typing import Any

from hermetic_manifest import canonical_json_bytes
from path_identity import reject_output_input_aliases
from release_manifest import (
    DirectoryNamespaceGuard,
    _file_identity,
    _is_reparse,
    verify_release_manifest,
)
import stage_saturn_release as staging


MEASUREMENT_SCHEMA = "sm64-saturn-native-math-measurement-v1"
MEASUREMENT_STATUS = "measured-unsealed"
EXPECTED_ROOT = "_game_loop_one_iteration"
FORBIDDEN_CALLERS = ("_atan2_lookup", "_atan2s")
MEASUREMENT_FIELDS = {
    "schema", "status", "root", "total", "callers", "elf_sha256",
    "release_manifest_sha256",
}


def _load_measurement(path: Path) -> dict[str, Any]:
    raw = path.read_bytes()
    try:
        document = json.loads(raw)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError("measurement is not canonical JSON") from error
    if not isinstance(document, dict) or canonical_json_bytes(document) != raw:
        raise ValueError("measurement is not canonical JSON")
    if set(document) != MEASUREMENT_FIELDS or document.get("schema") != MEASUREMENT_SCHEMA:
        raise ValueError("measurement schema or keys are invalid")
    if document.get("status") != MEASUREMENT_STATUS:
        raise ValueError("measurement status must be measured-unsealed")
    if document.get("root") != EXPECTED_ROOT:
        raise ValueError("measurement root is invalid")
    if type(document.get("total")) is not int or document["total"] < 0:
        raise ValueError("measurement total is invalid")
    callers = document.get("callers")
    if (
        not isinstance(callers, list)
        or any(not isinstance(caller, str) or not caller for caller in callers)
        or callers != sorted(set(callers))
    ):
        raise ValueError("measurement callers are invalid")
    for field in ("elf_sha256", "release_manifest_sha256"):
        value = document.get(field)
        if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value
        ):
            raise ValueError(f"measurement {field} is invalid")
    return document


def _render_contract(
    measurement: dict[str, Any], release: dict[str, Any], manifest_sha256: str
) -> bytes:
    return (
        "AUDIT_CONTRACT_VERSION 4\n"
        f"EXPECTED_ROOT {measurement['root']}\n"
        f"EXPECTED_TOTAL {measurement['total']}\n"
        f"EXPECTED_RELEASE_MANIFEST_SHA256 {manifest_sha256}\n"
        f"EXPECTED_IDENTITY_SHA256 {release['identity_sha256']}\n"
        f"EXPECTED_EFFECTIVE_CONFIG_SHA256 {release['effective_config_sha256']}\n"
        f"EXPECTED_TARGET_PROFILE_SHA256 {release['target_profile_sha256']}\n"
        f"EXPECTED_ELF_SHA256 {release['outputs']['elf']['sha256']}\n"
        "FORBIDDEN_CALLER _atan2_lookup\n"
        "FORBIDDEN_CALLER _atan2s\n"
    ).encode("ascii")


def _fsync_directory(namespace: DirectoryNamespaceGuard) -> None:
    if namespace._fd is not None:
        os.fsync(namespace._fd)


def _write_exclusive(path: Path, raw: bytes) -> None:
    """Privately write, durably flush, then atomically publish one new file."""
    path = path.absolute()
    adapter = staging._resolve_atomic_rename_adapter()
    private_name: str | None = None
    try:
        with DirectoryNamespaceGuard(path.parent) as namespace:
            try:
                namespace.lstat_child(path.name)
            except FileNotFoundError:
                pass
            else:
                raise ValueError(f"refusing to overwrite audit contract: {path}")
            for _attempt in range(32):
                candidate = staging._unique_name("private", path.name)
                try:
                    descriptor = namespace.open_child(
                        candidate, staging._exclusive_flags(), 0o644
                    )
                except FileExistsError:
                    continue
                private_name = candidate
                break
            else:
                raise RuntimeError("could not allocate private audit contract")
            try:
                with os.fdopen(descriptor, "wb") as stream:
                    written = stream.write(raw)
                    if written != len(raw):
                        raise OSError(
                            f"short private audit contract write: {written}/{len(raw)}"
                        )
                    stream.flush()
                    os.fsync(stream.fileno())
                    opened = os.fstat(stream.fileno())
                current = namespace.lstat_child(private_name)
                namespace.require_current()
                if (
                    _file_identity(opened) != _file_identity(current)
                    or not stat.S_ISREG(current.st_mode)
                    or stat.S_ISLNK(current.st_mode)
                    or _is_reparse(current)
                    or current.st_nlink != 1
                    or current.st_size != len(raw)
                ):
                    raise ValueError(
                        "private audit contract was replaced, aliased, or truncated"
                    )
                staging._rename_noreplace(
                    namespace, private_name, path.name, adapter
                )
                private_name = None
                _fsync_directory(namespace)
            except BaseException as error:
                if private_name is not None:
                    error.add_note(
                        "private audit publication state retained without cleanup: "
                        f"{namespace.path / private_name}"
                    )
                raise
    except FileExistsError:
        raise


def seal_v4_contract(
    measurement_path: Path, release_manifest_path: Path, output: Path
) -> bytes:
    """Verify unsealed evidence and publish a new canonical v4 contract once."""
    reject_output_input_aliases(
        output,
        (("measurement", measurement_path), ("release manifest", release_manifest_path)),
    )
    measurement = _load_measurement(measurement_path)
    with verify_release_manifest(release_manifest_path) as verified:
        release = verified.document
        if release["identity_version"] != 2:
            raise ValueError("release manifest identity version 2 is required")
        if measurement["release_manifest_sha256"] != verified.manifest_sha256:
            raise ValueError("measurement release manifest differs from verified manifest")
        if measurement["elf_sha256"] != release["outputs"]["elf"]["sha256"]:
            raise ValueError("measurement ELF differs from release manifest")
        for forbidden in FORBIDDEN_CALLERS:
            if forbidden in measurement["callers"]:
                raise ValueError(f"forbidden caller remains: {forbidden}")
        raw = _render_contract(measurement, release, verified.manifest_sha256)
    _write_exclusive(output, raw)
    return raw


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--measurement", type=Path, required=True)
    parser.add_argument("--release-manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        raw = seal_v4_contract(args.measurement, args.release_manifest, args.output)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(hashlib.sha256(raw).hexdigest())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
