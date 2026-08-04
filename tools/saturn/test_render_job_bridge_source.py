"""A5.5 must not register a second CPU-DUAL polling callback before cutover."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCES = (
    ROOT / "src/port/saturn/gfx/saturn_render_job_queue.c",
    ROOT / "src/port/saturn/gfx/saturn_render_job_bridge.c",
)


class RenderJobBridgeSourceTests(unittest.TestCase):
    def test_source_only_bridge_cannot_activate_cpu_dual(self):
        source = "\n".join(path.read_text(encoding="utf-8") for path in SOURCES)
        self.assertNotIn("cpu_dual_slave_set", source)
        self.assertNotIn("cpu_dual_slave_notify", source)
        self.assertIn("sm64_saturn_render_job_queue_source_arm", source)
        self.assertIn("sm64_saturn_render_job_queue_source_armed", source)


if __name__ == "__main__":
    unittest.main()
