#!/usr/bin/env python3
"""Stage one exact Saturn release through a private atomic namespace."""

from __future__ import annotations

import argparse
import ctypes
import errno
import os
import shutil
import stat
import sys
import uuid
import warnings
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from release_manifest import (
    DirectoryNamespaceGuard,
    MANIFEST_NAME,
    _file_identity,
    _is_reparse,
    remove_empty_directory_by_identity,
    verify_release_manifest,
)


FileIdentity = tuple[int, int]
_PLATFORM_SCOPE = (
    "Atomic publication supports Windows exclusive rename, Linux "
    "renameat2(RENAME_NOREPLACE), and macOS/BSD "
    "renameatx_np(RENAME_EXCL) when libc exports that directory-relative API; "
    "unsupported hosts fail before staging mutation."
)


@dataclass(frozen=True)
class _AtomicRenameAdapter:
    name: str
    function: Any | None
    flag: int


def _exclusive_flags() -> int:
    return (
        os.O_WRONLY
        | os.O_CREAT
        | os.O_EXCL
        | getattr(os, "O_BINARY", 0)
        | getattr(os, "O_NOFOLLOW", 0)
    )


def _copy_new(source: Path, target: Path) -> FileIdentity:
    """Copy to a securely opened new leaf; retain partial state on failure."""
    with DirectoryNamespaceGuard(target.parent) as namespace:
        descriptor = namespace.open_child(target.name, _exclusive_flags())
        try:
            with source.open("rb") as input_stream, os.fdopen(
                descriptor, "wb", closefd=False
            ) as output_stream:
                shutil.copyfileobj(input_stream, output_stream, length=1024 * 1024)
                output_stream.flush()
            opened = os.fstat(descriptor)
            current = namespace.lstat_child(target.name)
            namespace.require_current()
            if (
                _file_identity(opened) != _file_identity(current)
                or not stat.S_ISREG(current.st_mode)
                or _is_reparse(current)
            ):
                raise ValueError(f"staging target was replaced while copying: {target}")
            return _file_identity(opened)
        finally:
            os.close(descriptor)


def _write_new_bytes(data: bytes, target: Path) -> FileIdentity:
    with DirectoryNamespaceGuard(target.parent) as namespace:
        descriptor = namespace.open_child(target.name, _exclusive_flags())
        try:
            with os.fdopen(descriptor, "wb", closefd=False) as output_stream:
                output_stream.write(data)
                output_stream.flush()
            opened = os.fstat(descriptor)
            current = namespace.lstat_child(target.name)
            namespace.require_current()
            if (
                _file_identity(opened) != _file_identity(current)
                or not stat.S_ISREG(current.st_mode)
                or _is_reparse(current)
            ):
                raise ValueError(f"staging target was replaced while writing: {target}")
            return _file_identity(opened)
        finally:
            os.close(descriptor)


def _ensure_private_parent(root: Path, parent: Path) -> None:
    current = root
    for part in parent.relative_to(root).parts:
        with DirectoryNamespaceGuard(current) as namespace:
            try:
                metadata = namespace.lstat_child(part)
            except FileNotFoundError:
                namespace.mkdir_child(part)
                metadata = namespace.lstat_child(part)
            if (
                not stat.S_ISDIR(metadata.st_mode)
                or stat.S_ISLNK(metadata.st_mode)
                or _is_reparse(metadata)
            ):
                raise ValueError(f"private staging parent is not a real directory: {current / part}")
            namespace.require_current()
        current /= part


def _unique_name(kind: str, destination_name: str) -> str:
    return f".sm64-saturn-{kind}-{destination_name}-{uuid.uuid4().hex}"


def _make_private_tree(namespace: DirectoryNamespaceGuard, destination_name: str) -> str:
    for _attempt in range(32):
        name = _unique_name("private", destination_name)
        try:
            namespace.mkdir_child(name)
            return name
        except FileExistsError:
            continue
    raise RuntimeError("could not allocate a unique private staging directory")


