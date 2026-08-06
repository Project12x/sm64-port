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
import prepare_sourceboot_assets


# All source roots that can contribute to sourceboot's SH-2 closure.  Hashing a
# conservative superset is intentional: an unrelated source edit may reseal a
# build, but an ELF-affecting source/header/linker/tool edit can never escape
# its identity. Generated payloads are deliberately *not* listed here; their
# exact bytes own the package-named fields below.
SOURCE_CLOSURE_ROOTS = (
    "src", "include", "actors", "levels", "lib/src", "data", "bin", "tools/saturn",
    "textures", "assets",
)
SOURCE_CLOSURE_FILES = (
    "Makefile.saturn.mk", "src/port/saturn/sourceboot/Makefile",
    "src/port/saturn/sourceboot/sourceboot.specs",
    "src/port/saturn/sourceboot/sourceboot-cart.x",
    "tools/mario_anims_converter.py",
    "tools/saturn/bootstrap_sourceboot_identity_spec.py",
    "tools/saturn/gen_build_identity.py",
)

# Non-package execution/input artifacts. Package-named fields below name and
# hash their actual available payload bytes, never compiler recipes.
STATIC_INPUTS: dict[str, tuple[str, ...]] = {
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
}

SCENE_PAYLOAD = "build/saturn/sourceboot/generated/bob_area1_compiled.json"
SCENE_DEPENDENCY_PAYLOAD = "build/saturn/sourceboot/generated/bob_area1_bsp_report.json"
ACTOR_PAYLOAD = "build/saturn/actors/mario/mario.s64b"
ANIMATION_PAYLOAD = "build/saturn/sourceboot/generated/mario_anim_data.c"
ACTOR_BANK_C_PAYLOAD = "build/saturn/sourceboot/generated/mario_actor_bank.c"
GENERATED_IMAGE_INPUTS = (
    "build/saturn/sourceboot/generated/bob_area1_compiled.json",
    "build/saturn/sourceboot/generated/bob_area1_bsp_report.json",
    "build/saturn/sourceboot/generated/bob_scene.h",
    "build/saturn/sourceboot/generated/bob_bsp.h",
    "build/saturn/sourceboot/generated/bob_bsp_fragments.h",
    "build/saturn/sourceboot/generated/bob_tiles_clut16.bin",
    "build/saturn/sourceboot/generated/bob_tiles_clut16.pal",
    "build/saturn/sourceboot/generated/bob_bsp_fragments_clut16.bin",
    "build/saturn/sourceboot/generated/bob_bsp_fragments_clut16.pal",
    "build/saturn/sourceboot/generated/bob_sky_rgb1555.bin",
    "build/saturn/sourceboot/generated/saturn_quad_map.c",
    "build/saturn/sourceboot/generated/saturn_quad_map.h",
    "build/saturn/sourceboot/generated/mario_anim_data.c",
    "build/saturn/sourceboot/generated/sourceboot_collision_catalog.inc",
    "build/saturn/marioturntable/generated/mario_eye_uv_tiles.h",
    "build/us_pc/bin/water_skybox.c",
)
SOURCEBOOT_ASSET_FIXED_SOURCES = (
    "src/goddard/renderer.c", "levels/bob/script.c", "levels/bob/geo.c",
    "levels/bob/leveldata.c", "levels/menu/leveldata.c",
    "levels/castle_grounds/leveldata.c", "levels/ttc/leveldata.c",
)


def _route_input(camera_route: int) -> str:
    return ("tools/saturn/routes/bob_default_camera_v1.json"
            if camera_route == 1 else "tools/saturn/routes/bob_parity_v1.json")


def _audio_inputs(semantic_audio: int) -> tuple[str, ...]:
    if semantic_audio:
        raise ValueError(
            "audio_package_hash requires a staged S64A/AUDIO.DAT and sound-CPU image; "
            "semantic_audio=1 is blocked until Task 21/22 integrates exact payloads"
        )
    return ("src/port/saturn/sourceboot/source_audio_stub.c",)


def _animation_inputs(root: Path) -> tuple[str, ...]:
    animations = sorted(path.relative_to(root).as_posix()
                        for path in (root / "assets/anims").glob("*.inc.c"))
    if not animations:
        raise ValueError("animation_package_hash has no canonical animation inputs")
    return ("tools/saturn/extract_mario_actor.py", "tools/mario_anims_converter.py",
            *animations)


