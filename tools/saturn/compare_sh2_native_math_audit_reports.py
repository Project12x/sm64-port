#!/usr/bin/env python3
"""Compare and freeze layout-invariant SH native-math audit observations."""

from __future__ import annotations

import argparse
from hashlib import sha256
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from typing import Any


HEX40 = re.compile(r"[0-9a-f]{40}")
FACT_KEYS = (
    "closure_functions", "direct_call_facts", "helper_call_facts",
    "implementation_transfer_facts", "helper_total",
    "unresolved_indirect_transfers", "unresolved_effects",
)
PARSER_FILES = (
    "tools/saturn/compare_sh2_native_math_audit_reports.py",
    "tools/saturn/test_compare_sh2_native_math_audit_reports.py",
    "tools/saturn/test_verify_sh2_native_math.py",
    "tools/saturn/verify_sh2_native_math.py",
)
OBSERVATION_FILES = (
    "docs/saturn/evidence/reports/task3-native-math-legacy-route0-2026-07-29.json",
    "docs/saturn/evidence/reports/task3-native-math-legacy-route1-2026-07-29.json",
    "docs/saturn/evidence/reports/task3-native-math-corrected-route0-2026-07-29.json",
    "docs/saturn/evidence/reports/task3-native-math-corrected-route1-2026-07-29.json",
)
PROPOSAL_PATH = (
    "docs/saturn/evidence/reports/task3-native-math-pre-repin-proposal-2026-07-29.json"
)
REVIEW_PATH = "docs/saturn/evidence/reports/task3-native-math-parser-review-2026-07-29.json"
CONTRACT_PATH = "tools/saturn/sh2_native_math_sim_audit_contract_v2.txt"
REFERENCE_PATHS = (
    "tools/saturn/sh2_native_math_baseline_v1.txt",
    "tools/saturn/sh2_native_math_route_oracle_v1.txt",
    "tools/saturn/sh2_native_math_sim_route_oracle_v1.txt",
)
REVIEWED_FILES = tuple(sorted((*PARSER_FILES, *OBSERVATION_FILES, PROPOSAL_PATH)))


def digest(path: Path) -> str:
    return sha256(path.read_bytes()).hexdigest()


def safe_relative(path: Path, root: Path) -> str:
    root_resolved = root.resolve()
    path_resolved = path.resolve()
    try:
        relative = path_resolved.relative_to(root_resolved)
    except ValueError as error:
        raise ValueError(f"path escapes repository: {path}") from error
    if path.is_absolute() and not str(path_resolved).startswith(str(root_resolved)):
        raise ValueError(f"absolute path outside repository: {path}")
    return relative.as_posix()


def read_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict) or value.get("schema_version") != 1:
        raise ValueError(f"invalid schema-1 JSON: {path}")
    return value


def _fact_delta(legacy: dict[str, Any], corrected: dict[str, Any], key: str) -> dict[str, list[Any]]:
    old = {json.dumps(x, sort_keys=True) for x in legacy[key]}
    new = {json.dumps(x, sort_keys=True) for x in corrected[key]}
    return {
        "added": [json.loads(x) for x in sorted(new - old)],
        "removed": [json.loads(x) for x in sorted(old - new)],
    }


