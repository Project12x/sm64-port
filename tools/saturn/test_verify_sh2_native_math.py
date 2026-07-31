#!/usr/bin/env python3
"""Regression tests for the SH-2 native-math census gate."""

from __future__ import annotations

import os
import unittest
from pathlib import Path
from unittest.mock import patch

from verify_sh2_native_math import (
    ConstSet,
    FunctionOwner,
    Interval,
    UNKNOWN,
    analyze_code_only,
    build_instruction_memory,
    build_owner_address_map,
    comparison_refined_states,
    enumerate_computed_targets,
    _write_effect,
    join_value,
    merge_code_analyses,
    parse_decoded_lines,
    parse_instructions,
    parse_readelf_sections,
    parse_readelf_symbols,
    refine_value,
    resolve_function_owners,
    resolve_local_islands,
    uncovered_decoded_line_seeds,
    widen_interval,
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
    source_locations,
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

    def test_absolute_addr2line_gets_sh_tool_environment_without_parent_mutation(self) -> None:
        parent_path = os.environ.get("PATH", "")
        with patch("verify_sh2_native_math.subprocess.run") as run:
            run.return_value.stdout = "_root\nsource.c:1\n"
            locations = source_locations(
                "D:/toolchain/bin/sh-elf-addr2line.exe",
                Path("fixture.elf"),
                [CallSite("_root", 0x6001000, "___addsf3")],
            )
        self.assertEqual(locations, {0x6001000: "source.c:1"})
        child_environment = run.call_args.kwargs["env"]
        self.assertTrue(child_environment["PATH"].startswith(
            r"C:\msys64\usr\bin" + os.pathsep
        ))
        self.assertEqual(os.environ.get("PATH", ""), parent_path)

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


class CodeOnlyAnalysisTests(unittest.TestCase):
    SECTIONS = """
  [ 1] .text PROGBITS 06001000 001000 000400 00 AX 0 0 4
"""
    SYMBOLS = """
   1: 06001000 32 FUNC GLOBAL DEFAULT 1 _root
   2: 06001020 16 FUNC GLOBAL DEFAULT 1 _child
   3: 06001030 0 FUNC LOCAL DEFAULT 1 _zero
   4: 06001030 0 FUNC GLOBAL DEFAULT 1 _zero_alias
   5: 06001040 16 FUNC GLOBAL DEFAULT 1 ___mulsf3
"""

    def analyze(self, disassembly: str, lines: str = ""):
        sections = parse_readelf_sections(self.SECTIONS)
        symbols = parse_readelf_symbols(self.SYMBOLS, sections)
        owners = resolve_function_owners(symbols, sections)
        return analyze_code_only(parse_instructions(disassembly), owners, parse_decoded_lines(lines, owners))

    def analyze_with_islands(self, disassembly: str, symbols_text: str):
        sections = parse_readelf_sections(self.SECTIONS)
        symbols = parse_readelf_symbols(symbols_text, sections)
        owners = resolve_function_owners(symbols, sections)
        islands = resolve_local_islands(symbols, sections, owners)
        return (
            analyze_code_only(
                parse_instructions(disassembly),
                owners,
                local_islands=islands,
            ),
            owners,
            islands,
        )

    def test_pool_bsr_and_pool_clobber_are_not_code(self) -> None:
        dis = """
06001000 <_root>:
 6001000: d8 03 mov.l 6001010 <_root+0x10>,r8 ! 06001040 <___mulsf3>
 6001002: 48 0b jsr @r8
 6001004: 00 09 nop
 6001006: 00 0b rts
 6001008: 00 09 nop
 600100a: b4 a0 bsr 6001020 <_child>
 600100c: 68 ac extu.b r10,r8
 600100e: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
06001040 <___mulsf3>:
 6001040: 00 0b rts
 6001042: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertEqual([(c.caller, c.address, c.helper) for c in result.calls],
                         [("_root", 0x6001002, "___mulsf3")])
        self.assertNotIn(0x600100A, result.code_addresses)
        self.assertNotIn(0x600100C, result.code_addresses)

    def test_real_internal_offset_bsr_and_delay_slot_survive(self) -> None:
        dis = """
06001000 <_root>:
 6001000: b0 0e bsr 6001020 <_child>
 6001002: e8 07 mov #7,r8
 6001004: 00 0b rts
 6001006: 00 09 nop
06001020 <_child>:
 6001020: 00 09 nop
 6001022: 00 0b rts
 6001024: 00 09 nop
"""
        result = self.analyze(dis)
        fact = result.direct_calls[0]
        self.assertEqual((fact.callee, fact.callee_offset), ("_child", 0))
        self.assertIn(0x6001002, result.code_addresses)

    def test_conditional_and_delayed_successors(self) -> None:
        dis = """
06001000 <_root>:
 6001000: 89 02 bt 6001008 <_root+0x8>
 6001002: 8f 03 bf.s 600100c <_root+0xc>
 6001004: e8 01 mov #1,r8
 6001006: a0 03 bra 6001010 <_root+0x10>
 6001008: 00 09 nop
 600100a: 00 0b rts
 600100c: 00 09 nop
 600100e: 00 09 nop
 6001010: 00 0b rts
 6001012: 00 09 nop
"""
        result = self.analyze(dis)
        for address in (0x6001002, 0x6001004, 0x6001006, 0x6001008,
                        0x600100A, 0x600100C, 0x6001010, 0x6001012):
            self.assertIn(address, result.code_addresses)

    def test_disconnected_line_seed_does_not_inherit_register(self) -> None:
        dis = """
06001000 <_root>:
 6001000: d8 03 mov.l 6001010 <_root+0x10>,r8 ! 06001040 <___mulsf3>
 6001002: 00 0b rts
 6001004: 00 09 nop
 6001008: 48 0b jsr @r8
 600100a: 00 09 nop
 600100c: 00 0b rts
 600100e: 00 09 nop
"""
        lines = "x.c 1 0x06001000\nx.c 9 0x06001008\n"
        result = self.analyze(dis, lines)
        self.assertTrue(any(
            x.address == 0x6001008 and x.helper == "<indirect:r8>" for x in result.calls
        ))

    def test_join_domain_and_typed_interval_rules(self) -> None:
        self.assertEqual(join_value(ConstSet("unsigned", frozenset({1})),
                                    ConstSet("unsigned", frozenset({2}))),
                         ConstSet("unsigned", frozenset({1, 2})))
        self.assertIs(join_value(ConstSet("unsigned", frozenset({1})), UNKNOWN), UNKNOWN)
        self.assertIs(join_value(ConstSet("unsigned", frozenset({1})),
                                 ConstSet("signed", frozenset({1}))), UNKNOWN)
        large = join_value(ConstSet("unsigned", frozenset(range(256))),
                           ConstSet("unsigned", frozenset({256})))
        self.assertEqual(large, Interval("unsigned", 0, 256))

    def test_refinement_respects_signedness_and_empty_paths(self) -> None:
        self.assertEqual(refine_value(Interval("unsigned", 0, 9), "unsigned", 3, 6),
                         Interval("unsigned", 3, 6))
        self.assertIsNone(refine_value(ConstSet("signed", frozenset({-2, -1})),
                                       "signed", 0, 4))
        self.assertIs(refine_value(Interval("signed", -2, 2), "unsigned", 0, 1), UNKNOWN)

    def test_symbol_ownership_alias_zero_size_and_overlap(self) -> None:
        sections = parse_readelf_sections(self.SECTIONS)
        owners = resolve_function_owners(parse_readelf_symbols(self.SYMBOLS, sections), sections)
        zero = next(x for x in owners if x.start == 0x6001030)
        self.assertEqual((zero.name, zero.end, zero.aliases), ("_zero_alias", 0x6001040, ("_zero",)))
        bad = self.SYMBOLS + "   6: 06001008 40 FUNC GLOBAL DEFAULT 1 _overlap\n"
        with self.assertRaisesRegex(ValueError, "overlap"):
            resolve_function_owners(parse_readelf_symbols(bad, sections), sections)

    def test_decoded_line_filters_end_and_one_past(self) -> None:
        sections = parse_readelf_sections(self.SECTIONS)
        owners = resolve_function_owners(parse_readelf_symbols(self.SYMBOLS, sections), sections)
        rows = parse_decoded_lines(
            "x.c 1 0x06001002\nx.c - 0x06001020 end_sequence\nx.c 2 0x06001020\n", owners)
        self.assertEqual(rows["_root"], {0x6001002})

    def test_unresolved_jump_fails_closed(self) -> None:
        dis = """
06001000 <_root>:
 6001000: 42 2b jmp @r2
 6001002: 00 09 nop
"""
        self.assertTrue(self.analyze(dis).unresolved_transfers)

    def test_calls_clobber_r0_but_preserve_r8_after_delay_slot(self) -> None:
        dis = """
06001000 <_root>:
 6001000: d0 07 mov.l 6001020 <_child>,r0 ! 06001020 <_child>
 6001002: d8 07 mov.l 6001024 <_child+4>,r8 ! 06001020 <_child>
 6001004: 40 0b jsr @r0
 6001006: 00 09 nop
 6001008: 40 0b jsr @r0
 600100a: 00 09 nop
 600100c: 48 0b jsr @r8
 600100e: 00 09 nop
 6001010: 00 0b rts
 6001012: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertEqual([x.address for x in result.calls],
                         [0x6001004, 0x6001008, 0x600100C])
        self.assertEqual(next(x for x in result.calls if x.address == 0x6001008).helper,
                         "<indirect:r0>")

    def test_unparseable_register_effect_fails_closed(self) -> None:
        dis = """
06001000 <_root>:
 6001000: 12 34 mystery r1,@(r2,r3)
 6001002: 00 0b rts
 6001004: 00 09 nop
"""
        self.assertTrue(self.analyze(dis).unresolved_effects)

    def test_interval_widening_converges_for_both_signednesses(self) -> None:
        for kind, minimum, maximum in (
            ("signed", -0x80000000, 0x7FFFFFFF),
            ("unsigned", 0, 0xFFFFFFFF),
        ):
            first, lower, upper = widen_interval(
                Interval(kind, 2, 4), Interval(kind, 1, 5))
            self.assertEqual(first, Interval(kind, 1, 5))
            second, _, _ = widen_interval(
                first, Interval(kind, 0, 6), lower, upper)
            self.assertEqual(second, Interval(kind, minimum, maximum))

    def test_loop_carried_intervals_reach_a_fixed_point(self) -> None:
        dis = """
06001000 <_root>:
 6001000: e0 00 mov #0,r0
 6001002: c9 01 and #1,r0
 6001004: 70 01 add #1,r0
 6001006: af fd bra 6001004 <_root+0x4>
 6001008: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertEqual(
            result.code_addresses,
            {0x6001000, 0x6001002, 0x6001004, 0x6001006, 0x6001008},
        )

    def test_repeated_seed_analyses_reuse_instruction_memory(self) -> None:
        class CountingInstructions(dict):
            values_calls = 0

            def values(self):
                self.values_calls += 1
                return super().values()

        dis = """
06001000 <_root>:
 6001000: 00 0b rts
 6001002: 00 09 nop
"""
        sections = parse_readelf_sections(self.SECTIONS)
        owners = resolve_function_owners(parse_readelf_symbols(self.SYMBOLS, sections), sections)
        instructions = CountingInstructions(parse_instructions(dis))
        memory = build_instruction_memory(instructions)
        analyze_code_only(instructions, owners, {}, {"_root"}, memory)
        analyze_code_only(instructions, owners, {"_root": {0x6001002}}, {"_root"}, memory)
        self.assertEqual(instructions.values_calls, 1)

    def test_fixed_point_guard_is_applied_per_decoded_line_seed(self) -> None:
        dis = """
06001000 <_root>:
 6001000: 00 09 nop
 6001002: 00 0b rts
 6001004: 00 09 nop
"""
        sections = parse_readelf_sections(self.SECTIONS)
        owners = resolve_function_owners(parse_readelf_symbols(self.SYMBOLS, sections), sections)
        result = analyze_code_only(
            parse_instructions(dis),
            owners,
            {"_root": {0x6001002}},
            {"_root"},
            max_steps_per_seed=2,
        )
        self.assertIn(0x6001000, result.code_addresses)
        self.assertIn(0x6001002, result.code_addresses)

    def test_decoded_seed_suppression_uses_live_code_coverage(self) -> None:
        entry_result_coverage = {0x6001000}
        covered = set(entry_result_coverage)
        seeds = uncovered_decoded_line_seeds(
            {"_root": {0x6001002, 0x6001004}, "_child": {0x6001020}},
            {"_root"},
            covered,
        )
        self.assertEqual(next(seeds), ("_root", 0x6001002))
        replacement_result_coverage = entry_result_coverage | {0x6001002, 0x6001004}
        covered.update(replacement_result_coverage)
        self.assertEqual(list(seeds), [])

    def test_repeated_seed_analyses_reuse_full_owner_map(self) -> None:
        dis = """
06001000 <_root>:
 6001000: b0 0e bsr 6001020 <_child>
 6001002: 00 09 nop
 6001004: 00 0b rts
 6001006: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        sections = parse_readelf_sections(self.SECTIONS)
        owners = resolve_function_owners(parse_readelf_symbols(self.SYMBOLS, sections), sections)
        owner_map = build_owner_address_map(owners)
        instructions = parse_instructions(dis)
        root = next(owner for owner in owners if owner.name == "_root")
        with patch(
            "verify_sh2_native_math.build_owner_address_map",
            side_effect=AssertionError("owner map was rebuilt"),
        ):
            first = analyze_code_only(
                instructions,
                (root,),
                {},
                {"_root"},
                owner_address_map=owner_map,
            )
            second = analyze_code_only(
                instructions,
                (root,),
                {"_root": {0x6001004}},
                {"_root"},
                include_owner_entry=False,
                owner_address_map=owner_map,
            )
        self.assertEqual(first.direct_calls[0].callee, "_child")
        self.assertIn(0x6001004, second.code_addresses)

    def test_flag_only_tst_preserves_modeled_callback_for_jmp(self) -> None:
        dis = """
06001000 <_root>:
 6001000: d2 04 mov.l 6001014 <_root+0x14>,r2 ! 06002000 <_callback_slot>
 6001002: 62 22 mov.l @r2,r2
 6001004: 22 28 tst r2,r2
 6001006: 42 2b jmp @r2
 6001008: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertEqual(result.unresolved_transfers, [])
        self.assertEqual(result.direct_calls[0].callee, "<indirect:_callback_slot*>")

    def test_signed_numeric_movw_literal_resolves_braf(self) -> None:
        dis = """
06001000 <_root>:
 6001000: 90 04 mov.w 600100c <_root+0xc>,r0 ! 0004
 6001002: 00 23 braf r0
 6001004: 00 09 nop
 600100a: 00 0b rts
 600100c: 00 09 nop
"""
        self.assertEqual(self.analyze(dis).unresolved_transfers, [])

    def test_loop_carried_constset_widens_before_cardinality_churn(self) -> None:
        dis = """
06001000 <_root>:
 6001000: e1 00 mov #0,r1
 6001002: 71 01 add #1,r1
 6001004: e2 64 mov #100,r2
 6001006: 31 26 cmp/hi r2,r1
 6001008: 8b fb bf 6001002 <_root+0x2>
 600100a: 00 0b rts
 600100c: 00 09 nop
"""
        profile: dict[str, Counter[str]] = {}
        instructions = parse_instructions(dis)
        owner = FunctionOwner("_root", 0x6001000, 0x600100E, 1)
        analysis = analyze_code_only(
            instructions,
            (owner,),
            max_steps_per_seed=100,
            profile_by_owner=profile,
        )
        self.assertEqual(analysis.unresolved_transfers, [])
        self.assertLess(profile["_root"]["worklist_states"], 40)

    def test_small_shifted_interval_retains_aligned_braf_targets(self) -> None:
        dis = """
06001000 <_root>:
 6001000: c9 03 and #3,r0
 6001002: 40 08 shll2 r0
 6001004: 00 23 braf r0
 6001006: 00 09 nop
 6001008: 00 0b rts
 600100a: 00 09 nop
 600100c: 00 0b rts
 600100e: 00 09 nop
 6001010: 00 0b rts
 6001012: 00 09 nop
 6001014: 00 0b rts
 6001016: 00 09 nop
"""
        self.assertEqual(self.analyze(dis).unresolved_transfers, [])

    def test_extu_b_and_register_add_preserve_exact_braf_offset(self) -> None:
        dis = """
06001000 <_root>:
 6001000: e1 01 mov #1,r1
 6001002: 61 1c extu.b r1,r1
 6001004: 31 1c add r1,r1
 6001006: 01 23 braf r1
 6001008: 00 09 nop
 600100c: 00 0b rts
 600100e: 00 09 nop
"""
        self.assertEqual(self.analyze(dis).unresolved_transfers, [])

    def test_bounded_same_register_add_retains_even_jump_table_stride(self) -> None:
        dis = """
06001000 <_root>:
 6001000: c9 03 and #3,r1
 6001002: 31 1c add r1,r1
 6001004: 01 23 braf r1
 6001006: 00 09 nop
 6001008: 00 0b rts
 600100a: 00 09 nop
 600100c: 00 0b rts
 600100e: 00 09 nop
 6001010: 00 0b rts
 6001012: 00 09 nop
 6001014: 00 0b rts
 6001016: 00 09 nop
"""
        self.assertEqual(self.analyze(dis).unresolved_transfers, [])

    def test_byte_dereference_of_symbol_address_becomes_signed_numeric_range(self) -> None:
        instruction = parse_instructions(" 6001000: 61 90 mov.b @r9,r1\n")[0x6001000]
        state = {
            **{f"r{index}": UNKNOWN for index in range(16)},
            "mach": UNKNOWN,
            "macl": UNKNOWN,
            "t_predicate": UNKNOWN,
        }
        state["r9"] = ConstSet("symbol", frozenset())
        _write_effect(
            instruction,
            state,
            [],
            FunctionOwner("_root", 0x6001000, 0x6001002, 1),
        )
        self.assertEqual(state["r1"], Interval("signed", -0x80, 0x7F))

    def test_cmp_hi_fallthrough_refines_unknown_jump_table_index(self) -> None:
        dis = """
06001000 <_root>:
 6001000: e2 03 mov #3,r2
 6001002: 31 26 cmp/hi r2,r1
 6001004: 89 0a bt 600101c <_root+0x1c>
 6001006: c7 13 mova 6001030 <_root+0x30>,r0
 6001008: 01 1e mov.b @(r0,r1),r1
 600100a: 01 23 braf r1
 600100c: 00 09 nop
 6001010: 00 09 nop
 6001014: 00 0b rts
 6001016: 00 0b rts
 6001018: 00 0b rts
 600101a: 00 0b rts
 600101c: 00 0b rts
 6001030: 04 06 .word 0x0406
 6001032: 08 0a .word 0x080a
"""
        self.assertEqual(self.analyze(dis).unresolved_transfers, [])

    def test_cmp_hi_splits_unknown_into_conservative_unsigned_ranges(self) -> None:
        compare = parse_instructions(
            " 6001000: 31 26 cmp/hi r2,r1\n"
        )[0x6001000]
        true_state, false_state = comparison_refined_states(
            compare,
            {"r1": UNKNOWN, "r2": ConstSet("unsigned", frozenset({3}))},
        )
        self.assertEqual(true_state["r1"], Interval("unsigned", 4, 0xFFFFFFFF))
        self.assertEqual(false_state["r1"], Interval("unsigned", 0, 3))

    def test_cmp_pz_discards_contradictory_outcome(self) -> None:
        compare = parse_instructions(
            " 6001000: 41 11 cmp/pz r1\n"
        )[0x6001000]
        true_state, false_state = comparison_refined_states(
            compare,
            {"r1": Interval("signed", -4, -1)},
        )
        self.assertIsNone(true_state)
        self.assertEqual(false_state["r1"], Interval("signed", -4, -1))

    def test_unsigned_predicate_intersects_proven_nonnegative_signed_range(self) -> None:
        compare = parse_instructions(
            " 6001000: 31 26 cmp/hi r2,r1\n"
        )[0x6001000]
        true_state, false_state = comparison_refined_states(
            compare,
            {
                "r1": Interval("signed", 0, 30),
                "r2": ConstSet("signed", frozenset({30})),
            },
        )
        self.assertIsNone(true_state)
        self.assertEqual(false_state["r1"], Interval("signed", 0, 30))

    def test_opposite_signed_predicate_does_not_reinterpret_negative_range(self) -> None:
        compare = parse_instructions(
            " 6001000: 31 26 cmp/hi r2,r1\n"
        )[0x6001000]
        true_state, false_state = comparison_refined_states(
            compare,
            {
                "r1": Interval("signed", -4, 3),
                "r2": ConstSet("signed", frozenset({3})),
            },
        )
        self.assertIs(true_state["r1"], UNKNOWN)
        self.assertEqual(false_state["r1"], Interval("signed", 0, 3))

    def test_unsigned_cmp_hi_fallthrough_filters_signed_byte_machine_values(self) -> None:
        compare = parse_instructions(
            " 6001000: 31 26 cmp/hi r2,r1\n"
        )[0x6001000]
        true_state, false_state = comparison_refined_states(
            compare,
            {
                "r1": Interval("signed", -0x80, 0x7F),
                "r2": ConstSet("signed", frozenset({3})),
            },
        )
        self.assertIs(true_state["r1"], UNKNOWN)
        self.assertEqual(false_state["r1"], Interval("signed", 0, 3))

    def test_bf_s_uses_false_cmp_pz_state_and_executes_delay_slot(self) -> None:
        dis = """
06001000 <_root>:
 6001000: e1 ff mov #-1,r1
 6001002: 41 11 cmp/pz r1
 6001004: 8f 02 bf.s 600100c <_root+0xc>
 6001006: e8 07 mov #7,r8
 6001008: 00 0b rts
 600100a: 00 09 nop
 600100c: 00 0b rts
 600100e: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertIn(0x6001006, result.code_addresses)
        self.assertIn(0x600100C, result.code_addresses)
        self.assertNotIn(0x6001008, result.code_addresses)

    def test_invented_slash_delayed_branch_spelling_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "unsupported delayed-branch spelling"):
            parse_instructions(" 6001000: 8f 02 bf/s 6001008 <_root+0x8>\n")

    def test_cmp_predicate_survives_flag_neutral_add_before_bt_s(self) -> None:
        dis = """
06001000 <_root>:
 6001000: e1 03 mov #3,r1
 6001002: e2 02 mov #2,r2
 6001004: 32 16 cmp/hi r1,r2
 6001006: 73 01 add #1,r3
 6001008: 8d 02 bt.s 6001010 <_root+0x10>
 600100a: 00 09 nop
 600100c: 00 0b rts
 600100e: 00 09 nop
 6001010: 00 0b rts
 6001012: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertIn(0x600100C, result.code_addresses)
        self.assertNotIn(0x6001010, result.code_addresses)

    def test_disconnected_seed_stops_at_entry_covered_merge(self) -> None:
        dis = """
