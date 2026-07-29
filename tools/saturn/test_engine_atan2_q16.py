#!/usr/bin/env python3
"""Differential gate for the TARGET_SATURN atan2 Q16 seam.

The fixture is emitted from two independent 2,000-tick BOB replay captures.
Each value stays in its captured IEEE-754 representation until the compiled
engine path decodes it, so this test exercises the real guarded source rather
than a reimplementation in Python.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tools/saturn/fixtures/bob_parity_v1_atan2_smc1_v1.json"
RUNNER = ROOT / "tools/saturn/engine_atan2_q16_fixture_test.c"


class EngineAtan2Q16FixtureTests(unittest.TestCase):
    def test_target_q16_seam_matches_every_captured_atan2_result(self) -> None:
        fixture = json.loads(FIXTURE.read_text(encoding="utf-8"))
        self.assertEqual(fixture["capture_contract"]["checkpoint_tick"], 2000)
        samples = fixture["samples"]
        self.assertEqual(len(samples), 128)
        self.assertEqual({sample["function"] for sample in samples}, {"atan2s", "atan2_lookup"})

        compiler = shutil.which("gcc")
        self.assertIsNotNone(compiler, "gcc is required for the engine seam fixture")
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / ("engine-atan2-q16-fixture.exe" if os.name == "nt" else "engine-atan2-q16-fixture")
            corpus = "".join(
                f"{sample['function']} {sample['y_bits']:08x} {sample['x_bits']:08x} {sample['result']}\n"
                for sample in samples
            )
            for target in (False, True):
                compiled = subprocess.run(
                    [compiler, "-std=c11", "-D_GNU_SOURCE", "-Wall", "-Wextra", "-Werror", "-ffunction-sections", "-fdata-sections",
                     *( ["-DENGINE_ATAN2_Q16_TARGET=1"] if target else [] ),
                     *( ["-DSM64_SATURN_TEST_MUTATE_ATAN2_Q16=1"] if os.environ.get("SM64_SATURN_TEST_MUTATE_ATAN2_Q16") else [] ),
                     "-I", str(ROOT / "include"), "-I", str(ROOT / "src"),
                     "-I", str(ROOT / "src/port/saturn/gfx"), "-I", str(ROOT / "src/port/saturn/platform"),
                     "-I", str(ROOT / "src/port/saturn/runtime"),
                     str(RUNNER), "-Wl,--gc-sections", "-lm", "-o", str(executable)],
                    text=True, capture_output=True, check=False,
                )
                self.assertEqual(compiled.returncode, 0, compiled.stderr)
                completed = subprocess.run([str(executable)], input=corpus, text=True, capture_output=True, check=False)
                self.assertEqual(completed.returncode, 0, completed.stderr)


if __name__ == "__main__":
    unittest.main()
