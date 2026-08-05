#!/usr/bin/env python3
"""Fail-closed contracts for preserving the accepted A9A artifact trio."""

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

try:
    import archive_a9a_baseline as archive
except ModuleNotFoundError as error:
    raise AssertionError("A9A baseline archive helper is missing") from error


CONFIG_LABEL = (
    "e2-bob-demo-replay-camroute0-live-input-boot600-atan2v2-camv3-"
    "stage8-r6000-slave1-poly2-hot1-clip1-bsp1-frag0-pipe4"
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class ArchiveA9ABaselineTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.source = self.root / "build" / "saturn" / "sourceboot" / CONFIG_LABEL
        (self.source / "obj").mkdir(parents=True)
        self.paths = {
            "elf": self.source / "obj" / "sm64-saturn-sourceboot-e2.elf",
            "iso": self.source / "sm64-saturn-sourceboot-e2.iso",
            "cue": self.source / "sm64-saturn-sourceboot-e2.cue",
        }
        self.paths["elf"].write_bytes(b"accepted elf")
        self.paths["iso"].write_bytes(b"accepted iso")
        self.paths["cue"].write_text(
            'FILE "sm64-saturn-sourceboot-e2.iso" BINARY\n'
            "  TRACK 01 MODE1/2048\n"
            "    INDEX 01 00:00:00\n",
            encoding="ascii",
        )
        self.hashes = {kind: sha256(path) for kind, path in self.paths.items()}
        self.capture_path = self.root / "capture.json"
        self.profile_path = self.root / "profile.json"
        self.archive_dir = self.root / "archive"
        self.manifest_path = self.root / "manifest.json"
        self.capture = {
            "schema": "sm64-saturn-sourceboot-throughput-v1",
            "evidence_kind": "ymir-sourceboot-queue-throughput",
            "status": "complete",
            "artifacts": {
                kind: {"path": str(path), "sha256": self.hashes[kind]}
                for kind, path in self.paths.items()
            },
            "target_identity": {"match": True, "expected_sha256": "11" * 32,
                                "observed_sha256": "11" * 32},
            "observation": {"measurement": {"guest_fps_mean": 5.294117647058823}},
        }
        self.profile = {
            "execution": {"requested": True, "alive_after_monitor": True},
            "plan": {
                "launcher": "desktop-ymir-profile",
                "profile": str(self.root / ".ymir-profile"),
                "ram_cart": "profile-managed-32-mbit-dram",
                "cue": {"path": str(self.paths["cue"]), "sha256": self.hashes["cue"]},
                "iso": {"path": str(self.paths["iso"]), "sha256": self.hashes["iso"]},
            },
        }

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write_evidence(self) -> None:
        self.capture_path.write_text(json.dumps(self.capture), encoding="utf-8")
        self.profile_path.write_text(json.dumps(self.profile), encoding="utf-8")

    def run_archive(self, **overrides: object) -> dict[str, object]:
        self.write_evidence()
        arguments = {
            "capture_report": self.capture_path,
            "profile_report": self.profile_path,
            "archive_dir": self.archive_dir,
            "manifest_path": self.manifest_path,
            "repo_root": self.root,
            "expected_hashes": self.hashes,
            "required_commits": ("one", "two", "three"),
            "ancestry_check": lambda _repo, _commit: True,
        }
        arguments.update(overrides)
        return archive.archive_baseline(**arguments)

    def test_archives_exact_trio_and_records_capture_profile_config_and_ancestry(self) -> None:
        manifest = self.run_archive()
        self.assertEqual(manifest["schema"], "sm64-saturn-a9a-baseline-v1")
        self.assertEqual(manifest["measurement"]["guest_fps_mean"], 5.294117647058823)
        self.assertEqual(manifest["config_evidence"]["output_label"], CONFIG_LABEL)
        self.assertEqual(manifest["profile_evidence"]["ram_cart"],
                         "profile-managed-32-mbit-dram")
        self.assertEqual(set(manifest["ancestry"]), {"one", "two", "three"})
        for kind, expected_hash in self.hashes.items():
            archived = Path(manifest["artifacts"][kind]["archive_path"])
            self.assertTrue(archived.is_file())
            self.assertEqual(sha256(archived), expected_hash)

    def test_rejects_each_wrong_source_hash_without_copying(self) -> None:
        for kind in ("elf", "iso", "cue"):
            with self.subTest(kind=kind):
                wrong = dict(self.hashes)
                wrong[kind] = "00" * 32
                with self.assertRaisesRegex(ValueError, f"{kind} SHA-256"):
                    self.run_archive(expected_hashes=wrong)
                self.assertFalse(self.archive_dir.exists())

    def test_rejects_cue_that_names_a_different_iso(self) -> None:
        self.paths["cue"].write_text('FILE "other.iso" BINARY\n', encoding="ascii")
        self.hashes["cue"] = sha256(self.paths["cue"])
        self.capture["artifacts"]["cue"]["sha256"] = self.hashes["cue"]
        self.profile["plan"]["cue"]["sha256"] = self.hashes["cue"]
        with self.assertRaisesRegex(ValueError, "CUE.*ISO"):
            self.run_archive()

    def test_rejects_missing_capture_evidence(self) -> None:
        self.capture.pop("target_identity")
        with self.assertRaisesRegex(ValueError, "capture"):
            self.run_archive()

    def test_rejects_missing_config_evidence(self) -> None:
        bad_source = self.source.with_name("unreviewed-config")
        self.source.rename(bad_source)
        self.paths = {
            "elf": bad_source / "obj" / self.paths["elf"].name,
            "iso": bad_source / self.paths["iso"].name,
            "cue": bad_source / self.paths["cue"].name,
        }
        for kind, path in self.paths.items():
            self.capture["artifacts"][kind]["path"] = str(path)
        self.profile["plan"]["cue"]["path"] = str(self.paths["cue"])
        self.profile["plan"]["iso"]["path"] = str(self.paths["iso"])
        with self.assertRaisesRegex(ValueError, "config"):
            self.run_archive()

    def test_rejects_missing_profile_evidence(self) -> None:
        self.profile["plan"].pop("ram_cart")
        with self.assertRaisesRegex(ValueError, "profile"):
            self.run_archive()

    def test_refuses_to_overwrite_a_different_archived_file(self) -> None:
        self.archive_dir.mkdir(parents=True)
        (self.archive_dir / self.paths["elf"].name).write_bytes(b"different")
        with self.assertRaisesRegex(ValueError, "refuse.*overwrite"):
            self.run_archive()

    def test_rejects_failed_preserved_commit_ancestry(self) -> None:
        with self.assertRaisesRegex(ValueError, "ancestry.*two"):
            self.run_archive(
                ancestry_check=lambda _repo, commit: commit != "two",
            )
        self.assertFalse(self.archive_dir.exists())


if __name__ == "__main__":
    unittest.main()
