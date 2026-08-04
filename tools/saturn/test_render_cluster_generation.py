"""Contract checks for the generated compact render-cluster streams."""
from __future__ import annotations

import json
import unittest
from pathlib import Path

from emit_bob_scene import emit
from emit_bob_bsp_fragments import emit as emit_fragments


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

    def test_fragment_bank_declares_compact_lod_position_streams(self) -> None:
        root = Path(__file__).resolve().parents[2]
        generated = root / "build/saturn/sourceboot/generated"
        mesh = json.loads((generated / "bob_area1_compiled.json").read_text())
        scene = json.loads((generated / "bob_bsp_fragments_scene.json").read_text())
        first = emit_fragments(mesh, scene)
        second = emit_fragments(mesh, scene)

        self.assertEqual(first, second)
        self.assertIn("SM64_SATURN_BOB_FRAGMENT_LOD_POSITION_REF_COUNT", first)
        self.assertIn("sm64_saturn_bob_fragment_lod_position_refs", first)

    def test_renderer_marks_only_the_selected_compact_span(self) -> None:
        root = Path(__file__).resolve().parents[2]
        source = (root / "src/port/saturn/gfx/saturn_demo_render.c").read_text()
        begin = source.index("static uint16_t demo_build_visible_position_set")
        end = source.index("static uint16_t demo_choose_work_split", begin)
        marking = source[begin:end]

        self.assertIn("sm64_saturn_visible_position_set_mark_refs", marking)
        self.assertIn("sm64_saturn_bob_lod_position_ref_offsets", marking)
        self.assertNotIn("sm64_saturn_visible_position_set_mark_primitive", marking)
        self.assertIn("demo_pretransform_primitive_admitted", source)


if __name__ == "__main__":
    unittest.main(verbosity=2)
