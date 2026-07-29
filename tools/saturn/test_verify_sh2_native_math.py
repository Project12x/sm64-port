#!/usr/bin/env python3
"""Regression tests for the SH-2 native-math census gate."""

from __future__ import annotations

import unittest
from pathlib import Path

from verify_sh2_native_math import (
    CallSite,
    address_batches,
    audit_failures,
    baseline_digest,
    baseline_failures,
    census_rows,
    is_native_math_helper,
    parse_audit_contract,
    parse_baseline,
    parse_route_oracle,
    route_reachable_functions,
    scan_call_graph,
    scan_disassembly,
    SIM_ROUTE_ORACLE_V1_SHA256,
    SIM_AUDIT_CONTRACT_V2_SHA256,
    verify_audit_contract_integrity,
    verify_baseline_integrity,
    verify_route_oracle_integrity,
)


ROUTE_DISASSEMBLY = """
06001000 <_frame_root>:
 6001000: d1 01        mov.l   6001008 <_frame_root+0x8>,r1 ! 06002000 <_per_frame_child>
 6001004: 41 0b        jsr     @r1
06002000 <_per_frame_child>:
 6002000: d1 01        mov.l   6002008 <_per_frame_child+0x8>,r1 ! 06003000 <___addsf3>
 6002004: 41 0b        jsr     @r1
06004000 <_cold_setup>:
 6004000: d2 01        mov.l   6004008 <_cold_setup+0x8>,r2 ! 06005000 <_sinf>
 6004004: 42 0b        jsr     @r2
"""

STACK_SPILL_DISASSEMBLY = """
06006000 <_stack_spill_calls>:
 6006000: d6 01        mov.l   6006008 <_stack_spill_calls+0x8>,r6 ! 06007000 <___fixsfsi>
 6006002: 1f 65        mov.l   r6,@(20,r15)
 6006004: 46 0b        jsr     @r6
 6006006: 60 36        mov     r0,r6
 6006008: 5f 66        mov.l   @(20,r15),r6
 600600a: 46 0b        jsr     @r6
 600600c: 60 36        mov     r0,r6
 600600e: 5f 66        mov.l   @(20,r15),r6
 6006010: 46 0b        jsr     @r6
"""

INDIRECT_ROUTE_DISASSEMBLY = """
06008000 <_frame_root>:
 6008000: d1 01        mov.l   6008008 <_frame_root+0x8>,r1 ! 06008100 <_terrain_worker_run>
 6008004: 41 0b        jsr     @r1
06008100 <_terrain_worker_run>:
 6008100: d1 01        mov.l   6008108 <_terrain_worker_run+0x8>,r1 ! 06008200 <_dual_worker_run>
 6008104: 41 0b        jsr     @r1
06008200 <_dual_worker_run>:
 6008200: 00 09        nop
06008300 <_callback_only>:
 6008300: d1 01        mov.l   6008308 <_callback_only+0x8>,r1 ! 06008400 <___mulsf3>
 6008304: 41 0b        jsr     @r1
"""


