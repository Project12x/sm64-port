#!/usr/bin/env python3
"""Task 5 RED/GREEN coverage for the real bounded BOB S64F-v3 build."""

from __future__ import annotations

import ast
import json
import re
import shutil
import subprocess
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
    _package_class_bytes,
    _publish,
    compile_scene_bundle,
    validate_publication,
)


ROOT = Path(__file__).resolve().parents[2]
CLOSURE = ROOT / "build/saturn/packages/bob/1/closure.json"
FAMILY_REPORT = ROOT / "build/saturn/packages/bob/1/actors/actor-families.json"
MODEL_IDS = ROOT / "include/model_ids.h"
GENERATION = json.loads(FAMILY_REPORT.read_text(encoding="utf-8"))[
    "scene_package_generation"]


def _local_import_closure(entry: Path) -> set[Path]:
    """Return the deterministic repository-local import closure for one tool."""
    tools_dir = ROOT / "tools/saturn"
    pending = [entry.resolve()]
    closure: set[Path] = set()
    while pending:
        source = pending.pop()
        if source in closure:
            continue
        closure.add(source)
        tree = ast.parse(source.read_text(encoding="utf-8"), filename=str(source))
        module_names: set[str] = set()
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                module_names.update(alias.name for alias in node.names)
            elif isinstance(node, ast.ImportFrom) and node.level == 0 and node.module:
                module_names.add(node.module)
        for module_name in sorted(module_names):
            candidate = tools_dir.joinpath(*module_name.split(".")).with_suffix(".py")
            if candidate.is_file() and candidate.resolve() not in closure:
                pending.append(candidate.resolve())
    return closure


def _make_tool_inputs() -> set[Path]:
    makefile = (ROOT / "Makefile.saturn.mk").read_text(encoding="utf-8")
    match = re.search(
        r"^ACTOR_FAMILY_BUNDLE_TOOL_INPUTS := \\\n(?P<body>(?:\t.*(?:\\\n|\n))*)",
        makefile,
        flags=re.MULTILINE,
    )
    if match is None:
        raise AssertionError("ACTOR_FAMILY_BUNDLE_TOOL_INPUTS assignment is missing")
    return {
        ROOT / line.strip().removesuffix("\\").strip().removeprefix(
            "$(SATURN_REPO_ROOT)/")
        for line in match.group("body").splitlines()
        if line.strip()
    }


