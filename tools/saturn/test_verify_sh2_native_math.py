#!/usr/bin/env python3
"""Regression tests for the SH-2 native-math census gate."""

from __future__ import annotations

import unittest

from verify_sh2_native_math import (
    CallSite,
    allowlist_failures,
    address_batches,
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
            "HOT _frame_tick ___addsf3 1\nCOLD _cold_setup _sinf 1\n"
        )
        calls = [
            CallSite("_frame_tick", 0x6001004, "___addsf3"),
            CallSite("_cold_setup", 0x600100A, "_sinf"),
        ]
        self.assertEqual(allowlist_failures(calls, rules), [])

        removed_hot_call = [CallSite("_cold_setup", 0x600100A, "_sinf")]
        self.assertEqual(
            allowlist_failures(removed_hot_call, rules),
            ["stale allowlist entry: HOT _frame_tick ___addsf3 expected 1, found 0"],
        )

    def test_hot_source_row_marks_each_function_and_requires_its_total(self) -> None:
        rules = parse_allowlist("HOT_SOURCE src/game/camera.c 2\n")
        calls = [
            CallSite("_tick_camera", 0x6001004, "___addsf3"),
            CallSite("_tick_camera", 0x6001006, "___mulsf3"),
        ]
        locations = {
            0x6001004: "/worktree/src/game/camera.c:20",
            0x6001006: "/worktree/src/game/camera.c:21",
        }
        self.assertEqual(allowlist_failures(calls, rules, locations), [])

        directory_rules = parse_allowlist("HOT_SOURCE src/port/saturn/ 2\n")
        port_locations = {
            0x6001004: "/worktree/src/port/saturn/gfx/demo.c:20",
            0x6001006: "/worktree/src/port/saturn/gfx/demo.c:21",
        }
        self.assertEqual(allowlist_failures(calls, directory_rules, port_locations), [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
