#!/usr/bin/env python3
"""RED/GREEN coverage for the generic, content-addressed actor family bank."""

from __future__ import annotations

import copy
import json
import tempfile
import unittest
from pathlib import Path

from actor_source import (
    ACTOR_CAP_ALPHA,
    ACTOR_CAP_EFFECT,
    ACTOR_CAP_HELD,
    ACTOR_CAP_LOD,
    ACTOR_CAP_PARENTED,
    ACTOR_CAP_PARTICLE,
    ACTOR_CAP_ANIMATED,
    ACTOR_CAP_BILLBOARD,
    ACTOR_CAP_MODEL_MUTATION,
    ACTOR_CAP_SHADOW,
    ACTOR_CAP_SURFACE,
    ACTOR_CAP_TRANSLUCENT,
    analyze_actor_capabilities,
)
from collect_scene_closure import collect_scene_closure
from compile_actor_bank import (
    compile_actor_family_banks,
    select_actor_family,
    validate_family_bank_payload,
)


ROOT = Path(__file__).resolve().parents[2]
RULES = ROOT / "tools/saturn/behavior_spawn_rules.json"


class GenericActorBankTest(unittest.TestCase):
    def test_feature_fixture_is_source_vocabulary_not_family_name(self) -> None:
        report = analyze_actor_capabilities(
            """
            GEO_SHADOW(1, 2, 3), GEO_ANIMATED_PART(0, 0, 0, 0, x),
            GEO_BILLBOARD(), GEO_SWITCH_CASE(2, fn), GEO_RENDER_RANGE(1, 2),
            GEO_HELD_OBJECT(0, 0, 0, 0, x),
            GEO_OPEN_NODE(), GEO_CLOSE_NODE()
            """,
            material_feature_bits=("alpha", "translucent", "surface", "particle"),
            model_variants=({"model": "a"}, {"model": "b"}),
            object_roots=("spawn:fixture",), effects=("fixture_effect",),
        )
        self.assertTrue(report.mask & ACTOR_CAP_ANIMATED)
        self.assertTrue(report.mask & ACTOR_CAP_BILLBOARD)
        self.assertTrue(report.mask & ACTOR_CAP_SHADOW)
        self.assertTrue(report.mask & ACTOR_CAP_MODEL_MUTATION)
        for bit in (ACTOR_CAP_ALPHA, ACTOR_CAP_TRANSLUCENT, ACTOR_CAP_PARENTED,
                    ACTOR_CAP_HELD, ACTOR_CAP_SURFACE, ACTOR_CAP_LOD,
                    ACTOR_CAP_PARTICLE, ACTOR_CAP_EFFECT):
            self.assertTrue(report.mask & bit)
        self.assertEqual(report.unsupported, ())

    def test_unsupported_node_and_hash_drift_are_explicit(self) -> None:
        report = analyze_actor_capabilities("GEO_OPEN_NODE(), GEO_PRIVATE_NODE()")
        self.assertEqual(report.unsupported, ("GEO_PRIVATE_NODE",))
        closure = collect_scene_closure(ROOT, "bob", 1, RULES)
        fixture = copy.deepcopy(closure)
        fixture["records"] = [fixture["records"][0]]
        fixture["records"][0]["sources"][0]["sha256"] = "0" * 64
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "closure.json"
            path.write_text(json.dumps(fixture), encoding="utf-8")
            report = compile_actor_family_banks(ROOT, path, Path(directory) / "actors")
        self.assertFalse(report["complete_closure"])
        self.assertTrue(any("SOURCE_HASH_DRIFT" in item
                            for item in report["families"][0]["unsupported"]))

    def test_real_bob_compiles_deterministically_and_preserves_unsupported(self) -> None:
        closure = collect_scene_closure(ROOT, "bob", 1, RULES)
        with tempfile.TemporaryDirectory() as left, tempfile.TemporaryDirectory() as right:
            left_path = Path(left) / "closure.json"
            right_path = Path(right) / "closure.json"
            left_path.write_text(json.dumps(closure, sort_keys=True), encoding="utf-8")
            right_path.write_text(json.dumps(closure, sort_keys=True), encoding="utf-8")
            first = compile_actor_family_banks(ROOT, left_path, Path(left) / "actors")
            second = compile_actor_family_banks(ROOT, right_path, Path(right) / "actors")
            first_payload = (Path(first["payload"])).read_bytes()
        self.assertEqual(first["payload_sha256"], second["payload_sha256"])
        self.assertEqual(first["closure_record_count"], len(closure["records"]))
        self.assertGreater(first["family_count"], 0)
        self.assertTrue(any(not item["supported"] for item in first["families"]))
        validate_family_bank_payload(first_payload)

    def test_selection_is_smallest_supported_capability_and_capacity(self) -> None:
        families = [
            {"stable_id": "large", "supported": True, "capability_mask": 1,
             "maximum_live_instances": 20, "family_id": 30, "family_key": "large"},
            {"stable_id": "small", "supported": True, "capability_mask": 1,
             "maximum_live_instances": 2, "family_id": 20, "family_key": "small"},
            {"stable_id": "unsupported", "supported": False, "capability_mask": 1,
             "maximum_live_instances": 1, "family_id": 1, "family_key": "bad"},
        ]
        self.assertEqual(select_actor_family(families, 1, 1), "small")
        self.assertEqual(select_actor_family(families, 1, 3), "large")
        self.assertIsNone(select_actor_family(families, 2, 1))


if __name__ == "__main__":
    unittest.main()
