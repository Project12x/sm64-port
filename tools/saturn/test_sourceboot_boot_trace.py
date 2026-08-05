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
    "SOURCEBOOT_BOOT_TRACE_STAGE_USER_INIT_ENTRY",
    "SOURCEBOOT_BOOT_TRACE_STAGE_USER_INIT_CALLBACKS_REGISTERED",
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
        r"sourceboot_boot_trace\s*=\s*\{\s*"
        r"\.magic\s*=\s*SOURCEBOOT_BOOT_TRACE_MAGIC\s*,\s*"
        r"\.version\s*=\s*SOURCEBOOT_BOOT_TRACE_VERSION\s*,\s*"
        r"\}\s*;",
        text,
        re.DOTALL,
    ) is None:
        raise AssertionError(
            "boot trace must be an exported volatile ELF .data record with magic/version"
        )

    trace_type = re.search(
        r"typedef\s+struct\s*\{(?P<body>.*?)\}\s*"
        r"sm64_saturn_sourceboot_boot_trace_t\s*;",
        text,
        re.DOTALL,
    )
    if trace_type is None:
        raise AssertionError("boot trace record type is missing")
    expected_fields = (
        "magic",
        "version",
        "stage",
        "stage_id",
        "observed_vblank_generation",
        "scheduler_credit",
        "vdp1_presentation_generation",
        "vdp2_presentation_generation",
    )
    fields = tuple(re.findall(r"\buint32_t\s+(\w+)\s*;", trace_type.group("body")))
    if fields != expected_fields:
        raise AssertionError("boot trace record must retain its exact eight-word ABI")
    if re.search(
        r"_Static_assert\s*\(\s*sizeof\s*\(\s*"
        r"sm64_saturn_sourceboot_boot_trace_t\s*\)\s*==\s*32U\s*,",
        text,
    ) is None:
        raise AssertionError("boot trace record must assert its 32-byte ABI")

    visible = extract_c_function(text, "sourceboot_boot_trace_visible")
    if "CPU_CACHE_THROUGH |" not in visible or \
            "(uintptr_t)&sourceboot_boot_trace" not in visible:
        raise AssertionError("boot trace must publish through the cache-through alias")

    writer = extract_c_function(text, "sourceboot_boot_trace_write")
    if "sourceboot_boot_trace_visible()" not in writer or "trace->" not in writer:
        raise AssertionError("boot trace writer must use the cache-through record")
    if "sourceboot_boot_trace." in writer:
        raise AssertionError("boot trace writer must not store through cached WRAM")
    if "trace->stage++;" not in writer:
        raise AssertionError("boot trace stage sequence must advance monotonically")
    for forbidden in ("vdp1_sync", "vdp2_sync", "malloc", "SATURN_DEMO", "BOB"):
        if forbidden in writer:
            raise AssertionError(f"boot trace writer must not perform {forbidden}")

    for stage in TRACE_STAGES:
        if stage not in text:
            raise AssertionError(f"boot trace stage {stage} is missing")

    user_init = extract_c_function(text, "user_init")
    entry_trace_call = (
        "sourceboot_boot_trace_write("
        "SOURCEBOOT_BOOT_TRACE_STAGE_USER_INIT_ENTRY,"
    )
    if entry_trace_call not in user_init:
        raise AssertionError("user_init entry trace must use the cache-through writer")
    user_init_entry = user_init.index(entry_trace_call)
    vdp_configuration = user_init.index("vdp2_tvmd_display_res_set(")
    if not user_init_entry < vdp_configuration:
        raise AssertionError("user_init entry trace must precede VDP configuration")
    user_init_callbacks = user_init.index(
        "SOURCEBOOT_BOOT_TRACE_STAGE_USER_INIT_CALLBACKS_REGISTERED"
    )
    vblank_callback = user_init.index("vdp_sync_vblank_out_set(")
    first_intback = user_init.index("smpc_peripheral_intback_issue();")
    if not vblank_callback < user_init_callbacks < first_intback:
        raise AssertionError(
            "user_init callback trace must follow registration before the first INTBACK"
        )

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

    dispatch = extract_c_function(text, "sourceboot_frame_pipeline_dispatch")
    stale_before = dispatch.index("SOURCEBOOT_BOOT_TRACE_STAGE_STALE_WAIT_BEFORE")
    stale_wait = dispatch.index("sm64_saturn_source_runtime_wait_vblank();", stale_before)
    stale_after = dispatch.index("SOURCEBOOT_BOOT_TRACE_STAGE_STALE_WAIT_AFTER", stale_wait)
    if not stale_before < stale_wait < stale_after:
        raise AssertionError("stale trace must bracket the VBlank wait")

    run_tick = extract_c_function(text, "sourceboot_frame_run_sim_tick")
    source_before = run_tick.index("SOURCEBOOT_BOOT_TRACE_STAGE_SOURCE_TICK_BEFORE")
    source_tick = run_tick.index("sourceboot_run_source_tick();", source_before)
    source_after = run_tick.index("SOURCEBOOT_BOOT_TRACE_STAGE_SOURCE_TICK_AFTER", source_tick)
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
            "volatile sm64_saturn_sourceboot_boot_trace_t sourceboot_boot_trace = {",
            "static sm64_saturn_sourceboot_boot_trace_t sourceboot_boot_trace = {",
            1,
        )
        with self.assertRaisesRegex(AssertionError, "exported volatile ELF .data"):
            assert_boot_trace_contract(absent_global)

        bss_trace = self.source.replace(
            " = {\n    .magic = SOURCEBOOT_BOOT_TRACE_MAGIC,\n"
            "    .version = SOURCEBOOT_BOOT_TRACE_VERSION,\n};",
            ";",
            1,
        )
        with self.assertRaisesRegex(AssertionError, "ELF .data"):
            assert_boot_trace_contract(bss_trace)

        non_monotonic = self.source.replace(
            "trace->stage++;",
            "trace->stage = 1U;",
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
            "trace->stage++;",
            "trace->stage++;\n    vdp1_sync();",
            1,
        )
        with self.assertRaisesRegex(AssertionError, "must not perform vdp1_sync"):
            assert_boot_trace_contract(writer_sync)

        cached_alias = self.source.replace(
            "CPU_CACHE_THROUGH |",
            "0U |",
            1,
        )
        with self.assertRaisesRegex(AssertionError, "cache-through alias"):
            assert_boot_trace_contract(cached_alias)

        cached_writer = self.source.replace(
            "sourceboot_boot_trace_visible()",
            "&sourceboot_boot_trace",
            1,
        )
        with self.assertRaisesRegex(AssertionError, "cache-through record"):
            assert_boot_trace_contract(cached_writer)

        late_user_init_trace = self.source.replace(
            "sourceboot_boot_trace_write(SOURCEBOOT_BOOT_TRACE_STAGE_USER_INIT_ENTRY,\n"
            "                                0U);\n    smpc_peripheral_init();\n"
            "    vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE,\n"
            "                              VDP2_TVMD_HORZ_NORMAL_A,\n"
            "                              VDP2_TVMD_VERT_224);",
            "smpc_peripheral_init();\n"
            "    vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE,\n"
            "                              VDP2_TVMD_HORZ_NORMAL_A,\n"
            "                              VDP2_TVMD_VERT_224);\n"
            "    sourceboot_boot_trace_write(SOURCEBOOT_BOOT_TRACE_STAGE_USER_INIT_ENTRY,\n"
            "                                0U);",
            1,
        )
        with self.assertRaisesRegex(AssertionError, "precede VDP configuration"):
            assert_boot_trace_contract(late_user_init_trace)

        cached_user_init_trace = self.source.replace(
            "sourceboot_boot_trace_write(SOURCEBOOT_BOOT_TRACE_STAGE_USER_INIT_ENTRY,",
            "sourceboot_boot_trace_cached_write("
            "SOURCEBOOT_BOOT_TRACE_STAGE_USER_INIT_ENTRY,",
            1,
        )
        with self.assertRaisesRegex(AssertionError, "cache-through writer"):
            assert_boot_trace_contract(cached_user_init_trace)

        resized_record = self.source.replace(
            "_Static_assert(sizeof(sm64_saturn_sourceboot_boot_trace_t) == 32U,",
            "_Static_assert(sizeof(sm64_saturn_sourceboot_boot_trace_t) == 28U,",
            1,
        )
        with self.assertRaisesRegex(AssertionError, "32-byte ABI"):
            assert_boot_trace_contract(resized_record)


if __name__ == "__main__":
    unittest.main()
