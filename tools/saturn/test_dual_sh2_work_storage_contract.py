#!/usr/bin/env python3
"""Pin the dual-SH2 renderer three-way work-storage split.

Sprint 1 Task 10 stage 1b: the stage-1 link smoke
(docs/saturn/evidence/reports/sprint1-stage1-link-smoke.md) measured
49,648 B of HWRAM relief required at pool 208 -- the .bss growth is
committed and always-on, not feature-gated as the Task 8 (3ad7cb5c)
full-HWRAM revert assumed.  The pinned policy is therefore a split:

  - Terrain/primitive scratch: DEMO_CPU_WORK_CACHE stays an EMPTY macro
    (32-bit HWRAM .bss).  The 16-bit LWRAM eviction (ec7b992a/91f02ffd)
    was the mechanism of the 5.29 FPS -> ~1 FPS collapse on this
    memory-bound multi-pass loop and must not silently return.
  - Actor-path-only scratch: DEMO_ACTOR_WORK_CACHE places the five
    actor-lane arrays (~10,304 B) in LWRAM .lwram_bss.
  - The 43,776 B hot workarea (s_bob_hot_workarea) lives in LWRAM
    .lwram_bss, 16-byte aligned.

Moving any symbol between these sets requires editing the macros AND this
contract explicitly, with the fallback ladder in CHANGELOG.md.
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

    def test_terrain_cpu_work_arrays_are_hwram_owned(self):
        # These arrays are either master-only assembly state or disjoint
        # lane-owned classify state.  They are not VDP1/SCU transport banks.
        # They are per-primitive inner-loop operands of the terrain
        # multi-pass loop, so they stay in 32-bit HWRAM via the (empty)
        # shared placement macro.
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
            "s_bob_terrain_template_valid",
        )
        self.assertIn("\n#define DEMO_CPU_WORK_CACHE\n", DEMO)
        self.assertNotIn("#define DEMO_CPU_WORK_CACHE __attribute__", DEMO)
        for symbol in symbols:
            declaration = self._declaration(DEMO, symbol)
            self.assertIn("DEMO_CPU_WORK_CACHE", declaration, symbol)
            self.assertNotIn("DEMO_ACTOR_WORK_CACHE", declaration, symbol)
            self.assertNotIn("lwram", declaration, symbol)

    def test_actor_work_arrays_are_lwram_owned(self):
        # Actor-path-only scratch: consumed exclusively by the actor lanes
        # (demo_actor_queue_assemble_done, demo_reserve_mario_gouraud,
        # demo_emit_mario, demo_emit_mario_range), never by the terrain
        # path.  Stage 1b evicts them to LWRAM as part of the 49,648 B
        # HWRAM relief.
        symbols = (
            "s_actor_queue_merge_ids",
            "s_actor_slots",
            "s_actor_texture_slots",
            "s_actor_gouraud",
            "s_actor_gouraud_addresses",
        )
        self.assertIn(
            '#define DEMO_ACTOR_WORK_CACHE '
            '__attribute__((section(".lwram_bss")))', DEMO)
        for symbol in symbols:
            declaration = self._declaration(DEMO, symbol)
            self.assertIn("DEMO_ACTOR_WORK_CACHE", declaration, symbol)

    def test_hot_workarea_is_lwram_owned(self):
        # The 43,776 B promoted-geometry workarea is the largest single
        # recoverable HWRAM block; stage 1b returns it to LWRAM (the
        # 91f02ffd placement) with its 16-byte alignment intact.
        declaration = self._declaration(DEMO, "s_bob_hot_workarea")
        self.assertIn('section(".lwram_bss")', declaration)
        self.assertIn("aligned(16)", declaration)
        self.assertNotIn("DEMO_CPU_WORK_CACHE", declaration)

    def test_scene_admission_scratch_is_phase_owned(self):
        self.assertIn("sm64_saturn_scene_admission_scratch_t", ADMISSION)
        self.assertIn("sm64_saturn_scene_admit_with_scratch", ADMISSION)
        self.assertNotIn("SCENE_ADMISSION_WORK_CACHE", ADMISSION)
        self.assertIn("sm64_saturn_scene_admit_with_scratch(", DEMO)
        self.assertIn("s_terrain_master_commands[0][0]", DEMO)


if __name__ == "__main__":
    unittest.main()
