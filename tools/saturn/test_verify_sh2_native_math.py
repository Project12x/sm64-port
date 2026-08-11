#!/usr/bin/env python3
"""Regression tests for the SH-2 native-math census gate."""

from __future__ import annotations

import hashlib
import io
import json
import os
import re
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest.mock import patch

import verify_sh2_native_math as verifier
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
    GOAL_AUDIT_CONTRACT_V3_SHA256,
    GOAL_AUDIT_CONTRACT_V4_SHA256,
    verify_audit_contract_integrity,
    verify_audit_contract_target,
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

    def test_analysis_candidates_seed_each_declared_indirect_edge_endpoint(self) -> None:
        oracle = parse_route_oracle(
            "ROUTE_ORACLE_VERSION 1\nROOT _root\n"
            "INDIRECT_EDGE _dispatcher _callback\n"
        )
        self.assertEqual(
            verifier.route_analysis_candidate_names(
                {"_root": {"_direct_child"}}, (oracle,)
            ),
            {"_root", "_direct_child", "_dispatcher", "_callback"},
        )

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

    def test_source_manifest_confirms_dynamic_edge_with_direct_fallback(self) -> None:
        oracle = parse_route_oracle(
            "ROUTE_ORACLE_VERSION 1\nROOT _root\n"
            "STATIC_MANIFEST_EDGE _dispatcher _callback\n"
            "INDIRECT_EDGE _dispatcher _callback\n"
        )
        result = audit_indirect_edges(
            {"_root": {"_dispatcher", "_callback"}}, oracle,
            (
                self.indirect_owner("_root", 0x6001000),
                self.indirect_owner("_dispatcher", 0x6001020),
                self.indirect_owner("_callback", 0x6001040),
            ),
            [UnresolvedTransfer("_dispatcher", 0x6001024, "jsr", "r1")],
        )
        self.assertEqual(
            result.closure,
            frozenset({"_root", "_dispatcher", "_callback"}),
        )
        self.assertEqual(result.unlisted_transfers, ())

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
        required_geo_edges = {
            ("_saturn_geo_enter_background", "_geo_skybox_main"),
            ("_saturn_geo_enter_perspective", "_geo_camera_fov"),
            ("_saturn_geo_enter_camera", "_geo_camera_main"),
            ("_saturn_geo_enter_generated_list", "_geo_envfx_main"),
            ("_saturn_geo_enter_generated_list", "_geo_cannon_circle_base"),
        }
        self.assertEqual(
            {callback for _, callback in required_geo_edges}, bob_callbacks,
        )
        required_edges = required_geo_edges | {
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
                edge for edge in oracle.static_manifest_edges
                if edge[0] in {"_play_cutscene", "_play_mode_change_level"}
            },
            set(),
        )
        self.assertEqual(
            {
                (dispatcher, callback)
                for dispatcher, callback in oracle.static_manifest_edges
                if dispatcher in {owner for owner, _ in required_geo_edges}
                and callback in bob_callbacks
            },
            required_geo_edges,
        )
        self.assertEqual(
            {
                callback
                for dispatcher, callback in oracle.static_manifest_edges
                if dispatcher == "_level_script_execute"
            },
            set(sourceboot_callbacks.values()),
        )

    def test_checked_in_sim_oracle_matches_all_source_derived_dispatcher_sets(self) -> None:
        repo_root = Path(__file__).parents[2]
        oracle_text = Path(__file__).with_name(
            "sh2_native_math_sim_route_oracle_v1.txt"
        ).read_text(encoding="utf-8")
        _assert_bob_source_oracle_matches(self, repo_root, oracle_text)

    def test_source_manifest_comparison_rejects_omitted_and_underived_callbacks(self) -> None:
        repo_root = Path(__file__).parents[2]
        oracle_text = Path(__file__).with_name(
            "sh2_native_math_sim_route_oracle_v1.txt"
        ).read_text(encoding="utf-8")
        omitted = oracle_text
        for line in (
            "STATIC_MANIFEST_EDGE _saturn_geo_enter_generated_list _geo_envfx_main\n",
            "INDIRECT_EDGE _saturn_geo_enter_generated_list _geo_envfx_main\n",
        ):
            self.assertEqual(omitted.count(line), 1)
            omitted = omitted.replace(line, "", 1)
        with self.assertRaises(AssertionError):
            _assert_bob_source_oracle_matches(self, repo_root, omitted)

        static_anchor = (
            "STATIC_MANIFEST_EDGE _sm64_saturn_source_runtime_read_controllers "
            "_controller_saturn_read\n"
        )
        indirect_anchor = (
            "INDIRECT_EDGE _sm64_saturn_source_runtime_read_controllers "
            "_controller_saturn_read\n"
        )
        underived_static = (
            "STATIC_MANIFEST_EDGE _saturn_geo_enter_camera _geo_skybox_main\n"
        )
        underived_indirect = (
            "INDIRECT_EDGE _saturn_geo_enter_camera _geo_skybox_main\n"
        )
        mutations = (
            oracle_text.replace(
                static_anchor, underived_static + static_anchor, 1
            ),
            oracle_text.replace(
                indirect_anchor, underived_indirect + indirect_anchor, 1
            ),
            oracle_text.replace(
                static_anchor, underived_static + static_anchor, 1
            ).replace(
                indirect_anchor, underived_indirect + indirect_anchor, 1
            ),
        )
        for mutated in mutations:
            with self.subTest(
                static=underived_static in mutated,
                indirect=underived_indirect in mutated,
            ):
                with self.assertRaises(AssertionError):
                    _assert_bob_source_oracle_matches(self, repo_root, mutated)

    def test_pinned_bob_camera_trigger_table_derives_empty_and_stays_undeclared(self) -> None:
        repo_root = Path(__file__).parents[2]
        self.assertEqual(_derive_bob_camera_trigger_targets(repo_root), frozenset())
        oracle = parse_route_oracle(
            Path(__file__).with_name(
                "sh2_native_math_sim_route_oracle_v1.txt"
            ).read_text(encoding="utf-8")
        )
        self.assertEqual(
            {
                edge for edge in oracle.static_manifest_edges
                if edge[0] == "_camera_course_processing"
            },
            set(),
        )
        self.assertEqual(
            {
                edge for edge in oracle.indirect_edges
                if edge[0] == "_camera_course_processing"
            },
            set(),
        )
        transfers = (
            UnresolvedTransfer(
                "_camera_course_processing", 0x601011E, "jsr", "r1"
            ),
            UnresolvedTransfer(
                "_camera_course_processing", 0x601013C, "jsr", "r1"
            ),
        )
        result = audit_indirect_edges(
            {"_root": {"_camera_course_processing"}},
            parse_route_oracle("ROUTE_ORACLE_VERSION 1\nROOT _root\n"),
            (
                self.indirect_owner("_root", 0x6000000),
                self.indirect_owner("_camera_course_processing", 0x6010000),
            ),
            transfers,
        )
        self.assertEqual(result.unlisted_transfers, transfers)

    def test_post_manifest_dispatchers_consume_only_matching_exact_dynamic_sites(self) -> None:
        sites = (
            ("_init_graph_node_perspective", 84),
            ("_init_graph_node_switch_case", 74),
            ("_init_graph_node_camera", 90),
            ("_init_graph_node_generated", 60),
            ("_init_graph_node_background", 70),
            ("_init_graph_node_held_object", 78),
            ("_saturn_geo_enter_perspective", 84),
            ("_saturn_geo_enter_switch", 74),
            ("_saturn_geo_enter_camera", 90),
            ("_saturn_geo_enter_generated_list", 60),
            ("_saturn_geo_enter_background", 70),
            ("_saturn_geo_enter_held_object", 78),
            ("_sm64_saturn_geo_walk_runtime_run", 164),
            ("_sm64_saturn_geo_walk_runtime_run", 492),
            ("_sm64_saturn_geo_walk_runtime_run", 524),
        )
        for index, (dispatcher, offset) in enumerate(sites):
            with self.subTest(dispatcher=dispatcher):
                start = 0x6010000 + index * 0x100
                callback = sorted(POST_MANIFEST_DISPATCHER_TARGETS[dispatcher])[0]
                transfer = UnresolvedTransfer(
                    dispatcher, start + offset, "jsr", "r1"
                )
                oracle = parse_route_oracle(
                    "ROUTE_ORACLE_VERSION 1\nROOT _root\n"
                    f"STATIC_MANIFEST_EDGE {dispatcher} {callback}\n"
                    f"INDIRECT_EDGE {dispatcher} {callback}\n"
                )
                owners = (
                    self.indirect_owner("_root", 0x6000000),
                    self.indirect_owner(dispatcher, start),
                    self.indirect_owner(callback, 0x6020000),
                )
                declared = audit_indirect_edges(
                    {"_root": {dispatcher}}, oracle, owners, [transfer]
                )
                self.assertEqual(declared.unlisted_transfers, ())

                wrong_dispatcher = "_wrong_dispatcher"
                wrong_oracle = parse_route_oracle(
                    "ROUTE_ORACLE_VERSION 1\nROOT _root\n"
                    f"STATIC_MANIFEST_EDGE {wrong_dispatcher} {callback}\n"
                    f"INDIRECT_EDGE {wrong_dispatcher} {callback}\n"
                )
                with self.assertRaisesRegex(ValueError, "unconsumed INDIRECT_EDGE"):
                    audit_indirect_edges(
                        {"_root": {dispatcher, wrong_dispatcher}},
                        wrong_oracle,
                        owners + (
                            self.indirect_owner(wrong_dispatcher, 0x6030000),
                        ),
                        [transfer],
                    )

    def test_static_near_match_remains_unlisted_in_each_post_manifest_family(self) -> None:
        sites = {
            "_init_graph_node_perspective": 84,
            "_init_graph_node_switch_case": 74,
            "_init_graph_node_camera": 90,
            "_init_graph_node_generated": 60,
            "_init_graph_node_background": 70,
            "_init_graph_node_held_object": 78,
            "_saturn_geo_enter_perspective": 84,
            "_saturn_geo_enter_switch": 74,
            "_saturn_geo_enter_camera": 90,
            "_saturn_geo_enter_generated_list": 60,
            "_saturn_geo_enter_background": 70,
            "_saturn_geo_enter_held_object": 78,
            "_sm64_saturn_geo_walk_runtime_run": 164,
        }
        for index, (dispatcher, offset) in enumerate(sites.items()):
            with self.subTest(dispatcher=dispatcher):
                start = 0x6040000 + index * 0x100
                callback = sorted(POST_MANIFEST_DISPATCHER_TARGETS[dispatcher])[0]
                dynamic = UnresolvedTransfer(
                    dispatcher, start + offset, "jsr", "r1"
                )
                static = UnresolvedTransfer(
                    dispatcher, start + offset + 2, "jsr", "r7",
                    stack_source_offsets=(-32,),
                    stack_store_addresses=(start + offset - 8,),
                    provenance="static",
                )
                oracle = parse_route_oracle(
                    "ROUTE_ORACLE_VERSION 1\nROOT _root\n"
                    f"STATIC_MANIFEST_EDGE {dispatcher} {callback}\n"
                    f"INDIRECT_EDGE {dispatcher} {callback}\n"
                )
                result = audit_indirect_edges(
                    {"_root": {dispatcher}},
                    oracle,
                    (
                        self.indirect_owner("_root", 0x6000000),
                        self.indirect_owner(dispatcher, start),
                        self.indirect_owner(callback, 0x6050000),
                    ),
                    (dynamic, static),
                )
                self.assertEqual(result.unlisted_transfers, (static,))

    def test_checked_source_group_allows_shared_camera_callback_contribution(self) -> None:
        callback = "_geo_camera_main"
        dispatchers = (
            "_geo_call_global_function_nodes_helper",
            "_saturn_geo_enter_camera",
        )
        self.assertTrue(all(
            callback in BOB_DISPATCHER_TARGETS[dispatcher]
            for dispatcher in dispatchers
        ))
        oracle_text = "ROUTE_ORACLE_VERSION 1\nROOT _root\n" + "".join(
            f"STATIC_MANIFEST_EDGE {dispatcher} {callback}\n"
            f"INDIRECT_EDGE {dispatcher} {callback}\n"
            for dispatcher in dispatchers
        )
        result = audit_indirect_edges(
            {"_root": set(dispatchers)},
            parse_route_oracle(oracle_text),
            (
                self.indirect_owner("_root", 0x6000000),
                self.indirect_owner(dispatchers[0], 0x6060000),
                self.indirect_owner(dispatchers[1], 0x6060100),
                self.indirect_owner(callback, 0x6060200),
            ),
            (
                UnresolvedTransfer(dispatchers[0], 0x6060028, "jsr", "r1"),
                UnresolvedTransfer(dispatchers[1], 0x606015A, "jsr", "r1"),
            ),
        )
        self.assertIn(callback, result.closure)
        self.assertEqual(result.unlisted_transfers, ())

    def test_four_wave1_dispatchers_consume_only_declared_dynamic_transfers(self) -> None:
        sites = {
            "_geo_call_global_function_nodes_helper": (0x6001000, 40),
            "_level_cmd_call": (0x6001100, 18),
            "_level_cmd_call_loop": (0x6001200, 18),
            "_process_geo_layout": (0x6001300, 90),
        }
        for dispatcher, (start, offset) in sites.items():
            with self.subTest(dispatcher=dispatcher):
                callback = sorted(BOB_DISPATCHER_TARGETS[dispatcher])[0]
                transfer = UnresolvedTransfer(
                    dispatcher, start + offset, "jsr", "r1"
                )
                owners = (
                    self.indirect_owner("_root", 0x6000000),
                    self.indirect_owner(dispatcher, start),
                    self.indirect_owner(callback, 0x6002000),
                )
                graph = {"_root": {dispatcher}}
                undeclared = audit_indirect_edges(
                    graph,
                    parse_route_oracle("ROUTE_ORACLE_VERSION 1\nROOT _root\n"),
                    owners,
                    [transfer],
                )
                self.assertEqual(undeclared.unlisted_transfers, (transfer,))

                oracle = parse_route_oracle(
                    "ROUTE_ORACLE_VERSION 1\nROOT _root\n"
                    f"STATIC_MANIFEST_EDGE {dispatcher} {callback}\n"
                    f"INDIRECT_EDGE {dispatcher} {callback}\n"
                )
                declared = audit_indirect_edges(
                    graph, oracle, owners, [transfer]
                )
                self.assertEqual(declared.unlisted_transfers, ())

    def test_static_transfer_remains_unlisted_in_each_wave1_dispatcher(self) -> None:
        sites = {
            "_geo_call_global_function_nodes_helper": (0x6001000, 40),
            "_level_cmd_call": (0x6001100, 18),
            "_level_cmd_call_loop": (0x6001200, 18),
            "_process_geo_layout": (0x6001300, 90),
        }
        for dispatcher, (start, offset) in sites.items():
            with self.subTest(dispatcher=dispatcher):
                callback = sorted(BOB_DISPATCHER_TARGETS[dispatcher])[0]
                dynamic = UnresolvedTransfer(
                    dispatcher, start + offset, "jsr", "r1"
                )
                static = UnresolvedTransfer(
                    dispatcher, start + offset + 2, "jsr", "r7",
                    stack_source_offsets=(-32,),
                    stack_store_addresses=(start + offset - 8,),
                    provenance="static",
                )
                oracle = parse_route_oracle(
                    "ROUTE_ORACLE_VERSION 1\nROOT _root\n"
                    f"STATIC_MANIFEST_EDGE {dispatcher} {callback}\n"
                    f"INDIRECT_EDGE {dispatcher} {callback}\n"
                )
                result = audit_indirect_edges(
                    {"_root": {dispatcher}},
                    oracle,
                    (
                        self.indirect_owner("_root", 0x6000000),
                        self.indirect_owner(dispatcher, start),
                        self.indirect_owner(callback, 0x6002000),
                    ),
                    [dynamic, static],
                )
                self.assertEqual(result.unlisted_transfers, (static,))

    def test_phase_a_dispatcher_granularity_applies_complete_bob_set_per_site(self) -> None:
        dispatcher = "_process_geo_layout"
        callbacks = BOB_DISPATCHER_TARGETS[dispatcher]
        oracle_text = "ROUTE_ORACLE_VERSION 1\nROOT _root\n" + "".join(
            f"STATIC_MANIFEST_EDGE {dispatcher} {callback}\n"
            f"INDIRECT_EDGE {dispatcher} {callback}\n"
            for callback in sorted(callbacks)
        )
        transfers = (
            UnresolvedTransfer(dispatcher, 0x600135A, "jsr", "r1"),
            UnresolvedTransfer(dispatcher, 0x600135C, "jsr", "r2"),
        )
        result = audit_indirect_edges(
            {"_root": {dispatcher}},
            parse_route_oracle(oracle_text),
            (
                self.indirect_owner("_root", 0x6000000),
                self.indirect_owner(dispatcher, 0x6001300),
                *(self.indirect_owner(callback, 0x6002000 + index * 0x20)
                  for index, callback in enumerate(sorted(callbacks))),
            ),
            transfers,
        )
        self.assertTrue(callbacks <= result.closure)
        self.assertEqual(result.unlisted_transfers, ())

    def test_source_manifest_allows_one_callback_required_by_two_dispatchers(self) -> None:
        callback = "_lvl_init_or_update"
        dispatchers = ("_level_cmd_call", "_level_cmd_call_loop")
        oracle_text = "ROUTE_ORACLE_VERSION 1\nROOT _root\n" + "".join(
            f"STATIC_MANIFEST_EDGE {dispatcher} {callback}\n"
            f"INDIRECT_EDGE {dispatcher} {callback}\n"
            for dispatcher in dispatchers
        )
        result = audit_indirect_edges(
            {"_root": set(dispatchers)},
            parse_route_oracle(oracle_text),
            (
                self.indirect_owner("_root", 0x6000000),
                self.indirect_owner(dispatchers[0], 0x6001100),
                self.indirect_owner(dispatchers[1], 0x6001200),
                self.indirect_owner(callback, 0x6002000),
            ),
            (
                UnresolvedTransfer(dispatchers[0], 0x6001112, "jsr", "r1"),
                UnresolvedTransfer(dispatchers[1], 0x6001212, "jsr", "r1"),
            ),
        )
        self.assertIn(callback, result.closure)
        self.assertEqual(result.unlisted_transfers, ())

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


POST_MANIFEST_DISPATCHER_TARGETS = {
    "_init_graph_node_perspective": frozenset({"_geo_camera_fov"}),
    "_init_graph_node_switch_case": frozenset({
        "_geo_switch_anim_state",
        "_geo_switch_mario_cap_effect",
        "_geo_switch_mario_cap_on_off",
        "_geo_switch_mario_eyes",
        "_geo_switch_mario_hand",
        "_geo_switch_mario_stand_run",
    }),
    "_init_graph_node_camera": frozenset({"_geo_camera_main"}),
    "_init_graph_node_generated": frozenset({
        "_geo_cannon_circle_base",
        "_geo_envfx_main",
        "_geo_mario_hand_foot_scaler",
        "_geo_mario_head_rotation",
        "_geo_mario_rotate_wing_cap_wings",
        "_geo_mario_tilt_torso",
        "_geo_mirror_mario_backface_culling",
        "_geo_mirror_mario_set_alpha",
        "_geo_move_mario_part_from_parent",
        "_geo_scale_bowser_key",
        "_geo_update_held_mario_pos",
        "_geo_update_layer_transparency",
    }),
    "_init_graph_node_background": frozenset({"_geo_skybox_main"}),
    "_init_graph_node_held_object": frozenset({
        "_geo_switch_mario_hand_grab_pos"
    }),
    "_saturn_geo_enter_perspective": frozenset({"_geo_camera_fov"}),
    "_saturn_geo_enter_switch": frozenset({
        "_geo_switch_anim_state",
        "_geo_switch_mario_cap_effect",
        "_geo_switch_mario_cap_on_off",
        "_geo_switch_mario_eyes",
        "_geo_switch_mario_hand",
        "_geo_switch_mario_stand_run",
    }),
    "_saturn_geo_enter_camera": frozenset({"_geo_camera_main"}),
    "_saturn_geo_enter_generated_list": frozenset({
        "_geo_cannon_circle_base",
        "_geo_envfx_main",
        "_geo_mario_hand_foot_scaler",
        "_geo_mario_head_rotation",
        "_geo_mario_rotate_wing_cap_wings",
        "_geo_mario_tilt_torso",
        "_geo_mirror_mario_backface_culling",
        "_geo_mirror_mario_set_alpha",
        "_geo_move_mario_part_from_parent",
        "_geo_scale_bowser_key",
        "_geo_update_held_mario_pos",
        "_geo_update_layer_transparency",
    }),
    "_saturn_geo_enter_background": frozenset({"_geo_skybox_main"}),
    "_saturn_geo_enter_held_object": frozenset({
        "_geo_switch_mario_hand_grab_pos"
    }),
    "_sm64_saturn_geo_walk_runtime_run": frozenset({
        "_saturn_geo_walk_dispatch",
        "_saturn_geo_walk_enter",
        "_saturn_geo_walk_leave",
    }),
}


