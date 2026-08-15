#!/usr/bin/env python3
"""Pin the dual-SH2 renderer work-storage placement: ALL hot scratch in HWRAM.

Sprint 2 T2.2 un-split: T1's attribution
(docs/saturn/evidence/reports/sprint2-t1-hwram-attribution.md) proved the
stage-1b three-way split (49370e31) was link-pressure relief, not design;
the peak-gated capacity shrinks (cmdt 2048->1664, GFX_POOL_SIZE 6400->4096,
libyaul _private_pool 0xA000->0x4000 = 67,584 B) fund the full 54,080 B
return.  T2.0's reference sweep corroborates the shape (L2: neither
SlaveDriver nor Z-Treme places any per-frame working set in LWRAM).  The
pinned policy:

  - Terrain/primitive scratch: DEMO_CPU_WORK_CACHE stays an EMPTY macro
    (32-bit HWRAM .bss).  The 16-bit LWRAM eviction (ec7b992a/91f02ffd)
    was the mechanism of the 5.29 FPS -> ~1 FPS collapse on this
    memory-bound multi-pass loop and must not silently return.
  - Actor-path-only scratch: DEMO_ACTOR_WORK_CACHE is likewise an EMPTY
    macro (32-bit HWRAM .bss).  It remains a distinct placement class so
    the two sets stay independently steerable, but re-evicting it requires
    editing the macro AND this contract.
  - The 43,776 B hot workarea (s_bob_hot_workarea) lives in HWRAM .bss,
    16-byte aligned, with no section attribute.

_sourceboot_fast3d (44,616 B, main.c) deliberately stays in LWRAM -- the
reclamation arithmetic does not close for it; recorded in the T2.2
evidence as the next rung (T2.0 L3 staging-window lever).

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

    def test_actor_work_arrays_are_hwram_owned(self):
        # Actor-path-only scratch: consumed exclusively by the actor lanes
        # (demo_actor_queue_assemble_done, demo_reserve_mario_gouraud,
        # demo_emit_mario, demo_emit_mario_range), never by the terrain
        # path.  T2.2 returns it to HWRAM: the macro must stay EMPTY and no
        # declaration may carry an lwram placement.
        symbols = (
            "s_actor_queue_merge_ids",
            "s_actor_slots",
            "s_actor_texture_slots",
            "s_actor_gouraud",
            "s_actor_gouraud_addresses",
        )
        self.assertIn("\n#define DEMO_ACTOR_WORK_CACHE\n", DEMO)
        self.assertNotIn("#define DEMO_ACTOR_WORK_CACHE __attribute__", DEMO)
        for symbol in symbols:
            declaration = self._declaration(DEMO, symbol)
            self.assertIn("DEMO_ACTOR_WORK_CACHE", declaration, symbol)
            self.assertNotIn("DEMO_CPU_WORK_CACHE", declaration, symbol)
            self.assertNotIn("lwram", declaration, symbol)

    def test_hot_workarea_is_hwram_owned(self):
        # The 43,776 B promoted-geometry workarea is the renderer's hottest
        # per-frame read bank; T2.2 returns it to HWRAM .bss with its
        # 16-byte alignment intact and no section attribute.
        declaration = self._declaration(DEMO, "s_bob_hot_workarea")
        self.assertIn("aligned(16)", declaration)
        self.assertNotIn("section(", declaration)
        self.assertNotIn("lwram", declaration)
        self.assertNotIn("DEMO_CPU_WORK_CACHE", declaration)
        self.assertNotIn("DEMO_ACTOR_WORK_CACHE", declaration)

    def test_scene_admission_scratch_is_phase_owned(self):
        self.assertIn("sm64_saturn_scene_admission_scratch_t", ADMISSION)
        self.assertIn("sm64_saturn_scene_admit_with_scratch", ADMISSION)
        self.assertNotIn("SCENE_ADMISSION_WORK_CACHE", ADMISSION)
        self.assertIn("sm64_saturn_scene_admit_with_scratch(", DEMO)
        self.assertIn("s_terrain_master_commands[0][0]", DEMO)


if __name__ == "__main__":
    unittest.main()