06001000 <_root>:
 6001000: e2 03 mov #3,r2
 6001002: 31 26 cmp/hi r2,r1
 6001004: 89 0a bt 600101c <_root+0x1c>
 6001006: c7 13 mova 6001030 <_root+0x30>,r0
 6001008: 01 1e mov.b @(r0,r1),r1
 600100a: 01 23 braf r1
 600100c: 00 09 nop
 6001010: 00 09 nop
 6001014: 00 0b rts
 6001016: 00 0b rts
 6001018: 00 0b rts
 600101a: 00 0b rts
 600101c: 00 0b rts
 6001030: 04 06 .word 0x0406
 6001032: 08 0a .word 0x080a
"""
        sections = parse_readelf_sections(self.SECTIONS)
        owners = resolve_function_owners(parse_readelf_symbols(self.SYMBOLS, sections), sections)
        instructions = parse_instructions(dis)
        entry = analyze_code_only(instructions, owners, {}, {"_root"})
        extra = analyze_code_only(
            instructions,
            owners,
            {"_root": {0x6001006}},
            {"_root"},
            include_owner_entry=False,
            stop_at_addresses=entry.code_addresses,
        )
        merged = merge_code_analyses(entry, extra)
        self.assertEqual(merged.unresolved_transfers, [])

    def test_local_notype_div0_island_preserves_eight_exact_bsr_sites(self) -> None:
        symbols = """
   1: 06001000 64 FUNC GLOBAL DEFAULT 1 _root
   2: 060010e0 36 FUNC GLOBAL DEFAULT 1 ___sdivsi3
   3: 06001100 0 NOTYPE LOCAL DEFAULT 1 div0
   4: 06001120 32 FUNC GLOBAL HIDDEN 1 ___udivsi3
