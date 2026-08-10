#!/usr/bin/env python3
"""Host contracts for one-shot native-math audit-v4 sealing."""

from __future__ import annotations

import hashlib
import json
import os
import sys
import tempfile
import threading
import unittest
from pathlib import Path
from unittest.mock import patch


TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from hermetic_manifest import canonical_json_bytes
from test_release_manifest import ReleaseFixture

import release_manifest
import path_identity
import seal_sh2_native_math_audit_v4 as sealer


class AuditV4SealerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(dir=TOOLS_DIR)
        self.root = Path(self.temporary.name)
        self.fixture = ReleaseFixture(self.root)
        self.manifest = self.fixture.write()
        self.measurement = self.root / "measurement.json"
        self.output = self.root / "audit-v4.txt"
        with release_manifest.verify_release_manifest(self.manifest) as verified:
            self.release = verified.document
            self.manifest_sha256 = verified.manifest_sha256
        self.document = {
            "schema": "sm64-saturn-native-math-measurement-v1",
            "status": "measured-unsealed",
            "root": "_game_loop_one_iteration",
            "total": 701,
            "callers": ["_game_loop_one_iteration"],
            "elf_sha256": self.release["outputs"]["elf"]["sha256"],
            "release_manifest_sha256": self.manifest_sha256,
        }
        self._write_measurement()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _write_measurement(self) -> None:
        self.measurement.write_bytes(canonical_json_bytes(self.document))

    def test_sealer_emits_canonical_v4_from_verified_release_fields(self) -> None:
        raw = sealer.seal_v4_contract(self.measurement, self.manifest, self.output)
        expected = (
            "AUDIT_CONTRACT_VERSION 4\n"
            "EXPECTED_ROOT _game_loop_one_iteration\n"
            "EXPECTED_TOTAL 701\n"
            f"EXPECTED_RELEASE_MANIFEST_SHA256 {self.manifest_sha256}\n"
            f"EXPECTED_IDENTITY_SHA256 {self.release['identity_sha256']}\n"
            f"EXPECTED_EFFECTIVE_CONFIG_SHA256 {self.release['effective_config_sha256']}\n"
            f"EXPECTED_TARGET_PROFILE_SHA256 {self.release['target_profile_sha256']}\n"
            f"EXPECTED_ELF_SHA256 {self.release['outputs']['elf']['sha256']}\n"
            "FORBIDDEN_CALLER _atan2_lookup\n"
            "FORBIDDEN_CALLER _atan2s\n"
        ).encode("ascii")
        self.assertEqual(raw, expected)
        self.assertEqual(self.output.read_bytes(), expected)
        self.assertEqual(hashlib.sha256(raw).hexdigest(), hashlib.sha256(expected).hexdigest())

    def test_sealer_rejects_measurement_manifest_or_elf_mismatch(self) -> None:
        mutations = {
            "manifest": ("release_manifest_sha256", "a" * 64, "manifest"),
            "ELF": ("elf_sha256", "b" * 64, "ELF"),
        }
        for name, (field, value, message) in mutations.items():
            with self.subTest(name=name):
                original = self.document[field]
                self.document[field] = value
                self._write_measurement()
                with self.assertRaisesRegex(ValueError, message):
                    sealer.seal_v4_contract(self.measurement, self.manifest, self.output)
                self.assertFalse(self.output.exists())
                self.document[field] = original

    def test_sealer_requires_unsealed_schema_root_identity_v2_and_safe_callers(self) -> None:
        mutations = (
            ("schema", "wrong", "schema"),
            ("status", "accepted", "measured-unsealed"),
            ("root", "_wrong", "root"),
            ("callers", ["_atan2_lookup"], "forbidden caller"),
        )
        for field, value, message in mutations:
            with self.subTest(field=field):
                original = self.document[field]
                self.document[field] = value
                self._write_measurement()
                with self.assertRaisesRegex(ValueError, message):
                    sealer.seal_v4_contract(self.measurement, self.manifest, self.output)
                self.assertFalse(self.output.exists())
                self.document[field] = original

        self.fixture.rebuild_identity(1)
        self.manifest.write_bytes(self.fixture.build())
        with self.assertRaisesRegex(ValueError, "identity version 2"):
            sealer.seal_v4_contract(self.measurement, self.manifest, self.output)

    def test_sealer_refuses_to_overwrite_existing_contract(self) -> None:
        original = b"preserve me\n"
        self.output.write_bytes(original)
        with self.assertRaisesRegex(ValueError, "refusing to overwrite"):
            sealer.seal_v4_contract(self.measurement, self.manifest, self.output)
        self.assertEqual(self.output.read_bytes(), original)

    def test_final_name_is_absent_while_private_bytes_are_partial(self) -> None:
        real_fdopen = os.fdopen

        class PartialWriter:
            def __init__(inner_self, descriptor: int, mode: str):
                inner_self.stream = real_fdopen(descriptor, mode)

            def __enter__(inner_self):
                inner_self.stream.__enter__()
                return inner_self

            def __exit__(inner_self, *args: object):
                return inner_self.stream.__exit__(*args)

            def write(inner_self, raw: bytes) -> int:
                midpoint = max(1, len(raw) // 2)
                inner_self.stream.write(raw[:midpoint])
                inner_self.stream.flush()
                self.assertFalse(self.output.exists())
                return midpoint + inner_self.stream.write(raw[midpoint:])

            def flush(inner_self) -> None:
                inner_self.stream.flush()

            def fileno(inner_self) -> int:
                return inner_self.stream.fileno()

        def partial_fdopen(descriptor: int, mode: str, *args: object, **kwargs: object):
            if mode == "wb" and not kwargs:
                return PartialWriter(descriptor, mode)
            return real_fdopen(descriptor, mode, *args, **kwargs)

        with patch.object(sealer.os, "fdopen", side_effect=partial_fdopen):
            raw = sealer.seal_v4_contract(
                self.measurement, self.manifest, self.output
            )
        self.assertEqual(self.output.read_bytes(), raw)

    def test_concurrent_reader_sees_absent_then_complete_contract(self) -> None:
        real_fdopen = os.fdopen
        partial_ready = threading.Event()
        finish_write = threading.Event()
        result: list[bytes] = []
        failures: list[BaseException] = []

        class SlowWriter:
            def __init__(inner_self, descriptor: int, mode: str):
                inner_self.stream = real_fdopen(descriptor, mode)

            def __enter__(inner_self):
                inner_self.stream.__enter__()
                return inner_self

            def __exit__(inner_self, *args: object):
                return inner_self.stream.__exit__(*args)

            def write(inner_self, raw: bytes) -> int:
                midpoint = len(raw) // 2
                inner_self.stream.write(raw[:midpoint])
                inner_self.stream.flush()
                partial_ready.set()
                if not finish_write.wait(5):
                    raise RuntimeError("test reader did not release private writer")
                return midpoint + inner_self.stream.write(raw[midpoint:])

            def flush(inner_self) -> None:
                inner_self.stream.flush()

            def fileno(inner_self) -> int:
                return inner_self.stream.fileno()

        def slow_fdopen(descriptor: int, mode: str, *args: object, **kwargs: object):
            if mode == "wb" and not kwargs:
                return SlowWriter(descriptor, mode)
            return real_fdopen(descriptor, mode, *args, **kwargs)

        def seal() -> None:
            try:
                result.append(
                    sealer.seal_v4_contract(
                        self.measurement, self.manifest, self.output
                    )
                )
            except BaseException as error:
                failures.append(error)

        with patch.object(sealer.os, "fdopen", side_effect=slow_fdopen):
            worker = threading.Thread(target=seal)
            worker.start()
            self.assertTrue(partial_ready.wait(5))
            self.assertFalse(self.output.exists())
            finish_write.set()
            worker.join(5)
        self.assertFalse(worker.is_alive())
        self.assertEqual(failures, [])
        self.assertEqual(self.output.read_bytes(), result[0])

    def test_file_and_containing_directory_are_fsynced(self) -> None:
        directory_fsync_observations: list[bytes] = []

        def observe_directory_fsync(namespace: release_manifest.DirectoryNamespaceGuard):
            directory_fsync_observations.append(self.output.read_bytes())

        with patch.object(
            sealer, "_fsync_directory", side_effect=observe_directory_fsync
        ), patch.object(sealer.os, "fsync", wraps=os.fsync) as fsync:
            raw = sealer.seal_v4_contract(
                self.measurement, self.manifest, self.output
            )
        self.assertEqual(directory_fsync_observations, [raw])
        fsync.assert_called()

    def test_crash_before_publish_leaves_final_absent_and_retryable(self) -> None:
        real_fdopen = os.fdopen

        class CrashingWriter:
            def __init__(inner_self, descriptor: int, mode: str):
                inner_self.stream = real_fdopen(descriptor, mode)

            def __enter__(inner_self):
                inner_self.stream.__enter__()
                return inner_self

            def __exit__(inner_self, *args: object):
                return inner_self.stream.__exit__(*args)

            def write(inner_self, raw: bytes) -> int:
                inner_self.stream.write(raw[:1])
                inner_self.stream.flush()
                self.assertFalse(self.output.exists())
                raise RuntimeError("simulated crash")

            def flush(inner_self) -> None:
                inner_self.stream.flush()

            def fileno(inner_self) -> int:
                return inner_self.stream.fileno()

        def crashing_fdopen(descriptor: int, mode: str, *args: object, **kwargs: object):
            if mode == "wb" and not kwargs:
                return CrashingWriter(descriptor, mode)
            return real_fdopen(descriptor, mode, *args, **kwargs)

        with patch.object(sealer.os, "fdopen", side_effect=crashing_fdopen):
            with self.assertRaisesRegex(RuntimeError, "simulated crash") as caught:
                sealer.seal_v4_contract(self.measurement, self.manifest, self.output)
        self.assertFalse(self.output.exists())
        self.assertTrue(any("retained" in note for note in caught.exception.__notes__))
        raw = sealer.seal_v4_contract(self.measurement, self.manifest, self.output)
        self.assertEqual(self.output.read_bytes(), raw)

    def test_publish_race_never_clobbers_concurrent_final(self) -> None:
        concurrent = b"concurrent winner\n"
        real_publish = path_identity._publish_held_file

        def race(descriptor, namespace, private_name, target_name):
            (namespace.path / target_name).write_bytes(concurrent)
            return real_publish(
                descriptor, namespace, private_name, target_name
            )

        with patch.object(path_identity, "_publish_held_file", side_effect=race):
            with self.assertRaises(FileExistsError) as caught:
                sealer.seal_v4_contract(self.measurement, self.manifest, self.output)
        self.assertEqual(self.output.read_bytes(), concurrent)
        self.assertTrue(any("retained" in note for note in caught.exception.__notes__))

    def test_replaced_private_name_is_retained_without_cleanup(self) -> None:
        replacement = b"do not delete a concurrent replacement\n"

        def replace_private(descriptor, namespace, private_name, target_name):
            original = namespace.path / private_name
            displaced = namespace.path / f"{private_name}.displaced"
            original.rename(displaced)
            original.write_bytes(replacement)
            raise OSError("simulated rename failure")

        with patch.object(
            path_identity, "_publish_held_file", side_effect=replace_private
        ):
            with self.assertRaisesRegex(OSError, "simulated rename failure") as caught:
                sealer.seal_v4_contract(self.measurement, self.manifest, self.output)
        self.assertFalse(self.output.exists())
        retained = list(self.root.glob(".sm64-saturn-private-audit-v4.txt-*"))
        self.assertGreaterEqual(len(retained), 2)
        self.assertIn(replacement, [path.read_bytes() for path in retained])
        self.assertTrue(any("retained" in note for note in caught.exception.__notes__))

    def test_source_substitution_before_publish_cannot_publish_foreign_bytes(self) -> None:
        foreign = b"substituted foreign bytes\n"

        def substitute(namespace, private_name: str) -> None:
            private = namespace.path / private_name
            private.rename(namespace.path / f"{private_name}.displaced")
            private.write_bytes(foreign)

        real_publish = path_identity._publish_held_file

        def exact_race(descriptor, namespace, private_name, target_name):
            substitute(namespace, private_name)
            return real_publish(
                descriptor, namespace, private_name, target_name
            )

        with patch.object(
            path_identity, "_publish_held_file", side_effect=exact_race
        ):
            raw = sealer.seal_v4_contract(
                self.measurement, self.manifest, self.output
            )
        self.assertEqual(self.output.read_bytes(), raw)
        self.assertNotEqual(self.output.read_bytes(), foreign)
        self.assertIn(
            foreign,
            [
                path.read_bytes()
                for path in self.root.glob(".sm64-saturn-private-audit-v4.txt-*")
                if path.is_file()
            ],
        )

        class PosixNamespace:
            _fd = 19

        with patch.object(path_identity.Path, "is_dir", return_value=True), \
                patch.object(path_identity.os, "link") as link:
            path_identity._publish_posix_handle(
                23, PosixNamespace(), "final.txt"
            )
        link.assert_called_once_with(
            "/proc/self/fd/23",
            "final.txt",
            dst_dir_fd=19,
            follow_symlinks=True,
        )
        with patch.object(path_identity.Path, "is_dir", return_value=False):
            with self.assertRaisesRegex(RuntimeError, "unsupported"):
                path_identity._publish_posix_handle(
                    23, PosixNamespace(), "final.txt"
                )

    def test_sealer_rejects_output_aliases_to_read_inputs(self) -> None:
        for name, source in (
            ("measurement", self.measurement),
            ("release manifest", self.manifest),
        ):
            with self.subTest(kind="hardlink", source=name):
                alias = self.root / f"{name.replace(' ', '-')}-hardlink.txt"
                os.link(source, alias)
                original = source.read_bytes()
                with self.assertRaisesRegex(ValueError, "aliases.*input"):
                    sealer.seal_v4_contract(self.measurement, self.manifest, alias)
                self.assertEqual(source.read_bytes(), original)
                alias.unlink()
            with self.subTest(kind="symlink", source=name):
                alias = self.root / f"{name.replace(' ', '-')}-symlink.txt"
                try:
                    alias.symlink_to(source)
                except (OSError, NotImplementedError):
                    continue
                original = source.read_bytes()
                with self.assertRaisesRegex(ValueError, "symlink|aliases.*input"):
                    sealer.seal_v4_contract(self.measurement, self.manifest, alias)
                self.assertEqual(source.read_bytes(), original)
                alias.unlink()


if __name__ == "__main__":
    unittest.main(verbosity=2)
