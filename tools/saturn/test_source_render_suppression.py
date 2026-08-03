#!/usr/bin/env python3
"""Source policy checks for the Saturn IR-owned render path."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
AREA_C = REPO_ROOT / "src" / "game" / "area.c"
SOURCEBOOT_C = REPO_ROOT / "src" / "port" / "saturn" / "sourceboot" / "main.c"
SOURCEBOOT_MAKEFILE = REPO_ROOT / "src" / "port" / "saturn" / "sourceboot" / "Makefile"


def extract_c_function(path: Path, name: str) -> str:
    text = path.read_text(encoding="utf-8")
    match = re.search(rf"\b{name}\s*\([^)]*\)\s*\{{", text)
    if match is None:
        raise AssertionError(f"function {name} not found in {path}")
    depth = 0
    for index in range(match.end() - 1, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[match.end() : index]
    raise AssertionError(f"function {name} is not terminated in {path}")


def extract_if_block(body: str, condition: str) -> str:
    match = re.search(rf"if\s*\(\s*{re.escape(condition)}\s*\)\s*\{{", body)
    if match is None:
        raise AssertionError(f"guard if ({condition}) not found")
    depth = 0
    for index in range(match.end() - 1, len(body)):
        if body[index] == "{":
            depth += 1
        elif body[index] == "}":
            depth -= 1
            if depth == 0:
                return body[match.end() : index]
    raise AssertionError(f"guard if ({condition}) is not terminated")


class SourceRenderSuppressionTests(unittest.TestCase):
    def test_dormant_policy_guard_keeps_known_outside_state_calls(self) -> None:
        """Keep the dormant guard structurally separated from known state calls."""
        body = extract_c_function(AREA_C, "render_game")
        self.assertIn("sm64_saturn_source_runtime_scene_graph_suppressed", body)
        guarded = extract_if_block(body, "!scene_graph_suppressed")
        self.assertIn("geo_process_root(", guarded)
        for call in (
            "do_cutscene_handler(",
            "print_displaying_credits_entry(",
            "render_menus_and_dialogs(",
            "render_screen_transition(",
        ):
            self.assertIn(call, body)
            self.assertNotIn(call, guarded)

    def test_skip_geo_diagnostic_is_sealed_and_default_off(self) -> None:
        """The upper-bound diagnostic is opt-in and scopes only one source tick."""
        body = extract_c_function(SOURCEBOOT_C, "sourceboot_run_source_tick")
        makefile = SOURCEBOOT_MAKEFILE.read_text(encoding="utf-8")
        self.assertIn("SATURN_EXPERIMENTAL_SKIP_GEO_WALK ?= 0", makefile)
        self.assertIn("SATURN_EXPERIMENTAL_SKIP_GEO_WALK must be 0 or 1", makefile)
        self.assertIn("SATURN_EXPERIMENTAL_SKIP_GEO_WALK=1 requires SATURN_DEMO_PATH=1 and SATURN_SOURCEBOOT_ROUTE_REPLAY=1", makefile)
        self.assertIn("-DSATURN_EXPERIMENTAL_SKIP_GEO_WALK=$(SATURN_EXPERIMENTAL_SKIP_GEO_WALK)", makefile)
        self.assertIn("diag-skip-geo", makefile)
        self.assertIn("#if SATURN_EXPERIMENTAL_SKIP_GEO_WALK", body)
        branch = body.split("#if SATURN_EXPERIMENTAL_SKIP_GEO_WALK", 1)[1].split("#endif", 1)[0]
        setter = "sm64_saturn_source_runtime_set_scene_graph_suppressed("
        self.assertEqual(branch.count(setter), 2)
        self.assertRegex(branch, re.compile(r"set_scene_graph_suppressed\(true\);\s*game_loop_one_iteration\(\);\s*sm64_saturn_source_runtime_set_scene_graph_suppressed\(false\);", re.S))
        normal = body.split("#if SATURN_EXPERIMENTAL_SKIP_GEO_WALK", 1)[0]
        self.assertNotIn(setter, normal)


if __name__ == "__main__":
    unittest.main()
