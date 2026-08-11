#!/usr/bin/env python3
"""Exact synthetic contracts for source-selected generic S64B banks."""

from __future__ import annotations

import hashlib
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from actor_variant_bank import (  # noqa: E402
    ActorAnimationBindingError,
    ActorJointOwnershipError,
    MalformedActorSourceError,
    ActorSourceDriftError,
    ActorSourceSelectionError,
    UnsupportedActorSourceError,
    compile_actor_variant,
)
from compile_actor_bank import decode_animation_channels  # noqa: E402


_MODEL = """
Lights1 test_light = gdSPDefLights1(
    0x20, 0x10, 0x08,
    0xf8, 0x80, 0x40, 0x28, 0x28, 0x28
);

const Vtx test_vertices[] = {
    {{{  0,  0, 0}, 0, {0, 0}, {0, 0, 127, 255}}},
    {{{ 10,  0, 0}, 0, {0, 0}, {0, 0, 127, 255}}},
    {{{ 10, 10, 0}, 0, {0, 0}, {0, 0, 127, 255}}},
    {{{  0, 10, 0}, 0, {0, 0}, {0, 0, 127, 255}}},
};

const Vtx test_child_vertices[] = {
    {{{ 0, 0, 0}, 0, {0, 0}, {0, 0, 127, 255}}},
    {{{ 4, 0, 0}, 0, {0, 0}, {0, 0, 127, 255}}},
    {{{ 0, 4, 0}, 0, {0, 0}, {0, 0, 127, 255}}},
};

const Gfx test_material_dl[] = {
    gsSPLight(&test_light.l, 1),
    gsSPEndDisplayList(),
};

const Gfx test_child_dl[] = {
    gsSPVertex(test_vertices, 4, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsSPEndDisplayList(),
};

const Gfx test_root_dl[] = {
    gsSPDisplayList(test_material_dl),
    gsSPDisplayList(test_child_dl),
    gsSPEndDisplayList(),
};

const Gfx test_case_a_dl[] = {
    gsSPDisplayList(test_material_dl),
    gsSPVertex(test_child_vertices, 3, 0),
    gsSP1Triangle(0, 1, 2, 0),
    gsSPEndDisplayList(),
};

const Gfx test_case_b_dl[] = {
    gsSPDisplayList(test_material_dl),
    gsSPVertex(test_child_vertices, 3, 0),
    gsSP1Triangle(0, 2, 1, 0),
    gsSPEndDisplayList(),
};
"""

_ANIMATION_TABLE = """
const struct Animation *const test_anims[] = {
    &test_anim,
    NULL,
};
"""

_ANIMATION = """
static const s16 test_anim_values[] = {
    10, 11, 20, 21, 30, 31,
    40, 41, 50, 51, 60, 61,
    70, 71, 80, 81, 90, 91,
};
static const u16 test_anim_indices[] = {
    2, 0, 2, 2, 2, 4,
    2, 6, 2, 8, 2, 10,
    2, 12, 2, 14, 2, 16,
};
static const struct Animation test_anim[] = {
    1,
    1,
    0,
    0,
    2,
    ANIMINDEX_NUMPARTS(test_anim_indices),
    test_anim_values,
    test_anim_indices,
    0,
};
"""


