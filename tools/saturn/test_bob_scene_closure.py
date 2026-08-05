#!/usr/bin/env python3
"""Integration contract for BOB's generated generic closure."""
from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from collect_scene_closure import collect_scene_closure, write_closure
from scene_package_schema import validate_scene_closure


ROOT = Path(__file__).resolve().parents[2]


class BobSceneClosureTest(unittest.TestCase):
    def test_bob_closure_is_byte_stable_and_complete(self) -> None:
        with tempfile.TemporaryDirectory(prefix="bob-scene-closure-") as temp:
            first_path = Path(temp) / "first.json"
            second_path = Path(temp) / "second.json"
            first = collect_scene_closure(ROOT, "bob", 1, ROOT / "tools/saturn/behavior_spawn_rules.json")
            second = collect_scene_closure(ROOT, "bob", 1, ROOT / "tools/saturn/behavior_spawn_rules.json")
            write_closure(first_path, first)
            write_closure(second_path, second)
            self.assertEqual(first_path.read_bytes(), second_path.read_bytes())
            validate_scene_closure(first)
            records = {record["stable_id"]: record for record in first["records"]}
            self.assertEqual(records["bhvGoomba"]["maximum_live_instances"], 11)
            self.assertIn("bhvGoomba", records["bhvGoombaTripletSpawner"]["spawned_children"])
            self.assertEqual(records["bhvGoombaTripletSpawner"]["model"], "MODEL_NONE")
            self.assertIn("bhvWaterBomb", records["bhvWaterBombSpawner"]["spawned_children"])
            self.assertIn("bhvBowlingBall", records["bhvBobBowlingBallSpawner"]["spawned_children"])
            self.assertIn("bhvBowlingBall", records["bhvTtmBowlingBallSpawner"]["spawned_children"])
            self.assertIn("bhvStar", records["bhvKingBobomb"]["spawned_children"])
            self.assertTrue(first["source_hashes"])
            self.assertEqual(set(first["source_hashes"]), {source["path"] for record in first["records"] for source in record["sources"]} | set(first["scene_sources"]))


if __name__ == "__main__":
    unittest.main()