def _resolve_atomic_rename_adapter(
    platform_name: str | None = None, libc: Any | None = None
) -> _AtomicRenameAdapter:
    platform_name = sys.platform if platform_name is None else platform_name
    if platform_name == "win32":
        return _AtomicRenameAdapter("windows-exclusive-rename", None, 0)
    if platform_name.startswith("linux"):
        symbol = "renameat2"
        flag = 1  # RENAME_NOREPLACE
    elif platform_name.startswith(
        ("darwin", "freebsd", "openbsd", "netbsd", "dragonfly")
    ):
        symbol = "renameatx_np"
        flag = 4  # RENAME_EXCL
    else:
        raise RuntimeError(
            f"atomic exclusive rename is unsupported on {platform_name}; "
            "no staging namespace was mutated"
        )
    libc = ctypes.CDLL(None, use_errno=True) if libc is None else libc
    function = getattr(libc, symbol, None)
    if function is None:
        raise RuntimeError(
            f"atomic exclusive rename is unsupported on {platform_name}; "
            f"libc does not export directory-relative {symbol}; "
            "no staging namespace was mutated"
        )
    function.argtypes = (
        ctypes.c_int,
        ctypes.c_char_p,
        ctypes.c_int,
        ctypes.c_char_p,
        ctypes.c_uint,
    )
    function.restype = ctypes.c_int
    return _AtomicRenameAdapter(f"{symbol}({flag})", function, flag)


def _rename_noreplace(
    namespace: DirectoryNamespaceGuard,
    source: str,
    target: str,
    adapter: _AtomicRenameAdapter,
) -> None:
    namespace.require_current()
    if adapter.function is None:
        namespace.rename_child(source, target)
    else:
        if namespace._fd is None:
            raise RuntimeError(
                f"{adapter.name} requires a directory-relative namespace descriptor"
            )
        result = adapter.function(
            namespace._fd,
            os.fsencode(source),
            namespace._fd,
            os.fsencode(target),
            adapter.flag,
        )
        if result:
            error = ctypes.get_errno()
            if error == errno.EEXIST:
                raise FileExistsError(error, os.strerror(error), target)
            raise OSError(error, os.strerror(error), target)
    namespace.require_current()


def _quarantine_child(
    namespace: DirectoryNamespaceGuard,
    child: str,
    destination_name: str,
    quarantines: list[Path],
    adapter: _AtomicRenameAdapter,
) -> Path:
    for _attempt in range(32):
        quarantine_name = _unique_name("quarantine", destination_name)
        try:
            _rename_noreplace(namespace, child, quarantine_name, adapter)
            quarantine = namespace.path / quarantine_name
            quarantines.append(quarantine)
            return quarantine
        except FileExistsError:
            continue
    raise RuntimeError(f"could not quarantine contaminated namespace: {child}")


def _child_metadata(
    namespace: DirectoryNamespaceGuard, name: str
) -> os.stat_result | None:
    try:
        return namespace.lstat_child(name)
    except FileNotFoundError:
        return None


def _is_empty_real_directory(path: Path, metadata: os.stat_result) -> bool:
    return (
        stat.S_ISDIR(metadata.st_mode)
        and not stat.S_ISLNK(metadata.st_mode)
        and not _is_reparse(metadata)
        and not any(path.iterdir())
    )


def _restore_requested_state(
    namespace: DirectoryNamespaceGuard,
    destination_name: str,
    preexisted: bool,
    quarantines: list[Path],
    adapter: _AtomicRenameAdapter,
) -> None:
    for _attempt in range(32):
        if _child_metadata(namespace, destination_name) is not None:
            _quarantine_child(
                namespace, destination_name, destination_name, quarantines, adapter
            )
            continue
        if not preexisted:
            return
        try:
            namespace.mkdir_child(destination_name)
            return
        except FileExistsError:
            continue
    raise RuntimeError("could not restore the requested destination namespace")


