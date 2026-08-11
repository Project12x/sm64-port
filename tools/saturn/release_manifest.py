#!/usr/bin/env python3
"""Seal, verify, and compare exact Saturn release artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import stat
import subprocess
import tempfile
import unicodedata
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any, Literal, Mapping

if os.name == "nt":
    import ctypes
    from ctypes import wintypes

import gen_build_identity as identity
from gen_source_closure import CLASS_PRECEDENCE, verify_release_provenance
from hermetic_manifest import canonical_json_bytes, write_if_changed
from target_profile import PACKAGE_CLASSES


SCHEMA = "sm64-saturn-release-manifest-v1"
COMPARISON_SCHEMA = "sm64-saturn-release-comparison-v1"
MANIFEST_NAME = "saturn-release-manifest-v1.json"
OUTPUT_NAMES = ("elf", "source_dat", "iso", "cue")
_SHA256 = re.compile(r"[0-9a-f]{64}\Z")
_FILE_LINE = re.compile(
    r'^\s*FILE\s+"([^"]+)"\s+\S+\s*$', re.IGNORECASE | re.MULTILINE
)
_ANY_FILE_LINE = re.compile(r"^\s*FILE\b", re.IGNORECASE | re.MULTILINE)
_WINDOWS_INVALID = frozenset('<>:"|?*')
_WINDOWS_RESERVED = frozenset(
    {"con", "prn", "aux", "nul"}
    | {f"com{index}" for index in range(1, 10)}
    | {f"lpt{index}" for index in range(1, 10)}
)


@dataclass(frozen=True)
class ReleaseManifestVerification:
    document: dict[str, Any]
    manifest_sha256: str
    outputs: dict[str, Path]
    manifest_bytes: bytes
    snapshot_outputs: dict[str, Path]
    _snapshot_owner: "_SnapshotOwner"

    @property
    def snapshot_root(self) -> Path:
        return self._snapshot_owner.path

    def close(self) -> None:
        self._snapshot_owner.cleanup()

    def __enter__(self) -> "ReleaseManifestVerification":
        return self

    def __exit__(self, *_args: object) -> None:
        self.close()


class _SnapshotOwner:
    """Own one private verified snapshot without TemporaryDirectory warnings."""

    def __init__(self) -> None:
        self.path = Path(tempfile.mkdtemp(prefix="sm64-saturn-release-"))
        self._closed = False

    def cleanup(self) -> None:
        if not self._closed:
            shutil.rmtree(self.path, ignore_errors=True)
            self._closed = True

    def __del__(self) -> None:
        self.cleanup()


FileIdentity = tuple[int, int]


def _file_identity(metadata: os.stat_result) -> FileIdentity:
    return metadata.st_dev, metadata.st_ino


def _is_reparse(metadata: os.stat_result) -> bool:
    attributes = getattr(metadata, "st_file_attributes", 0)
    return bool(attributes & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400))


if os.name == "nt":
    class _ByHandleFileInformation(ctypes.Structure):
        _fields_ = [
            ("attributes", wintypes.DWORD),
            ("creation_time", wintypes.FILETIME),
            ("last_access_time", wintypes.FILETIME),
            ("last_write_time", wintypes.FILETIME),
            ("volume_serial", wintypes.DWORD),
            ("file_size_high", wintypes.DWORD),
            ("file_size_low", wintypes.DWORD),
            ("number_of_links", wintypes.DWORD),
            ("file_index_high", wintypes.DWORD),
            ("file_index_low", wintypes.DWORD),
        ]

    _KERNEL32 = ctypes.WinDLL("kernel32", use_last_error=True)
    _KERNEL32.CreateFileW.argtypes = (
        wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
        wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE,
    )
    _KERNEL32.CreateFileW.restype = wintypes.HANDLE
    _KERNEL32.GetFileInformationByHandle.argtypes = (
        wintypes.HANDLE, ctypes.POINTER(_ByHandleFileInformation),
    )
    _KERNEL32.GetFileInformationByHandle.restype = wintypes.BOOL
    _KERNEL32.CloseHandle.argtypes = (wintypes.HANDLE,)
    _KERNEL32.CloseHandle.restype = wintypes.BOOL
    _KERNEL32.SetFileInformationByHandle.argtypes = (
        wintypes.HANDLE, ctypes.c_int, wintypes.LPVOID, wintypes.DWORD,
    )
    _KERNEL32.SetFileInformationByHandle.restype = wintypes.BOOL
    _INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value

    class _FileDispositionInformation(ctypes.Structure):
        _fields_ = [("delete_file", wintypes.BOOL)]


class DirectoryNamespaceGuard:
    """Pin one real directory namespace for no-follow child operations."""

    def __init__(self, path: Path) -> None:
        self.path = path.absolute()
        self._fd: int | None = None
        self._handles: list[int] = []
        self.identity: FileIdentity
        if os.name == "nt":
            self._acquire_windows_chain()
        else:
            self._acquire_posix_chain()

    def _acquire_windows_chain(self) -> None:
        current = Path(self.path.anchor)
        parts = self.path.parts[1:]
        for index in range(len(parts) + 1):
            if index:
                current /= parts[index - 1]
            before = current.lstat()
            if (
                not stat.S_ISDIR(before.st_mode)
                or stat.S_ISLNK(before.st_mode)
                or _is_reparse(before)
                or (hasattr(current, "is_junction") and current.is_junction())
            ):
                self.close()
                raise ValueError(f"directory ancestor is a symlink, junction, or reparse point: {current}")
            handle = _KERNEL32.CreateFileW(
                str(current),
                0,  # metadata-only directory handle
                0x1 | 0x2 | (0x4 if index < len(parts) else 0),
                # Ancestors are fully identity-checked; the guarded directory
                # itself deliberately denies delete/rename while in use.
                None,
                3,  # OPEN_EXISTING
                0x02000000 | 0x00200000,  # BACKUP_SEMANTICS | OPEN_REPARSE_POINT
                None,
            )
            if handle == _INVALID_HANDLE_VALUE:
                error = ctypes.get_last_error()
                self.close()
                raise OSError(error, os.strerror(error), str(current))
            information = _ByHandleFileInformation()
            if not _KERNEL32.GetFileInformationByHandle(handle, ctypes.byref(information)):
                error = ctypes.get_last_error()
                _KERNEL32.CloseHandle(handle)
                self.close()
                raise OSError(error, os.strerror(error), str(current))
            opened_file_index = (
                information.file_index_high << 32
            ) | information.file_index_low
            if (
                information.attributes & 0x400
                or not information.attributes & 0x10
                or before.st_ino != opened_file_index
                or _file_identity(current.lstat()) != _file_identity(before)
            ):
                _KERNEL32.CloseHandle(handle)
                self.close()
                raise ValueError(f"directory ancestor changed identity: {current}")
            self._handles.append(handle)
        self.identity = _file_identity(self.path.lstat())

    def _acquire_posix_chain(self) -> None:
        flags = os.O_RDONLY | getattr(os, "O_DIRECTORY", 0)
        nofollow = getattr(os, "O_NOFOLLOW", 0)
        current_fd = os.open(self.path.anchor, flags)
        try:
            for part in self.path.parts[1:]:
                before = os.stat(part, dir_fd=current_fd, follow_symlinks=False)
                if not stat.S_ISDIR(before.st_mode) or stat.S_ISLNK(before.st_mode):
                    raise ValueError(f"directory ancestor is a symlink: {self.path}")
                next_fd = os.open(part, flags | nofollow, dir_fd=current_fd)
                opened = os.fstat(next_fd)
                if _file_identity(before) != _file_identity(opened):
                    os.close(next_fd)
                    raise ValueError(f"directory ancestor changed identity: {self.path}")
                os.close(current_fd)
                current_fd = next_fd
            self._fd = current_fd
            current_fd = -1
            self.identity = _file_identity(os.fstat(self._fd))
        finally:
            if current_fd >= 0:
                os.close(current_fd)

    def close(self) -> None:
        if self._fd is not None:
            os.close(self._fd)
            self._fd = None
        while self._handles:
            _KERNEL32.CloseHandle(self._handles.pop())

    def __enter__(self) -> "DirectoryNamespaceGuard":
        return self

    def __exit__(self, *_args: object) -> None:
        self.close()

    def require_current(self) -> None:
        try:
            current = self.path.lstat()
        except OSError as error:
            raise ValueError(f"guarded directory namespace disappeared: {self.path}") from error
        if (
            _file_identity(current) != self.identity
            or stat.S_ISLNK(current.st_mode)
            or _is_reparse(current)
        ):
            raise ValueError(f"guarded directory namespace was replaced: {self.path}")

    def lstat_child(self, name: str) -> os.stat_result:
        if self._fd is not None:
            return os.stat(name, dir_fd=self._fd, follow_symlinks=False)
        return (self.path / name).lstat()

    def open_child(self, name: str, flags: int, mode: int = 0o666) -> int:
        if self._fd is not None:
            return os.open(
                name, flags | getattr(os, "O_NOFOLLOW", 0), mode, dir_fd=self._fd
            )
        return os.open(self.path / name, flags, mode)

    def mkdir_child(self, name: str) -> None:
        if self._fd is not None:
            os.mkdir(name, dir_fd=self._fd)
        else:
            (self.path / name).mkdir()

    def rename_child(self, source: str, target: str) -> None:
        if self._fd is not None:
            os.rename(
                source, target, src_dir_fd=self._fd, dst_dir_fd=self._fd
            )
        else:
            (self.path / source).rename(self.path / target)

    def remove_child_by_identity(self, name: str, *, directory: bool) -> None:
        """Remove one validated child while this directory namespace is pinned."""
        before = self.lstat_child(name)
        identity = _file_identity(before)
        if (
            stat.S_ISLNK(before.st_mode)
            or _is_reparse(before)
            or (directory and not stat.S_ISDIR(before.st_mode))
            or (not directory and not stat.S_ISREG(before.st_mode))
        ):
            raise ValueError(f"guarded child has unsafe type: {self.path / name}")
        self.require_current()
        if os.name == "nt":
            handle = _KERNEL32.CreateFileW(
                str(self.path / name),
                0x00010000 | 0x00000080,  # DELETE | FILE_READ_ATTRIBUTES
                0x1 | 0x2 | 0x4,
                None,
                3,  # OPEN_EXISTING
                0x02000000 | 0x00200000,  # BACKUP_SEMANTICS | OPEN_REPARSE_POINT
                None,
            )
            if handle == _INVALID_HANDLE_VALUE:
                error = ctypes.get_last_error()
                raise OSError(error, os.strerror(error), str(self.path / name))
            try:
                information = _ByHandleFileInformation()
                if not _KERNEL32.GetFileInformationByHandle(
                    handle, ctypes.byref(information)
                ):
                    error = ctypes.get_last_error()
                    raise OSError(error, os.strerror(error), str(self.path / name))
                opened_file_index = (
                    information.file_index_high << 32
                ) | information.file_index_low
                opened_is_directory = bool(information.attributes & 0x10)
                if (
                    opened_file_index != identity[1]
                    or bool(information.attributes & 0x400)
                    or opened_is_directory != directory
                ):
                    raise ValueError(
                        f"guarded child changed identity: {self.path / name}"
                    )
                disposition = _FileDispositionInformation(True)
                if not _KERNEL32.SetFileInformationByHandle(
                    handle, 4, ctypes.byref(disposition), ctypes.sizeof(disposition)
                ):
                    error = ctypes.get_last_error()
                    raise OSError(error, os.strerror(error), str(self.path / name))
            finally:
                _KERNEL32.CloseHandle(handle)
        else:
            assert self._fd is not None
            operation = os.rmdir if directory else os.unlink
            if operation not in os.supports_dir_fd:
                raise RuntimeError(
                    "directory-relative cleanup is unsupported on this host"
                )
            current = self.lstat_child(name)
            if _file_identity(current) != identity:
                raise ValueError(
                    f"guarded child changed identity: {self.path / name}"
                )
            operation(name, dir_fd=self._fd)
        self.require_current()


def remove_empty_directory_by_identity(path: Path, identity: FileIdentity) -> bool:
    """Delete only the opened Windows directory object with the expected identity."""
    if os.name != "nt":
        return False
    handle = _KERNEL32.CreateFileW(
        str(path),
        0x00010000,  # DELETE
        0x1 | 0x2 | 0x4,
        None,
        3,
        0x02000000 | 0x00200000,
        None,
    )
    if handle == _INVALID_HANDLE_VALUE:
        return False
    try:
        information = _ByHandleFileInformation()
        if not _KERNEL32.GetFileInformationByHandle(handle, ctypes.byref(information)):
            return False
        opened_file_index = (
            information.file_index_high << 32
        ) | information.file_index_low
        if (
            opened_file_index != identity[1]
            or information.attributes & 0x400
            or not information.attributes & 0x10
        ):
            return False
        disposition = _FileDispositionInformation(True)
        return bool(
            _KERNEL32.SetFileInformationByHandle(
                handle, 4, ctypes.byref(disposition), ctypes.sizeof(disposition)
            )
        )
    finally:
        _KERNEL32.CloseHandle(handle)


def _portable_identifier(value: Any, label: str) -> tuple[str, str]:
    if (
        not isinstance(value, str)
        or not value
        or value != value.strip()
        or unicodedata.normalize("NFC", value) != value
        or any(ord(character) < 32 for character in value)
        or any(character in "/\\" for character in value)
    ):
        raise ValueError(f"{label} is invalid")
    return value, value.casefold()


def _source_closure_owner(value: Any) -> tuple[str, str]:
    if isinstance(value, str) and value in CLASS_PRECEDENCE:
        return value, value.casefold()
    return _portable_identifier(value, "source closure owner")


def _portable_relative_path(value: Any, label: str) -> tuple[PurePosixPath, str]:
    if not isinstance(value, str) or not value or "\\" in value:
        raise ValueError(f"{label} release path is invalid")
    if unicodedata.normalize("NFC", value) != value:
        raise ValueError(f"{label} release path is not Unicode-canonical")
    path = PurePosixPath(value)
    if path.is_absolute() or path.as_posix() != value:
        raise ValueError(f"{label} release path is not canonical")
    for part in path.parts:
        stem = part.split(".", 1)[0].casefold()
        if (
            part in ("", ".", "..")
            or part.endswith((" ", "."))
            or stem in _WINDOWS_RESERVED
            or any(character in _WINDOWS_INVALID or ord(character) < 32 for character in part)
        ):
            raise ValueError(f"{label} release path is not portable")
    return path, value.casefold()


def _reject_duplicate_casefold(values: list[tuple[str, str]], label: str) -> None:
    spellings = [value for value, _key in values]
    keys = [key for _value, key in values]
    if len(spellings) != len(set(spellings)):
        raise ValueError(f"duplicate {label}")
    if len(keys) != len(set(keys)):
        raise ValueError(f"case-colliding {label}")


def _strict_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    document: dict[str, Any] = {}
    for key, value in pairs:
        if key in document:
            raise ValueError(f"duplicate JSON object key: {key}")
        document[key] = value
    return document


def _load_canonical(path: Path, label: str) -> tuple[bytes, dict[str, Any]]:
    path = path.absolute()
    with DirectoryNamespaceGuard(path.parent) as namespace:
        try:
            before = namespace.lstat_child(path.name)
        except OSError as error:
            raise ValueError(f"{label} is not a file: {path}") from error
        if (
            not stat.S_ISREG(before.st_mode)
            or stat.S_ISLNK(before.st_mode)
            or _is_reparse(before)
        ):
            raise ValueError(f"{label} is not a regular non-symlink file: {path}")
        descriptor = namespace.open_child(path.name, os.O_RDONLY | getattr(os, "O_BINARY", 0))
        try:
            opened = os.fstat(descriptor)
            if _file_identity(opened) != _file_identity(before):
                raise ValueError(f"{label} changed identity before open: {path}")
            with os.fdopen(descriptor, "rb", closefd=False) as stream:
                raw = stream.read()
            closed = os.fstat(descriptor)
            current = namespace.lstat_child(path.name)
            namespace.require_current()
            if (
                _file_identity(opened) != _file_identity(closed)
                or _file_identity(current) != _file_identity(opened)
                or stat.S_ISLNK(current.st_mode)
                or _is_reparse(current)
                or opened.st_size != closed.st_size
                or opened.st_mtime_ns != closed.st_mtime_ns
            ):
                raise ValueError(f"{label} changed identity or contents while reading: {path}")
        finally:
            os.close(descriptor)
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
    _portable_relative_path(rendered, label)
    return rendered


def _resolve_release_path(base: Path, value: Any, label: str) -> Path:
    if base.is_symlink():
        raise ValueError("release manifest directory is a symlink")
    portable, _key = _portable_relative_path(value, label)
    requested = Path(*portable.parts)
    candidate = base / requested
    current = candidate
    while current != base:
        if current.is_symlink():
            raise ValueError(f"{label} output path uses a symlink")
        current = current.parent
    resolved = candidate.resolve()
    try:
        resolved.relative_to(base.resolve())
    except ValueError as error:
        raise ValueError(f"{label} release path escapes manifest directory") from error
    return resolved


def _reject_output_aliases(outputs: Mapping[str, Path]) -> None:
    identities: dict[tuple[int, int], str] = {}
    for name, path in outputs.items():
        if path.is_symlink() or not path.is_file():
            raise ValueError(f"{name} output is not a regular non-symlink file")
        stat = path.stat()
        key = (stat.st_dev, stat.st_ino)
        if key in identities:
            raise ValueError(f"output alias: {name} and {identities[key]}")
        identities[key] = name


def _copy_verified_snapshot(
    source: Path, target: Path, record: Mapping[str, Any], label: str
) -> tuple[int, int]:
    target.parent.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256()
    size = 0
    with source.open("rb") as input_stream, target.open("xb") as output_stream:
        opened = os.fstat(input_stream.fileno())
        for chunk in iter(lambda: input_stream.read(1024 * 1024), b""):
            output_stream.write(chunk)
            size += len(chunk)
            digest.update(chunk)
        closed = os.fstat(input_stream.fileno())
    source_metadata = source.stat()
    if (
        (opened.st_dev, opened.st_ino) != (closed.st_dev, closed.st_ino)
        or source.is_symlink()
        or not source.is_file()
        or (source_metadata.st_dev, source_metadata.st_ino) != (opened.st_dev, opened.st_ino)
    ):
        raise ValueError(f"{label} output changed identity during verification")
    if digest.hexdigest() != record["sha256"]:
        raise ValueError(
            f"{label} output SHA-256 mismatch: expected {record['sha256']}, "
            f"got {digest.hexdigest()}"
        )
    if size != record["size"]:
        raise ValueError(
            f"{label} output size mismatch: expected {record['size']}, got {size}"
        )
    return opened.st_dev, opened.st_ino


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


def _effective_config_from_profile(
    profile_config: Mapping[str, Any], values: Mapping[str, Any]
) -> dict[str, Any]:
    expected_profile_keys = {
        *(f"features.{name}" for name in identity.FEATURE_BITS),
        *identity.SCALAR_FIELDS,
        *identity.COMPILER_CONFIG_FIELDS,
    }
    if set(profile_config) != expected_profile_keys:
        raise ValueError("profile effective config keys are invalid")
    document: dict[str, Any] = {
        "schema": f"sm64-saturn-effective-config-v{values['version']}",
        "features": {
            name: profile_config[f"features.{name}"]
            for name in identity.FEATURE_BITS
        },
        **{field: profile_config[field] for field in identity.SCALAR_FIELDS},
        **{
            field: profile_config[field]
            for field in identity.COMPILER_CONFIG_FIELDS
        },
        "artifact_hashes": {
            field: values[field] for field in identity.ARTIFACT_HASH_FIELDS
        },
    }
    if values["version"] == identity.IDENTITY_V2_VERSION:
        document["identity_version"] = identity.IDENTITY_V2_VERSION
        document["root_hashes"] = {
            field: values[field] for field in identity.V2_ROOT_HASH_FIELDS.values()
        }
    return document


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
        or type(profile.get("release_enabled")) is not bool
        or not isinstance(profile.get("effective_config"), dict)
        or not isinstance(profile.get("package_manifests"), list)
        or not isinstance(profile.get("output_names"), dict)
        or set(profile["output_names"]) != set(OUTPUT_NAMES)
    ):
        raise ValueError("resolved target profile id is invalid")
    _portable_identifier(profile["profile_id"], "resolved target profile id")
    output_name_keys: list[tuple[str, str]] = []
    for name in OUTPUT_NAMES:
        value, key = _portable_identifier(
            profile["output_names"].get(name), f"profile output name {name}"
        )
        if PurePosixPath(value).name != value:
            raise ValueError(f"profile output name {name} is not a basename")
        output_name_keys.append((value, key))
    _reject_duplicate_casefold(output_name_keys, "profile output names")
    if (
        set(source_closure) != {"schema", "inputs"}
        or source_closure.get("schema") != "sm64-saturn-source-closure-v2"
        or not isinstance(source_closure.get("inputs"), list)
    ):
        raise ValueError("source closure schema is invalid")
    closure_paths: list[tuple[str, str]] = []
    for row in source_closure["inputs"]:
        if (
            not isinstance(row, dict)
            or set(row) != {"path", "sha256", "class", "owners"}
            or _SHA256.fullmatch(str(row.get("sha256"))) is None
            or row.get("class") not in CLASS_PRECEDENCE
            or not isinstance(row.get("owners"), list)
            or not row["owners"]
            or row["owners"] != sorted(row["owners"])
        ):
            raise ValueError("source closure record schema is invalid")
        path, key = _portable_relative_path(row.get("path"), "source closure")
        closure_paths.append((path.as_posix(), key))
        owners = [_source_closure_owner(owner) for owner in row["owners"]]
        _reject_duplicate_casefold(owners, "source closure owners")
    _reject_duplicate_casefold(closure_paths, "source closure paths")
    if source_closure["inputs"] != sorted(
        source_closure["inputs"],
        key=lambda row: (row["path"].encode("utf-8"), row["class"].encode("ascii")),
    ):
        raise ValueError("source closure records are not sorted")
    if (
        set(package_set) != {
            "schema", "profile_id", "packages", "package_class_hashes"
        }
        or package_set.get("schema") != "sm64-saturn-package-set-v2"
        or package_set.get("profile_id") != profile["profile_id"]
        or not isinstance(package_set.get("packages"), list)
        or package_set["packages"] != profile["package_manifests"]
        or not isinstance(package_set.get("package_class_hashes"), dict)
        or set(package_set["package_class_hashes"]) != set(PACKAGE_CLASSES)
    ):
        raise ValueError("package set differs from resolved target profile")
    package_keys: list[tuple[str, str]] = []
    for row in package_set["packages"]:
        if (
            not isinstance(row, dict)
            or set(row) != {"package_class", "package_id", "manifest_sha256"}
            or row.get("package_class") not in PACKAGE_CLASSES
            or _SHA256.fullmatch(str(row.get("manifest_sha256"))) is None
        ):
            raise ValueError("package set record schema is invalid")
        package_class = _portable_identifier(
            row["package_class"], "package class"
        )
        package_id = _portable_identifier(row.get("package_id"), "package id")
        package_keys.append((
            f"{package_class[0]}/{package_id[0]}",
            f"{package_class[1]}/{package_id[1]}",
        ))
    _reject_duplicate_casefold(package_keys, "package identities")
    if package_set["packages"] != sorted(
        package_set["packages"],
        key=lambda row: (
            row["package_class"].encode("utf-8"), row["package_id"].encode("utf-8")
        ),
    ):
        raise ValueError("package set records are not sorted")
    class_keys = [
        _portable_identifier(name, "package class hash id")
        for name in package_set["package_class_hashes"]
    ]
    _reject_duplicate_casefold(class_keys, "package class hash ids")
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
        or not toolchain["components"]
    ):
        raise ValueError("toolchain attestation schema is invalid")
    _portable_identifier(toolchain["target_abi"], "toolchain target ABI")
    component_ids: list[tuple[str, str]] = []
    toolchain_paths: list[tuple[str, str]] = []
    for component in toolchain["components"]:
        if (
            not isinstance(component, dict)
            or set(component) != {"id", "version", "binaries", "dependencies"}
            or not isinstance(component.get("version"), str)
            or not component["version"]
            or not isinstance(component.get("binaries"), list)
            or not isinstance(component.get("dependencies"), list)
        ):
            raise ValueError("toolchain component schema is invalid")
        component_ids.append(
            _portable_identifier(component.get("id"), "toolchain component id")
        )
        for records in (component["binaries"], component["dependencies"]):
            for record in records:
                if (
                    not isinstance(record, dict)
                    or set(record) != {"path", "sha256"}
                    or _SHA256.fullmatch(str(record.get("sha256"))) is None
                ):
                    raise ValueError("toolchain path record schema is invalid")
                path, key = _portable_relative_path(
                    record.get("path"), "toolchain"
                )
                toolchain_paths.append((path.as_posix(), key))
    _reject_duplicate_casefold(component_ids, "toolchain component ids")
    _reject_duplicate_casefold(toolchain_paths, "toolchain paths")
    if toolchain["components"] != sorted(
        toolchain["components"], key=lambda row: row["id"].encode("utf-8")
    ):
        raise ValueError("toolchain components are not sorted")


def _git_provenance(
    root: Path, source_closure: Mapping[str, Any], mode: str
) -> dict[str, Any]:
    """Record revision and one closure-clean fact without serializing dirty paths."""
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
    if mode == "release":
        sealed_rows = {
            (row["path"], row["class"]): row
            for row in source_closure["inputs"]
        }
        try:
            revision = verify_release_provenance(root, sealed_rows)
        except ValueError as error:
            raise ValueError("release source closure provenance is invalid") from error
        return {
            "git_revision": revision,
            "closure_clean": True,
        }
    revision = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=root, check=False,
        capture_output=True, text=True,
    )
    if revision.returncode != 0 or re.fullmatch(r"[0-9a-fA-F]{40}\s*", revision.stdout) is None:
        raise ValueError("repository Git revision is unavailable")
    status = subprocess.run(
        ["git", "status", "--porcelain=v1", "--untracked-files=all", "--", *paths],
        cwd=root, check=False, capture_output=True, text=True,
    )
    if status.returncode != 0:
        raise ValueError("source closure cleanliness is unavailable")
    clean = not bool(status.stdout)
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
    requested_outputs = {name: Path(outputs[name]).absolute() for name in OUTPUT_NAMES}
    _reject_output_aliases(requested_outputs)
    requested_release_dir = requested_outputs["cue"].parent
    if requested_release_dir.is_symlink():
        raise ValueError("release output directory is a symlink")
    for name, path in requested_outputs.items():
        current = path
        while current != requested_release_dir:
            if current.is_symlink():
                raise ValueError(f"{name} output path uses a symlink")
            parent = current.parent
            if parent == current:
                break
            current = parent
    resolved_outputs = {name: path.resolve() for name, path in requested_outputs.items()}
    release_dir = resolved_outputs["cue"].parent
    relative_paths = [
        (_release_relative(release_dir, resolved_outputs[name], name), "")
        for name in OUTPUT_NAMES
    ]
    relative_paths = [
        (value, _portable_relative_path(value, name)[1])
        for name, (value, _unused) in zip(OUTPUT_NAMES, relative_paths)
    ]
    _reject_duplicate_casefold(relative_paths, "output release paths")
    for name in OUTPUT_NAMES:
        if profile["output_names"][name] != resolved_outputs[name].name:
            raise ValueError(
                f"profile output name for {name} differs from sealed artifact basename"
            )
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
    effective_config = _effective_config_from_profile(
        profile["effective_config"], values
    )
    if values["version"] == identity.IDENTITY_V2_VERSION:
        identity_config = identity_document.get("effective_config")
        if not isinstance(identity_config, dict):
            raise ValueError("identity JSON effective config is missing")
        if identity.canonical_effective_config(identity_config) != (
            identity.canonical_effective_config(effective_config)
        ):
            raise ValueError(
                "profile effective config differs from identity effective config"
            )
    _validate_identity_binding(
        raw_embedded, values, identity_document, effective_config
    )
    profile_sha = hashlib.sha256(profile_raw).hexdigest()
    closure_sha = hashlib.sha256(closure_raw).hexdigest()
    package_sha = hashlib.sha256(package_raw).hexdigest()
    toolchain_sha = hashlib.sha256(toolchain_raw).hexdigest()
    if values["source_hash"] != closure_sha:
        raise ValueError("source closure SHA-256 differs from ELF identity")
    if values["version"] == identity.IDENTITY_V2_VERSION:
        if values.get("target_profile_hash") != profile_sha:
            raise ValueError("target profile SHA-256 differs from ELF identity")
        if values.get("package_set_root_hash") != package_sha:
            raise ValueError("package set SHA-256 differs from ELF identity")
        if values.get("toolchain_attestation_hash") != toolchain_sha:
            raise ValueError("toolchain attestation SHA-256 differs from ELF identity")
    _validate_cue(resolved_outputs["cue"], resolved_outputs["iso"])

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
    # This is deliberately the last read boundary before canonical manifest
    # publication: release mode rechecks clean index state, every declared
    # closure digest, and an unchanged HEAD after all other inputs/outputs.
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
    output_paths: list[tuple[str, str]] = []
    for name in OUTPUT_NAMES:
        record = document["outputs"][name]
        if not isinstance(record, dict) or set(record) != {"path", "size", "sha256"}:
            raise ValueError(f"release manifest {name} output record is invalid")
        if type(record["size"]) is not int or record["size"] <= 0:
            raise ValueError(f"release manifest {name} output size is invalid")
        _require_sha256(record["sha256"], f"{name} output")
        portable, key = _portable_relative_path(record["path"], name)
        output_paths.append((portable.as_posix(), key))
    _reject_duplicate_casefold(output_paths, "output release paths")


def _validate_exact_inventory(base: Path, document: Mapping[str, Any]) -> None:
    expected_files = {MANIFEST_NAME}
    expected_directories: set[str] = set()
    for record in document["outputs"].values():
        path = PurePosixPath(record["path"])
        expected_files.add(path.as_posix())
        parent = path.parent
        while parent != PurePosixPath("."):
            expected_directories.add(parent.as_posix())
            parent = parent.parent
    actual_files: set[str] = set()
    actual_directories: set[str] = set()
    with DirectoryNamespaceGuard(base) as namespace:
        for root, directories, files in os.walk(base, followlinks=False):
            root_path = Path(root)
            for name in directories:
                candidate = root_path / name
                metadata = candidate.lstat()
                if stat.S_ISLNK(metadata.st_mode) or _is_reparse(metadata):
                    raise ValueError(f"release inventory contains a path alias: {candidate}")
                actual_directories.add(candidate.relative_to(base).as_posix())
            for name in files:
                candidate = root_path / name
                metadata = candidate.lstat()
                if not stat.S_ISREG(metadata.st_mode) or _is_reparse(metadata):
                    raise ValueError(f"release inventory contains a non-file: {candidate}")
                actual_files.add(candidate.relative_to(base).as_posix())
        namespace.require_current()
    if actual_files != expected_files or actual_directories != expected_directories:
        extras = sorted(
            (actual_files - expected_files) | (actual_directories - expected_directories)
        )
        missing = sorted(
            (expected_files - actual_files) | (expected_directories - actual_directories)
        )
        raise ValueError(
            f"release inventory differs from manifest; extras={extras}, missing={missing}"
        )


def verify_release_manifest(
    path: Path, *, required_profile: str | None = None, exact_inventory: bool = False
) -> ReleaseManifestVerification:
    """Verify canonical manifest bytes, every output, CUE binding, and identity."""
    raw, document = _load_canonical(path, "release manifest")
    _validate_manifest_shape(document)
    if required_profile is not None and document["profile_id"] != required_profile:
        raise ValueError(
            f"release manifest required profile is {required_profile}, "
            f"got {document['profile_id']}"
        )
    base = path.absolute().parent
    if base.is_symlink():
        raise ValueError("release manifest directory is a symlink")
    outputs: dict[str, Path] = {}
    for name in OUTPUT_NAMES:
        record = document["outputs"][name]
        output = _resolve_release_path(base, record["path"], name)
        outputs[name] = output
    _reject_output_aliases(outputs)
    snapshot_owner = _SnapshotOwner()
    snapshot_base = snapshot_owner.path
    snapshot_outputs: dict[str, Path] = {}
    source_identities: dict[tuple[int, int], str] = {}
    try:
        for name in OUTPUT_NAMES:
            record = document["outputs"][name]
            target = snapshot_base / Path(*PurePosixPath(record["path"]).parts)
            source_identity = _copy_verified_snapshot(
                outputs[name], target, record, name
            )
            if source_identity in source_identities:
                raise ValueError(
                    f"output alias: {name} and {source_identities[source_identity]}"
                )
            source_identities[source_identity] = name
            snapshot_outputs[name] = target.resolve()

        embedded, values = _extract_build_identity(snapshot_outputs["elf"])
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
                    raise ValueError(f"{manifest_field} differs from ELF identity")
        _validate_cue(snapshot_outputs["cue"], snapshot_outputs["iso"])
        if exact_inventory:
            _validate_exact_inventory(base, document)
        return ReleaseManifestVerification(
            document=dict(document),
            manifest_sha256=hashlib.sha256(raw).hexdigest(),
            outputs=outputs,
            manifest_bytes=raw,
            snapshot_outputs=snapshot_outputs,
            _snapshot_owner=snapshot_owner,
        )
    except BaseException:
        snapshot_owner.cleanup()
        raise


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
    try:
        second_verified = verify_release_manifest(second)
        try:
            def semantic(document: Mapping[str, Any]) -> dict[str, Any]:
                return {
                    field: document[field]
                    for field in (
                        "profile_id", "target_profile_sha256", "identity_version",
                        "identity_sha256", "effective_config_sha256", "effective_config",
                        "source_closure_sha256", "package_set_sha256",
                        "toolchain_attestation_sha256",
                    )
                } | {
                    "outputs": {
                        name: {
                            "size": document["outputs"][name]["size"],
                            "sha256": document["outputs"][name]["sha256"],
                        }
                        for name in OUTPUT_NAMES
                    }
                }
            differing = _differences(
                semantic(first_verified.document), semantic(second_verified.document)
            )
            return {
                "schema": COMPARISON_SCHEMA,
                "identical": not differing,
                "differing_fields": differing,
                "first_manifest_sha256": first_verified.manifest_sha256,
                "second_manifest_sha256": second_verified.manifest_sha256,
            }
        finally:
            second_verified.close()
    finally:
        first_verified.close()


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
        try:
            print(verified.manifest_sha256)
        finally:
            verified.close()
        return 0
    comparison = compare_release_manifests(args.first, args.second)
    raw = canonical_json_bytes(comparison)
    if args.output is not None:
        write_if_changed(args.output.resolve(), raw)
    print(raw.decode("ascii"), end="")
    return 0 if comparison["identical"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
