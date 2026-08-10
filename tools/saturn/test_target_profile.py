#!/usr/bin/env python3
"""Focused behavioral contract for hermetic Saturn target profiles."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "saturn"))

from hermetic_manifest import (  # noqa: E402
    canonical_json_bytes,
    normalize_repo_path,
    reject_case_collisions,
)
from target_profile import resolve_target_profile  # noqa: E402


CLASSES = (
    "route", "input", "camera", "cart", "level", "shared-data", "actor",
    "animation", "audio", "texture",
)
CONTENT_CLASSES = {"level", "shared-data", "actor", "animation", "audio", "texture"}
CONFIG = {"bootstrap_ticks": 600, "level_id": 9}


class TargetProfileTests(unittest.TestCase):
    """A mutation of profile/package data must not be able to silently reseal."""

    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.output = self.root / "out"
        self.config = dict(CONFIG)
        (self.root / "tools/saturn/profiles").mkdir(parents=True)
        (self.root / "tools/saturn/manifests").mkdir(parents=True)

    def tearDown(self) -> None:
        self.temp.cleanup()

    def _write_json(self, relative: str, document: object) -> Path:
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(document), encoding="utf-8")
        return path

    def _descriptor(self, package_class: str, package_id: str, payload: str) -> str:
        payload_path = self.root / payload
        payload_path.parent.mkdir(parents=True, exist_ok=True)
        payload_path.write_text(f"{package_class}:{package_id}\\n", encoding="utf-8")
        return self._write_json(
            f"tools/saturn/manifests/{package_class}-{package_id}.json",
            {
                "schema": "sm64-saturn-package-descriptor-v1",
                "package_class": package_class,
                "package_id": package_id,
                "inputs": [{"path": payload}],
            },
        ).relative_to(self.root).as_posix()

    def _profile(self, paths: list[str], *, profile_id: str = "synthetic-full",
                 release_enabled: bool = True) -> Path:
        return self._write_json(
            "tools/saturn/profiles/profile.json",
            {
                "schema": "sm64-saturn-target-profile-v1",
                "profile_id": profile_id,
                "release_enabled": release_enabled,
                "release_config": self.config,
                "package_classes": list(CLASSES),
                "package_descriptors": paths,
                "output_names": {
                    "elf": "game.elf", "source_dat": "SOURCE.DAT",
                    "iso": "game.iso", "cue": "game.cue",
                },
            },
        )

    def resolve_demo(self):
        descriptors = [self._descriptor(kind, "demo", f"payload/{kind}.bin")
                       for kind in CLASSES]
        return resolve_target_profile(
            self.root, self._profile(descriptors), self.config, self.output,
            mode="development",
        )

    def synthetic_full_profile(self, order: str) -> Path:
        descriptors: list[str] = []
        for kind in CLASSES:
            count = 2 if kind in CONTENT_CLASSES else 1
            for index in range(count):
                descriptors.append(self._descriptor(kind, f"{kind}-{index}",
                                                    f"payload/{kind}-{index}.bin"))
        if order == "reverse":
            descriptors.reverse()
        return self._profile(descriptors)

    def test_canonical_json_has_fixed_bytes(self) -> None:
        self.assertEqual(
            canonical_json_bytes({"z": 1, "a": "é"}),
            b'{"a":"\\u00e9","z":1}\n',
        )

    def test_normalize_repo_path_rejects_escape_and_case_collision(self) -> None:
        with self.assertRaisesRegex(ValueError, "escapes repository"):
            normalize_repo_path(self.root, "../outside")
        with self.assertRaisesRegex(ValueError, "case-colliding"):
            reject_case_collisions(["levels/bob/data.bin", "LEVELS/BOB/data.bin"])

    def test_unrelated_tool_change_does_not_reseal_demo_profile(self) -> None:
        first = self.resolve_demo()
        (self.root / "tools/saturn/unselected_capture.py").write_text("changed\\n")
        second = self.resolve_demo()
        self.assertEqual(first.canonical, second.canonical)
        self.assertEqual(first.package_set_sha256, second.package_set_sha256)

    def test_synthetic_full_profile_aggregates_multiple_packages_deterministically(self) -> None:
        first = resolve_target_profile(
            self.root, self.synthetic_full_profile(order="reverse"), self.config,
            self.output / "first", mode="development",
        )
        second = resolve_target_profile(
            self.root, self.synthetic_full_profile(order="forward"), self.config,
            self.output / "second", mode="development",
        )
        self.assertEqual(first.package_set_canonical, second.package_set_canonical)
        self.assertEqual(len(first.package_set_document["packages"]), 16)

    def test_payload_mutation_changes_only_its_class_aggregate_and_package_set(self) -> None:
        descriptors = [self._descriptor(kind, "only", f"payload/{kind}.bin")
                       for kind in CLASSES]
        profile = self._profile(descriptors)
        baseline = resolve_target_profile(self.root, profile, self.config, self.output / "base",
                                          mode="development")
        for kind in CLASSES:
            (self.root / f"payload/{kind}.bin").write_text(f"changed:{kind}\\n", encoding="utf-8")
            changed = resolve_target_profile(self.root, profile, self.config,
                                             self.output / kind, mode="development")
            self.assertNotEqual(baseline.package_class_hashes[kind],
                                changed.package_class_hashes[kind])
            self.assertEqual(
                {key: value for key, value in baseline.package_class_hashes.items() if key != kind},
                {key: value for key, value in changed.package_class_hashes.items() if key != kind},
            )
            self.assertNotEqual(baseline.package_set_sha256, changed.package_set_sha256)
            (self.root / f"payload/{kind}.bin").write_text(f"{kind}:only\\n", encoding="utf-8")

    def test_rejects_missing_duplicate_and_malformed_inputs(self) -> None:
        descriptor = self._descriptor("route", "one", "payload/route.bin")
        profile = self._profile([descriptor])
        for bad in (
            {"unknown": 1},
            {"release_config": {"bootstrap_ticks": "600", "level_id": 9}},
            {"package_descriptors": [descriptor, descriptor]},
        ):
            document = json.loads(profile.read_text(encoding="utf-8"))
            document.update(bad)
            candidate = self._write_json("tools/saturn/profiles/bad.json", document)
            with self.assertRaises(ValueError):
                resolve_target_profile(self.root, candidate, self.config, self.output, mode="development")

    def test_rejects_descriptor_schema_path_and_case_failures(self) -> None:
        descriptor = self._descriptor("route", "one", "payload/route.bin")
        profile = self._profile([descriptor])
        cases = (
            ({"inputs": []}, "missing payload"),
            ({"unknown": 1}, "unknown descriptor key"),
            ({"schema": "wrong"}, "invalid schema"),
            ({"inputs": [{"path": "/absolute.bin"}]}, "absolute path"),
            ({"inputs": [{"path": "../outside.bin"}]}, "escaping path"),
            ({"inputs": [{"path": "payload/route.bin"},
                         {"path": "PAYLOAD/ROUTE.BIN"}]}, "case collision"),
        )
        original = json.loads((self.root / descriptor).read_text(encoding="utf-8"))
        for update, _ in cases:
            altered = dict(original)
            altered.update(update)
            self._write_json(descriptor, altered)
            with self.assertRaises(ValueError):
                resolve_target_profile(self.root, profile, self.config, self.output,
                                       mode="development")
        self._write_json(descriptor, original)

    def test_incomplete_full_game_profile_cannot_release(self) -> None:
        with self.assertRaisesRegex(ValueError, "sm64-saturn-full.*release-enabled"):
            resolve_target_profile(
                ROOT, ROOT / "tools/saturn/profiles/sm64-saturn-full-v1.json",
                self.config, self.output, mode="release")


if __name__ == "__main__":
    unittest.main()
