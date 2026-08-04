"""Contract checks for the generated compact render-cluster streams."""
from __future__ import annotations

import json
import unittest
from pathlib import Path

from emit_bob_scene import emit


class RenderClusterGenerationTest(unittest.TestCase):
    def test_generated_scene_declares_compact_lod_position_streams(self) -> None:
        root = Path(__file__).resolve().parents[2]
        generated = root / "build/saturn/sourceboot/generated"
        mesh = json.loads((generated / "bob_area1_compiled.json").read_text())
        manifest = json.loads((generated / "bob_tiles_manifest.json").read_text())
        bsp = json.loads((generated / "bob_area1_bsp_report.json").read_text())
        first = emit(mesh, manifest, bsp)
        second = emit(mesh, manifest, bsp)

        self.assertEqual(first, second)
        self.assertIn("SM64_SATURN_BOB_LOD_POSITION_REF_COUNT", first)
        self.assertIn("sm64_saturn_bob_lod_position_refs", first)
        self.assertIn("SM64_SATURN_BOB_CLUSTER_COUNT", first)


if __name__ == "__main__":
    unittest.main(verbosity=2)
