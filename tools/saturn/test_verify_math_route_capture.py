#!/usr/bin/env python3
"""Regression coverage for the SMC1 route-capture fixture contract."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent
VERIFY = ROOT / "verify_math_route_capture.py"


def capture() -> dict[str, object]:
    corpus = [
        {"function": function, "y_bits": 0x3F800000 + index, "x_bits": 0x40000000 + index,
         "result": (4096 if function == "atan2s" else 61440) + index}
        for function in ("atan2s", "atan2_lookup")
        for index in range(64)
    ]
    math = {
        "magic": 0x534D4331,
        "version": 2,
        "replay_ticks": 2000,
        "atan2s_calls": 42,
        "atan2_lookup_calls": 42,
        "atan2s_samples": 64,
        "atan2_lookup_samples": 64,
        "corpus": corpus,
    }
    return {
        "evidence_kind": "ymir-sourceboot-smc1-math-route",
        "schema_version": 2,
        "artifacts": {"game": {"sha256": "game"}, "elf": {"sha256": "elf"}},
        "route_window": {"decoded": {"replay_ticks": 2000}},
        "math_window": {"decoded": math},
    }


class VerifyMathRouteCaptureTests(unittest.TestCase):
    def run_verifier(self, first: dict[str, object], second: dict[str, object]) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            run1, run2, fixture = root / "run1.json", root / "run2.json", root / "fixture.json"
            run1.write_text(json.dumps(first), encoding="utf-8")
            run2.write_text(json.dumps(second), encoding="utf-8")
            completed = subprocess.run(
                [sys.executable, str(VERIFY), str(run1), str(run2), "--fixture", str(fixture)],
                text=True,
                capture_output=True,
                check=False,
            )
            if completed.returncode == 0:
                completed.fixture = json.loads(fixture.read_text(encoding="utf-8"))  # type: ignore[attr-defined]
            return completed

    def test_accepts_complete_deterministic_corpus_at_2000_ticks(self) -> None:
        completed = self.run_verifier(capture(), capture())
        self.assertEqual(completed.returncode, 0, completed.stderr)
        fixture = completed.fixture  # type: ignore[attr-defined]
        self.assertEqual(fixture["capture_contract"]["checkpoint_tick"], 2000)
        self.assertEqual(fixture["samples"], capture()["math_window"]["decoded"]["corpus"])

    def test_rejects_missing_function_from_corpus(self) -> None:
        first = capture()
        first["math_window"]["decoded"]["corpus"] = [  # type: ignore[index]
            sample for sample in first["math_window"]["decoded"]["corpus"]  # type: ignore[index]
            if sample["function"] == "atan2s"
        ]
        completed = self.run_verifier(first, first)
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("atan2_lookup", completed.stderr)


if __name__ == "__main__":
    unittest.main()
