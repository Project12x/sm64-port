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


def assert_presentation_boundary(text: str) -> None:
    if "#define SOURCEBOOT_MAX_SIM_CATCHUP 2U" not in text:
        raise AssertionError("scheduler must allow one normal and one recovery tick")
    if "sourceboot_sim_vblank_credit_dropped" not in text:
        raise AssertionError("dropped eligible VBlank credit must be counted")

    loop = extract_c_function(text, "main")
    credit_sample = "sim_vblank_credit += scheduler_now - scheduler_vblank_clock;"
    if loop.count(credit_sample) != 1:
        raise AssertionError("VBlank credit must be sampled exactly once per outer loop")
    if "if (scheduler_now == sourceboot_presentation_generation)" not in loop:
        raise AssertionError("a stale VBlank generation must reuse the completed VDP1 list")
    if "sm64_saturn_source_runtime_wait_vblank();\n            continue;" not in loop:
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
    without_terminal = text.replace(terminal, "")
    if "vdp1_sync_render();" in without_terminal or "vdp1_sync();" in without_terminal:
        raise AssertionError("VDP1 submission escapes the terminal boundary")
    if "sm64_saturn_vdp2_frame_commit(" in without_terminal:
        raise AssertionError("VDP2 commit escapes the terminal VDP1 boundary")
    if "sourceboot_present_generation(scheduler_now);" not in loop:
        raise AssertionError("presentation must use the observed VBlank generation")
    if "sourceboot_vdp1_bank_generation = presentation_generation;" not in terminal:
        raise AssertionError("VDP1 ownership must be keyed to the presentation generation")


class SourcebootPresentationBoundaryTests(unittest.TestCase):
    def setUp(self) -> None:
        self.source = SOURCEBOOT_C.read_text(encoding="utf-8")

    def test_sourceboot_has_one_vblank_one_presentation_contract(self) -> None:
        assert_presentation_boundary(self.source)

    def test_rejects_four_tick_catchup_mutation(self) -> None:
        mutated = self.source.replace(
            "#define SOURCEBOOT_MAX_SIM_CATCHUP 2U",
            "#define SOURCEBOOT_MAX_SIM_CATCHUP 4U",
        )
        with self.assertRaisesRegex(AssertionError, "one normal and one recovery"):
            assert_presentation_boundary(mutated)

    def test_rejects_credit_refill_inside_tick_mutation(self) -> None:
        needle = "            sourceboot_run_source_tick();"
        mutated = self.source.replace(
            needle,
            needle
            + "\n            scheduler_now = sourceboot_vblank_out_count;"
            + "\n            sim_vblank_credit += scheduler_now - scheduler_vblank_clock;",
            1,
        )
        with self.assertRaisesRegex(AssertionError, "sampled exactly once"):
            assert_presentation_boundary(mutated)

    def test_rejects_terminal_boundary_escape_mutations(self) -> None:
        escaped_vdp1 = self.source.replace(
            "    sourceboot_present_generation(scheduler_now);",
            "    sourceboot_present_generation(scheduler_now);\n    vdp1_sync_render();",
            1,
        )
        with self.assertRaisesRegex(AssertionError, "VDP1 submission escapes"):
            assert_presentation_boundary(escaped_vdp1)

        escaped_vdp2 = self.source.replace(
            "    sourceboot_present_generation(scheduler_now);",
            "    sourceboot_present_generation(scheduler_now);\n    sm64_saturn_vdp2_frame_commit(&sourceboot_vdp2_frame,\n                                      &sourceboot_vdp2_backend);",
            1,
        )
        with self.assertRaisesRegex(AssertionError, "VDP2 commit escapes"):
            assert_presentation_boundary(escaped_vdp2)


if __name__ == "__main__":
    unittest.main()