"""
        sites = [
            (0x6001000, 0x6001106),
            (0x6001004, 0x6001108),
            (0x6001008, 0x6001106),
            (0x600100C, 0x6001108),
            (0x6001010, 0x6001118),
            (0x6001014, 0x6001118),
            (0x6001018, 0x6001118),
            (0x600101C, 0x6001118),
        ]
        rows = ["06001000 <_root>:"]
        for address, target in sites:
            rows.append(
                f" {address:x}: b0 00 bsr {target:x} "
                f"<div0+0x{target - 0x6001100:x}>"
            )
            rows.append(f" {address + 2:x}: 00 09 nop")
        rows += [
            " 6001020: 00 0b rts",
            " 6001022: 00 09 nop",
            "06001100 <div0>:",
            " 6001106: 00 0b rts",
            " 6001108: 00 09 nop",
            " 6001118: 00 0b rts",
            " 600111a: 00 09 nop",
            "06001120 <___udivsi3>:",
            " 6001120: 00 0b rts",
            " 6001122: 00 09 nop",
        ]
        analysis, _, islands = self.analyze_with_islands("\n".join(rows), symbols)
        self.assertEqual([(x.name, x.start, x.end) for x in islands],
                         [("div0", 0x6001100, 0x6001120)])
        self.assertEqual(islands[0].code_start, 0x6001104)
        self.assertEqual(len(analysis.implementation_transfers), 8)
        self.assertEqual(
            sorted({x.target_offset for x in analysis.implementation_transfers}),
            [6, 8, 0x18],
        )
        self.assertFalse(any(call.helper == "div0" for call in analysis.calls))

    def test_island_helper_call_is_attributed_to_origin_and_island_site(self) -> None:
        symbols = """
   1: 06001000 32 FUNC GLOBAL DEFAULT 1 _root
   2: 06001100 0 NOTYPE LOCAL DEFAULT 1 island_a
   3: 06001120 16 FUNC GLOBAL DEFAULT 1 ___mulsf3
