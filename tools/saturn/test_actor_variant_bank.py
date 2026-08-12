#!/usr/bin/env python3
"""Exact synthetic contracts for source-selected generic S64B banks."""

from __future__ import annotations

import copy
import hashlib
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from actor_variant_bank import (  # noqa: E402
    _Fast3DCompiler,
    _fast3d_scalar,
    ActorAnimationBindingError,
    ActorJointOwnershipError,
    MalformedActorSourceError,
    ActorSourceDriftError,
    ActorSourceSelectionError,
    UnsupportedActorSourceError,
    compile_actor_variant,
)
from actor_bank_format import validate_actor_bank  # noqa: E402
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
    NULL
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
    0
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


_ROOT_DISPLAY_LIST = """const Gfx test_root_dl[] = {
    gsSPDisplayList(test_material_dl),
    gsSPDisplayList(test_child_dl),
    gsSPEndDisplayList(),
};"""


def _replace_root_display_list(fixture: _Fixture, replacement: str) -> Path:
    model = fixture.root / "actors/test/model.inc.c"
    source = model.read_text(encoding="utf-8")
    if source.count(_ROOT_DISPLAY_LIST) != 1:
        raise AssertionError("fixture root display list changed unexpectedly")
    model.write_text(source.replace(_ROOT_DISPLAY_LIST, replacement, 1),
                     encoding="utf-8", newline="\n")
    fixture.rehash()
    return model


def _add_model_none_variant(fixture: _Fixture, model_id: int = 0,
                            *, newline: str = "\n") -> None:
    model_ids = fixture.root / "include/model_ids.h"
    model_ids.write_text(
        model_ids.read_text(encoding="utf-8") +
        f"#define MODEL_NONE {model_id}\n",
        encoding="utf-8", newline=newline,
    )
    fixture.record["model_variants"].append({
        "model": "MODEL_NONE", "geo_root": "none",
    })
    fixture.record["root_provenance"]["models"]["MODEL_NONE"] = {
        "source": "include/model_ids.h",
        "binding_source": "include/model_ids.h",
        "geo_symbol": "none",
        "geo_source": None,
    }
    fixture.rehash()


