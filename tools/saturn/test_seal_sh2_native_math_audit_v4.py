#!/usr/bin/env python3
"""Host contracts for one-shot native-math audit-v4 sealing."""

from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from hermetic_manifest import canonical_json_bytes
from test_release_manifest import ReleaseFixture

import release_manifest
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


if __name__ == "__main__":
    unittest.main(verbosity=2)
