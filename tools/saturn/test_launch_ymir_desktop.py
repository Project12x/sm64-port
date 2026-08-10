#!/usr/bin/env python3
"""Host-contract tests for the profile-backed desktop Ymir launcher."""

from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock


TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

try:
    import launch_ymir_desktop as desktop
except ModuleNotFoundError as error:
    raise AssertionError("desktop Ymir launch helper is missing") from error


class DesktopYmirLaunchTests(unittest.TestCase):
    def test_cli_requires_release_manifest(self) -> None:
        with self.assertRaises(SystemExit) as caught:
            desktop.main([])
        self.assertEqual(caught.exception.code, 2)

    def test_dry_run_report_records_verified_release_manifest_sha256(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            executable = root / "ymir.exe"
            profile = root / "profile"
            cue = root / "game.cue"
            iso = root / "game.iso"
            manifest = root / "release.json"
            output = root / "launch.json"
            executable.write_bytes(b"ymir")
            profile.mkdir()
            iso.write_bytes(b"disc")
            cue.write_text('FILE "game.iso" BINARY\n', encoding="ascii")
            manifest.write_bytes(b"manifest")
            with mock.patch.object(
                desktop, "resolve_release_cue", return_value=(cue.resolve(), "a" * 64)
            ):
                result = desktop.main([
                    "--release-manifest", str(manifest), "--ymir", str(executable),
                    "--profile", str(profile), "--output", str(output),
                ])
            report = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(result, 0)
        self.assertEqual(report["release_manifest_sha256"], "a" * 64)

    def test_release_manifest_selects_cue_and_rejects_a_separate_mismatch(self) -> None:
        resolver = getattr(desktop, "resolve_release_cue", None)
        self.assertTrue(callable(resolver), "desktop launch must verify its release manifest")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cue = root / "game.cue"
            cue.write_bytes(b"cue")
            verified = SimpleNamespace(
                manifest_sha256="a" * 64,
                outputs={"cue": cue.resolve()},
            )
            with mock.patch.object(desktop, "verify_release_manifest", return_value=verified):
                resolved, digest = resolver(root / "release.json", None)
                self.assertEqual(resolved, cue.resolve())
                self.assertEqual(digest, "a" * 64)
                other = root / "other.cue"
                other.write_bytes(b"cue")
                with self.assertRaisesRegex(ValueError, "CUE differs"):
                    resolver(root / "release.json", other)

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
            self.assertEqual(
                plan["command"],
                [str(executable), "--profile", str(profile), "--disc", str(cue)],
            )
            self.assertEqual(plan["working_directory"], str(executable.parent))
            self.assertEqual(plan["profile"], str(profile))
            self.assertEqual(plan["cue"]["path"], str(cue))
            self.assertEqual(plan["cue"]["sha256"], hashlib.sha256(cue.read_bytes()).hexdigest())
            self.assertEqual(plan["iso"]["path"], str(iso))
            self.assertEqual(plan["iso"]["sha256"], hashlib.sha256(b"disc").hexdigest())
            self.assertEqual(plan["ram_cart"], "profile-managed-32-mbit-dram")

    def test_default_executable_is_the_proven_build_agent_desktop(self) -> None:
        self.assertIn("ymir-agent\\build-agent\\apps", str(desktop.DEFAULT_YMIR))
        self.assertNotIn("build-agent2", str(desktop.DEFAULT_YMIR))

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

    def test_launch_redirects_gui_output_to_report_adjacent_durable_logs(self) -> None:
        """A live GUI must not depend on pipes owned by the short-lived helper."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report = root / "ymir-desktop-launch-20260804T010203Z.json"
            plan = {
                "command": ["ymir-sdl3.exe"],
                "working_directory": str(root),
            }
            process = mock.Mock(pid=1234)
            process.poll.return_value = None

            with mock.patch.object(desktop.subprocess, "Popen", return_value=process) as popen:
                execution = desktop.launch_and_monitor(plan, 0, report)

            kwargs = popen.call_args.kwargs
            self.assertNotEqual(kwargs["stdout"], desktop.subprocess.PIPE)
            self.assertNotEqual(kwargs["stderr"], desktop.subprocess.PIPE)
            self.assertEqual(Path(kwargs["stdout"].name), report.with_suffix(".stdout.log"))
            self.assertEqual(Path(kwargs["stderr"].name), report.with_suffix(".stderr.log"))
            self.assertEqual(execution["stdout_log"], str(report.with_suffix(".stdout.log")))
            self.assertEqual(execution["stderr_log"], str(report.with_suffix(".stderr.log")))
            self.assertTrue(report.with_suffix(".stdout.log").is_file())
            self.assertTrue(report.with_suffix(".stderr.log").is_file())


if __name__ == "__main__":
    unittest.main()
