#!/usr/bin/env python3
"""Stage only the verified files from one exact Saturn release manifest."""

from __future__ import annotations

import argparse
import os
import shutil
import stat
from pathlib import Path

from release_manifest import MANIFEST_NAME, verify_release_manifest


FileIdentity = tuple[int, int]


def _path_identity(path: Path) -> FileIdentity:
    metadata = path.lstat()
    return metadata.st_dev, metadata.st_ino


def _remove_owned_file(path: Path, identity: FileIdentity) -> None:
    metadata = path.lstat()
    if stat.S_ISLNK(metadata.st_mode) or (metadata.st_dev, metadata.st_ino) != identity:
        raise ValueError("owned staging path was replaced")
    path.unlink()


def _require_owned_directory(path: Path, identity: FileIdentity) -> None:
    if path.is_symlink() or _path_identity(path) != identity:
        raise ValueError(f"staging directory was replaced: {path}")


def _copy_new(source: Path, target: Path) -> FileIdentity:
    identity: FileIdentity | None = None
    try:
        with source.open("rb") as input_stream, target.open("xb") as output_stream:
            metadata = os.fstat(output_stream.fileno())
            identity = metadata.st_dev, metadata.st_ino
            shutil.copyfileobj(input_stream, output_stream, length=1024 * 1024)
    except BaseException:
        if identity is not None:
            try:
                _remove_owned_file(target, identity)
            except (FileNotFoundError, ValueError):
                pass
        raise
    if _path_identity(target) != identity or target.is_symlink():
        raise ValueError(f"staging target was replaced while copying: {target}")
    return identity


def _write_new_bytes(data: bytes, target: Path) -> FileIdentity:
    identity: FileIdentity | None = None
    try:
        with target.open("xb") as output_stream:
            metadata = os.fstat(output_stream.fileno())
            identity = metadata.st_dev, metadata.st_ino
            output_stream.write(data)
    except BaseException:
        if identity is not None:
            try:
                _remove_owned_file(target, identity)
            except (FileNotFoundError, ValueError):
                pass
        raise
    if _path_identity(target) != identity or target.is_symlink():
        raise ValueError(f"staging target was replaced while writing: {target}")
    return identity


def _make_owned_parents(
    destination: Path,
    parent: Path,
    created_directories: list[tuple[Path, FileIdentity]],
    destination_identity: FileIdentity,
) -> None:
    _require_owned_directory(destination, destination_identity)
    missing: list[Path] = []
    current = parent
    while current != destination:
        if current.exists():
            if current.is_symlink() or not current.is_dir():
                raise ValueError(f"release destination path is not a directory: {current}")
            break
        if current.is_symlink():
            raise ValueError(f"release destination path is a symlink: {current}")
        missing.append(current)
        current = current.parent
    for directory in reversed(missing):
        directory.mkdir()
        created_directories.append((directory, _path_identity(directory)))
    _require_owned_directory(destination, destination_identity)
    for directory, identity in created_directories:
        _require_owned_directory(directory, identity)


def _rollback(
    created_files: list[tuple[Path, FileIdentity]],
    created_directories: list[tuple[Path, FileIdentity]],
    destination: Path,
    destination_identity: FileIdentity,
    remove_destination: bool,
) -> list[str]:
    failures: list[str] = []
    for path, identity in reversed(created_files):
        try:
            _remove_owned_file(path, identity)
        except FileNotFoundError:
            pass
        except BaseException as error:
            failures.append(f"remove {path}: {error}")
    for path, identity in reversed(created_directories):
        try:
            if path.is_symlink() or _path_identity(path) != identity:
                raise ValueError("owned staging directory was replaced")
            path.rmdir()
        except FileNotFoundError:
            pass
        except BaseException as error:
            failures.append(f"remove directory {path}: {error}")
    if remove_destination:
        try:
            if destination.is_symlink() or _path_identity(destination) != destination_identity:
                raise ValueError("owned staging destination was replaced")
            destination.rmdir()
        except FileNotFoundError:
            pass
        except BaseException as error:
            failures.append(f"remove destination {destination}: {error}")
    return failures


def stage_release(manifest: Path, destination: Path) -> Path:
    """Verify all sources, then copy into a missing or empty destination."""
    verified = verify_release_manifest(manifest)
    destination = destination.absolute()
    if destination.is_symlink():
        verified.close()
        raise ValueError(f"release destination is a symlink: {destination}")
    destination_preexisted = destination.exists()
    if destination.exists():
        if not destination.is_dir() or any(destination.iterdir()):
            verified.close()
            raise ValueError(f"release destination is not empty: {destination}")
    else:
        if destination.parent.is_symlink() or not destination.parent.is_dir():
            verified.close()
            raise ValueError(
                f"release destination parent is not a real directory: {destination.parent}"
            )
        destination.mkdir()
    destination_identity = _path_identity(destination)
    created_files: list[tuple[Path, FileIdentity]] = []
    created_directories: list[tuple[Path, FileIdentity]] = []
    try:
        for name, source in verified.snapshot_outputs.items():
            relative = Path(verified.document["outputs"][name]["path"])
            target = destination / relative
            _make_owned_parents(
                destination, target.parent, created_directories, destination_identity
            )
            if target.exists() or target.is_symlink():
                raise ValueError(f"release destination target already exists: {target}")
            identity = _copy_new(source, target)
            created_files.append((target, identity))
            _require_owned_directory(destination, destination_identity)
            for directory, owned_identity in created_directories:
                _require_owned_directory(directory, owned_identity)
        manifest_target = destination / MANIFEST_NAME
        if manifest_target.exists() or manifest_target.is_symlink():
            raise ValueError(
                f"release destination target already exists: {manifest_target}"
            )
        manifest_identity = _write_new_bytes(verified.manifest_bytes, manifest_target)
        created_files.append((manifest_target, manifest_identity))
        _require_owned_directory(destination, destination_identity)
        staged_verification = verify_release_manifest(manifest_target)
        staged_verification.close()
        _require_owned_directory(destination, destination_identity)
        return destination
    except BaseException as error:
        failures = _rollback(
            created_files,
            created_directories,
            destination,
            destination_identity,
            not destination_preexisted,
        )
        if failures:
            error.add_note("staging rollback failures: " + "; ".join(failures))
        raise
    finally:
        verified.close()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--destination", type=Path, required=True)
    args = parser.parse_args(argv)
    print(stage_release(args.manifest, args.destination))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
