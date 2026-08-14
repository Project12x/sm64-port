#!/usr/bin/env python3
"""Real-source contracts for the exact BOB S64B-v2 material subset."""

from __future__ import annotations

import copy
import hashlib
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from actor_bank_format import validate_actor_bank  # noqa: E402
from actor_material_v2 import BOB_DIRECT_TEXTURED_KEYS  # noqa: E402
from actor_variant_bank import (  # noqa: E402
    _SourceIndex,
    _source_model_id,
    ActorSourceDriftError,
    UnsupportedActorSourceError,
    compile_actor_variant,
)
from collect_scene_closure import collect_scene_closure  # noqa: E402
from compile_actor_bank import _family_record  # noqa: E402


ROOT = Path(__file__).resolve().parents[2]

# Hand-frozen from the reviewed 2026-08-11 all-key inventory.  The values are
# independent literals, not derived from the compiler under test.
DRAWABLE_KEYS = (
    (3, 0x007C, "GEO_SHADOW"),
    (4, 0x00CD, "v2"),
    (5, 0x00D7, "GEO_SHADOW"),
    (6, 0x00DB, "v2"),
    (7, 0x00BC, "v2"),
    (8, 0x008F, "v2"),
    (9, 0x0076, "GEO_SHADOW"),
    (11, 0x0068, "GEO_SHADOW"),
    (13, 0x00A3, "v2"),
    (14, 0x006A, "GEO_SCALE"),
    (16, 0x0079, "GEO_SHADOW"),
    (18, 0x00A4, "v2"),
    (19, 0x00C9, "v2"),
    (21, 0x00A8, "v2"),
    (21, 0x008E, "GEO_ASM"),
    (22, 0x00C0, "GEO_SHADOW"),
    (24, 0x007F, "v2"),
    (26, 0x00D4, "GEO_SHADOW"),
    (27, 0x00BE, "GEO_SHADOW"),
    (28, 0x0084, "v2"),
    (29, 0x0080, "v2"),
    (30, 0x007A, "GEO_SHADOW"),
    (32, 0x0096, "v2"),
    (33, 0x0078, "GEO_SHADOW"),
    (35, 0x0066, "GEO_SHADOW"),
    (36, 0x0065, "GEO_SHADOW"),
    (38, 0x00B4, "GEO_SHADOW"),
    (39, 0x00A5, "v2"),
    (40, 0x0095, "v2"),
    (42, 0x0056, "GEO_SHADOW"),
    (43, 0x00C3, "GEO_SHADOW"),
    (44, 0x0087, "GEO_SHADOW"),
    (45, 0x0074, "GEO_SHADOW"),
    (46, 0x00A6, "v2"),
)

DIRECT_KEYS = tuple((ordinal, model) for ordinal, model, result in DRAWABLE_KEYS
                    if result == "v2")


