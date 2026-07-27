#!/usr/bin/env python3
"""Validate two deterministic sourceboot-route capture reports."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path
from typing import Any

MAGIC = 0x53425231
VERSION = 1
PROBE_BYTES = 13 * 4


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
    return route


def decode_probe(report: dict[str, Any]) -> dict[str, int]:
    window = report.get("probe_window")
    if not isinstance(window, dict) or not isinstance(window.get("data"), list):
        raise ValueError("report lacks a route probe_window")
    data = window["data"]
    if len(data) < PROBE_BYTES or not all(isinstance(byte, int) and 0 <= byte <= 255 for byte in data):
        raise ValueError(f"route probe must contain at least {PROBE_BYTES} byte values")
    values = struct.unpack(">13I", bytes(data[:PROBE_BYTES]))
    names = (
        "magic", "version", "replay_ticks", "global_timer", "mario_action",
        "mario_pos_x_bits", "mario_pos_y_bits", "mario_pos_z_bits", "camera_mode",
        "triangles_transformed", "triangles_vdp1_emitted", "fault_flags",
        "command_capacity_rejects",
    )
    probe = dict(zip(names, values, strict=True))
    if probe["magic"] != MAGIC or probe["version"] != VERSION:
        raise ValueError("route checkpoint is absent or has an unsupported version")
    return probe


def compare_reports(left: dict[str, Any], right: dict[str, Any], route: dict[str, Any]) -> dict[str, Any]:
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
    for field in ("triangles_transformed", "triangles_vdp1_emitted"):
        if abs(first[field] - second[field]) > tolerance:
            errors.append(f"{field} differs beyond tolerance {tolerance}")
    return {
        "route_version": route["route_version"],
        "simulation_ticks": expected_ticks,
        "left": first,
        "right": second,
        "left_checkpoint_sha256": signatures[0],
        "right_checkpoint_sha256": signatures[1],
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
