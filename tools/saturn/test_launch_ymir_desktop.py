#!/usr/bin/env python3
"""Host-contract tests for the profile-backed desktop Ymir launcher."""

from __future__ import annotations

import hashlib
import sys
import tempfile
import unittest
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

try:
    import launch_ymir_desktop as desktop
except ModuleNotFoundError as error:
    raise AssertionError("desktop Ymir launch helper is missing") from error


class DesktopYmirLaunchTests(unittest.TestCase):
    def test_build_plan_uses_project_profile_disc_and_executable_directory(self) -> None:
        builder = getattr(desktop, "build_launch_plan", None)
        self.assertTrue(callable(builder), "helper must expose a deterministic launch plan")

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            executable = root / "ymir" / "ymir-sdl3.exe"
            profile = root / ".ymir-profile"
            cue = root / "staged" / "sm64-saturn-sourceboot-e2.cue"
            executable.parent.mkdir(parents=True)
            profile.mkdir()
            cue.parent.mkdir()
            executable.write_bytes(b"ymir")
            cue.write_text('FILE "sm64-saturn-sourceboot-e2.iso" BINARY\n', encoding="utf-8")
            iso = cue.parent / "sm64-saturn-sourceboot-e2.iso"
            iso.write_bytes(b"disc")

            plan = builder(executable, profile, cue)
            self.assertEqual(plan["command"], [str(executable), "-p", str(profile), "-d", str(cue)])
            self.assertEqual(plan["working_directory"], str(executable.parent))
            self.assertEqual(plan["profile"], str(profile))
            self.assertEqual(plan["cue"]["path"], str(cue))
            self.assertEqual(plan["cue"]["sha256"], hashlib.sha256(cue.read_bytes()).hexdigest())
            self.assertEqual(plan["iso"]["path"], str(iso))
            self.assertEqual(plan["iso"]["sha256"], hashlib.sha256(b"disc").hexdigest())
            self.assertEqual(plan["ram_cart"], "profile-managed-32-mbit-dram")

    def test_build_plan_rejects_a_missing_cue_local_iso(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            executable = root / "ymir-sdl3.exe"
            profile = root / ".ymir-profile"
            cue = root / "game.cue"
            executable.write_bytes(b"ymir")
            profile.mkdir()
            cue.write_text('FILE "missing.iso" BINARY\n', encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "referenced ISO"):
                desktop.build_launch_plan(executable, profile, cue)


if __name__ == "__main__":
    unittest.main()
