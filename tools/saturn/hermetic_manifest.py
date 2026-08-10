"""Strict, deterministic primitives shared by Saturn release manifests."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
from typing import Any, Iterable, Mapping


def canonical_json_bytes(document: Mapping[str, Any]) -> bytes:
    """Encode a document using the one canonical repository representation."""
    return (
        json.dumps(document, sort_keys=True, separators=(",", ":"), ensure_ascii=True)
        + "\n"
    ).encode("ascii")


def sha256_file(path: Path) -> str:
    """Return the lowercase digest of the bytes presently on disk."""
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def normalize_repo_path(root: Path, value: str | Path) -> str:
    """Return a non-empty forward-slash path beneath ``root`` or fail closed."""
    root = root.resolve()
    candidate = (root / value).resolve() if not Path(value).is_absolute() else Path(value).resolve()
    try:
        relative = candidate.relative_to(root)
    except ValueError as error:
        raise ValueError(f"path escapes repository: {value}") from error
    rendered = relative.as_posix()
    if not rendered or rendered == ".":
        raise ValueError("repository input path is empty")
    return rendered


def reject_case_collisions(values: Iterable[str]) -> None:
    """Reject paths that differ only by case, including Windows aliases."""
    seen: dict[str, str] = {}
    for value in values:
        key = value.casefold()
        existing = seen.get(key)
        if existing is not None and existing != value:
            raise ValueError(f"case-colliding repository paths: {existing}, {value}")
        seen[key] = value


def write_if_changed(path: Path, data: bytes) -> None:
    """Atomically replace ``path`` only after complete new bytes are durable."""
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_file() and path.read_bytes() == data:
        return
    temporary = path.with_name(path.name + ".tmp")
    try:
        with temporary.open("wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    except BaseException:
        try:
            temporary.unlink(missing_ok=True)
        finally:
            raise
