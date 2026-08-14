#!/usr/bin/env python3
"""Contract tests for Mario graph ownership and generic actor integration.

The generic actor queue is infrastructure-only.  Until its production
cutover, the four-entry graph's ACTOR_ADMIT/ACTOR_LOWER descriptors remain the
Mario transform/classify pair in both feature modes. Generic actors use their
own preparation and emission path and must not steal those callback IDs.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "src/port/saturn/gfx/saturn_demo_render.c"
SOURCEBOOT_MAKEFILE = ROOT / "src/port/saturn/sourceboot/Makefile"


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


def assert_contract(source: str) -> None:
    if not re.search(
        r"#ifndef\s+SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE\s*"
        r"#define\s+SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE\s+0",
        source,
    ):
        raise AssertionError("renderer must default dynamic actors to feature-off")

    admit = extract_function(source, "demo_actor_admit_compat_wrapper")
    lower = extract_function(source, "demo_actor_lower_compat_wrapper")
    for function, legacy in (
            (admit, "demo_actor_queue_transform"),
            (lower, "demo_actor_queue_classify")):
        if f"return {legacy}(job, claimed_state, context);" not in function:
            raise AssertionError("render graph changed the Mario callback owner")
        if "SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE" in function:
            raise AssertionError("generic feature flag may not retarget Mario jobs")
        if "demo_generic_actor_" in function:
            raise AssertionError("Mario job delegates to generic actor consumer")

    prepare = extract_function(source, "demo_render_prepare_publish")
    finalize = extract_function(source, "demo_render_finalize")
    if "demo_generic_actor_prepare(transaction, generation)" not in prepare:
        raise AssertionError("generic actor preparation is not on the normal frame path")
    if "demo_generic_actor_emit(transaction, true)" not in finalize:
        raise AssertionError("generic actor emission is not on the normal frame path")

    emit = extract_function(source, "demo_generic_actor_emit")
    if "descriptor_index < transaction->actor_runtime->queue.count" not in emit:
        raise AssertionError("generic actor emission must consume the compact queue")
    if "descriptor_index < transaction->actor_handoff.snapshot_count" in emit:
        raise AssertionError("generic actor emission walks culled snapshots")
    if not re.search(
        r"if \(cross == 0\)\s*\{\s*"
        r"transaction->profile->reject_degenerate\+\+;\s*"
        r"emitted\+\+;\s*continue;\s*\}",
        emit,
    ):
        raise AssertionError("zero-area actor records must be consumed and culled")
    if not re.search(
        r"if \(!projected_visible\)\s*\{\s*"
        r"transaction->profile->reject_near_far\+\+;\s*"
        r"emitted\+\+;\s*continue;\s*\}",
        emit,
    ):
        raise AssertionError("near-plane actor records must be consumed and culled")

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

    def test_mario_graph_is_preserved_while_generic_actors_use_their_own_path(self) -> None:
        assert_contract(self.source)

    def test_sourceboot_links_generic_actor_consumer_modules(self) -> None:
        makefile = SOURCEBOOT_MAKEFILE.read_text(encoding="utf-8")
        for module in (
            "saturn_actor_batch.c",
            "saturn_actor_instance_queue.c",
            "saturn_actor_material.c",
            "saturn_actor_runtime_handoff.c",
        ):
            with self.subTest(module=module):
                self.assertIn(module, makefile)

    def test_direct_table_binding_regression_is_caught(self) -> None:
        mutated = self.source.replace(
            "demo_actor_admit_compat_wrapper,\n        demo_actor_lower_compat_wrapper,",
            "demo_actor_queue_transform,\n        demo_actor_queue_classify,",
        )
        with self.assertRaises(AssertionError):
            assert_contract(mutated)

    def test_missing_mario_delegation_regression_is_caught(self) -> None:
        mutated = self.source.replace(
            "return demo_actor_queue_transform(job, claimed_state, context);",
            "return false;",
            1,
        )
        with self.assertRaises(AssertionError):
            assert_contract(mutated)

    def test_generic_callback_retarget_regression_is_caught(self) -> None:
        mutated = self.source.replace(
            "return demo_actor_queue_transform(job, claimed_state, context);",
            "return demo_generic_actor_admit(job, claimed_state, context);",
            1,
        )
        with self.assertRaises(AssertionError):
            assert_contract(mutated)

    def test_mario_failure_regression_is_caught(self) -> None:
        mutated = self.source.replace(
            "return demo_actor_queue_transform(job, claimed_state, context);",
            "return false;",
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

    def test_actor_emission_cannot_walk_the_uncompacted_snapshot_bank(self) -> None:
        mutated = self.source.replace(
            "descriptor_index < transaction->actor_runtime->queue.count",
            "descriptor_index < transaction->actor_handoff.snapshot_count",
            1,
        )
        with self.assertRaises(AssertionError):
            assert_contract(mutated)

    def test_zero_area_actor_record_cannot_abort_the_frame(self) -> None:
        mutated = self.source.replace(
            "transaction->profile->reject_degenerate++;\n"
            "                    emitted++;\n"
            "                    continue;",
            "transaction->profile->reject_degenerate++;\n"
            "                    valid = false;",
            1,
        )
        with self.assertRaises(AssertionError):
            assert_contract(mutated)

    def test_near_plane_actor_record_cannot_abort_the_frame(self) -> None:
        mutated = self.source.replace(
            "transaction->profile->reject_near_far++;\n"
            "                    emitted++;\n"
            "                    continue;",
            "transaction->profile->reject_near_far++;\n"
            "                    valid = false;",
            1,
        )
        with self.assertRaises(AssertionError):
            assert_contract(mutated)


if __name__ == "__main__":
    unittest.main()
