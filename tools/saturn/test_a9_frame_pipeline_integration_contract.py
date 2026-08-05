#!/usr/bin/env python3
"""RED source contracts for A9's sourceboot frame-pipeline adapter.

The pure scheduler owns policy; these checks define the deliberately small
hardware adapter expected in sourceboot/main.c.  They remain source-level so
the initial cutover can reuse the existing renderer and transfer machinery
without requiring Yaul in a host test.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE_PATH = ROOT / "src" / "port" / "saturn" / "sourceboot" / "main.c"


def extract_c_function(source: str, name: str) -> str:
    match = re.search(rf"\b{name}\s*\([^)]*\)\s*\{{", source)
    if match is None:
        raise AssertionError(f"required adapter helper {name} not found")
    depth = 0
    for index in range(match.end() - 1, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start() : index + 1]
    raise AssertionError(f"adapter helper {name} is not terminated")


def call_uses_generation(body: str, function: str) -> bool:
    return re.search(
        rf"\b{re.escape(function)}\s*\([^;]*\bgeneration\b[^;]*\)",
        body,
        re.S,
    ) is not None


class A9FramePipelineIntegrationContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.source = SOURCE_PATH.read_text(encoding="utf-8")

    def test_outer_loop_is_owned_by_the_pure_scheduler_dispatch(self) -> None:
        if '#include "saturn_frame_pipeline.h"' not in self.source:
            self.fail("sourceboot must include the pure frame scheduler")
        self.assertRegex(
            self.source,
            r"static\s+sm64_saturn_frame_pipeline_t\s+sourceboot_frame_pipeline\s*;",
        )
        main = extract_c_function(self.source, "main")
        self.assertIn("sm64_saturn_frame_pipeline_init(", main)
        self.assertIn("sm64_saturn_frame_pipeline_step(", main)
        self.assertIn("sm64_saturn_frame_pipeline_action_generation(", main)
        self.assertIn("sourceboot_frame_pipeline_dispatch(", main)

        dispatch = extract_c_function(
            self.source, "sourceboot_frame_pipeline_dispatch"
        )
        self.assertRegex(dispatch, r"switch\s*\(\s*action\s*\)")
        for action in (
            "SM64_SATURN_FRAME_RUN_SIM_TICK",
            "SM64_SATURN_FRAME_SERVICE_RENDER_JOBS",
            "SM64_SATURN_FRAME_POLL_TRANSFERS",
            "SM64_SATURN_FRAME_PUBLISH_FRAME",
            "SM64_SATURN_FRAME_REUSE_PREVIOUS_FRAME",
            "SM64_SATURN_FRAME_WAIT_VBLANK",
        ):
            self.assertEqual(dispatch.count(f"case {action}:"), 1)

        self.assertNotIn("SOURCEBOOT_MAX_SIM_CATCHUP", main)
        self.assertNotIn("sim_vblank_credit", main)
        self.assertNotIn("catchup <", main)
        self.assertNotIn("skipped_snapshot", main)

    def test_run_tick_action_produces_the_exact_scheduler_generation(self) -> None:
        run_tick = extract_c_function(
            self.source, "sourceboot_frame_run_sim_tick"
        )
        tick = run_tick.index("sourceboot_run_source_tick();")
        generation_check = re.search(
            r"sourceboot_sim_tick_count\s*!=\s*generation", run_tick
        )
        self.assertIsNotNone(
            generation_check,
            "the produced snapshot generation must be checked against the action",
        )
        assert generation_check is not None
        self.assertLess(tick, generation_check.start())
        for forbidden in (
            "sm64_saturn_demo_render_frame(",
            "sm64_saturn_vdp1_frame_bank_submit_transfers(",
            "sourceboot_present_generation(",
        ):
            self.assertNotIn(forbidden, run_tick)

    def test_render_service_keeps_snapshot_bank_and_ack_generation_coherent(self) -> None:
        render = extract_c_function(
            self.source, "sourceboot_frame_service_render"
        )
        ordered_calls = (
            "sm64_saturn_render_snapshot_acquire_ready(",
            "sm64_saturn_mario_actor_pose(",
            "sm64_saturn_vdp1_frame_bank_begin_build(",
            "sm64_saturn_vdp1_frame_bank_set_camera_snapshot(",
            "sm64_saturn_vdp1_backend_bind_frame_bank(",
            "sm64_saturn_demo_render_frame(",
            "sm64_saturn_vdp1_frame_bank_ready(",
            "sm64_saturn_frame_pipeline_render_complete(",
            "sm64_saturn_render_snapshot_complete(",
            "sm64_saturn_render_snapshot_retire(",
        )
        positions = [render.index(call) for call in ordered_calls]
        self.assertEqual(positions, sorted(positions))
        for call in (
            "sm64_saturn_render_snapshot_acquire_ready",
            "sm64_saturn_vdp1_frame_bank_begin_build",
            "sm64_saturn_vdp1_frame_bank_ready",
            "sm64_saturn_frame_pipeline_render_complete",
        ):
            self.assertTrue(
                call_uses_generation(render, call),
                f"{call} must use the scheduler action generation",
            )
        self.assertNotIn("scheduler_now", render)
        self.assertNotIn("sourceboot_vblank_out_count", render)
        self.assertIn("sourceboot_fast3d.profile.pipeline_faults++", render)
        self.assertIn("sm64_saturn_vdp1_frame_bank_quarantine(", render)
        for forbidden in (
            "sourceboot_run_source_tick();",
            "sm64_saturn_vdp1_frame_bank_submit_transfers(",
            "sm64_saturn_vdp1_frame_bank_publish(",
            "sourceboot_present_generation(",
        ):
            self.assertNotIn(forbidden, render)

    def test_poll_action_is_one_bounded_nonblocking_transfer_opportunity(self) -> None:
        poll = extract_c_function(self.source, "sourceboot_frame_poll_transfers")
        submit = poll.index("sm64_saturn_vdp1_frame_bank_submit_transfers(")
        poll_call = poll.index("sm64_saturn_vdp1_frame_bank_poll_transfers(")
        complete = poll.index("sm64_saturn_frame_pipeline_transfer_complete(")
        self.assertLess(submit, poll_call)
        self.assertLess(poll_call, complete)
        self.assertTrue(
            call_uses_generation(
                poll, "sm64_saturn_frame_pipeline_transfer_complete"
            )
        )
        self.assertRegex(
            poll,
            r"snapshot_generation\s*!=\s*generation",
            "the pending bank must be checked against the action generation",
        )
        poisoned = poll.index("sourceboot_vdp1_destination_poisoned = true;")
        clear_pending = poll.index("sourceboot_vdp1_transfer_pending = NULL;", poisoned)
        self.assertLess(poisoned, clear_pending)
        self.assertIn("sourceboot_vdp1_transfer_faults++", poll)
        self.assertNotRegex(poll, r"\b(?:for|while)\s*\(")
        self.assertNotIn("continue;", poll)
        self.assertNotIn("sourceboot_present_generation(", poll)

    def test_publish_ack_occurs_only_after_runtime_bank_publication(self) -> None:
        publish = extract_c_function(self.source, "sourceboot_frame_publish")
        generation_check = re.search(
            r"snapshot_generation\s*!=\s*generation", publish
        )
        self.assertIsNotNone(generation_check)
        arm = publish.index("sm64_saturn_vdp1_frame_bank_arm_resident_list(")
        target_publish = publish.index("sm64_saturn_vdp1_frame_bank_publish(")
        model_ack = publish.index("sm64_saturn_frame_pipeline_publish_complete(")
        terminal = publish.index("sourceboot_present_generation(")
        cadence = publish.index("sourceboot_cadence_trace_append(")
        self.assertLess(arm, target_publish)
        self.assertLess(target_publish, model_ack)
        self.assertLess(target_publish, terminal)
        self.assertLess(terminal, cadence)
        self.assertTrue(
            call_uses_generation(
                publish, "sm64_saturn_frame_pipeline_publish_complete"
            )
        )
        self.assertIn("sourceboot_vdp1_destination_poisoned = true;", publish)

    def test_reuse_never_changes_bank_ownership_or_creates_a_cadence_edge(self) -> None:
        reuse = extract_c_function(
            self.source, "sourceboot_frame_reuse_previous"
        )
        present = reuse.index("sourceboot_present_generation(")
        published_guard = reuse.index("sourceboot_vdp1_frame_banks.published")
        poison_guard = reuse.index("!sourceboot_vdp1_destination_poisoned")
        self.assertLess(published_guard, present)
        self.assertLess(poison_guard, present)
        for forbidden in (
            "sm64_saturn_vdp1_frame_bank_begin_build(",
            "sm64_saturn_vdp1_frame_bank_ready(",
            "sm64_saturn_vdp1_frame_bank_submit_transfers(",
            "sm64_saturn_vdp1_frame_bank_arm_resident_list(",
            "sm64_saturn_vdp1_frame_bank_publish(",
            "sm64_saturn_vdp1_frame_bank_retire(",
            "sm64_saturn_vdp1_frame_bank_quarantine(",
            "sourceboot_cadence_trace_append(",
        ):
            self.assertNotIn(forbidden, reuse)

    def test_dispatch_maps_wait_and_telemetry_without_extra_work(self) -> None:
        dispatch = extract_c_function(
            self.source, "sourceboot_frame_pipeline_dispatch"
        )
        wait_case = dispatch.index("case SM64_SATURN_FRAME_WAIT_VBLANK:")
        wait_call = dispatch.index(
            "sm64_saturn_source_runtime_wait_vblank();", wait_case
        )
        self.assertGreater(wait_call, wait_case)
        self.assertEqual(
            self.source.count("sm64_saturn_source_runtime_wait_vblank();"), 1,
            "only the scheduler WAIT action may block for VBlank",
        )
        self.assertRegex(
            dispatch,
            r"sourceboot_sim_vblank_credit_dropped\s*=\s*"
            r"sourceboot_frame_pipeline\.dropped_sim_credit\s*;",
        )
        self.assertRegex(
            dispatch,
            r"sim_vblank_credit_dropped\s*=\s*"
            r"sourceboot_sim_vblank_credit_dropped\s*;",
        )


if __name__ == "__main__":
    unittest.main()
