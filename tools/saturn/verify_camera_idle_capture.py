#!/usr/bin/env python3
"""Validate a report carrying a raw SCC1 camera-idle window."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any

from camera_acceptance_route import load_route_manifest
from camera_idle_contract import Scc1Capture, Scc1Error, decode_scc1, validate_scc1


def _window(report: dict[str, Any]) -> dict[str, Any]:
    for name in ("camera_idle_window", "scc1_window"):
        value = report.get(name)
        if isinstance(value, dict):
            return value
    raise ValueError("capture is missing raw SCC1 bytes")


def _raw_bytes(window: dict[str, Any]) -> bytes:
    data = window.get("data")
    if not isinstance(data, list) or not all(isinstance(byte, int) and 0 <= byte <= 255 for byte in data):
        raise ValueError("capture is missing raw SCC1 bytes")
    return bytes(data)


def decoded_scc1(capture: Scc1Capture) -> dict[str, Any]:
    return {
        "header": list(capture.header),
        "samples": [
            {"source_tick": sample.source_tick, "applied_input": sample.applied_input,
             "state_flags": sample.state_flags, "state_words": list(sample.state_words)}
            for sample in capture.samples
        ],
    }


def decode_capture_report(report: dict[str, Any]) -> Scc1Capture:
    if not isinstance(report, dict):
        raise ValueError("capture report must be an object")
    try:
        capture = decode_scc1(_raw_bytes(_window(report)))
    except Scc1Error as error:
        raise ValueError(f"capture has invalid raw SCC1 bytes: {error}") from error
    decoded = _window(report).get("decoded")
    if decoded is not None and decoded != decoded_scc1(capture):
        raise ValueError("decoded SCC1 does not match raw bytes")
    return capture


def verify_capture_report(report: dict[str, Any], *, route_path: Path,
                          expected_idle_start_tick: int) -> Scc1Capture:
    manifest = load_route_manifest(route_path)
    if manifest.get("route_id") != 2 or manifest.get("checkpoint_tick") != 2000:
        raise ValueError("route manifest is not bob-default-camera-v1")
    digest = report.get("route_manifest_sha256", report.get("route_manifest_digest"))
    if not isinstance(digest, str) or digest.lower() != hashlib.sha256(route_path.read_bytes()).hexdigest():
        raise ValueError("capture route-manifest digest does not match the selected route")
    role = report.get("capture_role")
    if not isinstance(role, str):
        raise ValueError("capture has no declared camera role")
    capture = decode_capture_report(report)
    try:
        validate_scc1(capture, expected_role=role,
                      expected_idle_start_tick=expected_idle_start_tick,
                      expected_route_id=manifest["route_id"])
    except Scc1Error as error:
        raise ValueError(str(error)) from error
    return capture


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("--route", type=Path, required=True)
    parser.add_argument("--idle-start-tick", type=int, required=True)
    args = parser.parse_args(argv)
    try:
        report = json.loads(args.report.read_text(encoding="utf-8"))
        capture = verify_capture_report(
            report, route_path=args.route, expected_idle_start_tick=args.idle_start_tick,
        )
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(str(error))
    print(json.dumps(decoded_scc1(capture), sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
