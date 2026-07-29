#!/usr/bin/env python3
"""Validate two complete SMC1 corpus captures and emit an atan2 fixture."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any

from capture_math_route import MATH_BYTES, decode_math
from compare_route_reports import decode_probe, validate_artifacts


MAGIC = 0x534D4331
VERSION = 2
EXPECTED_TICKS = 2000
CORPUS_SLOTS = 64
FIELDS = (
    "magic", "version", "replay_ticks", "atan2s_calls", "atan2_lookup_calls",
    "atan2s_samples", "atan2_lookup_samples",
)


def load(path: Path) -> dict[str, Any]:
    report = json.loads(path.read_text(encoding="utf-8"))
    if report.get("evidence_kind") != "ymir-sourceboot-smc1-math-route" or report.get("schema_version") != VERSION:
        raise ValueError(f"{path} is not an SMC1 capture v{VERSION}")
    return report


def decoded(report: dict[str, Any]) -> dict[str, Any]:
    window = report.get("math_window")
    if not isinstance(window, dict):
        raise ValueError("capture is missing raw SMC1 bytes")
    raw = window.get("data")
    if not isinstance(raw, list) or len(raw) < MATH_BYTES:
        raise ValueError("capture is missing raw SMC1 bytes")
    try:
        raw_value = decode_math(raw)
    except ValueError as error:
        raise ValueError(f"capture has invalid raw SMC1 bytes: {error}") from error
    value = window.get("decoded")
    if not isinstance(value, dict) or any(not isinstance(value.get(field), int) for field in FIELDS):
        raise ValueError("capture is missing decoded SMC1 fields")
    corpus = value.get("corpus")
    if not isinstance(corpus, list):
        raise ValueError("capture is missing the SMC1 corpus")
    for sample in corpus:
        if not isinstance(sample, dict) or sample.get("function") not in ("atan2s", "atan2_lookup") or any(
            not isinstance(sample.get(field), int) for field in ("y_bits", "x_bits", "result")
        ):
            raise ValueError("SMC1 corpus contains an invalid sample")
    result = {field: value[field] for field in FIELDS}
    result["corpus"] = corpus
    if result != raw_value:
        raise ValueError("decoded SMC1 does not match raw bytes")
    return raw_value


def validate_corpus(value: dict[str, Any]) -> None:
    for function, count_field in (("atan2s", "atan2s_samples"), ("atan2_lookup", "atan2_lookup_samples")):
        count = value[count_field]
        if count != CORPUS_SLOTS:
            raise ValueError(f"SMC1 {function} corpus is incomplete ({count}/{CORPUS_SLOTS})")
        observed = [sample for sample in value["corpus"] if sample["function"] == function]
        if len(observed) != CORPUS_SLOTS:
            raise ValueError(f"SMC1 corpus is missing {function} samples")
        if any(sample["result"] > 0xFFFF for sample in observed):
            raise ValueError(f"SMC1 {function} corpus contains a non-u16 result")


def capture_identity(path: Path) -> dict[str, str]:
    return {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run1", type=Path)
    parser.add_argument("run2", type=Path)
    parser.add_argument("--fixture", type=Path, required=True)
    args = parser.parse_args()
    try:
        reports = [load(args.run1), load(args.run2)]
        values = [decoded(report) for report in reports]
        if values[0]["magic"] != MAGIC or values[0]["version"] != VERSION:
            raise ValueError("run 1 does not contain SMC1 v1")
        if values[0] != values[1]:
            raise ValueError("SMC1 fields differ between deterministic replay runs")
        if values[0]["replay_ticks"] != EXPECTED_TICKS:
            raise ValueError(f"SMC1 capture is not at the frozen {EXPECTED_TICKS}-tick endpoint")
        if any(values[0][field] == 0 for field in ("atan2s_calls", "atan2_lookup_calls")):
            raise ValueError("both atan2 counters must be nonzero")
        validate_corpus(values[0])
        artifacts = [report.get("artifacts", {}) for report in reports]
        for index, report in enumerate(reports, start=1):
            validate_artifacts(report, f"run {index}")
        if artifacts[0] != artifacts[1]:
            raise ValueError("ELF/CUE artifacts differ between replay runs")
        route = [
            decode_probe({"probe_window": report.get("route_window")})
            for report in reports
        ]
        if route[0] != route[1] or route[0]["replay_ticks"] != EXPECTED_TICKS:
            raise ValueError(f"source route checkpoint is not stable at tick {EXPECTED_TICKS}")
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(str(error))

    fixture = {
        "schema": "sm64-saturn-atan2-route-fixture",
        "version": 1,
        "route": "bob-parity-v1",
        "capture_contract": {
            "checkpoint_tick": EXPECTED_TICKS,
            "smc_magic": "SMC1",
            "smc_version": VERSION,
            "two_run_fields_must_match": list(FIELDS),
            "input_encoding": "IEEE-754 binary32 bits, y then x; u16 result",
            "corpus_slots_per_function": CORPUS_SLOTS,
        },
        "artifacts": artifacts[0],
        "observed": values[0],
        "samples": values[0]["corpus"],
        "evidence": {"run1": capture_identity(args.run1), "run2": capture_identity(args.run2)},
        "limitations": [
            "The corpus is a bounded ring, so it is deterministic evidence rather than an exhaustive trace.",
            "This same-variant integrity check does not replace the separate host differential and role-bound positional-divergence A/B gates.",
        ],
    }
    args.fixture.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(fixture, indent=2, sort_keys=True) + "\n"
    args.fixture.write_text(text, encoding="utf-8")
    print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
