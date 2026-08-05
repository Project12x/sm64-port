#!/usr/bin/env python3
"""Source contracts for Task A9's target-visible phase cadence trace."""

from pathlib import Path
import re
import unittest


SOURCE = (Path(__file__).resolve().parents[2] / "src/port/saturn/sourceboot/main.c").read_text(encoding="utf-8")


class A9CadenceTraceContractTests(unittest.TestCase):
    def test_trace_is_fixed_size_seqlock_and_published_through_p2(self) -> None:
        self.assertIn("sizeof(sm64_saturn_sourceboot_cadence_trace_t) == 60U", SOURCE)
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
        ):
            self.assertIn(field, SOURCE)
        self.assertIn("sourceboot_cadence_trace_append", SOURCE)

    def test_existing_scheduler_is_only_instrumented_not_reordered(self) -> None:
        sim_before = SOURCE.index("const uint32_t simulation_vblank_start")
        tick = SOURCE.index("sourceboot_run_source_tick();", sim_before)
        sim_after = SOURCE.index("sourceboot_phase_accumulate", tick)
        self.assertLess(sim_before, tick)
        self.assertLess(tick, sim_after)
        render_start = SOURCE.index("const uint32_t construction_vblank_start")
        snapshot = SOURCE.index("sm64_saturn_render_snapshot_acquire_ready(", render_start)
        pose = SOURCE.index("sm64_saturn_mario_actor_pose(", render_start)
        render_call = SOURCE.index("sm64_saturn_demo_render_frame(", render_start)
        render_end = SOURCE.index("sourceboot_phase_accumulate", render_call)
        self.assertLess(render_start, snapshot)
        self.assertLess(render_start, pose)
        self.assertLess(render_start, render_call)
        self.assertLess(render_call, render_end)


if __name__ == "__main__":
    unittest.main()