"""
        dis = """
06001000 <_root>:
 6001000: b0 81 bsr 6001106 <island_a+0x6>
 6001002: 00 09 nop
 6001004: 00 0b rts
 6001006: 00 09 nop
06001100 <island_a>:
 6001106: d8 03 mov.l 6001114 <island_a+0x14>,r8 ! 06001120 <___mulsf3>
 6001108: 48 0b jsr @r8
 600110a: 00 09 nop
 600110c: 00 0b rts
 600110e: 00 09 nop
06001120 <___mulsf3>:
 6001120: 00 0b rts
 6001122: 00 09 nop
"""
        analysis, _, _ = self.analyze_with_islands(dis, symbols)
        helper = next(x for x in analysis.direct_calls if x.callee == "___mulsf3")
        self.assertEqual(
            (helper.caller, helper.caller_region, helper.caller_island,
             helper.caller_offset, helper.callee_offset),
            ("_root", "island", "island_a", 8, 0),
        )

    def test_two_island_calls_keep_both_continuations_and_do_not_leak_rts_state(self) -> None:
        symbols = """
   1: 06001000 64 FUNC GLOBAL DEFAULT 1 _root
   2: 06001100 0 NOTYPE LOCAL DEFAULT 1 island_a
   3: 06001120 16 FUNC GLOBAL DEFAULT 1 _child
