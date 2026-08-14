#!/usr/bin/env python3
"""Contract tests for the generic LevelScript dependency closure collector.

These fixtures name the production breaks they catch: losing an indirect
spawn, accepting an incomplete record, or accepting an unaudited schema.
"""
from __future__ import annotations

import hashlib
import json
import os
import tempfile
import unittest
from pathlib import Path

from collect_scene_closure import ClosureError, _file_source_regions, _native_discovery, _native_spawn_edges, _native_symbol_index, _reachable_native_regions, _rules, collect_scene_closure, write_closure
from scene_package_schema import validate_scene_closure


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


class SceneClosureTest(unittest.TestCase):
    def test_byte_identical_publication_preserves_existing_file_identity(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "closure.json"
            document = {"schema": "fixture", "source_root": str(Path(directory))}
            first = write_closure(output, document)
            fixed_ns = 1_700_000_000_000_000_000
            os.utime(output, ns=(fixed_ns, fixed_ns))
            second = write_closure(output, document)
            self.assertEqual(first, second)
            self.assertEqual(output.stat().st_mtime_ns, fixed_ns)

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
        write(root / "actors/parent/geo.inc.c", "const GeoLayout parent_geo[] = { GEO_ANIMATED_PART(0, 0, 0, 0, parent_dl), GEO_BRANCH_AND_LINK(parent_shared_geo), GEO_SHADOW(1, 2, 3), GEO_END(), };\n")
        write(root / "actors/parent/model.inc.c", """
            ALIGNED8 static const Texture parent_texture[] = {
            #include "actors/parent/parent.rgba16.inc.c"
            };
            const Gfx parent_dl[] = {
                gsDPSetTextureImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, parent_texture),
                gsSPDisplayList(parent_child_dl), gsSPEndDisplayList(),
            };
        """)
        (root / "actors/parent/parent.rgba16.png").write_bytes(b"fixture-png")
        write(root / "actors/shared/geo.inc.c", "const GeoLayout parent_shared_geo[] = { GEO_NODE_START(), GEO_RETURN(), };\n")
        write(root / "actors/shared/model.inc.c", "const Gfx parent_child_dl[] = { gsSPSetLights1(parent_light), gsSPVertex(parent_vtx, 3, 0), gsSP1Triangle(0, 1, 2, 0), gsSPBranchList(parent_tail_dl), };\n")
        write(root / "actors/shared/tail.inc.c", "const Gfx parent_tail_dl[] = { gsSPEndDisplayList(), };\n")
        write(root / "actors/shared/data.inc.c", "const Lights1 parent_light = gdSPDefLights1(0, 0, 0, 0, 0, 0, 0, 0, 0);\nconst Vtx parent_vtx[] = { {{{0, 0, 0}, 0, {0, 0}, {0, 0, 0, 0}}}, };\n")
        write(root / "actors/parent/anims/table.inc.c", "const struct Animation *const parent_anims[] = { 0 };\n")
        write(root / "actors/child/geo.inc.c", "const GeoLayout child_geo[] = { GEO_BILLBOARD(), GEO_DISPLAY_LIST(LAYER_ALPHA, child_dl), GEO_END(), };\n")
        write(root / "actors/child/model.inc.c", "const Gfx child_dl[] = { gsSPEndDisplayList(), };\n")
        write(root / "actors/fixture_roots/geo.inc.c", "const GeoLayout yellow_coin_geo[] = { GEO_END(), };\nconst GeoLayout water_bomb_geo[] = { GEO_END(), };\nconst GeoLayout smoke_geo[] = { GEO_END(), };\n")
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
        self.assertEqual(records["bhvParent"]["root_provenance"]["models"]["MODEL_PARENT"]["geo_source"], "actors/parent/geo.inc.c")
        self.assertEqual(records["bhvParent"]["children"], ["bhvChild"])

    def test_collects_reached_actor_asset_sources_and_hashes(self) -> None:
        root = self.fixture()
        closure = self.collect(root)
        record = next(record for record in closure["records"]
                      if record["stable_id"] == "bhvParent")
        sources = {source["path"]: source["sha256"]
                   for source in record["sources"]}

        self.assertEqual(
            record["root_provenance"]["models"]["MODEL_PARENT"]["geo_source"],
            "actors/parent/geo.inc.c",
        )
        reached = {
            "actors/parent/geo.inc.c",
            "actors/parent/model.inc.c",
            "actors/parent/parent.rgba16.png",
            "actors/shared/geo.inc.c",
            "actors/shared/model.inc.c",
            "actors/shared/tail.inc.c",
            "actors/shared/data.inc.c",
        }
        self.assertTrue(reached <= set(sources))
        for path in reached:
            digest = hashlib.sha256((root / path).read_bytes()).hexdigest()
            self.assertEqual(sources[path], digest)
            self.assertEqual(closure["source_hashes"][path], digest)

    def test_reached_texture_source_missing_ambiguous_computed_and_unsupported_fail(self) -> None:
        cases = (
            ("missing declaration", "missing_texture", (),
             "missing reached Texture missing_texture"),
            ("computed symbol", "select_texture(1)", (),
             "unsupported reached texture image expression"),
            ("ambiguous declaration", "shared_texture", (
                ("actors/texture_a/model.inc.c",
                 'ALIGNED8 static const Texture shared_texture[] = {\n#include "actors/parent/parent.rgba16.inc.c"\n};\n'),
                ("actors/texture_b/model.inc.c",
                 'ALIGNED8 static const Texture shared_texture[] = {\n#include "actors/parent/parent.rgba16.inc.c"\n};\n'),
            ), "ambiguous reached Texture shared_texture"),
            ("unsupported include", "bad_texture", (
                ("actors/texture_a/model.inc.c",
                 'ALIGNED8 static const Texture bad_texture[] = {\n#include "actors/parent/parent.ci8.inc.c"\n};\n'),
            ), "unsupported reached texture source path"),
            ("noncanonical include", "backslash_texture", (
                ("actors/texture_a/model.inc.c",
                 'ALIGNED8 static const Texture backslash_texture[] = {\n#include "actors\\parent\\parent.rgba16.inc.c"\n};\n'),
            ), "unsupported reached texture source path"),
            ("missing PNG", "missing_png_texture", (
                ("actors/texture_a/model.inc.c",
                 'ALIGNED8 static const Texture missing_png_texture[] = {\n#include "actors/parent/missing.rgba16.inc.c"\n};\n'),
            ), "missing reached texture PNG"),
        )
        for label, expression, files, reason in cases:
            with self.subTest(label=label):
                root = self.fixture()
                for relative, source in files:
                    write(root / relative, source)
                model = root / "actors/parent/model.inc.c"
                source = model.read_text(encoding="utf-8")
                source = source.replace("parent_texture),", f"{expression}),")
                write(model, source)
                with self.assertRaisesRegex(ClosureError, reason):
                    self.collect(root)

    def test_reached_load_texture_block_attests_its_png(self) -> None:
        root = self.fixture()
        model = root / "actors/parent/model.inc.c"
        source = model.read_text(encoding="utf-8")
        source = source.replace(
            "gsDPSetTextureImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, parent_texture)",
            "gsDPLoadTextureBlock(parent_texture, G_IM_FMT_RGBA, G_IM_SIZ_16b, "
            "32, 32, 0, G_TX_WRAP, G_TX_WRAP, 5, 5, 0, 0)",
        )
        write(model, source)
        closure = self.collect(root)
        record = next(record for record in closure["records"]
                      if record["stable_id"] == "bhvParent")
        self.assertIn("actors/parent/parent.rgba16.png",
                      {item["path"] for item in record["sources"]})

    def test_missing_reached_actor_source_fails_closed(self) -> None:
        root = self.fixture()
        write(root / "actors/parent/model.inc.c",
              "const Gfx parent_dl[] = { gsSPDisplayList(missing_dl), gsSPEndDisplayList(), };\n")
        with self.assertRaisesRegex(ClosureError, "missing reached Gfx missing_dl"):
            self.collect(root)

    def test_ambiguous_reached_actor_source_fails_closed(self) -> None:
        root = self.fixture()
        write(root / "actors/duplicate/model.inc.c",
              "const Gfx parent_child_dl[] = { gsSPEndDisplayList(), };\n")
        with self.assertRaisesRegex(ClosureError,
                                    "ambiguous reached Gfx parent_child_dl"):
            self.collect(root)

    def test_computed_reached_actor_source_fails_closed(self) -> None:
        root = self.fixture()
        write(root / "actors/parent/model.inc.c",
              "const Gfx parent_dl[] = { gsSPDisplayList(select_parent_dl(1)), gsSPEndDisplayList(), };\n")
        with self.assertRaisesRegex(ClosureError,
                                    "unsupported reached Gfx expression"):
            self.collect(root)

    def test_branch_less_zraw_reached_gfx_source_is_sealed(self) -> None:
        root = self.fixture()
        write(root / "actors/parent/model.inc.c", """
            const Gfx parent_dl[] = {
                gsSPBranchLessZraw(parent_branch_z_dl, 0, 0),
                gsSPEndDisplayList(),
            };
        """)
        write(root / "actors/branch_z/model.inc.c",
              "const Gfx parent_branch_z_dl[] = { gsSPEndDisplayList(), };\n")

        closure = self.collect(root)
        record = next(record for record in closure["records"]
                      if record["stable_id"] == "bhvParent")
        sources = {source["path"] for source in record["sources"]}
        self.assertIn("actors/branch_z/model.inc.c", sources)

    def test_branch_less_z_and_zrg_reached_gfx_sources_are_sealed(self) -> None:
        root = self.fixture()
        write(root / "actors/parent/model.inc.c", """
            const Gfx parent_dl[] = {
                gsSPBranchLessZ(parent_branch_z_dl, 0, 1, 2, 3, G_BZ_PERSP),
                gsSPBranchLessZrg(parent_branch_zrg_dl, 0, 1, 2, 3,
                                  G_BZ_ORTHO, 0, 1023),
                gsSPEndDisplayList(),
            };
        """)
        write(root / "actors/branch_z/model.inc.c",
              "const Gfx parent_branch_z_dl[] = { gsSPEndDisplayList(), };\n")
        write(root / "actors/branch_zrg/model.inc.c",
              "const Gfx parent_branch_zrg_dl[] = { gsSPEndDisplayList(), };\n")

        closure = self.collect(root)
        record = next(record for record in closure["records"]
                      if record["stable_id"] == "bhvParent")
        sources = {source["path"] for source in record["sources"]}
        self.assertIn("actors/branch_z/model.inc.c", sources)
        self.assertIn("actors/branch_zrg/model.inc.c", sources)

    def test_branch_less_z_missing_computed_and_arity_fail_closed(self) -> None:
        cases = (
            ("missing",
             "gsSPBranchLessZ(missing_dl, 0, 1, 2, 3, G_BZ_PERSP)",
             "missing reached Gfx missing_dl"),
            ("computed",
             "gsSPBranchLessZ(select_parent_dl(1), 0, 1, 2, 3, G_BZ_PERSP)",
             "unsupported reached Gfx expression"),
            ("arity",
             "gsSPBranchLessZ(missing_dl, 0, 1)",
             "unsupported reached Gfx expression"),
        )
        for label, command, message in cases:
            with self.subTest(label=label):
                root = self.fixture()
                write(root / "actors/parent/model.inc.c",
                      f"const Gfx parent_dl[] = {{ {command}, gsSPEndDisplayList(), }};\n")
                with self.assertRaisesRegex(ClosureError, message):
                    self.collect(root)

    def test_scalar_and_state_identifiers_are_not_source_references(self) -> None:
        root = self.fixture()
        write(root / "actors/parent/model.inc.c", """
            const Gfx parent_dl[] = {
                gsSPSetGeometryMode(G_CULL_BACK | G_LIGHTING),
                gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),
                gsSPEndDisplayList(),
            };
        """)

        closure = self.collect(root)
        record = next(record for record in closure["records"]
                      if record["stable_id"] == "bhvParent")
        sources = {source["path"] for source in record["sources"]}
        self.assertIn("actors/parent/model.inc.c", sources)

    def test_known_unmodeled_source_reference_command_fails_closed(self) -> None:
        root = self.fixture()
        write(root / "actors/parent/model.inc.c", """
            const Gfx parent_dl[] = {
                gsSPMatrix(missing_mtx, G_MTX_MODELVIEW | G_MTX_LOAD),
                gsSPEndDisplayList(),
            };
        """)

        with self.assertRaisesRegex(
                ClosureError,
                "unsupported reference-bearing Gfx command gsSPMatrix"):
            self.collect(root)

    def test_entirely_unmodeled_gfx_commands_fail_closed(self) -> None:
        cases = (
            ("indexed", "gsSPUnhandledReference(parent_child_dl)"),
            ("missing", "gsSPUnhandledReference(missing_dl)"),
            ("computed", "gsSPUnhandledReference(select_parent_dl(1))"),
        )
        for label, command in cases:
            with self.subTest(label=label):
                root = self.fixture()
                write(root / "actors/parent/model.inc.c",
                      f"const Gfx parent_dl[] = {{ {command}, gsSPEndDisplayList(), }};\n")
                with self.assertRaisesRegex(
                        ClosureError,
                        "unknown reached Gfx command gsSPUnhandledReference"):
                    self.collect(root)

    def test_actor_definition_index_refreshes_after_same_process_change(self) -> None:
        root = self.fixture()
        self.collect(root)
        write(root / "actors/duplicate/model.inc.c",
              "const Gfx parent_child_dl[] = { gsSPEndDisplayList(), };\n")

        with self.assertRaisesRegex(ClosureError,
                                    "ambiguous reached Gfx parent_child_dl"):
            self.collect(root)

    def test_asset_root_lookup_refreshes_after_same_process_change(self) -> None:
        root = self.fixture()
        self.collect(root)
        write(root / "actors/duplicate/geo.inc.c",
              "const GeoLayout parent_geo[] = { GEO_END(), };\n")

        with self.assertRaisesRegex(ClosureError,
                                    "ambiguous asset root parent_geo"):
            self.collect(root)

    def test_deep_acyclic_display_list_chain_fails_bounded(self) -> None:
        root = self.fixture()
        write(root / "actors/parent/geo.inc.c",
              "const GeoLayout parent_geo[] = { GEO_DISPLAY_LIST(LAYER_OPAQUE, parent_dl), GEO_END(), };\n")
        lists = ["const Gfx parent_dl[] = { gsSPDisplayList(chain_0000), };\n"]
        for index in range(1200):
            command = (f"gsSPDisplayList(chain_{index + 1:04d})"
                       if index + 1 < 1200 else "gsSPEndDisplayList()")
            lists.append(
                f"const Gfx chain_{index:04d}[] = {{ {command}, }};\n")
        write(root / "actors/parent/model.inc.c", "".join(lists))

        with self.assertRaisesRegex(
                ClosureError,
                "reached actor asset traversal depth limit exceeded"):
            self.collect(root)

    def test_reached_actor_source_hash_drift_fails_closed(self) -> None:
        root = self.fixture()
        closure = self.collect(root)
        path = root / "actors/shared/data.inc.c"
        path.write_text(path.read_text(encoding="utf-8") + "\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError,
                                    "stale source hash: actors/shared/data.inc.c"):
            validate_scene_closure(closure)

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

    def test_reachable_ambiguous_data_and_function_pointer_symbols_fail_closed(self) -> None:
        fixtures = [
            (
                "ambiguous action table",
                "void bhv_parent_loop(void) { ambiguous_actions[o->oAction](); }\n",
                "void (*ambiguous_actions[])(void) = { action_handler };\n",
                "void (*ambiguous_actions[])(void) = { action_handler };\n",
                "ambiguous cross-file native symbol ambiguous_actions",
            ),
            (
                "ambiguous function pointer",
                "void bhv_parent_loop(void) { parent_actions[o->oAction](); }\n"
                "void (*parent_actions[])(void) = { action_handler };\n",
                "void action_handler(void) {}\n",
                "void action_handler(void) {}\n",
                "ambiguous cross-file native symbol action_handler",
            ),
        ]
        for name, owner, first, second, message in fixtures:
            with self.subTest(name=name):
                root = self.native_rule_fixture()
                write(root / "src/game/behaviors/parent.inc.c", owner)
                write(root / "src/game/action_first.c", first)
                write(root / "src/game/action_second.c", second)
                write(root / "rules.json", json.dumps({"schema": "sm64-saturn-behavior-spawn-rules-v2", "rules": []}))
                with self.assertRaisesRegex(ClosureError, message):
                    self.collect(root)

    def test_repository_wide_callback_definition_is_used_and_missing_definition_fails(self) -> None:
        root = self.native_rule_fixture()
        write(root / "src/game/behaviors/parent.inc.c", "/* callback is defined by another repository source */\n")
        write(root / "src/engine/runtime/parent.c", "void bhv_parent_loop(void) { spawn_object(o, MODEL_CHILD, bhvChild); }\n")
        payload = json.loads((root / "rules.json").read_text(encoding="utf-8"))
        payload["rules"][0]["source"] = "src/engine/runtime/parent.c"
        write(root / "rules.json", json.dumps(payload))
        closure = self.collect(root)
        parent = next(record for record in closure["records"] if record["stable_id"] == "bhvParent")
        self.assertIn("src/engine/runtime/parent.c", {source["path"] for source in parent["sources"]})

        behavior = (root / "data/behavior_data.c").read_text(encoding="utf-8")
        write(root / "data/behavior_data.c", behavior.replace("CALL_NATIVE(bhv_parent_loop)", "CALL_NATIVE(bhv_missing_callback)"))
        write(root / "rules.json", json.dumps({"schema": "sm64-saturn-behavior-spawn-rules-v2", "rules": []}))
        _file_source_regions.cache_clear()
        _native_symbol_index.cache_clear()
        _reachable_native_regions.cache_clear()
        _native_discovery.cache_clear()
        with self.assertRaisesRegex(ClosureError, "missing native definition bhv_missing_callback"):
            self.collect(root)

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

    def test_audio_uses_call_site_argument_not_generic_helper_comparison_constants(self) -> None:
        root = self.native_rule_fixture()
        write(root / "src/game/behaviors/parent.inc.c", """
            static void generic_sound_helper(u32 sound) {
                if (sound == SOUND_GENERAL_MUST_NOT_LEAK) {
                    sound += 0;
                }
                play_sound(sound, o->header.gfx.cameraToObject);
            }
            void bhv_parent_loop(void) {
                u32 selected = SOUND_OBJ_SECOND_USED_BY_PARENT;
                generic_sound_helper(SOUND_OBJ_USED_BY_PARENT);
                generic_sound_helper(selected);
                spawn_object(o, MODEL_CHILD, bhvChild);
            }
        """)
        write(root / "include/sounds.h", """
            #define SOUND_OBJ_USED_BY_PARENT SOUND_ARG_LOAD(SOUND_BANK_OBJ, 1, 2, 3)
            #define SOUND_OBJ_SECOND_USED_BY_PARENT SOUND_ARG_LOAD(SOUND_BANK_OBJ, 7, 8, 9)
            #define SOUND_GENERAL_MUST_NOT_LEAK SOUND_ARG_LOAD(SOUND_BANK_GENERAL, 4, 5, 6)
        """)
        closure = self.collect(root)
        parent = next(record for record in closure["records"] if record["stable_id"] == "bhvParent")
        self.assertEqual(parent["sfx_ids"], ["SOUND_OBJ_SECOND_USED_BY_PARENT", "SOUND_OBJ_USED_BY_PARENT"])
        self.assertEqual(parent["sfx_banks"], ["obj"])

    def test_audio_ignores_non_audio_calls_and_preserves_direct_sinks_and_forwarders(self) -> None:
        root = self.native_rule_fixture()
        write(root / "src/game/behaviors/parent.inc.c", """
            static void consume_configuration(u32 value) {
                value += 1;
            }
            static void forward_sound(u32 sound) {
                u32 selected = sound;
                cur_obj_play_sound_2(selected);
            }
            void bhv_parent_loop(void) {
                consume_configuration(SOUND_GENERAL_MUST_NOT_LEAK);
                forward_sound(SOUND_OBJ_FORWARDED_BY_PARENT);
                cur_obj_play_sound_at_anim_range(2, 17, SOUND_OBJ_DIRECT_BY_PARENT);
                spawn_object(o, MODEL_CHILD, bhvChild);
            }
        """)
        write(root / "include/sounds.h", """
            #define SOUND_OBJ_FORWARDED_BY_PARENT SOUND_ARG_LOAD(SOUND_BANK_OBJ, 1, 2, 3)
            #define SOUND_OBJ_DIRECT_BY_PARENT SOUND_ARG_LOAD(SOUND_BANK_OBJ, 4, 5, 6)
            #define SOUND_GENERAL_MUST_NOT_LEAK SOUND_ARG_LOAD(SOUND_BANK_GENERAL, 7, 8, 9)
        """)
        closure = self.collect(root)
        parent = next(record for record in closure["records"] if record["stable_id"] == "bhvParent")
        self.assertEqual(parent["sfx_ids"], ["SOUND_OBJ_DIRECT_BY_PARENT", "SOUND_OBJ_FORWARDED_BY_PARENT"])
        self.assertEqual(parent["sfx_banks"], ["obj"])

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
        def swap_model_geo_claim(closure: dict) -> None:
            record = next(record for record in closure["records"] if record["stable_id"] == "bhvParent")
            record["model_variants"][0]["geo_root"] = "child_geo"
            record["geo_root"] = "child_geo"
            model = record["root_provenance"]["models"]["MODEL_PARENT"]
            model["geo_symbol"] = "child_geo"
            model["geo_source"] = "actors/child/geo.inc.c"
            child = next(record for record in closure["records"] if record["stable_id"] == "bhvChild")
            record["sources"].append(next(source for source in child["sources"] if source["path"] == "actors/child/geo.inc.c"))

        mutations = [
            (lambda closure: closure["records"][0]["spawned_children"].append("bhvMissing"), "unknown child reference"),
            (lambda closure: next(record for record in closure["records"] if record["stable_id"] == "bhvParent")["effects"].append("bhvReward"), "typed child lists disagree"),
            (lambda closure: next(record for record in closure["records"] if record["stable_id"] == "bhvParent")["children"].clear(), "typed child lists incomplete"),
            (lambda closure: closure["records"][0].update(level="other"), "record level/area mismatch"),
            (lambda closure: closure.update(sfx_ids=["SOUND_MISSING"]), "audio union mismatch"),
            (lambda closure: next(record for record in closure["records"] if record["stable_id"] == "bhvParent")["root_provenance"]["behavior"].update(symbol="bhvMissing"), "invalid root provenance"),
            (lambda closure: next(record for record in closure["records"] if record["stable_id"] == "bhvParent")["root_provenance"]["models"]["MODEL_PARENT"].update(geo_symbol="child_geo"), "invalid root provenance"),
            (swap_model_geo_claim, "invalid root provenance"),
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

    def test_behavior_script_capacity_is_computed_for_each_spawn_site(self) -> None:
        root = self.fixture()
        behavior_path = root / "data/behavior_data.c"
        text = behavior_path.read_text(encoding="utf-8")
        text = text.replace(
            "const BehaviorScript bhvController[] = {};",
            """const BehaviorScript bhvController[] = {
                SPAWN_CHILD(MODEL_ONE_SHOT, bhvOneShot),
                BEGIN_LOOP(),
                    SPAWN_CHILD(MODEL_RECURRENT, bhvRecurrent),
                END_LOOP(),
            };
            const BehaviorScript bhvOneShot[] = {};
            const BehaviorScript bhvRecurrent[] = {};""",
        )
        write(behavior_path, text)
        with (root / "include/model_ids.h").open("a", encoding="utf-8") as stream:
            stream.write("#define MODEL_ONE_SHOT 6 // one_shot_geo\n#define MODEL_RECURRENT 7 // recurrent_geo\n")
        write(root / "actors/site_bounds/geo.inc.c", "const GeoLayout one_shot_geo[] = { GEO_END(), };\nconst GeoLayout recurrent_geo[] = { GEO_END(), };\n")
        records = {record["stable_id"]: record for record in self.collect(root)["records"]}
        self.assertEqual(records["bhvOneShot"]["maximum_live_instances"], 3)
        self.assertEqual(records["bhvRecurrent"]["maximum_live_instances"], 240)

    def test_entry_jump_is_expanded_before_area_and_linked_comments_do_not_donate_music(self) -> None:
        root = self.fixture()
        write(root / "levels/test/script.c", """
            const LevelScript level_test_entry[] = {
                JUMP_LINK(level_test_areas),
                EXIT(),
            };
            const LevelScript level_test_areas[] = {
                LOAD_MODEL_FROM_GEO(MODEL_PARENT, parent_geo),
                LOAD_MODEL_FROM_GEO(MODEL_CHILD, child_geo),
                AREA(1, test_area_geo),
                    OBJECT(MODEL_PARENT, 0, 0, 0, 0, 0, 0, 0, bhvParent),
                    JUMP_LINK(level_test_music),
                END_AREA(),
                AREA(2, test_area_2_geo),
                    SET_BACKGROUND_MUSIC(0, SEQ_LEVEL_MUST_NOT_LEAK),
                END_AREA(),
                RETURN(),
            };
            const LevelScript level_test_music[] = {
                // SET_BACKGROUND_MUSIC(0, SEQ_COMMENT_MUST_NOT_LEAK),
                SET_BACKGROUND_MUSIC(0, SEQ_LEVEL_TEST),
                RETURN(),
            };
        """)
        closure = self.collect(root)
        self.assertEqual(closure["music_sequence_ids"], ["SEQ_LEVEL_TEST"])


if __name__ == "__main__":
    unittest.main()
