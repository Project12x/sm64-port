#!/usr/bin/env python3
"""Closure-derived checks for the bounded generic actor capability slice.

The closure is authoritative: this test never names a behavior/family in its
runtime logic.  A fixture can add a source-attested capability requirement to
prove that an unresolved field is reported with the exact family identity.
"""

from __future__ import annotations

import argparse
import copy
import json
import tempfile
import unittest
from pathlib import Path

from collect_scene_closure import collect_scene_closure
from compile_actor_bank import _family_record, compile_actor_family_banks


ROOT = Path(__file__).resolve().parents[2]
RULES = ROOT / "tools/saturn/behavior_spawn_rules.json"
DEFAULT_CLOSURE = ROOT / "build/saturn/packages/bob/1/closure.json"
DEFAULT_REPORT = ROOT / "build/saturn/packages/bob/1/actors/actor-families.json"
ORACLE = ROOT / "tools/saturn/fixtures/bob_actor_capability_oracle_v1.json"
EFFECT_ORACLE = ROOT / "tools/saturn/fixtures/bob_actor_effect_oracle_v1.json"

EFFECT_CLASSES = ("BILLBOARD", "ALPHA", "TRANSLUCENT", "SHADOW", "PARTICLE")
KNOWN_MATERIAL_FEATURES = frozenset({
    "alpha", "animated", "billboard", "decal", "shadow", "transparent",
    "translucent",
})


def effect_inventory(
    closure: dict[str, object], report: dict[str, object],
) -> dict[str, object]:
    families = {str(item["family_key"]): item for item in report["families"]}
    classes = {name: [] for name in (*EFFECT_CLASSES, "DECAL")}
    effect_records: set[str] = set()
    unsupported: set[str] = set()
    for record in closure["records"]:
        candidate = _family_record(ROOT, record)
        features = {str(item).lower()
                    for item in record.get("material_feature_bits", ())}
        unknown = features - KNOWN_MATERIAL_FEATURES
        if unknown:
            raise AssertionError("unknown effect material feature(s): " +
                                 ", ".join(sorted(unknown)))
        active_classes = [name for name in EFFECT_CLASSES
                          if name in candidate["capabilities"]]
        if "decal" in features:
            active_classes.append("DECAL")
        for name in active_classes:
            classes[name].append(str(record["stable_id"]))
        if active_classes:
            effect_records.add(str(record["stable_id"]))
            family = families.get(str(candidate["family_key"]))
            if family is None:
                raise AssertionError(f"missing effect family {record['stable_id']}")
            for reason in family["unsupported"]:
                if reason in {"UNSUPPORTED_GEO_NODE:GEO_CULLING_RADIUS",
                              "UNSUPPORTED_GEO_NODE:GEO_BRANCH_AND_LINK"}:
                    unsupported.add(
                        f"{record['stable_id']}[{int(family['family_id']):#010x}]:{reason}")
    return {
        "classes": {name: sorted(values) for name, values in classes.items()},
        # These roles are generated closure fields. They are not recomputed
        # from family names at runtime.
        "roles": {role: sorted({str(child) for record in closure["records"]
                                for child in record.get(role, ())})
                  for role in ("projectiles", "rewards", "effects")},
        "unsupported_effect_records": sorted(unsupported),
    }


def assert_effect_oracle(
    closure: dict[str, object], report: dict[str, object],
) -> None:
    oracle = json.loads(EFFECT_ORACLE.read_text(encoding="utf-8"))
    if oracle.get("schema") != "sm64-saturn-actor-effect-oracle-v1":
        raise AssertionError("unexpected actor effect oracle schema")
    if oracle.get("closure_schema") != closure.get("schema"):
        raise AssertionError("actor effect closure schema drift")
    if oracle.get("family_schema") != report.get("schema") or \
            oracle.get("family_header_content_sha256") != \
                report.get("header_content_sha256") or \
            oracle.get("family_payload_sha256") != report.get("payload_sha256"):
        raise AssertionError("actor effect family source identity drift")
    actual = effect_inventory(closure, report)
    expected = {key: oracle[key] for key in
                ("classes", "roles", "unsupported_effect_records")}
    if actual != expected:
        raise AssertionError(
            "actor effect oracle drift:\n"
            f"expected={json.dumps(expected, sort_keys=True)}\n"
            f"actual={json.dumps(actual, sort_keys=True)}")