class ActorMaterialV2Test(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.closure = collect_scene_closure(
            ROOT, "bob", 1, ROOT / "tools/saturn/behavior_spawn_rules.json")
        grouped: dict[str, list[dict[str, object]]] = {}
        family_docs: dict[str, dict[str, object]] = {}
        for record in cls.closure["records"]:
            family = _family_record(ROOT, record)
            key = str(family["family_key"])
            grouped.setdefault(key, []).append(record)
            existing = family_docs.get(key)
            if existing is None or str(family["stable_id"]) < str(existing["stable_id"]):
                family_docs[key] = family
        ordered = sorted(family_docs, key=lambda key: (
            int(family_docs[key]["family_id"]), str(family_docs[key]["family_key"])))
        cls.families = {ordinal: grouped[key]
                        for ordinal, key in enumerate(ordered, 1)}
        cls.family_docs = {ordinal: family_docs[key]
                           for ordinal, key in enumerate(ordered, 1)}

    def test_all_47_families_retain_one_exact_admission_outcome(self) -> None:
        """Catches capability-family or MODEL_NONE drift outside drawable replay."""
        unsupported = {
            1: "GEO_CULLING_RADIUS", 2: "GEO_CULLING_RADIUS",
            10: "GEO_CULLING_RADIUS", 12: "GEO_CULLING_RADIUS",
            15: "GEO_CULLING_RADIUS", 17: "GEO_CULLING_RADIUS",
            20: "GEO_BRANCH_AND_LINK", 25: "GEO_CULLING_RADIUS",
            31: "GEO_CULLING_RADIUS", 34: "GEO_CULLING_RADIUS",
            37: "GEO_CULLING_RADIUS", 41: "GEO_CULLING_RADIUS",
            47: "GEO_CULLING_RADIUS",
        }
        self.assertEqual(len(self.family_docs), 47)
        self.assertEqual(
            [(ordinal, variant["model"])
             for ordinal, family in self.family_docs.items()
             for variant in family["model_variants"]
             if variant["model"] == "MODEL_NONE"],
            [(23, "MODEL_NONE"), (36, "MODEL_NONE")],
        )
        actual_unsupported = {
            ordinal: family["unsupported"][0].split(":", 1)[1]
            for ordinal, family in self.family_docs.items()
            if family["unsupported"]
        }
        self.assertEqual(actual_unsupported, unsupported)
        admission_reasons = dict(unsupported)
        admission_reasons[20] = "textured"
        for ordinal, reason in admission_reasons.items():
            records = self.families[ordinal]
            model = self.family_docs[ordinal]["model_variants"][0]["model"]
            index = _SourceIndex(ROOT, records)
            model_id = _source_model_id(index, "include/model_ids.h", model)
            with self.subTest(family=ordinal, model=model), self.assertRaisesRegex(
                    UnsupportedActorSourceError, reason):
                compile_actor_variant(ROOT, ordinal, model_id, records)

    def test_all_34_real_drawable_keys_have_one_exact_outcome(self) -> None:
        """Catches admission drift, silent omission, or broad Geo-state relaxation."""
        self.assertEqual(tuple(sorted(BOB_DIRECT_TEXTURED_KEYS)), DIRECT_KEYS)
        self.assertEqual(len(DRAWABLE_KEYS), 34)
        self.assertEqual(len(DIRECT_KEYS), 15)
        for ordinal, model_id, expected in DRAWABLE_KEYS:
            with self.subTest(family=ordinal, model=f"0x{model_id:04x}"):
                if expected == "v2":
                    compiled = compile_actor_variant(
                        ROOT, ordinal, model_id, self.families[ordinal])
                    view = validate_actor_bank(compiled.payload)
                    self.assertEqual((view.version, view.family_ordinal, view.model_id),
                                     (2, ordinal, model_id))
                    self.assertEqual(compiled.report["schema"],
                                     "sm64-saturn-actor-bank-v2")
                    policy = compiled.report["material_policy"]
                    self.assertGreater(policy["textured_triangle_count"], 0)
                    self.assertEqual(policy["textured_pair_count"], 0)
                    self.assertEqual(policy["tile_input_count"],
                                     policy["textured_triangle_count"])
                else:
                    with self.assertRaisesRegex(
                            UnsupportedActorSourceError, expected):
                        compile_actor_variant(
                            ROOT, ordinal, model_id, self.families[ordinal])

    def test_real_cannon_is_materialized_without_textured_pairing(self) -> None:
        """Catches synthetic substitution or loss of the required demo key."""
        compiled = compile_actor_variant(ROOT, 29, 0x0080, self.families[29])
        repeated = compile_actor_variant(ROOT, 29, 0x0080, self.families[29])
        view = validate_actor_bank(compiled.payload)
        self.assertEqual((compiled.family_ordinal, compiled.model_id), (29, 0x0080))
        self.assertEqual(view.version, 2)
        self.assertEqual(compiled.report["selection"]["model"], "MODEL_CANNON_BASE")
        self.assertEqual(self.family_docs[29]["stable_id"], "bhvCannon")
        self.assertEqual((compiled.source_sha256, compiled.payload_sha256,
                          compiled.payload),
                         (repeated.source_sha256, repeated.payload_sha256,
                          repeated.payload))
        self.assertEqual(
            (compiled.source_sha256, compiled.payload_sha256, len(compiled.payload)),
            ("bb972afe2022977f4b7290d11e28e081f869c7cc7b62b378e28bac17bf76dc4f",
             "2c8eee36768f0a42063949298eca8351bacb74838165a8188520a4b180d63c6d",
             2952),
        )
        self.assertEqual(compiled.report["material_policy"]["textured_triangle_count"], 8)
        self.assertEqual(compiled.report["material_policy"]["textured_pair_count"], 0)
        self.assertEqual(compiled.report["format"]["header_size"], 192)
        self.assertGreater(view.texture_payload_size, 0)
        self.assertGreater(view.clut_payload_size, 0)
        texture_path = "actors/cannon_base/cannon_base.rgba16.png"
        sources = {item["path"]: item["sha256"]
                   for item in compiled.report["sources"]}
        self.assertEqual(sources[texture_path], hashlib.sha256(
            (ROOT / texture_path).read_bytes()).hexdigest())
        self.assertEqual(len(compiled.report["material_policy"]["category_sha256"]), 13)

    def test_real_bobomb_is_the_first_normal_enemy_without_injected_identity(self) -> None:
        compiled = compile_actor_variant(ROOT, 7, 0x00BC, self.families[7])
        repeated = compile_actor_variant(ROOT, 7, 0x00BC, self.families[7])
        view = validate_actor_bank(compiled.payload)
        self.assertEqual((compiled.family_ordinal, compiled.model_id), (7, 0x00BC))
        self.assertEqual(view.version, 2)
        self.assertEqual(compiled.report["selection"]["model"],
                         "MODEL_BLACK_BOBOMB")
        self.assertEqual(self.family_docs[7]["stable_id"], "bhvBobomb")
        self.assertEqual((compiled.source_sha256, compiled.payload_sha256,
                          compiled.payload),
                         (repeated.source_sha256, repeated.payload_sha256,
                          repeated.payload))
        self.assertGreater(view.primitive_count, 0)
        self.assertGreater(compiled.report["material_policy"][
            "textured_triangle_count"], 0)

    def _copied_cannon(self, directory: str) -> tuple[Path, list[dict[str, object]]]:
        root = Path(directory)
        records = copy.deepcopy(self.families[29])
        paths = sorted({str(source["path"])
                        for record in records for source in record["sources"]})
        for relative in paths:
            target = root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / relative, target)
        texture = Path("actors/cannon_base/cannon_base.rgba16.png")
        (root / texture).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / texture, root / texture)
        return root, records

    @staticmethod
    def _replace(root: Path, records: list[dict[str, object]], relative: str,
                 old: str, new: str) -> None:
        path = root / relative
        source = path.read_text(encoding="utf-8")
        if source.count(old) < 1:
            raise AssertionError(f"mutation token not found: {old}")
        path.write_text(source.replace(old, new, 1), encoding="utf-8", newline="\n")
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        for record in records:
            for item in record["sources"]:
                if item["path"] == relative:
                    item["sha256"] = digest

    def test_cannon_material_source_and_state_mutations_fail_named(self) -> None:
        """Catches partial/computed state or source drift entering the whitelist."""
        model = "actors/cannon_base/model.inc.c"
        geo = "actors/cannon_base/geo.inc.c"
        mutations = {
            "texture image": (model, "cannon_base_seg8_texture_080049B8",
                              "missing_cannon_texture"),
            "tile size": (model, "(32 - 1) << G_TEXTURE_IMAGE_FRAC",
                          "(31 - 1) << G_TEXTURE_IMAGE_FRAC"),
            "tile mask/shift": (model, "G_TX_CLAMP, 5, G_TX_NOLOD",
                                "G_TX_CLAMP, 4, G_TX_NOLOD"),
            "tile wrap/clamp": (model, "G_TX_CLAMP, 5, G_TX_NOLOD",
                                "G_TX_WRAP, 5, G_TX_NOLOD"),
            "combine mode": (model, "G_CC_MODULATERGB, G_CC_MODULATERGB",
                             "G_CC_DECALRGBA, G_CC_DECALRGBA"),
            "geometry mode": (model, "gsSPClearGeometryMode(G_SHADING_SMOOTH)",
                              "gsSPClearGeometryMode(G_LIGHTING)"),
            "material layer": (geo, "LAYER_OPAQUE", "LAYER_ALPHA"),
            "material opacity": (geo, "LAYER_OPAQUE", "LAYER_TRANSPARENT"),
            "display-list call": (model,
                                  "gsSPDisplayList(cannon_base_seg8_dl_08005658)",
                                  "gsSPDisplayList(cannon_base_seg8_dl_080056D0)"),
            "tail transfer": (model,
                              "gsSPEndDisplayList(),\n};\n\n// 0x080056D0",
                              "gsSPBranchList(cannon_base_seg8_dl_080056D0),\n};\n\n// 0x080056D0"),
            "texture coordinate": (model, "{     0,   1758}", "{    32,   1758}"),
            "material state trace": (
                model,
                "gsSPSetGeometryMode(G_SHADING_SMOOTH),\n    gsSPEndDisplayList(),",
                "gsSPSetGeometryMode(G_SHADING_SMOOTH),\n"
                "    gsDPSetEnvColor(1, 2, 3, 4),\n    gsSPEndDisplayList(),"),
        }
        for reason, mutation in mutations.items():
            with self.subTest(reason=reason), tempfile.TemporaryDirectory() as directory:
                root, records = self._copied_cannon(directory)
                self._replace(root, records, *mutation)
                with self.assertRaisesRegex(UnsupportedActorSourceError, reason):
                    compile_actor_variant(root, 29, 0x0080, records)

        with tempfile.TemporaryDirectory() as directory:
            root, records = self._copied_cannon(directory)
            texture = root / "actors/cannon_base/cannon_base.rgba16.png"
            payload = bytearray(texture.read_bytes())
            payload[-13] ^= 1
            texture.write_bytes(payload)
            with self.assertRaisesRegex(
                    ActorSourceDriftError,
                    "closure source hash drift.*actors/cannon_base"):
                compile_actor_variant(root, 29, 0x0080, records)

    def test_cannon_texture_png_must_be_in_the_input_closure(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root, records = self._copied_cannon(directory)
            png = "actors/cannon_base/cannon_base.rgba16.png"
            for record in records:
                record["sources"] = [source for source in record["sources"]
                                     if source["path"] != png]
            with self.assertRaisesRegex(
                    UnsupportedActorSourceError,
                    "texture PNG source is not closure-attested.*cannon_base"):
                compile_actor_variant(root, 29, 0x0080, records)


if __name__ == "__main__":
    unittest.main()
