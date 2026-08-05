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


def _balanced_function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"missing function {signature}")
    opening = source.find("{", start)
    if opening < 0:
        raise AssertionError(f"missing function body for {signature}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unclosed function body for {signature}")


class A9OverlapTargetCoherencyTests(unittest.TestCase):
    def test_bulk_lod_storage_is_lwram_but_lifetime_record_is_uncached(self) -> None:
        storage = re.search(
            r"typedef\s+struct\s+demo_lod_storage\s*\{(?P<body>.*?)\}\s*"
            r"demo_lod_storage_t\s*;",
            RENDERER,
            flags=re.DOTALL,
        )
        self.assertIsNotNone(storage)
        body = storage.group("body")
        self.assertRegex(
            body,
            r"uint8_t\s+primitive_tiers\s*\[\s*"
            r"SM64_SATURN_BOB_PRIMITIVE_COUNT\s*\]\s*;",
        )
        self.assertRegex(
            body,
            r"sm64_saturn_render_lod_state_t\s+cluster_lod\s*\[\s*"
            r"SM64_SATURN_BOB_CLUSTER_COUNT\s*\]\s*;",
        )
        self.assertRegex(
            RENDERER,
            r"s_lod_storage\s+__attribute__\(\(section\(\"\.lwram_bss\"\)\)\)\s*;",
        )
        self.assertRegex(
            RENDERER, r"s_lod_lifetime\s+DEMO_CROSS_CPU_SHARED\s*;"
        )
        self.assertNotRegex(
            RENDERER,
            r"s_lod_storage\s+(?:DEMO_CROSS_CPU_SHARED|"
            r"__attribute__\(\(section\(\"\.uncached\"\)\)\))",
        )
        self.assertNotRegex(RENDERER, r"static[^;]+\bs_primitive_lod_tier\b")
        self.assertNotRegex(RENDERER, r"static[^;]+\bs_render_cluster_lod\b")
        self.assertIn(
            '#define DEMO_CROSS_CPU_SHARED __attribute__((section(".uncached")))',
            RENDERER,
        )
        self.assertIn(".uncached (0x20000000 | ___bss_end)", LINKER)
        self.assertIn("*(.uncached)", LINKER)

    def test_master_and_worker_share_one_p2_lod_storage_path(self) -> None:
        accessor = _balanced_function_body(
            RENDERER, "static demo_lod_storage_t *demo_lod_storage_cache_through"
        )
        initializer = _balanced_function_body(
            RENDERER, "void sm64_saturn_demo_render_init(void)"
        )
        admission = _balanced_function_body(
            RENDERER, "static void demo_prepare_render_work_order"
        )
        worker_select = _balanced_function_body(
            RENDERER, "static uint8_t demo_lod_select"
        )
        worker = _balanced_function_body(
            RENDERER, "static void demo_classify_exact"
        )

        self.assertEqual(
            accessor.count(
                "sm64_saturn_dual_frame_cache_through(&s_lod_storage)"
            ),
            1,
        )
        self.assertEqual(RENDERER.count("&s_lod_storage"), 1)
        self.assertNotIn("return &s_lod_storage", accessor)
        self.assertIn("demo_lod_storage_cache_through()", initializer)
        self.assertIn("lod_storage->primitive_tiers", initializer)
        self.assertIn("lod_storage->cluster_lod", initializer)
        self.assertIn("demo_lod_storage_cache_through()", admission)
        self.assertIn("lod_storage->cluster_lod", admission)
        self.assertIn("sm64_saturn_lod_lifetime_select(", worker_select)
        self.assertIn("demo_lod_select(", worker)
        self.assertNotRegex(RENDERER, r"s_lod_storage\s*\.")

    def test_linker_checks_bounds_before_subtracting_memory_margins(self) -> None:
        contracts = (
            "ASSERT (___end <= ORIGIN (ram) + LENGTH (ram)",
            "ASSERT (ORIGIN (ram) + LENGTH (ram) - ___end >= "
            "__sourceboot_required_hwram_margin",
            "ASSERT (__lwram_camera_capture_end <= "
            "ORIGIN (lwram) + LENGTH (lwram)",
            "ASSERT (ORIGIN (lwram) + LENGTH (lwram) - "
            "__lwram_camera_capture_end >= __sourceboot_required_lwram_margin",
        )
        for contract in contracts:
            self.assertIn(contract, LINKER)
        hwram_bound, hwram_margin, lwram_bound, lwram_margin = (
            LINKER.index(contract) for contract in contracts
        )
        self.assertLess(hwram_bound, hwram_margin)
        self.assertLess(lwram_bound, lwram_margin)

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
