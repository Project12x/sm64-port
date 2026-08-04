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


if __name__ == "__main__":
    unittest.main()
