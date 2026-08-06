#!/usr/bin/env python3
import hashlib
import json
import tempfile
import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_sourceboot_variant as builder


class VariantTests(unittest.TestCase):
    def test_identity_rejects_missing_or_changed_artifact(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            paths = {}
            for kind in ("elf", "cue", "iso"):
                path = root / kind
                path.write_bytes(kind.encode())
                paths[kind] = {"path": str(path), "sha256": builder.sha256(path)}
            manifest = root / "artifact.json"
            manifest.write_text(json.dumps({
                "schema": "sm64-saturn-sourceboot-variant-v1", "label": "x",
                "features": {"complete_mario_animation": 1,
                              "dynamic_actor_closure": 0, "semantic_audio": 0},
                "diagnostic_mode": 1, "artifacts": paths,
            }), encoding="utf-8")
            self.assertEqual(builder.artifact_identity(manifest)["label"], "x")
            mutated = json.loads(manifest.read_text())
            mutated["artifacts"]["elf"]["sha256"] = "0" * 64
            manifest.write_text(json.dumps(mutated), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "hash"):
                builder.artifact_identity(manifest)

    def test_jobs_are_serial_only(self):
        with self.assertRaisesRegex(ValueError, "serialized"):
            builder.build_variant(label="x", animation=1, actors=0, audio=0,
                                  pipeline=4, diagnostic_mode="animation-sweep",
                                  jobs=2, output=Path("x"), repo_root=Path("."))


if __name__ == "__main__":
    unittest.main()
