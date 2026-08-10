#!/usr/bin/env python3
"""Host contracts for verify-before-copy Saturn release staging."""

from __future__ import annotations

import sys
import os
import errno
import io
import shutil
import subprocess
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

    @unittest.skipUnless(os.name == "nt", "Windows opened-handle cleanup contract")
    def test_windows_clean_preexisting_empty_destination_leaves_no_backup(self) -> None:
        fixture = release_fixtures.ReleaseFixture(self.root / "release-source")
        manifest = fixture.write()
        destination = self.root / "preexisting-empty"
        destination.mkdir()
        stage_saturn_release.stage_release(manifest, destination)
        self.assertEqual(
            list(
                destination.parent.glob(
                    f".sm64-saturn-quarantine-{destination.name}-*"
                )
            ),
            [],
        )

    def test_unavailable_identity_delete_retains_empty_backup_with_warning(self) -> None:
        fixture = release_fixtures.ReleaseFixture(self.root / "release-source")
        manifest = fixture.write()
        destination = self.root / "retained-empty"
        destination.mkdir()
        with (
            mock.patch.object(
                stage_saturn_release,
                "remove_empty_directory_by_identity",
                return_value=False,
            ),
            self.assertWarnsRegex(
                RuntimeWarning,
                r"retained proven empty destination backup.*\.sm64-saturn-quarantine-retained-empty-",
            ),
        ):
            stage_saturn_release.stage_release(manifest, destination)

        backups = list(
            destination.parent.glob(
                f".sm64-saturn-quarantine-{destination.name}-*"
            )
        )
        self.assertEqual(len(backups), 1)
        self.assertTrue(backups[0].is_dir())
        self.assertEqual(list(backups[0].iterdir()), [])
        staged = release_manifest.verify_release_manifest(
            destination / release_manifest.MANIFEST_NAME,
            exact_inventory=True,
        )
        staged.close()

        shutil.rmtree(destination)
        destination.mkdir()
        with (
            mock.patch.object(
                stage_saturn_release,
                "remove_empty_directory_by_identity",
                return_value=False,
            ),
            self.assertWarns(RuntimeWarning),
        ):
            stage_saturn_release.stage_release(manifest, destination)

    def test_missing_destination_success_never_creates_a_backup(self) -> None:
        fixture = release_fixtures.ReleaseFixture(self.root / "release-source")
        manifest = fixture.write()
        destination = self.root / "missing-no-backup"
        with mock.patch.object(
            stage_saturn_release,
            "remove_empty_directory_by_identity",
            side_effect=AssertionError("missing destination has no backup"),
        ):
            stage_saturn_release.stage_release(manifest, destination)
        self.assertEqual(
            list(
                destination.parent.glob(
                    f".sm64-saturn-quarantine-{destination.name}-*"
                )
            ),
            [],
        )

    def test_atomic_rename_dispatches_linux_and_darwin_bsd_exclusive_apis(self) -> None:
        cases = (
            ("linux", "renameat2", 1),
            ("darwin", "renameatx_np", 4),
            ("freebsd14", "renameatx_np", 4),
        )
        for platform_name, symbol, flag in cases:
            with self.subTest(platform=platform_name):
                function = mock.Mock(return_value=0)
                libc = mock.Mock(spec=[])
                setattr(libc, symbol, function)
                namespace = mock.Mock()
                namespace._fd = 17
                adapter = stage_saturn_release._resolve_atomic_rename_adapter(
                    platform_name, libc
                )
                stage_saturn_release._rename_noreplace(
                    namespace, "private", "published", adapter
                )
                function.assert_called_once_with(
                    17, b"private", 17, b"published", flag
                )
                self.assertEqual(namespace.require_current.call_count, 2)
                namespace.rename_child.assert_not_called()

    def test_atomic_rename_maps_exclusive_collisions_to_file_exists(self) -> None:
        for platform_name, symbol in (
            ("linux", "renameat2"),
            ("darwin", "renameatx_np"),
            ("openbsd7", "renameatx_np"),
        ):
            with self.subTest(platform=platform_name):
                def collide(*_args: object) -> int:
                    stage_saturn_release.ctypes.set_errno(errno.EEXIST)
                    return -1

                libc = mock.Mock(spec=[])
                setattr(libc, symbol, mock.Mock(side_effect=collide))
                namespace = mock.Mock()
                namespace._fd = 23
                adapter = stage_saturn_release._resolve_atomic_rename_adapter(
                    platform_name, libc
                )
                with self.assertRaises(FileExistsError):
                    stage_saturn_release._rename_noreplace(
                        namespace, "source", "target", adapter
                    )

    def test_unsupported_posix_fails_before_any_staging_namespace_mutation(self) -> None:
        fixture = release_fixtures.ReleaseFixture(self.root / "release-source")
        manifest = fixture.write()
        destination = self.root / "unsupported-destination"
        empty_libc = mock.Mock(spec=[])
        with (
            mock.patch.object(stage_saturn_release.sys, "platform", "sunos5"),
            mock.patch.object(
                stage_saturn_release.ctypes,
                "CDLL",
                return_value=empty_libc,
            ),
            self.assertRaisesRegex(
                RuntimeError, "atomic exclusive rename.*unsupported"
            ),
        ):
            stage_saturn_release.stage_release(manifest, destination)
        self.assertFalse(destination.exists())
        self.assertEqual(
            list(self.root.glob(".sm64-saturn-private-unsupported-destination-*")),
            [],
        )

    def test_stage_help_describes_supported_atomic_publication_platforms(self) -> None:
        output = io.StringIO()
        with (
            mock.patch("sys.stdout", output),
            self.assertRaises(SystemExit) as caught,
        ):
            stage_saturn_release.main(["--help"])
        self.assertEqual(caught.exception.code, 0)
        rendered = " ".join(output.getvalue().split())
        self.assertIn("Linux renameat2(RENAME_NOREPLACE)", rendered)
        self.assertIn("macOS/BSD renameatx_np(RENAME_EXCL)", rendered)
        self.assertIn("unsupported hosts fail before staging mutation", rendered)

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

                def fail_final_verify(path: Path, **kwargs: object):
                    nonlocal calls
                    calls += 1
                    if calls == 3:
                        raise ValueError("injected final verification failure")
                    return real_verify(path, **kwargs)

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

                staged = stage_saturn_release.stage_release(manifest, destination)
                self.assertTrue((staged / release_manifest.MANIFEST_NAME).is_file())

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
        preserved = [
            path
            for path in self.root.rglob(foreign_target.name)
            if path.is_file() and path.read_bytes() == b"foreign concurrent content"
        ]
        self.assertTrue(preserved)
        self.assertTrue(
            any("quarantine" in note for note in getattr(caught.exception, "__notes__", []))
        )

    def test_stage_rejects_destination_directory_replacement_without_deleting_foreign_data(self) -> None:
        fixture = release_fixtures.ReleaseFixture(self.root / "release-source")
        manifest = fixture.write()
        destination = self.root / "replace-destination"
        real_copy = stage_saturn_release._copy_new
        replaced = False

        def replace_destination(source: Path, target: Path):
            nonlocal replaced
            if not replaced:
                replaced = True
                destination.mkdir()
                (destination / "foreign.txt").write_bytes(b"foreign")
            return real_copy(source, target)

        with (
            mock.patch.object(
                stage_saturn_release, "_copy_new", side_effect=replace_destination
            ),
            self.assertRaisesRegex(ValueError, "contaminated"),
        ):
            stage_saturn_release.stage_release(manifest, destination)

        self.assertFalse(destination.exists())
        self.assertTrue(
            any(
                path.read_bytes() == b"foreign"
                for path in self.root.rglob("foreign.txt")
                if path.is_file()
            )
        )

    def test_ancestor_swap_before_publication_cannot_redirect_writes(self) -> None:
        fixture = release_fixtures.ReleaseFixture(self.root / "release-source")
        manifest = fixture.write()
        ancestor = self.root / "publication-ancestor"
        publication_parent = ancestor / "publication-parent"
        publication_parent.mkdir(parents=True)
        destination = publication_parent / "staged"
        displaced_parent = self.root / "displaced-ancestor"
        outside = self.root / "outside"
        outside.mkdir()
        real_publish = stage_saturn_release._publish_private_tree

        def swap_ancestor_then_publish(*args: object, **kwargs: object):
            try:
                ancestor.rename(displaced_parent)
                ancestor.symlink_to(outside, target_is_directory=True)
            except OSError as error:
                raise OSError("injected ancestor swap was blocked") from error
            return real_publish(*args, **kwargs)

        with (
            mock.patch.object(
                stage_saturn_release,
                "_publish_private_tree",
                side_effect=swap_ancestor_then_publish,
            ),
            self.assertRaises((OSError, ValueError)),
        ):
            stage_saturn_release.stage_release(manifest, destination)

        self.assertEqual(list(outside.iterdir()), [])
        self.assertFalse((outside / "staged").exists())

    def test_destination_rejects_a_junction_anywhere_in_ancestor_chain(self) -> None:
        fixture = release_fixtures.ReleaseFixture(self.root / "release-source")
        manifest = fixture.write()
        real_parent = self.root / "real-parent"
        real_parent.mkdir()
        alias = self.root / "ancestor-alias"
        if os.name == "nt":
            created = subprocess.run(
                ["cmd", "/c", "mklink", "/J", str(alias), str(real_parent)],
                capture_output=True,
                text=True,
            )
            if created.returncode:
                self.skipTest(f"cannot create Windows junction: {created.stderr}")
        else:
            alias.symlink_to(real_parent, target_is_directory=True)

        with self.assertRaisesRegex(ValueError, "symlink|junction|reparse"):
            stage_saturn_release.stage_release(manifest, alias / "staged")
        self.assertEqual(list(real_parent.iterdir()), [])

    def test_concurrent_destination_extra_is_quarantined_and_retryable(self) -> None:
        fixture = release_fixtures.ReleaseFixture(self.root / "release-source")
        manifest = fixture.write()
        real_publish = stage_saturn_release._publish_private_tree
        for destination_preexists in (False, True):
            with self.subTest(destination_preexists=destination_preexists):
                destination = self.root / f"contaminated-{destination_preexists}"
                if destination_preexists:
                    destination.mkdir()
                injected = False

                def contaminate_then_publish(*args: object, **kwargs: object):
                    nonlocal injected
                    if not injected:
                        injected = True
                        destination.mkdir(exist_ok=True)
                        (destination / "foreign.txt").write_bytes(b"foreign")
                    return real_publish(*args, **kwargs)

                with (
                    mock.patch.object(
                        stage_saturn_release,
                        "_publish_private_tree",
                        side_effect=contaminate_then_publish,
                    ),
                    self.assertRaisesRegex(ValueError, "contaminated|not empty"),
                ):
                    stage_saturn_release.stage_release(manifest, destination)

                if destination_preexists:
                    self.assertTrue(destination.is_dir())
                    self.assertEqual(list(destination.iterdir()), [])
                else:
                    self.assertFalse(destination.exists())
                quarantines = list(
                    destination.parent.glob(
                        f".sm64-saturn-quarantine-{destination.name}-*"
                    )
                )
                self.assertTrue(
                    any(
                        (path / "foreign.txt").read_bytes() == b"foreign"
                        for path in quarantines
                        if (path / "foreign.txt").is_file()
                    )
                )
                staged = stage_saturn_release.stage_release(manifest, destination)
                self.assertTrue((staged / release_manifest.MANIFEST_NAME).is_file())

    def test_extra_created_after_publish_fails_exactly_and_is_quarantined(self) -> None:
        fixture = release_fixtures.ReleaseFixture(self.root / "release-source")
        manifest = fixture.write()
        destination = self.root / "post-publish-extra"
        real_verify = stage_saturn_release.verify_release_manifest
        calls = 0

        def contaminate_final_verify(path: Path, **kwargs: object):
            nonlocal calls
            calls += 1
            if calls == 3:
                (destination / "foreign.txt").write_bytes(b"foreign")
            return real_verify(path, **kwargs)

        with (
            mock.patch.object(
                stage_saturn_release,
                "verify_release_manifest",
                side_effect=contaminate_final_verify,
            ),
            self.assertRaisesRegex(ValueError, "inventory"),
        ):
            stage_saturn_release.stage_release(manifest, destination)

        self.assertFalse(destination.exists())
        quarantines = list(
            destination.parent.glob(
                f".sm64-saturn-quarantine-{destination.name}-*"
            )
        )
        self.assertTrue(
            any(
                (path / "foreign.txt").read_bytes() == b"foreign"
                for path in quarantines
                if (path / "foreign.txt").is_file()
            )
        )
        staged = stage_saturn_release.stage_release(manifest, destination)
        self.assertTrue((staged / release_manifest.MANIFEST_NAME).is_file())


class StageSaturnReleaseMissingImplementationTests(unittest.TestCase):
    def test_stage_release_module_exists(self) -> None:
        self.assertIsNotNone(
            stage_saturn_release, "stage_saturn_release.py is missing"
        )


if __name__ == "__main__":
    unittest.main()