BOB_DISPATCHER_TARGETS = {
    "_geo_call_global_function_nodes_helper": frozenset({
        "_geo_camera_fov",
        "_geo_camera_main",
        "_geo_cannon_circle_base",
        "_geo_envfx_main",
        "_geo_skybox_main",
    }),
    "_level_cmd_call": frozenset({
        "_lvl_init_from_save_file",
        "_lvl_init_or_update",
        "_lvl_set_current_level",
        "_sourceboot_mark_save_file_exists",
    }),
    "_level_cmd_call_loop": frozenset({"_lvl_init_or_update"}),
    "_process_geo_layout": frozenset({
        "_geo_layout_cmd_branch",
        "_geo_layout_cmd_branch_and_link",
        "_geo_layout_cmd_close_node",
        "_geo_layout_cmd_end",
        "_geo_layout_cmd_node_animated_part",
        "_geo_layout_cmd_node_background",
        "_geo_layout_cmd_node_billboard",
        "_geo_layout_cmd_node_camera",
        "_geo_layout_cmd_node_culling_radius",
        "_geo_layout_cmd_node_display_list",
        "_geo_layout_cmd_node_generated",
        "_geo_layout_cmd_node_held_obj",
        "_geo_layout_cmd_node_level_of_detail",
        "_geo_layout_cmd_node_master_list",
        "_geo_layout_cmd_node_object_parent",
        "_geo_layout_cmd_node_ortho_projection",
        "_geo_layout_cmd_node_perspective",
        "_geo_layout_cmd_node_root",
        "_geo_layout_cmd_node_rotation",
        "_geo_layout_cmd_node_scale",
        "_geo_layout_cmd_node_shadow",
        "_geo_layout_cmd_node_start",
        "_geo_layout_cmd_node_switch_case",
        "_geo_layout_cmd_node_translation",
        "_geo_layout_cmd_node_translation_rotation",
        "_geo_layout_cmd_open_node",
        "_geo_layout_cmd_return",
    }),
    **POST_MANIFEST_DISPATCHER_TARGETS,
}


def _strip_c_comments(text: str) -> str:
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.DOTALL)


def _derive_bob_camera_trigger_targets(repo_root: Path) -> frozenset[str]:
    """Derive the camera-trigger events selected by sourceboot's level."""
    sourceboot = _strip_c_comments(
        (repo_root / "src/port/saturn/sourceboot/source_entry.c").read_text(
            encoding="utf-8"
        )
    )
    selected_levels = frozenset(re.findall(
        r"\bSET_REG\s*\(\s*(LEVEL_[A-Z0-9_]+)\s*\)", sourceboot
    ))
    if selected_levels != frozenset({"LEVEL_BOB"}):
        raise ValueError(
            "sourceboot must select exactly LEVEL_BOB for the pinned route"
        )
    selected_level = next(iter(selected_levels))

    level_defines = _strip_c_comments(
        (repo_root / "levels/level_defines.h").read_text(encoding="utf-8")
    )
    selected_tables: list[str] = []
    for match in re.finditer(
        r"^\s*DEFINE_LEVEL\s*\((.*?)\)\s*$",
        level_defines,
        flags=re.MULTILINE,
    ):
        fields = tuple(field.strip() for field in match.group(1).split(","))
        if len(fields) != 11:
            raise ValueError("cannot derive DEFINE_LEVEL camera-table field")
        if fields[1] == selected_level:
            selected_tables.append(fields[10])
    if len(selected_tables) != 1:
        raise ValueError(
            f"expected one {selected_level} camera-table selection, got "
            f"{selected_tables}"
        )

    camera_source = _strip_c_comments(
        (repo_root / "src/game/camera.c").read_text(encoding="utf-8")
    )
    if re.search(r"^\s*#define\s+_\s+NULL\s*$", camera_source, re.MULTILINE) is None:
        raise ValueError("camera-table null alias is missing")
    if re.search(
        r"^\s*#define\s+DEFINE_LEVEL\([^\n]*cameratable\)\s+cameratable,\s*$",
        camera_source,
        re.MULTILINE,
    ) is None:
        raise ValueError("sCameraTriggers DEFINE_LEVEL projection is missing")
    if re.search(
        r"sCameraTriggers\s*\[[^]]+\]\s*=\s*\{\s*"
        r"NULL,\s*#include\s+\"levels/level_defines\.h\"\s*\};",
        camera_source,
        re.DOTALL,
    ) is None:
        raise ValueError("sCameraTriggers level-table include is missing")

    selected_table = selected_tables[0]
    if selected_table == "_":
        return frozenset()

    definition = re.search(
        rf"struct\s+CameraTrigger\s+{re.escape(selected_table)}\s*\[\]\s*="
        r"\s*\{(.*?)^\s*\};",
        camera_source,
        flags=re.MULTILINE | re.DOTALL,
    )
    if definition is None:
        raise ValueError(f"missing selected CameraTrigger table: {selected_table}")
    events = re.findall(
        r"\{\s*[^,]+,\s*([A-Za-z_]\w*|NULL)\s*,", definition.group(1)
    )
    return frozenset("_" + event for event in events if event != "NULL")


def _derive_bob_dispatcher_targets(repo_root: Path) -> dict[str, frozenset[str]]:
    """Derive Phase-A callback sets from the pinned sourceboot/BOB roots."""
    level_paths = (
        repo_root / "src/port/saturn/sourceboot/source_entry.c",
        repo_root / "levels/bob/script.c",
        repo_root / "levels/scripts.c",
    )
    level_definitions: dict[str, str] = {}
    for path in level_paths:
        for match in re.finditer(
            r"(?:static\s+)?const\s+LevelScript\s+([A-Za-z_]\w*)"
            r"\s*\[\]\s*=\s*\{(.*?)^\};",
            path.read_text(encoding="utf-8"),
            flags=re.MULTILINE | re.DOTALL,
        ):
            name, body = match.groups()
            if name in level_definitions:
                raise ValueError(f"duplicate LevelScript definition: {name}")
            level_definitions[name] = _strip_c_comments(body)

    reachable_scripts: set[str] = set()
    pending_scripts = ["level_script_entry"]
    while pending_scripts:
        name = pending_scripts.pop()
        if name in reachable_scripts:
            continue
        if name not in level_definitions:
            raise ValueError(f"missing reached LevelScript definition: {name}")
        reachable_scripts.add(name)
        body = level_definitions[name]
        pending_scripts.extend(re.findall(
            r"^\s*(?:JUMP|JUMP_LINK)\s*\(\s*([A-Za-z_]\w*)\s*\)",
            body,
            flags=re.MULTILINE,
        ))
        pending_scripts.extend(re.findall(
            r"^\s*(?:EXECUTE|EXIT_AND_EXECUTE)\s*\(\s*[^,]+,\s*"
            r"[^,]+,\s*[^,]+,\s*([A-Za-z_]\w*)\s*\)",
            body,
            flags=re.MULTILINE,
        ))

    reached_level_text = "\n".join(
        level_definitions[name] for name in sorted(reachable_scripts)
    )
    call_targets = frozenset(
        "_" + target for target in re.findall(
            r"^\s*CALL\s*\(\s*[^,]+,\s*([A-Za-z_]\w*)\s*\)",
            reached_level_text,
            flags=re.MULTILINE,
        )
    )
    call_loop_targets = frozenset(
        "_" + target for target in re.findall(
            r"^\s*CALL_LOOP\s*\(\s*[^,]+,\s*([A-Za-z_]\w*)\s*\)",
            reached_level_text,
            flags=re.MULTILINE,
        )
    )

    geo_roots = set(re.findall(
        r"LOAD_MODEL_FROM_GEO\s*\(\s*[^,]+,\s*([A-Za-z_]\w*)\s*\)",
        reached_level_text,
    ))
    area_roots = set(re.findall(
        r"AREA\s*\(\s*[^,]+,\s*([A-Za-z_]\w*)\s*\)",
        reached_level_text,
    ))
    geo_roots.update(area_roots)

    geo_definitions: dict[str, str] = {}
    geo_paths = sorted((repo_root / "actors").rglob("geo.inc.c"))
    geo_paths.extend(sorted((repo_root / "levels/bob").rglob("*.c")))
    for path in geo_paths:
        for match in re.finditer(
            r"(?:static\s+)?const\s+GeoLayout\s+([A-Za-z_]\w*)"
            r"\s*\[\]\s*=\s*\{(.*?)^\};",
            path.read_text(encoding="utf-8"),
            flags=re.MULTILINE | re.DOTALL,
        ):
            name, body = match.groups()
            if name in geo_definitions:
                raise ValueError(f"duplicate GeoLayout definition: {name}")
            geo_definitions[name] = _strip_c_comments(body)

    def reachable_geo_layouts(roots: set[str]) -> set[str]:
        reached: set[str] = set()
        pending = list(roots)
        while pending:
            name = pending.pop()
            if name in reached:
                continue
            if name not in geo_definitions:
                raise ValueError(f"missing reached GeoLayout definition: {name}")
            reached.add(name)
            body = geo_definitions[name]
            pending.extend(re.findall(
                r"GEO_BRANCH_AND_LINK\s*\(\s*([A-Za-z_]\w*)\s*\)", body
            ))
            pending.extend(re.findall(
                r"GEO_BRANCH\s*\(\s*[^,]+,\s*([A-Za-z_]\w*)\s*\)", body
            ))
        return reached

    reached_geo = reachable_geo_layouts(geo_roots)
    reached_geo_commands = {
        command
        for name in reached_geo
        for command in re.findall(r"\b(GEO_[A-Z0-9_]+)\s*\(", geo_definitions[name])
    }
    command_header = (repo_root / "include/geo_commands.h").read_text(
        encoding="utf-8"
    )
    macro_bodies = {
        match.group(1): match.group(2)
        for match in re.finditer(
            r"^#define\s+(GEO_[A-Z0-9_]+)(?:\([^\n]*\))?\s*"
            r"(.*?)(?=^#define\s+|\Z)",
            command_header,
            flags=re.MULTILINE | re.DOTALL,
        )
    }

    def command_opcode(name: str, seen: frozenset[str] = frozenset()) -> int:
        if name in seen or name not in macro_bodies:
            raise ValueError(f"cannot derive GeoLayout opcode for {name}")
        body = macro_bodies[name]
        encoded = re.search(r"CMD_BBH\(0x([0-9A-Fa-f]{2})", body)
        if encoded is not None:
            return int(encoded.group(1), 16)
        aliases = re.findall(r"\b(GEO_[A-Z0-9_]+)\s*\(", body)
        if len(aliases) != 1:
            raise ValueError(f"cannot derive unique GeoLayout alias for {name}")
        return command_opcode(aliases[0], seen | {name})

    geo_engine = (repo_root / "src/engine/geo_layout.c").read_text(
        encoding="utf-8"
    )
    jump_table = re.search(
        r"GeoLayoutJumpTable\[\]\s*=\s*\{(.*?)\};", geo_engine, re.DOTALL
    )
    if jump_table is None:
        raise ValueError("GeoLayoutJumpTable definition is missing")
    handlers = re.findall(
        r"\b(geo_layout_cmd_[a-z0-9_]+)\s*,", jump_table.group(1)
    )
    process_targets = frozenset(
        "_" + handlers[command_opcode(command)]
        for command in reached_geo_commands
    )

    active_area_geo = reachable_geo_layouts(area_roots)
    active_area_text = "\n".join(
        geo_definitions[name] for name in sorted(active_area_geo)
    )
    global_targets = frozenset(
        "_" + target for target in re.findall(
            r"GEO_(?:BACKGROUND|CAMERA_FRUSTUM_WITH_FUNC|CAMERA|ASM)"
            r"\([^)]*,\s*(geo_[A-Za-z0-9_]+)\)",
            active_area_text,
        )
    )

    reached_geo_text = "\n".join(
        geo_definitions[name] for name in sorted(reached_geo)
    )
    initializer_patterns = {
        "_init_graph_node_perspective":
            r"GEO_CAMERA_FRUSTUM_WITH_FUNC\s*\(\s*[^,]+,\s*[^,]+,\s*"
            r"[^,]+,\s*(geo_[A-Za-z0-9_]+)\s*\)",
        "_init_graph_node_switch_case":
            r"GEO_SWITCH_CASE\s*\(\s*[^,]+,\s*"
            r"(geo_[A-Za-z0-9_]+)\s*\)",
        "_init_graph_node_camera":
            r"GEO_CAMERA\s*\(\s*[^,]+,\s*[^,]+,\s*[^,]+,\s*[^,]+,\s*"
            r"[^,]+,\s*[^,]+,\s*[^,]+,\s*(geo_[A-Za-z0-9_]+)\s*\)",
        "_init_graph_node_generated":
            r"GEO_ASM\s*\(\s*[^,]+,\s*(geo_[A-Za-z0-9_]+)\s*\)",
        "_init_graph_node_background":
            r"GEO_BACKGROUND\s*\(\s*[^,]+,\s*"
            r"(geo_[A-Za-z0-9_]+)\s*\)",
        "_init_graph_node_held_object":
            r"GEO_HELD_OBJECT\s*\(\s*[^,]+,\s*[^,]+,\s*[^,]+,\s*"
            r"[^,]+,\s*(geo_[A-Za-z0-9_]+)\s*\)",
    }
    initializer_targets = {
        dispatcher: frozenset(
            "_" + target for target in re.findall(pattern, reached_geo_text)
        )
        for dispatcher, pattern in initializer_patterns.items()
    }
    empty_initializers = sorted(
        dispatcher for dispatcher, targets in initializer_targets.items()
        if not targets
    )
    if empty_initializers:
        raise ValueError(
            f"reached BOB GeoLayouts have no callbacks for {empty_initializers}"
        )

    # GeoLayout construction stores callbacks in fnNode.func, and Task 14's
    # iterative walk invokes them again during rendering. Both sites are real
    # indirect-call owners and need declarations tied to their exact source
    # callback sets.
    callback_owner_by_initializer = {
        "_init_graph_node_perspective": "_saturn_geo_enter_perspective",
        "_init_graph_node_switch_case": "_saturn_geo_enter_switch",
        "_init_graph_node_camera": "_saturn_geo_enter_camera",
        "_init_graph_node_generated": "_saturn_geo_enter_generated_list",
        "_init_graph_node_background": "_saturn_geo_enter_background",
        "_init_graph_node_held_object": "_saturn_geo_enter_held_object",
    }
    rendering_graph = _strip_c_comments(
        (repo_root / "src/game/rendering_graph_node.c").read_text(
            encoding="utf-8"
        )
    )
    graph_node = _strip_c_comments(
        (repo_root / "src/engine/graph_node.c").read_text(encoding="utf-8")
    )
    creation_callback_names = {
        "_init_graph_node_perspective": "nodeFunc",
        "_init_graph_node_switch_case": "nodeFunc",
        "_init_graph_node_camera": "func",
        "_init_graph_node_generated": "gfxFunc",
        "_init_graph_node_background": "backgroundFunc",
        "_init_graph_node_held_object": "nodeFunc",
    }
    callback_targets: dict[str, frozenset[str]] = {}
    for initializer, owner in callback_owner_by_initializer.items():
        initializer_body = _single_braced_body(
            graph_node,
            r"\b" + re.escape(initializer[1:]) + r"\s*\(",
            initializer,
        )
        callback_name = creation_callback_names[initializer]
        if re.search(
            r"\b" + re.escape(callback_name) + r"\s*\(\s*GEO_CONTEXT_CREATE\b",
            initializer_body,
        ) is None:
            raise ValueError(f"{initializer} no longer creates a callback node")
        callback_targets[initializer] = initializer_targets[initializer]
        owner_body = _single_braced_body(
            rendering_graph,
            r"static\s+[^({;]*\b" + re.escape(owner[1:]) + r"\s*\(",
            owner,
        )
        if re.search(r"\bnode\s*->\s*fnNode\s*\.\s*func\s*\(", owner_body) is None:
            raise ValueError(f"{owner} no longer owns a fnNode.func callback")
        callback_targets[owner] = initializer_targets[initializer]

    runtime_ops = re.search(
        r"static\s+const\s+sm64_saturn_geo_walk_runtime_ops_t\s+ops\s*=\s*"
        r"\{(.*?)\};",
        rendering_graph,
        flags=re.DOTALL,
    )
    if runtime_ops is None:
        raise ValueError("saturn geo walk runtime ops table is missing")
    runtime_callbacks = frozenset(
        "_" + callback
        for callback in re.findall(
            r"\b(saturn_geo_walk_(?:enter|dispatch|leave))\b",
            runtime_ops.group(1),
        )
    )
    if runtime_callbacks != frozenset({
        "_saturn_geo_walk_enter",
        "_saturn_geo_walk_dispatch",
        "_saturn_geo_walk_leave",
    }):
        raise ValueError("saturn geo walk runtime ops table changed")
    callback_targets["_sm64_saturn_geo_walk_runtime_run"] = runtime_callbacks

    return {
        "_geo_call_global_function_nodes_helper": global_targets,
        "_level_cmd_call": call_targets,
        "_level_cmd_call_loop": call_loop_targets,
        "_process_geo_layout": process_targets,
        **callback_targets,
    }


def _assert_bob_source_oracle_matches(
    test_case: unittest.TestCase, repo_root: Path, oracle_text: str,
) -> None:
    """Require the full source-derived dispatcher map in both oracle sections."""
    derived = _derive_bob_dispatcher_targets(repo_root)
    test_case.assertEqual(derived, BOB_DISPATCHER_TARGETS)

    oracle = parse_route_oracle(oracle_text)
    expected_edges = frozenset(
        (dispatcher, callback)
        for dispatcher, callbacks in derived.items()
        for callback in callbacks
    )
    manifest_edges = frozenset(
        edge for edge in oracle.static_manifest_edges
        if edge[0] in derived
    )
    declared_edges = frozenset(
        edge for edge in oracle.indirect_edges
        if edge[0] in derived
    )
    test_case.assertEqual(manifest_edges, expected_edges)
    test_case.assertEqual(declared_edges, expected_edges)


def _single_braced_body(source: str, declaration: str, label: str) -> str:
    matches = list(re.finditer(declaration, source, flags=re.MULTILINE))
    if len(matches) != 1:
        raise ValueError(f"expected one {label} declaration, got {len(matches)}")
    opening = source.find("{", matches[0].end())
    if opening < 0:
        raise ValueError(f"{label} declaration has no body")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise ValueError(f"{label} declaration has an unterminated body")


