#!/usr/bin/env python3
"""Stage only the verified files from one exact Saturn release manifest."""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path

from release_manifest import MANIFEST_NAME, verify_release_manifest


def _copy_new(source: Path, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    with source.open("rb") as input_stream, target.open("xb") as output_stream:
        shutil.copyfileobj(input_stream, output_stream, length=1024 * 1024)


def stage_release(manifest: Path, destination: Path) -> Path:
    """Verify all sources, then copy into a missing or empty destination."""
    verified = verify_release_manifest(manifest)
    destination = destination.resolve()
    if destination.exists():
        if not destination.is_dir() or any(destination.iterdir()):
            raise ValueError(f"release destination is not empty: {destination}")
    destination.mkdir(parents=True, exist_ok=True)
    for name, source in verified.outputs.items():
        relative = Path(verified.document["outputs"][name]["path"])
        _copy_new(source, destination / relative)
    _copy_new(manifest.resolve(), destination / MANIFEST_NAME)
    verify_release_manifest(destination / MANIFEST_NAME)
    return destination


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--destination", type=Path, required=True)
    args = parser.parse_args(argv)
    print(stage_release(args.manifest, args.destination))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
