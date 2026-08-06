#!/usr/bin/env python3
"""Materialize the sourceboot identity spec from canonical build inputs.

The identity generator deliberately validates byte hashes, but a clean source
tree has no checked-in generated spec to validate.  This small bootstrap owns
that first-build boundary: it records the canonical provenance of each logical
artifact in deterministic manifests, then writes the generator's input spec.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
from typing import Mapping

import gen_build_identity as identity


# These are source/provenance boundaries until Task 22 replaces the provisional
# package inputs with a final BOB root and payloads.  They are still real files,
# never hand-written hashes or a stale generated identity.
STATIC_INPUTS: dict[str, tuple[str, ...]] = {
    "source_hash": (
        "src/port/saturn/sourceboot/Makefile",
        "src/port/saturn/sourceboot/source_entry.c",
        "src/port/saturn/runtime/saturn_source_runtime.c",
    ),
    "input_artifact_hash": (
        "src/port/saturn/sourceboot/source_demo_data.c",
        "src/port/saturn/controller/controller_saturn.c",
    ),
    "camera_artifact_hash": (
        "src/port/saturn/runtime/saturn_camera_role.c",
        "src/port/saturn/runtime/saturn_camera_fixed.c",
    ),
    "cart_profile_hash": (
        "src/port/saturn/sourceboot/source_cart.c",
        "src/port/saturn/sourceboot/source_cart.h",
        "tools/saturn/launch_ymir_desktop.py",
    ),
    "scene_package_hash": (
        "tools/saturn/compile_scene_package.py",
        "tools/saturn/scene_package_schema.py",
        "levels/bob/script.c",
    ),
    "scene_dependency_set_hash": (
        "tools/saturn/collect_scene_closure.py",
        "tools/saturn/behavior_spawn_rules.json",
        "levels/bob/areas/1/geo.inc.c",
    ),
    "actor_package_hash": (
        "tools/saturn/compile_actor_bank.py",
        "tools/saturn/manifests/actors/mario.json",
        "actors/mario/geo.inc.c",
    ),
    "animation_package_hash": (
        "tools/saturn/extract_mario_actor.py",
        "tools/mario_anims_converter.py",
        "assets/anims/anim_00.inc.c",
    ),
}


def _route_input(camera_route: int) -> str:
    return ("tools/saturn/routes/bob_default_camera_v1.json"
            if camera_route == 1 else "tools/saturn/routes/bob_parity_v1.json")


def _audio_inputs(semantic_audio: int) -> tuple[str, ...]:
    if semantic_audio:
        return (
            "src/port/saturn/sourceboot/source_audio_semantics.c",
            "src/port/saturn/audio/saturn_audio_policy.c",
            "src/port/saturn/audio/saturn_audio_spatial.c",
            "tools/saturn/compile_saturn_audio.py",
        )
    return ("src/port/saturn/sourceboot/source_audio_stub.c",)


def _animation_inputs(root: Path) -> tuple[str, ...]:
    animations = sorted(path.relative_to(root).as_posix()
                        for path in (root / "assets/anims").glob("*.inc.c"))
    if not animations:
        raise ValueError("animation_package_hash has no canonical animation inputs")
    return ("tools/saturn/extract_mario_actor.py", "tools/mario_anims_converter.py",
            *animations)


def all_input_paths() -> tuple[str, ...]:
    """Return every possible canonical input so isolated tests can seed a repo."""
    paths = {path for values in STATIC_INPUTS.values() for path in values}
    paths.update((_route_input(0), _route_input(1)))
    paths.update(_audio_inputs(0))
    paths.update(_audio_inputs(1))
    return tuple(sorted(paths))


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _integer_config(config: Mapping[str, int]) -> dict[str, int]:
    required = set(identity.SCALAR_FIELDS) | set(identity.COMPILER_CONFIG_FIELDS)
    required.update("features." + feature for feature in identity.FEATURE_BITS)
    if set(config) != required:
        missing = sorted(required - set(config))
        extra = sorted(set(config) - required)
        raise ValueError(f"identity bootstrap config mismatch; missing={missing}, extra={extra}")
    if any(type(value) is not int for value in config.values()):
        raise ValueError("identity bootstrap config values must be integers")
    return dict(config)


def _artifact_inputs(root: Path, config: Mapping[str, int]) -> dict[str, tuple[str, ...]]:
    inputs = dict(STATIC_INPUTS)
    inputs["animation_package_hash"] = _animation_inputs(root)
    inputs["route_artifact_hash"] = (_route_input(config["camera_route"]),)
    inputs["audio_package_hash"] = _audio_inputs(config["features.semantic_audio"])
    return inputs


def _write_if_changed(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_file() and path.read_bytes() == data:
        return
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_bytes(data)
    os.replace(temporary, path)


def _manifest(root: Path, field: str, relative_paths: tuple[str, ...]) -> bytes:
    inputs = []
    for relative in relative_paths:
        path = root / relative
        if not path.is_file():
            raise ValueError(f"{field} canonical input is not a file: {relative}")
        inputs.append({"path": relative.replace("\\", "/"), "sha256": _sha256(path)})
    document = {
        "schema": "sm64-saturn-identity-provenance-v1",
        "artifact": field,
        "inputs": inputs,
    }
    return (json.dumps(document, sort_keys=True, separators=(",", ":"),
                       ensure_ascii=True) + "\n").encode("ascii")


def write_spec(root: Path, output: Path, config: Mapping[str, int]) -> None:
    """Write a complete hash-checked spec, refusing invalid/stale source inputs."""
    root = root.resolve()
    values = _integer_config(config)
    artifacts: dict[str, dict[str, str]] = {}
    manifest_dir = output.parent / "saturn_build_identity_inputs"
    for field, relative_paths in _artifact_inputs(root, values).items():
        manifest_path = manifest_dir / f"{field}.json"
        _write_if_changed(manifest_path, _manifest(root, field, relative_paths))
        artifacts[field] = {"path": str(manifest_path.resolve()),
                            "sha256": _sha256(manifest_path)}
    spec = {
        "features": {feature: values[f"features.{feature}"]
                     for feature in identity.FEATURE_BITS},
        **{field: values[field] for field in identity.SCALAR_FIELDS},
        **{field: values[field] for field in identity.COMPILER_CONFIG_FIELDS},
        "artifacts": artifacts,
    }
    # The same validation used by the target generator makes bootstrap failure
    # fatal before Make can select a label or compile against stale identity C.
    identity.build_identity(spec)
    _write_if_changed(
        output,
        (json.dumps(spec, sort_keys=True, indent=2, ensure_ascii=True) + "\n").encode("utf-8"),
    )


def _parse_settings(items: list[str]) -> dict[str, int]:
    values: dict[str, int] = {}
    for item in items:
        name, separator, raw = item.partition("=")
        if not separator or not name:
            raise ValueError(f"invalid --set value: {item}")
        try:
            values[name] = int(raw, 10)
        except ValueError as error:
            raise ValueError(f"invalid integer for {name}: {raw}") from error
    return values


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--set", action="append", default=[], metavar="NAME=INTEGER")
    args = parser.parse_args()
    write_spec(args.root, args.output, _parse_settings(args.set))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
