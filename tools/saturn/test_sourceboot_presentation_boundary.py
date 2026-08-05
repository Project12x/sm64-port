#!/usr/bin/env python3
"""Mutation checks for sourceboot's one-VBlank presentation boundary.

The break protected here is a scheduler change that either refills fixed-step
credit while ticks are running or submits another VDP1/VDP2 presentation for
the same observed VBlank generation.  The checks operate on the real
sourceboot translation unit and mutate it in-memory so each forbidden shape
is demonstrated without building target code.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SOURCEBOOT_C = REPO_ROOT / "src" / "port" / "saturn" / "sourceboot" / "main.c"


def extract_c_function(text: str, name: str) -> str:
    match = re.search(rf"\b{name}\s*\([^)]*\)\s*\{{", text)
    if match is None:
        raise AssertionError(f"function {name} not found")
    depth = 0
    for index in range(match.end() - 1, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[match.end() : index]
    raise AssertionError(f"function {name} is not terminated")


def presentation_function(text: str) -> str:
    return extract_c_function(text, "sourceboot_present_generation")


BOOTSTRAP_VDP2_BEGIN = """sm64_saturn_vdp2_frame_begin(&sourceboot_vdp2_frame, NULL,
                                 &sourceboot_fast3d.profile,
                                 sourceboot_sim_tick_count);"""
BOOTSTRAP_VDP2_COMMIT = """sm64_saturn_vdp2_frame_commit(&sourceboot_vdp2_frame,
                                  &sourceboot_vdp2_backend);"""


def bootstrap_vdp2_retirement(text: str) -> tuple[str, str]:
    main = extract_c_function(text, "main")
    begin_count = main.count(BOOTSTRAP_VDP2_BEGIN)
    if begin_count != 1:
        raise AssertionError("bootstrap must contain exactly one null-snapshot VDP2 begin")
    commit_count = main.count(BOOTSTRAP_VDP2_COMMIT)
    if commit_count != 1:
        raise AssertionError("bootstrap must contain exactly one VDP2 commit")
    wait_count = main.count("vdp2_sync_wait();")
    if wait_count != 1:
        raise AssertionError("bootstrap must contain exactly one VDP2 retirement wait")

    begin = main.index(BOOTSTRAP_VDP2_BEGIN)
    commit = main.index(BOOTSTRAP_VDP2_COMMIT, begin)
    wait = main.index("vdp2_sync_wait();", commit)
    frontend_init = main.index("sm64_saturn_fast3d_frontend_init(&sourceboot_fast3d);")
    scheduler_init = main.index("sm64_saturn_frame_pipeline_init(")
    if not begin < commit < wait < frontend_init < scheduler_init:
        raise AssertionError("bootstrap VDP2 retirement must precede frontend and scheduler initialization")

    bootstrap = main[begin : wait + len("vdp2_sync_wait();")]
    forbidden = (
        "vdp1_sync_render();",
        "vdp1_sync();",
        "sourceboot_run_source_tick();",
        "geo_process_root();",
        "sourceboot_present_generation(",
        "sourceboot_vdp1_bank_generation =",
        "sourceboot_vdp1_bank_submitted =",
        "vblank_presentation_generation =",
    )
    for operation in forbidden:
        if operation in bootstrap:
            raise AssertionError(f"bootstrap must not perform {operation}")
    return main, bootstrap


def assert_presentation_boundary(text: str) -> None:
    if "sourceboot_sim_vblank_credit_dropped" not in text:
        raise AssertionError("dropped eligible VBlank credit must be counted")

    loop, bootstrap = bootstrap_vdp2_retirement(text)
    for call in (
        "sm64_saturn_frame_pipeline_init(",
        "sm64_saturn_frame_pipeline_step(",
        "sm64_saturn_frame_pipeline_action_generation(",
        "sourceboot_frame_pipeline_dispatch(",
    ):
        if call not in loop:
            raise AssertionError(f"pure scheduler dispatch missing {call}")
    for legacy in ("SOURCEBOOT_MAX_SIM_CATCHUP", "sim_vblank_credit", "catchup <"):
        if legacy in loop:
            raise AssertionError(f"legacy catch-up scheduler remains: {legacy}")
    if "sourceboot_run_source_tick();" in loop:
        raise AssertionError("source tick escapes RUN_SIM_TICK action helper")

    terminal = presentation_function(text)
    for call in ("vdp1_sync_render();", "vdp1_sync();", "sm64_saturn_vdp2_frame_commit("):
        if terminal.count(call) != 1:
            raise AssertionError(f"terminal presentation boundary must contain one {call}")
    without_terminal = text.replace(terminal, "").replace(bootstrap, "")
    if "vdp1_sync_render();" in without_terminal or "vdp1_sync();" in without_terminal:
        raise AssertionError("VDP1 submission escapes the terminal boundary")
    if "sm64_saturn_vdp2_frame_commit(" in without_terminal:
        raise AssertionError("VDP2 commit escapes the terminal VDP1 boundary")
    if "sourceboot_vdp1_bank_generation = presentation_generation;" in terminal:
        raise AssertionError("build ownership must not be overwritten by presentation cadence")
    publish = extract_c_function(text, "sourceboot_frame_publish")
    reuse = extract_c_function(text, "sourceboot_frame_reuse_previous")
    if publish.count("sourceboot_present_generation(") != 1:
        raise AssertionError("fresh publish must make exactly one presentation attempt")
    if reuse.count("sourceboot_present_generation(") != 1:
        raise AssertionError("reuse must redraw exactly one completed presentation")
    if "sourceboot_cadence_trace_append(" in reuse:
        raise AssertionError("reuse must not manufacture a fresh cadence edge")
    if "!sourceboot_vdp1_destination_poisoned" not in reuse:
        raise AssertionError("reuse must remain fail-closed after destination poison")
    init_region = loop[loop.index("const int16_vec2_t clip"):
                       loop.index("vdp1_vram_partitions_set")]
    if init_region.count("sm64_saturn_vdp1_backend_init_with_storage(") != 2:
        raise AssertionError("both command banks need unconditional setup prefixes")
    render = extract_c_function(text, "sourceboot_frame_service_render")
    begin = render.index("sm64_saturn_vdp1_frame_bank_begin_build(")
    bind = render.index("sm64_saturn_vdp1_backend_bind_frame_bank(")
    quarantine = render.index("sm64_saturn_vdp1_frame_bank_quarantine(build_bank)")
    if not begin < bind < quarantine:
        raise AssertionError("bank acquisition must precede binding and failure quarantine")


class SourcebootPresentationBoundaryTests(unittest.TestCase):
    def setUp(self) -> None:
        self.source = SOURCEBOOT_C.read_text(encoding="utf-8")

    def source_with_bootstrap_vdp2_retirement(self) -> str:
        if BOOTSTRAP_VDP2_BEGIN in self.source:
            return self.source
        bootstrap = (
            f"    {BOOTSTRAP_VDP2_BEGIN}\n"
            f"    {BOOTSTRAP_VDP2_COMMIT}\n"
            "    vdp2_sync_wait();\n"
        )
        return self.source.replace(
            "    sm64_saturn_fast3d_frontend_init(&sourceboot_fast3d);",
            bootstrap + "    sm64_saturn_fast3d_frontend_init(&sourceboot_fast3d);",
            1,
        )

    def test_sourceboot_has_one_vblank_one_presentation_contract(self) -> None:
        assert_presentation_boundary(self.source)

    def test_rejects_bootstrap_vdp2_retirement_mutations(self) -> None:
        baseline = self.source_with_bootstrap_vdp2_retirement()
        assert_presentation_boundary(baseline)
        bootstrap = (
            f"    {BOOTSTRAP_VDP2_BEGIN}\n"
            f"    {BOOTSTRAP_VDP2_COMMIT}\n"
            "    vdp2_sync_wait();\n"
        )
        absent = baseline.replace(
            f"    {BOOTSTRAP_VDP2_BEGIN}\n"
            f"    {BOOTSTRAP_VDP2_COMMIT}\n"
            "    vdp2_sync_wait();\n",
            "",
            1,
        )
        with self.assertRaisesRegex(AssertionError, "null-snapshot VDP2 begin"):
            assert_presentation_boundary(absent)

        late = baseline.replace(
            f"    {BOOTSTRAP_VDP2_BEGIN}\n"
            f"    {BOOTSTRAP_VDP2_COMMIT}\n"
            "    vdp2_sync_wait();\n",
            "",
            1,
        ).replace(
            "    sm64_saturn_fast3d_frontend_init(&sourceboot_fast3d);",
            "    sm64_saturn_fast3d_frontend_init(&sourceboot_fast3d);\n"
            f"    {BOOTSTRAP_VDP2_BEGIN}\n"
            f"    {BOOTSTRAP_VDP2_COMMIT}\n"
            "    vdp2_sync_wait();",
            1,
        )
        with self.assertRaisesRegex(AssertionError, "precede frontend and scheduler"):
            assert_presentation_boundary(late)

        duplicate = baseline.replace(bootstrap, bootstrap * 2, 1)
        with self.assertRaisesRegex(AssertionError, "exactly one null-snapshot VDP2 begin"):
            assert_presentation_boundary(duplicate)

        vdp1_work = baseline.replace(
            "    vdp2_sync_wait();\n",
            "    vdp1_sync();\n    vdp2_sync_wait();\n",
            1,
        )
        with self.assertRaisesRegex(AssertionError, "must not perform vdp1_sync"):
            assert_presentation_boundary(vdp1_work)

        simulation_work = baseline.replace(
            "    vdp2_sync_wait();\n",
            "    sourceboot_run_source_tick();\n    vdp2_sync_wait();\n",
            1,
        )
        with self.assertRaisesRegex(AssertionError, "must not perform sourceboot_run_source_tick"):
            assert_presentation_boundary(simulation_work)

    def test_rejects_direct_source_tick_escape_mutation(self) -> None:
        mutated = self.source_with_bootstrap_vdp2_retirement().replace(
            "        sourceboot_frame_pipeline_dispatch(action, generation);",
            "        sourceboot_run_source_tick();\n"
            "        sourceboot_frame_pipeline_dispatch(action, generation);",
            1,
        )
        with self.assertRaisesRegex(AssertionError, "escapes RUN_SIM_TICK"):
            assert_presentation_boundary(mutated)

    def test_rejects_legacy_credit_reintroduction_mutation(self) -> None:
        source = self.source_with_bootstrap_vdp2_retirement()
        needle = "        sourceboot_frame_pipeline_dispatch(action, generation);"
        mutated = source.replace(
            needle,
            "        sim_vblank_credit += sourceboot_vblank_out_count;\n" + needle,
            1,
        )
        with self.assertRaisesRegex(AssertionError, "legacy catch-up"):
            assert_presentation_boundary(mutated)

    def test_rejects_terminal_boundary_escape_mutations(self) -> None:
        source = self.source_with_bootstrap_vdp2_retirement()
        call = "        sourceboot_present_generation(bank);"
        escaped_vdp1 = source.replace(
            call,
            call + "\n    vdp1_sync_render();",
            1,
        )
        with self.assertRaisesRegex(AssertionError, "VDP1 submission escapes"):
            assert_presentation_boundary(escaped_vdp1)

        escaped_vdp2 = source.replace(
            call,
            call + "\n    sm64_saturn_vdp2_frame_commit(&sourceboot_vdp2_frame,\n                                      &sourceboot_vdp2_backend);",
            1,
        )
        with self.assertRaisesRegex(AssertionError, "VDP2 commit escapes"):
            assert_presentation_boundary(escaped_vdp2)

    def test_rejects_duplicate_terminal_presentation_mutation(self) -> None:
        source = self.source_with_bootstrap_vdp2_retirement()
        call = "        sourceboot_present_generation(bank);"
        mutated = source.replace(call, f"{call}\n{call}", 1)
        with self.assertRaisesRegex(AssertionError, "exactly one presentation"):
            assert_presentation_boundary(mutated)


if __name__ == "__main__":
    unittest.main()
