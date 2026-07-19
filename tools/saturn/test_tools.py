#!/usr/bin/env python3
"""Small standard-library regression tests for Saturn host-side tools."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))

from asset_classifier import classify_primitives, source_scan  # noqa: E402
from capture_hwtest import has_cd_block_copy_limitation, input_pulse_request  # noqa: E402
from extract_mario_actor import animation_frame_count, animation_rotations, animation_translation, geo_layout_parts  # noqa: E402
from extract_mario_textures import saturn_rgb1555  # noqa: E402
from extract_introface_mesh import goddard_deformation  # noqa: E402
from bake_mario_eye_uv import TILE, bilinear_weights  # noqa: E402
from vdp1_texture import distorted_sprite_weights, downsample_rgb1555, repeated_vertex_weights  # noqa: E402
from inspect_castle_area import inventory  # noqa: E402
from extract_castle_area import extract, flatten  # noqa: E402
from extract_castle_gameplay_config import extract as extract_castle_gameplay_config  # noqa: E402
from extract_castle_geo_root import extract as extract_castle_geo_root  # noqa: E402
from compile_castle_area import compile_opaque, compile_scene  # noqa: E402
from bake_castle_uv import (  # noqa: E402
    adaptive_subdivide_triangle,
    should_subdivide,
    texture_coordinate,
)
from compile_castle_bsp import compile_bsp  # noqa: E402
from static_bsp import (  # noqa: E402
    Polygon as BspPolygon,
    Vertex as BspVertex,
    build as build_bsp,
    painter_order as bsp_painter_order,
    split_polygon as split_bsp_polygon,
)
from plan_castle_camera_coverage import plan  # noqa: E402
from quad_pairing import QuadCandidate, candidates, maximum_weight_matching, pair_triangles  # noqa: E402
from saturn_mesh_ir import compile_mesh_ir, validate_mesh_ir  # noqa: E402
from telemetry_decode import decode  # noqa: E402


class AssetClassifierTests(unittest.TestCase):
    def test_direct_quad_requires_shared_uv_agreement(self) -> None:
        primitives = [
            {"indices": [0, 1, 2], "uvs": [[0, 0], [16, 0], [16, 16]], "material": 7},
            {"indices": [0, 2, 3], "uvs": [[0, 0], [16, 16], [0, 16]], "material": 7},
        ]
        report = classify_primitives(primitives)
        self.assertEqual(report["direct_textured_quad_candidates"], 1)

        primitives[1]["uvs"][1] = [8, 8]
        report = classify_primitives(primitives)
        self.assertEqual(report["direct_textured_quad_candidates"], 0)
        self.assertEqual(report["rejection_reasons"]["shared_uv_mismatch"], 1)

    def test_source_scan_counts_quadrangle_macro(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "display.c").write_text(
                "gsSP1Triangle(0, 1, 2, 0); gsSP2Triangles(0,1,2,0, 0,2,3,0); "
                "gsSP1Quadrangle(0,1,2,3,0); gsSPVertex(v, 4, 0); "
                "gsDPSetTextureImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, tex);\n",
                encoding="utf-8",
            )
            report = source_scan(root)
        self.assertEqual(report["gsSP1Triangle"], 1)
        self.assertEqual(report["gsSP2Triangles"], 1)
        self.assertEqual(report["gsSP1Quadrangle"], 1)
        self.assertEqual(report["static_triangles"], 3)
        self.assertEqual(report["geometry_macro_counts"]["gsSPVertex"], 1)
        self.assertEqual(report["geometry_macro_counts"]["gsDPSetTextureImage"], 1)

    def test_static_planar_policy_does_not_reject_an_edge_on_wall(self) -> None:
        vertices = [(0, 0, 0), (0, 10, 0), (0, 10, 10), (0, 0, 10)]
        faces = [(0, 0, 1, 2), (0, 0, 2, 3)]
        sampled, _ = candidates(vertices, faces)
        planar, _ = candidates(vertices, faces, projection_policy="planar")
        self.assertEqual(sampled, [])
        self.assertEqual(len(planar), 1)

    def test_six_way_representation_counts_are_conservative(self) -> None:
        primitives = [
            {"indices": [0, 1, 2], "material": 1},
            {"indices": [3, 4, 5], "uvs": [[0, 0], [8, 0], [0, 8]], "material": 2},
            {"indices": [6, 7, 8], "uvs": [[0, 0], [8, 0], [0, 8]], "split": True},
            {"indices": [9, 10, 11], "uvs": [[0, 0], [8, 0], [0, 8]], "bake": True},
            {"indices": [12, 13, 14], "uvs": [[0, 0], [8, 0], [0, 8]], "effect": "shadow"},
        ]
        counts = classify_primitives(primitives)["representation_counts"]
        self.assertEqual(counts["direct_untextured_triangle"], 1)
        self.assertEqual(counts["textured_degenerate_triangle"], 1)
        self.assertEqual(counts["split_cropped_surface"], 1)
        self.assertEqual(counts["baked_surface"], 1)
        self.assertEqual(counts["effect_fallback"], 1)

    def test_malformed_uvs_are_rejected_without_raising(self) -> None:
        primitives = [
            {"indices": [0, 1, 2], "uvs": [[0, 0], [8, 0]], "material": 1},
            {"indices": [0, 2, 3], "uvs": [[0, 0], [8, 8], [0, 8]], "material": 1},
        ]
        report = classify_primitives(primitives)
        self.assertEqual(report["direct_textured_quad_candidates"], 0)
        self.assertEqual(report["rejection_reasons"]["uvs_length_mismatch"], 1)


class QuadPairingTests(unittest.TestCase):
    def test_exact_matcher_prefers_cardinality_over_single_edge_quality(self) -> None:
        options = [
            QuadCandidate(0, 1, (0, 1, 2, 3), 1.0, 100),
            QuadCandidate(0, 2, (0, 1, 2, 3), 0.9, 10),
            QuadCandidate(1, 3, (0, 1, 2, 3), 0.9, 10),
        ]
        matched = maximum_weight_matching(options)
        self.assertEqual({tuple(sorted((option.first, option.second))) for option in matched.values()}, {(0, 2), (1, 3)})

    def test_coplanar_pair_becomes_one_ordered_quad(self) -> None:
        vertices = [(0, 0, 0), (10, 0, 0), (10, 10, 0), (0, 10, 0)]
        faces = [(3, 0, 1, 2), (3, 0, 2, 3)]
        primitives, report = pair_triangles(vertices, faces)
        self.assertEqual(report["quad_count"], 1)
        self.assertEqual(report["commands_saved"], 1)
        self.assertEqual(primitives[0].vertices, (0, 1, 2, 3))
        self.assertEqual((primitives[0].first_triangle, primitives[0].second_triangle), (0, 1))

    def test_material_boundary_keeps_original_triangles(self) -> None:
        vertices = [(0, 0, 0), (10, 0, 0), (10, 10, 0), (0, 10, 0)]
        faces = [(3, 0, 1, 2), (4, 0, 2, 3)]
        primitives, report = pair_triangles(vertices, faces)
        self.assertEqual(report["quad_count"], 0)
        self.assertEqual(report["rejection_reasons"], {"material_mismatch": 1})
        self.assertEqual([primitive.vertices for primitive in primitives], [(0, 1, 2, 2), (0, 2, 3, 3)])

    def test_inconsistent_winding_is_rejected(self) -> None:
        vertices = [(0, 0, 0), (10, 0, 0), (10, 10, 0), (0, 10, 0)]
        faces = [(3, 0, 1, 2), (3, 0, 3, 2)]
        _, report = pair_triangles(vertices, faces)
        self.assertEqual(report["quad_count"], 0)
        self.assertEqual(report["rejection_reasons"], {"winding_or_topology": 1})

    def test_pair_must_remain_safe_in_every_deformation_pose(self) -> None:
        vertices = [(0, 0, 0), (10, 0, 0), (10, 10, 0), (0, 10, 0)]
        faces = [(3, 0, 1, 2), (3, 0, 2, 3)]
        folded = [(0, 0, 0), (10, 0, 0), (2, -2, 0), (0, 10, 0)]
        _, neutral_report = pair_triangles(vertices, faces)
        _, animated_report = pair_triangles(
            vertices, faces, deformation_poses=[("folded", folded)]
        )
        self.assertEqual(neutral_report["quad_count"], 1)
        self.assertEqual(animated_report["quad_count"], 0)
        self.assertEqual(animated_report["deformation_pose_names"], ["folded"])
        self.assertTrue(
            any(reason.startswith("pose_") for reason in animated_report["rejection_reasons"])
        )


class MarioActorPoseTests(unittest.TestCase):
    def test_c5_source_frame_count_is_preserved(self) -> None:
        root = TOOLS.parents[1]
        source = (root / "assets/anims/anim_C5.inc.c").read_text(encoding="utf-8")
        self.assertEqual(animation_frame_count(source), 30)

    def test_rgb1555_box_filter_averages_channels_and_alpha(self) -> None:
        width, height, pixels = downsample_rgb1555(
            [0x801F, 0x83E0, 0xFC00, 0xFFFF], 2, 2, 2
        )
        self.assertEqual((width, height), (1, 1))
        self.assertEqual(pixels, [0xC210])

    def test_rgb1555_box_filter_rejects_invalid_scale(self) -> None:
        with self.assertRaisesRegex(ValueError, "scale must be 1, 2, or 4"):
            downsample_rgb1555([0xFFFF], 1, 1, 3)

    def test_n64_rgba16_channels_map_to_saturn_rgb1555_lanes(self) -> None:
        self.assertEqual(saturn_rgb1555(0xF801), 0x801F)  # opaque red
        self.assertEqual(saturn_rgb1555(0x07C1), 0x83E0)  # opaque green
        self.assertEqual(saturn_rgb1555(0x003F), 0xFC00)  # opaque blue
        self.assertEqual(saturn_rgb1555(0xFFFF), 0xFFFF)  # opaque white

    def test_c5_root_rotation_keeps_both_feet_on_the_ground(self) -> None:
        root = TOOLS.parents[1]
        rotations = animation_rotations(
            (root / "assets/anims/anim_C5.inc.c").read_text(encoding="utf-8"), 0
        )
        animation_source = (root / "assets/anims/anim_C5.inc.c").read_text(encoding="utf-8")
        parts = {
            name: matrix
            for name, matrix, _light in geo_layout_parts(
                (root / "actors/mario/geo.inc.c").read_text(encoding="utf-8"), rotations,
                animation_translation(animation_source, 0)
            )
        }
        head_y = parts["mario_cap_on_eyes_front"][10]
        left_foot_y = parts["mario_left_foot"][10]
        right_foot_y = parts["mario_right_foot"][10]
        # mario_geo wraps the body in GEO_SCALE(..., 16384), so these are
        # source-space world coordinates after the real 0.25 actor scale and
        # anim_C5's root translation have both been applied.
        self.assertGreater(head_y, 70.0)
        self.assertGreater(left_foot_y, 5.0)
        self.assertLess(left_foot_y, 20.0)
        self.assertGreater(right_foot_y, 5.0)
        self.assertLess(right_foot_y, 20.0)
        self.assertLess(abs(left_foot_y - right_foot_y), 1.0)

    def test_vdp1_repeated_vertex_tile_corner_order_is_c_b_a(self) -> None:
        self.assertEqual(TILE, 16)
        self.assertEqual(bilinear_weights(0, 0), repeated_vertex_weights(0, 0, 16, 16))
        for x, y in ((0, 0), (15, 0), (0, 15), (15, 15), (8, 8)):
            self.assertAlmostEqual(sum(bilinear_weights(x, y)), 1.0)
        # The formerly masked lower-right half contributes the repeated C
        # corner and therefore contains source texture data.
        self.assertGreater(bilinear_weights(15, 15)[2], 0.9)
        self.assertLessEqual(200 * TILE * TILE * 2, 0x0006BFE0)

    def test_vdp1_native_quad_uses_measured_c_b_a_d_corner_order(self) -> None:
        for x, y in ((0, 0), (15, 0), (0, 15), (15, 15), (8, 8)):
            a, b, c, d = distorted_sprite_weights(x, y, 16, 16)
            self.assertAlmostEqual(a + b + c + d, 1.0)
            repeated = repeated_vertex_weights(x, y, 16, 16)
            self.assertEqual(repeated, (a, b, c + d))
        self.assertGreater(distorted_sprite_weights(0, 0, 16, 16)[2], 0.9)
        self.assertGreater(distorted_sprite_weights(15, 15, 16, 16)[3], 0.9)


class CastleAreaInventoryTests(unittest.TestCase):
    def test_castle_gameplay_config_comes_from_source(self) -> None:
        root = TOOLS.parents[1]
        result = extract_castle_gameplay_config(
            root / "levels/castle_inside/script.c",
            root / "levels/castle_inside/areas/1/collision.inc.c",
            root / "src/game/camera.c",
        )
        self.assertEqual(result["spawn"]["position"], [-1023, 0, 1152])
        self.assertEqual(result["spawn"]["yaw_degrees"], 180)
        # The LevelScript starts Mario at Y=0, while the source collision
        # triangle beneath (-1023, 1152) evaluates to Y=-37.
        self.assertEqual(result["collision"]["floor_height"], -37)
        self.assertEqual(result["camera"]["entrance_base"], [-813, 378, 1103])

    def test_castle_geo_root_preserves_original_layer_selection(self) -> None:
        root = TOOLS.parents[1]
        source = (root / "levels/castle_inside/areas/1/geo.inc.c").read_text(encoding="utf-8")
        body, displays = extract_castle_geo_root(source, "castle_geo_000F30")
        self.assertIn("geo_exec_inside_castle_light", body)
        self.assertEqual(displays, [
            ("LAYER_OPAQUE", "inside_castle_seg7_dl_07028FD0"),
            ("LAYER_ALPHA", "inside_castle_seg7_dl_07029578"),
            ("LAYER_OPAQUE", "inside_castle_seg7_dl_0702A650"),
            ("LAYER_TRANSPARENT_DECAL", "inside_castle_seg7_dl_0702AA10"),
            ("LAYER_ALPHA", "inside_castle_seg7_dl_0702AB20"),
        ])

    def test_area_one_intake_points_at_the_real_lobby_source(self) -> None:
        root = TOOLS.parents[1]
        report = inventory(
            root / "levels/castle_inside/areas/1",
            root / "levels/castle_inside/texture.inc.c",
        )
        self.assertEqual(report["model_file_count"], 26)
        self.assertGreater(report["fast3d"]["static_triangle_upper_bound"], 1000)
        self.assertEqual(report["collision"]["declared_vertices"], report["collision"]["vertices"])
        self.assertIn("LAYER_OPAQUE", report["root_layers"])

    def test_area_one_root_display_lists_flatten_to_source_triangles(self) -> None:
        root = TOOLS.parents[1]
        scene = extract(root / "levels/castle_inside/areas/1")
        # The 2,317-triangle bank includes every room/object unit. The Area 1
        # entry GeoLayout intentionally selects a 619-triangle first-camera
        # slice; keep this gate tied to the actual root, not the full bank.
        self.assertGreater(scene["triangle_count"], 500)
        self.assertEqual(scene["triangle_count"], sum(scene["layers"].values()))
        self.assertGreater(scene["textured_triangle_count"], 0)
        self.assertIn("LAYER_OPAQUE", scene["layers"])
        self.assertEqual({triangle["root_display_list"] for triangle in scene["triangles"]},
                         {root["display_list"] for root in scene["roots"]})

    def test_area_one_compiler_preserves_all_source_roots_and_layers(self) -> None:
        root = TOOLS.parents[1]
        bank = compile_scene(root / "levels/castle_inside/areas/1")
        self.assertEqual(bank["triangle_count"], 619)
        self.assertEqual(len(bank["triangle_roots"]), 619)
        self.assertEqual(len(bank["triangle_layers"]), 619)
        self.assertEqual(bank["roots"], [
            "inside_castle_seg7_dl_07028FD0",
            "inside_castle_seg7_dl_07029578",
            "inside_castle_seg7_dl_0702A650",
            "inside_castle_seg7_dl_0702AA10",
            "inside_castle_seg7_dl_0702AB20",
        ])
        self.assertEqual({index: bank["triangle_roots"].count(index)
                          for index in range(5)}, {0: 472, 1: 32, 2: 105, 3: 8, 4: 2})
        self.assertEqual({layer: bank["triangle_layers"].count(value)
                          for layer, value in bank["layers"].items()}, {
                              "LAYER_OPAQUE": 577,
                              "LAYER_TRANSPARENT_DECAL": 8,
                              "LAYER_ALPHA": 34,
                          })
        self.assertEqual(set(bank["textures"]), {
            "inside_09000000", "inside_09001000", "inside_09003800",
            "inside_09004000", "inside_09005000", "inside_09008000",
            "inside_09008800", "inside_castle_seg7_texture_07000800",
            "inside_castle_seg7_texture_07002000",
        })
        self.assertEqual(bank["primitive_count"], 567)
        self.assertEqual(bank["pairing"]["quad_count"], 52)
        self.assertEqual(bank["pairing"]["commands_saved"], 52)
        self.assertEqual(
            sum(primitive["second_triangle"] is not None
                for primitive in bank["primitives"]),
            52,
        )
        self.assertTrue(all(
            bank["triangle_roots"][primitive["first_triangle"]] == primitive["root"]
            and bank["triangle_layers"][primitive["first_triangle"]] == primitive["layer"]
            for primitive in bank["primitives"]
        ))

    def test_area_one_opaque_compiler_preserves_root_topology(self) -> None:
        root = TOOLS.parents[1]
        bank = compile_opaque(root / "levels/castle_inside/areas/1")
        self.assertEqual(bank["triangle_count"], 577)
        self.assertEqual(bank["vertex_count"], 436)
        self.assertEqual(len(bank["positions"]), bank["vertex_count"])
        self.assertEqual(len(bank["triangles"]), bank["triangle_count"])
        self.assertIn("inside_castle_seg7_dl_07027DE8", bank["source_display_lists"])
        self.assertEqual(len(bank["texture_indices"]), bank["triangle_count"])
        self.assertEqual(len(bank["uv"]), bank["triangle_count"])
        self.assertIn("inside_09000000", bank["textures"])
        self.assertTrue(any(tile and tile.get("width") == 32 for tile in bank["tile_state"]))
        self.assertEqual(bank["texture_state_version"], 2)
        # Castle's 64x32 walls clamp vertically and wrap horizontally.  The
        # earlier string-presence parser incorrectly clamped both axes.
        self.assertEqual(sum(
            tile["clamp_t"] and not tile["clamp_s"] and tile["width"] == 64
            for tile in bank["tile_state"]
        ), 283)

    def test_tmem_bindings_choose_render_tile_not_last_texture_image(self) -> None:
        display_lists = {
            "root": """
                gsDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_16b, 8, 0, G_TX_RENDERTILE, 0,
                            G_TX_CLAMP, 5, 0, G_TX_WRAP, 5, 0),
                gsDPSetTileSize(0, 0, 0, (32 - 1) << G_TEXTURE_IMAGE_FRAC,
                                (32 - 1) << G_TEXTURE_IMAGE_FRAC),
                gsDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_16b, 8, 256, G_TX_RENDERTILE + 1, 0,
                            G_TX_CLAMP, 5, 0, G_TX_CLAMP, 5, 0),
                gsDPSetTileSize(1, 0, 0, 124, 124),
                gsSPTexture(0xFFFF, 0xFFFF, 1, G_TX_RENDERTILE, G_ON),
                gsDPSetTextureImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, texture_primary),
                gsDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0,
                            G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD,
                            G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD),
                gsDPLoadBlock(G_TX_LOADTILE, 0, 0, 32 * 32 - 1, 0),
                gsDPSetTextureImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, texture_secondary),
                gsDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 256, G_TX_LOADTILE, 0,
                            G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD,
                            G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD),
                gsDPLoadBlock(G_TX_LOADTILE, 0, 0, 32 * 32 - 1, 0),
                gsSPVertex(vertices, 3, 0),
                gsSP1Triangle(0, 1, 2, 0),
                gsSPEndDisplayList(),
            """,
        }
        vertices = {"vertices": [(0, 0, 0, 0, 0), (1, 0, 0, 992, 0), (0, 1, 0, 0, 992)]}
        triangles: list[dict[str, object]] = []
        flatten(display_lists, vertices, "root", "LAYER_OPAQUE", triangles)
        self.assertEqual(triangles[0]["texture"], "texture_primary")
        self.assertEqual(triangles[0]["textures"], ["texture_primary", "texture_secondary"])
        self.assertFalse(triangles[0]["tile"]["clamp_s"])
        self.assertTrue(triangles[0]["tile"]["clamp_t"])

    def test_fast3d_texture_coordinate_wrap_clamp_mirror_and_origin(self) -> None:
        raw_40_texels = 40 * 32
        self.assertEqual(texture_coordinate(raw_40_texels, 65536, 0, 32, 5, 0, False, False), 8)
        self.assertEqual(texture_coordinate(raw_40_texels, 65536, 0, 32, 5, 0, True, False), 31)
        self.assertEqual(texture_coordinate(raw_40_texels, 65536, 0, 32, 5, 0, False, True), 23)
        self.assertEqual(texture_coordinate(8 * 32, 65536, 4 * 4, 32, 5, 0, True, False), 4)

    def test_castle_subdivision_uses_source_geometry_not_camera(self) -> None:
        small = [(0, 0, 0), (64, 0, 0), (0, 64, 0)]
        large = [(0, 0, 0), (600, 0, 0), (0, 0, 0)]
        self.assertFalse(should_subdivide(small, 1, 512))
        self.assertTrue(should_subdivide(large, 1, 512))
        self.assertTrue(should_subdivide(small, 4, 0))

    def test_post_bsp_subdivision_preserves_winding_and_exact_uvs(self) -> None:
        polygon = BspPolygon((
            BspVertex.make((0, 0, 0), (0, 0)),
            BspVertex.make((1024, 0, 0), (1024, 0)),
            BspVertex.make((0, 128, 0), (0, 128)),
        ), source=7, texture=3)
        fragments = adaptive_subdivide_triangle(polygon, 512)
        self.assertGreater(len(fragments), 1)
        self.assertTrue(all(fragment.source == 7 and fragment.texture == 3
                            for fragment in fragments))
        self.assertIn(
            BspVertex.make((512, 0, 0), (512, 0)),
            {vertex for fragment in fragments for vertex in fragment.vertices},
        )
        for fragment in fragments:
            a, b, c = (vertex.position for vertex in fragment.vertices)
            cross_z = ((b[0] - a[0]) * (c[1] - a[1]) -
                       (b[1] - a[1]) * (c[0] - a[0]))
            self.assertGreater(cross_z, 0)
            for left, right in ((a, b), (b, c), (c, a)):
                self.assertLessEqual(sum(
                    (right[axis] - left[axis]) ** 2 for axis in range(3)
                ), 512 * 512)

    def test_static_bsp_splits_geometry_and_interpolates_uv_exactly(self) -> None:
        polygon = BspPolygon(tuple(BspVertex.make(position, uv) for position, uv in (
            ((-2, 0, -2), (0, 0)),
            ((2, 0, -2), (64, 0)),
            ((0, 0, 2), (32, 64)),
        )), source=7)
        front, back = split_bsp_polygon(polygon, (1, 0, 0, 0))
        seam = [vertex for fragment in (front, back) for vertex in fragment.vertices
                if vertex.position[0] == 0]
        self.assertGreaterEqual(len(seam), 4)
        self.assertTrue(all(vertex.attributes[0] == 32 for vertex in seam))
        self.assertEqual({fragment.source for fragment in (front, back)}, {7})

    def test_static_bsp_order_changes_with_camera_side(self) -> None:
        polygons = [
            BspPolygon(tuple(BspVertex.make(point) for point in points), source=source)
            for source, points in (
                (0, ((0, -2, -2), (0, 2, -2), (0, 0, 2))),
                (1, ((-2, -2, 0), (2, -2, 0), (0, 2, 0))),
            )
        ]
        root, stats = build_bsp(polygons, candidate_limit=2)
        self.assertGreater(stats.split_events, 0)
        positive = [polygon.vertices for polygon in bsp_painter_order(root, (4, 0, 4))]
        negative = [polygon.vertices for polygon in bsp_painter_order(root, (-4, 0, -4))]
        self.assertNotEqual(positive, negative)
        self.assertEqual(stats.digest, build_bsp(polygons, candidate_limit=2)[1].digest)

    def test_castle_bsp_prototype_preserves_source_driven_policy(self) -> None:
        root = TOOLS.parents[1]
        report = compile_bsp(compile_scene(root / "levels/castle_inside/areas/1"),
                             subdivision=1, candidate_limit=8)
        self.assertEqual(report["source"], "levels/castle_inside/areas/1")
        self.assertEqual(report["input_render_polygons"], 567)
        self.assertGreater(report["node_count"], 1)
        self.assertGreater(report["output_convex_polygons"], 0)
        self.assertEqual(len(report["deterministic_sha256"]), 64)

    def test_fixed_camera_coverage_prefers_the_visible_blue_white_material(self) -> None:
        root = TOOLS.parents[1]
        ranked = plan(compile_opaque(root / "levels/castle_inside/areas/1"))["ranked_materials"]
        self.assertEqual(ranked[0]["texture"], "inside_09008000")
        self.assertGreater(ranked[0]["projected_pixels"], ranked[1]["projected_pixels"])


class SaturnMeshIRTests(unittest.TestCase):
    @staticmethod
    def document() -> dict[str, object]:
        return {
            "schema": "sm64-saturn-mesh-ir",
            "version": 1,
            "name": "unit_quad",
            "source": {"path": "fixture"},
            "positions": [[0, 0, 0], [10, 0, 0], [10, 10, 0], [0, 10, 0]],
            "materials": [{"id": 3, "rgb555": [31, 15, 0]}],
            "triangles": [
                {"source": 40, "material": 3, "indices": [0, 1, 2]},
                {"source": 41, "material": 3, "indices": [0, 2, 3]},
            ],
            "vertex_attributes": {},
            "validation_poses": [],
        }

    def test_compiler_preserves_source_ids_and_emits_true_quad(self) -> None:
        compiled, _primitives, report = compile_mesh_ir(self.document())
        self.assertEqual(report["quad_count"], 1)
        self.assertEqual(compiled["schema"], "sm64-saturn-compiled-mesh")
        self.assertEqual(compiled["primitives"][0]["representation"], "quad")
        self.assertEqual(compiled["primitives"][0]["source_triangles"], [40, 41])

    def test_v1_keeps_textured_triangles_as_explicit_fallbacks(self) -> None:
        document = self.document()
        document["vertex_attributes"] = {
            "uv": [[0, 0], [32, 0], [32, 32], [0, 32]]
        }
        compiled, _primitives, report = compile_mesh_ir(document)
        self.assertEqual(report["quad_count"], 0)
        self.assertEqual(report["rejection_reasons"], {"textured_pairing_not_implemented": 1})
        self.assertEqual(
            [primitive["representation"] for primitive in compiled["primitives"]],
            ["triangle_fallback", "triangle_fallback"],
        )

    def test_dynamic_region_can_forbid_neutral_pose_quad_pairing(self) -> None:
        document = self.document()
        document["pairing_forbidden_triangles"] = [0]
        compiled, _primitives, report = compile_mesh_ir(document)
        self.assertEqual(report["quad_count"], 0)
        self.assertEqual(report["pairing_forbidden_triangle_count"], 1)
        self.assertEqual(
            [primitive["representation"] for primitive in compiled["primitives"]],
            ["triangle_fallback", "triangle_fallback"],
        )

    def test_linear_blend_weights_must_sum_to_q15_one(self) -> None:
        document = self.document()
        document["deformation"] = {
            "mode": "linear_blend",
            "joint_count": 2,
            "influences": [[{"joint": 0, "weight_q15": 24576}, {"joint": 1, "weight_q15": 8192}]] * 4,
        }
        validate_mesh_ir(document)
        document["deformation"]["influences"][0] = [{"joint": 0, "weight_q15": 32767}]
        with self.assertRaisesRegex(ValueError, "sum to 32768"):
            validate_mesh_ir(document)

    def test_goddard_mode_preserves_more_than_one_total_weight(self) -> None:
        document = self.document()
        document["deformation"] = {
            "mode": "goddard_weighted_accumulation",
            "joint_count": 3,
            "influences": [
                [
                    {"joint": 0, "weight_q15": 32768},
                    {"joint": 1, "weight_q15": 32768},
                    {"joint": 2, "weight_q15": 16384},
                ]
            ] + [[] for _ in range(3)],
        }
        compiled, _primitives, report = compile_mesh_ir(document)
        self.assertEqual(compiled["deformation"]["mode"], "goddard_weighted_accumulation")
        self.assertEqual(report["deformation_influence_count"], 3)
        self.assertEqual(report["deformation_vertices_over_q15_one"], 1)

    def test_goddard_extractor_preserves_joint_order_and_overweight_vertices(self) -> None:
        source = """
            MakeAttachedJoint(JOINT_A),
                SetSkinWeight(1, 100.0),
            MakeAttachedJoint(JOINT_B),
                SetSkinWeight(1, 75.0),
                SetSkinWeight(2, 0.02),
        """
        deformation = goddard_deformation(source, 3)
        self.assertEqual(deformation["joints"], ["JOINT_A", "JOINT_B"])
        self.assertEqual(
            deformation["influences"][1],
            [{"joint": 0, "weight_q15": 32768}, {"joint": 1, "weight_q15": 24576}],
        )
        self.assertEqual(deformation["influences"][2][0]["weight_q15"], 7)


class YmirInputTests(unittest.TestCase):
    def test_pressed_mask_is_converted_to_active_low_pad_report(self) -> None:
        message = input_pulse_request(7, 0x0400)
        self.assertEqual(message["id"], 7)
        self.assertEqual(message["params"]["buttons"], 0xFBF8)
        self.assertEqual(message["params"]["frames"], 1)

    def test_hold_duration_is_forwarded(self) -> None:
        message = input_pulse_request(8, 0x8000, 12)
        self.assertEqual(message["params"], {"buttons": 0x7FF8, "frames": 12})


class TelemetryTests(unittest.TestCase):
    def test_complete_pass_status_decodes(self) -> None:
        words = [
            0x53415430, 1, 1, 0x8000001F, 0x5C, 0x400000, 0x400000,
            0xFFFFFFFF, 0, 0, 10, 20, 30, 40, 4, 15360,
        ]
        data = []
        for word in words:
            data.extend(word.to_bytes(4, "big"))
        data.extend((0x53415458).to_bytes(4, "big"))
        data.extend((1).to_bytes(4, "big"))
        data.extend([0] * 48)
        decoded = decode(data, require_complete=True)
        self.assertTrue(decoded["ok"])
        self.assertTrue(decoded["status_flags"]["complete"])
        self.assertEqual(decoded["cart_id"], 0x5C)
        self.assertEqual(decoded["extended"]["cpu_dmac_pass"], 0)
        self.assertEqual(decoded["extended"]["vdp1_textured_triangle_ticks"], 0)

    def test_wrong_cart_identity_is_not_ok(self) -> None:
        words = [
            0x53415430, 1, 1, 0x8000001F, 0x00, 0x400000, 0x400000,
            0xFFFFFFFF, 0, 0, 10, 20, 30, 40, 4, 15360,
        ]
        data = b"".join(word.to_bytes(4, "big") for word in words)
        decoded = decode(list(data), require_complete=True)
        self.assertFalse(decoded["ok"])

    def test_ymir_cd_block_diagnostic_is_recognized(self) -> None:
        self.assertTrue(has_cd_block_copy_limitation("info | CDBlock | Get copy error command is unimplemented"))
        self.assertTrue(has_cd_block_copy_limitation("CD-block copy operation unavailable"))
        self.assertFalse(has_cd_block_copy_limitation("Filesystem built successfully"))

    def test_bad_extended_magic_is_rejected(self) -> None:
        words = [
            0x53415430, 1, 1, 0x8000001F, 0x5C, 0x400000, 0x400000,
            0xFFFFFFFF, 0, 0, 10, 20, 30, 40, 4, 15360,
        ]
        data = bytearray(b"".join(word.to_bytes(4, "big") for word in words))
        data.extend((0).to_bytes(4, "big"))
        data.extend((1).to_bytes(4, "big"))
        data.extend(b"\0" * 48)
        with self.assertRaisesRegex(ValueError, "extended telemetry magic"):
            decode(list(data), require_complete=True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
