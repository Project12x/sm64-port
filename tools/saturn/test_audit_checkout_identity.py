#!/usr/bin/env python3
"""Checkout-byte contracts for immutable Saturn native-math text fixtures."""

from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = (
    "sh2_native_math_route_oracle_v1.txt",
    "sh2_native_math_sim_route_oracle_v1.txt",
    "sh2_native_math_sim_audit_contract_v2.txt",
    "sh2_native_math_goal_audit_contract_v3.txt",
    "sh2_native_math_goal_audit_contract_v4.txt",
)


class AuditCheckoutIdentityTests(unittest.TestCase):
    def test_windows_autocrlf_filtered_checkout_preserves_canonical_lf_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repository = Path(temporary) / "repository"
            fixture_dir = repository / "tools" / "saturn"
            fixture_dir.mkdir(parents=True)
            shutil.copy2(ROOT / ".gitattributes", repository / ".gitattributes")
            for name in FIXTURES:
                shutil.copy2(ROOT / "tools" / "saturn" / name, fixture_dir / name)

            subprocess.run(["git", "init", "-q"], cwd=repository, check=True)
            subprocess.run(
                ["git", "-c", "core.autocrlf=false", "add", ".gitattributes", "tools/saturn"],
                cwd=repository,
                check=True,
            )
            checkout = Path(temporary) / "filtered-checkout"
            checkout.mkdir()
            subprocess.run(
                [
                    "git", "-c", "core.autocrlf=true", "checkout-index", "--all", "--force",
                    f"--prefix={checkout.resolve().as_posix()}/",
                ],
                cwd=repository,
                check=True,
            )

            for name in FIXTURES:
                with self.subTest(name=name):
                    expected = (ROOT / "tools" / "saturn" / name).read_bytes()
                    actual = (checkout / "tools" / "saturn" / name).read_bytes()
                    self.assertNotIn(b"\r", expected)
                    self.assertEqual(actual, expected)


if __name__ == "__main__":
    unittest.main()