class NativeMathCensusTests(unittest.TestCase):
    def test_addr2line_addresses_are_batched_for_windows_command_limits(self) -> None:
        self.assertEqual(list(address_batches([1, 2, 3, 4, 5], 2)), [(1, 2), (3, 4), (5,)])

    def test_attributes_literal_pool_jsr_to_containing_function(self) -> None:
        calls = scan_disassembly(ROUTE_DISASSEMBLY)
        self.assertEqual(
            calls,
            [
                CallSite("_per_frame_child", 0x6002004, "___addsf3"),
                CallSite("_cold_setup", 0x6004004, "_sinf"),
            ],
        )

    def test_attributes_all_helper_calls_after_stack_spill_and_reload(self) -> None:
        self.assertEqual(
            scan_disassembly(STACK_SPILL_DISASSEMBLY),
            [
                CallSite("_stack_spill_calls", 0x6006004, "___fixsfsi"),
                CallSite("_stack_spill_calls", 0x600600A, "___fixsfsi"),
                CallSite("_stack_spill_calls", 0x6006010, "___fixsfsi"),
            ],
        )

    def test_recognizes_complete_linked_softfp_helper_spellings(self) -> None:
        for helper in ("___gesf2", "___lesf2", "___gedf2", "___ledf2", "___powisf2", "_absf"):
            with self.subTest(helper=helper):
                self.assertTrue(is_native_math_helper(helper))

    def test_route_oracle_expands_to_unlisted_per_frame_child(self) -> None:
        oracle = parse_route_oracle("ROUTE_ORACLE_VERSION 1\nROOT _frame_root\n")
        route_functions = route_reachable_functions(scan_call_graph(ROUTE_DISASSEMBLY), oracle.roots)
        self.assertEqual(route_functions, {"_frame_root", "_per_frame_child"})

        rows = census_rows(scan_disassembly(ROUTE_DISASSEMBLY), route_functions)
        self.assertEqual(rows[0].caller, "_cold_setup")
        self.assertEqual(rows[0].heat, "COLD")
        self.assertEqual(rows[1].caller, "_per_frame_child")
        self.assertEqual(rows[1].heat, "HOT")

    def test_unlisted_route_child_helper_is_a_hot_contract_failure(self) -> None:
        oracle = parse_route_oracle("ROUTE_ORACLE_VERSION 1\nROOT _frame_root\n")
        route_functions = route_reachable_functions(scan_call_graph(ROUTE_DISASSEMBLY), oracle.roots)
        baseline = parse_baseline(
            "BASELINE_VERSION 1\nHOT_CEILING 1\nHOT _retired_route ___mulsf3 1\n"
        )
        failures = baseline_failures(scan_disassembly(ROUTE_DISASSEMBLY), route_functions, baseline)
        self.assertEqual(
            failures,
            ["unallowlisted helper in HOT function: _per_frame_child ___addsf3 found 1"],
        )

    def test_checked_indirect_worker_callback_is_hot_and_enforced(self) -> None:
        oracle = parse_route_oracle(
            "ROUTE_ORACLE_VERSION 1\nROOT _frame_root\n"
            "INDIRECT_EDGE _dual_worker_run _callback_only\n"
        )
        route_functions = route_reachable_functions(
            scan_call_graph(INDIRECT_ROUTE_DISASSEMBLY), oracle.roots, oracle.indirect_edges
        )
        self.assertIn("_callback_only", route_functions)
        baseline = parse_baseline("BASELINE_VERSION 1\nHOT_CEILING 0\n")
        self.assertEqual(
            baseline_failures(scan_disassembly(INDIRECT_ROUTE_DISASSEMBLY), route_functions, baseline),
            [
                "unallowlisted helper in HOT function: _callback_only ___mulsf3 found 1",
                "HOT total ceiling 0, found 1",
            ],
        )

    def test_baseline_allows_hot_calls_to_disappear_but_not_grow(self) -> None:
        baseline = parse_baseline(
            "BASELINE_VERSION 1\nHOT_CEILING 2\nHOT _per_frame_child ___addsf3 2\n"
        )
        route_functions = {"_per_frame_child"}
        self.assertEqual(
            baseline_failures([CallSite("_per_frame_child", 1, "___addsf3")], route_functions, baseline),
            [],
        )
        self.assertEqual(
            baseline_failures(
                [
                    CallSite("_per_frame_child", 1, "___addsf3"),
                    CallSite("_per_frame_child", 2, "___addsf3"),
                    CallSite("_per_frame_child", 3, "___addsf3"),
                ],
                route_functions,
                baseline,
            ),
            ["HOT baseline exceeded: _per_frame_child ___addsf3 ceiling 2, found 3", "HOT total ceiling 2, found 3"],
        )

    def test_lowering_baseline_total_and_removing_category_breaks_integrity(self) -> None:
        original = (
            "BASELINE_VERSION 1\nHOT_CEILING 2\n"
            "HOT _per_frame_child ___addsf3 1\nHOT _per_frame_child ___mulsf3 1\n"
        )
        lowered = "BASELINE_VERSION 1\nHOT_CEILING 1\nHOT _per_frame_child ___addsf3 1\n"
        expected_digest = baseline_digest(original)
        verify_baseline_integrity(original, parse_baseline(original), expected_digest=expected_digest)
        with self.assertRaisesRegex(ValueError, "immutable baseline digest mismatch"):
            verify_baseline_integrity(lowered, parse_baseline(lowered), expected_digest=expected_digest)

    def test_checked_in_v1_fixtures_match_their_pinned_digests(self) -> None:
        fixture_dir = Path(__file__).parent
        baseline_text = (fixture_dir / "sh2_native_math_baseline_v1.txt").read_text(encoding="utf-8")
        route_text = (fixture_dir / "sh2_native_math_route_oracle_v1.txt").read_text(encoding="utf-8")
        verify_baseline_integrity(baseline_text, parse_baseline(baseline_text))
        verify_route_oracle_integrity(route_text, parse_route_oracle(route_text))

    def test_checked_in_simulation_audit_fixture_is_pinned(self) -> None:
        fixture_dir = Path(__file__).parent
        audit_text = (fixture_dir / "sh2_native_math_sim_route_oracle_v1.txt").read_text(encoding="utf-8")
        verify_route_oracle_integrity(
            audit_text, parse_route_oracle(audit_text), expected_digest=SIM_ROUTE_ORACLE_V1_SHA256
        )

    def test_checked_in_simulation_audit_contract_is_pinned(self) -> None:
        fixture_dir = Path(__file__).parent
        contract_text = (fixture_dir / "sh2_native_math_sim_audit_contract_v2.txt").read_text(encoding="utf-8")
        verify_audit_contract_integrity(
            contract_text, parse_audit_contract(contract_text), expected_digest=SIM_AUDIT_CONTRACT_V2_SHA256
        )

    def test_audit_fails_when_expected_root_is_missing(self) -> None:
        oracle = parse_route_oracle("ROUTE_ORACLE_VERSION 1\nROOT _wrong_root\n")
        contract = parse_audit_contract(
            "AUDIT_CONTRACT_VERSION 2\nEXPECTED_ROOT _game_loop_one_iteration\n"
            "EXPECTED_TOTAL 1\nFORBIDDEN_CALLER _atan2_lookup\n"
        )
        self.assertIn(
            "audit root missing: expected _game_loop_one_iteration",
            audit_failures([], set(), oracle, contract),
        )

    def test_audit_fails_when_total_differs_from_checked_conversion_baseline(self) -> None:
        oracle = parse_route_oracle("ROUTE_ORACLE_VERSION 1\nROOT _frame_root\n")
        contract = parse_audit_contract(
            "AUDIT_CONTRACT_VERSION 2\nEXPECTED_ROOT _frame_root\n"
            "EXPECTED_TOTAL 2\nFORBIDDEN_CALLER _atan2_lookup\n"
        )
        self.assertEqual(
            audit_failures([CallSite("_frame_root", 1, "___addsf3")], {"_frame_root"}, oracle, contract),
            ["audit total differs from fixed post-conversion baseline 2, found 1"],
        )

    def test_audit_rejects_any_native_math_helper_reintroduced_in_converted_atan2_callers(self) -> None:
        oracle = parse_route_oracle("ROUTE_ORACLE_VERSION 1\nROOT _atan2_lookup\n")
        contract = parse_audit_contract(
            "AUDIT_CONTRACT_VERSION 2\nEXPECTED_ROOT _atan2_lookup\n"
            "EXPECTED_TOTAL 1\nFORBIDDEN_CALLER _atan2_lookup\nFORBIDDEN_CALLER _atan2s\n"
        )
        self.assertEqual(
            audit_failures(
                [CallSite("_atan2_lookup", 1, "___divsf3")],
                {"_atan2_lookup"},
                oracle,
                contract,
            ),
            ["audit forbidden caller uses native math: _atan2_lookup ___divsf3 found 1"],
        )

    def test_renderer_baseline_and_post_conversion_simulation_audit_are_isolated(self) -> None:
        baseline = parse_baseline("BASELINE_VERSION 1\nHOT_CEILING 1\nHOT _frame_root ___addsf3 1\n")
        oracle = parse_route_oracle("ROUTE_ORACLE_VERSION 1\nROOT _candidate\n")
        contract = parse_audit_contract(
            "AUDIT_CONTRACT_VERSION 2\nEXPECTED_ROOT _candidate\n"
            "EXPECTED_TOTAL 0\nFORBIDDEN_CALLER _atan2_lookup\n"
        )
        calls = [CallSite("_frame_root", 1, "___addsf3")]
        self.assertEqual(baseline_failures(calls, {"_frame_root"}, baseline), [])
        self.assertEqual(audit_failures(calls, {"_candidate"}, oracle, contract), [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
