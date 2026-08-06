#!/usr/bin/env python3
"""Verify pinned MC68000 compiler and freshly linked Task 17 module artifact."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import subprocess
import sys


PONESOUND_COMMIT = "31782e4c61337327f23eb9aa45ecd37fe0944ea0"
M68K_GCC_SHA256 = "e863c1bbcbf86e0493989721346dfa28450236505abbcbc5aeb79f48645a286b"
M68K_GCC_VERSION = "11.1.0"


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def undefined_symbols(output: str) -> list[str]:
    return [line.split()[-1] for line in output.splitlines() if line.strip()]


def require_m68k_elf(output: str) -> None:
    if "file format elf32-m68k" not in output or "architecture: m68k" not in output:
        raise ValueError("artifact is not an elf32-m68k object")


def require_artifact_fresh(artifact: pathlib.Path,
                           inputs: list[pathlib.Path]) -> None:
    artifact_mtime = artifact.stat().st_mtime_ns
    newer = [str(path) for path in inputs if path.stat().st_mtime_ns > artifact_mtime]
    if newer:
        raise ValueError("module input is newer than artifact: " + ", ".join(newer))


def run(*arguments: str) -> str:
    return subprocess.run(arguments, check=True, text=True,
                          stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT).stdout


def tool_path(bindir: pathlib.Path, name: str) -> pathlib.Path:
    for candidate in (bindir / f"{name}.exe", bindir / name):
        if candidate.is_file():
            return candidate.resolve()
    raise FileNotFoundError(f"missing pinned tool {name} under {bindir}")


def verify(args: argparse.Namespace) -> dict[str, object]:
    ponesound_root = args.ponesound_root.resolve()
    bindir = args.m68k_bindir.resolve()
    artifact = args.artifact.resolve()
    objects = [path.resolve() for path in args.object]
    gcc = tool_path(bindir, "m68k-elf-gcc")
    nm = tool_path(bindir, "m68k-elf-nm")
    objdump = tool_path(bindir, "m68k-elf-objdump")

    if bindir.parent != ponesound_root:
        raise ValueError("M68K_BINDIR is not the pinned PoneSound m68k-elf directory")
    commit = run("git", "-c", f"safe.directory={ponesound_root}",
                 "-C", str(ponesound_root), "rev-parse", "HEAD").strip()
    if commit != PONESOUND_COMMIT:
        raise ValueError(f"PoneSound commit mismatch: {commit}")
    gcc_sha = sha256_file(gcc)
    if gcc_sha != M68K_GCC_SHA256:
        raise ValueError(f"m68k-elf-gcc SHA-256 mismatch: {gcc_sha}")
    version = run(str(gcc), "--version").splitlines()[0]
    if M68K_GCC_VERSION not in version:
        raise ValueError(f"m68k-elf-gcc version mismatch: {version}")
    if not artifact.is_file() or not objects or any(not path.is_file() for path in objects):
        raise ValueError("module artifact or one of its forced-rebuild objects is missing")
    require_artifact_fresh(artifact, objects)
    require_m68k_elf(run(str(objdump), "-f", str(artifact)))
    unresolved = undefined_symbols(run(str(nm), "-u", str(artifact)))
    if unresolved:
        raise ValueError("module has undefined symbols: " + ", ".join(unresolved))
    return {
        "ponesound_commit": commit,
        "compiler": str(gcc),
        "compiler_sha256": gcc_sha,
        "compiler_version": version,
        "artifact": str(artifact),
        "artifact_sha256": sha256_file(artifact),
        "object_count": len(objects),
        "undefined_symbols": unresolved,
    }


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ponesound-root", type=pathlib.Path, required=True)
    parser.add_argument("--m68k-bindir", type=pathlib.Path, required=True)
    parser.add_argument("--artifact", type=pathlib.Path, required=True)
    parser.add_argument("--object", type=pathlib.Path, action="append", required=True)
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    try:
        record = verify(parse_args(argv))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"audio68k module verification failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps(record, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
