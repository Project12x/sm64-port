#!/usr/bin/env python3
"""Validate two deterministic sourceboot-route capture reports."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path
from typing import Any

MAGIC = 0x53425232
VERSION = 2
PROBE_BYTES = 21 * 4
PROBE_FIELDS = (
    "magic", "version", "replay_ticks", "global_timer", "mario_action",
    "mario_pos_x_bits", "mario_pos_y_bits", "mario_pos_z_bits", "camera_mode",
    "triangles_transformed", "triangles_vdp1_emitted", "fault_flags",
    "command_capacity_rejects", "frame_serial", "sim_frt_ticks_accum",
    "render_frt_ticks_accum", "render_frt_ticks_last", "master_wait_ticks",
    "slave_busy_ticks", "slave_jobs_completed", "slave_timeouts",
)


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
    if not isinstance(schema, dict) or schema.get("version") not in (
            "sourceboot-route-v3",):
        raise ValueError("route must declare the sourceboot-route-v1 or v2 report schema")
    for name in ("required_report_fields", "required_probe_fields"):
        if not isinstance(schema.get(name), list) or not all(
                isinstance(field, str) for field in schema[name]):
            raise ValueError(f"route report_schema.{name} must be a list of field names")
    if tuple(schema["required_probe_fields"]) != PROBE_FIELDS:
        raise ValueError("route report_schema.required_probe_fields must match the v2 probe")
    return route


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
        values = struct.unpack(">21I", bytes(data[:PROBE_BYTES]))
        probe = dict(zip(PROBE_FIELDS, values, strict=True))
        if probe["magic"] == MAGIC and probe["version"] == VERSION:
            return probe
    if not saw_data:
        raise ValueError("report lacks a route probe_window")
    raise ValueError("route checkpoint is absent or has an unsupported version")


def compare_reports(left: dict[str, Any], right: dict[str, Any], route: dict[str, Any]) -> dict[str, Any]:
    for label, report in (("left", left), ("right", right)):
        missing = [field for field in route["report_schema"]["required_report_fields"]
                   if field not in report]
        if missing:
            raise ValueError(f"{label} report lacks required schema fields: {', '.join(missing)}")
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
    errors: list[str] = []
    expected_ticks = route["checkpoint_tick"]
    for label, probe in (("left", first), ("right", second)):
        if probe["replay_ticks"] != expected_ticks:
            errors.append(f"{label} replay_ticks={probe['replay_ticks']}, expected {expected_ticks}")
        if probe["triangles_vdp1_emitted"] == 0:
            errors.append(f"{label} captured no complete VDP1 frame")
        for field in ("fault_flags", "command_capacity_rejects"):
            if probe[field] != 0:
                errors.append(f"{label} {field}={probe[field]}")
    checkpoint_fields = (
        "global_timer", "mario_action", "mario_pos_x_bits", "mario_pos_y_bits",
        "mario_pos_z_bits", "camera_mode",
    )
    signatures = [hashlib.sha256(b"".join(
        probe[field].to_bytes(4, "big") for field in checkpoint_fields)).hexdigest()
        for probe in (first, second)]
    if signatures[0] != signatures[1]:
        errors.append("source checkpoint signature differs")
    tolerance = route["primitive_count_tolerance"]
    renderer_deltas = {}
    for field in ("triangles_transformed", "triangles_vdp1_emitted"):
        delta = abs(first[field] - second[field])
        renderer_deltas[field] = delta
        if schema.get("compare_renderer_counters", True) and delta > tolerance:
            errors.append(f"{field} differs beyond tolerance {tolerance}")
    telemetry_deltas = {
        field: abs(first[field] - second[field]) for field in PROBE_FIELDS[13:]
    }
    return {
        "route_version": route["route_version"],
        "report_schema_version": route["report_schema"]["version"],
        "simulation_ticks": expected_ticks,
        "left": first,
        "right": second,
        "left_checkpoint_sha256": signatures[0],
        "right_checkpoint_sha256": signatures[1],
        "renderer_counter_deltas": renderer_deltas,
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
        result = compare_reports(
            json.loads(args.left.read_text(encoding="utf-8")),
            json.loads(args.right.read_text(encoding="utf-8")), load_route(args.route))
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(str(error))
    text = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(text, encoding="utf-8")
    print(text, end="")
    return 0 if result["deterministic"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