"""
        dis = """
06001000 <_root>:
 6001000: d8 0f mov.l 6001040 <_root+0x40>,r8 ! 06001120 <_child>
 6001002: b0 7d bsr 6001100 <island_a>
 6001004: 00 09 nop
 6001006: 48 0b jsr @r8
 6001008: 00 09 nop
 600100a: b0 79 bsr 6001100 <island_a>
 600100c: 00 09 nop
 600100e: 48 0b jsr @r8
 6001010: 00 09 nop
 6001012: 00 0b rts
 6001014: 00 09 nop
06001100 <island_a>:
 6001100: e8 00 mov #0,r8
 6001102: 00 0b rts
 6001104: e8 01 mov #1,r8
06001120 <_child>:
 6001120: 00 0b rts
 6001122: 00 09 nop
"""
        analysis, _, _ = self.analyze_with_islands(dis, symbols)
        self.assertEqual(
            [call.address for call in analysis.calls if call.helper == "_child"],
            [0x6001006, 0x600100E],
        )
        self.assertIn(0x6001006, analysis.code_addresses)
        self.assertIn(0x600100E, analysis.code_addresses)
        self.assertNotIn(0x6001106, analysis.code_addresses)

    def test_nested_and_cyclic_islands_terminate_with_each_continuation(self) -> None:
        symbols = """
   1: 06001000 32 FUNC GLOBAL DEFAULT 1 _root
   2: 06001100 0 NOTYPE LOCAL DEFAULT 1 island_a
   3: 06001120 0 NOTYPE LOCAL DEFAULT 1 island_b
   4: 06001140 16 FUNC GLOBAL DEFAULT 1 _child
