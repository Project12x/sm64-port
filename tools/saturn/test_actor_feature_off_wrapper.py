#!/usr/bin/env python3
"""Contract tests for Task 16's feature-off Mario callback wrappers.

The generic actor queue is infrastructure-only.  Until its production
cutover, the four-entry world graph must route ACTOR_ADMIT/ACTOR_LOWER through
the established Mario callbacks when the dynamic-actor feature is disabled;
an accidental feature-on build must fail closed rather than reinterpret that
pair as a generic actor renderer.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "src/port/saturn/gfx/saturn_demo_render.c"


def extract_function(source: str, name: str) -> str:
    match = re.search(rf"\b{name}\s*\([^)]*\)\s*\{{", source)
    if match is None:
        raise AssertionError(f"missing function {name}")
    depth = 0
    for index in range(match.end() - 1, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start() : index + 1]
    raise AssertionError(f"unterminated function {name}")


def split_feature_branches(function: str) -> tuple[str, str]:
    """Return (feature_on, feature_off) source for one wrapper."""
    marker = "#if SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE"
    start = function.find(marker)
    if start < 0:
        raise AssertionError("wrapper is missing the canonical feature branch")
    else_marker = function.find("#else", start + len(marker))
    end_marker = function.find("#endif", else_marker + len("#else"))
    if else_marker < 0 or end_marker < 0:
        raise AssertionError("wrapper feature branch is not a complete #if/#else/#endif")
    return function[start + len(marker) : else_marker], function[
        else_marker + len("#else") : end_marker
    ]


def assert_contract(source: str) -> None:
    if not re.search(
        r"#ifndef\s+SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE\s*"
        r"#define\s+SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE\s+0",
        source,
    ):
        raise AssertionError("renderer must default dynamic actors to feature-off")

    admit = extract_function(source, "demo_actor_admit_compat_wrapper")
    lower = extract_function(source, "demo_actor_lower_compat_wrapper")
    for function, legacy in ((admit, "demo_actor_queue_transform"),
                             (lower, "demo_actor_queue_classify")):
        feature_on, feature_off = split_feature_branches(function)
        if "return false;" not in feature_on:
            raise AssertionError("feature-on actor path must fail closed")
        if legacy in feature_on:
            raise AssertionError("feature-on branch delegates to Mario callback")
        if f"return {legacy}(job, claimed_state, context);" not in feature_off:
            raise AssertionError("feature-off wrapper changed the Mario callback")
        if "return false;" in feature_off:
            raise AssertionError("feature-off branch fails instead of preserving Mario")
        if "sm64_saturn_actor_instance_queue" in function:
            raise AssertionError("feature-off wrapper may not enter generic queue")

    table = extract_function(source, "demo_render_job_callbacks")
    if "demo_actor_admit_compat_wrapper" not in table:
        raise AssertionError("ACTOR_ADMIT table entry bypasses compatibility wrapper")
    if "demo_actor_lower_compat_wrapper" not in table:
        raise AssertionError("ACTOR_LOWER table entry bypasses compatibility wrapper")
    if "demo_actor_queue_transform," in table or "demo_actor_queue_classify," in table:
        raise AssertionError("world graph still directly binds Mario callbacks")


class ActorFeatureOffWrapperTests(unittest.TestCase):
    def setUp(self) -> None:
        self.source = SOURCE.read_text(encoding="utf-8")

    def test_feature_off_preserves_the_mario_pair_and_feature_on_fails_closed(self) -> None:
        assert_contract(self.source)

    def test_direct_table_binding_regression_is_caught(self) -> None:
        mutated = self.source.replace(
            "demo_actor_admit_compat_wrapper,\n        demo_actor_lower_compat_wrapper,",
            "demo_actor_queue_transform,\n        demo_actor_queue_classify,",
        )
        with self.assertRaises(AssertionError):
            assert_contract(mutated)

    def test_missing_feature_off_delegation_regression_is_caught(self) -> None:
        mutated = self.source.replace(
            "return demo_actor_queue_transform(job, claimed_state, context);",
            "return false;",
            1,
        )
        with self.assertRaises(AssertionError):
            assert_contract(mutated)

    def test_feature_on_delegation_regression_is_caught(self) -> None:
        mutated = self.source.replace(
            "    return false;\n#else\n    return demo_actor_queue_transform",
            "    return demo_actor_queue_transform(job, claimed_state, context);\n#else\n    return demo_actor_queue_transform",
            1,
        )
        with self.assertRaises(AssertionError):
            assert_contract(mutated)

    def test_feature_off_failure_regression_is_caught(self) -> None:
        mutated = self.source.replace(
            "#else\n    return demo_actor_queue_transform(job, claimed_state, context);",
            "#else\n    return false;",
            1,
        )
        with self.assertRaises(AssertionError):
            assert_contract(mutated)

    def test_lower_wrapper_delegation_change_is_caught(self) -> None:
        mutated = self.source.replace(
            "return demo_actor_queue_classify(job, claimed_state, context);",
            "return demo_actor_queue_transform(job, claimed_state, context);",
            1,
        )
        with self.assertRaises(AssertionError):
            assert_contract(mutated)


if __name__ == "__main__":
    unittest.main()
