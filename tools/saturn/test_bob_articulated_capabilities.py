#!/usr/bin/env python3
"""Closure-derived articulated/enemy capability admission checks.

The closure remains authoritative.  This diagnostic deliberately reports
unsupported representatives and records instead of adding a family allow-list
or inventing HELD/LOD evidence that BOB does not currently provide.
"""

from __future__ import annotations

import copy
import hashlib
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
ORACLE = ROOT / "tools/saturn/fixtures/bob_articulated_capability_oracle_v1.json"

CAPABILITY_CLASSES = (
    "ANIMATED", "SWITCH", "PARENTED", "HELD", "MODEL_MUTATION", "LOD",
)
ADMISSION_UNAVAILABLE = frozenset({"PARENTED", "HELD", "LOD"})
ADMISSION_REASONS = {
    "PARENTED": "unavailable:typed-parent-identity",
    "HELD": "unavailable:no-source-evidence",
    "LOD": "unavailable:no-source-evidence",
}


def _required(record: dict[str, object], candidate: dict[str, object],
              class_name: str) -> bool:
    nodes = set(candidate["geo_nodes"])
    if class_name == "ANIMATED":
        return bool(candidate["animation_table"]) or "GEO_ANIMATED_PART" in nodes
    if class_name == "SWITCH":
        return "GEO_SWITCH_CASE" in nodes
    if class_name == "PARENTED":
        return any(str(root).startswith("spawn:")
                   for root in record.get("object_roots", ()))
    if class_name == "HELD":
        return "GEO_HELD_OBJECT" in nodes
    if class_name == "MODEL_MUTATION":
        return len(candidate["model_variants"]) > 1 or "GEO_SWITCH_CASE" in nodes
    if class_name == "LOD":
        return "GEO_RENDER_RANGE" in nodes
    raise AssertionError(f"unknown capability class {class_name}")


def capability_records(
    root: Path, closure: dict[str, object], report: dict[str, object],
    class_name: str,
) -> list[tuple[str, int, bool]]:
    families = {str(item["family_key"]): item for item in report["families"]}
    result: list[tuple[str, int, bool]] = []
    for record in closure["records"]:
        candidate = _family_record(root, record)
        if not _required(record, candidate, class_name):
            continue
        family = families.get(str(candidate["family_key"]))
        if family is None:
            result.append((str(record["stable_id"]), 0, False))
        else:
            result.append((str(record["stable_id"]), int(family["family_id"]),
                           bool(family["supported"])))
    return result


def unresolved_records(
    root: Path, closure: dict[str, object], report: dict[str, object],
    class_name: str,
) -> list[str]:
    return sorted({
        f"{stable_id}[{family_id:#010x}]"
        for stable_id, family_id, supported
        in capability_records(root, closure, report, class_name)
        if not supported
    })


def admitted_records(
    root: Path, closure: dict[str, object], report: dict[str, object],
    class_name: str,
) -> list[tuple[str, int]]:
    """Return records eligible for runtime admission, not mere provenance."""
    if class_name in ADMISSION_UNAVAILABLE:
        return []
    return [
        (stable_id, family_id)
        for stable_id, family_id, supported
        in capability_records(root, closure, report, class_name)
        if supported
    ]


def query_snapshot(
    root: Path, closure: dict[str, object], report: dict[str, object],
    class_name: str,
) -> dict[str, object]:
    records = capability_records(root, closure, report, class_name)
    record_ids = sorted(
        f"{stable_id}[{family_id:#010x}]"
        for stable_id, family_id, _ in records
    )
    return {
        "capability_bit": int(report["capability_bits"][class_name]),
        "records": len(records),
        "representatives": len({family_id for _, family_id, _ in records}),
        "record_set_sha256": hashlib.sha256(
            "\n".join(record_ids).encode("utf-8")
        ).hexdigest(),
        "unresolved": unresolved_records(root, closure, report, class_name),
        "admitted": len(admitted_records(root, closure, report, class_name)),
        "admission": ADMISSION_REASONS.get(class_name, "available"),
    }


def load_oracle() -> dict[str, object]:
    oracle = json.loads(ORACLE.read_text(encoding="utf-8"))
    if oracle.get("schema") != "sm64-saturn-articulated-capability-oracle-v1":
        raise AssertionError("unexpected articulated capability oracle schema")
    return oracle


