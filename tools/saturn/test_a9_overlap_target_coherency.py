#!/usr/bin/env python3
"""Target-aware ownership contracts for A9A overlap state."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
RENDERER = (ROOT / "src/port/saturn/gfx/saturn_demo_render.c").read_text(
    encoding="utf-8"
)
RUNTIME = (ROOT / "src/port/saturn/gfx/saturn_render_job_runtime.c").read_text(
    encoding="utf-8"
)
SOURCEBOOT = (ROOT / "src/port/saturn/sourceboot/main.c").read_text(
    encoding="utf-8"
)
LINKER = (ROOT / "src/port/saturn/sourceboot/sourceboot-cart.x").read_text(
    encoding="utf-8"
)


class A9OverlapTargetCoherencyTests(unittest.TestCase):
    def test_every_worker_visible_lod_lifetime_object_is_uncached(self) -> None:
        declarations = (
            r"s_primitive_lod_tier\s*\[[^;]+?\]\s*DEMO_CROSS_CPU_SHARED\s*;",
            r"s_render_cluster_lod\s*\[[^;]+?\]\s*DEMO_CROSS_CPU_SHARED\s*;",
            r"s_lod_lifetime\s+DEMO_CROSS_CPU_SHARED\s*;",
        )
        for declaration in declarations:
            self.assertRegex(RENDERER, declaration)
        self.assertIn(
            '#define DEMO_CROSS_CPU_SHARED __attribute__((section(".uncached")))',
            RENDERER,
        )
        self.assertIn(".uncached (0x20000000 | ___bss_end)", LINKER)
        self.assertIn("*(.uncached)", LINKER)

    def test_runtime_marker_clock_and_phase_record_are_uncached(self) -> None:
        self.assertRegex(
            SOURCEBOOT,
            r"sourceboot_vblank_out_count\s+__uncached\s*;",
        )
        self.assertRegex(
            SOURCEBOOT,
            r"sourceboot_render_overlap_phase\s+__uncached\s*;",
        )
        self.assertRegex(
            SOURCEBOOT,
            r"sourceboot_render_overlap_event_ok\s+__uncached\s*;",
        )
        self.assertIn(
            "sm64_saturn_render_job_runtime_observe_markers(", SOURCEBOOT
        )
        self.assertNotIn("sm64_saturn_demo_render_observe_lifecycle(", SOURCEBOOT)

    def test_exact_phase_hooks_are_at_runtime_release_markers(self) -> None:
        self.assertIn("runtime_publish_notify_marker(", RUNTIME)
        self.assertIn("runtime_publish_retirement_marker(", RUNTIME)
        self.assertIn("s_runtime.notify_sequence = sequence;", RUNTIME)
        self.assertIn("s_runtime.retired_sequence = sequence;", RUNTIME)
        self.assertRegex(
            RUNTIME,
            r"runtime_observe_marker\("
            r"SM64_SATURN_RENDER_JOB_RUNTIME_MARKER_NOTIFIED,[^;]+;\s*"
            r"#endif\s*#if defined\(__sh__\)\s*cpu_dual_slave_notify\(\);",
        )
        self.assertRegex(
            RUNTIME,
            r"runtime_observe_marker\("
            r"SM64_SATURN_RENDER_JOB_RUNTIME_MARKER_RETIRED,[^;]+;\s*"
            r"#endif\s*#if !defined\("
            r"SM64_SATURN_RENDER_JOB_RUNTIME_TEST_LATE_RETIRE_MARKER\)\s*"
            r"s_runtime\.retired_sequence = sequence;",
        )
        self.assertIn(
            "SM64_SATURN_RENDER_JOB_RUNTIME_TEST_LATE_NOTIFY_MARKER", RUNTIME
        )
        self.assertIn(
            "SM64_SATURN_RENDER_JOB_RUNTIME_TEST_LATE_RETIRE_MARKER", RUNTIME
        )


if __name__ == "__main__":
    unittest.main()
