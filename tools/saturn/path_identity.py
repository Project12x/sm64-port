"""Portable path-alias checks for immutable inputs and output leaves."""

from __future__ import annotations

import os
import stat
import unicodedata
import uuid
import warnings
from pathlib import Path
from typing import Callable, Iterable

from release_manifest import DirectoryNamespaceGuard, _file_identity, _is_reparse

if os.name == "nt":
    import ctypes
    import msvcrt
    from ctypes import wintypes

    _KERNEL32 = ctypes.WinDLL("kernel32", use_last_error=True)
    _KERNEL32.CreateFileW.argtypes = (
        wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
        wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE,
    )
    _KERNEL32.CreateFileW.restype = wintypes.HANDLE
    _KERNEL32.CloseHandle.argtypes = (wintypes.HANDLE,)
    _KERNEL32.CloseHandle.restype = wintypes.BOOL
    _KERNEL32.SetFileInformationByHandle.argtypes = (
        wintypes.HANDLE, ctypes.c_int, wintypes.LPVOID, wintypes.DWORD,
    )
    _KERNEL32.SetFileInformationByHandle.restype = wintypes.BOOL
    _INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value

    class _FileRenameInformation(ctypes.Structure):
        _fields_ = (
            ("replace_if_exists", wintypes.BOOLEAN),
            ("root_directory", wintypes.HANDLE),
            ("file_name_length", wintypes.DWORD),
            ("file_name", wintypes.WCHAR * 1),
        )


def _portable_path_key(path: Path) -> str:
    resolved = path.resolve(strict=False)
    normalized = unicodedata.normalize("NFC", os.path.normpath(str(resolved)))
    return normalized.replace("\\", "/").casefold()


def _identity(path: Path) -> tuple[int, int] | None:
    try:
        metadata = path.stat()
    except OSError:
        return None
    return metadata.st_dev, metadata.st_ino


def reject_output_input_aliases(
    output: Path, inputs: Iterable[tuple[str, Path]]
) -> None:
    """Reject lexical, portable-spelling, symlink, and hardlink aliases."""
    if output.is_symlink():
        raise ValueError(f"output path is a symlink: {output}")
    output_key = _portable_path_key(output)
    output_identity = _identity(output)
    for label, input_path in inputs:
        if output_key == _portable_path_key(input_path):
            raise ValueError(f"output aliases {label} input: {output}")
        input_identity = _identity(input_path)
        if output_identity is not None and output_identity == input_identity:
            raise ValueError(f"output aliases {label} input: {output}")


def _private_name(target_name: str) -> str:
    return f".sm64-saturn-private-{target_name}-{uuid.uuid4().hex}"


def _open_private_file(
    namespace: DirectoryNamespaceGuard, private_name: str
) -> int:
    if os.name != "nt":
        flags = (
            os.O_WRONLY | os.O_CREAT | os.O_EXCL
            | getattr(os, "O_BINARY", 0) | getattr(os, "O_NOFOLLOW", 0)
        )
        return namespace.open_child(private_name, flags, 0o644)
    handle = _KERNEL32.CreateFileW(
        str(namespace.path / private_name),
        0x40000000 | 0x00010000 | 0x00000080,  # GENERIC_WRITE | DELETE | READ_ATTRS
        0x1 | 0x2 | 0x4,  # share read/write/delete; publication is handle-bound
        None,
        1,  # CREATE_NEW
        0x80 | 0x00200000,  # NORMAL | OPEN_REPARSE_POINT
        None,
    )
    if handle == _INVALID_HANDLE_VALUE:
        error = ctypes.get_last_error()
        if error in (80, 183):
            raise FileExistsError(error, os.strerror(error), private_name)
        raise OSError(error, os.strerror(error), private_name)
    try:
        return msvcrt.open_osfhandle(
            handle, os.O_WRONLY | getattr(os, "O_BINARY", 0)
        )
    except BaseException:
        _KERNEL32.CloseHandle(handle)
        raise


