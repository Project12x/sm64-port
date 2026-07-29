#!/usr/bin/env python3
"""Regression coverage for the expected-failure atan2 mutation runner."""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent
VERIFY = ROOT / "verify_engine_atan2_mutation.py"


class VerifyEngineAtan2MutationTests(unittest.TestCase):
    def run_wrapper(self, *, deliberately_escape: bool = False) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as temporary:
            child = Path(temporary) / "child.py"
            child.write_text(
                "import os\n"
                "expected = os.environ.get('SM64_SATURN_EXPECT_ATAN2_Q16_MUTATION') == '1'\n"
                "mutated = os.environ.get('SM64_SATURN_TEST_MUTATE_ATAN2_Q16') == '1'\n"
                "raise SystemExit(0 if expected and mutated else 1)\n",
                encoding="utf-8",
            )
            return subprocess.run(
                [
                    sys.executable, str(VERIFY),
                    "--python", sys.executable,
                    "--test", str(child),
                    *(["--deliberately-escape"] if deliberately_escape else []),
                ],
                text=True,
                capture_output=True,
                check=False,
            )

    def test_caught_mutation_returns_success(self) -> None:
        completed = self.run_wrapper()
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("mutation caught", completed.stdout)

    def test_escaped_mutation_returns_nonzero(self) -> None:
        completed = self.run_wrapper(deliberately_escape=True)
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("mutation escaped", completed.stderr)


if __name__ == "__main__":
    unittest.main()
