#!/usr/bin/env python3
"""Build the untracked S64A catalog and scene closure manifests."""
from pathlib import Path
import argparse
import json

from saturn_audio_package import compile_catalog


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    out = args.output_dir
    manifest = out / "audio_manifest.json"
    result = compile_catalog(args.root.resolve(), out / "AUDIO.DAT", manifest)
    for scene in ("bob", "wf"):
        (out / f"{scene}_audio_closure.json").write_text(
            json.dumps(result["closures"][scene], indent=2, sort_keys=True) + "\n",
            encoding="utf-8")
    print(json.dumps(result, sort_keys=True))


if __name__ == "__main__":
    main()