def _sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class _Fixture:
    def __init__(self, root: Path, geo: str, *, animated: bool = False) -> None:
        self.root = root
        files = {
            "include/model_ids.h": "#define MODEL_TEST 1 // test_geo\n",
            "levels/test/script.c": "LOAD_MODEL_FROM_GEO(MODEL_TEST, test_geo)\n",
            "data/behavior_data.c": (
                "LOAD_ANIMATIONS(oAnimations, test_anims)\n" if animated else "/* rigid */\n"
            ),
            "actors/test/geo.inc.c": geo,
            "actors/test/model.inc.c": _MODEL,
        }
        if animated:
            files.update({
                "actors/test/anims/table.inc.c": _ANIMATION_TABLE,
                "actors/test/anims/anim.inc.c": _ANIMATION,
            })
        for relative, text in files.items():
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8", newline="\n")
        self.record: dict[str, object] = {
            "stable_id": "bhvTest",
            "model": "MODEL_TEST",
            "geo_root": "test_geo",
            "model_variants": [{"model": "MODEL_TEST", "geo_root": "test_geo"}],
            "animation_table": ["test_anims"] if animated else [],
            "material_feature_bits": [],
            "maximum_live_instances": 3,
            "sources": [],
            "root_provenance": {
                "behavior": {"symbol": "bhvTest", "source": "data/behavior_data.c"},
                "models": {
                    "MODEL_TEST": {
                        "source": "include/model_ids.h",
                        "binding_source": "levels/test/script.c",
                        "geo_symbol": "test_geo",
                        "geo_source": "actors/test/geo.inc.c",
                    },
                },
                "animation": (
                    {"test_anims": "actors/test/anims/table.inc.c"} if animated else {}
                ),
            },
        }
        self.rehash()

    def rehash(self) -> None:
        self.record["sources"] = [
            {"path": path.relative_to(self.root).as_posix(), "sha256": _sha(path)}
            for path in sorted(self.root.rglob("*.c"))
        ] + [{"path": "include/model_ids.h", "sha256": _sha(self.root / "include/model_ids.h")}]
        self.record["sources"] = sorted(
            self.record["sources"], key=lambda item: str(item["path"])
        )

    def compile(self, *, family: int = 7, model: int = 1):
        return compile_actor_variant(self.root, family, model, [self.record])


_RIGID_GEO = """
const GeoLayout test_geo[] = {
    GEO_NODE_START(),
    GEO_OPEN_NODE(),
        GEO_DISPLAY_LIST(LAYER_OPAQUE, test_root_dl),
    GEO_CLOSE_NODE(),
    GEO_END(),
};
"""

_ARTICULATED_GEO = """
const GeoLayout test_geo[] = {
    GEO_NODE_START(),
    GEO_OPEN_NODE(),
        GEO_ANIMATED_PART(LAYER_OPAQUE, 10, 0, 0, test_case_a_dl),
        GEO_OPEN_NODE(),
            GEO_ANIMATED_PART(LAYER_OPAQUE, 0, 5, 0, test_case_b_dl),
        GEO_CLOSE_NODE(),
    GEO_CLOSE_NODE(),
    GEO_END(),
};
"""


