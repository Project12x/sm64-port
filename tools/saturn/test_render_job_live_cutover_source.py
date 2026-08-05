"""Static contract for A5's one-owner live queue cutover.

The renderer must not keep the fixed SlaveDriver range dispatcher on its
accepted frame path once the descriptor-owned queue owns CPU-DUAL.
"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
RENDERER = ROOT / "src/port/saturn/gfx/saturn_demo_render.c"


def function_body(source: str, name: str) -> str:
    match = re.search(r"\b%s\s*\([\s\S]*?\)\s*\{" % re.escape(name), source)
    if match is None:
        raise AssertionError("missing function %s" % name)
    depth = 1
    index = match.end()
    while depth and index < len(source):
        depth += (source[index] == "{") - (source[index] == "}")
        index += 1
    if depth:
        raise AssertionError("unterminated function %s" % name)
    return source[match.start():index]


class RenderJobLiveCutoverSourceTests(unittest.TestCase):
    def test_default_frame_path_uses_descriptor_queue_not_fixed_worker(self):
        source = RENDERER.read_text(encoding="utf-8")
        prepare = function_body(source, "demo_render_prepare_publish")
        finalize = function_body(source, "demo_render_finalize")
        accepted_lifecycle = prepare + finalize

        self.assertIn('#include "saturn_render_job_bridge.h"', source)
        self.assertIn("sm64_saturn_render_job_graph_publish", prepare)
        self.assertIn("sm64_saturn_render_job_runtime_drain_master", source)
        self.assertIn("sm64_saturn_render_job_queue_all_terminal", finalize)
        self.assertIn("sm64_saturn_render_job_runtime_activate", source)
        self.assertNotIn("sm64_saturn_terrain_worker_run", accepted_lifecycle)
        self.assertNotIn("demo_dispatch_mario_transform", accepted_lifecycle)
        self.assertNotIn("sm64_saturn_demo_render_frame(", source)

    def test_master_only_terrain_merge_scratch_stays_out_of_hwram(self):
        source = RENDERER.read_text(encoding="utf-8")
        for symbol in ("s_terrain_emit_refs", "s_terrain_emit_scratch"):
            declaration = re.search(
                r"static\s+sm64_saturn_terrain_emit_ref_t\s+"
                + symbol
                + r"\s*\[[^;]+;",
                source,
                re.MULTILINE,
            )
            self.assertIsNotNone(declaration, "missing " + symbol)
            self.assertIn(
                '__attribute__((section(".lwram_bss")))', declaration.group(0)
            )


if __name__ == "__main__":
    unittest.main()