def assert_oracle(
    root: Path, closure: dict[str, object], report: dict[str, object],
) -> None:
    oracle = load_oracle()
    if oracle.get("closure_schema") != closure.get("schema"):
        raise AssertionError("articulated oracle closure schema drift")
    actual = {
        class_name: query_snapshot(root, closure, report, class_name)
        for class_name in CAPABILITY_CLASSES
    }
    expected = oracle.get("queries")
    if actual != expected:
        raise AssertionError(
            "articulated capability oracle drift:\n"
            f"expected={json.dumps(expected, sort_keys=True)}\n"
            f"actual={json.dumps(actual, sort_keys=True)}"
        )


class BobArticulatedCapabilityTest(unittest.TestCase):
    def _real_report(self) -> tuple[dict[str, object], dict[str, object]]:
        closure = collect_scene_closure(ROOT, "bob", 1, RULES)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "closure.json"
            path.write_text(json.dumps(closure, sort_keys=True), encoding="utf-8")
            report = compile_actor_family_banks(ROOT, path, Path(directory) / "actors")
        return closure, report

    def test_closure_queries_are_generic_and_source_derived(self) -> None:
        closure, report = self._real_report()
        assert_oracle(ROOT, closure, report)
        for class_name in CAPABILITY_CLASSES:
            records = capability_records(ROOT, closure, report, class_name)
            unresolved = unresolved_records(ROOT, closure, report, class_name)
            for stable_id, family_id, supported in records:
                if not supported:
                    self.assertNotEqual(family_id, 0, stable_id)
            if class_name in {"ANIMATED", "SWITCH", "MODEL_MUTATION"}:
                self.assertGreater(len(records), 0, class_name)
            if unresolved:
                families = {int(item["family_id"]): item for item in report["families"]}
                self.assertTrue(all(
                    any(str(reason).startswith("UNSUPPORTED_GEO_NODE:")
                        for reason in families[int(item.rsplit("[", 1)[1][:-1], 16)]["unsupported"])
                    for item in unresolved
                ))

        # Spawn provenance is not a render-time parent identity.  Task 14's
        # immutable snapshot currently publishes NO_PARENT, so parent/held/LOD
        # are deliberately not runtime-admissible even when provenance exists.
        self.assertEqual(len(capability_records(ROOT, closure, report, "PARENTED")), 52)
        self.assertEqual(admitted_records(ROOT, closure, report, "PARENTED"), [])
        self.assertIn(
            ("bhvChainChompChainPart", 0xC8FF5F76, True),
            capability_records(ROOT, closure, report, "PARENTED"),
        )
        self.assertIn(
            ("bhvSpawnedStar", 0xAE675DF8, True),
            capability_records(ROOT, closure, report, "PARENTED"),
        )
        self.assertEqual(capability_records(ROOT, closure, report, "HELD"), [])
        self.assertEqual(capability_records(ROOT, closure, report, "LOD"), [])

    def test_unresolved_requirement_names_exact_family(self) -> None:
        closure = collect_scene_closure(ROOT, "bob", 1, RULES)
        fixture = copy.deepcopy(closure)
        fixture["records"] = [copy.deepcopy(closure["records"][0])]
        fixture["records"][0]["capability_requirements"] = ["ANIMATED", "MYSTERY"]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "closure.json"
            path.write_text(json.dumps(fixture, sort_keys=True), encoding="utf-8")
            report = compile_actor_family_banks(ROOT, path, Path(directory) / "actors")
        family = report["families"][0]
        self.assertEqual(family["stable_id"], "bhv1Up")
        self.assertEqual(family["family_id"], 0x9322461F)
        self.assertEqual(family["unsupported"], [
            "UNKNOWN_CAPABILITY:MYSTERY",
            "UNRESOLVED_CAPABILITY:ANIMATED",
        ])


def main(closure_path: Path = DEFAULT_CLOSURE,
         report_path: Path = DEFAULT_REPORT) -> None:
    closure = json.loads(closure_path.read_text(encoding="utf-8"))
    report = json.loads(report_path.read_text(encoding="utf-8"))
    assert_oracle(ROOT, closure, report)
    print(
        "actor articulated capabilities: closure_records="
        f"{len(closure['records'])} family_representatives={len(report['families'])}"
    )
    for class_name in CAPABILITY_CLASSES:
        records = capability_records(ROOT, closure, report, class_name)
        representatives = sorted({family_id for _, family_id, _ in records})
        unresolved = unresolved_records(ROOT, closure, report, class_name)
        print(
            f"{class_name}: records={len(records)} representatives={len(representatives)} "
            f"unresolved_records={len(unresolved)}"
        )
        if unresolved:
            print("  unresolved: " + ", ".join(unresolved))


if __name__ == "__main__":
    main()
