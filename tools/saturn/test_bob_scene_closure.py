#!/usr/bin/env python3
"""Integration contract for BOB's generated generic closure."""
from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from collect_scene_closure import ClosureError, _rules, collect_scene_closure, find_unruled_native_spawn_sites, write_closure
from scene_package_schema import validate_scene_closure


ROOT = Path(__file__).resolve().parents[2]


class BobSceneClosureTest(unittest.TestCase):
    def test_bob_closure_is_byte_stable_and_complete(self) -> None:
        with tempfile.TemporaryDirectory(prefix="bob-scene-closure-") as temp:
            first_path = Path(temp) / "first.json"
            second_path = Path(temp) / "second.json"
            first = collect_scene_closure(ROOT, "bob", 1, ROOT / "tools/saturn/behavior_spawn_rules.json")
            second = collect_scene_closure(ROOT, "bob", 1, ROOT / "tools/saturn/behavior_spawn_rules.json")
            first_hash = write_closure(first_path, first)
            second_hash = write_closure(second_path, second)
            self.assertEqual(first_path.read_bytes(), second_path.read_bytes())
            self.assertEqual(first_hash, hashlib.sha256(first_path.read_bytes()).hexdigest())
            self.assertEqual(second_hash, hashlib.sha256(second_path.read_bytes()).hexdigest())
            validate_scene_closure(first)
            self.assertEqual(len(first["records"]), 86)
            self.assertEqual(len(first["source_hashes"]), 127)
            records = {record["stable_id"]: record for record in first["records"]}
            self.assertEqual(records["bhvGoomba"]["maximum_live_instances"], 11)
            self.assertIn("bhvGoomba", records["bhvGoombaTripletSpawner"]["spawned_children"])
            self.assertEqual(records["bhvGoombaTripletSpawner"]["model"], "MODEL_NONE")
            self.assertIn("bhvWaterBomb", records["bhvWaterBombSpawner"]["spawned_children"])
            self.assertIn("bhvBowlingBall", records["bhvBobBowlingBallSpawner"]["spawned_children"])
            self.assertIn("bhvBowlingBall", records["bhvTtmBowlingBallSpawner"]["spawned_children"])
            self.assertIn("bhvStarSpawnCoordinates", records["bhvKingBobomb"]["spawned_children"])
            self.assertEqual(records["bhvCheckerboardElevatorGroup"]["spawned_children"], ["bhvCheckerboardPlatformSub"])
            self.assertEqual(records["bhvOpenableGrill"]["spawned_children"], ["bhvOpenableCageDoor"])
            self.assertIn("bhvCannon", records["bhvCannonClosed"]["spawned_children"])
            self.assertEqual(set(records["bhvHidden1upInPoleSpawner"]["spawned_children"]), {"bhvHidden1upInPole", "bhvHidden1upInPoleTrigger"})
            self.assertIn("bhvBobombExplosionBubble", records["bhvExplosion"]["effects"])
            self.assertTrue({"bhvObjectWaveTrail", "bhvWaterDroplet", "bhvKoopaShellFlame", "bhvSparkleSpawn"}.issubset(records["bhvKoopaShell"]["effects"]))
            self.assertTrue({"bhvRotatingExclamationMark", "bhvWingCap", "bhvSpawnedStar"}.issubset(records["bhvExclamationBox"]["spawned_children"]))
            self.assertIn("bhvStar", records["bhvHiddenRedCoinStar"]["spawned_children"])
            self.assertEqual(records["bhvWoodenPost"]["rewards"], ["bhvSingleCoinGetsSpawned"])
            self.assertEqual(records["bhvMovingYellowCoin"]["effects"], ["bhvGoldenCoinSparkles", "bhvObjectBubble", "bhvObjectWaterWave"])
            self.assertIn("include/sounds.h", first["source_hashes"])
            self.assertIn("bhvRespawner", records["bhvBobomb"]["spawned_children"])
            self.assertIn("bhvBobomb", records["bhvRespawner"]["spawned_children"])
            self.assertIn("bhvWhitePuffExplosion", records["bhvKingBobomb"]["effects"])
            self.assertIn("bhvWhitePuffExplosion", records["bhvKoopaShell"]["effects"])
            self.assertEqual(records["bhvWhitePuffExplosion"]["maximum_live_instances"], 240)
            self.assertTrue({"src/game/behavior_actions.c", "src/game/object_helpers.c", "src/game/behaviors/white_puff_explode.inc.c"}.issubset(first["source_hashes"]))
            self.assertEqual(records["bhvChainChomp"]["geo_root"], "chain_chomp_geo")
            self.assertIn("actors/chain_chomp/geo.inc.c", first["source_hashes"])
            self.assertIn("actors/chain_chomp/anims/table.inc.c", first["source_hashes"])
            self.assertIn("tools/saturn/behavior_spawn_rules.json", first["source_hashes"])
            self.assertEqual(find_unruled_native_spawn_sites(ROOT, first), [])
            self.assertTrue(first["source_hashes"])
            self.assertEqual(set(first["source_hashes"]), {source["path"] for record in first["records"] for source in record["sources"]} | set(first["scene_sources"]))
            rules = json.loads((ROOT / "tools/saturn/behavior_spawn_rules.json").read_text(encoding="utf-8"))
            water_rule = next(rule for rule in rules["rules"] if rule["behavior"] == "bhvWaterBombSpawner")
            shadow = next(child for child in water_rule["children"] if child["behavior"] == "bhvWaterBombShadow")
            shadow["maximum_instances"] = 2
            with tempfile.TemporaryDirectory(prefix="water-shadow-rule-", dir=ROOT / "tools/saturn") as rule_temp:
                mutated_rules = Path(rule_temp) / "rules.json"
                mutated_rules.write_text(json.dumps(rules), encoding="utf-8")
                with self.assertRaisesRegex(ClosureError, "capacity expression attests 1, not 2"):
                    _rules(ROOT, mutated_rules)


if __name__ == "__main__":
    unittest.main()
