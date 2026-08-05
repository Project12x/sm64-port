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
    scheduler_init = main.index("uint32_t scheduler_vblank_clock =")
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
    if "#define SOURCEBOOT_MAX_SIM_CATCHUP 2U" not in text:
        raise AssertionError("scheduler must allow one normal and one recovery tick")
    if "sourceboot_sim_vblank_credit_dropped" not in text:
        raise AssertionError("dropped eligible VBlank credit must be counted")

    loop, bootstrap = bootstrap_vdp2_retirement(text)
    credit_sample = "sim_vblank_credit += scheduler_now - scheduler_vblank_clock;"
    if loop.count(credit_sample) != 1:
        raise AssertionError("VBlank credit must be sampled exactly once per outer loop")
    if "if (scheduler_now == sourceboot_presentation_generation)" not in loop:
        raise AssertionError("a stale VBlank generation must reuse the completed VDP1 list")
    stale_wait = "sm64_saturn_source_runtime_wait_vblank();"
    stale_wait_index = loop.index(stale_wait)
    presentation_call = "sourceboot_present_generation(presentation_generation);"
    if re.search(
        r"presentation_generation\s*=\s*"
        r"sourceboot_vdp1_frame_banks\.published[^;]*snapshot_generation",
        loop,
        re.S,
    ) is None:
        raise AssertionError("presentation must use the published bank generation")
    if "continue;" not in loop[stale_wait_index : loop.index(presentation_call)]:
        raise AssertionError("stale VBlank generation must wait instead of rebuilding")
    if "catchup < SOURCEBOOT_MAX_SIM_CATCHUP" not in loop:
        raise AssertionError("recovery tick cap must guard source ticks")
    if re.search(
        r"sourceboot_sim_vblank_credit_dropped\s*\+=\s*dropped_vblank_credit;",
        loop,
    ) is None:
        raise AssertionError("eligible overrun credit must be dropped and accumulated")

    terminal = presentation_function(text)
    for call in ("vdp1_sync_render();", "vdp1_sync();", "sm64_saturn_vdp2_frame_commit("):
        if terminal.count(call) != 1:
            raise AssertionError(f"terminal presentation boundary must contain one {call}")
    without_terminal = text.replace(terminal, "").replace(bootstrap, "")
    if "vdp1_sync_render();" in without_terminal or "vdp1_sync();" in without_terminal:
        raise AssertionError("VDP1 submission escapes the terminal boundary")
    if "sm64_saturn_vdp2_frame_commit(" in without_terminal:
        raise AssertionError("VDP2 commit escapes the terminal VDP1 boundary")
    if loop.count(presentation_call) != 1:
        raise AssertionError("fresh generation must make exactly one presentation attempt")
    if stale_wait_index >= loop.index(presentation_call):
        raise AssertionError("stale VBlank wait/continue must precede presentation")
    if "sourceboot_vdp1_bank_generation = presentation_generation;" in terminal:
        raise AssertionError("build ownership must not be overwritten by presentation cadence")
    if re.search(
        r"sourceboot_vdp1_bank_displayed\s*=\s*"
        r"sourceboot_vdp1_frame_banks\.published->snapshot_generation;",
        loop,
    ) is None:
        raise AssertionError("display generation must come from the published frame bank")
    init_region = loop[loop.index("const int16_vec2_t clip"):
                       loop.index("vdp1_vram_partitions_set")]
    if init_region.count("sm64_saturn_vdp1_backend_init_with_storage(") != 2:
        raise AssertionError("both command banks need unconditional setup prefixes")
    begin = loop.index("sm64_saturn_vdp1_frame_bank_begin_build(")
    bind = loop.index("sm64_saturn_vdp1_backend_bind_frame_bank(")
    quarantine = loop.index("sm64_saturn_vdp1_frame_bank_quarantine(build_bank)")
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

    def test_rejects_four_tick_catchup_mutation(self) -> None:
        mutated = self.source_with_bootstrap_vdp2_retirement().replace(
            "#define SOURCEBOOT_MAX_SIM_CATCHUP 2U",
            "#define SOURCEBOOT_MAX_SIM_CATCHUP 4U",
        )
        with self.assertRaisesRegex(AssertionError, "one normal and one recovery"):
            assert_presentation_boundary(mutated)

    def test_rejects_credit_refill_inside_tick_mutation(self) -> None:
        source = self.source_with_bootstrap_vdp2_retirement()
        needle = "            sourceboot_run_source_tick();"
        mutated = source.replace(
            needle,
            needle
            + "\n            scheduler_now = sourceboot_vblank_out_count;"
            + "\n            sim_vblank_credit += scheduler_now - scheduler_vblank_clock;",
            1,
        )
        with self.assertRaisesRegex(AssertionError, "sampled exactly once"):
            assert_presentation_boundary(mutated)

    def test_rejects_terminal_boundary_escape_mutations(self) -> None:
        source = self.source_with_bootstrap_vdp2_retirement()
        escaped_vdp1 = source.replace(
            "            sourceboot_present_generation(presentation_generation);",
            "            sourceboot_present_generation(presentation_generation);\n    vdp1_sync_render();",
            1,
        )
        with self.assertRaisesRegex(AssertionError, "VDP1 submission escapes"):
            assert_presentation_boundary(escaped_vdp1)

        escaped_vdp2 = source.replace(
            "            sourceboot_present_generation(presentation_generation);",
            "            sourceboot_present_generation(presentation_generation);\n    sm64_saturn_vdp2_frame_commit(&sourceboot_vdp2_frame,\n                                      &sourceboot_vdp2_backend);",
            1,
        )
        with self.assertRaisesRegex(AssertionError, "VDP2 commit escapes"):
            assert_presentation_boundary(escaped_vdp2)

    def test_rejects_duplicate_terminal_presentation_mutation(self) -> None:
        source = self.source_with_bootstrap_vdp2_retirement()
        call = "            sourceboot_present_generation(presentation_generation);"
        mutated = source.replace(call, f"{call}\n{call}", 1)
        with self.assertRaisesRegex(AssertionError, "exactly one presentation"):
            assert_presentation_boundary(mutated)


if __name__ == "__main__":
    unittest.main()
