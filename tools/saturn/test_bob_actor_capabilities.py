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
from compile_actor_bank import compile_actor_family_banks


ROOT = Path(__file__).resolve().parents[2]
RULES = ROOT / "tools/saturn/behavior_spawn_rules.json"
DEFAULT_CLOSURE = ROOT / "build/saturn/packages/bob/1/closure.json"
DEFAULT_REPORT = ROOT / "build/saturn/packages/bob/1/actors/actor-families.json"

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
    def test_unknown_source_requirement_names_exact_family(self) -> None:
        closure = collect_scene_closure(ROOT, "bob", 1, RULES)
        fixture = copy.deepcopy(closure)
        fixture["records"] = [copy.deepcopy(closure["records"][0])]
        fixture["records"][0]["capability_requirements"] = ["surface"]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "closure.json"
            path.write_text(json.dumps(fixture, sort_keys=True), encoding="utf-8")
            report = compile_actor_family_banks(ROOT, path, Path(directory) / "actors")
        family = report["families"][0]
        self.assertFalse(family["supported"])
        self.assertEqual(family["stable_id"], "bhv1Up")
        self.assertIn("UNRESOLVED_CAPABILITY:SURFACE", family["unsupported"])

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


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--class", dest="class_name", choices=sorted(RUNTIME_FOR_CLASS),
                        default="opaque")
    parser.add_argument("--closure", type=Path, default=DEFAULT_CLOSURE)
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    args = parser.parse_args()
    closure = json.loads(args.closure.read_text(encoding="utf-8"))
    report = json.loads(args.report.read_text(encoding="utf-8"))
    unresolved = unresolved_family_ids(closure, report, args.class_name)
    if unresolved:
        raise SystemExit(
            f"unresolved {args.class_name} capability/family IDs: " + ", ".join(unresolved))
    print(f"actor capabilities: PASS class={args.class_name} required=0 unresolved=0")


if __name__ == "__main__":
    main()
