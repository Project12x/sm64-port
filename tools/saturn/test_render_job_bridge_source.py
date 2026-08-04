"""A5.5 must not register a second CPU-DUAL polling callback before cutover."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
QUEUE = ROOT / "src/port/saturn/gfx/saturn_render_job_queue.c"


class RenderJobBridgeSourceTests(unittest.TestCase):
    def test_source_only_bridge_does_not_register_cpu_dual_callback(self):
        source = QUEUE.read_text(encoding="utf-8")
        self.assertNotIn("cpu_dual_slave_set", source)
        self.assertNotIn("cpu_dual_slave_notify", source)
        self.assertIn("sm64_saturn_render_job_queue_slave_attach", source)
        self.assertIn("sm64_saturn_render_job_queue_slave_notify", source)


if __name__ == "__main__":
    unittest.main()
