#!/usr/bin/env python3
"""Validate two deterministic sourceboot-route capture reports."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import struct
from pathlib import Path
from typing import Any

MAGIC = 0x53425234
VERSION = 4
PROBE_FIELDS = (
    "magic", "version", "atan2_variant", "replay_ticks", "global_timer", "mario_action",
    "mario_pos_x_bits", "mario_pos_y_bits", "mario_pos_z_bits",
    "mario_face_angle_x", "mario_face_angle_y", "mario_face_angle_z",
    "camera_pos_x_bits", "camera_pos_y_bits", "camera_pos_z_bits",
    "camera_mode", "triangles_transformed", "triangles_emitted",
    "triangles_vdp1_emitted", "reject_near_far", "reject_backface",
    "reject_degenerate", "reject_vertex_range", "reject_command_capacity",
    "reject_vdp1_arena_capacity", "reject_w_nonpositive", "reject_z_near",
    "reject_z_far", "reject_offscreen", "reject_span",
    "reject_w_nonpositive_overflow_suspect", "fault_flags", "frame_serial",
    "sim_frt_ticks_accum", "render_frt_ticks_accum",
    "render_frt_ticks_last", "master_wait_ticks", "slave_busy_ticks",
    "slave_jobs_completed", "slave_timeouts",
)
PROBE_BYTES = len(PROBE_FIELDS) * 4
REJECT_FIELDS = (
    "reject_near_far", "reject_backface", "reject_degenerate",
    "reject_vertex_range", "reject_command_capacity",
    "reject_vdp1_arena_capacity", "reject_w_nonpositive", "reject_z_near",
    "reject_z_far", "reject_offscreen", "reject_span",
    "reject_w_nonpositive_overflow_suspect",
)
EXACT_BEHAVIOR_FIELDS = (
    "global_timer", "mario_action", "mario_face_angle_x",
    "mario_face_angle_y", "mario_face_angle_z", "camera_pos_x_bits",
    "camera_pos_y_bits", "camera_pos_z_bits", "camera_mode",
    "triangles_emitted",
)
SHA256_RE = re.compile(r"[0-9a-f]{64}")


def load_route(path: Path) -> dict[str, Any]:
    route = json.loads(path.read_text(encoding="utf-8"))
    if route.get("route_version") != "bob-parity-v1":
        raise ValueError("route_version must be bob-parity-v1")
    samples = route.get("samples")
    if not isinstance(samples, list) or not samples:
        raise ValueError("route must contain nonempty samples")
    total = 0
    for sample in samples:
        if not isinstance(sample, dict):
            raise ValueError("every route sample must be an object")
        for name, lower, upper in (("ticks", 1, 65535), ("stick_x", -80, 80),
                                   ("stick_y", -80, 80), ("buttons", 0, 65535)):
            value = sample.get(name)
            if not isinstance(value, int) or not lower <= value <= upper:
                raise ValueError(f"route sample {name} must be an integer in {lower}..{upper}")
        total += sample["ticks"]
    if total != route.get("simulation_ticks") or total != route.get("checkpoint_tick"):
        raise ValueError("route tick total must equal simulation_ticks and checkpoint_tick")
    schema = route.get("report_schema")
    if not isinstance(schema, dict) or schema.get("version") != "sourceboot-route-v6":
        raise ValueError("route must declare the sourceboot-route-v6 report schema")
    for name in ("required_report_fields", "required_probe_fields"):
        if not isinstance(schema.get(name), list) or not all(
                isinstance(field, str) for field in schema[name]):
            raise ValueError(f"route report_schema.{name} must be a list of field names")
    if tuple(schema["required_probe_fields"]) != PROBE_FIELDS:
        raise ValueError("route report_schema.required_probe_fields must match the SBR4 probe")
    gate = schema.get("behavioral_gate")
    if not isinstance(gate, dict) or gate.get("mario_position_linf_exclusive") != 1.0 \
            or gate.get("reject_relative_tolerance") != 0.01:
        raise ValueError("route must declare the Stage 2 behavioral gate")
    return route


def validate_artifacts(report: dict[str, Any], label: str = "report") -> None:
    artifacts = report.get("artifacts")
    if not isinstance(artifacts, dict):
        raise ValueError(f"{label} lacks artifact identities")
    for name in ("elf", "image"):
        artifact = artifacts.get(name)
        if not isinstance(artifact, dict):
            raise ValueError(f"{label} lacks artifact {name}")
        digest = artifact.get("sha256")
        if not isinstance(digest, str) or SHA256_RE.fullmatch(digest.lower()) is None:
            raise ValueError(f"{label} artifact {name} sha256 is invalid")
        if not isinstance(artifact.get("size"), int) or artifact["size"] <= 0:
            raise ValueError(f"{label} artifact {name} size is invalid")


def decode_probe(report: dict[str, Any]) -> dict[str, int]:
    # Paired captures keep the Fast3D profile in probe_window and the route
    # block in extra_probe_window. Prefer the legacy primary window, but fall
    # back to the explicitly named extra window when it is not a route block.
    windows = [report.get("probe_window"), report.get("extra_probe_window")]
    saw_data = False
    for window in windows:
        if not isinstance(window, dict) or not isinstance(window.get("data"), list):
            continue
        saw_data = True
        data = window["data"]
        if len(data) < PROBE_BYTES or not all(
                isinstance(byte, int) and 0 <= byte <= 255 for byte in data):
            continue
        values = struct.unpack(f">{len(PROBE_FIELDS)}I", bytes(data[:PROBE_BYTES]))
        probe = dict(zip(PROBE_FIELDS, values, strict=True))
        if probe["magic"] == MAGIC and probe["version"] == VERSION:
            decoded = window.get("decoded")
            if decoded is not None and decoded != probe:
                raise ValueError("decoded route probe does not match raw bytes")
            return probe
    if not saw_data:
        raise ValueError("report lacks a route probe_window")
    raise ValueError("route checkpoint is absent or has an unsupported version")


def _float_from_bits(bits: int) -> float:
    value = struct.unpack(">f", bits.to_bytes(4, "big"))[0]
    if not math.isfinite(value):
        raise ValueError("route position contains a non-finite float")
    return value


def compare_reports(left: dict[str, Any], right: dict[str, Any], route: dict[str, Any]) -> dict[str, Any]:
    for label, report in (("left", left), ("right", right)):
        missing = [field for field in route["report_schema"]["required_report_fields"]
                   if field not in report]
        if missing:
            raise ValueError(f"{label} report lacks required schema fields: {', '.join(missing)}")
        validate_artifacts(report, label)
    errors: list[str] = []
    if left["capture_role"] != "legacy" or right["capture_role"] != "q16":
        errors.append("capture roles must be ordered legacy then q16")
    for artifact_name in ("elf", "image"):
        if left["artifacts"][artifact_name]["sha256"] == right["artifacts"][artifact_name]["sha256"]:
            errors.append(f"legacy and q16 artifact {artifact_name} identities must differ")
    schema = route["report_schema"]
    if schema.get("compare_degradation", False):
        left_degradation = left["degradation"]
        right_degradation = right["degradation"]
        if not isinstance(left_degradation, dict) or not isinstance(right_degradation, dict):
            raise ValueError("degradation must be an object in both reports")
        if any(left_degradation.get(field) is None for field in ("view_radius", "poly_tier")) or \
                any(right_degradation.get(field) is None for field in ("view_radius", "poly_tier")):
            raise ValueError("degradation settings must be explicitly declared in both reports")
        if left_degradation != right_degradation:
            raise ValueError("degradation settings differ between reports")
    first = decode_probe(left)
    second = decode_probe(right)
    expected_ticks = route["checkpoint_tick"]
    for label, probe in (("left", first), ("right", second)):
        if probe["replay_ticks"] != expected_ticks:
            errors.append(f"{label} replay_ticks={probe['replay_ticks']}, expected {expected_ticks}")
        if probe["triangles_vdp1_emitted"] == 0:
            errors.append(f"{label} captured no complete VDP1 frame")
        expected_variant = 1 if label == "left" else 2
        if probe["atan2_variant"] != expected_variant:
            errors.append(
                f"{label} raw atan2 variant={probe['atan2_variant']}, "
                f"expected {expected_variant}"
            )
        for field in (
            "fault_flags", "reject_command_capacity",
            "reject_vdp1_arena_capacity",
        ):
            if probe[field] != 0:
                errors.append(f"{label} {field}={probe[field]}")
        try:
            for field in (
                "camera_pos_x_bits", "camera_pos_y_bits", "camera_pos_z_bits",
            ):
                _float_from_bits(probe[field])
            if probe["camera_mode"] == 0xFFFFFFFF:
                raise ValueError("camera mode sentinel")
        except ValueError:
            errors.append(f"{label} camera state is missing or non-finite")
    exact_deltas = {}
    for field in EXACT_BEHAVIOR_FIELDS:
        delta = abs(first[field] - second[field])
        exact_deltas[field] = delta
        if delta != 0:
            errors.append(f"{field} differs")

    positions = [
        tuple(_float_from_bits(probe[field]) for field in (
            "mario_pos_x_bits", "mario_pos_y_bits", "mario_pos_z_bits"
        ))
        for probe in (first, second)
    ]
    mario_position_linf = max(abs(a - b) for a, b in zip(*positions, strict=True))
    position_limit = schema["behavioral_gate"]["mario_position_linf_exclusive"]
    if not mario_position_linf < position_limit:
        errors.append(
            f"Mario position L-infinity divergence is not < {position_limit}"
        )

    reject_tolerance = schema["behavioral_gate"]["reject_relative_tolerance"]
    reject_deltas = {}
    reject_relative_deltas = {}
    for field in REJECT_FIELDS:
        before = first[field]
        delta = abs(before - second[field])
        relative = 0.0 if delta == 0 else (math.inf if before == 0 else delta / before)
        reject_deltas[field] = delta
        reject_relative_deltas[field] = relative
        if relative > reject_tolerance:
            errors.append(f"{field} differs beyond +/-{reject_tolerance:.0%}")

    renderer_deltas = {}
    for field in ("triangles_transformed", "triangles_vdp1_emitted"):
        delta = abs(first[field] - second[field])
        renderer_deltas[field] = delta
    telemetry_deltas = {
        field: abs(first[field] - second[field])
        for field in (
            "frame_serial", "sim_frt_ticks_accum", "render_frt_ticks_accum",
            "render_frt_ticks_last", "master_wait_ticks", "slave_busy_ticks",
            "slave_jobs_completed", "slave_timeouts",
        )
    }
    checkpoint_fields = (
        "global_timer", "mario_action", "mario_pos_x_bits", "mario_pos_y_bits",
        "mario_pos_z_bits", "mario_face_angle_x", "mario_face_angle_y",
        "mario_face_angle_z", "camera_pos_x_bits", "camera_pos_y_bits",
        "camera_pos_z_bits", "camera_mode",
    )
    signatures = [hashlib.sha256(b"".join(
        probe[field].to_bytes(4, "big") for field in checkpoint_fields)).hexdigest()
        for probe in (first, second)]
    return {
        "route_version": route["route_version"],
        "report_schema_version": route["report_schema"]["version"],
        "simulation_ticks": expected_ticks,
        "capture_roles": {
            "left": left["capture_role"],
            "right": right["capture_role"],
        },
        "left": first,
        "right": second,
        "left_checkpoint_sha256": signatures[0],
        "right_checkpoint_sha256": signatures[1],
        "renderer_counter_deltas": renderer_deltas,
        "behavioral_gate": {
            "mario_position_linf": mario_position_linf,
            "mario_position_linf_exclusive": position_limit,
            "exact_field_deltas": exact_deltas,
            "reject_counter_deltas": reject_deltas,
            "reject_counter_relative_deltas": reject_relative_deltas,
            "reject_relative_tolerance": reject_tolerance,
        },
        "telemetry_deltas": telemetry_deltas,
        "degradation": left.get("degradation"),
        "deterministic": not errors,
        "errors": errors,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("left", type=Path)
    parser.add_argument("right", type=Path)
    parser.add_argument("--route", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    try:
        left_bytes = args.left.read_bytes()
        right_bytes = args.right.read_bytes()
        result = compare_reports(
            json.loads(left_bytes),
            json.loads(right_bytes), load_route(args.route))
        result["left_capture_sha256"] = hashlib.sha256(left_bytes).hexdigest()
        result["right_capture_sha256"] = hashlib.sha256(right_bytes).hexdigest()
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(str(error))
    text = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(text, encoding="utf-8")
    print(text, end="")
    return 0 if result["deterministic"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