"""
        dis = """
06001000 <_root>:
 6001000: b0 7e bsr 6001100 <island_a>
 6001002: 00 09 nop
 6001004: 00 0b rts
 6001006: 00 09 nop
06001100 <island_a>:
 6001100: b0 0e bsr 6001120 <island_b>
 6001102: 00 09 nop
 6001104: 00 0b rts
 6001106: 00 09 nop
06001120 <island_b>:
 6001120: bf ee bsr 6001100 <island_a>
 6001122: 00 09 nop
 6001124: 00 0b rts
 6001126: 00 09 nop
06001140 <_child>:
 6001140: 00 0b rts
 6001142: 00 09 nop
"""
        analysis, _, _ = self.analyze_with_islands(dis, symbols)
        facts = {
            (x.caller_region, x.caller_island, x.caller_offset, x.target_island)
            for x in analysis.implementation_transfers
        }
        self.assertEqual(
            facts,
            {
                ("owner", None, 0, "island_a"),
                ("island", "island_a", 0, "island_b"),
                ("island", "island_b", 0, "island_a"),
            },
        )
        self.assertTrue({0x6001004, 0x6001104, 0x6001124} <= analysis.code_addresses)

    def test_local_island_candidate_exclusions_and_exact_dot_l_annotation(self) -> None:
        symbols = """
   1: 06001000 32 FUNC GLOBAL DEFAULT 1 _root
   2: 06001100 0 NOTYPE GLOBAL DEFAULT 1 global_label
   3: 06001120 0 OBJECT LOCAL DEFAULT 1 object_label
   4: 06001140 0 NOTYPE LOCAL DEFAULT 1 .Lgood
   5: 06001160 16 FUNC GLOBAL DEFAULT 1 _child
   6: 00000000 0 NOTYPE LOCAL DEFAULT UND undefined_label
   7: 00000001 0 NOTYPE LOCAL DEFAULT ABS absolute_label
