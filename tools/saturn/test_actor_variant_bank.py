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
    ActorSourceDriftError,
    ActorSourceSelectionError,
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

    def compile(self, *, family: int = 7, model: int = 11):
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
    def test_rigid_nested_list_has_exact_geometry_and_neutral_pose(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = _Fixture(Path(directory), _RIGID_GEO)
            compiled = fixture.compile()

        self.assertEqual((compiled.family_ordinal, compiled.model_id), (7, 11))
        self.assertEqual(len(compiled.payload), 358)
        self.assertEqual(compiled.lane_bytes, 104)
        self.assertEqual(compiled.maximum_scratch, 211)
        self.assertEqual(
            compiled.payload_sha256,
            "3aa76aa8d63d012e5117ed28748bf21f144e31010ac8aea03038f3fedb57a043",
        )
        self.assertEqual(
            compiled.source_sha256,
            "129e2d833160e29caba88e00552512b739484bc8ee14b1861475b3e0da5033f1",
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
            compiled = fixture.compile(family=9, model=13)

        self.assertEqual(len(compiled.payload), 468)
        self.assertEqual(compiled.lane_bytes, 192)
        self.assertEqual(compiled.maximum_scratch, 387)
        self.assertEqual(
            compiled.payload_sha256,
            "1e688dc471c5590c672632377c31d8c3f4906bb31f7c3d476e8cf310069c395e",
        )
        self.assertEqual(
            compiled.source_sha256,
            "9efc768508379b3d07c36d76b54171f9382eee42c6c6e423cb8af4c4319f5ef2",
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
