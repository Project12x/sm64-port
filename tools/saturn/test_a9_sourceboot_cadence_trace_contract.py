#!/usr/bin/env python3
"""Source contracts for Task A9's target-visible phase cadence trace."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/port/saturn/sourceboot/main.c").read_text(encoding="utf-8")
RENDERER = (ROOT / "src/port/saturn/gfx/saturn_demo_render.c").read_text(
    encoding="utf-8"
)


class A9CadenceTraceContractTests(unittest.TestCase):
    def test_trace_is_fixed_size_seqlock_and_published_through_p2(self) -> None:
        self.assertIn("SOURCEBOOT_CADENCE_TRACE_VERSION 2U", SOURCE)
        self.assertIn("sizeof(sm64_saturn_sourceboot_cadence_trace_t) == 76U", SOURCE)
        self.assertIn("sourceboot_cadence_trace_visible", SOURCE)
        self.assertIn("CPU_CACHE_THROUGH | (uintptr_t)&sourceboot_cadence_trace", SOURCE)
        self.assertIn("trace->sequence_begin = next_sequence - 1U", SOURCE)
        self.assertIn("trace->sequence_end = next_sequence - 1U", SOURCE)
        self.assertIn("trace->sequence_end = next_sequence", SOURCE)
        self.assertIn("trace->sequence_begin = next_sequence", SOURCE)
        odd_begin = SOURCE.index("trace->sequence_begin = next_sequence - 1U")
        odd_end = SOURCE.index("trace->sequence_end = next_sequence - 1U")
        payload = SOURCE.index("trace->observed_vblank_generation", odd_begin)
        self.assertLess(odd_begin, payload)
        self.assertLess(odd_end, payload)

    def test_trace_records_all_three_phase_accumulators_and_coherence_generations(self) -> None:
        for field in (
            "simulation_vblank_crossings", "simulation_count",
            "construction_vblank_crossings", "construction_count",
            "transport_presentation_vblank_crossings",
            "transport_presentation_count",
            "frame_generation", "build_generation", "presentation_generation",
            "dropped_vblank_credit",
            "slave_work_vblank_crossings", "slave_work_count",
            "master_finalize_vblank_crossings", "master_finalize_count",
        ):
            self.assertIn(field, SOURCE)
        self.assertIn("sourceboot_cadence_trace_append", SOURCE)

    def test_existing_scheduler_is_only_instrumented_not_reordered(self) -> None:
        sim_before = SOURCE.index("const uint32_t simulation_vblank_start")
        tick = SOURCE.index("sourceboot_run_source_tick();", sim_before)
        sim_after = SOURCE.index("sourceboot_phase_accumulate", tick)
        self.assertLess(sim_before, tick)
        self.assertLess(tick, sim_after)
        render_start = SOURCE.index("static void sourceboot_frame_service_render")
        phase_begin = SOURCE.index(
            "sm64_saturn_render_overlap_phase_begin(", render_start
        )
        snapshot = SOURCE.index("sm64_saturn_render_snapshot_acquire_ready(", render_start)
        pose = SOURCE.index("sm64_saturn_mario_actor_pose(", render_start)
        phase_bind = SOURCE.index(
            "sm64_saturn_render_overlap_phase_bind(", snapshot
        )
        render_call = SOURCE.index("sm64_saturn_demo_render_start_frame(", render_start)
        render_started = SOURCE.index("sourceboot_render_started = true", render_call)
        retained = SOURCE.index(
            "sm64_saturn_render_overlap_phase_retains(", render_start
        )
        poll_call = SOURCE.index("sm64_saturn_demo_render_poll_frame(", render_start)
        render_end = SOURCE.index("sourceboot_render_overlap_terminal", poll_call)
        self.assertLess(render_start, phase_begin)
        self.assertLess(phase_begin, snapshot)
        self.assertLess(render_start, pose)
        self.assertLess(pose, phase_bind)
        self.assertLess(phase_bind, render_call)
        self.assertLess(render_start, render_call)
        self.assertLess(render_call, render_started)
        self.assertLess(retained, poll_call)
        self.assertLess(poll_call, render_end)
        self.assertNotIn("sourceboot_active_slave_vblank_start", SOURCE)
        self.assertNotIn("master_finalize_vblank_start", SOURCE)
        self.assertIn("sm64_saturn_demo_render_observe_lifecycle(", SOURCE)
        self.assertIn("sourceboot_render_overlap_phase.construction_vblank_crossings", SOURCE)

    def test_renderer_uses_generation_lifetime_and_terminal_refresh(self) -> None:
        self.assertIn("sm64_saturn_lod_lifetime_observe_scene(", RENDERER)
        self.assertIn("sm64_saturn_lod_lifetime_begin(", RENDERER)
        self.assertIn("sm64_saturn_lod_lifetime_select(", RENDERER)
        self.assertIn("sm64_saturn_lod_lifetime_finish(", RENDERER)
        refresh = RENDERER.index(
            "sm64_saturn_render_job_runtime_refresh_terminal_telemetry();"
        )
        snapshot = RENDERER.index(
            "sm64_saturn_render_job_runtime_telemetry_snapshot(", refresh
        )
        reset = RENDERER.index(
            "sm64_saturn_render_job_queue_reset_retired(", snapshot
        )
        self.assertLess(refresh, snapshot)
        self.assertLess(snapshot, reset)


if __name__ == "__main__":
    unittest.main()
