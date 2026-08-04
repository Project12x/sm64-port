#!/usr/bin/env python3
"""Mutation checks for sourceboot's persistent post-BIOS boot trace.

The break protected here is losing the last target-readable boundary when the
post-BIOS failure stalls before a debugger can inspect live control flow.
These checks operate on the real source translation unit and mutate it
in-memory; they deliberately do not build or launch the target.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SOURCEBOOT_C = REPO_ROOT / "src" / "port" / "saturn" / "sourceboot" / "main.c"


def extract_c_function(text: str, name: str) -> str:
    match = re.search(rf"\b{name}\s*\([^)]*\)\s*\{{", text)
    if match is None:
        raise AssertionError(f"function {name} not found")
    depth = 0
    for index in range(match.end() - 1, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[match.end() : index]
    raise AssertionError(f"function {name} is not terminated")


TRACE_STAGES = (
    "SOURCEBOOT_BOOT_TRACE_STAGE_BOOTSTRAP_BEFORE",
    "SOURCEBOOT_BOOT_TRACE_STAGE_BOOTSTRAP_RETIRED",
    "SOURCEBOOT_BOOT_TRACE_STAGE_THREAD5_BEFORE",
    "SOURCEBOOT_BOOT_TRACE_STAGE_THREAD5_AFTER",
    "SOURCEBOOT_BOOT_TRACE_STAGE_STALE_WAIT_BEFORE",
    "SOURCEBOOT_BOOT_TRACE_STAGE_STALE_WAIT_AFTER",
    "SOURCEBOOT_BOOT_TRACE_STAGE_SOURCE_TICK_BEFORE",
    "SOURCEBOOT_BOOT_TRACE_STAGE_SOURCE_TICK_AFTER",
    "SOURCEBOOT_BOOT_TRACE_STAGE_VDP1_RENDER_BEFORE",
    "SOURCEBOOT_BOOT_TRACE_STAGE_VDP1_RENDER_AFTER",
    "SOURCEBOOT_BOOT_TRACE_STAGE_VDP1_SYNC_BEFORE",
    "SOURCEBOOT_BOOT_TRACE_STAGE_VDP1_SYNC_AFTER",
    "SOURCEBOOT_BOOT_TRACE_STAGE_VDP2_COMMIT_BEFORE",
    "SOURCEBOOT_BOOT_TRACE_STAGE_VDP2_COMMIT_AFTER",
)


def assert_boot_trace_contract(text: str) -> None:
    if "#define SOURCEBOOT_BOOT_TRACE_MAGIC" not in text:
        raise AssertionError("boot trace must publish a magic word")
    if "#define SOURCEBOOT_BOOT_TRACE_VERSION" not in text:
        raise AssertionError("boot trace must publish a version word")
    if re.search(
        r"volatile\s+sm64_saturn_sourceboot_boot_trace_t\s+"
        r"sourceboot_boot_trace\s*;",
        text,
    ) is None:
        raise AssertionError("boot trace must be an exported volatile RAM global")

    trace_type = re.search(
        r"typedef\s+struct\s*\{(?P<body>.*?)\}\s*"
        r"sm64_saturn_sourceboot_boot_trace_t\s*;",
        text,
        re.DOTALL,
    )
    if trace_type is None:
        raise AssertionError("boot trace record type is missing")
    for field in (
        "magic",
        "version",
        "stage",
        "stage_id",
        "observed_vblank_generation",
        "scheduler_credit",
        "vdp1_presentation_generation",
        "vdp2_presentation_generation",
    ):
        if re.search(rf"\buint32_t\s+{field}\s*;", trace_type.group("body")) is None:
            raise AssertionError(f"boot trace record must contain {field}")

    writer = extract_c_function(text, "sourceboot_boot_trace_write")
    if "sourceboot_boot_trace.stage++;" not in writer:
        raise AssertionError("boot trace stage sequence must advance monotonically")
    for forbidden in ("vdp1_sync", "vdp2_sync", "malloc", "SATURN_DEMO", "BOB"):
        if forbidden in writer:
            raise AssertionError(f"boot trace writer must not perform {forbidden}")

    for stage in TRACE_STAGES:
        if stage not in text:
            raise AssertionError(f"boot trace stage {stage} is missing")

    main = extract_c_function(text, "main")
    bootstrap_before = main.index("SOURCEBOOT_BOOT_TRACE_STAGE_BOOTSTRAP_BEFORE")
    bootstrap_begin = main.index("sm64_saturn_vdp2_frame_begin(", bootstrap_before)
    bootstrap_wait = main.index("vdp2_sync_wait();", bootstrap_begin)
    bootstrap_after = main.index("SOURCEBOOT_BOOT_TRACE_STAGE_BOOTSTRAP_RETIRED")
    if not bootstrap_before < bootstrap_begin < bootstrap_wait < bootstrap_after:
        raise AssertionError("bootstrap trace must bracket VDP2 retirement")

    thread_before = main.index("SOURCEBOOT_BOOT_TRACE_STAGE_THREAD5_BEFORE")
    thread_handoff = main.index("thread5_game_loop(NULL);")
    thread_after = main.index("SOURCEBOOT_BOOT_TRACE_STAGE_THREAD5_AFTER")
    if not thread_before < thread_handoff < thread_after:
        raise AssertionError("thread5 trace must bracket the handoff")

    stale_before = main.index("SOURCEBOOT_BOOT_TRACE_STAGE_STALE_WAIT_BEFORE")
    stale_wait = main.index("sm64_saturn_source_runtime_wait_vblank();", stale_before)
    stale_after = main.index("SOURCEBOOT_BOOT_TRACE_STAGE_STALE_WAIT_AFTER", stale_wait)
    if not stale_before < stale_wait < stale_after:
        raise AssertionError("stale trace must bracket the VBlank wait")

    source_before = main.index("SOURCEBOOT_BOOT_TRACE_STAGE_SOURCE_TICK_BEFORE")
    source_tick = main.index("sourceboot_run_source_tick();", source_before)
    source_after = main.index("SOURCEBOOT_BOOT_TRACE_STAGE_SOURCE_TICK_AFTER", source_tick)
    if not source_before < source_tick < source_after:
        raise AssertionError("source tick trace must bracket the source tick")

    present = extract_c_function(text, "sourceboot_present_generation")
    for operation, before, after in (
        (
            "vdp1_sync_render();",
            "SOURCEBOOT_BOOT_TRACE_STAGE_VDP1_RENDER_BEFORE",
            "SOURCEBOOT_BOOT_TRACE_STAGE_VDP1_RENDER_AFTER",
        ),
        (
            "vdp1_sync();",
            "SOURCEBOOT_BOOT_TRACE_STAGE_VDP1_SYNC_BEFORE",
            "SOURCEBOOT_BOOT_TRACE_STAGE_VDP1_SYNC_AFTER",
        ),
        (
            "sm64_saturn_vdp2_frame_commit(",
            "SOURCEBOOT_BOOT_TRACE_STAGE_VDP2_COMMIT_BEFORE",
            "SOURCEBOOT_BOOT_TRACE_STAGE_VDP2_COMMIT_AFTER",
        ),
    ):
        before_index = present.index(before)
        operation_index = present.index(operation, before_index)
        after_index = present.index(after, operation_index)
        if not before_index < operation_index < after_index:
            raise AssertionError(f"{operation} trace must bracket its boundary")


class SourcebootBootTraceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.source = SOURCEBOOT_C.read_text(encoding="utf-8")

    def test_sourceboot_exports_post_bios_boundary_trace(self) -> None:
        assert_boot_trace_contract(self.source)

    def test_rejects_trace_mutations(self) -> None:
        assert_boot_trace_contract(self.source)

        absent_global = self.source.replace(
            "volatile sm64_saturn_sourceboot_boot_trace_t sourceboot_boot_trace;",
            "static sm64_saturn_sourceboot_boot_trace_t sourceboot_boot_trace;",
            1,
        )
        with self.assertRaisesRegex(AssertionError, "exported volatile"):
            assert_boot_trace_contract(absent_global)

        non_monotonic = self.source.replace(
            "sourceboot_boot_trace.stage++;",
            "sourceboot_boot_trace.stage = 1U;",
            1,
        )
        with self.assertRaisesRegex(AssertionError, "advance monotonically"):
            assert_boot_trace_contract(non_monotonic)

        missing_stale = self.source.replace(
            "SOURCEBOOT_BOOT_TRACE_STAGE_STALE_WAIT_AFTER",
            "SOURCEBOOT_BOOT_TRACE_STAGE_STALE_WAIT_REMOVED",
        )
        with self.assertRaisesRegex(AssertionError, "STALE_WAIT_AFTER"):
            assert_boot_trace_contract(missing_stale)

        writer_sync = self.source.replace(
            "sourceboot_boot_trace.stage++;",
            "sourceboot_boot_trace.stage++;\n    vdp1_sync();",
            1,
        )
        with self.assertRaisesRegex(AssertionError, "must not perform vdp1_sync"):
            assert_boot_trace_contract(writer_sync)


if __name__ == "__main__":
    unittest.main()