"""
        sections = parse_readelf_sections(self.SECTIONS)
        parsed = parse_readelf_symbols(symbols, sections)
        owners = resolve_function_owners(parsed, sections)
        islands = resolve_local_islands(parsed, sections, owners)
        self.assertEqual([item.name for item in islands], [".Lgood"])
        bad_dis = """
06001000 <_root>:
 6001000: b0 9e bsr 6001140 <.Lwrong>
 6001002: 00 09 nop
"""
        bad = analyze_code_only(
            parse_instructions(bad_dis), owners, local_islands=islands
        )
        self.assertTrue(bad.unresolved_transfers)

    def test_local_island_with_no_halfwords_after_owner_exclusion_is_rejected(self) -> None:
        symbols = """
   1: 06001000 64 FUNC GLOBAL DEFAULT 1 _root
   2: 06001010 0 NOTYPE LOCAL DEFAULT 1 buried
   3: 06001020 0 NOTYPE GLOBAL DEFAULT 1 boundary
   4: 06001100 16 FUNC GLOBAL DEFAULT 1 _child
"""
        sections = parse_readelf_sections(self.SECTIONS)
        parsed = parse_readelf_symbols(symbols, sections)
        owners = resolve_function_owners(parsed, sections)
        self.assertEqual(resolve_local_islands(parsed, sections, owners), ())

    def test_island_literal_pool_rows_are_not_code_without_cfg_reachability(self) -> None:
        symbols = """
   1: 06001000 32 FUNC GLOBAL DEFAULT 1 _root
   2: 06001100 0 NOTYPE LOCAL DEFAULT 1 island_a
   3: 06001120 16 FUNC GLOBAL DEFAULT 1 _child