RUNTIME_FOR_CLASS = {
    "rigid": ("TRANSFORM", "SCALE", "MATERIAL", "LIFECYCLE"),
    "opaque": ("TRANSFORM", "SCALE", "MATERIAL", "LIFECYCLE"),
    "static-transform": ("TRANSFORM", "SCALE", "MATERIAL", "LIFECYCLE"),
    "platform": ("TRANSFORM", "SCALE", "MATERIAL", "SURFACE", "LIFECYCLE"),
    "surface": ("TRANSFORM", "SCALE", "MATERIAL", "SURFACE", "LIFECYCLE"),
    "collectible": ("TRANSFORM", "SCALE", "MATERIAL", "LIFECYCLE"),
}


def _family_key(root: Path, record: dict[str, object]) -> str:
    provenance = record.get("root_provenance", {})
    models = provenance.get("models", {}) if isinstance(provenance, dict) else {}
    model = str(record.get("model", "MODEL_NONE"))
    binding = models.get(model, {}) if isinstance(models, dict) else {}
    geo_source = binding.get("geo_source") if isinstance(binding, dict) else None
    key = {
        "model": model,
        "geo_source": geo_source,
        "geo_root": str(record.get("geo_root", "none")),
        "animation_table": sorted(record.get("animation_table", ())),
        "model_variants": sorted(
            record.get("model_variants", ()),
            key=lambda item: json.dumps(item, sort_keys=True, separators=(",", ":")),
        ),
    }
    return json.dumps(key, sort_keys=True, separators=(",", ":"))


def _required(record: dict[str, object], class_name: str) -> bool:
    explicit = {str(item).lower().replace("_", "-")
                for item in record.get("capability_requirements", ())}
    if class_name in explicit:
        return True
    if class_name in {"platform", "surface", "collectible"}:
        return False
    model = str(record.get("model", "MODEL_NONE"))
    if model == "MODEL_NONE":
        return False
    features = {str(item).lower() for item in record.get("material_feature_bits", ())}
    variants = record.get("model_variants", ())
    rigid = (not record.get("animation_table") and len(variants) <= 1)
    if class_name in {"rigid", "static-transform"}:
        return rigid
    if class_name == "opaque":
        return not features.intersection({"alpha", "transparent", "translucent"})
    return False


def unresolved_family_ids(
    closure: dict[str, object], report: dict[str, object], class_name: str,
) -> list[str]:
    families = {str(item["family_key"]): item for item in report["families"]}
    capability_bits = report["capability_bits"]
    runtime_bits = report["runtime_capability_bits"]
    required_bit = capability_bits.get(class_name.upper().replace("-", "_"))
    if required_bit is None:
        raise AssertionError(f"closure requested unknown capability class {class_name}")
    required_runtime = 0
    for name in RUNTIME_FOR_CLASS[class_name]:
        required_runtime |= int(runtime_bits[name])
    unresolved: list[str] = []
    for record in closure["records"]:
        if not _required(record, class_name):
            continue
        family = families.get(_family_key(ROOT, record))
        if family is None:
            unresolved.append(f"{record['stable_id']}[family-missing]")
            continue
        if (int(family["capability_mask"]) & int(required_bit) != int(required_bit) or
                int(family["runtime_capability_mask"]) & required_runtime != required_runtime or
                not family["supported"]):
            unresolved.append(f"{record['stable_id']}[{family['family_id']:#010x}]")
    return sorted(set(unresolved))