def compare_reports(
    legacy_route0: dict[str, Any],
    legacy_route1: dict[str, Any],
    corrected_route0: dict[str, Any],
    corrected_route1: dict[str, Any],
    *,
    parser_base_commit: str,
    parser_commit: str,
    producer_worktree_name: str,
    isolation_clean: bool,
    transport_diff_count: int,
    transport_only_present_count: int,
) -> dict[str, Any]:
    if not HEX40.fullmatch(parser_base_commit) or not HEX40.fullmatch(parser_commit):
        raise ValueError("parser commits must be full lowercase 40-hex")
    if not isolation_clean or transport_diff_count or transport_only_present_count:
        raise ValueError("pre-build isolation evidence is not clean")
    expected_name = f"audit-parser-evidence-{parser_commit[:7]}"
    if producer_worktree_name != expected_name:
        raise ValueError(f"producer worktree must be {expected_name}")
    if legacy_route0["analysis_mode"] != "legacy-linear" or legacy_route1["analysis_mode"] != "legacy-linear":
        raise ValueError("legacy inputs have wrong analysis mode")
    if corrected_route0["analysis_mode"] != "code-only" or corrected_route1["analysis_mode"] != "code-only":
        raise ValueError("corrected inputs have wrong analysis mode")
    if corrected_route0["elf_sha256"] == corrected_route1["elf_sha256"]:
        raise ValueError("route ELFs must be distinct")
    for field in (
        "producer_commit", "parser_sha256", "route_oracle_sha256",
        "contract_before_sha256", "contract_before_expected_total", "root",
    ):
        if corrected_route0[field] != corrected_route1[field]:
            raise ValueError(f"corrected layout metadata differs: {field}")
    for key in FACT_KEYS:
        if corrected_route0[key] != corrected_route1[key]:
            raise ValueError(f"corrected layout facts differ: {key}")
    if corrected_route0["unresolved_indirect_transfers"] or corrected_route0["unresolved_effects"]:
        raise ValueError("corrected observations contain unresolved diagnostics")
    return {
        "schema_version": 1,
        "status": "pre-repin-proposal",
        "parser_base_commit": parser_base_commit,
        "parser_commit": parser_commit,
        "reviewed_range": f"{parser_base_commit}..{parser_commit}",
        "legacy_totals": {
            "route0": legacy_route0["helper_total"],
            "route1": legacy_route1["helper_total"],
        },
        "corrected_helper_total": corrected_route0["helper_total"],
        "corrected_closure_functions": corrected_route0["closure_functions"],
        "corrected_direct_call_facts": corrected_route0["direct_call_facts"],
        "corrected_helper_call_facts": corrected_route0["helper_call_facts"],
        "corrected_implementation_transfer_facts": (
            corrected_route0["implementation_transfer_facts"]
        ),
        "delta": {
            "route0": {
                "direct": _fact_delta(legacy_route0, corrected_route0, "direct_call_facts"),
                "helper": _fact_delta(legacy_route0, corrected_route0, "helper_call_facts"),
                "implementation": _fact_delta(
                    legacy_route0, corrected_route0, "implementation_transfer_facts"
                ),
            },
            "route1": {
                "direct": _fact_delta(legacy_route1, corrected_route1, "direct_call_facts"),
                "helper": _fact_delta(legacy_route1, corrected_route1, "helper_call_facts"),
                "implementation": _fact_delta(
                    legacy_route1, corrected_route1, "implementation_transfer_facts"
                ),
            },
        },
        "isolation": {
            "producer_worktree_name": producer_worktree_name,
            "clean_detached_head": True,
            "transport_diff_count": transport_diff_count,
            "transport_only_present_count": transport_only_present_count,
        },
    }


