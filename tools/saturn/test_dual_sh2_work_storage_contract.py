#!/usr/bin/env python3
"""Pin dual-SH2 renderer scratch placement to one shared macro.

Since the Sprint 1 recovery revert (Task 8), DEMO_CPU_WORK_CACHE is
deliberately empty: the per-primitive working set lives in 32-bit HWRAM
.bss as it did at the A9A baseline.  The 16-bit LWRAM eviction
(ec7b992a/91f02ffd) was the mechanism of the 5.29 FPS -> ~1 FPS collapse
and must not silently return; re-evicting requires editing the macro (and
this contract) explicitly, with the fallback ladder in CHANGELOG.md.
"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
DEMO = (ROOT / "src/port/saturn/gfx/saturn_demo_render.c").read_text()
ADMISSION = (ROOT / "src/port/saturn/gfx/saturn_scene_admission.c").read_text()


class DualSh2WorkStorageContractTests(unittest.TestCase):
    def _declaration(self, source: str, symbol: str) -> str:
        match = re.search(
            rf"static[^;]*\b{re.escape(symbol)}\b[^;]*;", source,
            re.MULTILINE | re.DOTALL,
        )
        self.assertIsNotNone(match, symbol)
        return match.group(0)

    def test_demo_cpu_work_arrays_are_hwram_owned(self):
        # These arrays are either master-only assembly state or disjoint
        # lane-owned classify state.  They are not VDP1/SCU transport banks.
        # They are per-primitive inner-loop operands, so they stay in 32-bit
        # HWRAM via the (empty) shared placement macro.
        symbols = (
            "s_spatial_ref_seen",
            "s_spatial_node_seen",
            "s_render_work_order",
            "s_primitive_leaf_id",
            "s_primitive_work_weight",
            "s_primitive_visible",
            "s_primitive_clipped",
            "s_primitive_recovery",
            "s_primitive_corner_count",
            "s_primitive_lod_transition",
            "s_primitive_lod_suppressed",
            "s_primitive_lod_texture_downgraded",
            "s_primitive_buckets",
            "s_primitive_depth",
            "s_primitive_slots",
            "s_terrain_queue_merge_spans",
            "s_terrain_queue_merge_ids",
            "s_actor_queue_merge_ids",
            "s_actor_slots",
            "s_actor_texture_slots",
            "s_actor_gouraud",
            "s_actor_gouraud_addresses",
            "s_bob_terrain_template_valid",
        )
        self.assertIn("\n#define DEMO_CPU_WORK_CACHE\n", DEMO)
        self.assertNotIn("#define DEMO_CPU_WORK_CACHE __attribute__", DEMO)
        for symbol in symbols:
            declaration = self._declaration(DEMO, symbol)
            self.assertIn("DEMO_CPU_WORK_CACHE", declaration, symbol)
            self.assertNotIn("lwram", declaration, symbol)

    def test_scene_admission_scratch_is_phase_owned(self):
        self.assertIn("sm64_saturn_scene_admission_scratch_t", ADMISSION)
        self.assertIn("sm64_saturn_scene_admit_with_scratch", ADMISSION)
        self.assertNotIn("SCENE_ADMISSION_WORK_CACHE", ADMISSION)
        self.assertIn("sm64_saturn_scene_admit_with_scratch(", DEMO)
        self.assertIn("s_terrain_master_commands[0][0]", DEMO)


if __name__ == "__main__":
    unittest.main()