class BobActorCapabilityTest(unittest.TestCase):
    def test_exact_bob_effect_inventory_and_roles(self) -> None:
        closure = json.loads(DEFAULT_CLOSURE.read_text(encoding="utf-8"))
        report = json.loads(DEFAULT_REPORT.read_text(encoding="utf-8"))
        assert_effect_oracle(closure, report)

    def test_effect_oracle_rejects_unknown_material_and_stale_identity(self) -> None:
        closure = json.loads(DEFAULT_CLOSURE.read_text(encoding="utf-8"))
        report = json.loads(DEFAULT_REPORT.read_text(encoding="utf-8"))
        unknown = copy.deepcopy(closure)
        unknown["records"][0]["material_feature_bits"] = ["mystery-effect"]
        with self.assertRaisesRegex(AssertionError, "unknown effect material"):
            effect_inventory(unknown, report)
        for identity_field in ("header_content_sha256", "payload_sha256"):
            with self.subTest(identity_field=identity_field):
                stale = copy.deepcopy(report)
                stale[identity_field] = "00" * 32
                with self.assertRaisesRegex(AssertionError,
                                             "source identity drift"):
                    assert_effect_oracle(closure, stale)

    def test_effect_oracle_rejects_inventory_role_and_family_mutations(self) -> None:
        closure = json.loads(DEFAULT_CLOSURE.read_text(encoding="utf-8"))
        report = json.loads(DEFAULT_REPORT.read_text(encoding="utf-8"))

        inventory_mutation = copy.deepcopy(closure)
        record = next(item for item in inventory_mutation["records"]
                      if "alpha" in item.get("material_feature_bits", ()))
        record["material_feature_bits"] = [
            item for item in record["material_feature_bits"] if item != "alpha"
        ]
        with self.assertRaisesRegex(AssertionError, "oracle drift"):
            assert_effect_oracle(inventory_mutation, report)

        role_mutation = copy.deepcopy(closure)
        record = next(item for item in role_mutation["records"]
                      if item.get("effects"))
        record["effects"] = list(record["effects"]) + ["bhvMutationSentinel"]
        with self.assertRaisesRegex(AssertionError, "oracle drift"):
            assert_effect_oracle(role_mutation, report)

        missing_family = copy.deepcopy(report)
        candidate = _family_record(ROOT, closure["records"][0])
        missing_family["families"] = [
            family for family in missing_family["families"]
            if family["family_key"] != candidate["family_key"]
        ]
        with self.assertRaisesRegex(AssertionError, "missing effect family"):
            effect_inventory(closure, missing_family)

    def test_unknown_source_requirement_names_exact_family(self) -> None:
        closure = collect_scene_closure(ROOT, "bob", 1, RULES)
        oracle = json.loads(ORACLE.read_text(encoding="utf-8"))
        self.assertEqual(oracle["schema"], "sm64-saturn-actor-capability-oracle-v1")
        self.assertEqual(oracle["closure_schema"], closure["schema"])
        fixture_oracle = oracle["fixture"]
        fixture = copy.deepcopy(closure)
        fixture["records"] = [copy.deepcopy(closure["records"][0])]
        fixture["records"][0]["capability_requirements"] = fixture_oracle[
            "capability_requirements"
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "closure.json"
            path.write_text(json.dumps(fixture, sort_keys=True), encoding="utf-8")
            report = compile_actor_family_banks(ROOT, path, Path(directory) / "actors")
        family = report["families"][0]
        self.assertFalse(family["supported"])
        self.assertEqual(family["stable_id"], fixture_oracle["stable_id"])
        self.assertEqual(family["family_id"], int(fixture_oracle["family_id"], 16))
        self.assertEqual(family["unsupported"], fixture_oracle["unsupported"])

    def test_unverified_capability_hints_fail_closed(self) -> None:
        closure = collect_scene_closure(ROOT, "bob", 1, RULES)
        fixture = copy.deepcopy(closure)
        fixture["records"] = [copy.deepcopy(closure["records"][0])]
        fixture["records"][0]["capability_hints"] = ["platform", "collectible"]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "closure.json"
            path.write_text(json.dumps(fixture, sort_keys=True), encoding="utf-8")
            report = compile_actor_family_banks(ROOT, path, Path(directory) / "actors")
        family = report["families"][0]
        self.assertFalse(family["supported"])
        self.assertEqual(family["unsupported"], [
            "UNVERIFIED_CAPABILITY_HINT:COLLECTIBLE",
            "UNVERIFIED_CAPABILITY_HINT:PLATFORM",
        ])

    def test_real_bob_capability_requirements_are_closure_derived(self) -> None:
        closure = collect_scene_closure(ROOT, "bob", 1, RULES)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "closure.json"
            path.write_text(json.dumps(closure, sort_keys=True), encoding="utf-8")
            report = compile_actor_family_banks(ROOT, path, Path(directory) / "actors")
        for class_name in ("rigid", "opaque", "static-transform"):
            unresolved = unresolved_family_ids(closure, report, class_name)
            # The generic capability mask must not erase the independent
            # source-geometry gap.  Derive the expected unresolved IDs from
            # the generated report; no behavior/family allow-list is used.
            families = {str(item["family_key"]): item for item in report["families"]}
            expected = sorted({
                f"{record['stable_id']}[{families[_family_key(ROOT, record)]['family_id']:#010x}]"
                for record in closure["records"]
                if _required(record, class_name)
                and not families[_family_key(ROOT, record)]["supported"]
            })
            self.assertEqual(unresolved, expected)
            for item in expected:
                stable_id = item.split("[", 1)[0]
                family = next(
                    family for family in report["families"]
                    if family["family_id"] == int(item.rsplit("[", 1)[1][:-1], 16)
                )
                self.assertTrue(
                    any(str(reason).startswith("UNSUPPORTED_GEO_NODE:")
                        for reason in family["unsupported"]),
                    (class_name, stable_id, family["unsupported"]),
                )
        # No current Task 14 field authoritatively declares platform/surface or
        # collectible ownership.  The empty requirement set is intentional;
        # adding one must fail with the exact family ID above.
        self.assertEqual(unresolved_family_ids(closure, report, "surface"), [])
        self.assertEqual(unresolved_family_ids(closure, report, "collectible"), [])
        platform_bit = report["capability_bits"]["PLATFORM"]
        collectible_bit = report["capability_bits"]["COLLECTIBLE"]
        surface_runtime_bit = report["runtime_capability_bits"]["SURFACE"]
        for family in report["families"]:
            self.assertEqual(int(family["capability_mask"]) &
                             (platform_bit | collectible_bit), 0)
            self.assertEqual(int(family["runtime_capability_mask"]) &
                             surface_runtime_bit, 0)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--class", dest="class_name",
                        choices=sorted((*RUNTIME_FOR_CLASS, "articulated", "effect")),
                        default="opaque")
    parser.add_argument("--closure", type=Path, default=DEFAULT_CLOSURE)
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    args = parser.parse_args()
    if args.class_name == "articulated":
        from test_bob_articulated_capabilities import main as articulated_main
        articulated_main(args.closure, args.report)
        return
    closure = json.loads(args.closure.read_text(encoding="utf-8"))
    report = json.loads(args.report.read_text(encoding="utf-8"))
    if args.class_name == "effect":
        assert_effect_oracle(closure, report)
        inventory = effect_inventory(closure, report)
        counts = ", ".join(
            f"{name}={len(records)}"
            for name, records in inventory["classes"].items())
        print("actor effect capabilities: PASS " + counts +
              f", unsupported={len(inventory['unsupported_effect_records'])}")
        return
    unresolved = unresolved_family_ids(closure, report, args.class_name)
    if unresolved:
        raise SystemExit(
            f"unresolved {args.class_name} capability/family IDs: " + ", ".join(unresolved))
    print(f"actor capabilities: PASS class={args.class_name} required=0 unresolved=0")


if __name__ == "__main__":
    main()
