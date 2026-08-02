#!/usr/bin/env python3
"""Regression tests for the SH-2 native-math census gate."""

from __future__ import annotations

import os
import re
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from verify_sh2_native_math import (
    ConstSet,
    DirectCallFact,
    FunctionOwner,
    Interval,
    MAYBE_STACK_PTR,
    StackMemory,
    StackPtr,
    StackSlot,
    SymbolAtom,
    UNKNOWN,
    UnresolvedTransfer,
    _unknown_state,
    abstract_value_json,
    analyze_code_only,
    build_instruction_memory,
    build_owner_address_map,
    comparison_refined_states,
    enumerate_computed_targets,
    _write_effect,
    join_value,
    parse_decoded_lines,
    parse_instructions,
    parse_readelf_sections,
    parse_readelf_symbols,
    refine_value,
    resolve_function_owners,
    resolve_local_islands,
    widen_interval,
    CallSite,
    address_batches,
    audit_indirect_edges,
    audit_failures,
    baseline_digest,
    baseline_failures,
    census_rows,
    is_native_math_helper,
    make_observation,
    parse_audit_contract,
    parse_baseline,
    parse_route_oracle,
    prove_sourceboot_null_task_submit,
    sourceboot_null_task_submit_dead_nodes,
    route_reachable_functions,
    run_command,
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
    @staticmethod
    def indirect_owner(name: str, start: int) -> FunctionOwner:
        return FunctionOwner(name, start, start + 0x20, 1)

    def test_declared_graph_node_callback_extends_closure_and_covers_transfer(self) -> None:
        oracle = parse_route_oracle(
            "ROUTE_ORACLE_VERSION 1\nROOT _root\n"
            "INDIRECT_EDGE _geo_process_node_and_siblings _geo_camera_main\n"
        )
        transfer = UnresolvedTransfer(
            "_geo_process_node_and_siblings", 0x6001010, "jsr", "r3"
        )
        result = audit_indirect_edges(
            {"_root": {"_geo_process_node_and_siblings"}}, oracle,
            (
                self.indirect_owner("_root", 0x6001000),
                self.indirect_owner("_geo_process_node_and_siblings", 0x6001020),
                self.indirect_owner("_geo_camera_main", 0x6001040),
            ),
            [transfer],
        )
        self.assertEqual(
            result.closure,
            frozenset({"_root", "_geo_process_node_and_siblings", "_geo_camera_main"}),
        )
        self.assertEqual(result.unlisted_transfers, ())

    def test_undeclared_dynamic_transfer_remains_unlisted(self) -> None:
        oracle = parse_route_oracle("ROUTE_ORACLE_VERSION 1\nROOT _root\n")
        transfer = UnresolvedTransfer("_root", 0x6001004, "jmp", "r2")
        result = audit_indirect_edges(
            {}, oracle, (self.indirect_owner("_root", 0x6001000),), [transfer]
        )
        self.assertEqual(result.unlisted_transfers, (transfer,))

    def test_duplicate_indirect_edge_input_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "duplicate INDIRECT_EDGE"):
            parse_route_oracle(
                "ROUTE_ORACLE_VERSION 1\nROOT _root\n"
                "INDIRECT_EDGE _root _callback\n"
                "INDIRECT_EDGE _root _callback\n"
            )

    def test_indirect_edge_with_unreachable_dispatcher_is_rejected(self) -> None:
        oracle = parse_route_oracle(
            "ROUTE_ORACLE_VERSION 1\nROOT _root\n"
            "INDIRECT_EDGE _stale_dispatcher _callback\n"
        )
        with self.assertRaisesRegex(ValueError, "unreachable dispatcher.*_stale_dispatcher"):
            audit_indirect_edges(
                {}, oracle,
                (
                    self.indirect_owner("_root", 0x6001000),
                    self.indirect_owner("_stale_dispatcher", 0x6001020),
                    self.indirect_owner("_callback", 0x6001040),
                ),
                [UnresolvedTransfer("_stale_dispatcher", 0x6001024, "jsr", "r1")],
            )

    def test_indirect_edge_with_missing_callback_owner_is_rejected(self) -> None:
        oracle = parse_route_oracle(
            "ROUTE_ORACLE_VERSION 1\nROOT _root\n"
            "INDIRECT_EDGE _root _missing_callback\n"
        )
        with self.assertRaisesRegex(ValueError, "missing callback owner.*_missing_callback"):
            audit_indirect_edges(
                {}, oracle, (self.indirect_owner("_root", 0x6001000),),
                [UnresolvedTransfer("_root", 0x6001004, "jsr", "r1")],
            )

    def test_unconsumed_indirect_edge_is_rejected(self) -> None:
        oracle = parse_route_oracle(
            "ROUTE_ORACLE_VERSION 1\nROOT _root\n"
            "INDIRECT_EDGE _root _callback\n"
        )
        with self.assertRaisesRegex(ValueError, "unconsumed INDIRECT_EDGE.*_root"):
            audit_indirect_edges(
                {}, oracle,
                (
                    self.indirect_owner("_root", 0x6001000),
                    self.indirect_owner("_callback", 0x6001020),
                ),
                [],
            )

    def test_indirect_edge_without_closure_contribution_is_rejected(self) -> None:
        oracle = parse_route_oracle(
            "ROUTE_ORACLE_VERSION 1\nROOT _root\n"
            "INDIRECT_EDGE _root _callback\n"
        )
        with self.assertRaisesRegex(ValueError, "no closure contribution.*_callback"):
            audit_indirect_edges(
                {"_root": {"_callback"}}, oracle,
                (
                    self.indirect_owner("_root", 0x6001000),
                    self.indirect_owner("_callback", 0x6001020),
                ),
                [UnresolvedTransfer("_root", 0x6001004, "jsr", "r1")],
            )

    def test_indirect_edge_to_callback_reachable_by_another_path_is_rejected(self) -> None:
        oracle = parse_route_oracle(
            "ROUTE_ORACLE_VERSION 1\nROOT _root\n"
            "INDIRECT_EDGE _dispatcher _callback\n"
        )
        with self.assertRaisesRegex(ValueError, "no closure contribution.*_callback"):
            audit_indirect_edges(
                {"_root": {"_dispatcher", "_callback"}}, oracle,
                (
                    self.indirect_owner("_root", 0x6001000),
                    self.indirect_owner("_dispatcher", 0x6001020),
                    self.indirect_owner("_callback", 0x6001040),
                ),
                [UnresolvedTransfer("_dispatcher", 0x6001024, "jsr", "r1")],
            )

    def test_six_sites_in_one_dispatcher_inherit_the_same_callback_set(self) -> None:
        oracle = parse_route_oracle(
            "ROUTE_ORACLE_VERSION 1\nROOT _root\n"
            "INDIRECT_EDGE _dispatcher _callback_a\n"
            "INDIRECT_EDGE _dispatcher _callback_b\n"
        )
        transfers = [
            UnresolvedTransfer("_dispatcher", 0x6001100 + offset * 2, "jsr", "r3")
            for offset in range(6)
        ]
        result = audit_indirect_edges(
            {"_root": {"_dispatcher"}}, oracle,
            (
                self.indirect_owner("_root", 0x6001000),
                self.indirect_owner("_dispatcher", 0x6001100),
                self.indirect_owner("_callback_a", 0x6001200),
                self.indirect_owner("_callback_b", 0x6001300),
            ),
            transfers,
        )
        self.assertTrue({"_callback_a", "_callback_b"} <= result.closure)
        self.assertEqual(result.unlisted_transfers, ())

    def test_omitted_route_derived_target_is_absent_from_indirect_closure(self) -> None:
        oracle = parse_route_oracle(
            "ROUTE_ORACLE_VERSION 1\nROOT _root\n"
            "INDIRECT_EDGE _dispatcher _geo_camera_main\n"
        )
        result = audit_indirect_edges(
            {"_root": {"_dispatcher"}}, oracle,
            (
                self.indirect_owner("_root", 0x6001000),
                self.indirect_owner("_dispatcher", 0x6001100),
                self.indirect_owner("_geo_camera_main", 0x6001200),
                self.indirect_owner("_geo_skybox_main", 0x6001300),
            ),
            [UnresolvedTransfer("_dispatcher", 0x6001104, "jsr", "r0")],
        )
        self.assertIn("_geo_camera_main", result.closure)
        self.assertNotIn("_geo_skybox_main", result.closure)

    def test_checked_in_sim_oracle_declares_complete_bob_callback_sets(self) -> None:
        text = Path(__file__).with_name(
            "sh2_native_math_sim_route_oracle_v1.txt"
        ).read_text(encoding="utf-8")
        oracle = parse_route_oracle(text)
        repo_root = Path(__file__).parents[2]
        bob_geo = (repo_root / "levels/bob/areas/1/geo.inc.c").read_text(
            encoding="utf-8"
        )
        bob_callbacks = frozenset(
            "_" + callback
            for callback in re.findall(
                r"GEO_(?:BACKGROUND|CAMERA_FRUSTUM_WITH_FUNC|CAMERA|ASM)"
                r"\([^)]*,\s*(geo_[A-Za-z0-9_]+)\)",
                bob_geo,
            )
        )
        script_sources = (
            repo_root / "src/port/saturn/sourceboot/source_entry.c",
            repo_root / "levels/bob/script.c",
            repo_root / "levels/scripts.c",
        )
        script_definitions: dict[str, str] = {}
        for source_path in script_sources:
            for match in re.finditer(
                r"(?:static\s+)?const\s+LevelScript\s+([A-Za-z_][A-Za-z0-9_]*)"
                r"\s*\[\]\s*=\s*\{(.*?)^\};",
                source_path.read_text(encoding="utf-8"),
                flags=re.MULTILINE | re.DOTALL,
            ):
                name, body = match.groups()
                self.assertNotIn(name, script_definitions)
                script_definitions[name] = body

        def nested_script_targets(body: str) -> set[str]:
            # The target is the sole JUMP/JUMP_LINK argument and EXECUTE's
            # fourth argument. Strip C comments first so named argument
            # annotations cannot look like targets.
            uncommented = re.sub(r"/\*.*?\*/", "", body, flags=re.DOTALL)
            return {
                *re.findall(
                    r"^\s*(?:JUMP|JUMP_LINK)\s*\(\s*"
                    r"([A-Za-z_][A-Za-z0-9_]*)\s*\)",
                    uncommented,
                    flags=re.MULTILINE,
                ),
                *re.findall(
                    r"^\s*(?:EXECUTE|EXIT_AND_EXECUTE)\s*\(\s*[^,]+,\s*"
                    r"[^,]+,\s*[^,]+,\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)",
                    uncommented,
                    flags=re.MULTILINE,
                ),
            }

        reachable_scripts: set[str] = set()
        pending_scripts = ["level_script_entry"]
        sourceboot_commands: set[str] = set()
        while pending_scripts:
            script_name = pending_scripts.pop()
            if script_name in reachable_scripts:
                continue
            self.assertIn(script_name, script_definitions)
            reachable_scripts.add(script_name)
            body = script_definitions[script_name]
            sourceboot_commands.update(re.findall(
                r"^\s*([A-Z][A-Z0-9_]+)\s*\(",
                body,
                flags=re.MULTILINE,
            ))
            pending_scripts.extend(nested_script_targets(body))
        self.assertTrue({
            "level_bob_entry", "script_func_global_1", "script_func_global_4",
            "script_func_global_15",
        } <= reachable_scripts)
        command_header = (repo_root / "include/level_commands.h").read_text(
            encoding="utf-8"
        )
        level_script = (repo_root / "src/engine/level_script.c").read_text(
            encoding="utf-8"
        )
        handlers_by_opcode = {
            int(opcode, 16): "_" + handler
            for opcode, handler in re.findall(
                r"/\*([0-9A-F]{2})\*/\s+(level_cmd_[a-z0-9_]+)",
                level_script,
            )
        }

        def command_handler(command: str) -> str | None:
            # OBJECT is the only reached macro that aliases another command
            # macro rather than owning its own opcode.
            command = {"OBJECT": "OBJECT_WITH_ACTS"}.get(command, command)
            definition = re.search(
                rf"^#define {command}\b[\s\S]*?(?=^#define |\Z)",
                command_header,
                flags=re.MULTILINE,
            )
            if definition is None:
                return None
            opcode = re.search(r"CMD_[A-Z]+\(0x([0-9A-F]{2})", definition.group())
            if opcode is None:
                return None
            return handlers_by_opcode.get(int(opcode.group(1), 16))

        sourceboot_callbacks = {
            command: command_handler(command)
            for command in sourceboot_commands
            if command_handler(command) is not None
        }
        self.assertEqual(set(sourceboot_callbacks), sourceboot_commands)
        self.assertTrue({
            "_level_cmd_load_model_from_dl", "_level_cmd_load_model_from_geo",
        } <= set(sourceboot_callbacks.values()))
        required_edges = {
            ("_geo_process_node_and_siblings", "_geo_skybox_main"),
            ("_geo_process_node_and_siblings", "_geo_camera_fov"),
            ("_geo_process_node_and_siblings", "_geo_camera_main"),
            ("_geo_process_node_and_siblings", "_geo_envfx_main"),
            ("_geo_process_node_and_siblings", "_geo_cannon_circle_base"),
            ("_level_script_execute", "_level_cmd_init_level"),
            ("_level_script_execute", "_level_cmd_get_or_set_var"),
            ("_level_script_execute", "_level_cmd_call"),
            ("_level_script_execute", "_level_cmd_load_and_execute"),
            ("_level_script_execute", "_level_cmd_clear_level"),
            ("_level_script_execute", "_level_cmd_jump"),
            ("_level_script_execute", "_level_cmd_set_register"),
        }
        self.assertEqual(required_edges - oracle.indirect_edges, set())
        self.assertEqual(oracle.static_manifest_edges, oracle.indirect_edges)
        self.assertEqual(
            {
                callback
                for dispatcher, callback in oracle.static_manifest_edges
                if dispatcher == "_geo_process_node_and_siblings"
            },
            bob_callbacks,
        )
        self.assertEqual(
            {
                callback
                for dispatcher, callback in oracle.static_manifest_edges
                if dispatcher == "_level_script_execute"
            },
            set(sourceboot_callbacks.values()),
        )

    def test_declared_dispatcher_does_not_mask_stack_derived_static_helper(self) -> None:
        oracle = parse_route_oracle(
            "ROUTE_ORACLE_VERSION 1\nROOT _root\n"
            "INDIRECT_EDGE _geo_process_node_and_siblings _geo_camera_main\n"
        )
        dynamic_callback = UnresolvedTransfer(
            "_geo_process_node_and_siblings", 0x6001110, "jsr", "r0"
        )
        regressed_static_helper = UnresolvedTransfer(
            "_geo_process_node_and_siblings", 0x6001190, "jsr", "r7",
            stack_source_offsets=(-224,), stack_store_addresses=(0x6001180,),
            provenance="static",
        )
        result = audit_indirect_edges(
            {"_root": {"_geo_process_node_and_siblings"}}, oracle,
            (
                self.indirect_owner("_root", 0x6001000),
                self.indirect_owner("_geo_process_node_and_siblings", 0x6001100),
                self.indirect_owner("_geo_camera_main", 0x6001200),
            ),
            [dynamic_callback, regressed_static_helper],
        )
        self.assertEqual(result.unlisted_transfers, (regressed_static_helper,))

    def test_stale_indirect_edge_target_is_rejected_against_static_manifest(self) -> None:
        oracle = parse_route_oracle(
            "ROUTE_ORACLE_VERSION 1\nROOT _root\n"
            "STATIC_MANIFEST_EDGE _dispatcher _callback\n"
            "INDIRECT_EDGE _dispatcher _stale\n"
        )
        with self.assertRaisesRegex(ValueError, "stale INDIRECT_EDGE target"):
            audit_indirect_edges(
                {"_root": {"_dispatcher"}}, oracle,
                (
                    self.indirect_owner("_root", 0x6001000),
                    self.indirect_owner("_dispatcher", 0x6001020),
                    self.indirect_owner("_callback", 0x6001040),
                    self.indirect_owner("_stale", 0x6001060),
                ),
                [UnresolvedTransfer("_dispatcher", 0x6001024, "jsr", "r1")],
            )

    def test_sourceboot_null_task_submit_proof_clears_only_the_guarded_transfer(self) -> None:
        disassembly = """
06001000 <_main>:
 6001000: d1 3f mov.l 6001100 <_sm64_saturn_source_runtime_configure>,r1 ! 06001100 <_sm64_saturn_source_runtime_configure>
 6001002: e5 00 mov #0,r5
 6001004: 41 0b jsr @r1
 6001006: e4 00 mov #0,r4
 6001008: 00 0b rts
 600100a: 00 09 nop
06001100 <_sm64_saturn_source_runtime_configure>:
 6001100: d1 7f mov.l 6001300 <_sTaskSubmit>,r1 ! 06001300 <_sTaskSubmit>
 6001102: 21 42 mov.l r4,@r1
 6001104: d1 7f mov.l 6001304 <_sTaskSubmitContext>,r1 ! 06001304 <_sTaskSubmitContext>
 6001106: 00 0b rts
 6001108: 21 52 mov.l r5,@r1
06001200 <_exec_display_list>:
 6001200: 24 48 tst r4,r4
 6001202: d1 0b mov.l 6001230 <_sState>,r1 ! 06001308 <_sState>
 6001204: 8b 03 bf 600120e <_exec_display_list+0xe>
 6001206: 52 12 mov.l @(8,r1),r2
 6001208: 72 01 add #1,r2
 600120a: 00 0b rts
 600120c: 11 22 mov.l r2,@(8,r1)
 600120e: 52 11 mov.l @(4,r1),r2
 6001210: 72 01 add #1,r2
 6001212: 11 21 mov.l r2,@(4,r1)
 6001214: d2 06 mov.l 6001234 <_sTaskSubmit>,r2 ! 06001300 <_sTaskSubmit>
 6001216: 62 22 mov.l @r2,r2
 6001218: 22 28 tst r2,r2
 600121a: 89 f4 bt 6001206 <_exec_display_list+0x6>
 600121c: d1 06 mov.l 6001238 <_sTaskSubmitContext>,r1 ! 06001304 <_sTaskSubmitContext>
 600121e: 42 2b jmp @r2
 6001220: 65 12 mov.l @r1,r5
"""
        instructions = parse_instructions(disassembly)
        owners = (
            self.indirect_owner("_main", 0x6001000),
            self.indirect_owner("_sm64_saturn_source_runtime_configure", 0x6001100),
            FunctionOwner("_exec_display_list", 0x6001200, 0x6001222, 1),
        )
        null_slots = prove_sourceboot_null_task_submit(instructions, owners)
        self.assertEqual(null_slots, frozenset({0x6001300}))
        result = analyze_code_only(
            instructions, owners,
            decoded_lines={"_exec_display_list": {0x600121C}},
            selected_names={"_exec_display_list"},
            known_null_addresses=null_slots,
        )
        self.assertEqual(result.unresolved_transfers, [])
        self.assertEqual(result.calls, [])
        self.assertEqual(result.direct_calls, [])

    def test_sourceboot_null_task_submit_rejects_dead_window_branch_targets_and_gaps(self) -> None:
        def fixture(branch_target: int, transfer_address: int) -> str:
            delay_address = transfer_address + 2
            return f"""
06001000 <_main>:
 6001000: d1 3f mov.l 6001100 <_sm64_saturn_source_runtime_configure>,r1 ! 06001100 <_sm64_saturn_source_runtime_configure>
 6001002: e5 00 mov #0,r5
 6001004: 41 0b jsr @r1
 6001006: e4 00 mov #0,r4
 6001008: 00 0b rts
 600100a: 00 09 nop
06001100 <_sm64_saturn_source_runtime_configure>:
 6001100: d1 7f mov.l 6001300 <_sTaskSubmit>,r1 ! 06001300 <_sTaskSubmit>
 6001102: 21 42 mov.l r4,@r1
 6001104: d1 7f mov.l 6001304 <_sTaskSubmitContext>,r1 ! 06001304 <_sTaskSubmitContext>
 6001106: 00 0b rts
 6001108: 21 52 mov.l r5,@r1
06001200 <_exec_display_list>:
 6001200: 24 48 tst r4,r4
 6001202: 8b 01 bf 6001208 <_exec_display_list+0x8>
 6001204: 00 0b rts
 6001206: 00 09 nop
 6001214: d2 06 mov.l 6001234 <_sTaskSubmit>,r2 ! 06001300 <_sTaskSubmit>
 6001216: 62 22 mov.l @r2,r2
 6001218: 22 28 tst r2,r2
 600121a: 89 f4 bt {branch_target:x} <_exec_display_list>
 600121c: d1 06 mov.l 6001238 <_sTaskSubmitContext>,r1 ! 06001304 <_sTaskSubmitContext>
 {transfer_address:x}: 42 2b jmp @r2
 {delay_address:x}: 65 12 mov.l @r1,r5
"""

        owners = (
            self.indirect_owner("_main", 0x6001000),
            self.indirect_owner("_sm64_saturn_source_runtime_configure", 0x6001100),
            FunctionOwner("_exec_display_list", 0x6001200, 0x6001224, 1),
        )
        near_matches = (
            ("branch_to_intervening", 0x600121C, 0x600121E),
            ("branch_to_transfer", 0x600121E, 0x600121E),
            ("branch_to_delay_slot", 0x6001220, 0x600121E),
            ("noncontiguous_transfer", 0x6001204, 0x6001220),
        )
        for label, branch_target, transfer_address in near_matches:
            with self.subTest(label=label):
                instructions = parse_instructions(
                    fixture(branch_target, transfer_address)
                )
                # Exercise the dead-block recognizer with an independently
                # established known-null slot. Some malformed branch targets
                # are also rejected earlier by the whole-image proof, but the
                # recognizer must remain fail-closed at its own boundary.
                null_slots = frozenset({0x6001300})
                self.assertEqual(
                    prove_sourceboot_null_task_submit(instructions, owners),
                    frozenset(),
                )
                self.assertEqual(
                    sourceboot_null_task_submit_dead_nodes(
                        instructions, owners, null_slots
                    ),
                    frozenset(),
                )
                result = analyze_code_only(
                    instructions, owners,
                    decoded_lines={"_exec_display_list": {transfer_address}},
                    selected_names={"_exec_display_list"},
                    known_null_addresses=null_slots,
                )
                self.assertTrue(any(
                    item.address == transfer_address
                    for item in result.unresolved_transfers
                ))

    def test_sourceboot_nonnull_or_unknown_task_submit_remains_unresolved(self) -> None:
        template = """
06001000 <_main>:
 6001000: d1 3f mov.l 6001100 <_sm64_saturn_source_runtime_configure>,r1 ! 06001100 <_sm64_saturn_source_runtime_configure>
 6001002: 41 0b jsr @r1
 6001004: {argument}
 6001006: 00 0b rts
 6001008: 00 09 nop
06001100 <_sm64_saturn_source_runtime_configure>:
 6001100: d1 7f mov.l 6001300 <_sTaskSubmit>,r1 ! 06001300 <_sTaskSubmit>
 6001102: 21 42 mov.l r4,@r1
 6001104: 00 0b rts
 6001106: 00 09 nop
06001200 <_exec_display_list>:
 6001200: d2 3f mov.l 6001300 <_sTaskSubmit>,r2 ! 06001300 <_sTaskSubmit>
 6001202: 62 22 mov.l @r2,r2
 6001204: 22 28 tst r2,r2
 6001206: 89 02 bt 600120e <_exec_display_list+0xe>
 6001208: 42 2b jmp @r2
 600120a: 00 09 nop
 600120e: 00 0b rts
 6001210: 00 09 nop
"""
        owners = (
            self.indirect_owner("_main", 0x6001000),
            self.indirect_owner("_sm64_saturn_source_runtime_configure", 0x6001100),
            self.indirect_owner("_exec_display_list", 0x6001200),
        )
        for argument in ("e4 01 mov #1,r4", "00 09 nop"):
            with self.subTest(argument=argument):
                instructions = parse_instructions(template.format(argument=argument))
                self.assertEqual(
                    prove_sourceboot_null_task_submit(instructions, owners), frozenset()
                )
                result = analyze_code_only(
                    instructions, owners, selected_names={"_exec_display_list"}
                )
                self.assertEqual(
                    [(item.address, item.mnemonic) for item in result.unresolved_transfers],
                    [(0x6001208, "jmp")],
                )

    def test_addr2line_addresses_are_batched_for_windows_command_limits(self) -> None:
        self.assertEqual(list(address_batches([1, 2, 3, 4, 5], 2)), [(1, 2), (3, 4), (5,)])

    def test_absolute_addr2line_gets_sh_tool_environment_without_parent_mutation(self) -> None:
        parent_path = os.environ.get("PATH", "")
        with tempfile.TemporaryDirectory() as directory:
            runtime = Path(directory)
            executable = runtime / "sh-elf-addr2line.exe"
            executable.write_bytes(b"MZ\0msys-2.0.dll\0")
            (runtime / "msys-2.0.dll").write_bytes(b"runtime")
            (runtime / "msys-gcc_s-seh-1.dll").write_bytes(b"runtime")
            with patch("verify_sh2_native_math.subprocess.run") as run:
                run.return_value.stdout = "_root\nsource.c:1\n"
                locations = source_locations(
                    str(executable),
                    Path("fixture.elf"),
                    [CallSite("_root", 0x6001000, "___addsf3")],
                )
        self.assertEqual(locations, {0x6001000: "source.c:1"})
        child_environment = run.call_args.kwargs["env"]
        self.assertTrue(child_environment["PATH"].startswith(
            str(runtime) + os.pathsep
        ))
        self.assertEqual(run.call_args.kwargs.get("cwd"), runtime)
        self.assertEqual(os.environ.get("PATH", ""), parent_path)

    def test_msys_tool_missing_runtime_fails_before_subprocess_launch(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "sh-elf-readelf.exe"
            executable.write_bytes(b"MZ\0msys-2.0.dll\0")

            def only_executable_exists(path: Path) -> bool:
                return path == executable

            with patch.object(Path, "is_file", only_executable_exists), patch(
                "verify_sh2_native_math.subprocess.run"
            ) as run:
                with self.assertRaisesRegex(
                    ValueError,
                    r"MSYS runtime.*msys-2\.0\.dll.*msys-gcc_s-seh-1\.dll",
                ):
                    run_command([str(executable), "--version"])
            run.assert_not_called()

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

    def test_maybe_stack_pointer_diagnostic_is_serializable(self) -> None:
        self.assertEqual(
            abstract_value_json(MAYBE_STACK_PTR),
            {"kind": "MaybeStackPtr"},
        )


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

    def analyze_named_fixture(self, disassembly: str, symbols_text: str):
        sections = parse_readelf_sections(
            "  [ 1] .text PROGBITS 06001000 001000 003000 00 AX 0 0 4\n"
        )
        symbols = parse_readelf_symbols(symbols_text, sections)
        owners = resolve_function_owners(symbols, sections)
        return analyze_code_only(parse_instructions(disassembly), owners)

    def _register_add_result(self, source, target=MAYBE_STACK_PTR):
        instruction = parse_instructions(" 6001000: 34 5c add r5,r4\n")[0x6001000]
        state = _unknown_state()
        state["r5"] = source
        state["r4"] = target
        effects = []
        _write_effect(
            instruction,
            state,
            effects,
            FunctionOwner("_root", 0x6001000, 0x6001002, 1),
        )
        self.assertEqual(effects, [])
        return state["r4"]

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
        self.assertFalse(any(x.address == 0x6001008 for x in result.calls))
        self.assertTrue(any(
            x.address == 0x6001008 and x.mnemonic == "jsr"
            for x in result.unresolved_transfers
        ))

    def test_disconnected_seed_does_not_assume_entry_argument_alias_provenance(self) -> None:
        dis = """
06001000 <_root>:
 6001000: 00 0b rts
 6001002: 00 09 nop
 6001008: d8 05 mov.l 6001020 <_child>,r8 ! 06001020 <_child>
 600100a: 2f 82 mov.l r8,@r15
 600100c: 24 02 mov.l r0,@r4
 600100e: 61 f2 mov.l @r15,r1
 6001010: 41 0b jsr @r1
 6001012: 00 09 nop
 6001014: 00 0b rts
 6001016: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        result = self.analyze(dis, "x.c 9 0x06001008\n")
        self.assertFalse(any(call.address == 0x6001010 for call in result.calls))
        self.assertTrue(any(
            item.address == 0x6001010
            for item in result.unresolved_transfers
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
 6001020: 89 02 bt 6001028 <_child+0x8>
 6001022: 00 09 nop
 6001024: 00 0b rts
 6001026: e0 00 mov #0,r0
 6001028: 00 0b rts
 600102a: e0 00 mov #0,r0
"""
        result = self.analyze(dis)
        self.assertEqual([x.address for x in result.calls],
                         [0x6001004, 0x600100C])
        self.assertTrue(any(
            x.address == 0x6001008 and x.mnemonic == "jsr"
            for x in result.unresolved_transfers
        ))

    def test_joined_symbol_jsr_emits_deterministic_deduplicated_may_calls(self) -> None:
        dis = """
06001000 <_root>:
 6001000: 20 08 tst r0,r0
 6001002: 89 02 bt 600100a <_root+0xa>
 6001004: d1 06 mov.l 6001020 <_child>,r1 ! 06001020 <_child>
 6001006: a0 07 bra 6001018 <_root+0x18>
 6001008: 00 09 nop
 600100a: 22 08 tst r2,r2
 600100c: 89 02 bt 6001014 <_root+0x14>
 600100e: d1 07 mov.l 6001030 <_zero>,r1 ! 06001030 <_zero>
 6001010: a0 02 bra 6001018 <_root+0x18>
 6001012: 00 09 nop
 6001014: d1 06 mov.l 6001030 <_zero_alias>,r1 ! 06001030 <_zero_alias>
 6001016: 00 09 nop
 6001018: 41 0b jsr @r1
 600101a: 00 09 nop
 600101c: 00 0b rts
 600101e: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
06001030 <_zero_alias>:
 6001030: 00 0b rts
 6001032: 00 09 nop
"""
        sections = parse_readelf_sections(self.SECTIONS)
        owners = resolve_function_owners(parse_readelf_symbols(self.SYMBOLS, sections), sections)
        profile: dict[str, Counter[str]] = {}
        result = analyze_code_only(
            parse_instructions(dis), owners, profile_by_owner=profile
        )
        self.assertEqual(
            [call for call in result.calls if call.caller == "_root"],
            [
                CallSite("_root", 0x6001018, "_child"),
                CallSite("_root", 0x6001018, "_zero_alias"),
            ],
        )
        self.assertEqual(
            [(fact.callee, fact.callee_offset) for fact in result.direct_calls
             if fact.caller == "_root"],
            [("_child", 0), ("_zero_alias", 0)],
        )
        self.assertGreater(profile["_root"]["call_transfer_evaluations"], 0)
        self.assertEqual(
            profile["_root"]["abi_continuations_scheduled"],
            profile["_root"]["call_transfer_evaluations"],
        )
        self.assertEqual(result.unresolved_transfers, [])

    def test_resolved_straight_line_leaf_preserves_unwritten_caller_register(self) -> None:
        dis = """
06001000 <_root>:
 6001000: d7 07 mov.l 6001020 <_child>,r7 ! 06001020 <_child>
 6001002: d2 0b mov.l 6001030 <_leaf>,r2 ! 06001030 <_leaf>
 6001004: 42 0b jsr @r2
 6001006: 00 09 nop
 6001008: 47 0b jsr @r7
 600100a: 00 09 nop
 600100c: 00 0b rts
 600100e: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
06001030 <_leaf>:
 6001030: 44 21 shar r4
 6001032: 00 0b rts
 6001034: 44 21 shar r4
"""
        result = self.analyze(dis)
        self.assertIn(CallSite("_root", 0x6001008, "_child"), result.calls)
        self.assertEqual(result.unresolved_transfers, [])

    def test_resolved_straight_line_leaf_still_clobbers_written_register(self) -> None:
        dis = """
06001000 <_root>:
 6001000: d7 07 mov.l 6001020 <_child>,r7 ! 06001020 <_child>
 6001002: d2 0b mov.l 6001030 <_leaf>,r2 ! 06001030 <_leaf>
 6001004: 42 0b jsr @r2
 6001006: 00 09 nop
 6001008: 47 0b jsr @r7
 600100a: 00 09 nop
 600100c: 00 0b rts
 600100e: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
06001030 <_leaf>:
 6001030: 67 43 mov r4,r7
 6001032: 00 0b rts
 6001034: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertNotIn(CallSite("_root", 0x6001008, "_child"), result.calls)
        self.assertEqual(
            [(item.address, item.mnemonic) for item in result.unresolved_transfers],
            [(0x6001008, "jsr")],
        )

    def test_resolved_conditional_leaf_preserves_unwritten_caller_register(self) -> None:
        dis = """
06001000 <_root>:
 6001000: d3 07 mov.l 6001020 <_child>,r3 ! 06001020 <_child>
 6001002: d2 0b mov.l 6001030 <_leaf>,r2 ! 06001030 <_leaf>
 6001004: 42 0b jsr @r2
 6001006: 00 09 nop
 6001008: 43 0b jsr @r3
 600100a: 00 09 nop
 600100c: 00 0b rts
 600100e: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
06001030 <_leaf>:
 6001030: 24 48 tst r4,r4
 6001032: 89 02 bt 600103a <_leaf+0xa>
 6001034: 60 43 mov r4,r0
 6001036: 00 0b rts
 6001038: 00 09 nop
 600103a: 60 53 mov r5,r0
 600103c: 00 0b rts
 600103e: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertIn(CallSite("_root", 0x6001008, "_child"), result.calls)
        self.assertEqual(result.unresolved_transfers, [])

    def test_resolved_bounded_braf_leaf_preserves_unwritten_caller_register(self) -> None:
        dis = """
06001000 <_root>:
 6001000: d3 07 mov.l 6001020 <_child>,r3 ! 06001020 <_child>
 6001002: d2 0b mov.l 6001030 <_leaf>,r2 ! 06001030 <_leaf>
 6001004: 42 0b jsr @r2
 6001006: 00 09 nop
 6001008: 43 0b jsr @r3
 600100a: 00 09 nop
 600100c: 00 0b rts
 600100e: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
06001030 <_leaf>:
 6001030: c9 01 and #1,r0
 6001032: 40 08 shll2 r0
 6001034: 00 23 braf r0
 6001036: 60 43 mov r4,r0
 6001038: 00 0b rts
 600103a: 00 09 nop
 600103c: 00 0b rts
 600103e: 40 01 shlr r0
"""
        result = self.analyze(dis)
        self.assertIn(CallSite("_root", 0x6001008, "_child"), result.calls)
        self.assertEqual(result.unresolved_transfers, [])

    def test_discovery_unknown_does_not_invalidate_entry_jsr_may_calls(self) -> None:
        dis = """
06001000 <_root>:
 6001000: d1 07 mov.l 6001020 <_child>,r1 ! 06001020 <_child>
 6001002: a0 01 bra 6001008 <_root+0x8>
 6001004: 00 09 nop
 6001006: 00 09 nop
 6001008: 41 0b jsr @r1
 600100a: 00 09 nop
 600100c: 00 0b rts
 600100e: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        sections = parse_readelf_sections(self.SECTIONS)
        owners = resolve_function_owners(parse_readelf_symbols(self.SYMBOLS, sections), sections)
        result = analyze_code_only(
            parse_instructions(dis),
            owners,
            {"_root": {0x6001006, 0x6001008}},
            {"_root"},
        )
        self.assertIn(CallSite("_root", 0x6001008, "_child"), result.calls)
        self.assertEqual(result.unresolved_transfers, [])

    def test_mixed_symbol_numeric_jsr_join_fails_closed(self) -> None:
        dis = """
06001000 <_root>:
 6001000: 20 08 tst r0,r0
 6001002: 89 02 bt 600100a <_root+0xa>
 6001004: d1 06 mov.l 6001020 <_child>,r1 ! 06001020 <_child>
 6001006: a0 01 bra 600100c <_root+0xc>
 6001008: 00 09 nop
 600100a: e1 01 mov #1,r1
 600100c: 41 0b jsr @r1
 600100e: 00 09 nop
 6001010: 00 0b rts
 6001012: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertFalse(any(call.caller == "_root" for call in result.calls))
        self.assertEqual(
            [(item.address, item.mnemonic) for item in result.unresolved_transfers],
            [(0x600100C, "jsr")],
        )

    def test_unowned_symbol_jsr_fails_closed(self) -> None:
        dis = """
06001000 <_root>:
 6001000: d1 07 mov.l 6001020 <_external>,r1 ! 06002000 <_external>
 6001002: 41 0b jsr @r1
 6001004: 00 09 nop
 6001006: 00 0b rts
 6001008: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertEqual(result.calls, [])
        self.assertEqual(
            [(item.address, item.mnemonic) for item in result.unresolved_transfers],
            [(0x6001002, "jsr")],
        )

    def test_stack_spill_in_call_delay_slot_survives_abi_clobber(self) -> None:
        dis = """
06001000 <_root>:
 6001000: 7f fc add #-4,r15
 6001002: d8 07 mov.l 6001020 <_child>,r8 ! 06001020 <_child>
 6001004: d0 0e mov.l 6001040 <___mulsf3>,r0 ! 06001040 <___mulsf3>
 6001006: 40 0b jsr @r0
 6001008: 2f 82 mov.l r8,@r15
 600100a: 61 f2 mov.l @r15,r1
 600100c: 41 0b jsr @r1
 600100e: 7f 04 add #4,r15
 6001010: 00 0b rts
 6001012: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
06001040 <___mulsf3>:
 6001040: 00 0b rts
 6001042: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertIn(CallSite("_root", 0x6001006, "___mulsf3"), result.calls)
        self.assertIn(CallSite("_root", 0x600100C, "_child"), result.calls)
        self.assertEqual(result.unresolved_transfers, [])

    def test_literal_target_in_r7_resolves_direct_helper_call(self) -> None:
        dis = """
06001000 <_root>:
 6001000: d7 07 mov.l 6001020 <___mulsf3>,r7 ! 06001040 <___mulsf3>
 6001002: 47 0b jsr @r7
 6001004: 00 09 nop
 6001006: 00 0b rts
 6001008: 00 09 nop
06001040 <___mulsf3>:
 6001040: 00 0b rts
 6001042: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertIn(CallSite("_root", 0x6001002, "___mulsf3"), result.calls)
        self.assertEqual(result.unresolved_transfers, [])

    def test_delay_slot_spill_restores_r7_target_after_caller_saved_clobber(self) -> None:
        dis = """
06001000 <_root>:
 6001000: d7 07 mov.l 6001020 <_child>,r7 ! 06001020 <_child>
 6001002: d0 0e mov.l 6001040 <___mulsf3>,r0 ! 06001040 <___mulsf3>
 6001004: 40 0b jsr @r0
 6001006: 2f 72 mov.l r7,@r15
 6001008: 61 f2 mov.l @r15,r1
 600100a: 41 0b jsr @r1
 600100c: 00 09 nop
 600100e: 00 0b rts
 6001010: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
06001040 <___mulsf3>:
 6001040: 67 03 mov r0,r7
 6001042: 00 0b rts
 6001044: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertIn(CallSite("_root", 0x6001004, "___mulsf3"), result.calls)
        self.assertIn(CallSite("_root", 0x600100A, "_child"), result.calls)
        self.assertEqual(result.unresolved_transfers, [])

    def test_fresh_wave1_non_escaped_frame_spills_recover_seventeen_direct_calls(self) -> None:
        """Catch loss of exact literal spills across non-frame output stores."""
        dis = """
06001000 <_create_skybox_facing_camera>:
 6001000: 7f f8 add #-8,r15
 6001002: d1 7f mov.l 6001200 <_create_skybox_facing_camera+0x200>,r1 ! 06003000 <_atan2s>
 6001004: 1f 11 mov.l r1,@(4,r15)
 6001006: d8 7f mov.l 6001204 <_create_skybox_facing_camera+0x204>,r8 ! 06003800 <_sSkyBoxInfo>
 6001008: 62 43 mov r4,r2
 600100a: 38 2c add r2,r8
 600100c: a0 38 bra 6001080 <_create_skybox_facing_camera+0x80>
 600100e: 00 09 nop
 6001080: 28 01 mov.w r0,@r8
 6001082: 51 f1 mov.l @(4,r15),r1
 6001084: 41 0b jsr @r1
 6001086: 00 09 nop
 6001088: 00 0b rts
 600108a: 00 09 nop
06001200 <_envfx_update_snow_blizzard>:
 6001200: 7f f4 add #-12,r15
 6001202: d1 7f mov.l 6001400 <_envfx_update_snow_blizzard+0x200>,r1 ! 06003020 <___mulsf3>
 6001204: 2f 12 mov.l r1,@r15
 6001206: d2 7f mov.l 6001404 <_envfx_update_snow_blizzard+0x204>,r2 ! 06003040 <___addsf3>
 6001208: 1f 21 mov.l r2,@(4,r15)
 600120a: d3 7f mov.l 6001408 <_envfx_update_snow_blizzard+0x208>,r3 ! 06003060 <___floatsisf>
 600120c: 1f 32 mov.l r3,@(8,r15)
 600120e: 68 43 mov r4,r8
 6001210: a0 8e bra 6001330 <_envfx_update_snow_blizzard+0x130>
 6001212: 00 09 nop
 6001330: 28 02 mov.l r0,@r8
 6001332: a0 06 bra 6001342 <_envfx_update_snow_blizzard+0x142>
 6001334: 00 09 nop
 6001342: 61 f2 mov.l @r15,r1
 6001344: 41 0b jsr @r1
 6001346: 00 09 nop
 6001348: 00 09 nop
 600134a: 51 f1 mov.l @(4,r15),r1
 600134c: 00 09 nop
 600134e: 41 0b jsr @r1
 6001350: 00 09 nop
 6001352: a0 10 bra 6001376 <_envfx_update_snow_blizzard+0x176>
 6001354: 00 09 nop
 6001376: 61 f2 mov.l @r15,r1
 6001378: 41 0b jsr @r1
 600137a: 00 09 nop
 600137c: 00 09 nop
 600137e: 51 f1 mov.l @(4,r15),r1
 6001380: 41 0b jsr @r1
 6001382: 00 09 nop
 6001384: a0 28 bra 60013d8 <_envfx_update_snow_blizzard+0x1d8>
 6001386: 00 09 nop
 60013d8: 51 f2 mov.l @(8,r15),r1
 60013da: 41 0b jsr @r1
 60013dc: 00 09 nop
 60013de: 00 0b rts
 60013e0: 00 09 nop
06001400 <_envfx_update_snow_normal>:
 6001400: 7f f8 add #-8,r15
 6001402: d1 7f mov.l 6001600 <_envfx_update_snow_normal+0x200>,r1 ! 06003020 <___mulsf3>
 6001404: 2f 12 mov.l r1,@r15
 6001406: d2 7f mov.l 6001604 <_envfx_update_snow_normal+0x204>,r2 ! 06003060 <___floatsisf>
 6001408: 1f 21 mov.l r2,@(4,r15)
 600140a: 68 43 mov r4,r8
 600140c: a0 90 bra 6001530 <_envfx_update_snow_normal+0x130>
 600140e: 00 09 nop
 6001530: 28 02 mov.l r0,@r8
 6001532: a0 0a bra 600154a <_envfx_update_snow_normal+0x14a>
 6001534: 00 09 nop
 600154a: 61 f2 mov.l @r15,r1
 600154c: 41 0b jsr @r1
 600154e: 00 09 nop
 6001550: a0 08 bra 6001564 <_envfx_update_snow_normal+0x164>
 6001552: 00 09 nop
 6001564: 61 f2 mov.l @r15,r1
 6001566: 41 0b jsr @r1
 6001568: 00 09 nop
 600156a: a0 30 bra 60015ce <_envfx_update_snow_normal+0x1ce>
 600156c: 00 09 nop
 60015ce: 51 f1 mov.l @(4,r15),r1
 60015d0: 41 0b jsr @r1
 60015d2: 00 09 nop
 60015d4: 00 0b rts
 60015d6: 00 09 nop
06001600 <_envfx_update_snow_water>:
 6001600: 7f f4 add #-12,r15
 6001602: d1 7f mov.l 6001800 <_envfx_update_snow_water+0x200>,r1 ! 06003060 <___floatsisf>
 6001604: 2f 12 mov.l r1,@r15
 6001606: d2 7f mov.l 6001804 <_envfx_update_snow_water+0x204>,r2 ! 06003080 <___subsf3>
 6001608: 1f 21 mov.l r2,@(4,r15)
 600160a: d3 7f mov.l 6001808 <_envfx_update_snow_water+0x208>,r3 ! 060030c0 <_random_float>
 600160c: 1f 32 mov.l r3,@(8,r15)
 600160e: 68 43 mov r4,r8
 6001610: a0 41 bra 6001696 <_envfx_update_snow_water+0x96>
 6001612: 00 09 nop
 6001696: 28 02 mov.l r0,@r8
 6001698: 00 09 nop
 600169a: 61 f2 mov.l @r15,r1
 600169c: 41 0b jsr @r1
 600169e: 00 09 nop
 60016a0: 00 09 nop
 60016a2: 51 f1 mov.l @(4,r15),r1
 60016a4: 41 0b jsr @r1
 60016a6: 00 09 nop
 60016a8: a0 08 bra 60016bc <_envfx_update_snow_water+0xbc>
 60016aa: 00 09 nop
 60016bc: 51 f2 mov.l @(8,r15),r1
 60016be: 41 0b jsr @r1
 60016c0: 00 09 nop
 60016c2: 00 09 nop
 60016c4: 61 f2 mov.l @r15,r1
 60016c6: 41 0b jsr @r1
 60016c8: 00 09 nop
 60016ca: 00 09 nop
 60016cc: 00 09 nop
 60016ce: 51 f1 mov.l @(4,r15),r1
 60016d0: 41 0b jsr @r1
 60016d2: 00 09 nop
 60016d4: 00 0b rts
 60016d6: 00 09 nop
06001800 <_orbit_from_positions>:
 6001800: 7f fc add #-4,r15
 6001802: d1 7f mov.l 6001a00 <_orbit_from_positions+0x200>,r1 ! 060030a0 <_sqrtf>
 6001804: 2f 12 mov.l r1,@r15
 6001806: 6e 63 mov r6,r14
 6001808: a0 38 bra 600187c <_orbit_from_positions+0x7c>
 600180a: 00 09 nop
 600187c: 2e 01 mov.w r0,@r14
 600187e: 61 f2 mov.l @r15,r1
 6001880: 41 0b jsr @r1
 6001882: 00 09 nop
 6001884: 00 0b rts
 6001886: 00 09 nop
06001900 <_pos_from_orbit>:
 6001900: 7f fc add #-4,r15
 6001902: d1 7f mov.l 6001b00 <_pos_from_orbit+0x200>,r1 ! 06003040 <___addsf3>
 6001904: 2f 12 mov.l r1,@r15
 6001906: 69 53 mov r5,r9
 6001908: a0 38 bra 600197c <_pos_from_orbit+0x7c>
 600190a: 00 09 nop
 600197c: 29 01 mov.w r0,@r9
 600197e: 61 f2 mov.l @r15,r1
 6001980: 41 0b jsr @r1
 6001982: 00 09 nop
 6001984: a0 0c bra 60019a0 <_pos_from_orbit+0xa0>
 6001986: 00 09 nop
 60019a0: 61 f2 mov.l @r15,r1
 60019a2: 41 0b jsr @r1
 60019a4: 00 09 nop
 60019a6: 00 0b rts
 60019a8: 00 09 nop
06003000 <_atan2s>:
 6003000: 00 0b rts
 6003002: 00 09 nop
06003020 <___mulsf3>:
 6003020: 00 0b rts
 6003022: 00 09 nop
06003040 <___addsf3>:
 6003040: 00 0b rts
 6003042: 00 09 nop
06003060 <___floatsisf>:
 6003060: 00 0b rts
 6003062: 00 09 nop
06003080 <___subsf3>:
 6003080: 00 0b rts
 6003082: 00 09 nop
060030a0 <_sqrtf>:
 60030a0: 00 0b rts
 60030a2: 00 09 nop
060030c0 <_random_float>:
 60030c0: 00 0b rts
 60030c2: 00 09 nop
"""
        result = self.analyze_named_fixture(dis, """
   1: 06001000 256 FUNC GLOBAL DEFAULT 1 _create_skybox_facing_camera
   2: 06001200 512 FUNC GLOBAL DEFAULT 1 _envfx_update_snow_blizzard
   3: 06001400 512 FUNC GLOBAL DEFAULT 1 _envfx_update_snow_normal
   4: 06001600 512 FUNC GLOBAL DEFAULT 1 _envfx_update_snow_water
   5: 06001800 256 FUNC GLOBAL DEFAULT 1 _orbit_from_positions
   6: 06001900 176 FUNC GLOBAL DEFAULT 1 _pos_from_orbit
   7: 06003000 4 FUNC GLOBAL DEFAULT 1 _atan2s
   8: 06003020 4 FUNC GLOBAL DEFAULT 1 ___mulsf3
   9: 06003040 4 FUNC GLOBAL DEFAULT 1 ___addsf3
  10: 06003060 4 FUNC GLOBAL DEFAULT 1 ___floatsisf
  11: 06003080 4 FUNC GLOBAL DEFAULT 1 ___subsf3
  12: 060030a0 4 FUNC GLOBAL DEFAULT 1 _sqrtf
  13: 060030c0 4 FUNC GLOBAL DEFAULT 1 _random_float
""")
        expected = {
            DirectCallFact("_create_skybox_facing_camera", 132, "_atan2s", 0),
            DirectCallFact("_envfx_update_snow_blizzard", 324, "___mulsf3", 0),
            DirectCallFact("_envfx_update_snow_blizzard", 334, "___addsf3", 0),
            DirectCallFact("_envfx_update_snow_blizzard", 376, "___mulsf3", 0),
            DirectCallFact("_envfx_update_snow_blizzard", 384, "___addsf3", 0),
            DirectCallFact("_envfx_update_snow_blizzard", 474, "___floatsisf", 0),
            DirectCallFact("_envfx_update_snow_normal", 332, "___mulsf3", 0),
            DirectCallFact("_envfx_update_snow_normal", 358, "___mulsf3", 0),
            DirectCallFact("_envfx_update_snow_normal", 464, "___floatsisf", 0),
            DirectCallFact("_envfx_update_snow_water", 156, "___floatsisf", 0),
            DirectCallFact("_envfx_update_snow_water", 164, "___subsf3", 0),
            DirectCallFact("_envfx_update_snow_water", 190, "_random_float", 0),
            DirectCallFact("_envfx_update_snow_water", 198, "___floatsisf", 0),
            DirectCallFact("_envfx_update_snow_water", 208, "___subsf3", 0),
            DirectCallFact("_orbit_from_positions", 128, "_sqrtf", 0),
            DirectCallFact("_pos_from_orbit", 128, "___addsf3", 0),
            DirectCallFact("_pos_from_orbit", 162, "___addsf3", 0),
        }
        self.assertEqual(set(result.direct_calls), expected)
        self.assertEqual(result.unresolved_transfers, [])

    def test_reloaded_external_frame_alias_invalidates_literal_spill(self) -> None:
        dis = """
06001000 <_root>:
 6001000: 7f f8 add #-8,r15
 6001002: d7 0f mov.l 6001040 <___mulsf3>,r7 ! 06001040 <___mulsf3>
 6001004: 2f 72 mov.l r7,@r15
 6001006: 68 f3 mov r15,r8
 6001008: d1 3d mov.l 6001100 <_escaped_frame>,r1 ! 06002000 <_escaped_frame>
 600100a: 21 82 mov.l r8,@r1
 600100c: d1 3c mov.l 6001100 <_escaped_frame>,r1 ! 06002000 <_escaped_frame>
 600100e: 62 12 mov.l @r1,r2
 6001010: 22 02 mov.l r0,@r2
 6001012: 61 f2 mov.l @r15,r1
 6001014: 41 0b jsr @r1
 6001016: 00 09 nop
 6001018: 00 0b rts
 600101a: 00 09 nop
06001040 <___mulsf3>:
 6001040: 00 0b rts
 6001042: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertEqual(result.calls, [])
        self.assertEqual(result.direct_calls, [])
        self.assertEqual(
            [(item.address, item.mnemonic) for item in result.unresolved_transfers],
            [(0x6001014, "jsr")],
        )

    def test_unknown_frame_derived_alias_escaped_to_global_invalidates_spill(self) -> None:
        dis = """
06001000 <_root>:
 6001000: 7f f8 add #-8,r15
 6001002: 68 f3 mov r15,r8
 6001004: 78 01 add #1,r8
 6001006: d2 3d mov.l 6001100 <_escaped_frame>,r2 ! 06002000 <_escaped_frame>
 6001008: 22 82 mov.l r8,@r2
 600100a: d7 0d mov.l 6001040 <___mulsf3>,r7 ! 06001040 <___mulsf3>
 600100c: 2f 72 mov.l r7,@r15
 600100e: d2 3c mov.l 6001100 <_escaped_frame>,r2 ! 06002000 <_escaped_frame>
 6001010: 61 22 mov.l @r2,r1
 6001012: 21 02 mov.l r0,@r1
 6001014: 61 f2 mov.l @r15,r1
 6001016: 41 0b jsr @r1
 6001018: 00 09 nop
 600101a: 00 0b rts
 600101c: 00 09 nop
06001040 <___mulsf3>:
 6001040: 00 0b rts
 6001042: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertEqual(result.calls, [])
        self.assertEqual(result.direct_calls, [])
        self.assertEqual(
            [(item.address, item.mnemonic) for item in result.unresolved_transfers],
            [(0x6001016, "jsr")],
        )

    def test_predecrement_unknown_frame_alias_escape_invalidates_later_spill(self) -> None:
        dis = """
06001000 <_root>:
 6001000: 7f f8 add #-8,r15
 6001002: 68 f3 mov r15,r8
 6001004: 78 01 add #1,r8
 6001006: d2 3d mov.l 6001104 <_escaped_frame_end>,r2 ! 06002004 <_escaped_frame_end>
 6001008: 22 86 mov.l r8,@-r2
 600100a: d7 0d mov.l 6001040 <___mulsf3>,r7 ! 06001040 <___mulsf3>
 600100c: 2f 72 mov.l r7,@r15
 600100e: d2 3c mov.l 6001100 <_escaped_frame>,r2 ! 06002000 <_escaped_frame>
 6001010: 61 22 mov.l @r2,r1
 6001012: 21 02 mov.l r0,@r1
 6001014: 61 f2 mov.l @r15,r1
 6001016: 41 0b jsr @r1
 6001018: 00 09 nop
 600101a: 00 0b rts
 600101c: 00 09 nop
06001040 <___mulsf3>:
 6001040: 00 0b rts
 6001042: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertEqual(result.calls, [])
        self.assertEqual(result.direct_calls, [])
        self.assertEqual(
            [(item.address, item.mnemonic) for item in result.unresolved_transfers],
            [(0x6001016, "jsr")],
        )

    def test_geo_process_held_object_recovers_spilled_vec3f_helper(self) -> None:
        dis = """
06001000 <_geo_process_held_object>:
 6001000: 2f 86 mov.l r8,@-r15
 6001002: 2f 96 mov.l r9,@-r15
 6001004: 2f a6 mov.l r10,@-r15
 6001006: 2f b6 mov.l r11,@-r15
 6001008: 2f c6 mov.l r12,@-r15
 600100a: 2f d6 mov.l r13,@-r15
 600100c: 2f e6 mov.l r14,@-r15
 600100e: 4f 22 sts.l pr,@-r15
 6001010: 7f 84 add #-124,r15
 6001012: 6c f3 mov r15,r12
 6001014: 7c 3c add #60,r12
 6001016: a0 73 bra 6001100 <_geo_process_held_object+0x100>
 6001018: 00 09 nop
 6001100: d7 7e mov.l 60012fc <_geo_process_held_object+0x2fc>,r7 ! 06003000 <_saturn_vec3f_to_q16>
 6001102: 1f 72 mov.l r7,@(8,r15)
 6001104: 64 c3 mov r12,r4
 6001106: d2 7e mov.l 6001300 <_frame_writer>,r2 ! 06003020 <_frame_writer>
 6001108: 42 0b jsr @r2
 600110a: 00 09 nop
 600110c: 57 f2 mov.l @(8,r15),r7
 600110e: 00 09 nop
 6001110: 00 09 nop
 6001112: 00 09 nop
 6001114: 00 09 nop
 6001116: 00 09 nop
 6001118: 00 09 nop
 600111a: 00 09 nop
 600111c: 00 09 nop
 600111e: 00 09 nop
 6001120: 00 09 nop
 6001122: 00 09 nop
 6001124: 00 09 nop
 6001126: 00 09 nop
 6001128: 47 0b jsr @r7
 600112a: 00 09 nop
 600112c: 00 0b rts
 600112e: 00 09 nop
06003000 <_saturn_vec3f_to_q16>:
 6003000: 00 0b rts
 6003002: 00 09 nop
06003020 <_frame_writer>:
 6003020: 24 02 mov.l r0,@r4
 6003022: 00 0b rts
 6003024: 00 09 nop
"""
        result = self.analyze_named_fixture(dis, """
   1: 06001000 304 FUNC GLOBAL DEFAULT 1 _geo_process_held_object
   2: 06003000 4 FUNC GLOBAL DEFAULT 1 _saturn_vec3f_to_q16
   3: 06003020 6 FUNC GLOBAL DEFAULT 1 _frame_writer
""")
        self.assertIn(
            CallSite("_geo_process_held_object", 0x6001128, "_saturn_vec3f_to_q16"),
            result.calls,
        )
        self.assertIn(
            DirectCallFact(
                "_geo_process_held_object", 296, "_saturn_vec3f_to_q16", 0
            ),
            result.direct_calls,
        )
        self.assertFalse(any(x.address == 0x6001128 for x in result.unresolved_transfers))

    def test_geo_process_node_and_siblings_recovers_loop_spilled_q16_helper(self) -> None:
        dis = """
06001000 <_geo_process_node_and_siblings>:
 6001000: 2f 86 mov.l r8,@-r15
 6001002: 2f 96 mov.l r9,@-r15
 6001004: 2f a6 mov.l r10,@-r15
 6001006: 2f b6 mov.l r11,@-r15
 6001008: 2f c6 mov.l r12,@-r15
 600100a: 2f d6 mov.l r13,@-r15
 600100c: 2f e6 mov.l r14,@-r15
 600100e: 4f 22 sts.l pr,@-r15
 6001010: 7f 8c add #-116,r15
  6001012: 7f 8c add #-116,r15
  6001014: a4 e4 bra 60019e0 <_geo_process_node_and_siblings+0x9e0>
 6001016: 65 f3 mov r15,r5
 60019e0: 93 24 mov.w 6001a2c <_geo_process_node_and_siblings+0xa2c>,r3 ! a8
 60019e2: 6d 13 mov r1,r13
 60019e4: d7 1e mov.l 6001a60 <_geo_process_node_and_siblings+0xa60>,r7 ! 06003000 <_sm64_saturn_float_to_q16>
 60019e6: 62 13 mov r1,r2
 60019e8: 33 fc add r15,r3
 60019ea: 7d 10 add #16,r13
 60019ec: 72 50 add #80,r2
 60019ee: 61 d3 mov r13,r1
 60019f0: 71 f0 add #-16,r1
 60019f2: 65 f3 mov r15,r5
 60019f4: 64 16 mov.l @r1+,r4
 60019f6: 1f 18 mov.l r1,@(32,r15)
 60019f8: 1f 27 mov.l r2,@(28,r15)
 60019fa: 1f 39 mov.l r3,@(36,r15)
 60019fc: 47 0b jsr @r7
 60019fe: 1f 7a mov.l r7,@(40,r15)
 6001a00: 51 f8 mov.l @(32,r15),r1
 6001a02: 00 09 nop
 6001a04: 3d 10 cmp/eq r1,r13
 6001a06: 52 f7 mov.l @(28,r15),r2
 6001a08: 7e 04 add #4,r14
 6001a0a: 53 f9 mov.l @(36,r15),r3
 6001a0c: 8f f2 bf.s 60019f4 <_geo_process_node_and_siblings+0x9f4>
 6001a0e: 57 fa mov.l @(40,r15),r7
 6001a10: 00 0b rts
 6001a12: 00 09 nop
06003000 <_sm64_saturn_float_to_q16>:
 6003000: 67 03 mov r0,r7
 6003002: 24 02 mov.l r0,@r4
 6003004: 00 0b rts
 6003006: 00 09 nop
"""
        result = self.analyze_named_fixture(dis, """
   1: 06001000 2580 FUNC GLOBAL DEFAULT 1 _geo_process_node_and_siblings
   2: 06003000 8 FUNC GLOBAL DEFAULT 1 _sm64_saturn_float_to_q16
""")
        self.assertIn(
            CallSite(
                "_geo_process_node_and_siblings", 0x60019FC,
                "_sm64_saturn_float_to_q16",
            ),
            result.calls,
            result.unresolved_transfers,
        )
        self.assertIn(
            DirectCallFact(
                "_geo_process_node_and_siblings", 2556,
                "_sm64_saturn_float_to_q16", 0,
            ),
            result.direct_calls,
        )
        self.assertFalse(any(x.address == 0x60019FC for x in result.unresolved_transfers))

    def test_gu_mtx_f2l_recovers_loop_spilled_fixsfsi_helper(self) -> None:
        dis = """
06001000 <_guMtxF2L>:
 6001000: 2f 86 mov.l r8,@-r15
 6001002: 68 43 mov r4,r8
 6001004: 2f 96 mov.l r9,@-r15
 6001006: 78 50 add #80,r8
 6001008: 2f a6 mov.l r10,@-r15
 600100a: 2f b6 mov.l r11,@-r15
 600100c: 6b 43 mov r4,r11
 600100e: 2f c6 mov.l r12,@-r15
 6001010: 7b 10 add #16,r11
 6001012: 2f d6 mov.l r13,@-r15
 6001014: 2f e6 mov.l r14,@-r15
 6001016: 4f 22 sts.l pr,@-r15
 6001018: 7f b0 add #-80,r15
 600101a: 69 f3 mov r15,r9
 600101c: dc 1f mov.l 60010bc <_guMtxF2L+0xbc>,r12 ! 7fffffff
 600101e: 79 10 add #16,r9
 6001020: 9d 3b mov.w 600109a <_guMtxF2L+0x9a>,r13 ! 8d
 6001022: d3 1f mov.l 60010a0 <_guMtxF2L+0xa0>,r3 ! 807fffff
 6001024: d7 1f mov.l 60010a4 <_guMtxF2L+0xa4>,r7 ! 06003000 <___fixsfsi>
 6001026: 1f 53 mov.l r5,@(12,r15)
 6001028: 65 93 mov r9,r5
 600102a: 6a b3 mov r11,r10
 600102c: 7a f0 add #-16,r10
 600102e: 6e 93 mov r9,r14
 6001030: d1 1a mov.l 60010bc <_guMtxF2L+0xbc>,r1 ! 7fffffff
 6001032: 64 a6 mov.l @r10+,r4
 6001034: 24 18 tst r1,r4
 6001036: 8d 0b bt.s 6001050 <_guMtxF2L+0x50>
 6001038: e0 00 mov #0,r0
 600103a: 66 43 mov r4,r6
 600103c: 46 29 shlr16 r6
 600103e: 36 6c add r6,r6
 6001040: 46 19 shlr8 r6
 6001042: 66 6c extu.b r6,r6
 6001044: 36 d0 cmp/hi r13,r6
 6001046: 8b 1a bf 600107e <_guMtxF2L+0x7e>
 6001048: 60 43 mov r4,r0
 600104a: 40 00 shll r0
 600104c: 30 0a subc r0,r0
 600104e: 20 ca xor r12,r0
 6001050: 2e 02 mov.l r0,@r14
 6001052: 3a b0 cmp/eq r11,r10
 6001054: 8f ec bf.s 6001030 <_guMtxF2L+0x30>
 6001056: 7e 04 add #4,r14
 6001058: 6b a3 mov r10,r11
 600105a: 7b 10 add #16,r11
 600105c: 3b 80 cmp/eq r8,r11
 600105e: 8f e4 bf.s 600102a <_guMtxF2L+0x2a>
 6001060: 79 10 add #16,r9
 6001062: 00 0b rts
 6001064: 00 09 nop
 600107e: 76 10 add #16,r6
 6001080: 46 28 shll16 r6
 6001082: 46 01 shlr r6
 6001084: 24 39 and r3,r4
 6001086: 46 18 shll8 r6
 6001088: 1f 31 mov.l r3,@(4,r15)
 600108a: 24 6b or r6,r4
 600108c: 1f 52 mov.l r5,@(8,r15)
 600108e: 47 0b jsr @r7
 6001090: 2f 72 mov.l r7,@r15
 6001092: 67 f2 mov.l @r15,r7
 6001094: 55 f2 mov.l @(8,r15),r5
 6001096: af db bra 6001050 <_guMtxF2L+0x50>
 6001098: 53 f1 mov.l @(4,r15),r3
06003000 <___fixsfsi>:
 6003000: d2 07 mov.l 6003020 <_shift_helper>,r2 ! 06003020 <_shift_helper>
 6003002: 42 0b jsr @r2
 6003004: 67 03 mov r0,r7
 6003006: 00 0b rts
 6003008: 00 09 nop
06003020 <_shift_helper>:
 6003020: 00 0b rts
 6003022: 00 09 nop
"""
        result = self.analyze_named_fixture(dis, """
   1: 06001000 154 FUNC GLOBAL DEFAULT 1 _guMtxF2L
   2: 06003000 10 FUNC GLOBAL DEFAULT 1 ___fixsfsi
   3: 06003020 4 FUNC GLOBAL DEFAULT 1 _shift_helper
""")
        self.assertIn(CallSite("_guMtxF2L", 0x600108E, "___fixsfsi"), result.calls)
        self.assertIn(
            DirectCallFact("_guMtxF2L", 142, "___fixsfsi", 0),
            result.direct_calls,
        )
        self.assertFalse(any(x.address == 0x600108E for x in result.unresolved_transfers))

    def test_dereferenced_graph_node_func_pointer_stays_unresolved(self) -> None:
        dis = """
06001000 <_root>:
 6001000: d7 07 mov.l 6001020 <_GraphNodeFunc>,r7 ! 06002000 <_GraphNodeFunc>
 6001002: 67 72 mov.l @r7,r7
 6001004: 47 0b jsr @r7
 6001006: 00 09 nop
 6001008: 00 0b rts
 600100a: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertEqual(result.calls, [])
        self.assertEqual(result.direct_calls, [])
        self.assertEqual(
            [(item.address, item.mnemonic) for item in result.unresolved_transfers],
            [(0x6001004, "jsr")],
        )

    def test_fresh_wave1_data_driven_dispatchers_stay_unresolved(self) -> None:
        dis = """
06001000 <_geo_call_global_function_nodes_helper>:
 6001000: 00 09 nop
 6001002: 00 09 nop
 6001004: 00 09 nop
 6001006: 00 09 nop
 6001008: 00 09 nop
 600100a: 00 09 nop
 600100c: 00 09 nop
 600100e: 00 09 nop
 6001010: 00 09 nop
 6001012: 00 09 nop
 6001014: 00 09 nop
 6001016: 00 09 nop
 6001018: 00 09 nop
 600101a: 00 09 nop
 600101c: 00 09 nop
 600101e: 50 85 mov.l @(20,r8),r0
 6001020: 20 08 tst r0,r0
 6001022: 8d 03 bt.s 600102c <_geo_call_global_function_nodes_helper+0x2c>
 6001024: e6 00 mov #0,r6
 6001026: 65 83 mov r8,r5
 6001028: 40 0b jsr @r0
 600102a: 64 b3 mov r11,r4
 600102c: 00 0b rts
 600102e: 00 09 nop
06001100 <_level_cmd_call>:
 6001100: 2f 86 mov.l r8,@-r15
 6001102: 2f 96 mov.l r9,@-r15
 6001104: d8 09 mov.l 600112c <_sCurrentCmd>,r8 ! 06003800 <_sCurrentCmd>
 6001106: 4f 22 sts.l pr,@-r15
 6001108: 64 82 mov.l @r8,r4
 600110a: d9 09 mov.l 6001130 <_sRegister>,r9 ! 06003804 <_sRegister>
 600110c: 51 41 mov.l @(4,r4),r1
 600110e: 85 41 mov.w @(2,r4),r0
 6001110: 65 92 mov.l @r9,r5
 6001112: 41 0b jsr @r1
 6001114: 64 03 mov r0,r4
 6001116: 00 0b rts
 6001118: 00 09 nop
06001200 <_level_cmd_call_loop>:
 6001200: 2f 86 mov.l r8,@-r15
 6001202: 2f 96 mov.l r9,@-r15
 6001204: d8 0c mov.l 6001238 <_sCurrentCmd>,r8 ! 06003800 <_sCurrentCmd>
 6001206: 4f 22 sts.l pr,@-r15
 6001208: 64 82 mov.l @r8,r4
 600120a: d9 0c mov.l 600123c <_sRegister>,r9 ! 06003804 <_sRegister>
 600120c: 51 41 mov.l @(4,r4),r1
 600120e: 85 41 mov.w @(2,r4),r0
 6001210: 65 92 mov.l @r9,r5
 6001212: 41 0b jsr @r1
 6001214: 64 03 mov r0,r4
 6001216: 00 0b rts
 6001218: 00 09 nop
06001300 <_process_geo_layout>:
 6001300: 00 09 nop
 6001302: 00 09 nop
 6001304: 00 09 nop
 6001306: 00 09 nop
 6001308: 00 09 nop
 600130a: 00 09 nop
 600130c: 00 09 nop
 600130e: 00 09 nop
 6001310: 00 09 nop
 6001312: 00 09 nop
 6001314: 00 09 nop
 6001316: 00 09 nop
 6001318: 00 09 nop
 600131a: 00 09 nop
 600131c: 00 09 nop
 600131e: 00 09 nop
 6001320: 00 09 nop
 6001322: 00 09 nop
 6001324: 00 09 nop
 6001326: 00 09 nop
 6001328: 00 09 nop
 600132a: 00 09 nop
 600132c: 00 09 nop
 600132e: 00 09 nop
 6001330: 00 09 nop
 6001332: 00 09 nop
 6001334: 00 09 nop
 6001336: 00 09 nop
 6001338: 00 09 nop
 600133a: 00 09 nop
 600133c: d8 13 mov.l 600138c <_GeoLayoutJumpTable>,r8 ! 06003808 <_GeoLayoutJumpTable>
 600133e: 61 b2 mov.l @r11,r1
 6001340: 21 18 tst r1,r1
 6001342: 8b 06 bf 6001352 <_process_geo_layout+0x52>
 6001344: 00 0b rts
 6001346: 00 09 nop
 6001348: 00 09 nop
 600134a: 00 09 nop
 600134c: 00 09 nop
 600134e: 00 09 nop
 6001350: 00 09 nop
 6001352: 60 10 mov.b @r1,r0
 6001354: 60 0c extu.b r0,r0
 6001356: 40 08 shll2 r0
 6001358: 01 8e mov.l @(r0,r8),r1
 600135a: 41 0b jsr @r1
 600135c: 00 09 nop
 600135e: 00 0b rts
 6001360: 00 09 nop
"""
        result = self.analyze_named_fixture(dis, """
   1: 06001000 48 FUNC GLOBAL DEFAULT 1 _geo_call_global_function_nodes_helper
   2: 06001100 26 FUNC GLOBAL DEFAULT 1 _level_cmd_call
   3: 06001200 26 FUNC GLOBAL DEFAULT 1 _level_cmd_call_loop
   4: 06001300 98 FUNC GLOBAL DEFAULT 1 _process_geo_layout
""")
        self.assertEqual(result.calls, [])
        self.assertEqual(result.direct_calls, [])
        owner_starts = {
            "_geo_call_global_function_nodes_helper": 0x6001000,
            "_level_cmd_call": 0x6001100,
            "_level_cmd_call_loop": 0x6001200,
            "_process_geo_layout": 0x6001300,
        }
        self.assertEqual(
            [(item.caller, item.address - owner_starts[item.caller], item.mnemonic)
             for item in result.unresolved_transfers],
            [
                ("_geo_call_global_function_nodes_helper", 40, "jsr"),
                ("_level_cmd_call", 18, "jsr"),
                ("_level_cmd_call_loop", 18, "jsr"),
                ("_process_geo_layout", 90, "jsr"),
            ],
        )

    def test_extu_w_preserves_exact_unsigned_low_word(self) -> None:
        state = _unknown_state()
        state["r6"] = ConstSet("unsigned", frozenset({0x1234ABCD}))
        instruction = parse_instructions(
            " 6001000: 65 6d extu.w r6,r5\n"
        )[0x6001000]
        _write_effect(
            instruction, state, [],
            FunctionOwner("_root", 0x6001000, 0x6001002, 1),
        )
        self.assertEqual(
            state["r5"], ConstSet("unsigned", frozenset({0xABCD}))
        )

    def test_exts_b_preserves_exact_signed_low_byte(self) -> None:
        state = _unknown_state()
        state["r6"] = ConstSet("unsigned", frozenset({0xFF}))
        instruction = parse_instructions(
            " 6001000: 65 6e exts.b r6,r5\n"
        )[0x6001000]
        _write_effect(
            instruction, state, [],
            FunctionOwner("_root", 0x6001000, 0x6001002, 1),
        )
        self.assertEqual(state["r5"], ConstSet("signed", frozenset({-1})))

    def test_exts_w_preserves_exact_signed_low_word(self) -> None:
        state = _unknown_state()
        state["r6"] = ConstSet("unsigned", frozenset({0x8001}))
        instruction = parse_instructions(
            " 6001000: 65 6f exts.w r6,r5\n"
        )[0x6001000]
        _write_effect(
            instruction, state, [],
            FunctionOwner("_root", 0x6001000, 0x6001002, 1),
        )
        self.assertEqual(
            state["r5"], ConstSet("signed", frozenset({-0x7FFF}))
        )

    def test_stack_push_pop_preserves_known_symbol_target(self) -> None:
        dis = """
06001000 <_root>:
 6001000: d1 07 mov.l 6001020 <_child>,r1 ! 06001020 <_child>
 6001002: 2f 16 mov.l r1,@-r15
 6001004: d0 0e mov.l 6001040 <___mulsf3>,r0 ! 06001040 <___mulsf3>
 6001006: 40 0b jsr @r0
 6001008: 00 09 nop
 600100a: 61 f6 mov.l @r15+,r1
 600100c: 41 0b jsr @r1
 600100e: 00 09 nop
 6001010: 00 0b rts
 6001012: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
06001040 <___mulsf3>:
 6001040: 00 0b rts
 6001042: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertIn(CallSite("_root", 0x600100C, "_child"), result.calls)
        self.assertEqual(result.unresolved_transfers, [])

    def test_unknown_parameter_stack_save_reload_stays_unknown(self) -> None:
        dis = """
06001000 <_root>:
 6001000: 2f 46 mov.l r4,@-r15
 6001002: 61 f6 mov.l @r15+,r1
 6001004: 41 0b jsr @r1
 6001006: 00 09 nop
 6001008: 00 0b rts
 600100a: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertEqual(result.calls, [])
        self.assertEqual(
            [(item.address, item.mnemonic) for item in result.unresolved_transfers],
            [(0x6001004, "jsr")],
        )
        transfer = result.unresolved_transfers[0]
        self.assertEqual(transfer.stack_source_offsets, (-4,))
        self.assertEqual(transfer.stack_store_addresses, (0x6001000,))
        self.assertEqual(transfer.provenance, "dynamic")

    def test_literal_and_unknown_stack_merge_stays_dynamic(self) -> None:
        dis = """
06001000 <_root>:
 6001000: 7f fc add #-4,r15
 6001002: 20 08 tst r0,r0
 6001004: 89 03 bt 600100e <_root+0xe>
 6001006: d1 06 mov.l 6001020 <_child>,r1 ! 06001020 <_child>
 6001008: a0 02 bra 6001010 <_root+0x10>
 600100a: 2f 12 mov.l r1,@r15
 600100e: 2f 42 mov.l r4,@r15
 6001010: 61 f2 mov.l @r15,r1
 6001012: 41 0b jsr @r1
 6001014: 00 09 nop
 6001016: 00 0b rts
 6001018: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        result = self.analyze(dis)
        transfer = next(
            item for item in result.unresolved_transfers
            if item.address == 0x6001012
        )
        self.assertEqual(transfer.provenance, "dynamic")

    def test_conflicting_stack_targets_join_to_unknown(self) -> None:
        dis = """
06001000 <_root>:
 6001000: 7f fc add #-4,r15
 6001002: 20 08 tst r0,r0
 6001004: 89 03 bt 600100e <_root+0xe>
 6001006: d1 06 mov.l 6001020 <_child>,r1 ! 06001020 <_child>
 6001008: a0 03 bra 6001012 <_root+0x12>
 600100a: 2f 12 mov.l r1,@r15
 600100e: d1 07 mov.l 6001030 <_zero_alias>,r1 ! 06001030 <_zero_alias>
 6001010: 2f 12 mov.l r1,@r15
 6001012: 61 f2 mov.l @r15,r1
 6001014: 41 0b jsr @r1
 6001016: 00 09 nop
 6001018: 00 0b rts
 600101a: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
06001030 <_zero_alias>:
 6001030: 00 0b rts
 6001032: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertFalse(any(call.address == 0x6001014 for call in result.calls))
        self.assertTrue(any(item.address == 0x6001014 for item in result.unresolved_transfers))

    def test_missing_predecessor_stack_store_does_not_recover_target(self) -> None:
        dis = """
06001000 <_root>:
 6001000: 7f fc add #-4,r15
 6001002: 20 08 tst r0,r0
 6001004: 89 04 bt 6001010 <_root+0x10>
 6001006: d1 06 mov.l 6001020 <_child>,r1 ! 06001020 <_child>
 6001008: 2f 12 mov.l r1,@r15
 600100a: a0 02 bra 6001012 <_root+0x12>
 600100c: 00 09 nop
 6001010: 00 09 nop
 6001012: 61 f2 mov.l @r15,r1
 6001014: 41 0b jsr @r1
 6001016: 00 09 nop
 6001018: 00 0b rts
 600101a: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertEqual(result.calls, [])
        self.assertEqual(result.direct_calls, [])
        self.assertEqual(
            [(item.address, item.mnemonic) for item in result.unresolved_transfers],
            [(0x6001014, "jsr")],
        )
        atom = SymbolAtom("_child", 0x6001020)
        joined = join_value(
            StackMemory(),
            StackMemory(((-4, StackSlot(
                ConstSet("symbol", frozenset({atom})),
                (0x6001008,),
                frozenset({atom}),
            )),)),
        )
        self.assertTrue(dict(joined.slots)[-4].unknown_store)

    def test_unknown_store_alias_invalidates_known_stack_slots(self) -> None:
        dis = """
06001000 <_root>:
 6001000: 7f fc add #-4,r15
 6001002: d8 07 mov.l 6001020 <_child>,r8 ! 06001020 <_child>
 6001004: 2f 82 mov.l r8,@r15
 6001006: 61 03 mov r0,r1
 6001008: 21 22 mov.l r2,@r1
 600100a: 61 f2 mov.l @r15,r1
 600100c: 41 0b jsr @r1
 600100e: 00 09 nop
 6001010: 00 0b rts
 6001012: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertFalse(any(call.address == 0x600100C for call in result.calls))
        self.assertTrue(any(item.address == 0x600100C for item in result.unresolved_transfers))

    def test_predecrement_stack_alias_overwrite_invalidates_target(self) -> None:
        dis = """
06001000 <_root>:
 6001000: 7f f8 add #-8,r15
 6001002: d8 07 mov.l 6001020 <_child>,r8 ! 06001020 <_child>
 6001004: 1f 81 mov.l r8,@(4,r15)
 6001006: ee 08 mov #8,r14
 6001008: 3e fc add r15,r14
 600100a: 2e 26 mov.l r2,@-r14
 600100c: 61 f1 mov.l @(4,r15),r1
 600100e: 41 0b jsr @r1
 6001010: 00 09 nop
 6001012: 7f 08 add #8,r15
 6001014: 00 0b rts
 6001016: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertFalse(any(call.address == 0x600100E for call in result.calls))
        self.assertTrue(any(item.address == 0x600100E for item in result.unresolved_transfers))

    def test_indexed_stack_alias_overwrite_invalidates_target(self) -> None:
        dis = """
06001000 <_root>:
 6001000: 7f f8 add #-8,r15
 6001002: d8 07 mov.l 6001020 <_child>,r8 ! 06001020 <_child>
 6001004: 1f 81 mov.l r8,@(4,r15)
 6001006: 6e f3 mov r15,r14
 6001008: e0 04 mov #4,r0
 600100a: 0e 24 mov.l r2,@(r0,r14)
 600100c: 61 f1 mov.l @(4,r15),r1
 600100e: 41 0b jsr @r1
 6001010: 00 09 nop
 6001012: 7f 08 add #8,r15
 6001014: 00 0b rts
 6001016: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertFalse(any(call.address == 0x600100E for call in result.calls))
        self.assertTrue(any(item.address == 0x600100E for item in result.unresolved_transfers))

    def test_unknown_indexed_store_invalidates_known_stack_slots(self) -> None:
        dis = """
06001000 <_root>:
 6001000: 7f fc add #-4,r15
 6001002: d8 07 mov.l 6001020 <_child>,r8 ! 06001020 <_child>
 6001004: 2f 82 mov.l r8,@r15
 6001006: 01 24 mov.l r2,@(r0,r1)
 6001008: 61 f2 mov.l @r15,r1
 600100a: 41 0b jsr @r1
 600100c: 00 09 nop
 600100e: 7f 04 add #4,r15
 6001010: 00 0b rts
 6001012: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertFalse(any(call.address == 0x600100A for call in result.calls))
        self.assertTrue(any(item.address == 0x600100A for item in result.unresolved_transfers))

    def test_nonleaf_call_invalidates_stack_slot_whose_address_escaped(self) -> None:
        dis = """
06001000 <_root>:
 6001000: 7f fc add #-4,r15
 6001002: d8 07 mov.l 6001020 <_child>,r8 ! 06001020 <_child>
 6001004: 2f 82 mov.l r8,@r15
 6001006: 64 f3 mov r15,r4
 6001008: d2 09 mov.l 6001030 <_zero_alias>,r2 ! 06001030 <_zero_alias>
 600100a: 42 0b jsr @r2
 600100c: 00 09 nop
 600100e: 61 f2 mov.l @r15,r1
 6001010: 41 0b jsr @r1
 6001012: 00 09 nop
 6001014: 7f 04 add #4,r15
 6001016: 00 0b rts
 6001018: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
06001030 <_zero_alias>:
 6001030: 24 02 mov.l r0,@r4
 6001032: 00 0b rts
 6001034: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertFalse(any(call.address == 0x6001010 for call in result.calls))
        self.assertTrue(any(item.address == 0x6001010 for item in result.unresolved_transfers))

    def test_nonleaf_call_invalidates_neighbor_of_escaped_stack_address(self) -> None:
        dis = """
06001000 <_root>:
 6001000: 7f f8 add #-8,r15
 6001002: d8 07 mov.l 6001020 <_child>,r8 ! 06001020 <_child>
 6001004: 2f 82 mov.l r8,@r15
 6001006: e4 04 mov #4,r4
 6001008: 34 fc add r15,r4
 600100a: d2 09 mov.l 6001030 <_zero_alias>,r2 ! 06001030 <_zero_alias>
 600100c: 42 0b jsr @r2
 600100e: 00 09 nop
 6001010: 61 f2 mov.l @r15,r1
 6001012: 41 0b jsr @r1
 6001014: 00 09 nop
 6001016: 7f 08 add #8,r15
 6001018: 00 0b rts
 600101a: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
06001030 <_zero_alias>:
 6001030: 24 02 mov.l r0,@r4
 6001032: 00 0b rts
 6001034: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertFalse(any(call.address == 0x6001012 for call in result.calls))
        self.assertTrue(any(item.address == 0x6001012 for item in result.unresolved_transfers))

    def test_joined_maybe_stack_argument_invalidates_escaped_frame(self) -> None:
        dis = """
06001000 <_root>:
 6001000: 7f f8 add #-8,r15
 6001002: d8 07 mov.l 6001020 <_child>,r8 ! 06001020 <_child>
 6001004: 2f 82 mov.l r8,@r15
 6001006: 20 08 tst r0,r0
 6001008: 89 02 bt 6001010 <_root+0x10>
 600100a: 6f 43 mov r15,r4
 600100c: a0 01 bra 6001012 <_root+0x12>
 600100e: 00 09 nop
 6001010: e4 00 mov #0,r4
 6001012: b0 0d bsr 6001030 <_zero_alias>
 6001014: 00 09 nop
 6001016: 61 f2 mov.l @r15,r1
 6001018: 41 0b jsr @r1
 600101a: 00 09 nop
 600101c: 00 0b rts
 600101e: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
06001030 <_zero_alias>:
 6001030: 24 02 mov.l r0,@r4
 6001032: 00 0b rts
 6001034: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertFalse(any(call.address == 0x6001018 for call in result.calls))
        self.assertEqual(
            [(item.address, item.mnemonic) for item in result.unresolved_transfers],
            [(0x6001018, "jsr")],
        )

    def test_stack_slot_join_preserves_maybe_stack_pointer(self) -> None:
        left = StackMemory(((0, StackSlot(StackPtr(4))),))
        right = StackMemory(((0, StackSlot(ConstSet("signed", frozenset({0})))),))
        self.assertEqual(
            join_value(left, right),
            StackMemory(((0, StackSlot(MAYBE_STACK_PTR)),)),
        )

    def test_add_immediate_preserves_maybe_stack_pointer(self) -> None:
        instruction = parse_instructions(" 6001000: 74 04 add #4,r4\n")[0x6001000]
        state = _unknown_state()
        state["r4"] = MAYBE_STACK_PTR
        effects = []
        _write_effect(
            instruction,
            state,
            effects,
            FunctionOwner("_root", 0x6001000, 0x6001002, 1),
        )
        self.assertIs(state["r4"], MAYBE_STACK_PTR)
        self.assertEqual(effects, [])

    def test_register_add_preserves_maybe_stack_pointer_before_memory_call(self) -> None:
        dis = """
06001000 <_root>:
 6001000: 7f f8 add #-8,r15
 6001002: d8 0e mov.l 6001040 <_child>,r8 ! 06001040 <_child>
 6001004: 2f 82 mov.l r8,@r15
 6001006: 20 08 tst r0,r0
 6001008: 89 02 bt 6001010 <_root+0x10>
 600100a: 6f 43 mov r15,r4
 600100c: a0 01 bra 6001012 <_root+0x12>
 600100e: 00 09 nop
 6001010: e4 00 mov #0,r4
 6001012: e5 04 mov #4,r5
 6001014: 34 5c add r5,r4
 6001016: b0 1b bsr 6001050 <_leaf>
 6001018: 00 09 nop
 600101a: 61 f2 mov.l @r15,r1
 600101c: 41 0b jsr @r1
 600101e: 00 09 nop
 6001020: 00 0b rts
 6001022: 00 09 nop
06001040 <_child>:
 6001040: 00 0b rts
 6001042: 00 09 nop
06001050 <_leaf>:
 6001050: 24 02 mov.l r0,@r4
 6001052: 00 0b rts
 6001054: 00 09 nop
"""
        sections = parse_readelf_sections(self.SECTIONS)
        owners = resolve_function_owners(
            parse_readelf_symbols(
                "   1: 06001000 36 FUNC GLOBAL DEFAULT 1 _root\n"
                "   2: 06001040 16 FUNC GLOBAL DEFAULT 1 _child\n"
                "   3: 06001050 16 FUNC GLOBAL DEFAULT 1 _leaf\n",
                sections,
            ),
            sections,
        )
        result = analyze_code_only(parse_instructions(dis), owners)
        self.assertFalse(any(call.address == 0x600101C for call in result.calls))
        self.assertEqual(
            [(item.address, item.mnemonic) for item in result.unresolved_transfers],
            [(0x600101C, "jsr")],
        )

    def test_register_add_preserves_maybe_stack_pointer_with_constset(self) -> None:
        self.assertIs(
            self._register_add_result(ConstSet("signed", frozenset({0, 4}))),
            MAYBE_STACK_PTR,
        )

    def test_register_add_preserves_maybe_stack_pointer_with_interval(self) -> None:
        self.assertIs(
            self._register_add_result(Interval("signed", -4, 4)),
            MAYBE_STACK_PTR,
        )

    def test_register_add_preserves_maybe_stack_pointer_with_unknown(self) -> None:
        self.assertIs(
            self._register_add_result(UNKNOWN),
            MAYBE_STACK_PTR,
        )

    def test_register_add_preserves_two_maybe_stack_pointers(self) -> None:
        self.assertIs(
            self._register_add_result(MAYBE_STACK_PTR),
            MAYBE_STACK_PTR,
        )

    def test_register_add_degrades_exact_stack_pointer_with_constset(self) -> None:
        self.assertIs(
            self._register_add_result(
                ConstSet("signed", frozenset({0, 4})), StackPtr(8)
            ),
            MAYBE_STACK_PTR,
        )

    def test_register_add_degrades_exact_stack_pointer_with_interval(self) -> None:
        self.assertIs(
            self._register_add_result(Interval("signed", -4, 4), StackPtr(8)),
            MAYBE_STACK_PTR,
        )

    def test_register_add_degrades_exact_stack_pointer_with_unknown(self) -> None:
        self.assertIs(
            self._register_add_result(UNKNOWN, StackPtr(8)),
            MAYBE_STACK_PTR,
        )

    def test_register_add_degrades_two_exact_stack_pointers(self) -> None:
        self.assertIs(
            self._register_add_result(StackPtr(4), StackPtr(8)),
            MAYBE_STACK_PTR,
        )

    def test_register_add_retains_exact_stack_pointer_with_singleton_offset(self) -> None:
        self.assertEqual(
            self._register_add_result(
                ConstSet("signed", frozenset({4})), StackPtr(8)
            ),
            StackPtr(12),
        )

    def test_register_add_degrades_unaligned_singleton_stack_offset(self) -> None:
        self.assertIs(
            self._register_add_result(
                ConstSet("signed", frozenset({2})), StackPtr(8)
            ),
            MAYBE_STACK_PTR,
        )

    def test_register_add_degrades_out_of_bound_singleton_stack_offset(self) -> None:
        self.assertIs(
            self._register_add_result(
                ConstSet("signed", frozenset({4})), StackPtr(4096)
            ),
            MAYBE_STACK_PTR,
        )

    def test_store_through_maybe_stack_pointer_invalidates_frame(self) -> None:
        instruction = parse_instructions(" 6001000: 24 02 mov.l r0,@r4\n")[0x6001000]
        state = _unknown_state()
        state["r4"] = MAYBE_STACK_PTR
        state["stack_memory"] = StackMemory((
            (0, StackSlot(ConstSet("symbol", frozenset()))),
        ))
        effects = []
        _write_effect(
            instruction,
            state,
            effects,
            FunctionOwner("_root", 0x6001000, 0x6001002, 1),
        )
        self.assertEqual(
            state["stack_memory"],
            StackMemory(((0, StackSlot(UNKNOWN, (), frozenset(), True)),)),
        )
        self.assertEqual(effects, [])

    def test_derived_stack_alias_preserves_nonoverlapping_target_spill(self) -> None:
        dis = """
06001000 <_root>:
 6001000: 7f f0 add #-16,r15
 6001002: d8 07 mov.l 6001020 <_child>,r8 ! 06001020 <_child>
 6001004: 1f 81 mov.l r8,@(4,r15)
 6001006: ee 00 mov #0,r14
 6001008: 3e fc add r15,r14
 600100a: 2e 22 mov.l r2,@r14
 600100c: 7e 04 add #4,r14
 600100e: 61 e2 mov.l @r14,r1
 6001010: 41 0b jsr @r1
 6001012: 00 09 nop
 6001014: 7f 10 add #16,r15
 6001016: 00 0b rts
 6001018: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertIn(CallSite("_root", 0x6001010, "_child"), result.calls)
        self.assertEqual(result.unresolved_transfers, [])

    def test_derived_stack_alias_overwrite_does_not_preserve_stale_target(self) -> None:
        dis = """
06001000 <_root>:
 6001000: 7f f0 add #-16,r15
 6001002: d8 07 mov.l 6001020 <_child>,r8 ! 06001020 <_child>
 6001004: 1f 81 mov.l r8,@(4,r15)
 6001006: ee 04 mov #4,r14
 6001008: 3e fc add r15,r14
 600100a: 2e 22 mov.l r2,@r14
 600100c: 61 e2 mov.l @r14,r1
 600100e: 41 0b jsr @r1
 6001010: 00 09 nop
 6001012: 7f 10 add #16,r15
 6001014: 00 0b rts
 6001016: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertNotIn(CallSite("_root", 0x600100E, "_child"), result.calls)
        self.assertEqual(
            [(item.address, item.mnemonic) for item in result.unresolved_transfers],
            [(0x600100E, "jsr")],
        )

    def test_div0s_sequence_has_known_semantics_without_symbol_creation(self) -> None:
        dis = """
06001000 <_root>:
 6001000: d0 07 mov.l 6001020 <_child>,r0 ! 06001020 <_child>
 6001002: d1 06 mov.l 6001020 <_child>,r1 ! 06001020 <_child>
 6001004: d2 0e mov.l 6001040 <___mulsf3>,r2 ! 06001040 <___mulsf3>
 6001006: d3 04 mov.l 6001030 <_zero_alias>,r3 ! 06001030 <_zero_alias>
 6001008: 21 27 div0s r2,r1
 600100a: 33 3a subc r3,r3
 600100c: 23 07 div0s r0,r3
 600100e: 41 24 rotcl r1
 6001010: 33 04 div1 r0,r3
 6001012: 42 0b jsr @r2
 6001014: 00 09 nop
 6001016: 43 0b jsr @r3
 6001018: 00 09 nop
 600101a: 00 0b rts
 600101c: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
06001030 <_zero_alias>:
 6001030: 00 0b rts
 6001032: 00 09 nop
06001040 <___mulsf3>:
 6001040: 00 0b rts
 6001042: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertEqual(result.unresolved_effects, [])
        self.assertIn(CallSite("_root", 0x6001012, "___mulsf3"), result.calls)
        self.assertFalse(any(call.address == 0x6001016 for call in result.calls))
        self.assertTrue(any(item.address == 0x6001016 for item in result.unresolved_transfers))

    def test_unparseable_register_effect_fails_closed(self) -> None:
        dis = """
06001000 <_root>:
 6001000: 12 34 mystery r1,@(r2,r3)
 6001002: 00 0b rts
 6001004: 00 09 nop
"""
        self.assertTrue(self.analyze(dis).unresolved_effects)

    def test_unknown_single_register_instruction_blocks_leaf_proof(self) -> None:
        dis = """
06001000 <_root>:
 6001000: d3 07 mov.l 6001020 <_child>,r3 ! 06001020 <_child>
 6001002: d2 0b mov.l 6001030 <_leaf>,r2 ! 06001030 <_leaf>
 6001004: 42 0b jsr @r2
 6001006: 00 09 nop
 6001008: 43 0b jsr @r3
 600100a: 00 09 nop
 600100c: 00 0b rts
 600100e: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
06001030 <_leaf>:
 6001030: 12 34 mystery r2
 6001032: 00 0b rts
 6001034: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertTrue(any(
            effect.address == 0x6001030 and effect.mnemonic == "mystery"
            for effect in result.unresolved_effects
        ))
        self.assertFalse(any(call.address == 0x6001008 for call in result.calls))

    def test_unknown_register_free_instruction_blocks_leaf_proof(self) -> None:
        dis = """
06001000 <_root>:
 6001000: d3 07 mov.l 6001020 <_child>,r3 ! 06001020 <_child>
 6001002: d2 0b mov.l 6001030 <_leaf>,r2 ! 06001030 <_leaf>
 6001004: 42 0b jsr @r2
 6001006: 00 09 nop
 6001008: 43 0b jsr @r3
 600100a: 00 09 nop
 600100c: 00 0b rts
 600100e: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
06001030 <_leaf>:
 6001030: 12 34 mystery
 6001032: 00 0b rts
 6001034: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertTrue(any(
            effect.address == 0x6001030 and effect.mnemonic == "mystery"
            for effect in result.unresolved_effects
        ))
        self.assertFalse(any(call.address == 0x6001008 for call in result.calls))

    def test_memory_writing_leaf_preserves_proven_unwritten_gpr(self) -> None:
        dis = """
06001000 <_root>:
 6001000: d3 07 mov.l 6001020 <_child>,r3 ! 06001020 <_child>
 6001002: d2 0b mov.l 6001030 <_leaf>,r2 ! 06001030 <_leaf>
 6001004: 42 0b jsr @r2
 6001006: 00 09 nop
 6001008: 43 0b jsr @r3
 600100a: 00 09 nop
 600100c: 00 0b rts
 600100e: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
06001030 <_leaf>:
 6001030: 24 02 mov.l r0,@r4
 6001032: 00 0b rts
 6001034: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertIn(CallSite("_root", 0x6001008, "_child"), result.calls)

    def test_cross_owner_direct_bra_emits_tail_call_fact(self) -> None:
        dis = """
06001000 <_root>:
 6001000: a0 0e bra 6001020 <_child>
 6001002: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertIn(CallSite("_root", 0x6001000, "_child"), result.calls)
        self.assertTrue(any(
            fact.caller == "_root" and fact.callee == "_child"
            for fact in result.direct_calls
        ))

    def test_unowned_direct_bra_fails_closed(self) -> None:
        dis = """
06001000 <_root>:
 6001000: a0 0e bra 6002000 <_external>
 6001002: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertTrue(any(
            item.address == 0x6001000 and item.mnemonic == "bra"
            for item in result.unresolved_transfers
        ))

    def test_missing_delay_slot_fails_closed_without_scheduling_target(self) -> None:
        dis = """
06001000 <_root>:
 6001000: d8 07 mov.l 6001020 <_child>,r8 ! 06001020 <_child>
 6001002: a0 01 bra 6001008 <_root+0x8>
 6001008: 48 0b jsr @r8
 600100a: 00 09 nop
 600100c: 00 0b rts
 600100e: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertTrue(any(
            item.address == 0x6001002 and item.mnemonic == "bra"
            for item in result.unresolved_transfers
        ))
        self.assertFalse(any(call.address == 0x6001008 for call in result.calls))

    def test_nondelayed_branch_missing_boundary_fallthrough_fails_closed(self) -> None:
        dis = """
06001000 <_root>:
 6001000: a0 0d bra 600101e <_root+0x1e>
 6001002: 00 09 nop
 600101e: 8b ef bf 6001000 <_root>
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertTrue(any(
            item.address == 0x600101E and item.mnemonic == "bf"
            for item in result.unresolved_transfers
        ))

    def test_delayed_branch_missing_boundary_pc_plus_four_fails_closed(self) -> None:
        dis = """
06001000 <_root>:
 6001000: a0 0c bra 600101c <_root+0x1c>
 6001002: 00 09 nop
 600101c: 8f f0 bf.s 6001000 <_root>
 600101e: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertTrue(any(
            item.address == 0x600101C and item.mnemonic == "bf.s"
            for item in result.unresolved_transfers
        ))

    def test_ordinary_fallthrough_into_another_owner_fails_closed(self) -> None:
        dis = """
06001000 <_root>:
 6001000: a0 0d bra 600101e <_root+0x1e>
 6001002: 00 09 nop
 600101e: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertTrue(any(
            item.address == 0x600101E and item.mnemonic == "nop"
            for item in result.unresolved_transfers
        ))

    def test_control_instruction_in_delay_slot_fails_closed(self) -> None:
        dis = """
06001000 <_root>:
 6001000: d8 07 mov.l 6001020 <_child>,r8 ! 06001020 <_child>
 6001002: a0 01 bra 6001008 <_root+0x8>
 6001004: 00 0b rts
 6001006: 00 09 nop
 6001008: 48 0b jsr @r8
 600100a: 00 09 nop
 600100c: 00 0b rts
 600100e: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertTrue(any(
            item.address == 0x6001002 and item.mnemonic == "bra"
            for item in result.unresolved_transfers
        ))
        self.assertFalse(any(call.address == 0x6001008 for call in result.calls))

    def test_resolved_jsr_with_missing_delay_slot_keeps_diagnostic(self) -> None:
        dis = """
06001000 <_root>:
 6001000: d8 07 mov.l 6001020 <_child>,r8 ! 06001020 <_child>
 6001002: a0 0c bra 600101e <_root+0x1e>
 6001004: 00 09 nop
 600101e: 48 0b jsr @r8
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertTrue(any(
            item.address == 0x600101E and item.mnemonic == "jsr"
            for item in result.unresolved_transfers
        ))

    def test_resolved_bsr_with_control_delay_slot_keeps_diagnostic(self) -> None:
        dis = """
06001000 <_root>:
 6001000: a0 0c bra 600101c <_root+0x1c>
 6001002: 00 09 nop
 600101c: b0 00 bsr 6001020 <_child>
 600101e: 00 0b rts
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertTrue(any(
            item.address == 0x600101C and item.mnemonic == "bsr"
            for item in result.unresolved_transfers
        ))

    def test_resolved_bsr_with_missing_continuation_keeps_diagnostic(self) -> None:
        dis = """
06001000 <_root>:
 6001000: a0 0c bra 600101c <_root+0x1c>
 6001002: 00 09 nop
 600101c: b0 00 bsr 6001020 <_child>
 600101e: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertTrue(any(
            item.address == 0x600101C and item.mnemonic == "bsr"
            for item in result.unresolved_transfers
        ))

    def test_unparseable_conditional_target_fails_closed_without_fallthrough(self) -> None:
        dis = """
06001000 <_root>:
 6001000: d8 07 mov.l 6001020 <_child>,r8 ! 06001020 <_child>
 6001002: 89 02 bt <malformed>
 6001004: 48 0b jsr @r8
 6001006: 00 09 nop
 6001008: 00 0b rts
 600100a: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertTrue(any(
            item.address == 0x6001002 and item.mnemonic == "bt"
            for item in result.unresolved_transfers
        ))
        self.assertFalse(any(call.address == 0x6001004 for call in result.calls))

    def test_unknown_simple_destination_effect_kills_and_fails_closed(self) -> None:
        dis = """
06001000 <_root>:
 6001000: e2 07 mov #7,r2
 6001002: 12 34 mystery r1,r2
 6001004: 00 0b rts
 6001006: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertEqual(
            [
                (effect.function, effect.address, effect.mnemonic, effect.operands)
                for effect in result.unresolved_effects
            ],
            [("_root", 0x6001002, "mystery", "r1,r2")],
        )

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

    def test_all_owner_seeds_share_one_state_map_and_build_global_indexes_once(self) -> None:
        dis = """
06001000 <_root>:
 6001000: e1 01 mov #1,r1
 6001002: a0 01 bra 6001008 <_root+0x8>
 6001004: 00 09 nop
 6001006: 00 09 nop
 6001008: 71 01 add #1,r1
 600100a: 00 0b rts
 600100c: 00 09 nop
"""
        sections = parse_readelf_sections(self.SECTIONS)
        owners = resolve_function_owners(parse_readelf_symbols(self.SYMBOLS, sections), sections)
        instructions = parse_instructions(dis)
        profile: dict[str, Counter[str]] = {}
        with patch(
            "verify_sh2_native_math.build_owner_address_map",
            wraps=build_owner_address_map,
        ) as owner_map_builder, patch(
            "verify_sh2_native_math.build_instruction_memory",
            wraps=build_instruction_memory,
        ) as memory_builder:
            result = analyze_code_only(
                instructions,
                owners,
                {"_root": {0x6001006}},
                {"_root"},
                profile_by_owner=profile,
            )
        self.assertEqual(owner_map_builder.call_count, 1)
        self.assertEqual(memory_builder.call_count, 1)
        self.assertEqual(result.unresolved_transfers, [])
        self.assertLess(profile["_root"]["worklist_states"], 20)

    def test_discovery_seed_does_not_poison_entry_reachable_indirect_target(self) -> None:
        dis = """
06001000 <_root>:
 6001000: d1 07 mov.l 6001020 <_child>,r1 ! 06001020 <_child>
 6001002: a0 01 bra 6001008 <_root+0x8>
 6001004: 00 09 nop
 6001006: 00 09 nop
 6001008: 41 2b jmp @r1
 600100a: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        sections = parse_readelf_sections(self.SECTIONS)
        owners = resolve_function_owners(parse_readelf_symbols(self.SYMBOLS, sections), sections)
        result = analyze_code_only(
            parse_instructions(dis),
            owners,
            {"_root": {0x6001006, 0x6001008}},
            {"_root"},
        )
        self.assertEqual(result.unresolved_transfers, [])
        self.assertIn(CallSite("_root", 0x6001008, "_child"), result.calls)
        observation = make_observation(
            mode="code-only",
            producer_commit="0" * 40,
            parser_path=Path(__file__).with_name("verify_sh2_native_math.py"),
            elf=Path(__file__),
            route_oracle_path=Path(__file__).with_name(
                "sh2_native_math_sim_route_oracle_v1.txt"
            ),
            contract_path=Path(__file__).with_name(
                "sh2_native_math_sim_audit_contract_v2.txt"
            ),
            contract=parse_audit_contract(
                "AUDIT_CONTRACT_VERSION 2\nEXPECTED_ROOT _root\nEXPECTED_TOTAL 0\n"
                "FORBIDDEN_CALLER _forbidden\n"
            ),
            root="_root",
            closure={"_root"},
            calls=result.calls,
            owners=owners,
            direct_facts=result.direct_calls,
            unresolved_transfers=result.unresolved_transfers,
        )
        self.assertEqual(observation["unresolved_indirect_transfers"], [])

    def test_entry_covered_tail_jmp_decoded_row_is_not_seeded(self) -> None:
        dis = """
06001000 <_root>:
 6001000: d1 07 mov.l 6001020 <_child>,r1 ! 06001020 <_child>
 6001002: 41 2b jmp @r1
 6001004: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        sections = parse_readelf_sections(self.SECTIONS)
        owners = resolve_function_owners(parse_readelf_symbols(self.SYMBOLS, sections), sections)
        result = analyze_code_only(
            parse_instructions(dis),
            owners,
            {"_root": {0x6001002}},
            {"_root"},
        )
        self.assertIn(CallSite("_root", 0x6001002, "_child"), result.calls)
        self.assertEqual(result.unresolved_transfers, [])

    def test_syntactic_delay_slot_without_program_state_still_seeds_tail_jmp(self) -> None:
        dis = """
06001000 <_root>:
 6001000: a0 06 bra 6001010 <_root+0x10>
 6001002: d1 07 mov.l 6001020 <_child>,r1 ! 06001020 <_child>
 6001004: 41 2b jmp @r1
 6001006: 00 09 nop
 6001010: 00 0b rts
 6001012: 00 09 nop
06001020 <_child>:
 6001020: d2 07 mov.l 6001040 <___mulsf3>,r2 ! 06001040 <___mulsf3>
 6001022: 42 0b jsr @r2
 6001024: 00 09 nop
 6001026: 00 0b rts
 6001028: 00 09 nop
06001040 <___mulsf3>:
 6001040: 00 0b rts
 6001042: 00 09 nop
"""
        sections = parse_readelf_sections(self.SECTIONS)
        owners = resolve_function_owners(parse_readelf_symbols(self.SYMBOLS, sections), sections)
        result = analyze_code_only(
            parse_instructions(dis),
            owners,
            {"_root": {0x6001002, 0x6001004}},
            {"_root", "_child"},
        )
        self.assertIn(0x6001002, result.code_addresses)
        self.assertIn(CallSite("_root", 0x6001004, "_child"), result.calls)
        graph: dict[str, set[str]] = {}
        for call in result.calls:
            if not is_native_math_helper(call.helper):
                graph.setdefault(call.caller, set()).add(call.helper)
        closure = route_reachable_functions(graph, {"_root"})
        self.assertIn("_child", closure)
        self.assertIn(
            CallSite("_child", 0x6001022, "___mulsf3"),
            [call for call in result.calls if call.caller in closure],
        )
        self.assertEqual(result.unresolved_transfers, [])

    def test_first_uncovered_seed_covers_and_suppresses_later_decoded_seed(self) -> None:
        dis = """
06001000 <_root>:
 6001000: 00 0b rts
 6001002: 00 09 nop
 6001004: 00 09 nop
 6001006: 00 09 nop
 6001008: 41 2b jmp @r1
 600100a: 00 09 nop
"""
        sections = parse_readelf_sections(self.SECTIONS)
        owners = resolve_function_owners(parse_readelf_symbols(self.SYMBOLS, sections), sections)
        result = analyze_code_only(
            parse_instructions(dis),
            owners,
            {"_root": {0x6001004, 0x6001006}},
            {"_root"},
        )
        self.assertEqual(len(result.unresolved_transfers), 1)
        transfer = result.unresolved_transfers[0]
        self.assertEqual(transfer.address, 0x6001008)
        self.assertEqual(
            transfer.contributing_seeds,
            ((0x6001004, "decodedline"),),
        )
        self.assertNotIn((0x6001006, "decodedline"), transfer.contributing_seeds)

    def test_phase_two_new_computed_edge_restarts_then_converges(self) -> None:
        dis = """
06001000 <_root>:
 6001000: d1 07 mov.l 6001020 <_child>,r1 ! 06001010 <_root+0x10>
 6001002: a0 01 bra 6001008 <_root+0x8>
 6001004: 00 09 nop
 6001006: 00 09 nop
 6001008: 41 2b jmp @r1
 600100a: 00 09 nop
 6001010: d2 03 mov.l 6001020 <_child>,r2 ! 06001020 <_child>
 6001012: 42 2b jmp @r2
 6001014: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        sections = parse_readelf_sections(self.SECTIONS)
        owners = resolve_function_owners(parse_readelf_symbols(self.SYMBOLS, sections), sections)
        profile: dict[str, Counter[str]] = {}
        progress: list[dict[str, object]] = []
        result = analyze_code_only(
            parse_instructions(dis),
            owners,
            {"_root": {0x6001006, 0x6001010}},
            {"_root"},
            profile_by_owner=profile,
            progress_callback=progress.append,
        )
        self.assertEqual(result.unresolved_transfers, [])
        self.assertIn(CallSite("_root", 0x6001012, "_child"), result.calls)
        self.assertEqual(profile["_root"]["phase2_discovery_restarts"], 1)
        self.assertEqual(
            [event["phase"] for event in progress if event["event"] == "phase_start"],
            ["discovery", "acceptance", "discovery", "acceptance"],
        )
        component_events = [
            event for event in progress if event["event"] == "components_complete"
        ]
        self.assertEqual(
            [event["acceptance_seeds"] for event in component_events], [2, 1]
        )
        self.assertTrue(all(int(event["weak_components"]) >= 1 for event in component_events))
        restart = next(event for event in progress if event["event"] == "rediscovery_restart")
        self.assertEqual(restart["owners"], ["_root"])
        owner_progress = [
            event for event in progress
            if event["event"] == "owner_complete" and event["owner"] == "_root"
        ]
        self.assertTrue(all(int(event["states"]) > 0 for event in owner_progress))
        self.assertTrue(all("edges" in event and "elapsed_ms" in event for event in progress))

    def test_phase_two_new_computed_edge_honors_restart_cap(self) -> None:
        dis = """
06001000 <_root>:
 6001000: d1 07 mov.l 6001020 <_child>,r1 ! 06001010 <_root+0x10>
 6001002: a0 01 bra 6001008 <_root+0x8>
 6001004: 00 09 nop
 6001006: 00 09 nop
 6001008: 41 2b jmp @r1
 600100a: 00 09 nop
 6001010: 00 0b rts
 6001012: 00 09 nop
"""
        sections = parse_readelf_sections(self.SECTIONS)
        owners = resolve_function_owners(parse_readelf_symbols(self.SYMBOLS, sections), sections)
        with self.assertRaisesRegex(ValueError, "discovery restart cap"):
            analyze_code_only(
                parse_instructions(dis),
                owners,
                {"_root": {0x6001006, 0x6001010}},
                {"_root"},
                max_discovery_restarts=0,
            )

    def test_discovery_coalesces_many_equivalent_seed_states(self) -> None:
        root_start = 0x6001000
        child_start = 0x6001400
        nop_addresses = list(range(root_start + 2, root_start + 258, 2))
        call_address = nop_addresses[-1] + 2
        rows = [
            "06001000 <_root>:",
            " 6001000: d8 07 mov.l 6001020 <_root+0x20>,r8 ! 06001400 <_child>",
            *(f" {address:x}: 00 09 nop" for address in nop_addresses),
            f" {call_address:x}: 48 0b jsr @r8",
            f" {call_address + 2:x}: 00 09 nop",
            f" {call_address + 4:x}: 00 0b rts",
            f" {call_address + 6:x}: 00 09 nop",
            "06001400 <_child>:",
            " 6001400: 00 0b rts",
            " 6001402: 00 09 nop",
        ]
        instructions = parse_instructions("\n".join(rows))
        owners = (
            FunctionOwner("_root", root_start, child_start, 1),
            FunctionOwner("_child", child_start, child_start + 16, 1),
        )
        progress: list[dict[str, object]] = []
        result = analyze_code_only(
            instructions,
            owners,
            {"_root": set(nop_addresses)},
            {"_root"},
            progress_callback=progress.append,
        )
        discovery = next(
            event for event in progress
            if event["event"] == "phase_complete" and event["phase"] == "discovery"
        )
        root_instruction_count = len(nop_addresses) + 5
        self.assertLessEqual(int(discovery["states"]), root_instruction_count * 2)
        self.assertIn(CallSite("_root", call_address, "_child"), result.calls)
        self.assertEqual(result.unresolved_transfers, [])

    def test_decoded_only_block_that_merges_into_entry_cfg_is_seeded(self) -> None:
        dis = """
06001000 <_root>:
 6001000: a0 06 bra 6001010 <_root+0x10>
 6001002: 00 09 nop
 6001004: d1 06 mov.l 6001020 <_child>,r1 ! 06001020 <_child>
 6001006: 41 0b jsr @r1
 6001008: 00 09 nop
 600100a: a0 01 bra 6001010 <_root+0x10>
 600100c: 00 09 nop
 6001010: 00 0b rts
 6001012: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        sections = parse_readelf_sections(self.SECTIONS)
        owners = resolve_function_owners(parse_readelf_symbols(self.SYMBOLS, sections), sections)
        result = analyze_code_only(
            parse_instructions(dis),
            owners,
            {"_root": {0x6001004}},
            {"_root"},
        )
        self.assertIn(CallSite("_root", 0x6001006, "_child"), result.calls)
        self.assertEqual(result.unresolved_transfers, [])

    def test_infeasible_discovery_branch_does_not_suppress_decoded_block(self) -> None:
        dis = """
06001000 <_root>:
 6001000: e0 ff mov #-1,r0
 6001002: 40 11 cmp/pz r0
 6001004: 89 02 bt 600100c <_root+0xc>
 6001006: a0 04 bra 6001012 <_root+0x12>
 6001008: 00 09 nop
 600100a: 00 09 nop
 600100c: d1 04 mov.l 6001020 <_child>,r1 ! 06001020 <_child>
 600100e: 41 0b jsr @r1
 6001010: 00 09 nop
 6001012: 00 0b rts
 6001014: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        sections = parse_readelf_sections(self.SECTIONS)
        owners = resolve_function_owners(parse_readelf_symbols(self.SYMBOLS, sections), sections)
        result = analyze_code_only(
            parse_instructions(dis),
            owners,
            {"_root": {0x6001004, 0x600100C}},
            {"_root"},
        )
        self.assertIn(CallSite("_root", 0x600100E, "_child"), result.calls)
        self.assertEqual(result.unresolved_transfers, [])

    def test_decoded_lane_contributes_target_at_entry_evaluated_join(self) -> None:
        dis = """
06001000 <_root>:
 6001000: d1 07 mov.l 6001020 <_default>,r1 ! 06001020 <_default>
 6001002: a0 05 bra 6001010 <_root+0x10>
 6001004: 00 09 nop
 6001008: d1 05 mov.l 6001030 <_child>,r1 ! 06001030 <_child>
 600100a: a0 01 bra 6001010 <_root+0x10>
 600100c: 00 09 nop
 6001010: 41 0b jsr @r1
 6001012: 00 09 nop
 6001014: 00 0b rts
 6001016: 00 09 nop
06001020 <_default>:
 6001020: 00 0b rts
 6001022: 00 09 nop
06001030 <_child>:
 6001030: 00 0b rts
 6001032: 00 09 nop
"""
        sections = parse_readelf_sections(self.SECTIONS)
        owners = resolve_function_owners(parse_readelf_symbols(self.SYMBOLS, sections), sections)
        result = analyze_code_only(
            parse_instructions(dis),
            owners,
            {"_root": {0x6001008}},
            {"_root"},
        )
        self.assertEqual(
            [call for call in result.calls if call.address == 0x6001010],
            [
                CallSite("_root", 0x6001010, "_child"),
                CallSite("_root", 0x6001010, "_zero_alias"),
            ],
        )
        self.assertEqual(result.unresolved_transfers, [])

    def test_each_uncovered_decoded_arm_in_a_weak_component_is_seeded(self) -> None:
        dis = """
06001000 <_root>:
 6001000: 00 0b rts
 6001002: 00 09 nop
 6001004: a0 05 bra 6001012 <_root+0x12>
 6001006: 00 09 nop
 6001008: d1 05 mov.l 6001020 <_child>,r1 ! 06001020 <_child>
 600100a: 41 0b jsr @r1
 600100c: 00 09 nop
 600100e: a0 00 bra 6001012 <_root+0x12>
 6001010: 00 09 nop
 6001012: 00 0b rts
 6001014: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        sections = parse_readelf_sections(self.SECTIONS)
        owners = resolve_function_owners(parse_readelf_symbols(self.SYMBOLS, sections), sections)
        result = analyze_code_only(
            parse_instructions(dis),
            owners,
            {"_root": {0x6001004, 0x6001008}},
            {"_root"},
        )
        self.assertIn(CallSite("_root", 0x600100A, "_child"), result.calls)
        self.assertEqual(result.unresolved_transfers, [])

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

    def test_dereferenced_callback_jmp_stays_unresolved_after_flag_only_tst(self) -> None:
        dis = """
06001000 <_root>:
 6001000: d2 04 mov.l 6001014 <_root+0x14>,r2 ! 06002000 <_callback_slot>
 6001002: 62 22 mov.l @r2,r2
 6001004: 22 28 tst r2,r2
 6001006: 42 2b jmp @r2
 6001008: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertEqual(result.direct_calls, [])
        self.assertEqual(
            [(item.address, item.mnemonic) for item in result.unresolved_transfers],
            [(0x6001006, "jmp")],
        )

    def test_resolved_cross_owner_tail_jmp_is_a_closure_edge_and_direct_fact(self) -> None:
        dis = """
06001000 <_root>:
 6001000: d1 07 mov.l 6001020 <_child>,r1 ! 06001020 <_child>
 6001002: 41 2b jmp @r1
 6001004: 00 09 nop
06001020 <_child>:
 6001020: d2 07 mov.l 6001040 <___mulsf3>,r2 ! 06001040 <___mulsf3>
 6001022: 42 0b jsr @r2
 6001024: 00 09 nop
 6001026: 00 0b rts
 6001028: 00 09 nop
06001040 <___mulsf3>:
 6001040: 00 0b rts
 6001042: 00 09 nop
"""
        result = self.analyze(dis)
        graph: dict[str, set[str]] = {}
        for call in result.calls:
            if not is_native_math_helper(call.helper):
                graph.setdefault(call.caller, set()).add(call.helper)
        closure = route_reachable_functions(graph, {"_root"})
        self.assertIn(CallSite("_root", 0x6001002, "_child"), result.calls)
        self.assertIn("_child", closure)
        self.assertIn(
            CallSite("_child", 0x6001022, "___mulsf3"),
            [call for call in result.calls if call.caller in closure],
        )
        self.assertTrue(any(
            fact.caller == "_root" and fact.callee == "_child"
            for fact in result.direct_calls
        ))

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
 6001010: 00 0b rts
 6001012: 00 09 nop
 6001014: 00 0b rts
 6001016: 00 09 nop
 6001018: 00 0b rts
 600101a: 00 09 nop
 600101c: 00 0b rts
 600101e: 00 09 nop
 6001030: 02 06 .word 0x0206
 6001032: 0a 0e .word 0x0a0e
"""
        sections = parse_readelf_sections(self.SECTIONS)
        owners = resolve_function_owners(
            parse_readelf_symbols(
                "   1: 06001000 32 FUNC GLOBAL DEFAULT 1 _root\n",
                sections,
            ),
            sections,
        )
        result = analyze_code_only(parse_instructions(dis), owners)
        self.assertEqual(result.unresolved_transfers, [])

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

    def test_bf_mnemonic_reaches_decoded_arm_with_entry_defined_target(self) -> None:
        dis = """
06001000 <_root>:
 6001000: d8 07 mov.l 6001020 <_child>,r8 ! 06001020 <_child>
 6001002: 20 08 tst r0,r0
 6001004: 8b 01 bf 600100a <_root+0xa>
 6001006: 00 0b rts
 6001008: 00 09 nop
 600100a: 48 0b jsr @r8
 600100c: 00 09 nop
 600100e: 00 0b rts
 6001010: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        result = self.analyze(dis, "x.c 7 0x0600100a\n")
        self.assertIn(CallSite("_root", 0x600100A, "_child"), result.calls)
        self.assertEqual(result.unresolved_transfers, [])

    def test_overwriting_compared_register_invalidates_stale_predicate_only(self) -> None:
        dis = """
06001000 <_root>:
 6001000: e0 ff mov #-1,r0
 6001002: 40 11 cmp/pz r0
 6001004: d0 06 mov.l 6001020 <_child>,r0 ! 06001020 <_child>
 6001006: 8f 04 bf.s 6001012 <_root+0x12>
 6001008: 00 09 nop
 600100a: 40 0b jsr @r0
 600100c: 00 09 nop
 600100e: a0 05 bra 600101c <_root+0x1c>
 6001010: 00 09 nop
 6001012: 40 0b jsr @r0
 6001014: 00 09 nop
 6001016: a0 01 bra 600101c <_root+0x1c>
 6001018: 00 09 nop
 600101a: 00 09 nop
 600101c: 00 0b rts
 600101e: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        result = self.analyze(dis)
        self.assertEqual(
            [call.address for call in result.calls if call.caller == "_root"],
            [0x600100A, 0x6001012],
        )
        self.assertEqual(result.unresolved_transfers, [])

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

    def test_unnamed_local_notype_symbol_is_not_an_island_candidate(self) -> None:
        symbols = """
   1: 06001000 32 FUNC GLOBAL DEFAULT 1 _root
   2: 06001100 0 NOTYPE LOCAL DEFAULT 1
   3: 06001120 16 FUNC GLOBAL DEFAULT 1 _child
"""
        sections = parse_readelf_sections(self.SECTIONS)
        parsed = parse_readelf_symbols(symbols, sections)
        owners = resolve_function_owners(parsed, sections)
        self.assertEqual(resolve_local_islands(parsed, sections, owners), ())

    def test_different_local_notype_names_at_same_address_are_ambiguous(self) -> None:
        symbols = """
   1: 06001000 32 FUNC GLOBAL DEFAULT 1 _root
   2: 06001100 0 NOTYPE LOCAL DEFAULT 1 island_a
   3: 06001100 0 NOTYPE LOCAL DEFAULT 1 island_b
   4: 06001120 16 FUNC GLOBAL DEFAULT 1 _child
"""
        sections = parse_readelf_sections(self.SECTIONS)
        parsed = parse_readelf_symbols(symbols, sections)
        owners = resolve_function_owners(parsed, sections)
        with self.assertRaisesRegex(ValueError, "ambiguous local-label aliases"):
            resolve_local_islands(parsed, sections, owners)

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