class ActorVariantBankTest(unittest.TestCase):
    def test_model_id_selects_exact_primary_or_alternate_provenance(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = _Fixture(Path(directory), _RIGID_GEO)
            primary = fixture.compile(model=1)
            self.assertEqual(primary.report["selection"]["model"], "MODEL_TEST")
            self.assertEqual(primary.report["selection"]["geo_root"], "test_geo")

            model_ids = fixture.root / "include/model_ids.h"
            model_ids.write_text(
                model_ids.read_text(encoding="utf-8") +
                "#define MODEL_ALT 2 // test_alt_geo\n",
                encoding="utf-8", newline="\n",
            )
            script = fixture.root / "levels/test/script.c"
            script.write_text(
                script.read_text(encoding="utf-8") +
                "LOAD_MODEL_FROM_GEO(MODEL_ALT, test_alt_geo)\n",
                encoding="utf-8", newline="\n",
            )
            alternate_geo = fixture.root / "actors/test/alt_geo.inc.c"
            alternate_geo.write_text("""
const GeoLayout test_alt_geo[] = {
    GEO_DISPLAY_LIST(LAYER_OPAQUE, test_case_a_dl),
    GEO_END(),
};
""", encoding="utf-8", newline="\n")
            fixture.record["model_variants"].append(
                {"model": "MODEL_ALT", "geo_root": "test_alt_geo"}
            )
            fixture.record["root_provenance"]["models"]["MODEL_ALT"] = {
                "source": "include/model_ids.h",
                "binding_source": "levels/test/script.c",
                "geo_symbol": "test_alt_geo",
                "geo_source": "actors/test/alt_geo.inc.c",
            }
            fixture.rehash()

            alternate = fixture.compile(model=2)
            self.assertEqual(alternate.report["selection"]["model"], "MODEL_ALT")
            self.assertEqual(alternate.report["selection"]["geo_root"], "test_alt_geo")
            self.assertEqual([item.path for item in alternate.sources], [
                "actors/test/alt_geo.inc.c",
                "actors/test/model.inc.c",
                "data/behavior_data.c",
                "include/model_ids.h",
                "levels/test/script.c",
            ])
            self.assertEqual(alternate.report["geometry"]["primitives"], [
                {"material": 0, "indices": [0, 1, 2, 2]},
            ])
            with self.assertRaisesRegex(ActorSourceSelectionError, "model ID"):
                fixture.compile(model=3)

    def test_model_id_alias_ambiguity_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = _Fixture(Path(directory), _RIGID_GEO)
            model_ids = fixture.root / "include/model_ids.h"
            model_ids.write_text(
                model_ids.read_text(encoding="utf-8") +
                "#define MODEL_ALIAS 1 // test_alias_geo\n",
                encoding="utf-8", newline="\n",
            )
            alias_geo = fixture.root / "actors/test/alias_geo.inc.c"
            alias_geo.write_text(_RIGID_GEO.replace("test_geo", "test_alias_geo"),
                                 encoding="utf-8", newline="\n")
            fixture.record["model_variants"].append(
                {"model": "MODEL_ALIAS", "geo_root": "test_alias_geo"}
            )
            fixture.record["root_provenance"]["models"]["MODEL_ALIAS"] = {
                "source": "include/model_ids.h",
                "binding_source": "include/model_ids.h",
                "geo_symbol": "test_alias_geo",
                "geo_source": "actors/test/alias_geo.inc.c",
            }
            fixture.rehash()
            with self.assertRaisesRegex(ActorSourceSelectionError, "ambiguous model ID"):
                fixture.compile(model=1)

    def test_declared_root_must_exist_once_in_its_provenance_source(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = _Fixture(Path(directory), _RIGID_GEO)
            declared = fixture.root / "actors/test/geo.inc.c"
            alternate = fixture.root / "actors/test/other_geo.inc.c"
            alternate.write_text(declared.read_text(encoding="utf-8"),
                                 encoding="utf-8", newline="\n")
            declared.write_text("/* declared root removed */\n",
                                encoding="utf-8", newline="\n")
            fixture.rehash()
            with self.assertRaisesRegex(ActorSourceSelectionError, "declared GeoLayout"):
                fixture.compile(model=1)

        with tempfile.TemporaryDirectory() as directory:
            fixture = _Fixture(Path(directory), _RIGID_GEO)
            declared = fixture.root / "actors/test/geo.inc.c"
            declared.write_text(
                declared.read_text(encoding="utf-8") + _RIGID_GEO,
                encoding="utf-8", newline="\n",
            )
            fixture.rehash()
            with self.assertRaisesRegex(ActorSourceSelectionError, "declared GeoLayout"):
                fixture.compile(model=1)

    def test_unrepresentable_fast3d_material_and_texture_states_fail_closed(self) -> None:
        states = {
            "texture_image": "gsDPSetTextureImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, tex),",
            "texture_block": "gsDPLoadTextureBlock(tex, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),",
            "texture_enable": "gsSPTexture(0xffff, 0xffff, 0, G_TX_RENDERTILE, G_ON),",
            "combine": "gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),",
            "cull_set": "gsSPSetGeometryMode(G_CULL_BACK),",
            "cull_clear": "gsSPClearGeometryMode(G_CULL_BACK),",
            "environment": "gsDPSetEnvColor(1, 2, 3, 4),",
            "alpha_compare": "gsDPSetAlphaCompare(G_AC_THRESHOLD),",
            "load_sync": "gsDPLoadSync(),",
            "load_block": "gsDPLoadBlock(0, 0, 0, 0, 0),",
            "tile": "gsDPSetTile(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),",
            "tile_sync": "gsDPTileSync(),",
            "tile_size": "gsDPSetTileSize(0, 0, 0, 0, 0),",
            "ambient_light": "gsSPLight(&test_light.a, 2),",
        }
        for label, state in states.items():
            with self.subTest(label=label), tempfile.TemporaryDirectory() as directory:
                fixture = _Fixture(Path(directory), _RIGID_GEO)
                model = fixture.root / "actors/test/model.inc.c"
                model.write_text(model.read_text(encoding="utf-8").replace(
                    "const Gfx test_material_dl[] = {",
                    "const Gfx test_material_dl[] = {\n    " + state,
                ), encoding="utf-8", newline="\n")
                fixture.rehash()
                with self.assertRaises(UnsupportedActorSourceError):
                    fixture.compile(model=1)

    def test_selected_geo_list_and_vertex_bodies_require_complete_tokens(self) -> None:
        mutations = {
            "geo_token": ("actors/test/geo.inc.c", "GEO_END(),", "BROKEN_TOKEN,\n    GEO_END(),"),
            "geo_comma": ("actors/test/geo.inc.c", "GEO_NODE_START(),", "GEO_NODE_START()"),
            "geo_unterminated": (
                "actors/test/geo.inc.c",
                "GEO_DISPLAY_LIST(LAYER_OPAQUE, test_root_dl),",
                "GEO_DISPLAY_LIST(LAYER_OPAQUE, test_root_dl,",
            ),
            "list_token": ("actors/test/model.inc.c", "gsSPEndDisplayList(),", "BROKEN_TOKEN,\n    gsSPEndDisplayList(),"),
            "list_comma": ("actors/test/model.inc.c", "gsSPVertex(test_vertices, 4, 0),", "gsSPVertex(test_vertices, 4, 0)"),
            "list_unterminated": (
                "actors/test/model.inc.c",
                "gsSPVertex(test_vertices, 4, 0),",
                "gsSPVertex(test_vertices, 4, 0,",
            ),
            "vertex_row": (
                "actors/test/model.inc.c",
                "const Vtx test_vertices[] = {",
                "const Vtx test_vertices[] = {\n"
                "    {{{ 1 + 2, 0, 0}, 0, {0, 0}, {0, 0, 127, 255}}},",
            ),
        }
        for label, (relative, old, new) in mutations.items():
            with self.subTest(label=label), tempfile.TemporaryDirectory() as directory:
                fixture = _Fixture(Path(directory), _RIGID_GEO)
                path = fixture.root / relative
                path.write_text(path.read_text(encoding="utf-8").replace(old, new, 1),
                                encoding="utf-8", newline="\n")
                fixture.rehash()
                with self.assertRaises(MalformedActorSourceError):
                    fixture.compile(model=1)

    def test_animation_expressions_and_unencoded_fields_fail_closed(self) -> None:
        mutations = {
            "numeric_expression": ("10, 11,", "5 + 5, 11,"),
            "parts_expression": (
                "ANIMINDEX_NUMPARTS(test_anim_indices)",
                "BAD_PARTS(test_anim_indices)",
            ),
            "flags_range": ("    1,\n    1,", "    70000,\n    1,"),
        }
        for label, (old, new) in mutations.items():
            with self.subTest(label=label), tempfile.TemporaryDirectory() as directory:
                fixture = _Fixture(Path(directory), _ARTICULATED_GEO, animated=True)
                animation = fixture.root / "actors/test/anims/anim.inc.c"
                animation.write_text(animation.read_text(encoding="utf-8").replace(old, new),
                                     encoding="utf-8", newline="\n")
                fixture.rehash()
                with self.assertRaises(ActorAnimationBindingError):
                    fixture.compile(model=1)

    def test_public_boundary_translates_scalar_count_and_packing_failures(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = _Fixture(Path(directory), _RIGID_GEO)
            vertices = fixture.root / "actors/test/model.inc.c"
            vertices.write_text(vertices.read_text(encoding="utf-8").replace(
                "{{{ 10,  0, 0}", "{{{ 40000,  0, 0}", 1,
            ), encoding="utf-8", newline="\n")
            fixture.rehash()
            with self.assertRaises(MalformedActorSourceError):
                fixture.compile(model=1)

        with tempfile.TemporaryDirectory() as directory:
            fixture = _Fixture(Path(directory), _RIGID_GEO)
            vertices = fixture.root / "actors/test/model.inc.c"
            vertices.write_text(vertices.read_text(encoding="utf-8").replace(
                "{{{ 10,  0, 0}", "{{{ 08,  0, 0}", 1,
            ), encoding="utf-8", newline="\n")
            fixture.rehash()
            with self.assertRaises(MalformedActorSourceError):
                fixture.compile(model=1)

        with tempfile.TemporaryDirectory() as directory:
            fixture = _Fixture(Path(directory), _ARTICULATED_GEO, animated=True)
            animation = fixture.root / "actors/test/anims/anim.inc.c"
            animation.write_text(animation.read_text(encoding="utf-8").replace(
                "10, 11,", "40000, 11,", 1,
            ), encoding="utf-8", newline="\n")
            fixture.rehash()
            with self.assertRaises(ActorAnimationBindingError):
                fixture.compile(model=1)

        for invalid in (True, "three", 65536):
            with self.subTest(maximum_live_instances=invalid), \
                    tempfile.TemporaryDirectory() as directory:
                fixture = _Fixture(Path(directory), _RIGID_GEO)
                fixture.record["maximum_live_instances"] = invalid
                with self.assertRaises(ActorSourceSelectionError):
                    fixture.compile(model=1)

        with tempfile.TemporaryDirectory() as directory:
            fixture = _Fixture(Path(directory), _RIGID_GEO)
            with self.assertRaises(ActorSourceSelectionError):
                fixture.compile(family=True, model=1)
            with self.assertRaises(ActorSourceSelectionError):
                fixture.compile(model=True)
            with self.assertRaises(ActorSourceSelectionError):
                compile_actor_variant(fixture.root, 7, 1, None)  # type: ignore[arg-type]

    def test_rigid_nested_list_has_exact_geometry_and_neutral_pose(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = _Fixture(Path(directory), _RIGID_GEO)
            compiled = fixture.compile()

        self.assertEqual((compiled.family_ordinal, compiled.model_id), (7, 1))
        self.assertEqual(len(compiled.payload), 358)
        self.assertEqual(compiled.lane_bytes, 104)
        self.assertEqual(compiled.maximum_scratch, 211)
        self.assertEqual(
            compiled.payload_sha256,
            "d356417800cc21a0f982e18647be8ff1ffba27b4c1f4c50d52d4d21976771f30",
        )
        self.assertEqual(
            compiled.source_sha256,
            "780d1b65c6c27a8c7d1c77867f816ec07fd239f7fffcfa0c84a48a338fad68f7",
        )
        self.assertEqual(compiled.maximum_scratch, 3 + 2 * compiled.lane_bytes)
        self.assertEqual(
            [source.path for source in compiled.sources],
            [
                "actors/test/geo.inc.c",
                "actors/test/model.inc.c",
                "data/behavior_data.c",
                "include/model_ids.h",
                "levels/test/script.c",
            ],
        )
        geometry = compiled.report["geometry"]
        self.assertEqual(geometry["joints"], [{
            "joint_ordinal": 0, "parent_ordinal": -1, "translation": [0, 0, 0],
            "node_ordinal": 0, "branch_ordinal": 0xFFFF,
        }])
        self.assertEqual(geometry["vertices"], [
            {"local": [0, 0, 0], "joint_ordinal": 0, "branch_ordinal": 0},
            {"local": [10, 0, 0], "joint_ordinal": 0, "branch_ordinal": 0},
            {"local": [10, 10, 0], "joint_ordinal": 0, "branch_ordinal": 0},
            {"local": [0, 10, 0], "joint_ordinal": 0, "branch_ordinal": 0},
        ])
        self.assertEqual(geometry["materials"], [{
            "material_id": 0, "rgb": [31, 16, 8], "light": "test_light",
            "texture": None, "combine_mode": None, "cull_back": True,
            "env_color": None, "alpha_compare": None, "layer": "LAYER_OPAQUE",
        }])
        self.assertEqual(geometry["primitives"], [{"material": 0, "indices": [0, 1, 2, 3]}])
        self.assertEqual(
            [(item["material"], item["opacity"], item["source_ordinal"])
             for item in geometry["meshlets"]],
            [(0, 0, 0)],
        )
        self.assertEqual(len(compiled.report["animations"]), 1)
        self.assertEqual(compiled.report["animations"][0]["frame_count"], 1)
        self.assertEqual(decode_animation_channels(compiled.report, compiled.payload, 0, 0),
                         [0, 0, 0, 0, 0, 0])

    def test_articulated_fixture_has_joint_local_vertices_and_exact_samples(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = _Fixture(Path(directory), _ARTICULATED_GEO, animated=True)
            compiled = fixture.compile(family=9, model=1)

        self.assertEqual(len(compiled.payload), 468)
        self.assertEqual(compiled.lane_bytes, 192)
        self.assertEqual(compiled.maximum_scratch, 387)
        self.assertEqual(
            compiled.payload_sha256,
            "1bff9db7ae5c3cea3512f748a721706a3709b662cb9c8a88ad3b4c0528b0634e",
        )
        self.assertEqual(
            compiled.source_sha256,
            "0d617e2444ef50ce6d16e41aaac6572e47eacdff534c5aab8efcc08cdb118a60",
        )
        geometry = compiled.report["geometry"]
        self.assertEqual(geometry["joints"], [
            {"joint_ordinal": 0, "parent_ordinal": -1, "translation": [10, 0, 0],
             "node_ordinal": 2, "branch_ordinal": 0xFFFF},
            {"joint_ordinal": 1, "parent_ordinal": 0, "translation": [0, 5, 0],
             "node_ordinal": 4, "branch_ordinal": 0xFFFF},
        ])
        self.assertEqual(geometry["vertices"], [
            {"local": [0, 0, 0], "joint_ordinal": 0, "branch_ordinal": 0},
            {"local": [4, 0, 0], "joint_ordinal": 0, "branch_ordinal": 0},
            {"local": [0, 4, 0], "joint_ordinal": 0, "branch_ordinal": 0},
            {"local": [0, 0, 0], "joint_ordinal": 1, "branch_ordinal": 1},
            {"local": [0, 4, 0], "joint_ordinal": 1, "branch_ordinal": 1},
            {"local": [4, 0, 0], "joint_ordinal": 1, "branch_ordinal": 1},
        ])
        self.assertEqual(geometry["parts"], [
            {"branch_ordinal": 0, "joint_ordinal": 0, "display_list": "test_case_a_dl"},
            {"branch_ordinal": 1, "joint_ordinal": 1, "display_list": "test_case_b_dl"},
        ])
        self.assertEqual(geometry["primitives"], [
            {"material": 0, "indices": [0, 1, 2, 2]},
            {"material": 0, "indices": [3, 4, 5, 5]},
        ])
        self.assertEqual(decode_animation_channels(compiled.report, compiled.payload, 0, 0),
                         [10, 20, 30, 40, 50, 60, 70, 80, 90])
        self.assertEqual(decode_animation_channels(compiled.report, compiled.payload, 0, 1),
                         [11, 21, 31, 41, 51, 61, 71, 81, 91])
        self.assertEqual(compiled.report["selection"]["animation_bindings"], [{
            "table": "test_anims", "source": "actors/test/anims/table.inc.c",
            "symbols": ["test_anim"], "animation_ids": [0],
        }])

    def test_switch_billboard_alpha_and_translucent_keep_typed_selection(self) -> None:
        fixtures = {
            "switch": """
const GeoLayout test_geo[] = {
    GEO_SWITCH_CASE(2, test_switch),
    GEO_OPEN_NODE(),
        GEO_DISPLAY_LIST(LAYER_OPAQUE, test_case_a_dl),
        GEO_DISPLAY_LIST(LAYER_OPAQUE, test_case_b_dl),
    GEO_CLOSE_NODE(),
    GEO_END(),
};
""",
            "billboard": """
const GeoLayout test_geo[] = {
    GEO_BILLBOARD(),
    GEO_OPEN_NODE(),
        GEO_DISPLAY_LIST(LAYER_OPAQUE, test_case_a_dl),
    GEO_CLOSE_NODE(),
    GEO_END(),
};
""",
            "alpha": _RIGID_GEO.replace("LAYER_OPAQUE", "LAYER_ALPHA"),
            "translucent": _RIGID_GEO.replace("LAYER_OPAQUE", "LAYER_TRANSPARENT"),
        }
        results = {}
        for label, geo in fixtures.items():
            with self.subTest(label=label), tempfile.TemporaryDirectory() as directory:
                results[label] = _Fixture(Path(directory), geo).compile()

        switch = results["switch"].report
        self.assertEqual(switch["selection"]["switches"], [{
            "case_count": 2,
            "callback": "test_switch",
            "variant_display_lists": ["test_case_a_dl", "test_case_b_dl"],
        }])
        self.assertEqual(switch["geometry"]["primitives"], [
            {"material": 0, "indices": [0, 1, 2, 2]},
            {"material": 0, "indices": [3, 4, 5, 5]},
        ])
        billboard = results["billboard"].report
        self.assertEqual(billboard["selection"]["billboards"], [{
            "display_lists": ["test_case_a_dl"],
        }])
        self.assertEqual(billboard["geometry"]["vertices"], [
            {"local": [0, 0, 0], "joint_ordinal": 0, "branch_ordinal": 0},
            {"local": [4, 0, 0], "joint_ordinal": 0, "branch_ordinal": 0},
            {"local": [0, 4, 0], "joint_ordinal": 0, "branch_ordinal": 0},
        ])
        for label, layer in (("alpha", "LAYER_ALPHA"),
                             ("translucent", "LAYER_TRANSPARENT")):
            report = results[label].report
            self.assertEqual(report["selection"]["layers"], [{
                "part_ordinal": 0, "layer": layer, "opacity": label,
            }])
            self.assertEqual([item["opacity"] for item in report["geometry"]["meshlets"]], [1])
            self.assertEqual(report["geometry"]["primitives"], [
                {"material": 0, "indices": [0, 1, 2, 3]},
            ])

    def test_source_and_structure_mutations_change_output_or_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = _Fixture(Path(directory), _RIGID_GEO)
            baseline = fixture.compile()
            model = fixture.root / "actors/test/model.inc.c"
            model.write_text(model.read_text(encoding="utf-8") + "/* drift */\n",
                             encoding="utf-8", newline="\n")
            with self.assertRaises(ActorSourceDriftError):
                fixture.compile()

        with tempfile.TemporaryDirectory() as directory:
            fixture = _Fixture(Path(directory), _RIGID_GEO)
            geo = fixture.root / "actors/test/geo.inc.c"
            geo.write_text(geo.read_text(encoding="utf-8").replace(
                "test_root_dl", "missing_list"), encoding="utf-8", newline="\n")
            fixture.rehash()
            with self.assertRaises(ActorSourceSelectionError):
                fixture.compile()

        with tempfile.TemporaryDirectory() as directory:
            fixture = _Fixture(Path(directory), _RIGID_GEO)
            fixture.record["sources"] = [
                item for item in fixture.record["sources"]
                if item["path"] != "actors/test/model.inc.c"
            ]
            with self.assertRaisesRegex(ActorSourceSelectionError, "missing Gfx source"):
                fixture.compile()

        with tempfile.TemporaryDirectory() as directory:
            switch_geo = """
const GeoLayout test_geo[] = {
    GEO_SWITCH_CASE(2, test_switch),
    GEO_OPEN_NODE(),
        GEO_DISPLAY_LIST(LAYER_OPAQUE, test_case_a_dl),
        GEO_DISPLAY_LIST(LAYER_OPAQUE, missing_case_dl),
    GEO_CLOSE_NODE(),
    GEO_END(),
};
"""
            fixture = _Fixture(Path(directory), switch_geo)
            with self.assertRaises(ActorSourceSelectionError):
                fixture.compile()

        with tempfile.TemporaryDirectory() as directory:
            fixture = _Fixture(Path(directory), _ARTICULATED_GEO, animated=True)
            geo = fixture.root / "actors/test/geo.inc.c"
            geo.write_text(geo.read_text(encoding="utf-8").replace(
                "GEO_ANIMATED_PART(LAYER_OPAQUE, 0, 5, 0, test_case_b_dl)",
                "GEO_DISPLAY_LIST(LAYER_OPAQUE, test_case_b_dl)"),
                encoding="utf-8", newline="\n")
            fixture.rehash()
            with self.assertRaises(ActorJointOwnershipError):
                fixture.compile()

        with tempfile.TemporaryDirectory() as directory:
            fixture = _Fixture(Path(directory), _ARTICULATED_GEO, animated=True)
            animation = fixture.root / "actors/test/anims/anim.inc.c"
            animation.write_text(animation.read_text(encoding="utf-8").replace(
                "2, 16,", "2, 255,"), encoding="utf-8", newline="\n")
            fixture.rehash()
            with self.assertRaises(ActorAnimationBindingError):
                fixture.compile()

        with tempfile.TemporaryDirectory() as directory:
            fixture = _Fixture(Path(directory), _RIGID_GEO.replace(
                "LAYER_OPAQUE", "LAYER_ALPHA"))
            alpha = fixture.compile()
            geo = fixture.root / "actors/test/geo.inc.c"
            geo.write_text(geo.read_text(encoding="utf-8").replace(
                "LAYER_ALPHA", "LAYER_OPAQUE"), encoding="utf-8", newline="\n")
            fixture.rehash()
            opaque = fixture.compile()
            self.assertNotEqual(alpha.source_sha256, opaque.source_sha256)
            self.assertNotEqual(alpha.payload_sha256, opaque.payload_sha256)
            self.assertEqual(alpha.report["geometry"]["meshlets"][0]["opacity"], 1)
            self.assertEqual(opaque.report["geometry"]["meshlets"][0]["opacity"], 0)

        self.assertEqual(baseline.report["geometry"]["primitives"], [
            {"material": 0, "indices": [0, 1, 2, 3]},
        ])


if __name__ == "__main__":
    unittest.main()
