#!/usr/bin/env python3
"""Contract tests for the generic LevelScript dependency closure collector.

These fixtures name the production breaks they catch: losing an indirect
spawn, accepting an incomplete record, or accepting an unaudited schema.
"""
from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from collect_scene_closure import ClosureError, collect_scene_closure
from scene_package_schema import validate_scene_closure


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


class SceneClosureTest(unittest.TestCase):
    def fixture(self) -> Path:
        root = Path(tempfile.mkdtemp(prefix="scene-closure-"))
        write(root / "levels/test/script.c", """
            LOAD_MODEL_FROM_GEO(MODEL_PARENT, parent_geo),
            LOAD_MODEL_FROM_GEO(MODEL_CHILD, child_geo),
            AREA(1, test_area_geo),
            OBJECT_WITH_ACTS(MODEL_PARENT, 0, 0, 0, 0, 0, 0, 0, bhvParent, ACT_1 | ACT_3),
            OBJECT(MODEL_NONE, 0, 0, 0, 0, 0, 0, 0, bhvController),
            MACRO_OBJECTS(test_macro_objs),
            SET_BACKGROUND_MUSIC(0, SEQ_LEVEL_TEST),
            END_AREA(),
        """)
        write(root / "levels/test/areas/1/macro.inc.c", """
            MACRO_OBJECT(macro_child, 0, 0, 0, 0),
            MACRO_OBJECT(macro_cycle, 0, 0, 0, 0),
            MACRO_OBJECT_END(),
        """)
        write(root / "include/macro_presets.h", """
            {bhvChild, MODEL_CHILD, 0}, // macro_child
            {bhvCycle, MODEL_NONE, 0}, // macro_cycle
        """)
        write(root / "data/behavior_data.c", """
            const BehaviorScript bhvParent[] = { LOAD_ANIMATIONS(a, parent_anims), SPAWN_CHILD(MODEL_CHILD, bhvChild), SPAWN_CHILD(MODEL_YELLOW_COIN, bhvReward), SPAWN_OBJ(MODEL_WATER_BOMB, bhvProjectile), SPAWN_CHILD(MODEL_SMOKE, bhvEffect) };
            const BehaviorScript bhvChild[] = { BILLBOARD(), SPAWN_CHILD(MODEL_NONE, bhvController) };
            const BehaviorScript bhvController[] = { SPAWN_CHILD(MODEL_PARENT, bhvParent) };
            const BehaviorScript bhvCycle[] = { SPAWN_CHILD(MODEL_NONE, bhvCycle) };
            const BehaviorScript bhvReward[] = {};
            const BehaviorScript bhvProjectile[] = {};
            const BehaviorScript bhvEffect[] = {};
        """)
        write(root / "actors/parent/geo.inc.c", "const GeoLayout parent_geo[] = { GEO_ANIMATED_PART(0, 0, 0, 0, parent_dl), GEO_SHADOW(1, 2, 3) };\n")
        write(root / "actors/child/geo.inc.c", "const GeoLayout child_geo[] = { GEO_BILLBOARD(), GEO_DISPLAY_LIST(LAYER_ALPHA, child_dl) };\n")
        write(root / "include/model_ids.h", "#define MODEL_PARENT 1 // parent_geo\n#define MODEL_CHILD 2 // child_geo\n#define MODEL_YELLOW_COIN 3 // yellow_coin_geo\n#define MODEL_WATER_BOMB 4 // water_bomb_geo\n#define MODEL_SMOKE 5 // smoke_geo\n")
        return root

    def collect(self, root: Path) -> dict:
        return collect_scene_closure(root, "test", 1, root / "rules.json")

    def test_collects_transitive_generic_dependencies_deterministically(self) -> None:
        root = self.fixture()
        first = self.collect(root)
        second = self.collect(root)
        self.assertEqual(json.dumps(first, sort_keys=True), json.dumps(second, sort_keys=True))
        records = {record["stable_id"]: record for record in first["records"]}
        self.assertEqual(first["schema"], "sm64-saturn-scene-closure-v1")
        self.assertIn("ACT_1", records["bhvParent"]["act_mask"])
        self.assertIn("ACT_3", records["bhvParent"]["act_mask"])
        self.assertIn("bhvChild", records["bhvParent"]["spawned_children"])
        self.assertEqual(records["bhvParent"]["rewards"], ["bhvReward"])
        self.assertEqual(records["bhvParent"]["projectiles"], ["bhvProjectile"])
        self.assertEqual(records["bhvParent"]["effects"], ["bhvEffect"])
        self.assertIn("bhvController", records["bhvChild"]["spawned_children"])
        self.assertEqual(records["bhvController"]["model"], "MODEL_NONE")
        self.assertIn("animated", records["bhvParent"]["material_feature_bits"])
        self.assertIn("billboard", records["bhvChild"]["material_feature_bits"])
        self.assertEqual(first["music_sequence_ids"], ["SEQ_LEVEL_TEST"])
        self.assertTrue(all(record["sources"] for record in first["records"]))

    def test_rejects_undeclared_child_and_missing_model_geo(self) -> None:
        root = self.fixture()
        write(root / "data/behavior_data.c", """
            const BehaviorScript bhvParent[] = { SPAWN_CHILD(MODEL_BAD, bhvMissing) };
            const BehaviorScript bhvController[] = {};
            const BehaviorScript bhvChild[] = {};
            const BehaviorScript bhvCycle[] = {};
            const BehaviorScript bhvReward[] = {};
            const BehaviorScript bhvProjectile[] = {};
            const BehaviorScript bhvEffect[] = {};
        """)
        with self.assertRaisesRegex(ClosureError, "undeclared behavior bhvMissing"):
            self.collect(root)
        root = self.fixture()
        write(root / "include/model_ids.h", "#define MODEL_PARENT 1 // parent_geo\n")
        with self.assertRaisesRegex(ClosureError, "no geo root for MODEL_CHILD"):
            self.collect(root)

    def test_schema_rejects_stale_hash_duplicate_id_and_bob_only_field(self) -> None:
        closure = self.collect(self.fixture())
        closure["records"][0]["sources"][0]["sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "stale source hash"):
            validate_scene_closure(closure)
        closure = self.collect(self.fixture())
        closure["records"].append(dict(closure["records"][0]))
        with self.assertRaisesRegex(ValueError, "duplicate stable_id"):
            validate_scene_closure(closure)
        closure = self.collect(self.fixture())
        closure["bob_only_special_case"] = True
        with self.assertRaisesRegex(ValueError, "unknown field"):
            validate_scene_closure(closure)


if __name__ == "__main__":
    unittest.main()
