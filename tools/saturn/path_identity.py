"""Portable path-alias checks for immutable inputs and output leaves."""

from __future__ import annotations

import os
import unicodedata
from pathlib import Path
from typing import Iterable


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
