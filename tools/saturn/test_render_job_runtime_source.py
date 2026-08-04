"""A5.6 runtime must read queue lifecycle words through P2 on SH-2."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
RUNTIME = ROOT / "src/port/saturn/gfx/saturn_render_job_runtime.c"
QUEUE = ROOT / "src/port/saturn/gfx/saturn_render_job_queue.c"


class RenderJobRuntimeSourceTests(unittest.TestCase):
    def test_slave_poll_uses_cache_through_generation_accessor(self):
        source = RUNTIME.read_text(encoding="utf-8")
        self.assertIn("sm64_saturn_render_job_queue_generation", source)
        self.assertNotIn("s_runtime.queue->generation", source)

    def test_runtime_never_reads_job_descriptors_through_cached_owner(self):
        source = RUNTIME.read_text(encoding="utf-8")
        self.assertNotIn("s_runtime.queue->jobs", source)
        self.assertIn("sm64_saturn_render_job_queue_claimed_job", source)

    def test_generation_accessor_selects_cache_through_queue(self):
        source = QUEUE.read_text(encoding="utf-8")
        self.assertIn("sm64_saturn_render_job_queue_generation(", source)
        start = source.index("sm64_saturn_render_job_queue_generation(")
        body = source[start:source.index("\n}", start) + 2]
        self.assertIn("sm64_saturn_render_job_queue_cache_through", body)
        self.assertIn("queue->generation", body)

    def test_queue_imports_target_cache_through_definition(self):
        source = QUEUE.read_text(encoding="utf-8")
        self.assertIn(
            '#if defined(__sh__)\n#include <cpu/cache.h>\n#endif', source
        )

    def test_retirement_marker_is_published_after_telemetry(self):
        source = RUNTIME.read_text(encoding="utf-8")
        start = source.index("static void render_job_slave_entry(void)")
        body = source[start:source.index("\n}", start) + 2]
        self.assertLess(
            body.index("telemetry_retire(generation, notified)"),
            body.index("s_runtime.retired_sequence = notified"),
        )

        host_start = source.index("#if !defined(__sh__)")
        host_body = source[host_start:source.index("#endif", host_start)]
        self.assertLess(
            host_body.index("telemetry_retire(generation, s_runtime.notify_sequence)"),
            host_body.index("s_runtime.retired_sequence = s_runtime.notify_sequence"),
        )


if __name__ == "__main__":
    unittest.main()