def _publish_windows_handle(
    descriptor: int, namespace: DirectoryNamespaceGuard, target_name: str
) -> None:
    encoded = str(namespace.path / target_name).encode("utf-16-le")
    offset = _FileRenameInformation.file_name.offset
    buffer = ctypes.create_string_buffer(
        offset + len(encoded) + ctypes.sizeof(wintypes.WCHAR)
    )
    information = ctypes.cast(
        buffer, ctypes.POINTER(_FileRenameInformation)
    ).contents
    information.replace_if_exists = False
    information.root_directory = None
    information.file_name_length = len(encoded)
    ctypes.memmove(ctypes.addressof(buffer) + offset, encoded, len(encoded))
    handle = msvcrt.get_osfhandle(descriptor)
    if not _KERNEL32.SetFileInformationByHandle(
        handle, 3, buffer, len(buffer)  # FileRenameInfo
    ):
        error = ctypes.get_last_error()
        if error in (80, 183):
            raise FileExistsError(error, os.strerror(error), target_name)
        raise OSError(error, os.strerror(error), target_name)


def _publish_posix_handle(
    descriptor: int, namespace: DirectoryNamespaceGuard, target_name: str
) -> None:
    assert namespace._fd is not None
    failures: list[OSError] = []
    for root in ("/proc/self/fd", "/dev/fd"):
        source = f"{root}/{descriptor}"
        if not Path(root).is_dir():
            continue
        try:
            os.link(
                source, target_name,
                dst_dir_fd=namespace._fd,
                follow_symlinks=True,
            )
            return
        except FileExistsError:
            raise
        except OSError as error:
            failures.append(error)
    detail = "; ".join(str(error) for error in failures) or "no process fd path"
    raise RuntimeError(
        "exact opened-object publication is unsupported on this host: " + detail
    )


def _publish_held_file(
    descriptor: int,
    namespace: DirectoryNamespaceGuard,
    private_name: str,
    target_name: str,
) -> None:
    """Atomically publish the exact opened object under a new final name."""
    namespace.require_current()
    if os.name == "nt":
        _publish_windows_handle(descriptor, namespace, target_name)
    else:
        _publish_posix_handle(descriptor, namespace, target_name)
    namespace.require_current()


def _fsync_directory(namespace: DirectoryNamespaceGuard) -> None:
    if namespace._fd is not None:
        os.fsync(namespace._fd)


def publish_new_bytes(
    path: Path,
    raw: bytes,
    *,
    existing_error: str | None = None,
    fsync_directory: Callable[[DirectoryNamespaceGuard], None] = _fsync_directory,
) -> None:
    """Write privately, then exclusively publish the exact held file object."""
    path = path.absolute()
    private_name: str | None = None
    descriptor: int | None = None
    with DirectoryNamespaceGuard(path.parent) as namespace:
        try:
            namespace.lstat_child(path.name)
        except FileNotFoundError:
            pass
        else:
            raise ValueError(existing_error or f"output already exists: {path}")
        for _attempt in range(32):
            candidate = _private_name(path.name)
            try:
                descriptor = _open_private_file(namespace, candidate)
            except FileExistsError:
                continue
            private_name = candidate
            break
        else:
            raise RuntimeError("could not allocate private publication file")
        assert private_name is not None and descriptor is not None
        try:
            write_descriptor = os.dup(descriptor)
            with os.fdopen(write_descriptor, "wb") as stream:
                written = stream.write(raw)
                if written != len(raw):
                    raise OSError(f"short private write: {written}/{len(raw)}")
                stream.flush()
                os.fsync(stream.fileno())
            opened = os.fstat(descriptor)
            current = namespace.lstat_child(private_name)
            namespace.require_current()
            if (
                _file_identity(opened) != _file_identity(current)
                or not stat.S_ISREG(current.st_mode)
                or stat.S_ISLNK(current.st_mode)
                or _is_reparse(current)
                or current.st_nlink != 1
                or opened.st_size != len(raw)
            ):
                raise ValueError("private publication object changed before publish")
            _publish_held_file(
                descriptor, namespace, private_name, path.name
            )
            os.close(descriptor)
            descriptor = None
            published = namespace.lstat_child(path.name)
            if (
                _file_identity(published) != _file_identity(opened)
                or not stat.S_ISREG(published.st_mode)
                or _is_reparse(published)
                or published.st_size != len(raw)
            ):
                raise RuntimeError("published output is not the exact held object")
            fsync_directory(namespace)
        except BaseException as error:
            error.add_note(
                "private publication state retained without cleanup: "
                f"{namespace.path / private_name}"
            )
            raise
        finally:
            if descriptor is not None:
                os.close(descriptor)
        try:
            retained = namespace.lstat_child(private_name)
        except FileNotFoundError:
            retained = None
        if retained is not None:
            warnings.warn(
                "exact-object publication retained private namespace state at "
                f"{namespace.path / private_name}",
                RuntimeWarning,
            )
