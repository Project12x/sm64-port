#!/usr/bin/env python3
"""Generate and validate the immutable sourceboot feature/package identity."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping


MAGIC = 0x53424931  # SBI1
VERSION = 1
FEATURE_BITS = {
    "complete_mario_animation": 1 << 0,
    "dynamic_actor_closure": 1 << 1,
    "semantic_audio": 1 << 2,
}
KNOWN_FEATURE_BITS = sum(FEATURE_BITS.values())
SCALAR_FIELDS = (
    "renderer_pipeline", "level_id", "area_id", "route_id",
    "route_replay_mode", "live_input_mode", "camera_route",
    "camera_variant", "diagnostic_mode", "bootstrap_ticks", "cart_mbit",
    "cart_stage_sectors", "hot_promotion", "near_clip", "bsp_order",
    "polygon_tier", "fragment_mode",
)
COMPILER_CONFIG_FIELDS = (
    "atan2_variant", "demo_path", "demo_view_radius", "slave_render",
    "camera_idle_start_tick", "camera_idle_discovery", "camera_range_capture",
    "bsp_fragment_flat", "fast3d_q16_trace", "experimental_skip_geo_walk",
    "object_pool_capacity",
)
ARTIFACT_HASH_FIELDS = (
    "source_hash", "route_artifact_hash", "input_artifact_hash",
    "camera_artifact_hash", "cart_profile_hash", "scene_package_hash",
    "scene_dependency_set_hash", "actor_package_hash",
    "animation_package_hash", "audio_package_hash",
)
HASH_FIELDS = (
    "source_hash", "effective_config_hash", "route_artifact_hash",
    "input_artifact_hash", "camera_artifact_hash", "cart_profile_hash",
    "scene_package_hash", "scene_dependency_set_hash", "actor_package_hash",
    "animation_package_hash", "audio_package_hash",
)
IDENTITY_STRUCT = struct.Struct(
    ">IHHI"      # magic, version, size, features
    "HHHH"       # renderer, level, area, route
    "HH"         # replay, live input
    "HHHH"       # camera route/variant, diagnostic, reserved0
    "I"          # bootstrap ticks
    "HHHH"       # cart mbit/staging, hot promotion, near clip
    "HHHH"       # BSP order, polygon tier, fragmentation, reserved1
    + "32s" * len(HASH_FIELDS)
)
IDENTITY_SIZE = IDENTITY_STRUCT.size


@dataclass(frozen=True)
class BuiltIdentity:
    raw: bytes
    canonical_config: bytes
    values: dict[str, Any]


def _integer(name: str, value: Any, minimum: int, maximum: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{name} must be an integer")
    if not minimum <= value <= maximum:
        raise ValueError(f"{name} must be between {minimum} and {maximum}")
    return value


def _boolean(name: str, value: Any) -> int:
    if type(value) is not int or value not in (0, 1):
        raise ValueError(f"{name} must be 0 or 1")
    return value


def _choice(name: str, value: Any, choices: tuple[int, ...]) -> int:
    value = _integer(name, value, 0, 0xFFFFFFFF)
    if value not in choices:
        rendered = ", ".join(str(choice) for choice in choices)
        raise ValueError(f"{name} must be one of {rendered}")
    return value


def _validate_scalars(spec: Mapping[str, Any]) -> dict[str, int]:
    missing = [field for field in SCALAR_FIELDS if field not in spec]
    if missing:
        raise ValueError("missing identity scalar(s): " + ", ".join(missing))
    values = {
        "renderer_pipeline": _choice("renderer_pipeline", spec["renderer_pipeline"], (2, 3, 4)),
        "level_id": _integer("level_id", spec["level_id"], 0, 0xFFFF),
        "area_id": _integer("area_id", spec["area_id"], 0, 0xFFFF),
        "route_id": _integer("route_id", spec["route_id"], 0, 0xFFFF),
        "route_replay_mode": _boolean("route_replay_mode", spec["route_replay_mode"]),
        "live_input_mode": _boolean("live_input_mode", spec["live_input_mode"]),
        "camera_route": _choice("camera_route", spec["camera_route"], (0, 1)),
        "camera_variant": _choice("camera_variant", spec["camera_variant"], (1, 2, 3)),
        "diagnostic_mode": _choice("diagnostic_mode", spec["diagnostic_mode"], (0, 1, 2)),
        "bootstrap_ticks": _choice("bootstrap_ticks", spec["bootstrap_ticks"], (0, 600, 1200, 2000)),
        "cart_mbit": _choice("cart_mbit", spec["cart_mbit"], (32, 64)),
        "cart_stage_sectors": _choice("cart_stage_sectors", spec["cart_stage_sectors"], (4, 8, 16)),
        "hot_promotion": _boolean("hot_promotion", spec["hot_promotion"]),
        "near_clip": _boolean("near_clip", spec["near_clip"]),
        "bsp_order": _boolean("bsp_order", spec["bsp_order"]),
        "polygon_tier": _choice("polygon_tier", spec["polygon_tier"], (0, 1, 2)),
        "fragment_mode": _boolean("fragment_mode", spec["fragment_mode"]),
    }
    return values


def _validate_compiler_config(spec: Mapping[str, Any]) -> dict[str, int]:
    missing = [field for field in COMPILER_CONFIG_FIELDS if field not in spec]
    if missing:
        raise ValueError(
            "missing compiler config scalar(s): " + ", ".join(missing)
        )
    return {
        "atan2_variant": _choice("atan2_variant", spec["atan2_variant"], (1, 2)),
        "demo_path": _boolean("demo_path", spec["demo_path"]),
        "demo_view_radius": _integer(
            "demo_view_radius", spec["demo_view_radius"], 1, 0xFFFFFFFF
        ),
        "slave_render": _boolean("slave_render", spec["slave_render"]),
        "camera_idle_start_tick": _integer(
            "camera_idle_start_tick", spec["camera_idle_start_tick"], 0, 0xFFFFFFFF
        ),
        "camera_idle_discovery": _boolean(
            "camera_idle_discovery", spec["camera_idle_discovery"]
        ),
        "camera_range_capture": _boolean(
            "camera_range_capture", spec["camera_range_capture"]
        ),
        "bsp_fragment_flat": _boolean(
            "bsp_fragment_flat", spec["bsp_fragment_flat"]
        ),
        "fast3d_q16_trace": _boolean(
            "fast3d_q16_trace", spec["fast3d_q16_trace"]
        ),
        "experimental_skip_geo_walk": _boolean(
            "experimental_skip_geo_walk", spec["experimental_skip_geo_walk"]
        ),
        "object_pool_capacity": _integer(
            "object_pool_capacity", spec["object_pool_capacity"], 1, 240
        ),
    }


def _validate_features(features: Any) -> tuple[dict[str, int], int]:
    if not isinstance(features, Mapping):
        raise ValueError("features must be an object")
    if set(features) != set(FEATURE_BITS):
        missing = sorted(set(FEATURE_BITS) - set(features))
        extra = sorted(set(features) - set(FEATURE_BITS))
        raise ValueError(f"feature tuple mismatch; missing={missing}, extra={extra}")
    canonical = {
        name: _boolean(f"feature {name}", features[name])
        for name in FEATURE_BITS
    }
    bits = sum(FEATURE_BITS[name] for name, enabled in canonical.items() if enabled)
    return canonical, bits


def _sha256_file(field: str, descriptor: Any) -> str:
    if not isinstance(descriptor, Mapping):
        raise ValueError(f"{field} artifact descriptor is missing")
    path_value = descriptor.get("path")
    declared = descriptor.get("sha256")
    if not isinstance(path_value, str) or not path_value:
        raise ValueError(f"{field} artifact path is missing")
    if not isinstance(declared, str) or len(declared) != 64:
        raise ValueError(f"{field} declared SHA-256 is missing")
    path = Path(path_value)
    if not path.is_file():
        raise ValueError(f"{field} artifact is not a file: {path}")
    actual = hashlib.sha256(path.read_bytes()).hexdigest()
    if actual != declared.lower():
        raise ValueError(f"{field} artifact SHA-256 is stale: expected {declared}, got {actual}")
    return actual


def build_identity(spec: Mapping[str, Any]) -> BuiltIdentity:
    """Validate all inputs, hash their bytes, and create one canonical identity."""
    scalars = _validate_scalars(spec)
    compiler_config = _validate_compiler_config(spec)
    features, feature_bits = _validate_features(spec.get("features"))
    artifacts = spec.get("artifacts")
    if not isinstance(artifacts, Mapping):
        raise ValueError("artifacts object is missing")
    if set(artifacts) != set(ARTIFACT_HASH_FIELDS):
        missing = sorted(set(ARTIFACT_HASH_FIELDS) - set(artifacts))
        extra = sorted(set(artifacts) - set(ARTIFACT_HASH_FIELDS))
        raise ValueError(f"artifact identity mismatch; missing={missing}, extra={extra}")
    hashes = {
        field: _sha256_file(field, artifacts[field])
        for field in ARTIFACT_HASH_FIELDS
    }
    canonical_object = {
        "schema": "sm64-saturn-effective-config-v1",
        "features": features,
        **scalars,
        **compiler_config,
        "artifact_hashes": hashes,
    }
    canonical = json.dumps(
        canonical_object, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    ).encode("ascii")
    values: dict[str, Any] = {
        "magic": MAGIC,
        "version": VERSION,
        "size": IDENTITY_SIZE,
        "feature_bits": feature_bits,
        **scalars,
        "reserved0": 0,
        "reserved1": 0,
        **hashes,
        "effective_config_hash": hashlib.sha256(canonical).hexdigest(),
    }
    raw = pack_identity(values)
    validate_identity(raw)
    return BuiltIdentity(raw=raw, canonical_config=canonical, values=values)


def validate_spec_expectations(
    spec: Mapping[str, Any], expectations: Mapping[str, int]
) -> None:
    """Reject wrapper/spec drift before either C bytes or a label are emitted."""
    features, _bits = _validate_features(spec.get("features"))
    scalars = _validate_scalars(spec)
    compiler_config = _validate_compiler_config(spec)
    available = {**scalars, **compiler_config, **{
        f"features.{name}": value for name, value in features.items()
    }}
    unknown = sorted(set(expectations) - set(available))
    if unknown:
        raise ValueError("unknown identity expectation(s): " + ", ".join(unknown))
    for name, expected in expectations.items():
        if available[name] != expected:
            raise ValueError(
                f"compiled identity drift for {name}: "
                f"spec has {available[name]}, wrapper has {expected}"
            )


def _parse_expectations(arguments: list[str]) -> dict[str, int]:
    result: dict[str, int] = {}
    for argument in arguments:
        if "=" not in argument:
            raise ValueError(f"identity expectation must be NAME=INTEGER: {argument}")
        name, rendered = argument.split("=", 1)
        if not name or name in result:
            raise ValueError(f"duplicate or empty identity expectation: {name}")
        try:
            result[name] = int(rendered, 10)
        except ValueError as error:
            raise ValueError(f"identity expectation is not an integer: {argument}") from error
    return result


def pack_identity(values: Mapping[str, Any]) -> bytes:
    ordered = (
        values["magic"], values["version"], values["size"],
        values["feature_bits"], values["renderer_pipeline"], values["level_id"],
        values["area_id"], values["route_id"], values["route_replay_mode"],
        values["live_input_mode"], values["camera_route"],
        values["camera_variant"], values["diagnostic_mode"], values["reserved0"],
        values["bootstrap_ticks"], values["cart_mbit"],
        values["cart_stage_sectors"], values["hot_promotion"],
        values["near_clip"], values["bsp_order"], values["polygon_tier"],
        values["fragment_mode"], values["reserved1"],
        *(bytes.fromhex(str(values[field])) for field in HASH_FIELDS),
    )
    return IDENTITY_STRUCT.pack(*ordered)


def parse_identity(raw: bytes) -> dict[str, Any]:
    if len(raw) != IDENTITY_SIZE:
        raise ValueError(f"build identity has wrong size {len(raw)}, expected {IDENTITY_SIZE}")
    unpacked = IDENTITY_STRUCT.unpack(raw)
    names = (
        "magic", "version", "size", "feature_bits", "renderer_pipeline",
        "level_id", "area_id", "route_id", "route_replay_mode",
        "live_input_mode", "camera_route", "camera_variant", "diagnostic_mode",
        "reserved0", "bootstrap_ticks", "cart_mbit", "cart_stage_sectors",
        "hot_promotion", "near_clip", "bsp_order", "polygon_tier",
        "fragment_mode", "reserved1",
    )
    values = dict(zip(names, unpacked[:len(names)]))
    for field, value in zip(HASH_FIELDS, unpacked[len(names):]):
        values[field] = value.hex()
    return values


def validate_identity(raw: bytes, *, expected: bytes | None = None) -> dict[str, Any]:
    values = parse_identity(raw)
    if values["magic"] != MAGIC:
        raise ValueError("build identity has wrong magic")
    if values["version"] != VERSION:
        raise ValueError("build identity has wrong version")
    if values["size"] != IDENTITY_SIZE:
        raise ValueError("build identity declares wrong size")
    if values["feature_bits"] & ~KNOWN_FEATURE_BITS:
        raise ValueError("build identity has unknown feature bits")
    _choice("renderer_pipeline", values["renderer_pipeline"], (2, 3, 4))
    _boolean("route_replay_mode", values["route_replay_mode"])
    _boolean("live_input_mode", values["live_input_mode"])
    _choice("camera_route", values["camera_route"], (0, 1))
    _choice("camera_variant", values["camera_variant"], (1, 2, 3))
    _choice("diagnostic_mode", values["diagnostic_mode"], (0, 1, 2))
    _choice("bootstrap_ticks", values["bootstrap_ticks"], (0, 600, 1200, 2000))
    _choice("cart_mbit", values["cart_mbit"], (32, 64))
    _choice("cart_stage_sectors", values["cart_stage_sectors"], (4, 8, 16))
    for field in ("hot_promotion", "near_clip", "bsp_order", "fragment_mode"):
        _boolean(field, values[field])
    _choice("polygon_tier", values["polygon_tier"], (0, 1, 2))
    if values["reserved0"] != 0 or values["reserved1"] != 0:
        raise ValueError("build identity reserved fields must be zero")
    for field in HASH_FIELDS:
        if values[field] == "00" * 32:
            raise ValueError(f"build identity {field} is absent")
    if expected is not None:
        validate_identity(expected)
        if raw != expected:
            raise ValueError("loaded build identity tuple differs from ELF identity")
    return values


def identity_label(raw: bytes, *, expected: bytes | None = None) -> str:
    values = validate_identity(raw, expected=expected)
    feature = "".join(
        "1" if values["feature_bits"] & FEATURE_BITS[name] else "0"
        for name in FEATURE_BITS
    )
    return (
        f"feat{feature}-pipe{values['renderer_pipeline']}-l{values['level_id']}-"
        f"a{values['area_id']}-route{values['route_id']}-"
        f"replay{values['route_replay_mode']}-live{values['live_input_mode']}-"
        f"boot{values['bootstrap_ticks']}-cam{values['camera_route']}v{values['camera_variant']}-"
        f"diag{values['diagnostic_mode']}-cart{values['cart_mbit']}-"
        f"stage{values['cart_stage_sectors']}-hot{values['hot_promotion']}-"
        f"clip{values['near_clip']}-bsp{values['bsp_order']}-"
        f"poly{values['polygon_tier']}-frag{values['fragment_mode']}-"
        f"cfg{values['effective_config_hash'][:12]}"
    )


def identity_directory_tag(raw: bytes, *, expected: bytes | None = None) -> str:
    """Return the short, validated object-directory discriminator.

    The full label is intentionally descriptive and remains an emitted build
    artifact.  Yaul derives object names from their full paths, so using that
    label as a Windows worktree directory can exceed the filesystem component
    limit before the compiler starts.
    """
    values = validate_identity(raw, expected=expected)
    return "id-" + values["effective_config_hash"][:16]


def emit_c_include(raw: bytes) -> str:
    values = validate_identity(raw)
    hashes = {
        field: ", ".join(f"0x{byte:02x}" for byte in bytes.fromhex(values[field]))
        for field in HASH_FIELDS
    }
    return "\n".join((
        "/* Generated by tools/saturn/gen_build_identity.py; do not edit. */",
        "#define SATURN_BUILD_IDENTITY_INITIALIZER { \\",
        f"  0x{values['magic']:08x}U, {values['version']}U, {values['size']}U, \\",
        f"  0x{values['feature_bits']:08x}U, {values['renderer_pipeline']}U, {values['level_id']}U, {values['area_id']}U, {values['route_id']}U, \\",
        f"  {values['route_replay_mode']}U, {values['live_input_mode']}U, {values['camera_route']}U, {values['camera_variant']}U, {values['diagnostic_mode']}U, 0U, \\",
        f"  {values['bootstrap_ticks']}U, {values['cart_mbit']}U, {values['cart_stage_sectors']}U, {values['hot_promotion']}U, {values['near_clip']}U, \\",
        f"  {values['bsp_order']}U, {values['polygon_tier']}U, {values['fragment_mode']}U, 0U, \\",
        *(f"  {{ {hashes[field]} }}, \\" for field in HASH_FIELDS[:-1]),
        f"  {{ {hashes[HASH_FIELDS[-1]]} }} \\",
        "}",
        "",
    ))


def _write(path: Path, data: str | bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(data, bytes):
        path.write_bytes(data)
    else:
        path.write_text(data, encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--spec", type=Path, required=True)
    parser.add_argument("--output-binary", type=Path)
    parser.add_argument("--output-c-include", type=Path)
    parser.add_argument("--output-json", type=Path)
    parser.add_argument("--output-label", type=Path)
    parser.add_argument(
        "--print-directory-tag", action="store_true",
        help="print the validated short object-directory tag",
    )
    parser.add_argument(
        "--expect", action="append", default=[], metavar="NAME=INTEGER",
        help="require one spec scalar/feature to match its build-wrapper value",
    )
    args = parser.parse_args()
    spec = json.loads(args.spec.read_text(encoding="utf-8"))
    validate_spec_expectations(spec, _parse_expectations(args.expect))
    built = build_identity(spec)
    label = identity_label(built.raw)
    if args.print_directory_tag:
        if any((args.output_binary, args.output_c_include, args.output_json,
                args.output_label)):
            parser.error("--print-directory-tag cannot be combined with output files")
        print(identity_directory_tag(built.raw))
        return 0
    if args.output_binary:
        _write(args.output_binary, built.raw)
    if args.output_c_include:
        _write(args.output_c_include, emit_c_include(built.raw))
    if args.output_json:
        manifest = {"schema": "sm64-saturn-build-identity-v1", "label": label,
                    "identity_sha256": hashlib.sha256(built.raw).hexdigest(),
                    "identity": parse_identity(built.raw)}
        _write(args.output_json, json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    if args.output_label:
        _write(args.output_label, label + "\n")
    if not any((args.output_binary, args.output_c_include, args.output_json,
                args.output_label)):
        print(label)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
