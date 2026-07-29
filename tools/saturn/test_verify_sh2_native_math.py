#!/usr/bin/env python3
"""Regression tests for the SH-2 native-math census gate."""

from __future__ import annotations

import unittest
from pathlib import Path

from verify_sh2_native_math import (
    CallSite,
    address_batches,
    baseline_digest,
    baseline_failures,
    census_rows,
    is_native_math_helper,
    parse_baseline,
    parse_route_oracle,
    route_reachable_functions,
    scan_call_graph,
    scan_disassembly,
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


if __name__ == "__main__":
    unittest.main(verbosity=2)
