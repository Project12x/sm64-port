#!/usr/bin/env python3
"""Host contracts for verify-before-copy Saturn release staging."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import test_release_manifest as release_fixtures

try:
    import stage_saturn_release
except ModuleNotFoundError:
    stage_saturn_release = None  # type: ignore[assignment]


@unittest.skipIf(stage_saturn_release is None, "stage_saturn_release.py is not implemented")
class StageSaturnReleaseTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_stage_is_profile_agnostic_and_refuses_nonempty_destination(self) -> None:
        full = release_fixtures.ReleaseFixture(
            self.root / "full", profile_id="sm64-saturn-full"
        )
        destination = self.root / "staged"
        staged = stage_saturn_release.stage_release(full.write(), destination)
        self.assertEqual(staged, destination.resolve())
        self.assertTrue((staged / "saturn-release-manifest-v1.json").is_file())
        demo = release_fixtures.ReleaseFixture(
            self.root / "demo", profile_id="sourceboot-bob-demo"
        )
        with self.assertRaisesRegex(ValueError, "destination.*not empty"):
            stage_saturn_release.stage_release(demo.write(), destination)

    def test_mutated_input_is_rejected_before_destination_creation(self) -> None:
        fixture = release_fixtures.ReleaseFixture(self.root / "release-source")
        manifest = fixture.write()
        fixture.outputs["source_dat"].write_bytes(b"mutated")
        destination = self.root / "must-not-exist"
        with self.assertRaisesRegex(ValueError, "source_dat.*SHA-256 mismatch"):
            stage_saturn_release.stage_release(manifest, destination)
        self.assertFalse(destination.exists())

    def test_stage_copies_only_verified_outputs_and_manifest(self) -> None:
        fixture = release_fixtures.ReleaseFixture(self.root / "release-source")
        fixture.write()
        unsealed = fixture.release_dir / "notes.txt"
        unsealed.write_text("do not publish", encoding="utf-8")
        destination = self.root / "staged"
        stage_saturn_release.stage_release(fixture.manifest, destination)
        files = sorted(
            path.relative_to(destination).as_posix()
            for path in destination.rglob("*") if path.is_file()
        )
        self.assertEqual(files, [
            "game.cue", "game.iso", "obj/SOURCE.DAT", "obj/game.elf",
            "saturn-release-manifest-v1.json",
        ])
        self.assertFalse((destination / "notes.txt").exists())


class StageSaturnReleaseMissingImplementationTests(unittest.TestCase):
    def test_stage_release_module_exists(self) -> None:
        self.assertIsNotNone(
            stage_saturn_release, "stage_saturn_release.py is missing"
        )


if __name__ == "__main__":
    unittest.main()