def _source_closure_inputs(root: Path) -> tuple[str, ...]:
    paths: set[str] = set()
    for relative_root in SOURCE_CLOSURE_ROOTS:
        directory = root / relative_root
        if not directory.is_dir():
            raise ValueError(f"source_hash closure root is missing: {relative_root}")
        for path in directory.rglob("*"):
            if path.is_file():
                paths.add(path.relative_to(root).as_posix())
    for relative in SOURCE_CLOSURE_FILES:
        if not (root / relative).is_file():
            raise ValueError(f"source_hash closure input is not a file: {relative}")
        paths.add(relative)
    for relative in GENERATED_IMAGE_INPUTS:
        if not (root / relative).is_file():
            raise ValueError(
                f"source_hash generated image input is not a file: {relative}; "
                "run sourceboot identity-assets before sealing"
            )
        paths.add(relative)
    return tuple(sorted(paths))


def _sourceboot_asset_sources(root: Path) -> list[Path]:
    relative = [
        *(path.relative_to(root) for path in sorted((root / "bin").glob("*.c"))),
        *(Path(path) for path in SOURCEBOOT_ASSET_FIXED_SOURCES[:1]),
        *(path.relative_to(root) for path in sorted((root / "actors").glob("*.c"))),
        *(Path(path) for path in SOURCEBOOT_ASSET_FIXED_SOURCES[1:]),
    ]
    return [root / path for path in relative]


def _sourceboot_generated_asset_inputs(root: Path) -> tuple[str, ...]:
    """Use the same traversal/source roots as sourceboot's asset Make recipe."""
    targets = prepare_sourceboot_assets.collect_targets(
        root, _sourceboot_asset_sources(root), "build/us_pc", {"VERSION_US", "VERSION_JP_US"}
    )
    targets.append("build/us_pc/include/text_strings.h")
    for relative in targets:
        if not (root / relative).is_file():
            raise ValueError(
                f"source_hash generated source asset is not a file: {relative}; "
                "run sourceboot identity-assets before sealing"
            )
    return tuple(sorted(targets))


def all_input_paths() -> tuple[str, ...]:
    """Return every possible canonical input so isolated tests can seed a repo."""
    paths = {path for values in STATIC_INPUTS.values() for path in values}
    paths.update(SOURCE_CLOSURE_FILES)
    paths.update(SOURCEBOOT_ASSET_FIXED_SOURCES)
    paths.update((
        "src/port/saturn/sourceboot/main.c",
        "src/port/saturn/gfx/saturn_actor_instance.c",
        "textures/skyboxes/water.png",
        SCENE_PAYLOAD, SCENE_DEPENDENCY_PAYLOAD, ACTOR_PAYLOAD, ANIMATION_PAYLOAD,
        ACTOR_BANK_C_PAYLOAD, "build/us_pc/include/text_strings.h",
        *GENERATED_IMAGE_INPUTS,
    ))
    paths.update((_route_input(0), _route_input(1)))
    paths.update(_audio_inputs(0))
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
    source_inputs = set(_source_closure_inputs(root))
    source_inputs.update(_sourceboot_generated_asset_inputs(root))
    if config["features.complete_mario_animation"]:
        actor_bank_c = root / ACTOR_BANK_C_PAYLOAD
        if not actor_bank_c.is_file():
            raise ValueError(
                "source_hash feature-selected actor bank is not a file: "
                f"{ACTOR_BANK_C_PAYLOAD}; run sourceboot identity-assets before sealing"
            )
        source_inputs.add(ACTOR_BANK_C_PAYLOAD)
    inputs["source_hash"] = tuple(sorted(source_inputs))
    inputs["route_artifact_hash"] = (_route_input(config["camera_route"]),)
    # These are exact byte payloads currently consumed by the feature-off
    # sourceboot comparator. Task 22 alone replaces them with final S64P roots
    # and dependency packs. Missing payloads fail before label/build.
    inputs["scene_package_hash"] = (SCENE_PAYLOAD,)
    inputs["scene_dependency_set_hash"] = (SCENE_DEPENDENCY_PAYLOAD,)
    inputs["actor_package_hash"] = (ACTOR_PAYLOAD,)
    inputs["animation_package_hash"] = (ANIMATION_PAYLOAD,)
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
