#!/usr/bin/env python3
"""Host contracts for verify-before-copy Saturn release staging."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import test_release_manifest as release_fixtures
import release_manifest

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

    def test_stage_copies_the_verified_snapshot_after_every_source_mutates(self) -> None:
        fixture = release_fixtures.ReleaseFixture(self.root / "release-source")
        manifest = fixture.write()
        expected_manifest = manifest.read_bytes()
        expected_outputs = {
            name: path.read_bytes() for name, path in fixture.outputs.items()
        }
        real_verify = release_manifest.verify_release_manifest
        first = True

        def verify_then_mutate(path: Path, **kwargs: object):
            nonlocal first
            verified = real_verify(path, **kwargs)
            if first:
                first = False
                manifest.write_bytes(b"mutated manifest")
                for name, output in fixture.outputs.items():
                    output.write_bytes(f"mutated {name}".encode("ascii"))
            return verified

        destination = self.root / "staged"
        with mock.patch.object(
            stage_saturn_release, "verify_release_manifest",
            side_effect=verify_then_mutate,
        ):
            stage_saturn_release.stage_release(manifest, destination)

        self.assertEqual(
            (destination / release_manifest.MANIFEST_NAME).read_bytes(),
            expected_manifest,
        )
        for name, expected in expected_outputs.items():
            relative = release_manifest.verify_release_manifest(
                destination / release_manifest.MANIFEST_NAME
            ).document["outputs"][name]["path"]
            self.assertEqual((destination / relative).read_bytes(), expected)

    def test_stage_rolls_back_partial_writes_and_retry_succeeds(self) -> None:
        fixture = release_fixtures.ReleaseFixture(self.root / "release-source")
        manifest = fixture.write()
        real_copy = stage_saturn_release._copy_new
        for destination_preexists in (False, True):
            with self.subTest(destination_preexists=destination_preexists):
                destination = self.root / f"staged-{destination_preexists}"
                if destination_preexists:
                    destination.mkdir()
                calls = 0

                def fail_after_two(source: Path, target: Path):
                    nonlocal calls
                    calls += 1
                    if calls == 3:
                        raise OSError("injected staging failure")
                    return real_copy(source, target)

                with (
                    mock.patch.object(
                        stage_saturn_release, "_copy_new", side_effect=fail_after_two
                    ),
                    self.assertRaisesRegex(OSError, "injected staging failure"),
                ):
                    stage_saturn_release.stage_release(manifest, destination)

                if destination_preexists:
                    self.assertTrue(destination.is_dir())
                    self.assertEqual(list(destination.iterdir()), [])
                else:
                    self.assertFalse(destination.exists())
                staged = stage_saturn_release.stage_release(manifest, destination)
                self.assertTrue((staged / release_manifest.MANIFEST_NAME).is_file())

    def test_stage_rolls_back_when_final_reverification_fails(self) -> None:
        fixture = release_fixtures.ReleaseFixture(self.root / "release-source")
        manifest = fixture.write()
        real_verify = stage_saturn_release.verify_release_manifest
        for destination_preexists in (False, True):
            with self.subTest(destination_preexists=destination_preexists):
                destination = self.root / f"reverify-{destination_preexists}"
                if destination_preexists:
                    destination.mkdir()
                calls = 0

                def fail_final_verify(path: Path):
                    nonlocal calls
                    calls += 1
                    if calls == 2:
                        raise ValueError("injected final verification failure")
                    return real_verify(path)

                with (
                    mock.patch.object(
                        stage_saturn_release,
                        "verify_release_manifest",
                        side_effect=fail_final_verify,
                    ),
                    self.assertRaisesRegex(ValueError, "final verification failure"),
                ):
                    stage_saturn_release.stage_release(manifest, destination)

                if destination_preexists:
                    self.assertTrue(destination.is_dir())
                    self.assertEqual(list(destination.iterdir()), [])
                else:
                    self.assertFalse(destination.exists())

    def test_rollback_preserves_a_foreign_replacement_at_an_owned_path(self) -> None:
        fixture = release_fixtures.ReleaseFixture(self.root / "release-source")
        manifest = fixture.write()
        destination = self.root / "race-destination"
        real_copy = stage_saturn_release._copy_new
        calls = 0
        foreign_target: Path | None = None

        def replace_then_fail(source: Path, target: Path):
            nonlocal calls, foreign_target
            calls += 1
            identity = real_copy(source, target)
            if calls == 2:
                target.unlink()
                target.write_bytes(b"foreign concurrent content")
                foreign_target = target
                raise OSError("injected replacement race")
            return identity

        with (
            mock.patch.object(
                stage_saturn_release, "_copy_new", side_effect=replace_then_fail
            ),
            self.assertRaisesRegex(OSError, "replacement race") as caught,
        ):
            stage_saturn_release.stage_release(manifest, destination)

        self.assertIsNotNone(foreign_target)
        self.assertEqual(foreign_target.read_bytes(), b"foreign concurrent content")
        self.assertTrue(
            any("rollback failures" in note for note in getattr(caught.exception, "__notes__", []))
        )

    def test_stage_rejects_destination_directory_replacement_without_deleting_foreign_data(self) -> None:
        fixture = release_fixtures.ReleaseFixture(self.root / "release-source")
        manifest = fixture.write()
        destination = self.root / "replace-destination"
        displaced = self.root / "displaced-owned-destination"
        real_copy = stage_saturn_release._copy_new
        replaced = False

        def replace_destination(source: Path, target: Path):
            nonlocal replaced
            if not replaced:
                replaced = True
                destination.rename(displaced)
                destination.mkdir()
                (destination / "foreign.txt").write_bytes(b"foreign")
                target.parent.mkdir(parents=True, exist_ok=True)
            return real_copy(source, target)

        with (
            mock.patch.object(
                stage_saturn_release, "_copy_new", side_effect=replace_destination
            ),
            self.assertRaisesRegex(ValueError, "directory was replaced"),
        ):
            stage_saturn_release.stage_release(manifest, destination)

        self.assertEqual((destination / "foreign.txt").read_bytes(), b"foreign")
        self.assertTrue(displaced.is_dir())


class StageSaturnReleaseMissingImplementationTests(unittest.TestCase):
    def test_stage_release_module_exists(self) -> None:
        self.assertIsNotNone(
            stage_saturn_release, "stage_saturn_release.py is missing"
        )


if __name__ == "__main__":
    unittest.main()
