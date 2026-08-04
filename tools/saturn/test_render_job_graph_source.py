"""Static guard: A5.7 remains a graph foundation, never a partial live bind."""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
RENDERER = ROOT / "src/port/saturn/gfx/saturn_demo_render.c"
GRAPH = ROOT / "src/port/saturn/gfx/saturn_render_job_graph.c"


class RenderJobGraphSourceTests(unittest.TestCase):
    def test_graph_has_dependency_and_merge_guards(self):
        source = GRAPH.read_text(encoding="utf-8")
        self.assertIn("dependency_mask", source)
        self.assertIn("validate_terrain_merge", source)
        self.assertIn("propagate_failures", source)

    def test_live_cutover_stays_unbound_until_payload_conversion(self):
        source = RENDERER.read_text(encoding="utf-8")
        self.assertNotIn('#include "saturn_render_job_graph.h"', source)


if __name__ == "__main__":
    unittest.main()