def command_compare(args: argparse.Namespace) -> None:
    root = Path.cwd().resolve()
    paths = {
        "legacy_route0": args.legacy_route0,
        "legacy_route1": args.legacy_route1,
        "corrected_route0": args.corrected_route0,
        "corrected_route1": args.corrected_route1,
        "contract_before": args.contract_before,
    }
    for path in paths.values():
        safe_relative(path, root)
    values = {key: read_json(path) for key, path in paths.items() if key != "contract_before"}
    proposal = compare_reports(
        values["legacy_route0"], values["legacy_route1"],
        values["corrected_route0"], values["corrected_route1"],
        parser_base_commit=args.parser_base_commit, parser_commit=args.parser_commit,
        producer_worktree_name=args.producer_worktree_name,
        isolation_clean=args.isolation_clean_before_build == "true",
        transport_diff_count=args.transport_diff_count,
        transport_only_present_count=args.transport_only_present_count,
    )
    proposal["inputs"] = [
        {"path": safe_relative(path, root), "sha256": digest(path)}
        for _, path in sorted(paths.items())
    ]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(proposal, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def git(*arguments: str) -> str:
    return subprocess.run(["git", *arguments], check=True, text=True,
                          capture_output=True).stdout.strip()


def git_bytes(commit: str, path: str) -> bytes:
    return subprocess.run(
        ["git", "show", f"{commit}:{path}"],
        check=True,
        capture_output=True,
    ).stdout


def bytes_digest(value: bytes) -> str:
    return sha256(value).hexdigest()


def facts_digest(value: Any) -> str:
    return sha256(json.dumps(value, sort_keys=True).encode("utf-8")).hexdigest()


def historical_entry(commit: str, path: str) -> dict[str, str]:
    return {"path": path, "sha256": bytes_digest(git_bytes(commit, path))}


def _expected_review(
    review: dict[str, Any], proposal: dict[str, Any], proposal_path: Path
) -> None:
    if review.get("verdict") != "clean" or review.get("findings") != []:
        raise ValueError("missing or unclean independent review")
    for key in ("parser_base_commit", "parser_commit", "reviewed_range"):
        if review.get(key) != proposal.get(key):
            raise ValueError(f"review/proposal mismatch: {key}")
    if review.get("proposal_sha256") != digest(proposal_path):
        raise ValueError("review proposal hash drift")
    if tuple(sorted(review.get("reviewed_files", []))) != REVIEWED_FILES:
        raise ValueError("reviewed file inventory mismatch")


def command_finalize(args: argparse.Namespace) -> None:
    if not HEX40.fullmatch(args.repin_source_commit):
        raise ValueError("repin source commit must be full lowercase 40-hex")
    if git("rev-parse", "HEAD") != args.repin_source_commit:
        raise ValueError("repin source commit is not current HEAD")
    root = Path.cwd().resolve()
    proposal, review = read_json(args.proposal), read_json(args.review)
    _expected_review(review, proposal, args.proposal)
    input_paths = (
        args.legacy_route0, args.legacy_route1, args.corrected_route0,
        args.corrected_route1, args.proposal, args.review,
    )
    for path in (*input_paths, args.contract_after, args.verifier):
        safe_relative(path, root)
    observations = [read_json(path) for path in input_paths[:4]]
    if any(item.get("producer_commit") != proposal["parser_commit"] for item in observations):
        raise ValueError("observation producer commit mismatch")
    if observations[2]["parser_sha256"] != digest(args.verifier) \
            or observations[3]["parser_sha256"] != digest(args.verifier):
        raise ValueError("verifier pin differs from corrected observations")
    for path in input_paths:
        relative = safe_relative(path, root)
        if bytes_digest(git_bytes(args.repin_source_commit, relative)) != digest(path):
            raise ValueError(f"historical input mismatch: {relative}")
    corrected_total = proposal["corrected_helper_total"]
    contract_text = args.contract_after.read_text(encoding="utf-8")
    match = re.search(r"^EXPECTED_TOTAL\s+(\d+)$", contract_text, re.MULTILINE)
    if not match or int(match.group(1)) != corrected_total:
        raise ValueError("new contract total does not equal corrected total")
    old_total = observations[2]["contract_before_expected_total"]
    old_contract_hash = observations[2]["contract_before_sha256"]
    new_contract_hash = digest(args.contract_after)
    if old_contract_hash == new_contract_hash:
        raise ValueError("old and new contract digests must differ")
    proposal_delta = proposal["delta"]
    route0_elf = observations[2].get("elf_relative_path")
    route1_elf = observations[3].get("elf_relative_path")
    if not isinstance(route0_elf, str) or not isinstance(route1_elf, str):
        raise ValueError("corrected observations lack relative ELF paths")
    for relative in (route0_elf, route1_elf):
        safe_relative(Path(relative), root)
    report = {
        "schema_version": 1, "status": "reviewed-repin-final",
        "repin_source_commit": args.repin_source_commit,
        "legacy": {"route0_total": observations[0]["helper_total"],
                   "route1_total": observations[1]["helper_total"]},
        "corrected": {
            "helper_total": corrected_total,
            "closure_count": len(proposal["corrected_closure_functions"]),
            "direct_call_facts_sha256": facts_digest(
                proposal["corrected_direct_call_facts"]),
            "helper_call_facts_sha256": facts_digest(
                proposal["corrected_helper_call_facts"]),
            "implementation_transfer_facts_sha256": facts_digest(
                proposal["corrected_implementation_transfer_facts"]),
        },
        "delta": proposal_delta,
        "review": {
            "parser_base_commit": review["parser_base_commit"],
            "parser_commit": review["parser_commit"],
            "reviewed_range": review["reviewed_range"],
            "reviewed_files": review["reviewed_files"],
            "proposal_sha256": review["proposal_sha256"],
            "approval_record_sha256": digest(args.review),
            "verdict": review["verdict"],
        },
        "inputs": [
            {"path": safe_relative(path, root), "sha256": digest(path)}
            for path in input_paths
        ],
        "contract": {
            "old_expected_total": old_total, "old_sha256": old_contract_hash,
            "new_expected_total": corrected_total, "new_sha256": new_contract_hash,
        },
        "historical": {
            "parser_verifier": historical_entry(
                args.repin_source_commit, "tools/saturn/verify_sh2_native_math.py"),
            "comparator": historical_entry(
                args.repin_source_commit,
                "tools/saturn/compare_sh2_native_math_audit_reports.py"),
            "tests": [
                historical_entry(args.repin_source_commit, path)
                for path in (
                    "tools/saturn/test_verify_sh2_native_math.py",
                    "tools/saturn/test_compare_sh2_native_math_audit_reports.py",
                )
            ],
            "contract": historical_entry(args.repin_source_commit, CONTRACT_PATH),
            "references": [
                historical_entry(args.repin_source_commit, path)
                for path in REFERENCE_PATHS
            ],
        },
        "artifacts": {
            "route0_elf_relative_path": route0_elf,
            "route0_elf_sha256": observations[2]["elf_sha256"],
            "route1_elf_relative_path": route1_elf,
            "route1_elf_sha256": observations[3]["elf_sha256"],
            "analysis_parser_sha256": observations[2]["parser_sha256"],
        },
    }
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def verify_frozen_repository(
    report: dict[str, Any], report_path: Path, root: Path
) -> tuple[str, str]:
    if report.get("status") != "reviewed-repin-final":
        raise ValueError("not a finalized report")
    source = report.get("repin_source_commit", "")
    if not HEX40.fullmatch(source):
        raise ValueError("invalid historical source commit")
    report_relative = safe_relative(report_path, root)
    introducing = git("log", "--diff-filter=A", "--format=%H", "--", report_relative).splitlines()
    if len(introducing) != 1:
        raise ValueError("final report must have exactly one introducing commit")
    report_commit = introducing[0]
    subprocess.run(["git", "merge-base", "--is-ancestor", source, report_commit], check=True)
    subprocess.run(["git", "merge-base", "--is-ancestor", source, "HEAD"], check=True)
    changed = git("diff", "--name-only", source, report_commit).splitlines()
    if changed != [report_relative]:
        raise ValueError("final report commit range owns unexpected files")
    if git_bytes(report_commit, report_relative) != report_path.read_bytes():
        raise ValueError("current report differs from introduced report")
    for item in report.get("inputs", []):
        path = Path(item["path"])
        relative = safe_relative(path, root)
        if digest(root / relative) != item["sha256"] \
                or bytes_digest(git_bytes(source, relative)) != item["sha256"]:
            raise ValueError(f"input hash mismatch: {path}")
    historical = report["historical"]
    entries = [
        historical["parser_verifier"], historical["comparator"],
        *historical["tests"], historical["contract"], *historical["references"],
    ]
    for item in entries:
        if bytes_digest(git_bytes(source, item["path"])) != item["sha256"]:
            raise ValueError(f"historical git-show hash mismatch: {item['path']}")
    stable = [
        historical["comparator"], *historical["tests"],
        historical["contract"], *historical["references"],
    ]
    for item in stable:
        current = root / item["path"]
        if digest(current) != item["sha256"]:
            raise ValueError(f"current stable compatibility file drift: {item['path']}")
    artifacts = report["artifacts"]
    for route in ("route0", "route1"):
        relative = artifacts[f"{route}_elf_relative_path"]
        safe_relative(Path(relative), root)
        if digest(root / relative) != artifacts[f"{route}_elf_sha256"]:
            raise ValueError(f"{route} ELF hash drift")
    return source, report_commit


def _run_current_verifier(args: argparse.Namespace, report: dict[str, Any]) -> None:
    root = Path.cwd().resolve()
    common = [
        str(args.baseline), "--route-oracle", str(args.route_oracle),
        "--audit-route-oracle", str(args.audit_route_oracle),
        "--audit-contract", str(args.current_v2_contract),
        "--objdump", args.objdump, "--readelf", args.readelf,
        "--addr2line", args.addr2line,
    ]
    current_commit = git("rev-parse", "HEAD")
    frozen = [
        read_json(root / OBSERVATION_FILES[2]),
        read_json(root / OBSERVATION_FILES[3]),
    ]
    with tempfile.TemporaryDirectory() as directory:
        for index, elf in enumerate((args.route0_elf, args.route1_elf)):
            ordinary = [
                sys.executable, str(args.current_verifier), str(elf), *common
            ]
            subprocess.run(ordinary, check=True, cwd=root)
            output = Path(directory) / f"route{index}.json"
            observation = [
                *ordinary, "--audit-observation-only", "--analysis-mode", "code-only",
                "--producer-commit", current_commit, "--json-output", str(output),
            ]
            subprocess.run(observation, check=True, cwd=root)
            current = read_json(output)
            for key in FACT_KEYS:
                if current[key] != frozen[index][key]:
                    raise ValueError(f"current v2 behavioral drift: route{index} {key}")


def command_verify_final(args: argparse.Namespace) -> None:
    root = Path.cwd().resolve()
    report = read_json(args.report)
    verify_frozen_repository(report, args.report, root)
    _run_current_verifier(args, report)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    compare = subparsers.add_parser("compare")
    for option in ("legacy-route0", "legacy-route1", "corrected-route0",
                   "corrected-route1", "contract-before", "output"):
        compare.add_argument(f"--{option}", type=Path, required=True)
    compare.add_argument("--parser-base-commit", required=True)
    compare.add_argument("--parser-commit", required=True)
    compare.add_argument("--producer-worktree-name", required=True)
    compare.add_argument("--isolation-clean-before-build", choices=("true", "false"), required=True)
    compare.add_argument("--transport-diff-count", type=int, required=True)
    compare.add_argument("--transport-only-present-count", type=int, required=True)
    compare.set_defaults(handler=command_compare)
    finalize = subparsers.add_parser("finalize")
    finalize.add_argument("--repin-source-commit", required=True)
    for option in ("proposal", "review", "legacy-route0", "legacy-route1",
                   "corrected-route0", "corrected-route1", "contract-after",
                   "verifier", "output"):
        finalize.add_argument(f"--{option}", type=Path, required=True)
    finalize.set_defaults(handler=command_finalize)
    verify = subparsers.add_parser("verify-final")
    verify.add_argument("--report", type=Path, required=True)
    for option in ("current-verifier", "current-v2-contract", "baseline",
                   "route-oracle", "audit-route-oracle", "route0-elf",
                   "route1-elf", "objdump", "readelf", "addr2line"):
        verify.add_argument(
            f"--{option}",
            type=Path if option not in {"objdump", "readelf", "addr2line"} else str,
            required=True,
        )
    verify.set_defaults(handler=command_verify_final)
    return parser


def main(argv: list[str] | None = None) -> int:
    try:
        args = build_parser().parse_args(argv)
        args.handler(args)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"SH native-math audit report ERROR: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