def _derive_renderer_route_oracle(repo_root: Path) -> verifier.RouteOracle:
    """Derive the live descriptor route from its checked-in source owners."""
    renderer = _strip_c_comments(
        (repo_root / "src/port/saturn/gfx/saturn_demo_render.c").read_text(
            encoding="utf-8"
        )
    )
    lifecycle = _strip_c_comments(
        (repo_root / "src/port/saturn/gfx/saturn_render_lifecycle.c")
        .read_text(encoding="utf-8")
    )
    runtime = _strip_c_comments(
        (repo_root / "src/port/saturn/gfx/saturn_render_job_runtime.c")
        .read_text(encoding="utf-8")
    )

    callback_factory = _single_braced_body(
        renderer,
        r"static\s+const\s+sm64_saturn_render_job_callback_table_t\s+"
        r"\*\s*demo_render_job_callbacks\s*\(\s*void\s*\)\s*(?=\{)",
        "descriptor callback factory",
    )
    returned_tables = re.findall(
        r"\breturn\s+&([A-Za-z_]\w*)\s*;", callback_factory
    )
    if len(returned_tables) != 1:
        raise ValueError(
            "expected one returned callback table, got "
            f"{returned_tables}"
        )
    returned_table = returned_tables[0]
    callback_tables = re.findall(
        r"static\s+const\s+sm64_saturn_render_job_callback_table_t\s+"
        + re.escape(returned_table)
        + r"\s*=\s*\{\s*\{(.*?)\}\s*\}\s*;",
        callback_factory,
        flags=re.DOTALL,
    )
    if len(callback_tables) != 1:
        raise ValueError(
            "expected one returned callback table definition, got "
            f"{len(callback_tables)}"
        )
    callbacks = tuple(
        field.strip() for field in callback_tables[0].split(",")
        if field.strip()
    )
    if len(callbacks) != 4 or any(
        re.fullmatch(r"[A-Za-z_]\w*", callback) is None
        for callback in callbacks
    ):
        raise ValueError(
            f"expected four descriptor callbacks, got {callbacks}"
        )
    for callback in callbacks:
        _single_braced_body(
            renderer,
            rf"static\s+bool(?:\s+__attribute__\s*\(\([^)]*\)\))?\s+"
            rf"{re.escape(callback)}\s*\(",
            callback,
        )
    init_body = _single_braced_body(
        renderer,
        r"void\s+sm64_saturn_demo_render_init\s*\(\s*void\s*\)",
        "renderer initialization",
    )
    if len(re.findall(
        r"\bsm64_saturn_render_job_runtime_activate_graph\s*\(\s*"
        r"&s_render_job_graph\s*,\s*demo_render_job_callbacks\s*\(\s*\)\s*,"
        r"\s*NULL\s*\)",
        init_body,
    )) != 1:
        raise ValueError(
            "descriptor callback factory no longer reaches runtime activation"
        )

    lifecycle_tables = re.findall(
        r"static\s+const\s+sm64_saturn_render_lifecycle_ops_t\s+"
        r"s_demo_render_lifecycle_ops\s*=\s*\{(.*?)\};",
        renderer,
        flags=re.DOTALL,
    )
    if len(lifecycle_tables) != 1:
        raise ValueError(
            "expected one render lifecycle table, got "
            f"{len(lifecycle_tables)}"
        )
    lifecycle_pairs = re.findall(
        r"\.([A-Za-z_]\w*)\s*=\s*([A-Za-z_]\w*)\s*,?",
        lifecycle_tables[0],
    )
    lifecycle_callbacks = dict(lifecycle_pairs)
    expected_lifecycle_fields = {
        "prepare_publish", "notify", "slave_retired", "drain_master",
        "finalize", "quarantine",
    }
    if len(lifecycle_pairs) != len(expected_lifecycle_fields) or \
            set(lifecycle_callbacks) != expected_lifecycle_fields:
        raise ValueError(
            f"unexpected render lifecycle callback table: {lifecycle_pairs}"
        )

    start_body = _single_braced_body(
        lifecycle,
        r"bool\s+sm64_saturn_render_lifecycle_start\s*\(",
        "render lifecycle start",
    )
    poll_body = _single_braced_body(
        lifecycle,
        r"sm64_saturn_render_lifecycle_status_t\s+"
        r"sm64_saturn_render_lifecycle_poll\s*\(",
        "render lifecycle poll",
    )
    start_fields = frozenset(re.findall(r"\bops->([A-Za-z_]\w*)\s*\(", start_body))
    poll_fields = frozenset(re.findall(r"\bops->([A-Za-z_]\w*)\s*\(", poll_body))
    if start_fields != frozenset({"prepare_publish", "notify", "quarantine"}):
        raise ValueError(f"unexpected lifecycle-start calls: {sorted(start_fields)}")
    if poll_fields != frozenset({
        "slave_retired", "drain_master", "finalize", "quarantine",
    }):
        raise ValueError(f"unexpected lifecycle-poll calls: {sorted(poll_fields)}")

    start_root = "sm64_saturn_demo_render_start_frame"
    poll_root = "sm64_saturn_demo_render_poll_frame"
    start_root_body = _single_braced_body(
        renderer, rf"\b{start_root}\s*\(", start_root
    )
    poll_root_body = _single_braced_body(
        renderer, rf"\b{poll_root}\s*\(", poll_root
    )
    if len(re.findall(
        r"\bsm64_saturn_render_lifecycle_start\s*\(\s*"
        r"&s_demo_render_transaction\.lifecycle\s*,\s*"
        r"&s_demo_render_lifecycle_ops\s*,",
        start_root_body,
    )) != 1:
        raise ValueError("render start root no longer owns the lifecycle table")
    if len(re.findall(
        r"\bsm64_saturn_render_lifecycle_poll\s*\(\s*"
        r"&s_demo_render_transaction\.lifecycle\s*,\s*"
        r"&s_demo_render_lifecycle_ops\s*,",
        poll_root_body,
    )) != 1:
        raise ValueError("render poll root no longer owns the lifecycle table")

    for function in (
        "sm64_saturn_render_job_runtime_poll_slave",
        "sm64_saturn_render_job_runtime_drain_master",
    ):
        body = _single_braced_body(
            runtime, rf"uint16_t\s+{function}\s*\(", function
        )
        if body.count("s_runtime.callbacks->entries[callback_index]") != 1:
            raise ValueError(f"{function} no longer resolves the callback table")
        if len(re.findall(r"\bcallback\s*\(", body)) != 1:
            raise ValueError(f"{function} no longer invokes one resolved callback")
    slave_entry = _single_braced_body(
        runtime,
        r"static\s+void\s+render_job_slave_entry\s*\(",
        "render job slave entry",
    )
    if len(re.findall(
        r"\bsm64_saturn_render_job_runtime_poll_slave\s*\(\s*\)",
        slave_entry,
    )) != 1 or len(re.findall(
        r"\bcpu_dual_slave_set\s*\(\s*render_job_slave_entry\s*\)",
        runtime,
    )) != 1:
        raise ValueError("descriptor slave polling entry is no longer pinned")

    lifecycle_edges = {
        ("_sm64_saturn_render_lifecycle_start",
         "_" + lifecycle_callbacks[field])
        for field in start_fields
    } | {
        ("_sm64_saturn_render_lifecycle_poll",
         "_" + lifecycle_callbacks[field])
        for field in poll_fields
    }
    descriptor_edges = {
        ("_" + dispatcher, "_" + callback)
        for dispatcher in (
            "sm64_saturn_render_job_runtime_poll_slave",
            "sm64_saturn_render_job_runtime_drain_master",
        )
        for callback in callbacks
    }
    edges = frozenset(lifecycle_edges | descriptor_edges)
    return verifier.RouteOracle(
        1,
        frozenset({
            "_" + start_root,
            "_" + poll_root,
            "_sm64_saturn_render_job_runtime_poll_slave",
        }),
        edges,
        edges,
    )


