#!/usr/bin/env python3
"""Task 5 RED/GREEN coverage for the real bounded BOB S64F-v3 build."""

from __future__ import annotations

import json
import shutil
import sys
import tempfile
import unittest
from unittest import mock
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from actor_bank_format import validate_actor_bank  # noqa: E402
from actor_family_bundle import validate_bundle  # noqa: E402
from actor_material_v2 import BOB_DIRECT_TEXTURED_KEYS  # noqa: E402
from compile_actor_family_bundle import (  # noqa: E402
    _publish,
    compile_scene_bundle,
)


ROOT = Path(__file__).resolve().parents[2]
CLOSURE = ROOT / "build/saturn/packages/bob/1/closure.json"
FAMILY_REPORT = ROOT / "build/saturn/packages/bob/1/actors/actor-families.json"
MODEL_IDS = ROOT / "include/model_ids.h"


class CompileActorFamilyBundleTest(unittest.TestCase):
    def test_make_verifier_runs_c_parser_against_real_bob_bundle(self) -> None:
        makefile = (ROOT / "Makefile.saturn.mk").read_text(encoding="utf-8")
        self.assertIn(
            "verify-actor-family-bundle-build: compile-actor-family-bundle "
            "verify-actor-family-bundle",
            makefile,
        )
        self.assertIn(
            'actor-family-bundle-test$(HOST_EXEEXT)" --validate-only '
            '"$(ACTOR_FAMILY_BUNDLE_DIR)/bob-area1-actors-v3.s64f"',
            makefile,
        )

    def test_real_bob_bundle_is_v2_only_complete_and_host_neutral(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            report = compile_scene_bundle(
                ROOT, CLOSURE, FAMILY_REPORT, MODEL_IDS, 7, Path(temporary))
            payload = (Path(temporary) / report["outputs"]["bundle"]).read_bytes()
            view = validate_bundle(payload)
            self.assertEqual(view.family_count, 47)
            self.assertEqual(view.variant_count, 14)
            self.assertEqual(
                [(row.family_ordinal, row.model_id) for row in view.variants],
                list(BOB_DIRECT_TEXTURED_KEYS),
            )
            versions = []
            for row in view.variants:
                start = view.bank_payloads_offset + row.bank_offset
                versions.append(validate_actor_bank(payload[start:start + row.bank_size]).version)
            self.assertEqual(versions, [2] * 14)
            self.assertIn((29, 0x0080), BOB_DIRECT_TEXTURED_KEYS)
            self.assertEqual(report["supported_variant_count"], 14)
            self.assertEqual(report["unsupported_drawable_count"], 20)
            self.assertEqual(report["model_none_count"], 2)
            self.assertEqual(report["bundle_totals"]["embedded_s64b_bytes"], 40920)
            self.assertEqual(report["bundle_totals"]["texture_bytes"], 16640)
            self.assertEqual(report["bundle_totals"]["clut_bytes"], 2816)
            self.assertEqual(report["bundle_totals"]["workspace_bytes"], 1091)
            self.assertEqual(
                report["resource_inventory"]["workspace"],
                {"used_bytes": 1091, "capacity_bytes": 1280, "margin_bytes": 189},
            )
            self.assertIn(
                b"#define SM64_SATURN_ACTOR_BUNDLE_WORKSPACE_BYTES 1280U",
                (Path(temporary) / "actor_bundle_capacity.h").read_bytes(),
            )
            inventory = report["resource_inventory"]
            self.assertEqual(inventory["guaranteed_any_mix_count"], 19)
            self.assertEqual(inventory["vdp1_residency"]["future_repartition_margin_bytes"], 33216)
            self.assertFalse(inventory["vdp1_residency"]["current_binders_own_remaining"])
            self.assertEqual(inventory["source_ceiling_diagnostic"]["output_records"], 85512)
            reasons = [row["reason"] for row in report["unsupported_drawable"]]
            self.assertEqual(sum("GEO_SHADOW" in reason for reason in reasons), 18)
            self.assertEqual(sum("GEO_SCALE" in reason for reason in reasons), 1)
            self.assertEqual(sum("GEO_ASM" in reason for reason in reasons), 1)
            cannon = next(row for row in report["banks"]
                          if (row["family_ordinal"], row["model_id"]) == (29, 0x0080))
            self.assertEqual((cannon["payload_bytes"], cannon["draw_records_per_instance"],
                              cannon["texture_commands_per_instance"],
                              cannon["gouraud_tables_per_instance"]),
                             (2952, 30, 8, 30))
            rendered = json.dumps(report, sort_keys=True)
            self.assertNotIn(str(ROOT), rendered)
            self.assertNotIn("\\\\", rendered)

    def test_publication_is_report_last_no_clobber_and_rollback_safe(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            first = compile_scene_bundle(
                ROOT, CLOSURE, FAMILY_REPORT, MODEL_IDS, 9, output)
            names = tuple(first["publication_order"])
            self.assertEqual(names[-1], first["outputs"]["report"])
            before = {name: (output / name).read_bytes() for name in names}
            with self.assertRaisesRegex(ValueError, "publication target exists"):
                compile_scene_bundle(ROOT, CLOSURE, FAMILY_REPORT, MODEL_IDS, 9, output)
            self.assertEqual(before, {name: (output / name).read_bytes() for name in names})

    def test_mid_publication_failure_rolls_back_every_visible_target(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "out"
            files = {name: name.encode() for name in ("a", "b", "report")}
            real_link = __import__("os").link
            calls = 0

            def fail_second(source, target):
                nonlocal calls
                calls += 1
                if calls == 2:
                    raise OSError("injected publication failure")
                return real_link(source, target)

            with mock.patch("compile_actor_family_bundle.os.link", side_effect=fail_second):
                with self.assertRaisesRegex(OSError, "injected"):
                    _publish(output, files, ("a", "b", "report"))
            self.assertFalse(any(output.iterdir()))

    def test_relocated_real_bob_build_is_byte_identical(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            relocated = base / "relocated"
            closure = json.loads(CLOSURE.read_text(encoding="utf-8"))
            profile_path = ROOT / "tools/saturn/profiles/sourceboot-bob-demo-v1.json"
            profile = json.loads(profile_path.read_text(encoding="utf-8"))
            relatives = {"build/saturn/packages/bob/1/closure.json",
                         "build/saturn/packages/bob/1/actors/actor-families.json",
                         "build/saturn/packages/bob/1/provisional/scene-package-report.json",
                         "include/model_ids.h",
                         "tools/saturn/profiles/sourceboot-bob-demo-v1.json"}
            relatives.update(item["path"] for record in closure["records"]
                             for item in record["sources"])
            for descriptor in profile["package_descriptors"]:
                relatives.add(descriptor)
                document = json.loads((ROOT / descriptor).read_text(encoding="utf-8"))
                relatives.update(item["path"] for item in document["inputs"])
            for relative in sorted(relatives):
                target = relocated / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(ROOT / relative, target)
            out_a, out_b = base / "a", base / "b"
            first = compile_scene_bundle(ROOT, CLOSURE, FAMILY_REPORT, MODEL_IDS, 11, out_a)
            second = compile_scene_bundle(
                relocated, relocated / "build/saturn/packages/bob/1/closure.json",
                relocated / "build/saturn/packages/bob/1/actors/actor-families.json",
                relocated / "include/model_ids.h", 11, out_b)
            self.assertEqual(first, second)
            for name in first["publication_order"]:
                self.assertEqual((out_a / name).read_bytes(), (out_b / name).read_bytes())


if __name__ == "__main__":
    unittest.main()