class CompileActorFamilyBundleTest(unittest.TestCase):
    def test_residency_gate_builds_and_verifies_real_bundle_from_absent_output(
            self) -> None:
        make = shutil.which("make")
        self.assertIsNotNone(make, "GNU Make is required by Makefile.saturn.mk")
        with tempfile.TemporaryDirectory(
                prefix="actor-residency-make-", dir=ROOT / "build") as temporary:
            output_dir = Path(temporary) / "actors-v3"
            report = output_dir / "actor-family-bundle.json"
            completed = subprocess.run(
                [
                    make,
                    "-f",
                    "Makefile.saturn.mk",
                    "-n",
                    "verify-actor-texture-residency",
                    f"ACTOR_FAMILY_BUNDLE_DIR={output_dir.as_posix()}",
                    f"ACTOR_FAMILY_BUNDLE_REPORT={report.as_posix()}",
                ],
                cwd=ROOT,
                check=True,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
        self.assertIn("compile_actor_family_bundle.py", completed.stdout)
        self.assertIn("--verify-publication", completed.stdout)
        self.assertIn("--validate-only", completed.stdout)

    def test_make_tool_inputs_cover_repository_local_import_closure(self) -> None:
        entry = ROOT / "tools/saturn/compile_actor_family_bundle.py"
        closure = _local_import_closure(entry)
        declared_inputs = _make_tool_inputs()
        nonexistent = sorted(path.relative_to(ROOT).as_posix()
                             for path in declared_inputs if not path.is_file())
        self.assertEqual(nonexistent, [])
        declared = {path.resolve() for path in declared_inputs}
        missing = sorted(path.relative_to(ROOT).as_posix()
                         for path in closure - declared)
        self.assertEqual(missing, [])

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
        self.assertIn(
            '$(ACTOR_FAMILY_BUNDLE_REPORT): $(ACTOR_FAMILY_BUNDLE_INPUTS)', makefile)
        self.assertNotIn(
            '$(ACTOR_FAMILY_BUNDLE_REPORT): | compile-actor-banks', makefile)
        self.assertIn('--verify-publication', makefile)
        self.assertIn('ACTOR_FAMILY_BUNDLE_CLOSURE_INPUTS', makefile)
        self.assertIn("['source_hashes']", makefile)

    def test_family_report_is_exactly_reconciled_before_compilation(self) -> None:
        original = json.loads(FAMILY_REPORT.read_text(encoding="utf-8"))
        for label, mutate, generation in (
            ("ceiling", lambda report: report["families"][28].__setitem__(
                "maximum_live_instances", 1), GENERATION),
            ("host path", lambda report: report["families"][28]["sources"][0].__setitem__(
                "path", "C:/host-private/secret.bin"), GENERATION),
            ("escaping path", lambda report: report["families"][28]["sources"][0].__setitem__(
                "path", "../host-private/secret.bin"), GENERATION),
            ("case collision", lambda report: report["families"][28]["sources"].append({
                **report["families"][28]["sources"][0],
                "path": report["families"][28]["sources"][0]["path"].upper(),
            }), GENERATION),
            ("capabilities", lambda report: report["families"][28].__setitem__(
                "capability_mask", report["families"][28]["capability_mask"] ^ 1), GENERATION),
            ("source hash", lambda report: report["families"][28]["sources"][0].__setitem__(
                "sha256", "0" * 64), GENERATION),
            ("actor count", lambda report: report["families"][28].__setitem__(
                "actor_count", report["families"][28]["actor_count"] + 1), GENERATION),
            ("generation", lambda report: None, GENERATION + 1),
        ):
            with self.subTest(label=label), tempfile.TemporaryDirectory(
                    prefix="actor-family-report-test-", dir=ROOT / "build") as temporary:
                report = json.loads(json.dumps(original))
                mutate(report)
                report_path = Path(temporary) / "actor-families.json"
                report_path.write_text(json.dumps(report), encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "family report does not match"):
                    compile_scene_bundle(
                        ROOT, CLOSURE, report_path, MODEL_IDS, generation,
                        Path(temporary) / "out")

    def test_real_bob_bundle_is_v2_only_complete_and_host_neutral(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            report = compile_scene_bundle(
                ROOT, CLOSURE, FAMILY_REPORT, MODEL_IDS, GENERATION, Path(temporary))
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
                ROOT, CLOSURE, FAMILY_REPORT, MODEL_IDS, GENERATION, output)
            names = tuple(first["publication_order"])
            self.assertEqual(names[-1], first["outputs"]["report"])
            before = {name: (output / name).read_bytes() for name in names}
            with self.assertRaisesRegex(ValueError, "publication target exists"):
                compile_scene_bundle(
                    ROOT, CLOSURE, FAMILY_REPORT, MODEL_IDS, GENERATION, output)
            self.assertEqual(before, {name: (output / name).read_bytes() for name in names})

    def test_publication_validator_rejects_each_missing_or_corrupt_sidecar(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            source = base / "source"
            report = compile_scene_bundle(
                ROOT, CLOSURE, FAMILY_REPORT, MODEL_IDS, GENERATION, source)
            validate_publication(source)
            validate_publication(
                source, root=ROOT, closure_path=CLOSURE,
                family_report_path=FAMILY_REPORT, model_ids_path=MODEL_IDS,
                package_generation=GENERATION)
            with tempfile.TemporaryDirectory(
                    prefix="actor-family-current-test-", dir=ROOT / "build") as stale_dir:
                stale = json.loads(FAMILY_REPORT.read_text(encoding="utf-8"))
                stale["families"][28]["maximum_live_instances"] = 1
                stale_path = Path(stale_dir) / "actor-families.json"
                stale_path.write_text(json.dumps(stale), encoding="utf-8")
                with self.assertRaisesRegex(
                        ValueError, "family report does not match|stale for current"):
                    validate_publication(
                        source, root=ROOT, closure_path=CLOSURE,
                        family_report_path=stale_path, model_ids_path=MODEL_IDS,
                        package_generation=GENERATION)
            for name in report["publication_order"]:
                for mutation in ("missing", "corrupt"):
                    with self.subTest(name=name, mutation=mutation):
                        candidate = base / f"{name}-{mutation}"
                        shutil.copytree(source, candidate)
                        target = candidate / name
                        if mutation == "missing":
                            target.unlink()
                        else:
                            target.write_bytes(target.read_bytes() + b"corrupt")
                        with self.assertRaises(ValueError):
                            validate_publication(candidate)

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
            first = compile_scene_bundle(
                ROOT, CLOSURE, FAMILY_REPORT, MODEL_IDS, GENERATION, out_a)
            second = compile_scene_bundle(
                relocated, relocated / "build/saturn/packages/bob/1/closure.json",
                relocated / "build/saturn/packages/bob/1/actors/actor-families.json",
                relocated / "include/model_ids.h", GENERATION, out_b)
            self.assertEqual(first, second)
            for name in first["publication_order"]:
                self.assertEqual((out_a / name).read_bytes(), (out_b / name).read_bytes())

    def test_package_profile_reuses_canonical_path_and_ownership_validation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            relocated = base / "relocated"
            closure = json.loads(CLOSURE.read_text(encoding="utf-8"))
            profile_relative = "tools/saturn/profiles/sourceboot-bob-demo-v1.json"
            profile = json.loads((ROOT / profile_relative).read_text(encoding="utf-8"))
            relatives = {"build/saturn/packages/bob/1/closure.json",
                         "build/saturn/packages/bob/1/actors/actor-families.json",
                         "build/saturn/packages/bob/1/provisional/scene-package-report.json",
                         "include/model_ids.h", profile_relative}
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
            self.assertEqual(_package_class_bytes(relocated)[0], (
                "route", "input", "camera", "cart", "level", "shared-data",
                "actor", "animation", "audio", "texture"))
            profile_path = relocated / profile_relative
            first_descriptor = profile["package_descriptors"][0]
            descriptor_path = relocated / first_descriptor
            original_descriptor = json.loads(descriptor_path.read_text(encoding="utf-8"))
            for label in (
                "absolute descriptor", "escaping descriptor", "missing descriptor",
                "duplicate descriptor", "case-colliding descriptor", "missing class",
                "absolute payload", "escaping payload", "missing payload",
                "duplicate payload", "case-colliding payload",
            ):
                with self.subTest(label=label):
                    candidate = json.loads(json.dumps(profile))
                    descriptor = json.loads(json.dumps(original_descriptor))
                    if label == "absolute descriptor":
                        candidate["package_descriptors"][0] = str(ROOT / first_descriptor)
                    elif label == "escaping descriptor":
                        candidate["package_descriptors"][0] = "../outside.json"
                    elif label == "missing descriptor":
                        candidate["package_descriptors"][0] = "tools/saturn/manifests/missing.json"
                    elif label == "duplicate descriptor":
                        candidate["package_descriptors"][1] = first_descriptor
                    elif label == "case-colliding descriptor":
                        candidate["package_descriptors"][1] = first_descriptor.upper()
                    elif label == "missing class":
                        candidate["package_descriptors"] = candidate["package_descriptors"][:1]
                    elif label == "absolute payload":
                        descriptor["inputs"][0]["path"] = str(
                            ROOT / descriptor["inputs"][0]["path"])
                    elif label == "escaping payload":
                        descriptor["inputs"][0]["path"] = "../outside.bin"
                    elif label == "missing payload":
                        descriptor["inputs"][0]["path"] = "assets/missing.bin"
                    elif label == "duplicate payload":
                        descriptor["inputs"].append(dict(descriptor["inputs"][0]))
                    elif label == "case-colliding payload":
                        duplicate = dict(descriptor["inputs"][0])
                        duplicate["path"] = duplicate["path"].upper()
                        descriptor["inputs"].append(duplicate)
                    profile_path.write_text(json.dumps(candidate), encoding="utf-8")
                    descriptor_path.write_text(json.dumps(descriptor), encoding="utf-8")
                    with self.assertRaises(ValueError):
                        _package_class_bytes(relocated)


if __name__ == "__main__":
    unittest.main()