"""
        dis = """
06001000 <_root>:
 6001000: b0 7e bsr 6001100 <island_a>
 6001002: 00 09 nop
 6001004: 00 0b rts
 6001006: 00 09 nop
06001100 <island_a>:
 6001100: a0 04 bra 600110c <island_a+0xc>
 6001102: 00 09 nop
 6001104: b0 0c bsr 6001120 <_child>
 6001106: 00 09 nop
 600110c: 00 0b rts
 600110e: 00 09 nop
06001120 <_child>:
 6001120: 00 0b rts
 6001122: 00 09 nop
"""
        analysis, _, _ = self.analyze_with_islands(dis, symbols)
        self.assertNotIn(0x6001104, analysis.code_addresses)
        self.assertFalse(any(call.helper == "_child" for call in analysis.calls))

    def test_computed_target_gate_accepts_256_and_rejects_257_or_unowned(self) -> None:
        owner = FunctionOwner("_table", 0x6001000, 0x6001400, 1)
        good = ConstSet("unsigned", frozenset(range(0x6001000, 0x6001200, 2)))
        self.assertEqual(len(enumerate_computed_targets(good, (owner,))), 256)
        too_many = ConstSet("unsigned", frozenset(range(0x6001000, 0x6001202, 2)))
        self.assertIsNone(enumerate_computed_targets(too_many, (owner,)))
        self.assertIsNone(enumerate_computed_targets(
            ConstSet("unsigned", frozenset({0x6001001})), (owner,)))

    def test_same_start_different_end_alias_is_rejected(self) -> None:
        sections = parse_readelf_sections(self.SECTIONS)
        symbols = self.SYMBOLS + "   6: 06001020 4 FUNC WEAK DEFAULT 1 _bad_alias\n"
        with self.assertRaisesRegex(ValueError, "different ends"):
            resolve_function_owners(parse_readelf_symbols(symbols, sections), sections)

    def test_absent_line_table_entry_cfg_passes_when_resolved(self) -> None:
        dis = """
06001000 <_root>:
 6001000: 00 0b rts
 6001002: 00 09 nop
"""
        result = self.analyze(dis, "")
        self.assertFalse(result.unresolved_transfers)
        self.assertEqual(result.code_addresses, {0x6001000, 0x6001002})


if __name__ == "__main__":
    unittest.main(verbosity=2)
