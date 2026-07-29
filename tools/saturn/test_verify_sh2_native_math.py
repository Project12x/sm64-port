#!/usr/bin/env python3
"""Regression tests for the SH-2 native-math census gate."""

from __future__ import annotations

import unittest

from verify_sh2_native_math import (
    CallSite,
    allowlist_failures,
    address_batches,
    census_rows,
    is_native_math_helper,
    parse_allowlist,
    scan_disassembly,
)


class NativeMathCensusTests(unittest.TestCase):
    def test_addr2line_addresses_are_batched_for_windows_command_limits(self) -> None:
        self.assertEqual(
            list(address_batches([1, 2, 3, 4, 5], 2)),
            [(1, 2), (3, 4), (5,)],
        )

    def test_attributes_literal_pool_jsr_to_containing_function(self) -> None:
        disassembly = """
06001000 <_frame_tick>:
 6001000: d1 01        mov.l   6001008 <_frame_tick+0x8>,r1 ! 06002000 <___addsf3>
 6001002: 00 09        nop
 6001004: 41 0b        jsr     @r1
 6001006: 00 09        nop
06001008 <_cold_setup>:
 6001008: d2 01        mov.l   6001010 <_cold_setup+0x8>,r2 ! 06003000 <_sinf>
 600100a: 42 0b        jsr     @r2
"""
        calls = scan_disassembly(disassembly)
        self.assertEqual(
            calls,
            [
                CallSite("_frame_tick", 0x6001004, "___addsf3"),
                CallSite("_cold_setup", 0x600100A, "_sinf"),
            ],
        )

    def test_allowlist_requires_exact_hot_counts_and_rejects_stale_rows(self) -> None:
        rules = parse_allowlist(
            "ROUTE_FUNCTION _frame_tick\n"
            "HOT_TOTAL 1\n"
            "HOT _frame_tick ___addsf3 1\n"
            "COLD _cold_setup _sinf 1\n"
        )
        calls = [
            CallSite("_frame_tick", 0x6001004, "___addsf3"),
            CallSite("_cold_setup", 0x600100A, "_sinf"),
        ]
        self.assertEqual(allowlist_failures(calls, rules), [])

        removed_hot_call = [CallSite("_cold_setup", 0x600100A, "_sinf")]
        self.assertEqual(
            allowlist_failures(removed_hot_call, rules),
            [
                "stale allowlist entry: HOT _frame_tick ___addsf3 expected 1, found 0",
                "HOT total expected 1, found 0",
            ],
        )

    def test_recognizes_complete_linked_softfp_helper_spellings(self) -> None:
        for helper in (
            "___gesf2", "___lesf2", "___gedf2", "___ledf2", "___powisf2", "_absf",
        ):
            with self.subTest(helper=helper):
                self.assertTrue(is_native_math_helper(helper))

    def test_route_contract_marks_only_explicit_route_functions_hot(self) -> None:
        rules = parse_allowlist(
            "ROUTE_FUNCTION _tick_camera\n"
            "HOT_TOTAL 2\n"
            "HOT _tick_camera ___addsf3 1\n"
            "HOT _tick_camera ___mulsf3 1\n"
        )
        calls = [
            CallSite("_tick_camera", 0x6001004, "___addsf3"),
            CallSite("_tick_camera", 0x6001006, "___mulsf3"),
            CallSite("_unrelated_handler", 0x6001008, "___addsf3"),
        ]
        self.assertEqual(allowlist_failures(calls, rules), [])
        rows = census_rows(calls, rules)
        self.assertEqual(rows[0].caller, "_tick_camera")
        self.assertEqual(rows[0].heat, "HOT")
        self.assertEqual(rows[0].count, 2)
        self.assertEqual(rows[1].caller, "_unrelated_handler")
        self.assertEqual(rows[1].heat, "COLD")
        self.assertEqual(rows[1].count, 1)

    def test_route_total_fails_when_a_hot_category_is_removed(self) -> None:
        rules = parse_allowlist(
            "ROUTE_FUNCTION _tick_camera\n"
            "HOT_TOTAL 2\n"
            "HOT _tick_camera ___addsf3 1\n"
        )
        calls = [
            CallSite("_tick_camera", 0x6001004, "___addsf3"),
            CallSite("_tick_camera", 0x6001006, "___mulsf3"),
        ]
        self.assertEqual(
            allowlist_failures(calls, rules),
            [
                "unallowlisted helper in HOT function: _tick_camera ___mulsf3 found 1",
                "HOT total expected 2, found 1",
            ],
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
