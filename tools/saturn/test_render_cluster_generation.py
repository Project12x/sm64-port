"""Contract checks for the generated compact render-cluster streams."""
from __future__ import annotations

import json
import unittest
from pathlib import Path

from emit_bob_scene import build_render_clusters, emit
from emit_bob_bsp_fragments import (
    build_render_clusters as build_fragment_render_clusters,
    emit as emit_fragments,
)


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
        self.assertIn("sm64_saturn_bob_render_clusters", first)

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
        self.assertIn("sm64_saturn_bob_fragment_render_clusters", first)

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

    def test_renderer_uses_mario_selected_tier_reference_stream(self) -> None:
        root = Path(__file__).resolve().parents[2]
        source = (root / "src/port/saturn/gfx/saturn_demo_render.c").read_text()

        self.assertIn("sm64_mario_render_cluster_lod_vertex_offsets", source)
        self.assertIn("sm64_mario_render_cluster_lod_vertex_list", source)
        self.assertIn("transform_ref_count", source)
        self.assertIn("s_actor_vertex_owner", source)

    def test_generated_cluster_metadata_is_tight_partitioned_and_mandatory(self) -> None:
        root = Path(__file__).resolve().parents[2]
        generated = root / "build/saturn/sourceboot/generated"
        mesh = json.loads((generated / "bob_area1_compiled.json").read_text())
        manifest = json.loads((generated / "bob_tiles_manifest.json").read_text())
        scene = json.loads((generated / "bob_bsp_fragments_scene.json").read_text())
        cluster_sets = (
            (build_render_clusters(mesh, manifest), False),
            (build_fragment_render_clusters(mesh, scene), True),
        )
        for clusters, fragment_mode in cluster_sets:
            self.assertTrue(clusters)
            near_refs = 0
            far_refs = 0
            for cluster in clusters:
                self.assertEqual(cluster["primitive_count"], 1)
                self.assertLessEqual(cluster["bounds"]["min"][0],
                                     cluster["bounds"]["max"][0])
                self.assertIsInstance(cluster["material_partition"], int)
                material_index = (cluster["source_ordinal"] if fragment_mode
                                  else cluster["primitive_first"])
                self.assertEqual(cluster["material_partition"],
                                 int(mesh["primitives"][material_index]["material"]))
                self.assertEqual(len(cluster["position_refs"]), 3)
                self.assertEqual(cluster["position_refs"][0],
                                 sorted(set(cluster["position_refs"][0])))
                for point in cluster["position_values"]:
                    for axis in range(3):
                        self.assertLessEqual(cluster["bounds"]["min"][axis],
                                             point[axis])
                        self.assertGreaterEqual(cluster["bounds"]["max"][axis],
                                                point[axis])
                if cluster["mandatory"]:
                    self.assertTrue(cluster["position_refs"][2])
                near_refs += len(cluster["position_refs"][0])
                far_refs += len(cluster["position_refs"][2])
            self.assertLess(far_refs, near_refs)


if __name__ == "__main__":
    unittest.main(verbosity=2)