def _publish_private_tree(
    namespace: DirectoryNamespaceGuard,
    private_name: str,
    destination_name: str,
    initial_identity: FileIdentity | None,
    quarantines: list[Path],
    adapter: _AtomicRenameAdapter,
) -> Path | None:
    initial_backup: Path | None = None
    if initial_identity is not None:
        moved = _quarantine_child(
            namespace, destination_name, destination_name, quarantines, adapter
        )
        initial_backup = moved
        moved_metadata = moved.lstat()
        if (
            _file_identity(moved_metadata) != initial_identity
            or not _is_empty_real_directory(moved, moved_metadata)
        ):
            _restore_requested_state(
                namespace, destination_name, True, quarantines, adapter
            )
            raise ValueError(
                f"release destination was replaced or contaminated; retained at {moved}"
            )
    try:
        _rename_noreplace(namespace, private_name, destination_name, adapter)
    except OSError as error:
        if error.errno not in (errno.EEXIST, errno.EACCES, errno.ENOTEMPTY):
            raise
        if _child_metadata(namespace, destination_name) is not None:
            quarantine = _quarantine_child(
                namespace, destination_name, destination_name, quarantines, adapter
            )
            _restore_requested_state(
                namespace,
                destination_name,
                initial_identity is not None,
                quarantines,
                adapter,
            )
            raise ValueError(
                f"release destination was concurrently contaminated; retained at {quarantine}"
            ) from error
        raise
    return initial_backup


def _note_quarantines(error: BaseException, quarantines: list[Path]) -> None:
    if quarantines:
        error.add_note(
            "staging quarantine retained: "
            + "; ".join(str(path) for path in quarantines)
        )


def stage_release(manifest: Path, destination: Path) -> Path:
    """Verify, privately assemble, exactly check, then atomically publish."""
    verified = verify_release_manifest(manifest)
    destination = destination.absolute()
    quarantines: list[Path] = []
    private_name: str | None = None
    published = False
    try:
        adapter = _resolve_atomic_rename_adapter()
        with DirectoryNamespaceGuard(destination.parent) as namespace:
            initial = _child_metadata(namespace, destination.name)
            if initial is None:
                initial_identity = None
            else:
                if not _is_empty_real_directory(destination, initial):
                    raise ValueError(f"release destination is not empty: {destination}")
                initial_identity = _file_identity(initial)

            private_name = _make_private_tree(namespace, destination.name)
            private_root = namespace.path / private_name
            try:
                for name, source in verified.snapshot_outputs.items():
                    relative = Path(verified.document["outputs"][name]["path"])
                    target = private_root / relative
                    _ensure_private_parent(private_root, target.parent)
                    _copy_new(source, target)
                _write_new_bytes(
                    verified.manifest_bytes, private_root / MANIFEST_NAME
                )
                private_verification = verify_release_manifest(
                    private_root / MANIFEST_NAME, exact_inventory=True
                )
                private_verification.close()
                initial_backup = _publish_private_tree(
                    namespace,
                    private_name,
                    destination.name,
                    initial_identity,
                    quarantines,
                    adapter,
                )
                published = True
                private_name = None
                with DirectoryNamespaceGuard(destination) as published_namespace:
                    staged_verification = verify_release_manifest(
                        destination / MANIFEST_NAME, exact_inventory=True
                    )
                    staged_verification.close()
                    published_namespace.require_current()
                if initial_backup is not None:
                    assert initial_identity is not None
                    if remove_empty_directory_by_identity(
                        initial_backup, initial_identity
                    ):
                        quarantines.remove(initial_backup)
                    else:
                        warnings.warn(
                            "staging retained proven empty destination backup at "
                            f"{initial_backup}; this platform lacks safe "
                            "identity-conditional opened-object directory deletion",
                            RuntimeWarning,
                        )
                return destination
            except BaseException as error:
                if published and _child_metadata(namespace, destination.name) is not None:
                    _quarantine_child(
                        namespace,
                        destination.name,
                        destination.name,
                        quarantines,
                        adapter,
                    )
                    published = False
                elif private_name is not None and _child_metadata(namespace, private_name) is not None:
                    _quarantine_child(
                        namespace,
                        private_name,
                        destination.name,
                        quarantines,
                        adapter,
                    )
                    private_name = None
                _restore_requested_state(
                    namespace,
                    destination.name,
                    initial_identity is not None,
                    quarantines,
                    adapter,
                )
                _note_quarantines(error, quarantines)
                raise
    finally:
        verified.close()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, epilog=_PLATFORM_SCOPE)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--destination", type=Path, required=True)
    args = parser.parse_args(argv)
    print(stage_release(args.manifest, args.destination))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
