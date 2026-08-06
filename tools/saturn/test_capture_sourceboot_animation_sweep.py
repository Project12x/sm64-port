#!/usr/bin/env python3
import copy
import hashlib
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import capture_sourceboot_animation_sweep as sweep


class SweepTests(unittest.TestCase):
    def setUp(self):
        self.artifact = {"artifacts": {
            key: {"sha256": hashlib.sha256(key.encode()).hexdigest()}
            for key in ("elf", "cue", "iso")}}
        self.report = {
            "schema": "sm64-saturn-animation-sweep-v1", "diagnostic_mode": 1,
            "evaluator_symbol": sweep.PRODUCTION_EVALUATOR,
            "target_fault": False, "fallback_count": 0,
            "corrupt_bounds_count": 0, "ids": list(range(209)),
            "seen_count": 209,
            "elf_sha256": self.artifact["artifacts"]["elf"]["sha256"],
            "cue_sha256": self.artifact["artifacts"]["cue"]["sha256"],
            "iso_sha256": self.artifact["artifacts"]["iso"]["sha256"],
        }

    def test_complete_report_passes(self):
        self.assertEqual(sweep.validate_sweep_report(self.report, self.artifact)["seen_count"], 209)

    def test_missing_duplicate_symbol_identity_and_promotable_mode_fail(self):
        for mutate, message in (
            (lambda r: r.update(ids=r["ids"][:-1]), "cover"),
            (lambda r: r.update(ids=r["ids"][:-1] + [207]), "duplicate"),
            (lambda r: r.update(evaluator_symbol="diagnostic_animation_sweep"), "production"),
            (lambda r: r.update(diagnostic_mode=0), "diagnostic_mode"),
            (lambda r: r.update(elf_sha256="0" * 64), "identity"),
        ):
            report = copy.deepcopy(self.report)
            mutate(report)
            with self.subTest(message=message), self.assertRaises(ValueError):
                sweep.validate_sweep_report(report, self.artifact)


if __name__ == "__main__":
    unittest.main()
