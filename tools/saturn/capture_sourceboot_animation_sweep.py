#!/usr/bin/env python3
"""Validate the bounded 209-ID production animation sweep telemetry."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from build_sourceboot_variant import artifact_identity


PRODUCTION_EVALUATOR = "sm64_saturn_mario_actor_pose"


def validate_sweep_report(report: dict[str, Any], artifact: dict[str, Any]) -> dict[str, Any]:
    if report.get("schema") != "sm64-saturn-animation-sweep-v1":
        raise ValueError("animation sweep schema is invalid")
    if report.get("diagnostic_mode") != 1:
        raise ValueError("animation sweep is promotable only with diagnostic_mode=1")
    if report.get("evaluator_symbol") != PRODUCTION_EVALUATOR:
        raise ValueError("animation sweep did not use the production evaluator")
    if report.get("target_fault"):
        raise ValueError("animation sweep target fault")
    if report.get("fallback_count", 0) != 0 or report.get("corrupt_bounds_count", 0) != 0:
        raise ValueError("animation sweep reported fallback or corrupt bounds")
    ids = report.get("ids")
    if not isinstance(ids, list) or any(not isinstance(value, int) or not 0 <= value < 209 for value in ids):
        raise ValueError("animation sweep ID set is invalid")
    if len(ids) != len(set(ids)):
        raise ValueError("animation sweep contains duplicate IDs")
    if sorted(ids) != list(range(209)) or report.get("seen_count") != 209:
        raise ValueError("animation sweep did not cover all 209 IDs")
    expected = artifact.get("artifacts", {})
    if report.get("elf_sha256") != expected.get("elf", {}).get("sha256") or \
       report.get("cue_sha256") != expected.get("cue", {}).get("sha256") or \
       report.get("iso_sha256") != expected.get("iso", {}).get("sha256"):
        raise ValueError("animation sweep artifact identity mismatch")
    return report


def capture(*, artifact_path: Path, output: Path, input_report: Path | None = None) -> dict[str, Any]:
    artifact = artifact_identity(artifact_path)
    if input_report is None:
        raise RuntimeError("Ymir sweep transport is not configured; provide captured telemetry")
    report = json.loads(input_report.read_text(encoding="utf-8"))
    validated = validate_sweep_report(report, artifact)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(validated, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return validated


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ymir", type=Path, required=True)
    parser.add_argument("--ipl", type=Path, required=True)
    parser.add_argument("--artifact", type=Path, required=True)
    parser.add_argument("--startup-vblanks", type=int, default=600)
    parser.add_argument("--max-vblanks", type=int, default=4096)
    parser.add_argument("--input-report", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    capture(artifact_path=args.artifact, output=args.output,
            input_report=args.input_report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
