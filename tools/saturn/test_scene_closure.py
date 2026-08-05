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

from collect_scene_closure import ClosureError, _file_source_regions, _native_discovery, _native_spawn_edges, _native_symbol_index, _reachable_native_regions, _rules, collect_scene_closure
from scene_package_schema import validate_scene_closure


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


class SceneClosureTest(unittest.TestCase):
    def fixture(self, with_cycle: bool = False) -> Path:
        root = Path(tempfile.mkdtemp(prefix="scene-closure-"))
        write(root / "levels/test/script.c", """
            LOAD_MODEL_FROM_GEO(MODEL_PARENT, parent_geo),
            LOAD_MODEL_FROM_GEO(MODEL_CHILD, child_geo),
            static const LevelScript script_func_nested[] = {
                SET_BACKGROUND_MUSIC(0, SEQ_LEVEL_TEST),
                RETURN(),
            };
            AREA(1, test_area_geo),
            OBJECT_WITH_ACTS(MODEL_PARENT, 0, 0, 0, 0, 0, 0, 0, bhvParent, ACT_1 | ACT_3),
            OBJECT(MODEL_NONE, 0, 0, 0, 0, 0, 0, 0, bhvController),
            MACRO_OBJECTS(test_macro_objs),
            JUMP_LINK(script_func_nested),
            END_AREA(),
            AREA(2, test_area_2_geo),
            SET_BACKGROUND_MUSIC(0, SEQ_LEVEL_MUST_NOT_LEAK),
            END_AREA(),
        """)
        write(root / "levels/test/areas/1/macro.inc.c", """
            MACRO_OBJECT(macro_child, 0, 0, 0, 0),
            """ + ("MACRO_OBJECT(macro_cycle, 0, 0, 0, 0)," if with_cycle else "") + """
            MACRO_OBJECT_END(),
        """)
        write(root / "include/macro_presets.h", """
            {bhvChild, MODEL_CHILD, 0}, // macro_child
            {bhvCycle, MODEL_NONE, 0}, // macro_cycle
        """)
        write(root / "data/behavior_data.c", """
            const BehaviorScript bhvParent[] = { LOAD_ANIMATIONS(a, parent_anims), SPAWN_CHILD(MODEL_CHILD, bhvChild), SPAWN_CHILD(MODEL_YELLOW_COIN, bhvReward), SPAWN_OBJ(MODEL_WATER_BOMB, bhvProjectile), SPAWN_CHILD(MODEL_SMOKE, bhvEffect) };
            const BehaviorScript bhvChild[] = { BILLBOARD(), SPAWN_CHILD(MODEL_NONE, bhvController) };
            const BehaviorScript bhvController[] = {""" + (" SPAWN_CHILD(MODEL_PARENT, bhvParent) " if with_cycle else "") + """};
            const BehaviorScript bhvCycle[] = { SPAWN_CHILD(MODEL_NONE, bhvCycle) };
            const BehaviorScript bhvReward[] = {};
            const BehaviorScript bhvProjectile[] = {};
            const BehaviorScript bhvEffect[] = {};
        """)
        write(root / "actors/parent/geo.inc.c", "const GeoLayout parent_geo[] = { GEO_ANIMATED_PART(0, 0, 0, 0, parent_dl), GEO_SHADOW(1, 2, 3) };\n")
        write(root / "actors/parent/anims/table.inc.c", "const struct Animation *const parent_anims[] = { 0 };\n")
        write(root / "actors/child/geo.inc.c", "const GeoLayout child_geo[] = { GEO_BILLBOARD(), GEO_DISPLAY_LIST(LAYER_ALPHA, child_dl) };\n")
        write(root / "actors/fixture_roots/geo.inc.c", "const GeoLayout yellow_coin_geo[] = { 0 };\nconst GeoLayout water_bomb_geo[] = { 0 };\nconst GeoLayout smoke_geo[] = { 0 };\n")
        write(root / "include/model_ids.h", "#define MODEL_NONE 0\n#define MODEL_PARENT 1 // parent_geo\n#define MODEL_CHILD 2 // child_geo\n#define MODEL_YELLOW_COIN 3 // yellow_coin_geo\n#define MODEL_WATER_BOMB 4 // water_bomb_geo\n#define MODEL_SMOKE 5 // smoke_geo\n")
        write(root / "src/game/object_list_processor.h", "#define OBJECT_POOL_CAPACITY 240\n")
        return root

    def collect(self, root: Path) -> dict:
        return collect_scene_closure(root, "test", 1, root / "rules.json")

    def native_rule_fixture(self) -> Path:
        root = self.fixture()
        write(root / "data/behavior_data.c", """
            const BehaviorScript bhvParent[] = { CALL_NATIVE(bhv_parent_loop) };
            const BehaviorScript bhvChild[] = {};
            const BehaviorScript bhvController[] = {};
            const BehaviorScript bhvCycle[] = {};
            const BehaviorScript bhvReward[] = {};
            const BehaviorScript bhvProjectile[] = {};
            const BehaviorScript bhvEffect[] = {};
        """)
        write(root / "src/game/behaviors/parent.inc.c", """
            void bhv_parent_loop(void) {
                spawn_object(o, MODEL_CHILD, bhvChild);
            }
        """)
        write(root / "src/game/behaviors/unrelated.inc.c", """
            void bhv_unrelated_loop(void) {
                spawn_object(o, MODEL_CHILD, bhvChild);
            }
        """)
        write(root / "rules.json", json.dumps({
            "schema": "sm64-saturn-behavior-spawn-rules-v2",
            "rules": [{
                "behavior": "bhvParent",
                "owner": "bhv_parent_loop",
                "source": "src/game/behaviors/parent.inc.c",
                "reason": "The native owner creates one child.",
                "children": [{
                    "model": "MODEL_CHILD",
                    "behavior": "bhvChild",
                    "maximum_instances": 1,
                    "edge": {"location": "bhv_parent_loop", "expression": "spawn_object(o, MODEL_CHILD, bhvChild)"},
                    "capacity": {"location": "bhv_parent_loop", "expression": "spawn_object(o, MODEL_CHILD, bhvChild)", "kind": "single"},
                }],
            }],
        }))
        return root

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
        self.assertEqual(records["bhvParent"]["root_provenance"]["animation"]["parent_anims"], "actors/parent/anims/table.inc.c")
        self.assertEqual(records["bhvParent"]["root_provenance"]["geo"], "actors/parent/geo.inc.c")

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
        script = (root / "levels/test/script.c").read_text(encoding="utf-8")
        write(root / "levels/test/script.c", script.replace("LOAD_MODEL_FROM_GEO(MODEL_CHILD, child_geo),", ""))
        model_ids = (root / "include/model_ids.h").read_text(encoding="utf-8")
        write(root / "include/model_ids.h", model_ids.replace("#define MODEL_CHILD 2 // child_geo\n", ""))
        with self.assertRaisesRegex(ClosureError, "no geo root for MODEL_CHILD"):
            self.collect(root)

    def test_rejects_reachable_behavior_cycles(self) -> None:
        with self.assertRaisesRegex(ClosureError, "behavior spawn cycle"):
            self.collect(self.fixture(with_cycle=True))

    def test_rejects_unknown_computed_native_spawn_arguments(self) -> None:
        with self.assertRaisesRegex(ClosureError, "unrecognized dynamic native spawn form"):
            _native_spawn_edges("spawn_object(o, model_from_table, behavior_from_table);")

    def test_cross_file_helper_creation_is_discovered_hashed_and_unknown_rejected(self) -> None:
        root = self.native_rule_fixture()
        write(root / "src/game/behaviors/parent.inc.c", "void bhv_parent_loop(void) { create_child(); }\n")
        write(root / "src/game/create_child.c", "void create_child(void) { spawn_object(o, model_from_table, behavior_from_table); }\n")
        write(root / "rules.json", json.dumps({"schema": "sm64-saturn-behavior-spawn-rules-v2", "rules": []}))
        with self.assertRaisesRegex(ClosureError, "unrecognized dynamic native spawn form"):
            self.collect(root)
        write(root / "src/game/create_child.c", "void create_child(void) { spawn_object(o, MODEL_CHILD, bhvChild); }\n")
        _file_source_regions.cache_clear()
        _native_symbol_index.cache_clear()
        _reachable_native_regions.cache_clear()
        _native_discovery.cache_clear()
        closure = self.collect(root)
        parent = next(record for record in closure["records"] if record["stable_id"] == "bhvParent")
        self.assertIn("bhvChild", parent["spawned_children"])
        self.assertIn("src/game/create_child.c", {source["path"] for source in parent["sources"]})

    def test_recurring_native_creation_requires_a_source_attested_live_bound(self) -> None:
        root = self.native_rule_fixture()
        behavior = (root / "data/behavior_data.c").read_text(encoding="utf-8")
        write(root / "data/behavior_data.c", behavior.replace(
            "const BehaviorScript bhvParent[] = { CALL_NATIVE(bhv_parent_loop) };",
            "const BehaviorScript bhvParent[] = { BEGIN_LOOP(), CALL_NATIVE(bhv_parent_loop), END_LOOP() };",
        ))
        (root / "src/game/object_list_processor.h").unlink()
        with self.assertRaisesRegex(ClosureError, "no source-attested maximum-live bound"):
            self.collect(root)

    def test_rules_are_repo_relative_hash_covered_and_source_attested(self) -> None:
        root = self.native_rule_fixture()
        closure = self.collect(root)
        self.assertIn("rules.json", closure["scene_sources"])
        self.assertIn("rules.json", closure["source_hashes"])
        outside = Path(tempfile.mkdtemp(prefix="external-rules-")) / "rules.json"
        outside.write_bytes((root / "rules.json").read_bytes())
        with self.assertRaisesRegex(ClosureError, "rule file outside repository"):
            collect_scene_closure(root, "test", 1, outside)

    def test_rules_reject_unattested_owner_edge_capacity_and_duplicates(self) -> None:
        mutations = [
            (lambda rule: rule.update(source="src/game/behaviors/unrelated.inc.c"), "does not define owner"),
            (lambda rule: rule["children"][0]["edge"].update(expression="spawn_object(o, MODEL_CHILD, bhvMissing)"), "edge expression"),
            (lambda rule: rule["children"][0].update(maximum_instances=2), "capacity expression"),
            (lambda rule: rule.update(source="../parent.inc.c"), "repository-relative"),
            (lambda rule: rule["children"].append(dict(rule["children"][0])), "duplicate manual rule edge"),
        ]
        for mutate, message in mutations:
            with self.subTest(message=message):
                root = self.native_rule_fixture()
                payload = json.loads((root / "rules.json").read_text(encoding="utf-8"))
                mutate(payload["rules"][0])
                write(root / "rules.json", json.dumps(payload))
                with self.assertRaisesRegex(ClosureError, message):
                    _rules(root, root / "rules.json")
        root = self.native_rule_fixture()
        payload = json.loads((root / "rules.json").read_text(encoding="utf-8"))
        payload["rules"].append(dict(payload["rules"][0]))
        write(root / "rules.json", json.dumps(payload))
        with self.assertRaisesRegex(ClosureError, "duplicate manual rule behavior"):
            _rules(root, root / "rules.json")

    def test_audio_is_behavior_scoped_bank_bound_and_declaration_hashed(self) -> None:
        root = self.native_rule_fixture()
        write(root / "src/game/behaviors/parent.inc.c", """
            static void parent_sound_helper(void) {
                cur_obj_play_sound_2(SOUND_OBJ_USED_BY_PARENT);
            }
            static void unrelated_sound_helper(void) {
                cur_obj_play_sound_2(SOUND_GENERAL_MUST_NOT_LEAK);
            }
            void bhv_parent_loop(void) {
                parent_sound_helper();
                spawn_object(o, MODEL_CHILD, bhvChild);
            }
        """)
        write(root / "include/sounds.h", """
            #define SOUND_OBJ_USED_BY_PARENT SOUND_ARG_LOAD(SOUND_BANK_OBJ, 1, 2, 3)
            #define SOUND_GENERAL_MUST_NOT_LEAK SOUND_ARG_LOAD(SOUND_BANK_GENERAL, 4, 5, 6)
        """)
        closure = self.collect(root)
        parent = next(record for record in closure["records"] if record["stable_id"] == "bhvParent")
        self.assertEqual(parent["sfx_ids"], ["SOUND_OBJ_USED_BY_PARENT"])
        self.assertEqual(parent["sfx_banks"], ["obj"])
        self.assertIn("include/sounds.h", {source["path"] for source in parent["sources"]})
        self.assertIn("include/sounds.h", closure["source_hashes"])

    def test_audio_rejects_missing_and_ambiguous_sound_bank_declarations(self) -> None:
        for declarations, message in [
            ("", "missing SOUND_ARG_LOAD declaration"),
            ("""
                #define SOUND_OBJ_USED_BY_PARENT SOUND_ARG_LOAD(SOUND_BANK_OBJ, 1, 2, 3)
                #define SOUND_OBJ_USED_BY_PARENT SOUND_ARG_LOAD(SOUND_BANK_GENERAL, 1, 2, 3)
            """, "ambiguous SOUND_ARG_LOAD declaration"),
        ]:
            with self.subTest(message=message):
                root = self.native_rule_fixture()
                write(root / "src/game/behaviors/parent.inc.c", """
                    void bhv_parent_loop(void) {
                        cur_obj_play_sound_2(SOUND_OBJ_USED_BY_PARENT);
                        spawn_object(o, MODEL_CHILD, bhvChild);
                    }
                """)
                write(root / "include/sounds.h", declarations)
                with self.assertRaisesRegex(ClosureError, message):
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
        closure = self.collect(self.fixture())
        del closure["records"][0]["effects"]
        with self.assertRaisesRegex(ValueError, "missing record field"):
            validate_scene_closure(closure)

    def test_schema_enforces_references_typed_lists_scope_audio_and_provenance(self) -> None:
        mutations = [
            (lambda closure: closure["records"][0]["spawned_children"].append("bhvMissing"), "unknown child reference"),
            (lambda closure: closure["records"][0]["effects"].append("bhvChild"), "typed child lists disagree"),
            (lambda closure: closure["records"][0].update(level="other"), "record level/area mismatch"),
            (lambda closure: closure.update(sfx_ids=["SOUND_MISSING"]), "audio union mismatch"),
            (lambda closure: closure["records"][0].update(root_provenance={"behavior": "actors/parent/geo.inc.c", "model": "include/model_ids.h", "geo": None, "animation": {}}), "invalid root provenance"),
        ]
        for mutate, message in mutations:
            with self.subTest(message=message):
                closure = self.collect(self.fixture())
                mutate(closure)
                with self.assertRaisesRegex(ValueError, message):
                    validate_scene_closure(closure)

    def test_missing_animation_definition_fails_closed(self) -> None:
        root = self.fixture()
        (root / "actors/parent/anims/table.inc.c").unlink()
        with self.assertRaisesRegex(ClosureError, "unresolved animation root parent_anims"):
            self.collect(root)


if __name__ == "__main__":
    unittest.main()