# Keep the source-derivation machinery at module scope without splitting the
# one unittest fixture that owns the shared audit helpers above and below it.
class NativeMathCensusTests(NativeMathCensusTests):

    BOB_NULL_CAMERA_ELF_SHA256 = (
        "1905ec8d42ea00ea2c000b5f53dd88f2079ffda8ce"
        "b67bcd5879e8e96acfc2e2"
    )

    def test_checked_in_renderer_oracle_matches_live_descriptor_route(self) -> None:
        repo_root = Path(__file__).parents[2]
        oracle = parse_route_oracle(
            Path(__file__).with_name(
                "sh2_native_math_route_oracle_v1.txt"
            ).read_text(encoding="utf-8")
        )
        expected = _derive_renderer_route_oracle(repo_root)
        self.assertEqual(oracle, expected)
        linked_names = oracle.roots | frozenset(
            name for edge in oracle.indirect_edges for name in edge
        )
        self.assertNotIn("_sm64_saturn_demo_render_frame", linked_names)
        self.assertNotIn("_sm64_saturn_dual_worker_run", linked_names)
        self.assertNotIn("_demo_terrain_compact_range", linked_names)

    @staticmethod
    def _renderer_source_root(directory: str) -> Path:
        repo_root = Path(__file__).parents[2]
        fixture_root = Path(directory)
        for relative in (
            "src/port/saturn/gfx/saturn_demo_render.c",
            "src/port/saturn/gfx/saturn_render_lifecycle.c",
            "src/port/saturn/gfx/saturn_render_job_runtime.c",
        ):
            target = fixture_root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(
                (repo_root / relative).read_text(encoding="utf-8"),
                encoding="utf-8",
            )
        return fixture_root

    def test_renderer_oracle_rejects_callback_table_not_returned_to_activation(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source_root = self._renderer_source_root(directory)
            renderer_path = (
                source_root / "src/port/saturn/gfx/saturn_demo_render.c"
            )
            renderer = renderer_path.read_text(encoding="utf-8")
            before = "    return &callbacks;"
            after = (
                "    static const sm64_saturn_render_job_callback_table_t "
                "inactive_callbacks = {{0}};\n"
                "    return &inactive_callbacks;"
            )
            self.assertEqual(renderer.count(before), 1)
            renderer_path.write_text(
                renderer.replace(before, after, 1), encoding="utf-8"
            )
            with self.assertRaisesRegex(
                ValueError, "returned callback table|descriptor callbacks"
            ):
                _derive_renderer_route_oracle(source_root)

    def test_renderer_oracle_rejects_callback_factory_detached_from_activation(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source_root = self._renderer_source_root(directory)
            renderer_path = (
                source_root / "src/port/saturn/gfx/saturn_demo_render.c"
            )
            renderer = renderer_path.read_text(encoding="utf-8")
            before = (
                "&s_render_job_graph, demo_render_job_callbacks(), NULL)"
            )
            self.assertEqual(renderer.count(before), 1)
            renderer_path.write_text(
                renderer.replace(
                    before, "&s_render_job_graph, NULL, NULL)", 1
                ),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "runtime activation"):
                _derive_renderer_route_oracle(source_root)

    def test_renderer_oracle_rejects_start_call_outside_start_root_body(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source_root = self._renderer_source_root(directory)
            renderer_path = (
                source_root / "src/port/saturn/gfx/saturn_demo_render.c"
            )
            renderer = renderer_path.read_text(encoding="utf-8")
            live_call = "if (!sm64_saturn_render_lifecycle_start("
            self.assertEqual(renderer.count(live_call), 1)
            renderer = renderer.replace(
                live_call,
                "if (!sm64_saturn_render_lifecycle_start_detached(",
                1,
            )
            renderer += """
static bool demo_detached_start_decoy(uint32_t generation)
{
    return sm64_saturn_render_lifecycle_start(
        &s_demo_render_transaction.lifecycle,
        &s_demo_render_lifecycle_ops, &s_demo_render_transaction,
        generation);
}
"""
            renderer_path.write_text(renderer, encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "start root"):
                _derive_renderer_route_oracle(source_root)

    @staticmethod
    def _camera_trigger_source_root(
        directory: str, mutations: dict[str, tuple[str, str]] | None = None,
    ) -> Path:
        repo_root = Path(__file__).parents[2]
        fixture_root = Path(directory)
        paths = (
            "src/port/saturn/sourceboot/source_entry.c",
            "src/game/level_update.c",
            "src/game/camera.c",
            "levels/level_defines.h",
            "levels/bob/script.c",
        )
        for relative in paths:
            source = (repo_root / relative).read_text(encoding="utf-8")
            if mutations and relative in mutations:
                before, after = mutations[relative]
                if source.count(before) != 1:
                    raise AssertionError(
                        f"camera proof fixture mutation is not unique: {relative}"
                    )
                source = source.replace(before, after, 1)
            target = fixture_root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(source, encoding="utf-8")
        return fixture_root

    @classmethod
    def _prove_camera_trigger_source(cls, source_root: Path) -> bool:
        return verifier.prove_sourceboot_bob_null_camera_triggers(
            source_root,
            linked_elf_sha256=cls.BOB_NULL_CAMERA_ELF_SHA256,
        )

    @staticmethod
    def _camera_trigger_disassembly(*, first_call: str = "jsr", guard_gap: bool = False) -> str:
        gap = " 6000046: 00 09 nop\n" if guard_gap else ""
        exts_address = 0x6000048 if guard_gap else 0x6000046
        return f"""
06000000 <_camera_course_processing>:
 6000044: de 67 mov.l 6000200 <_camera_course_processing+0x200>,r14 ! 06010000 <_sCameraTriggers>
{gap} {exts_address:x}: 62 1f exts.w r1,r2
 {exts_address + 2:x}: 42 08 shll2 r2
 {exts_address + 4:x}: 3e 2c add r2,r14
 {exts_address + 6:x}: 62 e2 mov.l @r14,r2
 {exts_address + 8:x}: 22 28 tst r2,r2
 {exts_address + 10:x}: 89 01 bt 6000056 <_camera_course_processing+0x56>
 {exts_address + 12:x}: a0 7e bra 6000152 <_camera_course_processing+0x152>
 {exts_address + 14:x}: e9 00 mov #0,r9
 6000056: d9 64 mov.l 6000204 <_sStatusFlags>,r9 ! 06011000 <_sStatusFlags>
 6000058: 60 91 mov.w @r9,r0
 600005a: 60 08 swap.b r0,r0
 600005c: c8 10 tst #16,r0
 600005e: 8b 01 bf 6000064 <_camera_course_processing+0x64>
 6000060: a0 79 bra 6000092 <_camera_course_processing+0x92>
 6000062: 00 09 nop
 6000064: 92 b1 mov.w 6000208 <_camera_course_processing+0x208>,r2 ! efff
 6000066: 61 91 mov.w @r9,r1
 6000068: 50 f2 mov.l @(8,r15),r0
 600006a: 21 29 and r2,r1
 600006c: 88 06 cmp/eq #6,r0
 600006e: 8f 06 bf.s 600007e <_camera_course_processing+0x7e>
 6000070: 29 11 mov.w r1,@r9
 6000072: 60 80 mov.b @r8,r0
 6000074: d1 59 mov.l 600020c <_sModeInfo>,r1 ! 06012000 <_sModeInfo>
 6000076: 60 0c extu.b r0,r0
 6000078: 81 11 mov.w r0,@(2,r1)
 600007a: 84 ff mov.b @(15,r15),r0
 600007c: 28 00 mov.b r0,@r8
 600007e: 60 80 mov.b @r8,r0
 6000080: 60 0c extu.b r0,r0
 6000082: 7f 2c add #44,r15
 6000084: 4f 26 lds.l @r15+,pr
 6000086: 6e f6 mov.l @r15+,r14
 6000088: 6d f6 mov.l @r15+,r13
 600008a: 6c f6 mov.l @r15+,r12
 600008c: 6b f6 mov.l @r15+,r11
 600008e: 6a f6 mov.l @r15+,r10
 6000090: 69 f6 mov.l @r15+,r9
 6000092: 00 0b rts
 6000094: 68 f6 mov.l @r15+,r8
 6000096: 63 c0 mov.b @r12,r3
 6000118: 62 e2 mov.l @r14,r2
 600011a: 32 9c add r9,r2
 600011c: 52 21 mov.l @(4,r2),r2
 600011e: 42 0b {first_call} @r2
 6000120: ed 01 mov #1,r13
 6000138: 8b 02 bf 6000140 <_camera_course_processing+0x140>
 600013a: 52 21 mov.l @(4,r2),r2
 600013c: 42 0b jsr @r2
 600013e: 64 83 mov r8,r4
 6000140: 79 18 add #24,r9
 6000142: 6c e2 mov.l @r14,r12
 6000144: 3c 9c add r9,r12
 6000146: 53 c1 mov.l @(4,r12),r3
 6000148: 23 38 tst r3,r3
 600014a: 8f a4 bf.s 6000096 <_camera_course_processing+0x96>
 600014c: 51 f4 mov.l @(16,r15),r1
 600014e: af 82 bra 6000056 <_camera_course_processing+0x56>
 6000150: 00 09 nop
 6000152: af f6 bra 6000142 <_camera_course_processing+0x142>
 6000154: ed 00 mov #0,r13
 6000180: 43 0b jsr @r3
 6000182: 00 09 nop
"""

    def test_pinned_bob_null_camera_trigger_proof_removes_only_exact_two_sites(self) -> None:
        self.assertTrue(hasattr(
            verifier, "prove_sourceboot_bob_null_camera_triggers"
        ), "BOB camera-trigger source proof is missing")
        with tempfile.TemporaryDirectory() as directory:
            source_root = self._camera_trigger_source_root(directory)
            self.assertTrue(
                self._prove_camera_trigger_source(source_root)
            )

        instructions = parse_instructions(self._camera_trigger_disassembly())
        owners = (FunctionOwner(
            "_camera_course_processing", 0x6000000, 0x6000200, 1
        ),)
        dead = verifier.sourceboot_bob_null_camera_trigger_dead_transfers(
            instructions, owners, route_selects_null=True
        )
        self.assertEqual(dead, frozenset({
            ("_camera_course_processing", 0x600011E),
            ("_camera_course_processing", 0x600013C),
        }))
        result = analyze_code_only(
            instructions,
            owners,
            decoded_lines={"_camera_course_processing": {
                0x600011E, 0x600013C, 0x6000180,
            }},
            selected_names={"_camera_course_processing"},
            include_owner_entry=False,
            proven_dead_transfers=dead,
        )
        self.assertEqual(
            [(item.address, item.mnemonic) for item in result.unresolved_transfers
             if item.mnemonic == "jsr"],
            [(0x6000180, "jsr")],
        )

    def test_camera_trigger_proof_fails_closed_for_route_and_source_mutations(self) -> None:
        mutations = {
            "unknown_level": (
                "src/port/saturn/sourceboot/source_entry.c",
                ("SET_REG(/* value */ LEVEL_BOB)", "SET_REG(/* value */ level_from_runtime)"),
            ),
            "non_bob_level": (
                "src/port/saturn/sourceboot/source_entry.c",
                ("SET_REG(/* value */ LEVEL_BOB)", "SET_REG(/* value */ LEVEL_CCM)"),
            ),
            "bob_nonnull_table": (
                "levels/level_defines.h",
                (
                    'DEFINE_LEVEL("BATTLE FIELD",   LEVEL_BOB,              COURSE_BOB,      bob,              generic,  15000,    0x08, 0x08, 0x08, _,         _)',
                    'DEFINE_LEVEL("BATTLE FIELD",   LEVEL_BOB,              COURSE_BOB,      bob,              generic,  15000,    0x08, 0x08, 0x08, _,         sCamBOB)',
                ),
            ),
            "intervening_command": (
                "src/port/saturn/sourceboot/source_entry.c",
                (
                    "SET_REG(/* value */ LEVEL_BOB),\n    /* Before lvl_init_from_save_file",
                    "SET_REG(/* value */ LEVEL_BOB),\n    SLEEP(/* frames */ 1),\n    /* Before lvl_init_from_save_file",
                ),
            ),
            "init_rewrites_bob_to_ccm": (
                "src/game/level_update.c",
                (
                    "    gCurrLevelNum = levelNum;\n"
                    "    gCurrCourseNum = COURSE_NONE;",
                    "    levelNum = LEVEL_CCM;\n"
                    "    gCurrLevelNum = levelNum;\n"
                    "    gCurrCourseNum = COURSE_NONE;",
                ),
            ),
        }
        for label, (relative, mutation) in mutations.items():
            with self.subTest(label=label), tempfile.TemporaryDirectory() as directory:
                source_root = self._camera_trigger_source_root(
                    directory, {relative: mutation}
                )
                self.assertFalse(
                    self._prove_camera_trigger_source(source_root)
                )

    def test_camera_trigger_source_proof_rejects_other_elf_identity(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source_root = self._camera_trigger_source_root(directory)
            self.assertIs(
                verifier.prove_sourceboot_bob_null_camera_triggers(
                    source_root, linked_elf_sha256="0" * 64
                ),
                False,
                "source proof must reject an ELF outside the pinned identity",
            )

    def test_camera_trigger_dead_transfer_shape_rejects_near_matches_and_unknown_route(self) -> None:
        owners = (FunctionOwner(
            "_camera_course_processing", 0x6000000, 0x6000200, 1
        ),)
        cases = (
            ("unknown_route", self._camera_trigger_disassembly(), False),
            ("guard_gap", self._camera_trigger_disassembly(guard_gap=True), True),
            ("wrong_first_call", self._camera_trigger_disassembly(first_call="jmp"), True),
        )
        for label, disassembly, route_selects_null in cases:
            with self.subTest(label=label):
                self.assertEqual(
                    verifier.sourceboot_bob_null_camera_trigger_dead_transfers(
                        parse_instructions(disassembly), owners,
                        route_selects_null=route_selects_null,
                    ),
                    frozenset(),
                )

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

    def _v3_contract_text(self, elf_sha256: str = "a" * 64) -> str:
        return (
            "AUDIT_CONTRACT_VERSION 3\n"
            "EXPECTED_ROOT _game_loop_one_iteration\n"
            "EXPECTED_TOTAL 700\n"
            f"EXPECTED_ELF_SHA256 {elf_sha256}\n"
            "FORBIDDEN_CALLER _atan2_lookup\n"
            "FORBIDDEN_CALLER _atan2s\n"
        )

    def _v4_contract_text(self, **overrides: str) -> str:
        values = {
            "release_manifest": "a" * 64,
            "identity": "b" * 64,
            "effective_config": "c" * 64,
            "target_profile": "d" * 64,
            "elf": "e" * 64,
        } | overrides
        return (
            "AUDIT_CONTRACT_VERSION 4\n"
            "EXPECTED_ROOT _game_loop_one_iteration\n"
            "EXPECTED_TOTAL 701\n"
            f"EXPECTED_RELEASE_MANIFEST_SHA256 {values['release_manifest']}\n"
            f"EXPECTED_IDENTITY_SHA256 {values['identity']}\n"
            f"EXPECTED_EFFECTIVE_CONFIG_SHA256 {values['effective_config']}\n"
            f"EXPECTED_TARGET_PROFILE_SHA256 {values['target_profile']}\n"
            f"EXPECTED_ELF_SHA256 {values['elf']}\n"
            "FORBIDDEN_CALLER _atan2_lookup\n"
            "FORBIDDEN_CALLER _atan2s\n"
        )

    def test_v4_requires_all_release_identity_hashes(self) -> None:
        text = self._v4_contract_text()
        contract = parse_audit_contract(text)
        self.assertEqual(contract.version, 4)
        for directive in (
            "EXPECTED_RELEASE_MANIFEST_SHA256",
            "EXPECTED_IDENTITY_SHA256",
            "EXPECTED_EFFECTIVE_CONFIG_SHA256",
            "EXPECTED_TARGET_PROFILE_SHA256",
            "EXPECTED_ELF_SHA256",
        ):
            with self.subTest(directive=directive):
                without = "\n".join(
                    line for line in text.splitlines()
                    if not line.startswith(directive + " ")
                ) + "\n"
                with self.assertRaisesRegex(ValueError, "v4"):
                    parse_audit_contract(without)

    def test_v4_rejects_malformed_and_duplicate_release_hashes(self) -> None:
        directives = {
            "release_manifest": "EXPECTED_RELEASE_MANIFEST_SHA256",
            "identity": "EXPECTED_IDENTITY_SHA256",
            "effective_config": "EXPECTED_EFFECTIVE_CONFIG_SHA256",
            "target_profile": "EXPECTED_TARGET_PROFILE_SHA256",
            "elf": "EXPECTED_ELF_SHA256",
        }
        for field, directive in directives.items():
            message = "expected ELF SHA-256" if field == "elf" else directive
            for invalid in ("A" * 64, "a" * 63, "g" * 64):
                with self.subTest(field=field, invalid=invalid[:4]):
                    with self.assertRaisesRegex(ValueError, message):
                        parse_audit_contract(self._v4_contract_text(**{field: invalid}))
            with self.subTest(field=field, duplicate=True):
                with self.assertRaisesRegex(ValueError, "duplicate"):
                    parse_audit_contract(
                        self._v4_contract_text() + f"{directive} {'f' * 64}\n"
                    )

    def test_v2_and_v3_reject_every_v4_release_directive(self) -> None:
        additions = (
            "EXPECTED_RELEASE_MANIFEST_SHA256",
            "EXPECTED_IDENTITY_SHA256",
            "EXPECTED_EFFECTIVE_CONFIG_SHA256",
            "EXPECTED_TARGET_PROFILE_SHA256",
        )
        v2 = (
            "AUDIT_CONTRACT_VERSION 2\n"
            "EXPECTED_ROOT _game_loop_one_iteration\n"
            "EXPECTED_TOTAL 582\n"
            "FORBIDDEN_CALLER _atan2_lookup\n"
        )
        for version, base in ((2, v2), (3, self._v3_contract_text())):
            for directive in additions:
                with self.subTest(version=version, directive=directive):
                    with self.assertRaisesRegex(ValueError, f"v{version}.*{directive}"):
                        parse_audit_contract(base + f"{directive} {'a' * 64}\n")

    def test_v4_integrity_rejects_noncanonical_contract_after_pin(self) -> None:
        text = self._v4_contract_text()
        contract = parse_audit_contract(text)
        with self.assertRaisesRegex(ValueError, "immutable audit contract digest mismatch"):
            verify_audit_contract_integrity(text, contract)
        verify_audit_contract_integrity(
            text, contract, expected_digest=hashlib.sha256(text.encode("utf-8")).hexdigest()
        )

    def test_v4_release_preflight_accepts_only_matching_identity_v2_snapshot(self) -> None:
        from test_release_manifest import ReleaseFixture

        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as temporary:
            fixture = ReleaseFixture(Path(temporary))
            manifest = fixture.write()
            import release_manifest

            with release_manifest.verify_release_manifest(manifest) as release:
                document = release.document
                contract_values = {
                    "release_manifest": release.manifest_sha256,
                    "identity": document["identity_sha256"],
                    "effective_config": document["effective_config_sha256"],
                    "target_profile": document["target_profile_sha256"],
                    "elf": document["outputs"]["elf"]["sha256"],
                }
                contract = parse_audit_contract(
                    self._v4_contract_text(**contract_values)
                )
            with patch.object(verifier, "run_command") as run_command:
                digest_paths: list[Path] = []
                real_digest = verifier.file_digest

                def observe_digest(path: Path) -> str:
                    digest_paths.append(path)
                    return real_digest(path)

                with patch.object(verifier, "file_digest", side_effect=observe_digest):
                    verify_audit_contract_target(
                        contract, fixture.outputs["elf"], manifest
                    )
                self.assertEqual(len(digest_paths), 1)
                self.assertNotEqual(
                    digest_paths[0].resolve(), fixture.outputs["elf"].resolve()
                )
                self.assertFalse(digest_paths[0].exists())
                run_command.assert_not_called()
                mismatches = {
                    "release_manifest": "release manifest",
                    "identity": "identity_sha256",
                    "effective_config": "effective_config_sha256",
                    "target_profile": "target_profile_sha256",
                    "elf": "ELF",
                }
                for field, message in mismatches.items():
                    with self.subTest(field=field):
                        wrong = parse_audit_contract(
                            self._v4_contract_text(
                                **(contract_values | {field: "f" * 64})
                            )
                        )
                        with self.assertRaisesRegex(ValueError, message):
                            verify_audit_contract_target(
                                wrong, fixture.outputs["elf"], manifest
                            )
                        run_command.assert_not_called()
                fixture.outputs["elf"].write_bytes(b"wrong elf")
                with self.assertRaisesRegex(ValueError, "release manifest|ELF|elf output"):
                    verify_audit_contract_target(contract, fixture.outputs["elf"], manifest)
                run_command.assert_not_called()

    def test_checked_in_v2_v3_contract_bytes_and_digests_are_unchanged(self) -> None:
        fixture_dir = Path(__file__).parent
        expected = {
            "sh2_native_math_sim_audit_contract_v2.txt": (
                507, "87dabb51adc1c1cb6b646a826977658de305df086d1cfb21fc2c97a0bd6127e2"
            ),
            "sh2_native_math_goal_audit_contract_v3.txt": (
                416, "80f662863f6af8c8d905717cc06504677eedf144e2f00eff7b254ee7e099cba5"
            ),
        }
        for name, (size, digest) in expected.items():
            with self.subTest(name=name):
                raw = (fixture_dir / name).read_bytes()
                self.assertEqual(len(raw), size)
                self.assertEqual(hashlib.sha256(raw).hexdigest(), digest)

    def test_measurement_cli_writes_explicitly_unsealed_release_bound_report(self) -> None:
        from test_release_manifest import ReleaseFixture
        import release_manifest as release_manifest_module

        sections = "  [ 1] .text PROGBITS 06001000 001000 002004 00 AX 0 0 2\n"
        symbols = (
            "   1: 06001000 16 FUNC GLOBAL DEFAULT 1 _game_loop_one_iteration\n"
            "   2: 06002000 4 FUNC GLOBAL DEFAULT 1 ___mulsf3\n"
        )
        disassembly = (
            "06001000 <_game_loop_one_iteration>:\n"
            " 6001000: b7 fe bsr 6002000 <___mulsf3>\n"
            " 6001002: 00 09 nop\n"
            " 6001004: 00 0b rts\n"
            " 6001006: 00 09 nop\n"
            "06002000 <___mulsf3>:\n"
            " 6002000: 00 0b rts\n"
            " 6002002: 00 09 nop\n"
        )
        tool_elf_paths: list[Path] = []

        def fake_command(command: list[str]) -> str:
            if command[0] in {"objdump", "readelf"}:
                tool_elf_paths.append(Path(command[-1]))
                self.assertTrue(tool_elf_paths[-1].is_file())
            if command[1] == "-d":
                return disassembly
            if command[1] == "-SW":
                return sections
            if command[1] == "-sW":
                return symbols
            if command[1] == "--debug-dump=decodedline":
                return "fixture.c 1 0x06001000\n"
            raise AssertionError(command)

        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as temporary:
            root = Path(temporary)
            fixture = ReleaseFixture(root)
            manifest = fixture.write()
            baseline = root / "baseline.txt"
            route_oracle = root / "route.txt"
            audit_oracle = root / "audit-route.txt"
            report = root / "measurement.json"
            baseline.write_text(
                "BASELINE_VERSION 1\nHOT_CEILING 1\n"
                "HOT _game_loop_one_iteration ___mulsf3 1\n",
                encoding="utf-8",
            )
            route_oracle.write_text(
                "ROUTE_ORACLE_VERSION 1\nROOT _game_loop_one_iteration\n",
                encoding="utf-8",
            )
            audit_oracle.write_bytes(route_oracle.read_bytes())
            with release_manifest_module.verify_release_manifest(manifest) as release:
                expected_manifest = release.manifest_sha256
                expected_elf = release.document["outputs"]["elf"]["sha256"]
            output = io.StringIO()
            with patch.object(verifier, "run_command", side_effect=fake_command), \
                    patch.object(verifier, "verify_baseline_integrity"), \
                    patch.object(verifier, "verify_route_oracle_integrity"), \
                    patch.object(
                        verifier, "prove_sourceboot_bob_null_camera_triggers",
                        return_value=False,
                    ), patch.object(verifier, "source_locations", return_value={}), \
                    redirect_stdout(output):
                self.assertEqual(
                    verifier.main([
                        str(fixture.outputs["elf"]), str(baseline),
                        "--route-oracle", str(route_oracle),
                        "--audit-route-oracle", str(audit_oracle),
                        "--measure-audit-report", str(report),
                        "--release-manifest", str(manifest),
                        "--objdump", "objdump", "--readelf", "readelf",
                        "--addr2line", "addr2line",
                    ]),
                    0,
                )
            self.assertNotIn("PASS", output.getvalue())
            self.assertNotIn("accepted", output.getvalue().lower())
            self.assertEqual(json.loads(report.read_bytes()), {
                "schema": "sm64-saturn-native-math-measurement-v1",
                "status": "measured-unsealed",
                "root": "_game_loop_one_iteration",
                "total": 1,
                "callers": ["_game_loop_one_iteration"],
                "elf_sha256": expected_elf,
                "release_manifest_sha256": expected_manifest,
            })
            self.assertTrue(tool_elf_paths)
            self.assertTrue(all(path == tool_elf_paths[0] for path in tool_elf_paths))
            self.assertNotEqual(tool_elf_paths[0], fixture.outputs["elf"].resolve())
            self.assertFalse(tool_elf_paths[0].exists())

    def test_v4_acceptance_cli_writes_release_bound_result_without_observation_mode(self) -> None:
        from test_release_manifest import ReleaseFixture
        import release_manifest as release_manifest_module

        sections = "  [ 1] .text PROGBITS 06001000 001000 002004 00 AX 0 0 2\n"
        symbols = (
            "   1: 06001000 16 FUNC GLOBAL DEFAULT 1 _game_loop_one_iteration\n"
            "   2: 06002000 4 FUNC GLOBAL DEFAULT 1 ___mulsf3\n"
        )
        disassembly = (
            "06001000 <_game_loop_one_iteration>:\n"
            " 6001000: b7 fe bsr 6002000 <___mulsf3>\n"
            " 6001002: 00 09 nop\n"
            " 6001004: 00 0b rts\n"
            " 6001006: 00 09 nop\n"
            "06002000 <___mulsf3>:\n"
            " 6002000: 00 0b rts\n"
            " 6002002: 00 09 nop\n"
        )

        def fake_command(command: list[str]) -> str:
            if command[1] == "-d":
                return disassembly
            if command[1] == "-SW":
                return sections
            if command[1] == "-sW":
                return symbols
            if command[1] == "--debug-dump=decodedline":
                return "fixture.c 1 0x06001000\n"
            raise AssertionError(command)

        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as temporary:
            root = Path(temporary)
            fixture = ReleaseFixture(root)
            manifest = fixture.write()
            baseline = root / "baseline.txt"
            route_oracle = root / "route.txt"
            audit_oracle = root / "audit-route.txt"
            contract_path = root / "contract-v4.txt"
            report = root / "audit-v4.json"
            baseline.write_text(
                "BASELINE_VERSION 1\nHOT_CEILING 1\n"
                "HOT _game_loop_one_iteration ___mulsf3 1\n",
                encoding="utf-8",
            )
            route_oracle.write_text(
                "ROUTE_ORACLE_VERSION 1\nROOT _game_loop_one_iteration\n",
                encoding="utf-8",
            )
            audit_oracle.write_bytes(route_oracle.read_bytes())
            with release_manifest_module.verify_release_manifest(manifest) as release:
                document = release.document
                contract_text = self._v4_contract_text(
                    release_manifest=release.manifest_sha256,
                    identity=document["identity_sha256"],
                    effective_config=document["effective_config_sha256"],
                    target_profile=document["target_profile_sha256"],
                    elf=document["outputs"]["elf"]["sha256"],
                ).replace("EXPECTED_TOTAL 701", "EXPECTED_TOTAL 1")
                expected = {
                    "schema": "sm64-saturn-native-math-audit-v4-result-v1",
                    "status": "passed",
                    "root": "_game_loop_one_iteration",
                    "total": 1,
                    "callers": ["_game_loop_one_iteration"],
                    "verified_absent_callers": ["_atan2_lookup", "_atan2s"],
                    "audit_contract_sha256": hashlib.sha256(
                        contract_text.encode("utf-8")
                    ).hexdigest(),
                    "release_manifest_sha256": release.manifest_sha256,
                    "identity_sha256": document["identity_sha256"],
                    "effective_config_sha256": document["effective_config_sha256"],
                    "target_profile_sha256": document["target_profile_sha256"],
                    "elf_sha256": document["outputs"]["elf"]["sha256"],
                }
            contract_path.write_text(contract_text, encoding="utf-8")
            with patch.object(verifier, "run_command", side_effect=fake_command), \
                    patch.object(verifier, "verify_baseline_integrity"), \
                    patch.object(verifier, "verify_route_oracle_integrity"), \
                    patch.object(verifier, "verify_audit_contract_integrity"), \
                    patch.object(
                        verifier, "prove_sourceboot_bob_null_camera_triggers",
                        return_value=False,
                    ), patch.object(verifier, "source_locations", return_value={}):
                self.assertEqual(verifier.main([
                    str(fixture.outputs["elf"]), str(baseline),
                    "--route-oracle", str(route_oracle),
                    "--audit-route-oracle", str(audit_oracle),
                    "--audit-contract", str(contract_path),
                    "--release-manifest", str(manifest),
                    "--json-output", str(report),
                    "--objdump", "objdump", "--readelf", "readelf",
                    "--addr2line", "addr2line",
                ]), 0)
            self.assertEqual(json.loads(report.read_bytes()), expected)

    def test_measurement_cli_rejects_contract_or_missing_inputs_before_tools(self) -> None:
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as temporary:
            root = Path(temporary)
            elf = root / "target.elf"
            baseline = root / "baseline.txt"
            route = root / "route.txt"
            audit = root / "audit.txt"
            contract = root / "contract.txt"
            report = root / "measurement.json"
            elf.write_bytes(b"ELF")
            baseline.write_text("BASELINE_VERSION 1\nHOT_CEILING 0\n", encoding="utf-8")
            route.write_text("ROUTE_ORACLE_VERSION 1\nROOT _root\n", encoding="utf-8")
            audit.write_text(
                "ROUTE_ORACLE_VERSION 1\nROOT _game_loop_one_iteration\n",
                encoding="utf-8",
            )
            contract.write_text(self._v3_contract_text(), encoding="utf-8")
            common = [
                str(elf), str(baseline), "--route-oracle", str(route),
                "--objdump", "objdump", "--readelf", "readelf",
                "--addr2line", "addr2line", "--measure-audit-report", str(report),
            ]
            cases = (
                common,
                [*common, "--audit-route-oracle", str(audit)],
                [
                    *common, "--audit-route-oracle", str(audit),
                    "--audit-contract", str(contract),
                ],
            )
            with patch.object(verifier, "run_command") as run_command:
                for argv in cases:
                    with self.subTest(argv=argv[-4:]):
                        self.assertEqual(verifier.main(argv), 2)
                run_command.assert_not_called()

    def test_measurement_output_rejects_every_input_alias_before_verification_or_tools(self) -> None:
        from test_release_manifest import ReleaseFixture

        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as temporary:
            root = Path(temporary)
            fixture = ReleaseFixture(root)
            manifest = fixture.write()
            baseline = root / "baseline.txt"
            route = root / "route.txt"
            audit = root / "audit.txt"
            contract = root / "contract.txt"
            baseline.write_text("BASELINE_VERSION 1\nHOT_CEILING 0\n", encoding="utf-8")
            route.write_text("ROUTE_ORACLE_VERSION 1\nROOT _root\n", encoding="utf-8")
            audit.write_text(
                "ROUTE_ORACLE_VERSION 1\nROOT _game_loop_one_iteration\n",
                encoding="utf-8",
            )
            contract.write_text(self._v3_contract_text(), encoding="utf-8")
            inputs = {
                "ELF": fixture.outputs["elf"],
                "baseline": baseline,
                "route oracle": route,
                "audit route oracle": audit,
                "release manifest": manifest,
                "audit contract": contract,
            }

            def argv(report: Path, *, include_contract: bool = False) -> list[str]:
                result = [
                    str(fixture.outputs["elf"]), str(baseline),
                    "--route-oracle", str(route),
                    "--audit-route-oracle", str(audit),
                    "--measure-audit-report", str(report),
                    "--release-manifest", str(manifest),
                    "--objdump", "objdump", "--readelf", "readelf",
                    "--addr2line", "addr2line",
                ]
                if include_contract:
                    result.extend(("--audit-contract", str(contract)))
                return result

            originals = {name: path.read_bytes() for name, path in inputs.items()}
            with patch.object(verifier, "verify_baseline_integrity"), patch.object(
                verifier, "verify_route_oracle_integrity"
            ), patch.object(
                verifier, "verify_audit_contract_integrity"
            ), patch.object(
                verifier.release_manifest_module,
                "verify_release_manifest",
                side_effect=AssertionError("release verification called"),
            ) as verify_release, patch.object(
                verifier, "run_command", side_effect=AssertionError("tool called")
            ) as run_command:
                for name, source in inputs.items():
                    with self.subTest(kind="exact", source=name):
                        self.assertEqual(
                            verifier.main(argv(source, include_contract=name == "audit contract")),
                            2,
                        )
                nested = root / "nested"
                nested.mkdir()
                dotdot = nested / ".." / baseline.name
                with self.subTest(kind="dotdot"):
                    self.assertEqual(verifier.main(argv(dotdot)), 2)
                if os.name == "nt":
                    with self.subTest(kind="casefold"):
                        self.assertEqual(verifier.main(argv(Path(str(baseline).swapcase()))), 2)
                hardlink = root / "baseline-hardlink.json"
                os.link(baseline, hardlink)
                with self.subTest(kind="hardlink"):
                    self.assertEqual(verifier.main(argv(hardlink)), 2)
                symlink = root / "baseline-symlink.json"
                try:
                    symlink.symlink_to(baseline)
                except (OSError, NotImplementedError):
                    pass
                else:
                    with self.subTest(kind="symlink"):
                        self.assertEqual(verifier.main(argv(symlink)), 2)
                verify_release.assert_not_called()
                run_command.assert_not_called()
            for name, path in inputs.items():
                self.assertEqual(path.read_bytes(), originals[name])

    def test_release_manifest_is_only_legal_for_v4_or_measurement(self) -> None:
        from test_release_manifest import ReleaseFixture

        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as temporary:
            root = Path(temporary)
            fixture = ReleaseFixture(root)
            manifest = fixture.write()
            baseline = root / "baseline.txt"
            route = root / "route.txt"
            audit = root / "audit.txt"
            v2 = root / "contract-v2.txt"
            v3 = root / "contract-v3.txt"
            baseline.write_text("BASELINE_VERSION 1\nHOT_CEILING 0\n", encoding="utf-8")
            route.write_text("ROUTE_ORACLE_VERSION 1\nROOT _root\n", encoding="utf-8")
            audit.write_text(
                "ROUTE_ORACLE_VERSION 1\nROOT _game_loop_one_iteration\n",
                encoding="utf-8",
            )
            v2.write_text(
                "AUDIT_CONTRACT_VERSION 2\n"
                "EXPECTED_ROOT _game_loop_one_iteration\n"
                "EXPECTED_TOTAL 582\nFORBIDDEN_CALLER _atan2_lookup\n",
                encoding="utf-8",
            )
            v3.write_text(self._v3_contract_text(), encoding="utf-8")
            common = [
                str(fixture.outputs["elf"]), str(baseline),
                "--route-oracle", str(route),
                "--release-manifest", str(manifest),
                "--objdump", "objdump", "--readelf", "readelf",
                "--addr2line", "addr2line",
            ]
            cases = {
                "ordinary": common,
                "object-reference-only": [*common, "--object-reference-only"],
                "v2": [
                    *common, "--audit-route-oracle", str(audit),
                    "--audit-contract", str(v2),
                ],
                "v3": [
                    *common, "--audit-route-oracle", str(audit),
                    "--audit-contract", str(v3),
                ],
            }
            with patch.object(verifier, "verify_baseline_integrity"), patch.object(
                verifier, "verify_route_oracle_integrity"
            ), patch.object(verifier, "verify_audit_contract_integrity"), patch.object(
                verifier.release_manifest_module,
                "verify_release_manifest",
                side_effect=AssertionError("release verification called"),
            ) as verify_release, patch.object(
                verifier, "run_command", side_effect=AssertionError("tool called")
            ) as run_command:
                for name, case in cases.items():
                    with self.subTest(name=name):
                        errors = io.StringIO()
                        with redirect_stderr(errors):
                            self.assertEqual(verifier.main(case), 2)
                        self.assertIn(
                            "--release-manifest is only valid for audit v4 or measurement",
                            errors.getvalue(),
                        )
                verify_release.assert_not_called()
                run_command.assert_not_called()

    def test_measurement_rejects_preexisting_output_before_verification_or_tools(self) -> None:
        from test_release_manifest import ReleaseFixture

        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as temporary:
            root = Path(temporary)
            fixture = ReleaseFixture(root)
            manifest = fixture.write()
            baseline = root / "baseline.txt"
            route = root / "route.txt"
            audit = root / "audit.txt"
            report = root / "measurement.json"
            baseline.write_text("BASELINE_VERSION 1\nHOT_CEILING 0\n", encoding="utf-8")
            route.write_text("ROUTE_ORACLE_VERSION 1\nROOT _root\n", encoding="utf-8")
            audit.write_text(
                "ROUTE_ORACLE_VERSION 1\nROOT _game_loop_one_iteration\n",
                encoding="utf-8",
            )
            original = b"preserve preexisting output\n"
            report.write_bytes(original)
            with patch.object(verifier, "verify_baseline_integrity"), patch.object(
                verifier, "verify_route_oracle_integrity"
            ), patch.object(
                verifier.release_manifest_module,
                "verify_release_manifest",
                side_effect=AssertionError("release verification called"),
            ) as verify_release, patch.object(
                verifier, "run_command", side_effect=AssertionError("tool called")
            ) as run_command:
                self.assertEqual(verifier.main([
                    str(fixture.outputs["elf"]), str(baseline),
                    "--route-oracle", str(route),
                    "--audit-route-oracle", str(audit),
                    "--measure-audit-report", str(report),
                    "--release-manifest", str(manifest),
                    "--objdump", "objdump", "--readelf", "readelf",
                    "--addr2line", "addr2line",
                ]), 2)
                verify_release.assert_not_called()
                run_command.assert_not_called()
            self.assertEqual(report.read_bytes(), original)

    def test_measurement_late_input_aliases_fail_without_mutating_inputs(self) -> None:
        from test_release_manifest import ReleaseFixture

        sections = "  [ 1] .text PROGBITS 06001000 001000 002004 00 AX 0 0 2\n"
        symbols = (
            "   1: 06001000 16 FUNC GLOBAL DEFAULT 1 _game_loop_one_iteration\n"
            "   2: 06002000 4 FUNC GLOBAL DEFAULT 1 ___mulsf3\n"
        )
        disassembly = (
            "06001000 <_game_loop_one_iteration>:\n"
            " 6001000: b7 fe bsr 6002000 <___mulsf3>\n"
            " 6001002: 00 09 nop\n 6001004: 00 0b rts\n"
            " 6001006: 00 09 nop\n06002000 <___mulsf3>:\n"
            " 6002000: 00 0b rts\n 6002002: 00 09 nop\n"
        )

        for alias_kind in ("hardlink", "symlink"):
            with self.subTest(alias_kind=alias_kind), tempfile.TemporaryDirectory(
                dir=Path(__file__).parent
            ) as temporary:
                root = Path(temporary)
                fixture = ReleaseFixture(root)
                manifest = fixture.write()
                baseline = root / "baseline.txt"
                route = root / "route.txt"
                audit = root / "audit.txt"
                report = root / "measurement.json"
                baseline.write_text(
                    "BASELINE_VERSION 1\nHOT_CEILING 1\n"
                    "HOT _game_loop_one_iteration ___mulsf3 1\n",
                    encoding="utf-8",
                )
                route.write_text(
                    "ROUTE_ORACLE_VERSION 1\nROOT _game_loop_one_iteration\n",
                    encoding="utf-8",
                )
                audit.write_bytes(route.read_bytes())
                if alias_kind == "symlink":
                    probe = root / "symlink-probe"
                    try:
                        probe.symlink_to(baseline)
                    except (OSError, NotImplementedError):
                        continue
                    probe.unlink()
                originals = {
                    path: path.read_bytes()
                    for path in (
                        fixture.outputs["elf"], baseline, route, audit, manifest
                    )
                }
                calls: list[list[str]] = []

                def fake_command(command: list[str]) -> str:
                    calls.append(command)
                    if command[1] == "-d":
                        return disassembly
                    if command[1] == "-SW":
                        return sections
                    if command[1] == "-sW":
                        return symbols
                    if command[1] == "--debug-dump=decodedline":
                        if alias_kind == "hardlink":
                            os.link(baseline, report)
                        else:
                            report.symlink_to(baseline)
                        return "fixture.c 1 0x06001000\n"
                    raise AssertionError(command)

                with patch.object(
                    verifier, "run_command", side_effect=fake_command
                ), patch.object(
                    verifier, "verify_baseline_integrity"
                ), patch.object(
                    verifier, "verify_route_oracle_integrity"
                ), patch.object(
                    verifier, "prove_sourceboot_bob_null_camera_triggers",
                    return_value=False,
                ), patch.object(
                    verifier, "source_locations", return_value={}
                ):
                    self.assertEqual(verifier.main([
                        str(fixture.outputs["elf"]), str(baseline),
                        "--route-oracle", str(route),
                        "--audit-route-oracle", str(audit),
                        "--measure-audit-report", str(report),
                        "--release-manifest", str(manifest),
                        "--objdump", "objdump", "--readelf", "readelf",
                        "--addr2line", "addr2line",
                    ]), 2)
                self.assertEqual(len(calls), 4)
                for path, original in originals.items():
                    self.assertEqual(path.read_bytes(), original)

    def test_v4_cli_requires_release_manifest_before_tools_or_pin_lookup(self) -> None:
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as temporary:
            root = Path(temporary)
            elf = root / "target.elf"
            baseline = root / "baseline.txt"
            route = root / "route.txt"
            audit = root / "audit.txt"
            contract = root / "contract-v4.txt"
            elf.write_bytes(b"ELF")
            baseline.write_text(
                "BASELINE_VERSION 1\nHOT_CEILING 0\n", encoding="utf-8"
            )
            route.write_text(
                "ROUTE_ORACLE_VERSION 1\nROOT _root\n", encoding="utf-8"
            )
            audit.write_text(
                "ROUTE_ORACLE_VERSION 1\nROOT _game_loop_one_iteration\n",
                encoding="utf-8",
            )
            contract.write_text(self._v4_contract_text(), encoding="utf-8")
            with patch.object(verifier, "verify_baseline_integrity"), \
                    patch.object(verifier, "verify_route_oracle_integrity"), \
                    patch.object(verifier, "verify_audit_contract_integrity") as integrity, \
                    patch.object(verifier, "run_command") as run_command:
                self.assertEqual(verifier.main([
                    str(elf), str(baseline), "--route-oracle", str(route),
                    "--audit-route-oracle", str(audit),
                    "--audit-contract", str(contract),
                    "--objdump", "objdump", "--readelf", "readelf",
                    "--addr2line", "addr2line",
                ]), 2)
                integrity.assert_not_called()
                run_command.assert_not_called()

    def test_v3_audit_contract_requires_one_lowercase_exact_elf_sha256(self) -> None:
        contract = parse_audit_contract(self._v3_contract_text())
        self.assertEqual(contract.version, 3)
        self.assertEqual(contract.expected_elf_sha256, "a" * 64)
        for replacement in ("", "A" * 64, "a" * 63, "g" * 64):
            text = self._v3_contract_text(replacement)
            with self.subTest(replacement=replacement):
                with self.assertRaises(ValueError):
                    parse_audit_contract(text)
        duplicate = self._v3_contract_text() + f"EXPECTED_ELF_SHA256 {'b' * 64}\n"
        with self.assertRaisesRegex(ValueError, "duplicate expected ELF"):
            parse_audit_contract(duplicate)

    def test_v2_audit_contract_rejects_exact_elf_directive(self) -> None:
        text = (
            "AUDIT_CONTRACT_VERSION 2\n"
            "EXPECTED_ROOT _game_loop_one_iteration\n"
            "EXPECTED_TOTAL 582\n"
            f"EXPECTED_ELF_SHA256 {'a' * 64}\n"
            "FORBIDDEN_CALLER _atan2_lookup\n"
        )
        with self.assertRaisesRegex(ValueError, "v2.*EXPECTED_ELF_SHA256"):
            parse_audit_contract(text)

    def test_v3_target_binding_accepts_only_the_exact_file(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            elf = Path(temporary) / "target.elf"
            elf.write_bytes(b"sealed target")
            digest = hashlib.sha256(elf.read_bytes()).hexdigest()
            contract = parse_audit_contract(self._v3_contract_text(digest))
            verify_audit_contract_target(contract, elf)
            elf.write_bytes(b"sealed target!")
            with self.assertRaisesRegex(ValueError, "target ELF SHA-256 mismatch"):
                verify_audit_contract_target(contract, elf)

    def test_v2_target_binding_remains_artifact_agnostic(self) -> None:
        contract = parse_audit_contract(
            "AUDIT_CONTRACT_VERSION 2\n"
            "EXPECTED_ROOT _game_loop_one_iteration\n"
            "EXPECTED_TOTAL 582\n"
            "FORBIDDEN_CALLER _atan2_lookup\n"
        )
        with tempfile.TemporaryDirectory() as temporary:
            elf = Path(temporary) / "any.elf"
            elf.write_bytes(b"any historical artifact")
            verify_audit_contract_target(contract, elf)

    def test_main_rejects_wrong_v3_elf_before_invoking_sh_tools(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            elf = root / "wrong.elf"
            baseline = root / "baseline.txt"
            route_oracle = root / "route.txt"
            audit_oracle = root / "audit-route.txt"
            audit_contract = root / "audit-v3.txt"
            elf.write_bytes(b"wrong target")
            baseline.write_text(
                "BASELINE_VERSION 1\nHOT_CEILING 0\n", encoding="utf-8"
            )
            route_oracle.write_text(
                "ROUTE_ORACLE_VERSION 1\nROOT _game_loop_one_iteration\n",
                encoding="utf-8",
            )
            audit_oracle.write_text(
                "ROUTE_ORACLE_VERSION 1\nROOT _game_loop_one_iteration\n",
                encoding="utf-8",
            )
            audit_contract.write_text(
                self._v3_contract_text("a" * 64), encoding="utf-8"
            )
            with patch.object(
                bounded_verifier, "verify_baseline_integrity"
            ), patch.object(
                bounded_verifier, "verify_route_oracle_integrity"
            ), patch.object(
                bounded_verifier, "verify_audit_contract_integrity"
            ), patch.object(
                bounded_verifier, "run_command"
            ) as run_command:
                self.assertEqual(bounded_verifier.main([
                    str(elf), str(baseline),
                    "--route-oracle", str(route_oracle),
                    "--audit-route-oracle", str(audit_oracle),
                    "--audit-contract", str(audit_contract),
                    "--objdump", "objdump",
                    "--readelf", "readelf",
                    "--addr2line", "addr2line",
                ]), 2)
                run_command.assert_not_called()

    def test_checked_in_goal_audit_contract_v3_is_pinned(self) -> None:
        path = Path(__file__).parent / "sh2_native_math_goal_audit_contract_v3.txt"
        text = path.read_text(encoding="utf-8")
        contract = parse_audit_contract(text)
        verify_audit_contract_integrity(text, contract)
        self.assertEqual(contract.expected_total, 700)
        self.assertEqual(
            contract.expected_elf_sha256,
            "562fd6e47dd489f55f3c9d131ea2bca1fa417b8b3ce2c2ed90369db7d145978a",
        )
        with self.assertRaisesRegex(ValueError, "immutable audit contract digest mismatch"):
            verify_audit_contract_integrity(text.replace("700", "701"), contract)

    def test_checked_in_goal_audit_contract_v4_is_pinned(self) -> None:
        path = Path(__file__).parent / "sh2_native_math_goal_audit_contract_v4.txt"
        text = path.read_text(encoding="utf-8")
        contract = parse_audit_contract(text)
        self.assertEqual(
            GOAL_AUDIT_CONTRACT_V4_SHA256,
            "d52ecdb1d2a4f4847143af3d5e13629760ae8141f3fcecb413ad739b61ed7072",
        )
        verify_audit_contract_integrity(text, contract)
        self.assertEqual(contract.expected_total, 700)
        self.assertEqual(
            contract.expected_release_manifest_sha256,
            "5e04e2527e2373f30521bfcaaa63b56b061b74291cf1a4a3fd6e427aa311c1df",
        )
        with self.assertRaisesRegex(ValueError, "immutable audit contract digest mismatch"):
            verify_audit_contract_integrity(text.replace("700", "701"), contract)

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

    def test_next_lakitu_state_exact_r2_target_survives_neighboring_frame_stores(self) -> None:
        """Catch loss of the pinned +108 ___subsf3 target in the real instruction shape."""
        dis = """
06001000 <_next_lakitu_state>:
 6001000: a0 1a bra 6001038 <_next_lakitu_state+0x38>
 6001002: 7f 94 add #-108,r15
 6001038: c8 04 tst #4,r0
 600103a: 8d 46 bt.s 60010ca <_next_lakitu_state+0xca>
 600103c: e2 7c mov #124,r2
 600103e: d1 68 mov.l 60011e0 <_next_lakitu_state+0x1e0>,r1 ! 06003000 <_sMarioCamState>
 6001040: 32 fc add r15,r2
 6001042: 5c 24 mov.l @(16,r2),r12
 6001044: 66 d3 mov r13,r6
 6001046: 61 12 mov.l @r1,r1
 6001048: 69 f3 mov r15,r9
 600104a: 5a 25 mov.l @(20,r2),r10
 600104c: 6e f3 mov r15,r14
 600104e: d2 65 mov.l 60011e4 <_next_lakitu_state+0x1e4>,r2 ! 06002000 <___subsf3>
 6001050: 63 c3 mov r12,r3
 6001052: d8 65 mov.l 60011e8 <_next_lakitu_state+0x1e8>,r8 ! 06002100 <___addsf3>
 6001054: 71 04 add #4,r1
 6001056: 76 14 add #20,r6
 6001058: 79 54 add #84,r9
 600105a: 7e 60 add #96,r14
 600105c: 73 0c add #12,r3
 600105e: 67 c6 mov.l @r12+,r7
 6001060: 64 16 mov.l @r1+,r4
 6001062: 65 66 mov.l @r6+,r5
 6001064: 1f 63 mov.l r6,@(12,r15)
 6001066: 1f 12 mov.l r1,@(8,r15)
 6001068: 1f 21 mov.l r2,@(4,r15)
 600106a: 1f 34 mov.l r3,@(16,r15)
  600106c: 42 0b jsr @r2
  600106e: 1f 7a mov.l r7,@(40,r15)
 6001070: 54 fa mov.l @(40,r15),r4
 6001072: 65 03 mov r0,r5
 6001074: 48 0b jsr @r8
 6001076: 1f 05 mov.l r0,@(20,r15)
 6001078: 57 f5 mov.l @(20,r15),r7
 600107a: 29 02 mov.l r0,@r9
 600107c: 65 73 mov r7,r5
 600107e: 48 0b jsr @r8
 6001080: 64 a6 mov.l @r10+,r4
 6001082: 53 f4 mov.l @(16,r15),r3
 6001084: 79 04 add #4,r9
 6001086: 2e 02 mov.l r0,@r14
 6001088: 33 c0 cmp/eq r12,r3
 600108a: 51 f2 mov.l @(8,r15),r1
 600108c: 7e 04 add #4,r14
 600108e: 52 f1 mov.l @(4,r15),r2
 6001090: 8f e5 bf.s 600105e <_next_lakitu_state+0x5e>
 6001092: 56 f3 mov.l @(12,r15),r6
 6001094: 00 0b rts
 6001096: 00 09 nop
 60010ca: 00 0b rts
 60010cc: 00 09 nop
06002000 <___subsf3>:
 6002000: 00 0b rts
 6002002: 00 09 nop
06002100 <___addsf3>:
 6002100: 00 0b rts
 6002102: 00 09 nop
"""
        result = self.analyze_named_fixture(dis, """
   1: 06001000 206 FUNC GLOBAL DEFAULT 1 _next_lakitu_state
   2: 06002000 4 FUNC GLOBAL DEFAULT 1 ___subsf3
   3: 06002100 4 FUNC GLOBAL DEFAULT 1 ___addsf3
""")
        self.assertIn(
            DirectCallFact(
                "_next_lakitu_state", 108, "___subsf3", 0, 1,
                "owner", None,
            ),
            result.direct_calls,
            result.unresolved_transfers,
        )
        self.assertFalse(any(
            item.caller == "_next_lakitu_state" and item.address == 0x600106C
            for item in result.unresolved_transfers
        ))

    def test_update_lakitu_exact_delay_spill_survives_adjacent_output_pointer(self) -> None:
        """Catch poisoning the pinned +464 ___addsf3 spill at its exclusive end."""
        dis = self._update_lakitu_spill_fixture("""
 60011b2: 67 f3 mov r15,r7
 60011b4: 56 b6 mov.l @(24,r11),r6
 60011b6: 77 10 add #16,r7
""")
        result = self.analyze_named_fixture(dis, self._update_lakitu_spill_symbols())
        self.assertIn(
            DirectCallFact(
                "_update_lakitu", 464, "___addsf3", 0, 1,
                "owner", None,
            ),
            result.direct_calls,
            result.unresolved_transfers,
        )
        self.assertFalse(any(
            item.caller == "_update_lakitu" and item.address == 0x60011D0
            for item in result.unresolved_transfers
        ))

    def test_update_lakitu_unknown_frame_alias_still_poison_spill(self) -> None:
        """An unbounded near-match output alias must not fabricate the +464 call."""
        dis = self._update_lakitu_spill_fixture("""
 60011b2: 67 f3 mov r15,r7
 60011b4: 66 43 mov r4,r6
 60011b6: 37 6c add r6,r7
""")
        result = self.analyze_named_fixture(dis, self._update_lakitu_spill_symbols())
        self.assertNotIn(
            DirectCallFact(
                "_update_lakitu", 464, "___addsf3", 0, 1,
                "owner", None,
            ),
            result.direct_calls,
        )
        self.assertTrue(any(
            item.caller == "_update_lakitu" and item.address == 0x60011D0
            for item in result.unresolved_transfers
        ))

    @staticmethod
    def _update_lakitu_spill_symbols() -> str:
        return """
   1: 06001000 488 FUNC GLOBAL DEFAULT 1 _update_lakitu
   2: 06002000 4 FUNC GLOBAL DEFAULT 1 ___addsf3
   3: 06002100 6 FUNC GLOBAL DEFAULT 1 _find_floor
   4: 06002200 4 FUNC GLOBAL DEFAULT 1 ___eqsf2
"""

    @staticmethod
    def _update_lakitu_spill_fixture(alias_setup: str) -> str:
        return f"""
06001000 <_update_lakitu>:
 6001000: a0 d1 bra 60011a6 <_update_lakitu+0x1a6>
 6001002: 7f d4 add #-44,r15
 60011a6: d1 3a mov.l 6001290 <_update_lakitu+0x290>,r1 ! 06002000 <___addsf3>
 60011a8: d5 3a mov.l 6001294 <_update_lakitu+0x294>,r5 ! 41a00000
 60011aa: 41 0b jsr @r1
 60011ac: 1f 13 mov.l r1,@(12,r15)
 60011ae: 65 03 mov r0,r5
 60011b0: d0 39 mov.l 6001298 <_update_lakitu+0x298>,r0 ! 06002100 <_find_floor>
{alias_setup.rstrip()}
 60011b8: 40 0b jsr @r0
 60011ba: 54 b4 mov.l @(16,r11),r4
 60011bc: 64 03 mov r0,r4
 60011be: 6d 03 mov r0,r13
 60011c0: d0 36 mov.l 600129c <_update_lakitu+0x29c>,r0 ! 06002200 <___eqsf2>
 60011c2: d5 37 mov.l 60012a0 <_update_lakitu+0x2a0>,r5 ! c62be000
 60011c4: 40 0b jsr @r0
 60011c6: 00 09 nop
 60011c8: 20 08 tst r0,r0
 60011ca: 8d 0b bt.s 60011e4 <_update_lakitu+0x1e4>
 60011cc: 51 f3 mov.l @(12,r15),r1
 60011ce: d5 35 mov.l 60012a4 <_update_lakitu+0x2a4>,r5 ! 42c80000
 60011d0: 41 0b jsr @r1
 60011d2: 64 d3 mov r13,r4
 60011d4: 00 0b rts
 60011d6: 00 09 nop
 60011e4: 00 0b rts
 60011e6: 00 09 nop
06002000 <___addsf3>:
 6002000: 00 0b rts
 6002002: 00 09 nop
06002100 <_find_floor>:
 6002100: 27 02 mov.l r0,@r7
 6002102: 00 0b rts
 6002104: 00 09 nop
06002200 <___eqsf2>:
 6002200: 00 0b rts
 6002202: 00 09 nop
"""

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

    def test_nonleaf_call_preserves_neighbor_of_escaped_stack_address(self) -> None:
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
        self.assertIn(CallSite("_root", 0x6001012, "_child"), result.calls)
        self.assertFalse(any(
            item.address == 0x6001012 for item in result.unresolved_transfers
        ))

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

    def test_bounded_bf_mnemonic_reaches_taken_arm_without_decoded_seed(self) -> None:
        dis = """
06001000 <_root>:
 6001000: 20 08 tst r0,r0
 6001002: 8b 01 bf 6001008 <_root+0x8>
 6001004: 00 0b rts
 6001006: 00 09 nop
 6001008: b0 0a bsr 6001020 <_child>
 600100a: 00 09 nop
 600100c: 00 09 nop
 600100e: 00 0b rts
 6001010: 00 09 nop
06001020 <_child>:
 6001020: 00 0b rts
 6001022: 00 09 nop
"""
        owners = (
            FunctionOwner("_root", 0x6001000, 0x6001012, 1),
            FunctionOwner("_child", 0x6001020, 0x6001024, 1),
        )
        oracle = parse_route_oracle("ROUTE_ORACLE_VERSION 1\nROOT _root\n")
        bounded = bounded_verifier.prepare_route_bounded_code_only(
            dis, "", owners, (oracle,)
        )
        self.assertEqual(bounded.selected_names, frozenset({"_root", "_child"}))

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


import verify_sh2_native_math as bounded_verifier


class RouteBoundedLinkedElfTests(unittest.TestCase):
    OWNERS = (
        FunctionOwner("_route_root", 0x06001000, 0x06001010, 1),
        FunctionOwner("_route_child", 0x06002000, 0x06002008, 1),
        FunctionOwner("_irrelevant_large", 0x06003000, 0x06007000, 1),
    )
    ORACLE = bounded_verifier.RouteOracle(
        1, frozenset({"_route_root"}), frozenset(), frozenset()
    )

    @staticmethod
    def _owner_identity(owner: FunctionOwner) -> str:
        return bounded_verifier._bounded_owner_identity(owner)

    @staticmethod
    def _duplicate_local_owners() -> tuple[FunctionOwner, ...]:
        sections = parse_readelf_sections(
            "  [ 1] .text PROGBITS 06001000 001000 008004 00 AX 0 0 2\n"
        )
        symbols = parse_readelf_symbols(
            """
   1: 06001000 8 FUNC GLOBAL DEFAULT 1 _route_root
   2: 06002000 8 FUNC LOCAL DEFAULT 1 local_helper
   3: 06003000 4 FUNC LOCAL DEFAULT 1 local_helper
   4: 06009000 4 FUNC GLOBAL DEFAULT 1 ___addsf3
""",
            sections,
        )
        return resolve_function_owners(symbols, sections)

    @staticmethod
    def _route_disassembly(*, include_large: bool = True) -> str:
        text = """
06001000 <_route_root>:
 6001000: d1 02 mov.l 600100c <_route_root+0xc>,r1 ! 06002000 <_route_child>
 6001002: 41 0b jsr @r1
 6001004: 00 09 nop
 6001006: 00 0b rts
 6001008: 00 09 nop
06002000 <_route_child>:
 6002000: 00 0b rts
 6002002: 00 09 nop
"""
        if include_large:
            text += "06003000 <_irrelevant_large>:\n"
            text += "".join(
                f" {address:x}: 00 09 nop\n"
                for address in range(0x06003000, 0x06007000, 2)
            )
        return text

    @staticmethod
    def _analyze(prepared):
        return analyze_code_only(
            prepared.instructions,
            RouteBoundedLinkedElfTests.OWNERS,
            prepared.decoded_lines,
            instruction_memory=build_instruction_memory(prepared.instructions),
            selected_owner_identities=set(prepared.selected_identities),
        )

    def test_bounded_preparation_matches_full_route_and_skips_huge_irrelevant_block(self) -> None:
        disassembly = self._route_disassembly()
        decoded = """
fixture.c 1 0x06001002
fixture.c 2 0x06002002
fixture.c 3 0x06003002
"""
        prepared = bounded_verifier.prepare_route_bounded_code_only(
            disassembly, decoded, self.OWNERS, (self.ORACLE,)
        )
        self.assertEqual(prepared.closure, frozenset({
            self._owner_identity(self.OWNERS[0]),
            self._owner_identity(self.OWNERS[1]),
        }))
        self.assertEqual(prepared.selected_identities, prepared.closure)
        self.assertEqual(
            prepared.selected_names, frozenset({"_route_root", "_route_child"})
        )
        self.assertEqual(set(prepared.decoded_lines), {
            self._owner_identity(self.OWNERS[0]),
            self._owner_identity(self.OWNERS[1]),
        })
        self.assertLess(len(prepared.instructions), 10)
        self.assertNotIn(0x06003000, prepared.instructions)

        full_instructions = parse_instructions(disassembly)
        full = analyze_code_only(
            full_instructions,
            self.OWNERS,
            parse_decoded_lines(decoded, self.OWNERS),
            set(prepared.selected_names),
            build_instruction_memory(full_instructions),
        )
        bounded = self._analyze(prepared)
        self.assertEqual(bounded.calls, full.calls)
        self.assertEqual(bounded.direct_calls, full.direct_calls)
        self.assertEqual(bounded.unresolved_transfers, full.unresolved_transfers)
        self.assertEqual(bounded.unresolved_effects, full.unresolved_effects)
        self.assertEqual(bounded.code_addresses, full.code_addresses)

    def test_bounded_preparation_rejects_missing_route_root(self) -> None:
        missing = bounded_verifier.RouteOracle(
            1, frozenset({"_missing_root"}), frozenset(), frozenset()
        )
        with self.assertRaisesRegex(ValueError, "missing route root"):
            bounded_verifier.prepare_route_bounded_code_only(
                self._route_disassembly(include_large=False),
                "",
                self.OWNERS[:2],
                (missing,),
            )

    def test_bounded_preparation_rejects_reachable_block_without_owner(self) -> None:
        disassembly = """
06001000 <_route_root>:
 6001000: d1 02 mov.l 600100c <_route_root+0xc>,r1 ! 06008000 <_orphan>
 6001002: 41 0b jsr @r1
 6001004: 00 09 nop
 6001006: 00 0b rts
 6001008: 00 09 nop
06008000 <_orphan>:
 6008000: 00 0b rts
 6008002: 00 09 nop
"""
        with self.assertRaisesRegex(ValueError, "no linked owner"):
            bounded_verifier.prepare_route_bounded_code_only(
                disassembly,
                "fixture.c 1 0x06001002\n",
                self.OWNERS[:1],
                (self.ORACLE,),
            )

    def test_route_oracle_rejects_stale_owner_and_missing_owned_block(self) -> None:
        self.assertTrue(
            hasattr(bounded_verifier, "validate_route_oracle_owned_blocks"),
            "linked route-oracle owner validation is missing",
        )
        stale = bounded_verifier.RouteOracle(
            1, frozenset({"_stale_root"}), frozenset(), frozenset()
        )
        with self.assertRaisesRegex(ValueError, "no linked owner"):
            bounded_verifier.validate_route_oracle_owned_blocks(
                self._route_disassembly(include_large=False),
                self.OWNERS[:2],
                stale,
            )

        linked_edge = ("_route_root", "_route_child")
        oracle = bounded_verifier.RouteOracle(
            1,
            frozenset({"_route_root"}),
            frozenset({linked_edge}),
            frozenset({linked_edge}),
        )
        root_only = """
06001000 <_route_root>:
 6001000: 00 0b rts
 6001002: 00 09 nop
"""
        with self.assertRaisesRegex(ValueError, "no owned block.*_route_child"):
            bounded_verifier.validate_route_oracle_owned_blocks(
                root_only,
                self.OWNERS[:2],
                oracle,
            )

    def test_internal_local_header_call_is_owned_by_enclosing_route_function(self) -> None:
        disassembly = """
06001000 <_route_root>:
 6001000: d1 02 mov.l 600100c <_route_root+0xc>,r1 ! 06002000 <_route_child>
06001002 <.Lroute_inner>:
 6001002: 41 0b jsr @r1
 6001004: 00 09 nop
 6001006: 00 0b rts
 6001008: 00 09 nop
06002000 <_route_child>:
 6002000: b0 02 bsr 6009000 <___addsf3>
 6002002: 00 09 nop
 6002004: 00 0b rts
 6002006: 00 09 nop
"""
        owners = (*self.OWNERS[:2], FunctionOwner(
            "___addsf3", 0x06009000, 0x06009004, 1
        ))
        prepared = bounded_verifier.prepare_route_bounded_code_only(
            disassembly,
            "fixture.c 1 0x06001002\nfixture.c 2 0x06002000\n",
            owners,
            (self.ORACLE,),
        )
        self.assertEqual(prepared.closure, frozenset({
            self._owner_identity(owners[0]),
            self._owner_identity(owners[1]),
        }))
        analysis = analyze_code_only(
            prepared.instructions,
            owners,
            instruction_memory=build_instruction_memory(prepared.instructions),
            selected_owner_identities=set(prepared.selected_identities),
        )
        self.assertIn(
            ("_route_child", "___addsf3"),
            {(call.caller, call.helper) for call in analysis.calls},
        )

    def test_bounded_scanner_ignores_faux_rodata_bsr(self) -> None:
        disassembly = """
06001000 <_route_root>:
 6001000: 00 0b rts
 6001002: 00 09 nop
06008000 <_route_root>:
 6008000: b0 02 bsr 6002000 <_route_child>
 6008002: 00 09 nop
"""
        self.assertEqual(
            bounded_verifier.scan_direct_calls(disassembly),
            [CallSite("_route_root", 0x06008000, "_route_child")],
        )
        self.assertEqual(
            bounded_verifier.scan_direct_calls(disassembly, self.OWNERS[:2]),
            [],
        )
        prepared = bounded_verifier.prepare_route_bounded_code_only(
            disassembly, "", self.OWNERS[:2], (self.ORACLE,)
        )
        self.assertEqual(
            prepared.closure,
            frozenset({self._owner_identity(self.OWNERS[0])}),
        )

    def test_bounded_duplicate_local_name_call_uses_target_address_identity(self) -> None:
        owners = self._duplicate_local_owners()
        disassembly = """
06001000 <_route_root>:
 6001000: b0 02 bsr 6003000 <local_helper>
 6001002: 00 09 nop
 6001004: 00 0b rts
 6001006: 00 09 nop
06002000 <local_helper>:
 6002000: b0 02 bsr 6009000 <___addsf3>
 6002002: 00 09 nop
 6002004: 00 0b rts
 6002006: 00 09 nop
06003000 <local_helper>:
 6003000: 00 0b rts
 6003002: 00 09 nop
06009000 <___addsf3>:
 6009000: 00 0b rts
 6009002: 00 09 nop
"""
        prepared = bounded_verifier.prepare_route_bounded_code_only(
            disassembly, "", owners, (self.ORACLE,)
        )
        root_id = "@owner:1:06001000:_route_root"
        first_local_id = "@owner:1:06002000:local_helper"
        second_local_id = "@owner:1:06003000:local_helper"
        self.assertEqual(prepared.graph[root_id], {second_local_id})
        self.assertEqual(
            prepared.closure, frozenset({root_id, second_local_id})
        )
        self.assertEqual(prepared.selected_identities, prepared.closure)
        self.assertEqual(
            prepared.selected_names, frozenset({"_route_root", "local_helper"})
        )
        self.assertNotIn(first_local_id, prepared.closure)
        self.assertEqual(
            set(prepared.instructions),
            {
                0x06001000, 0x06001002, 0x06001004, 0x06001006,
                0x06003000, 0x06003002,
            },
        )
        decoded = parse_decoded_lines(
            "fixture.c 1 0x06002002\nfixture.c 2 0x06003002\n",
            owners,
            selected_owner_identities={second_local_id},
        )
        self.assertEqual(decoded, {second_local_id: {0x06003002}})
        full_instructions = parse_instructions(disassembly)
        analysis = analyze_code_only(
            full_instructions,
            owners,
            decoded,
            instruction_memory=build_instruction_memory(full_instructions),
            selected_owner_identities={second_local_id},
        )
        self.assertEqual(analysis.calls, [])
        self.assertEqual(analysis.code_addresses, {0x06003000, 0x06003002})

    def test_bounded_duplicate_local_name_route_root_is_ambiguous(self) -> None:
        owners = self._duplicate_local_owners()[1:3]
        oracle = bounded_verifier.RouteOracle(
            1, frozenset({"local_helper"}), frozenset(), frozenset()
        )
        disassembly = """
06002000 <local_helper>:
 6002000: 00 0b rts
 6002002: 00 09 nop
06003000 <local_helper>:
 6003000: 00 0b rts
 6003002: 00 09 nop
"""
        with self.assertRaisesRegex(
            ValueError, "ambiguous route root owner: local_helper"
        ):
            bounded_verifier.prepare_route_bounded_code_only(
                disassembly, "", owners, (oracle,)
            )

    def test_cfg_ignores_unreachable_literal_pool_word_decoded_as_bsr(self) -> None:
        """A literal word may decode as BSR but is not a call without CFG proof."""
        owners = (
            FunctionOwner(
                "_geo_layout_cmd_node_translation_rotation",
                0x06006900,
                0x060069A4,
                1,
            ),
            FunctionOwner("_literal_decoy", 0x060075B2, 0x060075BA, 1),
        )
        oracle = bounded_verifier.RouteOracle(
            1,
            frozenset({"_geo_layout_cmd_node_translation_rotation"}),
            frozenset(),
            frozenset(),
        )
        disassembly = """
06006900 <_geo_layout_cmd_node_translation_rotation>:
 6006900: d2 25 mov.l 6006998 <_geo_layout_cmd_node_translation_rotation+0x98>,r2 ! b60b60b7
 6006902: 01 23 braf r1
 6006904: 00 09 nop
 6006906: 00 09 nop
 6006908: 00 09 nop
 600690a: 00 09 nop
 600690c: 00 09 nop
 600690e: 00 09 nop
 6006910: 00 0b rts
 6006912: 00 09 nop
 6006998: b6 0b bsr 60075b2 <_literal_decoy>
 600699a: 60 b7 not r11,r0
060075b2 <_literal_decoy>:
 60075b2: 00 0b rts
 60075b4: 00 09 nop
"""
        prepared = bounded_verifier.prepare_route_bounded_code_only(
            disassembly,
            "fixture.c 1 0x06006900\n",
            owners,
            (oracle,),
        )
        root_id = self._owner_identity(owners[0])
        self.assertEqual(prepared.graph[root_id], set())
        self.assertEqual(prepared.closure, frozenset({root_id}))
        self.assertNotIn(self._owner_identity(owners[1]), prepared.closure)

    def test_cfg_follows_literal_resolved_indirect_tail_to_helper_call(self) -> None:
        """A bounded route retains a literal-resolved local ``jmp @rN`` tail."""
        owners = (
            FunctionOwner("_route_root", 0x06001000, 0x06001020, 1),
            FunctionOwner("_route_child", 0x06002000, 0x06002008, 1),
            FunctionOwner("___addsf3", 0x06009000, 0x06009008, 1),
        )
        oracle = bounded_verifier.RouteOracle(
            1, frozenset({"_route_root"}), frozenset(), frozenset()
        )
        disassembly = """
06001000 <_route_root>:
 6001000: d1 02 mov.l 600100c <_route_root+0xc>,r1 ! 06001010 <_route_root+0x10>
 6001002: 41 2b jmp @r1
 6001004: 00 09 nop
 6001010: b0 02 bsr 6002000 <_route_child>
 6001012: 00 09 nop
 6001014: 00 0b rts
 6001016: 00 09 nop
06002000 <_route_child>:
 6002000: b0 02 bsr 6009000 <___addsf3>
 6002002: 00 09 nop
 6002004: 00 0b rts
 6002006: 00 09 nop
06009000 <___addsf3>:
 6009000: 00 0b rts
 6009002: 00 09 nop
"""
        prepared = bounded_verifier.prepare_route_bounded_code_only(
            disassembly,
            "fixture.c 1 0x06001000\n",
            owners,
            (oracle,),
        )
        root_id = self._owner_identity(owners[0])
        child_id = self._owner_identity(owners[1])
        self.assertEqual(prepared.graph[root_id], {child_id})
        self.assertEqual(prepared.closure, frozenset({root_id, child_id}))
        analysis = analyze_code_only(
            prepared.instructions,
            owners,
            prepared.decoded_lines,
            selected_owner_identities=set(prepared.selected_identities),
        )
        self.assertTrue(any(
            call.caller == "_route_child" and call.helper == "___addsf3"
            for call in analysis.calls
        ))

    def test_cfg_follows_gu_mtx_ident_shape_cross_owner_literal_tail(self) -> None:
        """The candidate's entry-load tail jump retains its linked successor."""
        owners = (
            FunctionOwner("_guMtxIdent", 0x06004C24, 0x06004C30, 1),
            FunctionOwner("_guMtxIdentF", 0x06004BF0, 0x06004C24, 1),
        )
        oracle = bounded_verifier.RouteOracle(
            1,
            frozenset({"_guMtxIdent"}),
            frozenset(),
            frozenset(),
        )
        disassembly = """
06004bf0 <_guMtxIdentF>:
 6004bf0: 00 0b rts
 6004bf2: 00 09 nop
06004c24 <_guMtxIdent>:
 6004c24: d1 01 mov.l 6004c2c <_guMtxIdent+0x8>,r1 ! 06004bf0 <_guMtxIdentF>
 6004c26: 41 2b jmp @r1
 6004c28: 00 09 nop
 6004c2a: 00 09 nop
 6004c2c: 06 00 .word 0x0600
 6004c2e: 4b f0 .word 0x4bf0
"""
        prepared = bounded_verifier.prepare_route_bounded_code_only(
            disassembly,
            "fixture.c 1 0x06004c24\n",
            owners,
            (oracle,),
        )
        source_id = self._owner_identity(owners[0])
        target_id = self._owner_identity(owners[1])
        self.assertEqual(prepared.graph[source_id], {target_id})
        self.assertEqual(prepared.closure, frozenset({source_id, target_id}))

    def test_cfg_does_not_hide_unresolved_indirect_tail_call_suffix_as_literal_pool(self) -> None:
        """An unproven local tail is not classified as a literal pool."""
        owners = (
            FunctionOwner("_route_root", 0x06001000, 0x06001020, 1),
            FunctionOwner("_route_child", 0x06002000, 0x06002008, 1),
        )
        oracle = bounded_verifier.RouteOracle(
            1, frozenset({"_route_root"}), frozenset(), frozenset()
        )
        disassembly = """
06001000 <_route_root>:
 6001000: 41 2b jmp @r1
 6001002: 00 09 nop
 6001010: b0 02 bsr 6002000 <_route_child>
 6001012: 00 09 nop
 6001014: 00 0b rts
 6001016: 00 09 nop
06002000 <_route_child>:
 6002000: 00 0b rts
 6002002: 00 09 nop
"""
        with self.assertRaisesRegex(
            ValueError, "no decoded code provenance"
        ):
            bounded_verifier.prepare_route_bounded_code_only(
                disassembly,
                "fixture.c 1 0x06001000\n",
                owners,
                (oracle,),
            )

    def test_cfg_rejects_indirect_tail_when_dwarf_seed_bypasses_literal_load(self) -> None:
        """A nearby literal load is not proof when control enters at the jump."""
        owners = (
            FunctionOwner("_route_root", 0x06001000, 0x06001020, 1),
            FunctionOwner("_route_child", 0x06002000, 0x06002008, 1),
        )
        oracle = bounded_verifier.RouteOracle(
            1, frozenset({"_route_root"}), frozenset(), frozenset()
        )
        disassembly = """
06001000 <_route_root>:
 6001000: 00 0b rts
 6001002: 00 09 nop
 6001010: d1 02 mov.l 600101c <_route_root+0x1c>,r1 ! 06001018 <_route_root+0x18>
 6001012: 41 2b jmp @r1
 6001014: 00 09 nop
 6001018: b0 02 bsr 6002000 <_route_child>
 600101a: 00 09 nop
 600101c: 00 0b rts
 600101e: 00 09 nop
06002000 <_route_child>:
 6002000: 00 0b rts
 6002002: 00 09 nop
"""
        with self.assertRaisesRegex(
            ValueError, "no decoded code provenance"
        ):
            bounded_verifier.prepare_route_bounded_code_only(
                disassembly,
                "fixture.c 1 0x06001000\nfixture.c 2 0x06001012\n",
                owners,
                (oracle,),
            )

    def test_cfg_unresolved_branch_arm_prevents_other_arm_from_blessing_pool(self) -> None:
        """A return on one arm cannot classify an unknown jump target as data."""
        owners = (
            FunctionOwner("_route_root", 0x06001000, 0x06001030, 1),
            FunctionOwner("_route_child", 0x06002000, 0x06002008, 1),
            FunctionOwner("_declared_callback", 0x06003000, 0x06003008, 1),
        )
        oracle = bounded_verifier.RouteOracle(
            1,
            frozenset({"_route_root"}),
            frozenset({"_declared_callback"}),
            frozenset({("_route_root", "_declared_callback")}),
        )
        disassembly = """
06001000 <_route_root>:
 6001000: 89 06 bt 6001010 <_route_root+0x10>
 6001002: 41 2b jmp @r1
 6001004: 00 09 nop
 6001010: 00 0b rts
 6001012: 00 09 nop
 6001020: b0 02 bsr 6002000 <_route_child>
 6001022: 00 09 nop
 6001024: 00 0b rts
 6001026: 00 09 nop
06002000 <_route_child>:
 6002000: 00 0b rts
 6002002: 00 09 nop
06003000 <_declared_callback>:
 6003000: 00 0b rts
 6003002: 00 09 nop
"""
        with self.assertRaisesRegex(
            ValueError, "no decoded code provenance"
        ):
            bounded_verifier.prepare_route_bounded_code_only(
                disassembly,
                "fixture.c 1 0x06001000\n",
                owners,
                (oracle,),
            )

    def test_cfg_literal_tail_load_must_dominate_shared_jmp(self) -> None:
        """One loaded arm cannot resolve a shared jump reached by another arm."""
        owners = (
            FunctionOwner("_route_root", 0x06001000, 0x06001030, 1),
            FunctionOwner("_route_child", 0x06002000, 0x06002008, 1),
            FunctionOwner("_declared_callback", 0x06003000, 0x06003008, 1),
            FunctionOwner("___addsf3", 0x06009000, 0x06009008, 1),
        )
        edge = ("_route_root", "_declared_callback")
        oracle = bounded_verifier.RouteOracle(
            1,
            frozenset({"_route_root"}),
            frozenset({edge}),
            frozenset({edge}),
        )
        disassembly = """
06001000 <_route_root>:
 6001000: 89 02 bt 6001008 <_route_root+0x8>
 6001002: a0 02 bra 600100a <_route_root+0xa>
 6001004: 00 09 nop
 6001006: 00 09 nop
 6001008: d1 03 mov.l 6001018 <_route_root+0x18>,r1 ! 06001010 <_route_root+0x10>
 600100a: 41 2b jmp @r1
 600100c: 00 09 nop
 6001010: 00 0b rts
 6001012: 00 09 nop
 6001020: b0 02 bsr 6002000 <_route_child>
 6001022: 00 09 nop
 6001024: 00 0b rts
 6001026: 00 09 nop
06002000 <_route_child>:
 6002000: b0 02 bsr 6009000 <___addsf3>
 6002002: 00 09 nop
 6002004: 00 0b rts
 6002006: 00 09 nop
06003000 <_declared_callback>:
 6003000: 00 0b rts
 6003002: 00 09 nop
06009000 <___addsf3>:
 6009000: 00 0b rts
 6009002: 00 09 nop
"""
        with self.assertRaisesRegex(ValueError, "no decoded code provenance"):
            bounded_verifier.prepare_route_bounded_code_only(
                disassembly,
                "fixture.c 1 0x06001000\n",
                owners,
                (oracle,),
            )

    def test_cfg_literal_call_load_must_dominate_shared_jsr(self) -> None:
        """Linear preflight state cannot resolve a shared call on every path."""
        owners = (
            FunctionOwner("_route_root", 0x06001000, 0x06001030, 1),
            FunctionOwner("_route_child", 0x06002000, 0x06002008, 1),
            FunctionOwner("_declared_callback", 0x06003000, 0x06003008, 1),
            FunctionOwner("___addsf3", 0x06009000, 0x06009008, 1),
        )
        edge = ("_route_root", "_declared_callback")
        oracle = bounded_verifier.RouteOracle(
            1,
            frozenset({"_route_root"}),
            frozenset({edge}),
            frozenset({edge}),
        )
        disassembly = """
06001000 <_route_root>:
 6001000: 89 02 bt 6001008 <_route_root+0x8>
 6001002: a0 02 bra 600100a <_route_root+0xa>
 6001004: 00 09 nop
 6001006: 00 09 nop
 6001008: d1 03 mov.l 6001018 <_route_root+0x18>,r1 ! 06003000 <_declared_callback>
 600100a: 41 0b jsr @r1
 600100c: 00 09 nop
 600100e: 00 0b rts
 6001010: 00 09 nop
 6001020: b0 02 bsr 6002000 <_route_child>
 6001022: 00 09 nop
 6001024: 00 0b rts
 6001026: 00 09 nop
06002000 <_route_child>:
 6002000: b0 02 bsr 6009000 <___addsf3>
 6002002: 00 09 nop
 6002004: 00 0b rts
 6002006: 00 09 nop
06003000 <_declared_callback>:
 6003000: 00 0b rts
 6003002: 00 09 nop
06009000 <___addsf3>:
 6009000: 00 0b rts
 6009002: 00 09 nop
"""
        with self.assertRaisesRegex(ValueError, "no decoded code provenance"):
            bounded_verifier.prepare_route_bounded_code_only(
                disassembly,
                "fixture.c 1 0x06001000\n",
                owners,
                (oracle,),
            )

    def test_cfg_direct_call_to_owner_offset_seeds_exact_entry(self) -> None:
        """A proven BSR target inside an owner is an executable CFG root."""
        owners = (
            FunctionOwner("_route_root", 0x06001000, 0x06001008, 1),
            FunctionOwner("_offset_target", 0x06002000, 0x06002030, 1),
            FunctionOwner("_route_child", 0x06003000, 0x06003008, 1),
            FunctionOwner("___addsf3", 0x06009000, 0x06009008, 1),
        )
        oracle = bounded_verifier.RouteOracle(
            1,
            frozenset({"_route_root"}),
            frozenset(),
            frozenset(),
        )
        disassembly = """
06001000 <_route_root>:
 6001000: b0 02 bsr 6002020 <_offset_target+0x20>
 6001002: 00 09 nop
 6001004: 00 0b rts
 6001006: 00 09 nop
06002000 <_offset_target>:
 6002000: 00 0b rts
 6002002: 00 09 nop
 6002020: b0 02 bsr 6003000 <_route_child>
 6002022: 00 09 nop
 6002024: 00 0b rts
 6002026: 00 09 nop
06003000 <_route_child>:
 6003000: b0 02 bsr 6009000 <___addsf3>
 6003002: 00 09 nop
 6003004: 00 0b rts
 6003006: 00 09 nop
06009000 <___addsf3>:
 6009000: 00 0b rts
 6009002: 00 09 nop
"""
        prepared = bounded_verifier.prepare_route_bounded_code_only(
            disassembly,
            "fixture.c 1 0x06001000\n",
            owners,
            (oracle,),
        )
        target_id = self._owner_identity(owners[1])
        child_id = self._owner_identity(owners[2])
        self.assertIn(child_id, prepared.graph[target_id])
        self.assertIn(child_id, prepared.closure)
        analysis = analyze_code_only(
            prepared.instructions,
            owners,
            prepared.decoded_lines,
            selected_owner_identities=set(prepared.selected_identities),
        )
        self.assertTrue(any(
            call.caller == "_route_child" and call.helper == "___addsf3"
            for call in analysis.calls
        ))

    def test_cfg_stack_spill_is_invalid_after_r15_postincrement(self) -> None:
        """A fixed-frame target cannot survive an SH auto-update of r15."""
        owners = (
            FunctionOwner("_route_root", 0x06001000, 0x06001030, 1),
            FunctionOwner("_route_child", 0x06002000, 0x06002008, 1),
            FunctionOwner("_declared_callback", 0x06003000, 0x06003008, 1),
            FunctionOwner("___addsf3", 0x06009000, 0x06009008, 1),
        )
        edge = ("_route_root", "_declared_callback")
        oracle = bounded_verifier.RouteOracle(
            1,
            frozenset({"_route_root"}),
            frozenset({edge}),
            frozenset({edge}),
        )
        disassembly = """
06001000 <_route_root>:
 6001000: 89 06 bt 6001010 <_route_root+0x10>
 6001002: d1 04 mov.l 6001018 <_route_root+0x18>,r1 ! 06003000 <_declared_callback>
 6001004: 1f 12 mov.l r1,@(0,r15)
 6001006: 62 f6 mov.l @r15+,r2
 6001008: 61 f2 mov.l @(0,r15),r1
 600100a: 41 2b jmp @r1
 600100c: 00 09 nop
 6001010: 00 0b rts
 6001012: 00 09 nop
 6001018: 06 00 .word 0x0600
 600101a: 30 00 .word 0x3000
 6001020: b0 02 bsr 6002000 <_route_child>
 6001022: 00 09 nop
 6001024: 00 0b rts
 6001026: 00 09 nop
06002000 <_route_child>:
 6002000: b0 02 bsr 6009000 <___addsf3>
 6002002: 00 09 nop
 6002004: 00 0b rts
 6002006: 00 09 nop
06003000 <_declared_callback>:
 6003000: 00 0b rts
 6003002: 00 09 nop
06009000 <___addsf3>:
 6009000: 00 0b rts
 6009002: 00 09 nop
"""
        with self.assertRaisesRegex(ValueError, "no decoded code provenance"):
            bounded_verifier.prepare_route_bounded_code_only(
                disassembly,
                "fixture.c 1 0x06001000\n",
                owners,
                (oracle,),
            )

    def test_cfg_stack_spill_is_invalid_when_r15_is_loaded(self) -> None:
        """Any load into r15 changes the base of fixed-frame spill facts."""
        owners = (
            FunctionOwner("_route_root", 0x06001000, 0x06001030, 1),
            FunctionOwner("_route_child", 0x06002000, 0x06002008, 1),
            FunctionOwner("_declared_callback", 0x06003000, 0x06003008, 1),
            FunctionOwner("___addsf3", 0x06009000, 0x06009008, 1),
        )
        edge = ("_route_root", "_declared_callback")
        oracle = bounded_verifier.RouteOracle(
            1,
            frozenset({"_route_root"}),
            frozenset({edge}),
            frozenset({edge}),
        )
        cases = (
            (
                "fixed_stack_load",
                " 6001006: 6f f2 mov.l @(0,r15),r15\n",
                "",
            ),
            (
                "literal_load",
                " 6001006: df 05 mov.l 600101c <_route_root+0x1c>,r15 ! "
                "06003000 <_declared_callback>\n",
                " 600101c: 06 00 .word 0x0600\n"
                " 600101e: 30 00 .word 0x3000\n",
            ),
        )
        for label, mutation, extra_pool in cases:
            with self.subTest(label=label):
                disassembly = """
06001000 <_route_root>:
 6001000: 89 06 bt 6001010 <_route_root+0x10>
 6001002: d1 04 mov.l 6001018 <_route_root+0x18>,r1 ! 06003000 <_declared_callback>
 6001004: 1f 12 mov.l r1,@(0,r15)
""" + mutation + """ 6001008: 61 f2 mov.l @(0,r15),r1
 600100a: 41 2b jmp @r1
 600100c: 00 09 nop
 6001010: 00 0b rts
 6001012: 00 09 nop
 6001018: 06 00 .word 0x0600
 600101a: 30 00 .word 0x3000
""" + extra_pool + """ 6001020: b0 02 bsr 6002000 <_route_child>
 6001022: 00 09 nop
 6001024: 00 0b rts
 6001026: 00 09 nop
06002000 <_route_child>:
 6002000: b0 02 bsr 6009000 <___addsf3>
 6002002: 00 09 nop
 6002004: 00 0b rts
 6002006: 00 09 nop
06003000 <_declared_callback>:
 6003000: 00 0b rts
 6003002: 00 09 nop
06009000 <___addsf3>:
 6009000: 00 0b rts
 6009002: 00 09 nop
"""
                with self.assertRaisesRegex(
                    ValueError, "no decoded code provenance"
                ):
                    bounded_verifier.prepare_route_bounded_code_only(
                        disassembly,
                        "fixture.c 1 0x06001000\n",
                        owners,
                        (oracle,),
                    )

    def test_cfg_literal_target_is_invalidated_by_register_mutation(self) -> None:
        """Auto-update and one-operand writers kill a literal register fact."""
        owners = (
            FunctionOwner("_route_root", 0x06001000, 0x06001030, 1),
            FunctionOwner("_route_child", 0x06002000, 0x06002008, 1),
            FunctionOwner("_declared_callback", 0x06003000, 0x06003008, 1),
            FunctionOwner("___addsf3", 0x06009000, 0x06009008, 1),
        )
        edge = ("_route_root", "_declared_callback")
        oracle = bounded_verifier.RouteOracle(
            1,
            frozenset({"_route_root"}),
            frozenset({edge}),
            frozenset({edge}),
        )
        for label, mutation in (
            ("postincrement", " 6001004: 62 86 mov.l @r8+,r2\n"),
            ("single_writer", " 6001004: 48 00 shll r8\n"),
        ):
            with self.subTest(label=label):
                disassembly = """
06001000 <_route_root>:
 6001000: 89 06 bt 6001010 <_route_root+0x10>
 6001002: d8 04 mov.l 6001018 <_route_root+0x18>,r8 ! 06003000 <_declared_callback>
""" + mutation + """ 6001006: 48 2b jmp @r8
 6001008: 00 09 nop
 6001010: 00 0b rts
 6001012: 00 09 nop
 6001018: 06 00 .word 0x0600
 600101a: 30 00 .word 0x3000
 6001020: b0 02 bsr 6002000 <_route_child>
 6001022: 00 09 nop
 6001024: 00 0b rts
 6001026: 00 09 nop
06002000 <_route_child>:
 6002000: b0 02 bsr 6009000 <___addsf3>
 6002002: 00 09 nop
 6002004: 00 0b rts
 6002006: 00 09 nop
06003000 <_declared_callback>:
 6003000: 00 0b rts
 6003002: 00 09 nop
06009000 <___addsf3>:
 6009000: 00 0b rts
 6009002: 00 09 nop
"""
                with self.assertRaisesRegex(
                    ValueError, "no decoded code provenance"
                ):
                    bounded_verifier.prepare_route_bounded_code_only(
                        disassembly,
                        "fixture.c 1 0x06001000\n",
                        owners,
                        (oracle,),
                    )

    def test_cfg_reaches_plus_1e_call_and_skips_branched_over_literal_pool(self) -> None:
        owners = (
            FunctionOwner(
                "_demo_prepare_position_owners", 0x06001000, 0x06001040, 1
            ),
            FunctionOwner("_memset", 0x06002000, 0x06002008, 1),
            FunctionOwner("_owned_decoy", 0x06003000, 0x06003008, 1),
        )
        oracle = bounded_verifier.RouteOracle(
            1,
            frozenset({"_demo_prepare_position_owners"}),
            frozenset(),
            frozenset(),
        )
        disassembly = """
06001000 <_demo_prepare_position_owners>:
 6001000: a0 06 bra 6001010 <_demo_prepare_position_owners+0x10>
 6001002: 00 09 nop
 6001004: b0 02 bsr 6009000 <_gDialogTextAlpha>
 6001006: 00 09 nop
 6001008: d1 02 mov.l 6001014 <_demo_prepare_position_owners+0x14>,r1 ! 06009000 <_gDialogTextAlpha>
 600100a: 41 0b jsr @r1
 600100c: 00 09 nop
 6001010: 00 09 nop
 6001012: 00 09 nop
 6001014: 00 09 nop
 6001016: 00 09 nop
 6001018: 00 09 nop
 600101a: d1 04 mov.l 600102c <_demo_prepare_position_owners+0x2c>,r1 ! 06002000 <_memset>
 600101c: 00 09 nop
 600101e: 41 0b jsr @r1
 6001020: 00 09 nop
 6001022: 00 0b rts
 6001024: 00 09 nop
06002000 <_memset>:
 6002000: 00 0b rts
 6002002: 00 09 nop
06003000 <_owned_decoy>:
 6003000: 00 0b rts
 6003002: 00 09 nop
"""
        prepared = bounded_verifier.prepare_route_bounded_code_only(
            disassembly,
            (
                "fixture.c 1 0x06001000\n"
                "fixture.c - 0x06001004 end_sequence\n"
                "fixture.c 2 0x06002000\n"
            ),
            owners,
            (oracle,),
        )
        root_id = self._owner_identity(owners[0])
        memset_id = self._owner_identity(owners[1])
        self.assertEqual(
            prepared.graph[root_id], {memset_id}
        )
        self.assertEqual(
            prepared.closure,
            frozenset({root_id, memset_id}),
        )
        literal_pool_decoy = bounded_verifier.prepare_route_bounded_code_only(
            disassembly.replace(
                "6009000 <_gDialogTextAlpha>",
                "6003000 <_owned_decoy>",
                1,
            ),
            "fixture.c 1 0x06001000\n",
            owners,
            (oracle,),
        )
        self.assertEqual(literal_pool_decoy.graph[root_id], {memset_id})
        self.assertEqual(
            literal_pool_decoy.closure,
            frozenset({root_id, memset_id}),
        )

    def test_cfg_provenance_follows_both_conditional_arms(self) -> None:
        owners = (
            FunctionOwner("_route_root", 0x06001000, 0x06001020, 1),
            FunctionOwner("_route_left", 0x06002000, 0x06002008, 1),
            FunctionOwner("_route_right", 0x06003000, 0x06003008, 1),
        )
        disassembly = """
06001000 <_route_root>:
 6001000: 89 03 bt 600100a <_route_root+0xa>
 6001002: b0 7d bsr 6002000 <_route_left>
 6001004: 00 09 nop
 6001006: a0 04 bra 6001012 <_route_root+0x12>
 6001008: 00 09 nop
 600100a: b0 79 bsr 6003000 <_route_right>
 600100c: 00 09 nop
 600100e: 00 0b rts
 6001010: 00 09 nop
 6001012: 00 0b rts
 6001014: 00 09 nop
06002000 <_route_left>:
 6002000: 00 0b rts
 6002002: 00 09 nop
06003000 <_route_right>:
 6003000: 00 0b rts
 6003002: 00 09 nop
"""
        prepared = bounded_verifier.prepare_route_bounded_code_only(
            disassembly,
            "fixture.c 1 0x06001000\n",
            owners,
            (self.ORACLE,),
        )
        identities = [self._owner_identity(owner) for owner in owners]
        self.assertEqual(
            prepared.graph[identities[0]], {identities[1], identities[2]}
        )
        self.assertEqual(
            prepared.closure,
            frozenset(identities),
        )

    def test_executable_targetless_bsr_fails_closed(self) -> None:
        disassembly = """
06001000 <_route_root>:
 6001000: b0 02 bsr 6008000
 6001002: 00 09 nop
 6001004: 00 0b rts
 6001006: 00 09 nop
"""
        with self.assertRaisesRegex(ValueError, "unresolved direct call target"):
            bounded_verifier.prepare_route_bounded_code_only(
                disassembly,
                "fixture.c 1 0x06001000\n",
                self.OWNERS[:1],
                (self.ORACLE,),
            )

    def test_cfg_provenance_rejects_reachable_unknown_bra_target(self) -> None:
        disassembly = """
06001000 <_route_root>:
 6001000: a0 02 bra <unknown>
 6001002: 00 09 nop
"""
        with self.assertRaisesRegex(ValueError, "unknown bra target"):
            bounded_verifier.prepare_route_bounded_code_only(
                disassembly, "", self.OWNERS[:1], (self.ORACLE,)
            )

    def test_cfg_provenance_rejects_bra_into_owner_or_island(self) -> None:
        root = FunctionOwner("_route_root", 0x06001000, 0x06001004, 1)
        target_owner = FunctionOwner(
            "_route_tail", 0x06001004, 0x0600100C, 1
        )
        target_island = bounded_verifier.LocalIsland(
            "route_tail", 0x06001004, 0x06001004, 0x0600100C, 1
        )
        disassembly = """
06001000 <_route_root>:
 6001000: a0 00 bra 6001004 <route_tail>
 6001002: 00 09 nop
06001004 <route_tail>:
 6001004: b0 02 bsr 6009000 <___addsf3>
 6001006: 00 09 nop
 6001008: 00 0b rts
 600100a: 00 09 nop
"""
        cases = {
            "owner": ((root, target_owner), ()),
            "island": ((root,), (target_island,)),
        }
        for label, (owners, islands) in cases.items():
            with self.subTest(target_region=label):
                with self.assertRaisesRegex(
                    ValueError, "bra successor leaves bounded block"
                ):
                    bounded_verifier.prepare_route_bounded_code_only(
                        disassembly,
                        "",
                        owners,
                        (self.ORACLE,),
                        local_islands=islands,
                    )

    def test_cfg_provenance_rejects_fallthrough_into_owner_or_island(self) -> None:
        root = FunctionOwner("_route_root", 0x06001000, 0x06001002, 1)
        target_owner = FunctionOwner(
            "_route_tail", 0x06001002, 0x0600100A, 1
        )
        target_island = bounded_verifier.LocalIsland(
            "route_tail", 0x06001002, 0x06001002, 0x0600100A, 1
        )
        disassembly = """
06001000 <_route_root>:
 6001000: 00 09 nop
06001002 <route_tail>:
 6001002: b0 02 bsr 6009000 <___addsf3>
 6001004: 00 09 nop
 6001006: 00 0b rts
 6001008: 00 09 nop
"""
        cases = {
            "owner": ((root, target_owner), ()),
            "island": ((root,), (target_island,)),
        }
        for label, (owners, islands) in cases.items():
            with self.subTest(target_region=label):
                with self.assertRaisesRegex(
                    ValueError, "fallthrough successor leaves bounded block"
                ):
                    bounded_verifier.prepare_route_bounded_code_only(
                        disassembly,
                        "",
                        owners,
                        (self.ORACLE,),
                        local_islands=islands,
                    )

    def test_cfg_component_keeps_nested_shift_ladder_fallthrough(self) -> None:
        owners = (
            FunctionOwner("___ashrsi3_r4_10", 0x06001000, 0x0600100A, 1),
            FunctionOwner("___ashrsi3_r4_9", 0x06001002, 0x0600100A, 1),
            FunctionOwner("___addsf3", 0x06002000, 0x06002004, 1),
        )
        oracle = bounded_verifier.RouteOracle(
            1, frozenset({"___ashrsi3_r4_10"}), frozenset(), frozenset()
        )
        disassembly = """
06001000 <___ashrsi3_r4_10>:
 6001000: 44 01 shlr r4
06001002 <___ashrsi3_r4_9>:
 6001002: b0 7d bsr 6002000 <___addsf3>
 6001004: 00 09 nop
 6001006: 00 0b rts
 6001008: 00 09 nop
06002000 <___addsf3>:
 6002000: 00 0b rts
 6002002: 00 09 nop
"""
        prepared = bounded_verifier.prepare_route_bounded_code_only(
            disassembly, "", owners, (oracle,)
        )
        component_identities = frozenset(
            self._owner_identity(owner) for owner in owners[:2]
        )
        self.assertEqual(
            prepared.closure,
            component_identities,
        )
        self.assertEqual(prepared.selected_identities, component_identities)
        self.assertEqual(
            prepared.selected_names,
            frozenset({"___ashrsi3_r4_10", "___ashrsi3_r4_9"}),
        )
        self.assertEqual(
            set(prepared.instructions),
            {0x06001000, 0x06001002, 0x06001004, 0x06001006, 0x06001008},
        )
        analysis = analyze_code_only(
            prepared.instructions,
            owners,
            prepared.decoded_lines,
            instruction_memory=build_instruction_memory(prepared.instructions),
            selected_owner_identities=set(prepared.selected_identities),
        )
        self.assertEqual(
            [(call.caller, call.address, call.helper) for call in analysis.calls],
            [("___ashrsi3_r4_9", 0x06001002, "___addsf3")],
        )

    def test_cfg_component_rejects_partial_owner_overlap(self) -> None:
        owners = (
            FunctionOwner("_route_root", 0x06001000, 0x06001008, 1),
            FunctionOwner("_crossing_tail", 0x06001004, 0x0600100C, 1),
        )
        disassembly = """
06001000 <_route_root>:
 6001000: 00 0b rts
 6001002: 00 09 nop
06001004 <_crossing_tail>:
 6001004: 00 0b rts
 6001006: 00 09 nop
 6001008: 00 0b rts
 600100a: 00 09 nop
"""
        with self.assertRaisesRegex(ValueError, "partial function overlap"):
            bounded_verifier.prepare_route_bounded_code_only(
                disassembly, "", owners, (self.ORACLE,)
            )

    def test_cfg_provenance_requires_local_delay_slot(self) -> None:
        cases = {
            "missing": (
                (FunctionOwner("_route_root", 0x06001000, 0x06001004, 1),),
                """
06001000 <_route_root>:
 6001000: 00 0b rts
""",
            ),
            "cross-owner": (
                (
                    FunctionOwner("_route_root", 0x06001000, 0x06001002, 1),
                    FunctionOwner("_route_tail", 0x06001002, 0x06001004, 1),
                ),
                """
06001000 <_route_root>:
 6001000: 00 0b rts
06001002 <_route_tail>:
 6001002: 00 09 nop
""",
            ),
        }
        for label, (owners, disassembly) in cases.items():
            with self.subTest(delay_slot=label):
                with self.assertRaisesRegex(
                    ValueError, "delay slot leaves bounded block"
                ):
                    bounded_verifier.prepare_route_bounded_code_only(
                        disassembly, "", owners, (self.ORACLE,)
                    )

    def test_executable_direct_call_to_unowned_target_fails_closed(self) -> None:
        cases = {
            "bsr": (
                " 6001000: b0 02 bsr 6008000 <_orphan>\n"
                " 6001002: 00 09 nop\n"
            ),
            "literal-jsr": (
                " 6001000: d1 02 mov.l 600100c <_route_root+0xc>,r1 "
                "! 06008000 <_orphan>\n"
                " 6001002: 41 0b jsr @r1\n"
            ),
            "mismatched-bsr-symbol": (
                " 6001000: b0 02 bsr 6008000 <_route_child>\n"
                " 6001002: 00 09 nop\n"
            ),
        }
        for label, call in cases.items():
            with self.subTest(label=label):
                disassembly = (
                    "06001000 <_route_root>:\n"
                    + call
                    + " 6001004: 00 0b rts\n"
                    + " 6001006: 00 09 nop\n"
                )
                with self.assertRaisesRegex(ValueError, "no linked owner"):
                    bounded_verifier.prepare_route_bounded_code_only(
                        disassembly,
                        "fixture.c 1 0x06001000\nfixture.c 2 0x06001002\n",
                        self.OWNERS[:2],
                        (self.ORACLE,),
                    )

    def test_bounded_closure_traverses_validated_div0_island(self) -> None:
        owners = (
            FunctionOwner("___udivsi3", 0x06001120, 0x06001140, 1),
            FunctionOwner("_route_child", 0x06001200, 0x06001208, 1),
            FunctionOwner("___addsf3", 0x06001300, 0x06001308, 1),
        )
        island = bounded_verifier.LocalIsland(
            "div0", 0x06001100, 0x06001104, 0x06001120, 1
        )
        oracle = bounded_verifier.RouteOracle(
            1, frozenset({"___udivsi3"}), frozenset(), frozenset()
        )
        disassembly = """
06001100 <div0>:
 6001106: b0 7b bsr 6001200 <_route_child>
 6001108: 00 09 nop
 600110a: 00 0b rts
 600110c: 00 09 nop
 6001110: d8 03 mov.l 6001120 <div0+0x20>,r8 ! 06001300 <___addsf3>
 6001112: 48 0b jsr @r8
 6001114: 00 09 nop
 6001116: 00 0b rts
 6001118: 00 09 nop
06001120 <___udivsi3>:
 6001120: bf f1 bsr 6001106 <div0+0x6>
 6001122: 00 09 nop
 6001124: 00 0b rts
 6001126: 00 09 nop
06001200 <_route_child>:
 6001200: 00 0b rts
 6001202: 00 09 nop
06001300 <___addsf3>:
 6001300: 00 0b rts
 6001302: 00 09 nop
"""
        island_id = "@island:div0@06001100"
        udiv_id = self._owner_identity(owners[0])
        child_id = self._owner_identity(owners[1])
        prepared = bounded_verifier.prepare_route_bounded_code_only(
            disassembly,
            (
                "fixture.s 1 0x06001106\n"
                "fixture.s 2 0x06001110\n"
                "fixture.s 3 0x06001120\n"
                "fixture.s 4 0x06001122\n"
            ),
            owners,
            (oracle,),
            local_islands=(island,),
        )
        self.assertEqual(prepared.closure, frozenset({
            udiv_id, island_id, child_id,
        }))
        self.assertEqual(
            prepared.selected_identities, frozenset({udiv_id, child_id})
        )
        self.assertEqual(prepared.selected_names, frozenset({
            "___udivsi3", "_route_child",
        }))
        self.assertEqual(prepared.selected_islands, (island,))
        self.assertEqual(
            prepared.island_origins,
            {island_id: frozenset({udiv_id})},
        )
        self.assertTrue({0x06001106, 0x06001120, 0x06001200} <= set(
            prepared.instructions
        ))
        self.assertEqual(
            prepared.decoded_lines[island_id], {0x06001106, 0x06001110}
        )
        with self.assertRaisesRegex(ValueError, "island has no owner origin"):
            analyze_code_only(
                prepared.instructions,
                owners,
                prepared.decoded_lines,
                instruction_memory=build_instruction_memory(
                    prepared.instructions
                ),
                local_islands=prepared.selected_islands,
                selected_owner_identities=set(prepared.selected_identities),
            )

        analysis = analyze_code_only(
            prepared.instructions,
            owners,
            prepared.decoded_lines,
            instruction_memory=build_instruction_memory(prepared.instructions),
            local_islands=prepared.selected_islands,
            island_origins=prepared.island_origins,
            selected_owner_identities=set(prepared.selected_identities),
        )
        self.assertEqual(
            [
                (
                    fact.caller, fact.caller_region, fact.caller_island,
                    fact.caller_offset, fact.callee,
                )
                for fact in analysis.direct_calls
                if fact.caller_region == "island"
            ],
            [
                ("___udivsi3", "island", "div0", 0x6, "_route_child"),
                ("___udivsi3", "island", "div0", 0x12, "___addsf3"),
            ],
        )
        self.assertEqual(
            [
                (call.caller, call.address, call.helper)
                for call in analysis.calls
                if call.helper == "___addsf3"
            ],
            [("___udivsi3", 0x06001112, "___addsf3")],
        )

    def test_bounded_closure_rejects_unvalidated_local_code_target(self) -> None:
        owner = FunctionOwner("___udivsi3", 0x06001120, 0x06001140, 1)
        oracle = bounded_verifier.RouteOracle(
            1, frozenset({"___udivsi3"}), frozenset(), frozenset()
        )
        disassembly = """
06001100 <div0>:
 6001106: 00 0b rts
 6001108: 00 09 nop
06001120 <___udivsi3>:
 6001120: bf f1 bsr 6001106 <div0+0x6>
 6001122: 00 09 nop
 6001124: 00 0b rts
 6001126: 00 09 nop
"""
        with self.assertRaisesRegex(ValueError, "no linked owner"):
            bounded_verifier.prepare_route_bounded_code_only(
                disassembly,
                "fixture.s 1 0x06001120\n",
                (owner,),
                (oracle,),
            )

    def test_bounded_analysis_keeps_unresolved_indirect_transfer_fail_closed(self) -> None:
        disassembly = """
06001000 <_route_root>:
 6001000: 41 2b jmp @r1
 6001002: 00 09 nop
 6001004: 00 0b rts
 6001006: 00 09 nop
"""
        prepared = bounded_verifier.prepare_route_bounded_code_only(
            disassembly, "", self.OWNERS[:1], (self.ORACLE,)
        )
        analysis = self._analyze(prepared)
        audited = audit_indirect_edges(
            {}, self.ORACLE, self.OWNERS[:1],
            analysis.unresolved_transfers,
        )
        self.assertEqual(
            [(item.caller, item.address) for item in audited.unlisted_transfers],
            [("_route_root", 0x06001000)],
        )

    def test_cli_bounded_mode_is_opt_in_and_default_still_parses_full_elf(self) -> None:
        sections = """
  [ 1] .text PROGBITS 06001000 001000 006000 00 AX 0 0 2
"""
        symbols = """
   1: 06001000 16 FUNC GLOBAL DEFAULT 1 _route_root
   2: 06002000 8 FUNC GLOBAL DEFAULT 1 _route_child
   3: 06003000 16384 FUNC GLOBAL DEFAULT 1 _irrelevant_large
"""
        disassembly = self._route_disassembly()
        disassembly = disassembly.replace(
            " 6003000: 00 09 nop\n",
            " 6003000: 00 09 bt/s 6003004 <_irrelevant_large+0x4>\n",
            1,
        )

        def fake_command(command):
            if command[1] == "-d":
                return disassembly
            if command[1] == "-SW":
                return sections
            if command[1] == "-sW":
                return symbols
            if command[1] == "--debug-dump=decodedline":
                return "fixture.c 1 0x06001002\n"
            raise AssertionError(command)

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            elf = root / "fixture.elf"
            baseline = root / "baseline.txt"
            oracle = root / "oracle.txt"
            elf.write_bytes(b"ELF")
            baseline.write_text(
                "BASELINE_VERSION 1\nHOT_CEILING 0\n", encoding="utf-8"
            )
            oracle.write_text(
                "ROUTE_ORACLE_VERSION 1\nROOT _route_root\n", encoding="utf-8"
            )
            common = [
                str(elf), str(baseline), "--route-oracle", str(oracle),
                "--objdump", "objdump", "--readelf", "readelf",
                "--addr2line", "addr2line",
            ]
            with patch.object(bounded_verifier, "run_command", side_effect=fake_command), \
                    patch.object(bounded_verifier, "verify_baseline_integrity"), \
                    patch.object(bounded_verifier, "verify_route_oracle_integrity"), \
                    patch.object(bounded_verifier, "source_locations", return_value={}):
                self.assertEqual(bounded_verifier.main([
                    *common, "--analysis-mode", "code-only-route-bounded",
                ]), 0)
                self.assertEqual(bounded_verifier.main(common), 2)

    def test_cli_bounded_duplicate_local_identity_excludes_sibling_helper(self) -> None:
        sections = """
  [ 1] .text PROGBITS 06001000 001000 008004 00 AX 0 0 2
"""
        symbols = """
   1: 06001000 8 FUNC GLOBAL DEFAULT 1 _route_root
   2: 06002000 8 FUNC LOCAL DEFAULT 1 local_helper
   3: 06003000 4 FUNC LOCAL DEFAULT 1 local_helper
   4: 06009000 4 FUNC GLOBAL DEFAULT 1 ___addsf3
"""
        disassembly = """
06001000 <_route_root>:
 6001000: b0 02 bsr 6003000 <local_helper>
 6001002: 00 09 nop
 6001004: 00 0b rts
 6001006: 00 09 nop
06002000 <local_helper>:
 6002000: b0 02 bsr 6009000 <___addsf3>
 6002002: 00 09 nop
 6002004: 00 0b rts
 6002006: 00 09 nop
06003000 <local_helper>:
 6003000: 00 0b rts
 6003002: 00 09 nop
06009000 <___addsf3>:
 6009000: 00 0b rts
 6009002: 00 09 nop
"""

        def fake_command(command):
            if command[1] == "-d":
                return disassembly
            if command[1] == "-SW":
                return sections
            if command[1] == "-sW":
                return symbols
            if command[1] == "--debug-dump=decodedline":
                return (
                    "fixture.c 1 0x06002002\n"
                    "fixture.c 2 0x06003002\n"
                )
            raise AssertionError(command)

        observed = {}
        real_analyze = bounded_verifier.analyze_code_only

        def observe_analysis(*args, **kwargs):
            result = real_analyze(*args, **kwargs)
            observed["selected"] = kwargs.get("selected_owner_identities")
            observed["result"] = result
            return result

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            elf = root / "fixture.elf"
            baseline = root / "baseline.txt"
            oracle = root / "oracle.txt"
            elf.write_bytes(b"ELF")
            baseline.write_text(
                "BASELINE_VERSION 1\nHOT_CEILING 0\n", encoding="utf-8"
            )
            oracle.write_text(
                "ROUTE_ORACLE_VERSION 1\nROOT _route_root\n", encoding="utf-8"
            )
            with patch.object(
                bounded_verifier, "run_command", side_effect=fake_command
            ), patch.object(
                bounded_verifier, "verify_baseline_integrity"
            ), patch.object(
                bounded_verifier, "verify_route_oracle_integrity"
            ), patch.object(
                bounded_verifier, "source_locations", return_value={}
            ), patch.object(
                bounded_verifier,
                "analyze_code_only",
                side_effect=observe_analysis,
            ):
                self.assertEqual(bounded_verifier.main([
                    str(elf),
                    str(baseline),
                    "--route-oracle", str(oracle),
                    "--objdump", "objdump",
                    "--readelf", "readelf",
                    "--addr2line", "addr2line",
                    "--analysis-mode", "code-only-route-bounded",
                ]), 0)

        self.assertEqual(observed["selected"], {
            "@owner:1:06001000:_route_root",
            "@owner:1:06003000:local_helper",
        })
        self.assertNotIn(0x06002000, observed["result"].code_addresses)
        self.assertFalse(any(
            call.helper == "___addsf3" for call in observed["result"].calls
        ))

    def test_cli_bounded_duplicate_locals_keep_route_and_audit_accounting_separate(
        self,
    ) -> None:
        sections = """
  [ 1] .text PROGBITS 06001000 001000 008014 00 AX 0 0 2
"""
        symbols = """
   1: 06001000 8 FUNC GLOBAL DEFAULT 1 _route_root
   2: 06001100 8 FUNC GLOBAL DEFAULT 1 _audit_root
   3: 06002000 8 FUNC LOCAL DEFAULT 1 local_helper
   4: 06003000 8 FUNC LOCAL DEFAULT 1 local_helper
   5: 06009000 4 FUNC GLOBAL DEFAULT 1 ___addsf3
   6: 06009010 4 FUNC GLOBAL DEFAULT 1 ___mulsf3
"""
        disassembly = """
06001000 <_route_root>:
 6001000: b0 02 bsr 6002000 <local_helper>
 6001002: 00 09 nop
 6001004: 00 0b rts
 6001006: 00 09 nop
06001100 <_audit_root>:
 6001100: b0 02 bsr 6003000 <local_helper>
 6001102: 00 09 nop
 6001104: 00 0b rts
 6001106: 00 09 nop
06002000 <local_helper>:
 6002000: b0 02 bsr 6009000 <___addsf3>
 6002002: 00 09 nop
 6002004: 00 0b rts
 6002006: 00 09 nop
06003000 <local_helper>:
 6003000: b0 02 bsr 6009010 <___mulsf3>
 6003002: 00 09 nop
 6003004: 00 0b rts
 6003006: 00 09 nop
06009000 <___addsf3>:
 6009000: 00 0b rts
 6009002: 00 09 nop
06009010 <___mulsf3>:
 6009010: 00 0b rts
 6009012: 00 09 nop
"""

        def fake_command(command):
            if command[1] == "-d":
                return disassembly
            if command[1] == "-SW":
                return sections
            if command[1] == "-sW":
                return symbols
            if command[1] == "--debug-dump=decodedline":
                return ""
            raise AssertionError(command)

        observed_audits = []
        real_audit = bounded_verifier.audit_indirect_edges

        def observe_audit(graph, oracle, owners, unresolved_transfers):
            result = real_audit(graph, oracle, owners, unresolved_transfers)
            observed_audits.append((
                {caller: set(callees) for caller, callees in graph.items()},
                oracle,
                result,
            ))
            return result

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            elf = root / "fixture.elf"
            baseline = root / "baseline.txt"
            route_oracle = root / "route-oracle.txt"
            audit_oracle = root / "audit-oracle.txt"
            audit_contract = root / "audit-contract.txt"
            elf.write_bytes(b"ELF")
            baseline.write_text(
                "BASELINE_VERSION 1\n"
                "HOT_CEILING 1\n"
                "HOT local_helper ___addsf3 1\n",
                encoding="utf-8",
            )
            route_oracle.write_text(
                "ROUTE_ORACLE_VERSION 1\nROOT _route_root\n",
                encoding="utf-8",
            )
            audit_oracle.write_text(
                "ROUTE_ORACLE_VERSION 1\nROOT _audit_root\n",
                encoding="utf-8",
            )
            audit_contract.write_text(
                "AUDIT_CONTRACT_VERSION 2\n"
                "EXPECTED_ROOT _audit_root\n"
                "EXPECTED_TOTAL 1\n"
                "FORBIDDEN_CALLER _forbidden\n",
                encoding="utf-8",
            )
            with patch.object(
                bounded_verifier, "run_command", side_effect=fake_command
            ), patch.object(
                bounded_verifier, "verify_baseline_integrity"
            ), patch.object(
                bounded_verifier, "verify_route_oracle_integrity"
            ), patch.object(
                bounded_verifier, "verify_audit_contract_integrity"
            ), patch.object(
                bounded_verifier, "source_locations", return_value={}
            ), patch.object(
                bounded_verifier,
                "audit_indirect_edges",
                side_effect=observe_audit,
            ):
                self.assertEqual(bounded_verifier.main([
                    str(elf),
                    str(baseline),
                    "--route-oracle", str(route_oracle),
                    "--audit-route-oracle", str(audit_oracle),
                    "--audit-contract", str(audit_contract),
                    "--objdump", "objdump",
                    "--readelf", "readelf",
                    "--addr2line", "addr2line",
                    "--analysis-mode", "code-only-route-bounded",
                ]), 0)

        route_root_id = "@owner:1:06001000:_route_root"
        audit_root_id = "@owner:1:06001100:_audit_root"
        first_local_id = "@owner:1:06002000:local_helper"
        second_local_id = "@owner:1:06003000:local_helper"
        self.assertEqual(len(observed_audits), 2)
        graph, primary_oracle, primary_result = observed_audits[0]
        self.assertEqual(graph, {
            route_root_id: {first_local_id},
            audit_root_id: {second_local_id},
        })
        self.assertEqual(primary_oracle.roots, frozenset({route_root_id}))
        self.assertEqual(
            primary_result.closure,
            frozenset({route_root_id, first_local_id}),
        )
        _, secondary_oracle, secondary_result = observed_audits[1]
        self.assertEqual(secondary_oracle.roots, frozenset({audit_root_id}))
        self.assertEqual(
            secondary_result.closure,
            frozenset({audit_root_id, second_local_id}),
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
