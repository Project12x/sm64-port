#!/usr/bin/env python3
"""Build one serialized sourceboot feature variant and seal its artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path
from typing import Callable, Mapping


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _diagnostic_value(value: str) -> int:
    return {"none": 0, "animation-sweep": 1, "scene-transition": 2}[value]


def artifact_identity(path: Path) -> dict[str, object]:
    document = json.loads(path.read_text(encoding="utf-8"))
    required = {"schema", "label", "features", "diagnostic_mode", "artifacts"}
    if set(document) < required or document["schema"] != "sm64-saturn-sourceboot-variant-v1":
        raise ValueError("variant artifact schema is incomplete")
    artifacts = document["artifacts"]
    if not isinstance(artifacts, dict) or set(artifacts) != {"elf", "cue", "iso"}:
        raise ValueError("variant artifact trio is incomplete")
    for name, item in artifacts.items():
        if not isinstance(item, dict) or not isinstance(item.get("path"), str) or not isinstance(item.get("sha256"), str):
            raise ValueError(f"{name} artifact identity is malformed")
        artifact_path = Path(item["path"])
        if not artifact_path.is_file() or sha256(artifact_path) != item["sha256"]:
            raise ValueError(f"{name} artifact hash mismatch")
    return document


def build_variant(
    *, label: str, animation: int, actors: int, audio: int,
    pipeline: int, diagnostic_mode: str, jobs: int, output: Path,
    repo_root: Path, run: Callable[..., subprocess.CompletedProcess] = subprocess.run,
) -> dict[str, object]:
    if not label or any(value not in (0, 1) for value in (animation, actors, audio)):
        raise ValueError("label and feature values are invalid")
    if jobs != 1:
        raise ValueError("sourceboot variants are serialized; jobs must be 1")
    if pipeline not in (2, 3, 4):
        raise ValueError("renderer pipeline is invalid")
    diagnostic = _diagnostic_value(diagnostic_mode)
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    lock = output / ".build.lock"
    if lock.exists():
        raise ValueError("sourceboot variant build is already active")
    lock.write_text("serialized\n", encoding="ascii")
    try:
        wrapper = repo_root / "tools/saturn/with-msys-toolchain.ps1"
        command = [
            "powershell", "-ExecutionPolicy", "Bypass", "-File", str(wrapper),
            "mingw32-make", "-f", str(repo_root / "Makefile.saturn.mk"), "-j1",
            "sourceboot", f"SATURN_FEATURE_COMPLETE_MARIO_ANIMATION={animation}",
            f"SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE={actors}",
            f"SATURN_FEATURE_SEMANTIC_AUDIO={audio}",
            f"SATURN_RENDERER_PIPELINE={pipeline}",
            f"SATURN_DIAGNOSTIC_MODE={diagnostic}",
        ]
        completed = run(command, cwd=str(repo_root), check=False)
        if completed.returncode != 0:
            raise RuntimeError("serialized sourceboot variant build failed")
        sourceboot = repo_root / "build/saturn/sourceboot"
        candidates = {
            "elf": sorted(sourceboot.rglob("*.elf")),
            "cue": sorted(sourceboot.rglob("*.cue")),
            "iso": sorted(sourceboot.rglob("*.iso")),
        }
        if any(not values for values in candidates.values()):
            raise RuntimeError("sourceboot build did not produce ELF/CUE/ISO trio")
        artifacts: dict[str, dict[str, str]] = {}
        for kind, values in candidates.items():
            destination = output / f"sm64-saturn-sourceboot-{kind}{values[-1].suffix}"
            shutil.copy2(values[-1], destination)
            artifacts[kind] = {"path": str(destination), "sha256": sha256(destination)}
        cue = Path(artifacts["cue"]["path"])
        cue_lines = cue.read_text(encoding="ascii").splitlines()
        iso_name = Path(artifacts["iso"]["path"]).name
        cue.write_text("\n".join([line.replace(Path(candidates["iso"][-1]).name, iso_name) for line in cue_lines]) + "\n", encoding="ascii", newline="\n")
        artifacts["cue"]["sha256"] = sha256(cue)
        document = {
            "schema": "sm64-saturn-sourceboot-variant-v1", "label": label,
            "features": {"complete_mario_animation": animation,
                          "dynamic_actor_closure": actors, "semantic_audio": audio},
            "renderer_pipeline": pipeline, "diagnostic_mode": diagnostic,
            "artifacts": artifacts,
        }
        output_file = output / "artifact.json"
        output_file.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        return document
    finally:
        lock.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--label", required=True)
    parser.add_argument("--animation", type=int, choices=(0, 1), required=True)
    parser.add_argument("--actors", type=int, choices=(0, 1), required=True)
    parser.add_argument("--audio", type=int, choices=(0, 1), required=True)
    parser.add_argument("--pipeline", type=int, choices=(2, 3, 4), required=True)
    parser.add_argument("--diagnostic-mode", choices=("none", "animation-sweep", "scene-transition"), default="none")
    parser.add_argument("--jobs", type=int, default=1)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    build_variant(repo_root=Path(__file__).resolve().parents[2], **vars(args))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
