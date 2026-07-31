#!/usr/bin/env python3
from __future__ import annotations

from hashlib import sha256
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from compare_sh2_native_math_audit_reports import (
    CONTRACT_PATH,
    OBSERVATION_FILES,
    PARSER_FILES,
    PROPOSAL_PATH,
    REFERENCE_PATHS,
    REVIEW_PATH,
    compare_reports,
    safe_relative,
    verify_frozen_repository,
)


class AuditReportComparatorTests(unittest.TestCase):
    def git(self, root: Path, *args: str, input_text: str | None = None) -> str:
        return subprocess.run(
            ["git", *args], cwd=root, input=input_text, text=True,
            check=True, capture_output=True,
        ).stdout.strip()

    def repository_fixture(self, root: Path) -> tuple[dict, Path]:
        self.git(root, "init", "-q")
        self.git(root, "config", "user.email", "test@example.invalid")
        self.git(root, "config", "user.name", "Test")
        self.git(root, "config", "core.autocrlf", "false")
        source_paths = {
            *PARSER_FILES, *OBSERVATION_FILES, PROPOSAL_PATH, REVIEW_PATH,
            CONTRACT_PATH, *REFERENCE_PATHS,
        }
        for relative in source_paths:
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(f"fixture:{relative}\n", encoding="utf-8")
        for relative in ("build/route0.elf", "build/route1.elf"):
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(relative.encode("ascii"))
        self.git(root, "add", ".")
        self.git(root, "commit", "-qm", "source")
        source = self.git(root, "rev-parse", "HEAD")

        def entry(relative: str) -> dict[str, str]:
            value = (root / relative).read_bytes()
            return {"path": relative, "sha256": sha256(value).hexdigest()}

        input_paths = (*OBSERVATION_FILES, PROPOSAL_PATH, REVIEW_PATH)
        report = {
            "schema_version": 1,
            "status": "reviewed-repin-final",
            "repin_source_commit": source,
            "inputs": [entry(path) for path in input_paths],
            "historical": {
                "parser_verifier": entry("tools/saturn/verify_sh2_native_math.py"),
                "comparator": entry(
                    "tools/saturn/compare_sh2_native_math_audit_reports.py"),
                "tests": [
                    entry("tools/saturn/test_verify_sh2_native_math.py"),
                    entry("tools/saturn/test_compare_sh2_native_math_audit_reports.py"),
                ],
                "contract": entry(CONTRACT_PATH),
                "references": [entry(path) for path in REFERENCE_PATHS],
            },
            "artifacts": {
                "route0_elf_relative_path": "build/route0.elf",
                "route0_elf_sha256": sha256(
                    (root / "build/route0.elf").read_bytes()).hexdigest(),
                "route1_elf_relative_path": "build/route1.elf",
                "route1_elf_sha256": sha256(
                    (root / "build/route1.elf").read_bytes()).hexdigest(),
            },
        }
        report_path = root / "docs/saturn/evidence/reports/final.json"
        report_path.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        self.git(root, "add", report_path.relative_to(root).as_posix())
        self.git(root, "commit", "-qm", "report")
        return report, report_path

    def observation(self, mode: str, elf_hash: str, total: int) -> dict:
        return {
            "schema_version": 1, "analysis_mode": mode,
            "producer_commit": "b" * 40, "parser_sha256": "1" * 64,
            "elf_sha256": elf_hash, "route_oracle_sha256": "2" * 64,
            "contract_before_sha256": "3" * 64,
            "contract_before_expected_total": 582,
            "root": "_root", "closure_functions": ["_root"],
            "direct_call_facts": [
                {"caller": "_root", "caller_region": "owner", "caller_island": None,
                 "caller_offset": 2, "callee": "___mulsf3",
                 "callee_offset": 0, "count": total}
            ],
            "helper_call_facts": [
                {"caller": "_root", "caller_region": "owner", "caller_island": None,
                 "caller_offset": 2, "helper": "___mulsf3",
                 "helper_offset": 0, "count": total}
            ],
            "implementation_transfer_facts": [],
            "helper_total": total, "unresolved_indirect_transfers": [],
            "unresolved_effects": [],
        }

    def test_compare_requires_equal_corrected_facts_and_distinct_elfs(self) -> None:
        legacy0 = self.observation("legacy-linear", "a" * 64, 582)
        legacy1 = self.observation("legacy-linear", "b" * 64, 3686)
        corrected0 = self.observation("code-only", "a" * 64, 7)
        corrected1 = self.observation("code-only", "b" * 64, 7)
        proposal = compare_reports(
            legacy0, legacy1, corrected0, corrected1,
            parser_base_commit="a" * 40, parser_commit="b" * 40,
            producer_worktree_name="audit-parser-evidence-bbbbbbb",
            isolation_clean=True, transport_diff_count=0,
            transport_only_present_count=0,
        )
        self.assertEqual(proposal["corrected_helper_total"], 7)
        self.assertEqual(proposal["legacy_totals"], {"route0": 582, "route1": 3686})
        corrected1["helper_total"] = 8
        with self.assertRaisesRegex(ValueError, "corrected layout facts differ"):
            compare_reports(
                legacy0, legacy1, corrected0, corrected1,
                parser_base_commit="a" * 40, parser_commit="b" * 40,
                producer_worktree_name="audit-parser-evidence-bbbbbbb",
                isolation_clean=True, transport_diff_count=0,
                transport_only_present_count=0,
            )

    def test_isolation_and_worktree_name_are_enforced(self) -> None:
        rows = [self.observation("legacy-linear", "a" * 64, 1),
                self.observation("legacy-linear", "b" * 64, 2),
                self.observation("code-only", "a" * 64, 3),
                self.observation("code-only", "b" * 64, 3)]
        with self.assertRaisesRegex(ValueError, "isolation"):
            compare_reports(*rows, parser_base_commit="a" * 40,
                            parser_commit="b" * 40,
                            producer_worktree_name="audit-parser-evidence-bbbbbbb",
                            isolation_clean=False, transport_diff_count=0,
                            transport_only_present_count=0)
        with self.assertRaisesRegex(ValueError, "worktree"):
            compare_reports(*rows, parser_base_commit="a" * 40,
                            parser_commit="b" * 40,
                            producer_worktree_name="wrong",
                            isolation_clean=True, transport_diff_count=0,
                            transport_only_present_count=0)

    def test_paths_must_be_repository_relative(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.assertEqual(safe_relative(root / "a.json", root), "a.json")
            with self.assertRaises(ValueError):
                safe_relative(Path("C:/outside.json"), root)
            with self.assertRaises(ValueError):
                safe_relative(root / ".." / "outside.json", root)

    def test_frozen_history_allows_only_current_verifier_to_advance(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report, report_path = self.repository_fixture(root)
            verifier = root / "tools/saturn/verify_sh2_native_math.py"
            verifier.write_text("post-v3 verifier bytes\n", encoding="utf-8")
            self.git(root, "add", verifier.relative_to(root).as_posix())
            self.git(root, "commit", "-qm", "advance verifier")
            previous = Path.cwd()
            os.chdir(root)
            try:
                source, report_commit = verify_frozen_repository(
                    report, report_path, root)
            finally:
                os.chdir(previous)
            self.assertEqual(source, report["repin_source_commit"])
            self.assertNotEqual(report_commit, self.git(root, "rev-parse", "HEAD"))

    def test_frozen_history_rejects_historical_hash_substitution(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report, report_path = self.repository_fixture(root)
            report["historical"]["comparator"]["sha256"] = "0" * 64
            previous = Path.cwd()
            os.chdir(root)
            try:
                with self.assertRaisesRegex(ValueError, "historical git-show"):
                    verify_frozen_repository(report, report_path, root)
            finally:
                os.chdir(previous)

    def test_frozen_history_rejects_nonancestor_source(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report, report_path = self.repository_fixture(root)
            tree = self.git(root, "rev-parse", "HEAD^{tree}")
            report["repin_source_commit"] = self.git(
                root, "commit-tree", tree, input_text="unrelated\n")
            previous = Path.cwd()
            os.chdir(root)
            try:
                with self.assertRaises(subprocess.CalledProcessError):
                    verify_frozen_repository(report, report_path, root)
            finally:
                os.chdir(previous)

    def test_frozen_history_rejects_current_v2_compatibility_drift(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report, report_path = self.repository_fixture(root)
            (root / CONTRACT_PATH).write_text("behavior drift\n", encoding="utf-8")
            previous = Path.cwd()
            os.chdir(root)
            try:
                with self.assertRaisesRegex(ValueError, "stable compatibility"):
                    verify_frozen_repository(report, report_path, root)
            finally:
                os.chdir(previous)


if __name__ == "__main__":
    unittest.main(verbosity=2)