class ActorVariantBankTest(unittest.TestCase):
    def test_fast3d_scalar_rejects_shift_bounds_before_evaluation(self) -> None:
        with self.assertRaisesRegex(MalformedActorSourceError, "shift count"):
            _fast3d_scalar("1 << 1000000", "adversarial")
        with self.assertRaisesRegex(MalformedActorSourceError, "shift operand"):
            _fast3d_scalar("0x100000000 << 1", "adversarial")

    def test_material_trace_binds_canonical_command_even_when_final_state_matches(self) -> None:
        """Already-set geometry bits cannot make distinct commands trace-identical."""
        traces = []
        for mode in ("G_LIGHTING", "G_CULL_BACK"):
            compiler = _Fast3DCompiler(None, {}, {}, 4, 0x00CD)
            compiler._material_command("gsSPSetGeometryMode", mode, "fixture")
            compiler._trace_material_state(
                "actors/fixture/model.inc.c", "fixture", "gsSPSetGeometryMode",
                compiler._material_command_snapshot(
                    "gsSPSetGeometryMode", mode, "fixture"),
            )
            traces.append((compiler._state_snapshot(), compiler.material_trace))
        self.assertEqual(traces[0][0], traces[1][0])
        self.assertNotEqual(traces[0][1], traces[1][1])

    def test_crlf_model_none_identity_does_not_poison_selected_drawable(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = _Fixture(Path(directory), _RIGID_GEO)
            _add_model_none_variant(fixture, newline="\r\n")
            compiled = fixture.compile(model=1)

        self.assertEqual((compiled.report["selection"]["model"],
                          compiled.report["selection"]["geo_root"]),
                         ("MODEL_TEST", "test_geo"))

    def test_selected_drawable_ignores_exact_unselected_model_none_geometry(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = _Fixture(Path(directory), _RIGID_GEO)
            _add_model_none_variant(fixture)
            compiled = fixture.compile(model=1)

        self.assertEqual((len(compiled.payload), compiled.lane_bytes,
                          compiled.maximum_scratch), (358, 104, 211))
        self.assertEqual(compiled.payload_sha256,
                         "d2dd087722cda6e1e9e76c56e87bca50d0624facf3a83830a5e07bc682c1dc15")
        self.assertEqual(compiled.source_sha256,
                         "cab8055779d39b1738fe117a605f47d70b950234242b12548dcccc89fcdd5149")
        self.assertEqual(compiled.report["selection"]["model"], "MODEL_TEST")
        self.assertEqual(compiled.report["selection"]["geo_root"], "test_geo")
        self.assertEqual(compiled.report["selection"]["model_variants"], [
            {"model": "MODEL_NONE", "geo_root": "none"},
            {"model": "MODEL_TEST", "geo_root": "test_geo"},
        ])
        self.assertEqual(compiled.report["geometry"]["vertices"], [
            {"local": [0, 0, 0], "joint_ordinal": 0, "branch_ordinal": 0},
            {"local": [10, 0, 0], "joint_ordinal": 0, "branch_ordinal": 0},
            {"local": [10, 10, 0], "joint_ordinal": 0, "branch_ordinal": 0},
            {"local": [0, 10, 0], "joint_ordinal": 0, "branch_ordinal": 0},
        ])
        self.assertEqual(compiled.report["geometry"]["primitives"], [
            {"material": 0, "indices": [0, 1, 2, 3]},
        ])

    def test_selected_model_none_fails_as_non_drawable_without_geometry(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = _Fixture(Path(directory), _RIGID_GEO)
            _add_model_none_variant(fixture, model_id=2)
            with self.assertRaisesRegex(ActorSourceSelectionError, "non-drawable"):
                fixture.compile(model=2)

    def test_unselected_model_none_requires_exact_nondrawable_provenance(self) -> None:
        mutations = {
            "drawable_root": ("variant", "geo_root", "test_geo"),
            "drawable_symbol": ("binding", "geo_symbol", "test_geo"),
            "fabricated_geo_source": (
                "binding", "geo_source", "actors/test/geo.inc.c"),
            "missing_binding_source": ("binding", "binding_source", None),
            "mismatched_binding_source": (
                "binding", "binding_source", "levels/test/script.c"),
        }
        for label, (owner, field, value) in mutations.items():
            with self.subTest(label=label), tempfile.TemporaryDirectory() as directory:
                fixture = _Fixture(Path(directory), _RIGID_GEO)
                _add_model_none_variant(fixture)
                if owner == "variant":
                    fixture.record["model_variants"][1][field] = value
                else:
                    fixture.record["root_provenance"]["models"]["MODEL_NONE"][field] = value
                with self.assertRaises(ActorSourceSelectionError):
                    fixture.compile(model=1)

    def test_drawable_provenance_stays_strict_before_and_after_selection(self) -> None:
        selected_mutations = {
            "missing_geo_source": ("geo_source", None),
            "missing_binding_source": ("binding_source", None),
            "mismatched_geo_symbol": ("geo_symbol", "other_geo"),
        }
        for label, (field, value) in selected_mutations.items():
            with self.subTest(selected=label), tempfile.TemporaryDirectory() as directory:
                fixture = _Fixture(Path(directory), _RIGID_GEO)
                fixture.record["root_provenance"]["models"]["MODEL_TEST"][field] = value
                with self.assertRaises(ActorSourceSelectionError):
                    fixture.compile(model=1)

        with tempfile.TemporaryDirectory() as directory:
            fixture = _Fixture(Path(directory), _RIGID_GEO)
            model_ids = fixture.root / "include/model_ids.h"
            model_ids.write_text(
                model_ids.read_text(encoding="utf-8") +
                "#define MODEL_ALT 2 // alternate_geo\n",
                encoding="utf-8", newline="\n",
            )
            fixture.record["model_variants"].append({
                "model": "MODEL_ALT", "geo_root": "alternate_geo",
            })
            fixture.record["root_provenance"]["models"]["MODEL_ALT"] = {
                "source": "include/model_ids.h",
                "binding_source": "levels/test/script.c",
                "geo_symbol": "alternate_geo",
                "geo_source": None,
            }
            fixture.rehash()
            with self.assertRaisesRegex(ActorSourceSelectionError,
                                        "geo_source is incomplete: MODEL_ALT"):
                fixture.compile(model=1)

    def test_real_shaped_terminal_branch_list_executes_exact_tail_geometry(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = _Fixture(Path(directory), _RIGID_GEO)
            _replace_root_display_list(fixture, """const Gfx test_root_dl[] = {
    gsSPDisplayList(test_material_dl),
    gsDPPipeSync(),
    gsSPBranchList(test_child_dl),
};""")
            compiled = fixture.compile()

        self.assertEqual((len(compiled.payload), compiled.lane_bytes,
                          compiled.maximum_scratch), (358, 104, 211))
        self.assertEqual(compiled.payload_sha256,
                         "216112f7f8b59aeeefe15b86845f3aecfd4caf267f9a63a8d3d01663cbd944e2")
        self.assertEqual(compiled.source_sha256,
                         "5336a502acfb2f85072367ae5d0f5cef58f8958b91c54d58d6f31eebe47aa7bd")
        self.assertEqual(compiled.report["geometry"]["vertices"], [
            {"local": [0, 0, 0], "joint_ordinal": 0, "branch_ordinal": 0},
            {"local": [10, 0, 0], "joint_ordinal": 0, "branch_ordinal": 0},
            {"local": [10, 10, 0], "joint_ordinal": 0, "branch_ordinal": 0},
            {"local": [0, 10, 0], "joint_ordinal": 0, "branch_ordinal": 0},
        ])
        self.assertEqual(compiled.report["geometry"]["materials"], [{
            "material_id": 0, "rgb": [31, 16, 8], "light": "test_light",
            "texture": None, "combine_mode": None, "cull_back": True,
            "env_color": None, "alpha_compare": None, "layer": "LAYER_OPAQUE",
        }])
        self.assertEqual(compiled.report["geometry"]["primitives"], [
            {"material": 0, "indices": [0, 1, 2, 3]},
        ])
        self.assertEqual(compiled.report["animations"][0]["frame_count"], 1)
        self.assertEqual(decode_animation_channels(
            compiled.report, compiled.payload, 0, 0), [0, 0, 0, 0, 0, 0])

    def test_terminal_branch_list_malformed_forms_fail_closed(self) -> None:
        malformed = {
            "suffix_end": """const Gfx test_root_dl[] = {
    gsSPBranchList(test_child_dl),
    gsSPEndDisplayList(),
};""",
            "suffix_state": """const Gfx test_root_dl[] = {
    gsSPBranchList(test_child_dl),
    gsDPPipeSync(),
    gsSPEndDisplayList(),
};""",
            "wrong_arity": """const Gfx test_root_dl[] = {
    gsSPBranchList(test_child_dl, 0),
};""",
            "empty_target": """const Gfx test_root_dl[] = {
    gsSPBranchList(),
};""",
            "computed_target": """const Gfx test_root_dl[] = {
    gsSPBranchList(select_test_child_dl(1)),
};""",
            "ordinary_missing_end": """const Gfx test_root_dl[] = {
    gsSPDisplayList(test_child_dl),
};""",
        }
        for label, source in malformed.items():
            with self.subTest(label=label), tempfile.TemporaryDirectory() as directory:
                fixture = _Fixture(Path(directory), _RIGID_GEO)
                _replace_root_display_list(fixture, source)
                with self.assertRaises(MalformedActorSourceError):
                    fixture.compile()

    def test_terminal_branch_list_target_resolution_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = _Fixture(Path(directory), _RIGID_GEO)
            _replace_root_display_list(fixture, """const Gfx test_root_dl[] = {
    gsSPBranchList(missing_tail_dl),
};""")
            with self.assertRaisesRegex(ActorSourceSelectionError, "missing Gfx source"):
                fixture.compile()

        with tempfile.TemporaryDirectory() as directory:
            fixture = _Fixture(Path(directory), _RIGID_GEO)
            model = _replace_root_display_list(fixture, """const Gfx test_root_dl[] = {
    gsSPBranchList(test_child_dl),
};""")
            model.write_text(model.read_text(encoding="utf-8") + """
const Gfx test_child_dl[] = {
    gsSPEndDisplayList(),
};
""", encoding="utf-8", newline="\n")
            fixture.rehash()
            with self.assertRaisesRegex(ActorSourceSelectionError, "ambiguous Gfx source"):
                fixture.compile()

    def test_terminal_branch_list_cycle_and_depth_fail_named(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = _Fixture(Path(directory), _RIGID_GEO)
            model = _replace_root_display_list(fixture, """const Gfx test_root_dl[] = {
    gsSPBranchList(test_cycle_dl),
};""")
            model.write_text(model.read_text(encoding="utf-8") + """
const Gfx test_cycle_dl[] = {
    gsSPBranchList(test_root_dl),
};
""", encoding="utf-8", newline="\n")
            fixture.rehash()
            with self.assertRaisesRegex(ActorSourceSelectionError,
                                        "recursive display list"):
                fixture.compile()

        with tempfile.TemporaryDirectory() as directory:
            fixture = _Fixture(Path(directory), _RIGID_GEO)
            model = _replace_root_display_list(fixture, """const Gfx test_root_dl[] = {
    gsSPBranchList(test_chain_000),
};""")
            chain = []
            for index in range(300):
                target = (f"test_chain_{index + 1:03d}" if index < 299
                          else "test_child_dl")
                chain.append(
                    f"const Gfx test_chain_{index:03d}[] = {{\n"
                    f"    gsSPBranchList({target}),\n"
                    "};\n"
                )
            model.write_text(model.read_text(encoding="utf-8") + "\n".join(chain),
                             encoding="utf-8", newline="\n")
            fixture.rehash()
            with self.assertRaisesRegex(ActorSourceSelectionError, "depth"):
                fixture.compile()

    def test_real_shaped_adjacent_geo_and_direct_dl_bindings_select_geo(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = _Fixture(Path(directory), _RIGID_GEO)
            script = fixture.root / "levels/test/script.c"
            script.write_text(
                "LOAD_MODEL_FROM_GEO(MODEL_TEST, test_geo)\n"
                "LOAD_MODEL_FROM_DL( MODEL_WHITE_PARTICLE_SMALL, "
                "white_particle_small_dl, LAYER_ALPHA)\n"
                "LOAD_MODEL_FROM_DL(MODEL_UNSELECTED_DECAL, "
                "unselected_decal_dl, LAYER_TRANSPARENT_DECAL)\n",
                encoding="utf-8", newline="\n",
            )
            fixture.rehash()
            compiled = fixture.compile(model=1)

        self.assertEqual(compiled.report["selection"]["model"], "MODEL_TEST")
        self.assertEqual(compiled.report["selection"]["geo_root"], "test_geo")
        self.assertEqual(compiled.report["selection"]["model_binding"], {
            "kind": "geo", "layer": None,
        })
        self.assertEqual(compiled.report["geometry"]["primitives"], [
            {"material": 0, "indices": [0, 1, 2, 3]},
        ])
        self.assertEqual(compiled.report["selection"]["layers"], [])

    def test_selected_direct_dl_layer_drives_exact_typed_and_binary_semantics(self) -> None:
        def compile_layer(directory: str, layer: str):
            fixture = _Fixture(Path(directory), _RIGID_GEO)
            script = fixture.root / "levels/test/script.c"
            script.write_text(
                f"LOAD_MODEL_FROM_DL(MODEL_TEST, test_root_dl, {layer})\n",
                encoding="utf-8", newline="\n",
            )
            fixture.record["geo_root"] = "test_root_dl"
            fixture.record["model_variants"][0]["geo_root"] = "test_root_dl"
            binding = fixture.record["root_provenance"]["models"]["MODEL_TEST"]
            binding["geo_symbol"] = "test_root_dl"
            binding["geo_source"] = "actors/test/model.inc.c"
            fixture.rehash()
            return fixture.compile(model=1)

        with tempfile.TemporaryDirectory() as alpha_directory:
            alpha = compile_layer(alpha_directory, "LAYER_ALPHA")
        with tempfile.TemporaryDirectory() as opaque_directory:
            opaque = compile_layer(opaque_directory, "LAYER_OPAQUE")

        self.assertEqual((len(alpha.payload), alpha.lane_bytes, alpha.maximum_scratch),
                         (358, 104, 211))
        self.assertEqual(alpha.payload_sha256,
                         "89f2f521e98deda81c4af9be1b8867d23e9245421da8554ba720119234a7fb30")
        self.assertEqual(alpha.source_sha256,
                         "eaf9fcbffaa587ff5cb46a45f621e5f5294eddcd14cb640e56f1bb245d0d88d1")
        self.assertEqual([source.path for source in alpha.sources], [
            "actors/test/model.inc.c",
            "data/behavior_data.c",
            "include/model_ids.h",
            "levels/test/script.c",
        ])
        self.assertEqual(alpha.report["selection"]["model_binding"], {
            "kind": "display_list", "layer": "LAYER_ALPHA",
        })
        self.assertEqual(alpha.report["selection"]["layers"], [{
            "part_ordinal": 0, "layer": "LAYER_ALPHA", "opacity": "alpha",
        }])
        self.assertEqual(alpha.report["geometry"]["materials"], [{
            "material_id": 0, "rgb": [31, 16, 8], "light": "test_light",
            "texture": None, "combine_mode": None, "cull_back": True,
            "env_color": None, "alpha_compare": None, "layer": "LAYER_ALPHA",
        }])
        self.assertEqual(alpha.report["geometry"]["primitives"], [
            {"material": 0, "indices": [0, 1, 2, 3]},
        ])
        self.assertEqual(
            [meshlet["opacity"] for meshlet in alpha.report["geometry"]["meshlets"]],
            [1],
        )
        self.assertEqual(opaque.report["selection"]["model_binding"], {
            "kind": "display_list", "layer": "LAYER_OPAQUE",
        })
        self.assertEqual(opaque.payload_sha256,
                         "3472a30bf9c4be9e8f3f1b7c86a18e71fcffa3e5382f0c85d756317740811058")
        self.assertEqual(opaque.source_sha256,
                         "57234f661d1cb7dec5a2c15999bc649f247f89c1a37f33e064033c6b53e45909")
        self.assertEqual(opaque.report["selection"]["layers"], [])
        self.assertEqual(opaque.report["geometry"]["materials"][0]["layer"],
                         "LAYER_OPAQUE")
        self.assertEqual(
            [meshlet["opacity"] for meshlet in opaque.report["geometry"]["meshlets"]],
            [0],
        )
        self.assertNotEqual(alpha.source_sha256, opaque.source_sha256)
        self.assertNotEqual(alpha.payload_sha256, opaque.payload_sha256)

    def test_direct_dl_binding_arity_layer_and_conflicts_fail_closed(self) -> None:
        malformed = {
            "geo_three_args": (
                "LOAD_MODEL_FROM_GEO(MODEL_TEST, test_root_dl, LAYER_ALPHA)\n"
            ),
            "dl_two_args": "LOAD_MODEL_FROM_DL(MODEL_TEST, test_root_dl)\n",
            "dl_four_args": (
                "LOAD_MODEL_FROM_DL(MODEL_TEST, test_root_dl, LAYER_ALPHA, 0)\n"
            ),
            "dl_empty_layer": (
                "LOAD_MODEL_FROM_DL(MODEL_TEST, test_root_dl, )\n"
            ),
            "dl_empty_model": (
                "LOAD_MODEL_FROM_DL(, test_root_dl, LAYER_ALPHA)\n"
            ),
            "dl_empty_root": (
                "LOAD_MODEL_FROM_DL(MODEL_TEST, , LAYER_ALPHA)\n"
            ),
            "geo_empty_model": "LOAD_MODEL_FROM_GEO(, test_root_dl)\n",
            "geo_empty_root": "LOAD_MODEL_FROM_GEO(MODEL_TEST, )\n",
            "dl_duplicate": (
                "LOAD_MODEL_FROM_DL(MODEL_TEST, test_root_dl, LAYER_ALPHA)\n"
                "LOAD_MODEL_FROM_DL(MODEL_TEST, test_root_dl, LAYER_ALPHA)\n"
            ),
            "dl_conflicting_layer": (
                "LOAD_MODEL_FROM_DL(MODEL_TEST, test_root_dl, LAYER_ALPHA)\n"
                "LOAD_MODEL_FROM_DL(MODEL_TEST, test_root_dl, LAYER_OPAQUE)\n"
            ),
            "dl_trailing_token": (
                "LOAD_MODEL_FROM_DL(MODEL_TEST, test_root_dl, LAYER_ALPHA) BROKEN\n"
            ),
            "dl_unterminated": (
                "LOAD_MODEL_FROM_DL(MODEL_TEST, test_root_dl, LAYER_ALPHA\n"
            ),
        }
        for label, source in malformed.items():
            with self.subTest(label=label), tempfile.TemporaryDirectory() as directory:
                fixture = _Fixture(Path(directory), _RIGID_GEO)
                script = fixture.root / "levels/test/script.c"
                script.write_text(source, encoding="utf-8", newline="\n")
                fixture.record["geo_root"] = "test_root_dl"
                fixture.record["model_variants"][0]["geo_root"] = "test_root_dl"
                binding = fixture.record["root_provenance"]["models"]["MODEL_TEST"]
                binding["geo_symbol"] = "test_root_dl"
                binding["geo_source"] = "actors/test/model.inc.c"
                fixture.rehash()
                with self.assertRaises(ActorSourceSelectionError):
                    fixture.compile(model=1)

        with tempfile.TemporaryDirectory() as directory:
            fixture = _Fixture(Path(directory), _RIGID_GEO)
            script = fixture.root / "levels/test/script.c"
            script.write_text(
                "LOAD_MODEL_FROM_DL(MODEL_TEST, test_root_dl, LAYER_FORCE)\n",
                encoding="utf-8", newline="\n",
            )
            fixture.record["geo_root"] = "test_root_dl"
            fixture.record["model_variants"][0]["geo_root"] = "test_root_dl"
            binding = fixture.record["root_provenance"]["models"]["MODEL_TEST"]
            binding["geo_symbol"] = "test_root_dl"
            binding["geo_source"] = "actors/test/model.inc.c"
            fixture.rehash()
            with self.assertRaises(UnsupportedActorSourceError):
                fixture.compile(model=1)

    def test_model_binding_source_must_match_selected_provenance(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = _Fixture(Path(directory), _RIGID_GEO)
            alternate_geo = fixture.root / "actors/test/alternate_geo.inc.c"
            alternate_geo.write_text(
                _RIGID_GEO.replace("test_geo", "alternate_geo"),
                encoding="utf-8", newline="\n",
            )
            fixture.record["geo_root"] = "alternate_geo"
            fixture.record["model_variants"][0]["geo_root"] = "alternate_geo"
            binding = fixture.record["root_provenance"]["models"]["MODEL_TEST"]
            binding["geo_symbol"] = "alternate_geo"
            binding["geo_source"] = "actors/test/alternate_geo.inc.c"
            fixture.rehash()
            with self.assertRaisesRegex(ActorSourceSelectionError, "binding"):
                fixture.compile(model=1)

        mutations = {
            "missing": "/* selected binding removed */\n",
            "duplicate": (
                "LOAD_MODEL_FROM_GEO(MODEL_TEST, test_geo)\n"
                "LOAD_MODEL_FROM_GEO(MODEL_TEST, test_geo)\n"
            ),
            "conflicting": (
                "LOAD_MODEL_FROM_GEO(MODEL_TEST, test_geo)\n"
                "LOAD_MODEL_FROM_GEO(MODEL_TEST, other_geo)\n"
            ),
            "unterminated": "LOAD_MODEL_FROM_GEO(MODEL_TEST, test_geo\n",
            "trailing_token": (
                "LOAD_MODEL_FROM_GEO(MODEL_TEST, test_geo) BROKEN_TOKEN\n"
            ),
        }
        for label, source in mutations.items():
            with self.subTest(label=label), tempfile.TemporaryDirectory() as directory:
                fixture = _Fixture(Path(directory), _RIGID_GEO)
                script = fixture.root / "levels/test/script.c"
                script.write_text(source, encoding="utf-8", newline="\n")
                fixture.rehash()
                with self.assertRaisesRegex(ActorSourceSelectionError, "binding"):
                    fixture.compile(model=1)

        with tempfile.TemporaryDirectory() as directory:
            fixture = _Fixture(Path(directory), _RIGID_GEO)
            binding = fixture.record["root_provenance"]["models"]["MODEL_TEST"]
            binding["binding_source"] = "include/model_ids.h"
            compiled = fixture.compile(model=1)
            self.assertEqual(
                (compiled.report["selection"]["model"],
                 compiled.report["selection"]["geo_root"]),
                ("MODEL_TEST", "test_geo"),
            )

    def test_cross_record_model_id_conflict_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = _Fixture(Path(directory), _RIGID_GEO)
            model_ids = fixture.root / "include/model_ids.h"
            model_ids.write_text(
                model_ids.read_text(encoding="utf-8") +
                "#define MODEL_ALT 1 // alternate_geo\n",
                encoding="utf-8", newline="\n",
            )
            script = fixture.root / "levels/test/script.c"
            script.write_text(
                script.read_text(encoding="utf-8") +
                "LOAD_MODEL_FROM_GEO(MODEL_ALT, alternate_geo)\n",
                encoding="utf-8", newline="\n",
            )
            alternate_geo = fixture.root / "actors/test/alternate_geo.inc.c"
            alternate_geo.write_text(
                _RIGID_GEO.replace("test_geo", "alternate_geo"),
                encoding="utf-8", newline="\n",
            )
            fixture.rehash()
            other = copy.deepcopy(fixture.record)
            other["model"] = "MODEL_ALT"
            other["geo_root"] = "alternate_geo"
            other["model_variants"] = [
                {"model": "MODEL_ALT", "geo_root": "alternate_geo"},
            ]
            other["root_provenance"]["models"] = {
                "MODEL_ALT": {
                    "source": "include/model_ids.h",
                    "binding_source": "levels/test/script.c",
                    "geo_symbol": "alternate_geo",
                    "geo_source": "actors/test/alternate_geo.inc.c",
                },
            }
            with self.assertRaisesRegex(
                    ActorSourceSelectionError, "conflicting model/GeoLayout provenance"):
                compile_actor_variant(fixture.root, 7, 1, [fixture.record, other])

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
        # This synthetic fixture is intentionally outside the measured BOB-v2
        # key set.  It freezes the v1 boundary while the same compiler state
        # machine admits only closure-attested Task 4 signatures.
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
        view = validate_actor_bank(compiled.payload)
        self.assertEqual(view.version, 1)
        self.assertEqual(view.source_sha256.hex(), compiled.source_sha256)
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
            "2a4af81303a523a26f6ed3e9df2ac1a7aeb600a1b2c129158d3a6dc79231b93e",
        )
        self.assertEqual(
            compiled.source_sha256,
            "d5a472a4f4f90145e2882adf997b75912f205f82fc51d718ede704c329c3929d",
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
