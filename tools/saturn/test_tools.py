#!/usr/bin/env python3
"""Small standard-library regression tests for Saturn host-side tools."""

from __future__ import annotations

import json
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))

from asset_classifier import classify_primitives, source_scan  # noqa: E402
from capture_hwtest import (  # noqa: E402
    artifact_identity,
    cadence_summary,
    DEFAULT_CAPTURE_TIMEOUT_SECONDS,
    cap_stderr,
    emulation_timing,
    has_cd_block_copy_limitation,
    input_pulse_request,
    stale_game_image,
)
from compare_route_reports import compare_reports, load_route  # noqa: E402
from extract_mario_actor import (  # noqa: E402
    animation_frame_count,
    animation_rotations,
    animation_translation,
    c_render_cluster_metadata,
    c_u8_light_bank,
    flatten as flatten_mario,
    flatten_parts,
    geo_layout_parts,
    identity_matrix,
    mario_vertex_light_intensities,
    mario_render_clusters,
)
from extract_mario_textures import saturn_rgb1555  # noqa: E402
from extract_introface_mesh import goddard_deformation  # noqa: E402
from bake_mario_eye_uv import TILE, bilinear_weights, split_four  # noqa: E402
from vdp1_texture import distorted_sprite_weights, downsample_rgb1555, repeated_vertex_weights  # noqa: E402
from inspect_castle_area import inventory  # noqa: E402
from extract_castle_area import extract, flatten  # noqa: E402
from extract_bob_area import intake as intake_bob_area, mesh_ir as mesh_ir_bob_area  # noqa: E402
from extract_castle_gameplay_config import extract as extract_castle_gameplay_config  # noqa: E402
from extract_castle_geo_root import extract as extract_castle_geo_root  # noqa: E402
from compile_castle_area import compile_opaque, compile_scene  # noqa: E402
from bake_castle_uv import (  # noqa: E402
    adaptive_subdivide_triangle,
    adaptive_subdivide_quad,
    index_position_quads,
    pack_clut16,
    quantize_clut16,
    sample_triangle,
    should_subdivide,
    texture_coordinate,
)
from bake_bob_tiles import bake_bob, triangle_k  # noqa: E402
from bake_bob_sky import bake as bake_bob_sky  # noqa: E402
from emit_bob_scene import emit as emit_bob_scene  # noqa: E402
from compile_castle_bsp import compile_bsp  # noqa: E402
from compile_bob_bsp import (  # noqa: E402
    _flatten as flatten_bob_bsp,
    _polygons as bob_bsp_polygons,
    compile_bsp as compile_bob_bsp,
    header_text as header_bob_bsp,
)
from bake_bob_bsp_fragments import bake as bake_bob_bsp_fragments  # noqa: E402
from emit_bob_bsp_fragments import emit as emit_bob_bsp_fragments  # noqa: E402
from compile_castle_collision import compile_stream, surface_values  # noqa: E402
from static_bsp import (  # noqa: E402
    Polygon as BspPolygon,
    Vertex as BspVertex,
    build as build_bsp,
    painter_order as bsp_painter_order,
    split_polygon as split_bsp_polygon,
)
from plan_castle_camera_coverage import plan  # noqa: E402
from prepare_sourceboot_collision_catalog import catalog_paths  # noqa: E402
from quad_pairing import QuadCandidate, RenderPrimitive, candidates, maximum_weight_matching, pair_triangles  # noqa: E402
from saturn_mesh_ir import compile_mesh_ir, validate_mesh_ir  # noqa: E402
from telemetry_decode import decode  # noqa: E402
from fast3d_profile_decode import (  # noqa: E402
    PROFILE_HEADER,
    STRUCT_TYPEDEF,
    ProfileSizeMismatch,
    decode_profile,
    layout_members,
    load_probe,
    parse_members,
    profile_layout,
    strip_c_comments,
)
from dl_rigid_groups import (  # noqa: E402,F401
    REASON_GEO_ASM,
    REASON_TEXTURED,
    REASON_UNBALANCED_POP,
    REASON_UNKNOWN_GEO_NODE,
    REASON_UNKNOWN_MACRO,
    TriangleSite,
    rigid_group_stats,
    walk_display_lists,
    walk_geo_layout,
)
from quad_map import (  # noqa: E402
    REASON_UNRESOLVED_VERTICES,
    QuadMapEntry,
    build_quad_map,
    render_quad_map_c,
    render_quad_map_h,
    resolve_display_list_vertices,
)


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


class SourcebootCatalogTests(unittest.TestCase):
    def test_direct_level_collision_sources_are_excluded(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for level in ("bob", "castle_grounds", "ttc", "ssl"):
                level_root = root / "levels" / level
                level_root.mkdir(parents=True)
                (level_root / "collision.inc.c").write_text("/* source */\n", encoding="utf-8")
            (root / "levels" / "ssl" / "trajectory.inc.c").write_text("/* source */\n", encoding="utf-8")

            paths = catalog_paths(root, ("bob", "castle_grounds", "ttc"))

        self.assertEqual(
            [path.relative_to(root).as_posix() for path in paths],
            ["levels/ssl/collision.inc.c", "levels/ssl/trajectory.inc.c"],
        )


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
    def test_promoted_pose_bank_reproduces_source_animation_evaluator(self) -> None:
        """The shared C bank must remain a byte-identical source extraction.

        Running the extractor over every source animation frame exercises the
        same GeoLayout/Animation evaluator used to create the bank.  This is a
        stronger fixture than checking only frame counts: any changed joint
        matrix or vertex pose changes the generated header bytes.
        """
        root = TOOLS.parents[1]
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "mario_actor_mesh.h"
            report = Path(directory) / "mario_actor_report.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(TOOLS / "extract_mario_actor.py"),
                    "--model", str(root / "actors/mario/model.inc.c"),
                    "--geo", str(root / "actors/mario/geo.inc.c"),
                    "--animation", str(root / "assets/anims/anim_C5.inc.c"),
                    "--walking-animation", str(root / "assets/anims/anim_48.inc.c"),
                    "--animation-frame", "0",
                    "--output", str(output),
                    "--report", str(report),
                ],
                cwd=root,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(
                output.read_bytes(),
                (root / "src/port/saturn/gfx/saturn_mario_actor_mesh.h").read_bytes(),
            )

    def test_render_clusters_are_stable_compact_leaf_work_lists(self) -> None:
        triangles = [
            {"display_list": "mario_leaf_z"},
            {"display_list": "mario_leaf_a"},
            {"display_list": "mario_leaf_z"},
        ]
        primitives = [
            {"source_triangles": [0], "indices": [0, 1, 2, 2]},
            {"source_triangles": [1], "indices": [1, 3, 4, 4]},
            {"source_triangles": [2], "indices": [2, 4, 5, 5]},
        ]
        positions = [
            [0, 0, 0], [1, 0, 0], [0, 1, 0],
            [10, 0, 0], [10, 1, 0], [0, 2, 0],
        ]
        metadata = mario_render_clusters(triangles, primitives, positions)
        self.assertEqual(
            [cluster["name"] for cluster in metadata["clusters"]],
            ["mario_leaf_a", "mario_leaf_z"],
        )
        self.assertEqual(metadata["primitive_offsets"], [0, 1, 3])
        self.assertEqual(metadata["primitive_indices"], [1, 0, 2])
        self.assertEqual(metadata["unique_vertex_offsets"], [0, 3, 8])
        self.assertEqual(metadata["unique_vertex_indices"], [1, 3, 4, 0, 1, 2, 4, 5])
        self.assertEqual(metadata["lod_unique_vertex_offsets"], [0, 6, 12, 15])
        self.assertEqual(metadata["lod_unique_vertex_indices"],
                         [1, 3, 4, 0, 2, 5,
                          1, 3, 4, 0, 2, 5,
                          1, 3, 4])
        self.assertEqual(
            metadata["clusters"][1]["bounds"],
            {"min": [0, 0, 0], "max": [10, 2, 0]},
        )
        emitted = "\n".join(c_render_cluster_metadata(metadata))
        self.assertIn("#define SM64_MARIO_RENDER_CLUSTER_COUNT 2U", emitted)
        self.assertIn("sm64_mario_render_cluster_primitive_offsets", emitted)
        self.assertIn("sm64_mario_render_cluster_primitive_list", emitted)
        self.assertIn("sm64_mario_render_cluster_vertex_list", emitted)
        self.assertIn("sm64_mario_render_cluster_lod_vertex_offsets", emitted)
        self.assertIn("sm64_mario_render_cluster_lod_vertex_list", emitted)
        self.assertIn("sm64_mario_render_cluster_bounds", emitted)

    def test_paired_primitive_cannot_cross_leaf_render_clusters(self) -> None:
        triangles = [
            {"display_list": "mario_left_leaf"},
            {"display_list": "mario_right_leaf"},
        ]
        primitives = [
            {"source_triangles": [0, 1], "indices": [0, 1, 2, 3]},
        ]
        with self.assertRaisesRegex(ValueError, "cross render clusters"):
            mario_render_clusters(
                triangles,
                primitives,
                [[0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0]],
            )

    def test_offline_vertex_light_matches_runtime_integer_policy(self) -> None:
        vertices = [
            [0, 0, 0], [1, 0, 0], [0, 1, 0],
            [0, 2, 0], [0, 1, 1], [99, 99, 99],
        ]
        primitives = [
            RenderPrimitive(0, (0, 1, 2, 2), 0, None),
            RenderPrimitive(0, (2, 3, 4, 4), 1, None),
        ]
        # The shared vertex receives +Z and +X normals.  A repeated triangle
        # corner must not add +Z twice: the exact C integer result is 13.
        self.assertEqual(
            mario_vertex_light_intensities(vertices, primitives),
            [27, 27, 13, 8, 8, 20],
        )

    def test_vertex_light_bank_is_compact_uint8_frame_vertex_data(self) -> None:
        lines = c_u8_light_bank(
            "test_light", "TEST_FRAME_COUNT", [[8, 20, 31], [31, 20, 8]]
        )
        emitted = "\n".join(lines)
        self.assertIn(
            "static const uint8_t test_light[TEST_FRAME_COUNT][SM64_MARIO_VERTEX_COUNT]",
            emitted,
        )
        self.assertIn("8, 20, 31", emitted)
        self.assertIn("31, 20, 8", emitted)
        with self.assertRaisesRegex(ValueError, "RGB555 range"):
            c_u8_light_bank("bad", "BAD_COUNT", [[32]])

    def test_textured_fast3d_combine_mode_is_preserved(self) -> None:
        display_lists = {
            "root": """
                gsDPSetCombineMode(G_CC_BLENDRGBFADEA, G_CC_BLENDRGBFADEA),
                gsDPSetTextureImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 1,
                                    mario_texture_m_logo),
                gsSPVertex(vertices, 3, 0),
                gsSP1Triangle(0, 1, 2, 0),
            """
        }
        vertices = {
            "vertices": [
                (0, 0, 0, 0, 0),
                (1, 0, 0, 992, 0),
                (0, 1, 0, 0, 992),
            ]
        }
        triangles: list[dict[str, object]] = []
        flatten_mario(
            display_lists, vertices, "root", identity_matrix(),
            "mario_red_lights_group", triangles
        )
        self.assertEqual(triangles[0]["texture"], "mario_texture_m_logo")
        self.assertEqual(triangles[0]["combine_mode"], "G_CC_BLENDRGBFADEA")

    def test_geo_parts_thread_fast3d_material_and_cull_state(self) -> None:
        display_lists = {
            "part_a": """
                gsSPLight(&mario_red_lights_group.l, 1),
                gsDPSetTextureImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 1,
                                    mario_texture_m_logo),
                gsDPSetCombineMode(G_CC_BLENDRGBFADEA, G_CC_BLENDRGBFADEA),
                gsSPClearGeometryMode(G_CULL_BACK),
                gsSPVertex(vertices_a, 3, 0),
                gsSP1Triangle(0, 1, 2, 0),
            """,
            "part_b": """
                gsSPVertex(vertices_b, 3, 0),
                gsSP1Triangle(0, 1, 2, 0),
            """,
        }
        vertices = {
            "vertices_a": [(0, 0, 0, 0, 0), (1, 0, 0, 32, 0), (0, 1, 0, 0, 32)],
            "vertices_b": [(0, 0, 1, 0, 0), (1, 0, 1, 32, 0), (0, 1, 1, 0, 32)],
        }
        triangles: list[dict[str, object]] = []
        flatten_parts(
            display_lists,
            vertices,
            [("part_a", identity_matrix(), "mario_blue_lights_group"),
             ("part_b", identity_matrix(), "mario_blue_lights_group")],
            triangles,
        )
        self.assertEqual(len(triangles), 2)
        self.assertEqual(triangles[0]["rgb"], (31, 0, 0))
        self.assertEqual(triangles[1]["rgb"], (31, 0, 0))
        self.assertEqual(triangles[1]["texture"], "mario_texture_m_logo")
        self.assertEqual(triangles[1]["combine_mode"], "G_CC_BLENDRGBFADEA")
        self.assertFalse(triangles[1]["cull_back"])

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

    def test_vdp1_direct_color_canonicalizes_transparent_rgb_to_zero(self) -> None:
        # N64 A1=0 texels retain RGB (transparent white is 0x7FFF after lane
        # conversion), but VDP1 only treats color code 0x0000 as transparent.
        # Nonzero bit-15-clear words would paint the long-standing dark/white
        # Mario face patches and 0x7FFF also collides with the default end code.
        width, height, pixels = downsample_rgb1555(
            [0x7FFF, 0x0008], 2, 1, 1
        )
        self.assertEqual((width, height), (2, 1))
        self.assertEqual(pixels, [0x0000, 0x0000])

        _, _, reduced = downsample_rgb1555(
            [0xFFFF, 0x7FFF, 0x0008, 0x0000], 2, 2, 2
        )
        self.assertEqual(reduced, [0x0000])

    def test_n64_transparent_rgb1555_payload_is_discarded_at_conversion(self) -> None:
        self.assertEqual(saturn_rgb1555(0xFFFF), 0xFFFF)
        self.assertEqual(saturn_rgb1555(0xFFFE), 0x0000)
        self.assertEqual(saturn_rgb1555(0x0000), 0x0000)

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

    def test_vdp1_repeated_vertex_tile_corner_order_is_a_b_c(self) -> None:
        self.assertEqual(TILE, 16)
        self.assertEqual(bilinear_weights(0, 0), repeated_vertex_weights(0, 0, 16, 16))
        for x, y in ((0, 0), (15, 0), (0, 15), (15, 15), (8, 8)):
            self.assertAlmostEqual(sum(bilinear_weights(x, y)), 1.0)
        # The lower-left D corner contributes to repeated destination C and
        # therefore contains source texture data.
        self.assertGreater(bilinear_weights(15, 15)[2], 0.9)

    def test_actor_texture_quality_tier_splits_one_triangle_into_four(self) -> None:
        source = {
            "positions": [[0, 0, 0], [8, 0, 0], [0, 8, 0]],
            "uv": [[0, 0], [256, 0], [0, 256]],
        }
        split = split_four(source)
        self.assertEqual(len(split), 4)
        self.assertEqual(split[0]["positions"], [[0, 0, 0], [4, 0, 0], [0, 4, 0]])
        self.assertEqual(split[3]["uv"], [[128, 0], [128, 128], [0, 128]])
        self.assertLessEqual(200 * TILE * TILE * 2, 0x0006BFE0)

    def test_vdp1_native_quad_uses_measured_a_b_c_d_corner_order(self) -> None:
        for x, y in ((0, 0), (15, 0), (0, 15), (15, 15), (8, 8)):
            a, b, c, d = distorted_sprite_weights(x, y, 16, 16)
            self.assertAlmostEqual(a + b + c + d, 1.0)
            repeated = repeated_vertex_weights(x, y, 16, 16)
            self.assertEqual(repeated, (a, b, c + d))
        self.assertGreater(distorted_sprite_weights(0, 0, 16, 16)[0], 0.9)
        self.assertGreater(distorted_sprite_weights(15, 15, 16, 16)[2], 0.9)


class CastleAreaInventoryTests(unittest.TestCase):
    def test_final_tile_vertices_are_indexed_once(self) -> None:
        vertices, indices = index_position_quads(
            [
                ((0, 0, 0), (8, 0, 0), (0, 8, 0), (-8, 8, 0)),
                ((8, 0, 0), (8, 8, 0), (0, 8, 0), (0, 0, 0)),
            ],
            [True, False],
        )
        self.assertEqual(len(vertices), 4)
        self.assertEqual(indices[0][2], indices[0][3])
        self.assertEqual(indices[0][1], indices[1][0])

    def test_castle_collision_bank_preserves_source_surface_stream(self) -> None:
        root = TOOLS.parents[1]
        source = (root / "levels/castle_inside/areas/1/collision.inc.c").read_text(encoding="utf-8")
        stream, stats = compile_stream(source, surface_values(root / "include/surface_terrains.h"))
        self.assertEqual(stats["vertices"], 1563)
        self.assertEqual(stats["triangles"], 2144)
        self.assertEqual(stream[0:2], [0x40, 1563])
        self.assertEqual(stream[-2:], [0x41, 0x42])

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
        self.assertEqual(result["camera"]["fixed_base"], [-577, 143, 1443])
        self.assertEqual(result["camera"]["entrance_base"], [-813, 378, 1103])
        self.assertEqual(result["camera"]["entrance_trigger"]["center"], [-1023, 376, 1830])
        self.assertFalse(result["camera"]["entrance_trigger"]["active_at_spawn"])
        self.assertEqual(result["camera"]["base"], [-577, 143, 1443])

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
        self.assertEqual(scene["version"], 3)
        self.assertEqual(
            sum(bool(triangle["cull_back"]) for triangle in scene["triangles"]),
            587,
        )
        self.assertFalse(any(bool(triangle["cull_front"])
                             for triangle in scene["triangles"]))
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
        self.assertEqual(bank["triangle_cull_back"].count(True), 587)
        self.assertEqual(
            sum(bool(primitive["cull_back"]) for primitive in bank["primitives"]),
            544,
        )
        self.assertFalse(any(bool(primitive["cull_front"])
                             for primitive in bank["primitives"]))
        self.assertTrue(all(len(set(primitive["vertices"][:3])) == 3
                            for primitive in bank["primitives"]))
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

    def test_castle_triangle_tiles_preserve_complete_repeated_c_mapping(self) -> None:
        texture = (8, 8, [0x801F] * 64, "digest", 128)
        state = {
            "width": 8, "height": 8, "sp_scale_s": 65536,
            "sp_scale_t": 65536, "uls": 0, "ult": 0,
            "mask_s": 3, "mask_t": 3, "shift_s": 0, "shift_t": 0,
            "clamp_s": False, "clamp_t": False,
            "mirror_s": False, "mirror_t": False,
        }
        uv = ((0, 0), (32 * 65536, 0), (32 * 65536, 32 * 65536))
        self.assertEqual(sample_triangle(texture, uv, state, 0, 7, 8, 1), 0x801F)
        self.assertEqual(sample_triangle(texture, uv, state, 7, 0, 8, 1), 0x801F)

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

    def test_post_bsp_quad_subdivision_preserves_four_corner_attributes(self) -> None:
        polygon = BspPolygon(tuple(BspVertex.make(position, uv) for position, uv in (
            ((0, 0, 0), (0, 0)),
            ((1024, 0, 0), (1024, 0)),
            ((1024, 0, 1024), (1024, 1024)),
            ((0, 0, 1024), (0, 1024)),
        )), source=8, texture=4)
        fragments = adaptive_subdivide_quad(polygon, 1024)
        self.assertEqual(len(fragments), 4)
        self.assertTrue(all(len(fragment.vertices) == 4 for fragment in fragments))
        self.assertIn(
            BspVertex.make((512, 0, 512), (512, 512)),
            {vertex for fragment in fragments for vertex in fragment.vertices},
        )

    def test_castle_clut_reserves_transparency_and_is_deterministic(self) -> None:
        pixels = [0x0000, 0x801F, 0x83E0, 0xFC00, 0xFFFF] * 8
        palette, mapping = quantize_clut16(pixels)
        self.assertEqual(len(palette), 16)
        self.assertEqual(palette[0], 0)
        self.assertEqual(mapping[0], 0)
        self.assertTrue(all(1 <= mapping[value] <= 15
                            for value in pixels if value & 0x8000))
        self.assertEqual((palette, mapping), quantize_clut16(pixels))
        self.assertEqual(pack_clut16([0, 1, 14, 15]), [0x01, 0xEF])

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


class BobMeshIRTests(unittest.TestCase):
    AREA = TOOLS.parents[1] / "levels" / "bob" / "areas" / "1"

    def test_bob_intake_reconciles_known_triangle_and_layer_counts(self) -> None:
        intake = intake_bob_area(self.AREA)
        self.assertEqual(intake["schema"], "sm64-saturn-static-scene-intake")
        self.assertEqual(intake["triangle_count"], 1101)
        self.assertEqual(intake["textured_triangle_count"], 1101)
        self.assertEqual(
            intake["layers"],
            {"LAYER_OPAQUE": 1043, "LAYER_TRANSPARENT_DECAL": 34, "LAYER_ALPHA": 24},
        )

    def test_bob_clut16_tiles_hit_spec_classes_budget_and_determinism(self) -> None:
        root = TOOLS.parents[1]
        intake = intake_bob_area(self.AREA)
        first = bake_bob(intake, root)
        second = bake_bob(intake, root)
        self.assertEqual(first, second)
        bank, clut, manifest = first
        self.assertEqual(manifest["coverage"], {"16x16": 567, "32x32": 510, "gouraud": 24})
        self.assertEqual(len(bank), 333696)
        self.assertEqual(len(clut), (567 + 510) * 32)
        self.assertLessEqual(manifest["resident_bytes"], 446432)
        self.assertEqual(len(manifest["entries"]), 1077)

    def test_bob_k_thresholds_are_explicit_and_mutation_visible(self) -> None:
        intake = intake_bob_area(self.AREA)
        values = [triangle_k(triangle) for triangle in intake["triangles"]]
        self.assertEqual(sum(value <= 2 for value in values), 567)
        self.assertEqual(sum(2 < value <= 16 for value in values), 510)
        self.assertEqual(sum(value > 16 for value in values), 24)

    def test_bob_scene_emitter_preserves_ir_counts_and_manifest_offsets(self) -> None:
        root = TOOLS.parents[1]
        mesh = json.loads((root / "build/saturn/sourceboot/generated/bob_area1_compiled.json").read_text(encoding="utf-8"))
        manifest = json.loads((root / "build/saturn/sourceboot/generated/bob_tiles_manifest.json").read_text(encoding="utf-8"))
        bsp = compile_bob_bsp(mesh)
        header = emit_bob_scene(mesh, manifest, bsp)
        # Textured source triangles share castleviewer's repeated-C lowering;
        # no offline affine companion is retained in the runtime pool.
        self.assertIn("SM64_SATURN_BOB_POSITION_COUNT 1625U", header)
        self.assertIn("SM64_SATURN_BOB_PRIMITIVE_COUNT 867U", header)
        self.assertIn("SM64_SATURN_BOB_SCENE_NODE_SPAN_COUNT 1183U", header)
        self.assertIn("SM64_SATURN_BOB_SCENE_BSP_CONTENT_ID", header)
        self.assertEqual(header, emit_bob_scene(mesh, manifest, bsp))
        # A same-cardinality reordering must not silently stamp a stale scene
        # header: the emitter recomputes the packed-node digest before use.
        mismatched_bsp = json.loads(json.dumps(bsp))
        mismatched_bsp["node_spans"]["primitive_refs"].reverse()
        with self.assertRaisesRegex(ValueError, "digest mismatch"):
            emit_bob_scene(mesh, manifest, mismatched_bsp)
        # A separately regenerated same-cardinality reorder has a different
        # identity; the renderer and C smoke preprocessor checks reject it if
        # paired with the original BSP header.
        reordered = mismatched_bsp["node_spans"]
        reordered["sha256"] = hashlib.sha256(json.dumps(
            [reordered["node_first_ref"], reordered["node_ref_count"],
             reordered["primitive_refs"]],
            separators=(",", ":")).encode()).hexdigest()
        reordered_header = emit_bob_scene(mesh, manifest, mismatched_bsp)
        self.assertNotEqual(
            re.search(r"BSP_CONTENT_ID (0x[0-9a-f]+ULL)", header).group(1),
            re.search(r"BSP_CONTENT_ID (0x[0-9a-f]+ULL)",
                      reordered_header).group(1))
        self.assertEqual(header.count("    {{"), 867)
        self.assertIn("512U, 32U", header)

    def test_bob_bsp_report_is_exact_and_deterministic(self) -> None:
        root = TOOLS.parents[1]
        scene = json.loads((root / "build/saturn/sourceboot/generated/bob_area1_compiled.json").read_text(encoding="utf-8"))
        first = compile_bob_bsp(scene)
        second = compile_bob_bsp(scene)
        self.assertEqual(first, second)
        self.assertEqual(first["input_render_polygons"], 867)
        self.assertEqual(first["output_convex_polygons"], 1425)
        self.assertEqual(first["split_events"], 560)
        self.assertEqual(first["node_count"], 1183)
        self.assertEqual(len(first["deterministic_sha256"]), 64)
        self.assertEqual(first["version"], 2)
        self.assertEqual(first["bounds"]["root_min"], [-8191, -383, -8191])
        self.assertEqual(first["bounds"]["root_max"], [8192, 4294, 8192])
        self.assertGreater(first["bounds"]["root_work_weight"], 0)
        self.assertEqual(first["flattened_ref_count"], 1425)
        self.assertGreater(first["leaf_node_count"], 0)
        self.assertEqual(first["camera_octant_order"]["octants"], 8)
        self.assertEqual(len(first["leaf_range_sha256"]), 64)
        self.assertEqual(len(first["subtree_range_sha256"]), 64)
        self.assertIn("runtime traversal", " ".join(first["limits"]).lower())

    def test_bob_bsp_header_emits_conservative_bounds_and_work_weights(self) -> None:
        root = TOOLS.parents[1]
        scene = json.loads((root / "build/saturn/sourceboot/generated/bob_area1_compiled.json").read_text(encoding="utf-8"))
        header = header_bob_bsp(scene)
        self.assertIn("sm64_saturn_bob_bsp_bounds_min", header)
        self.assertIn("sm64_saturn_bob_bsp_bounds_max", header)
        self.assertIn("sm64_saturn_bob_bsp_work_weight", header)
        self.assertIn("sm64_saturn_bob_bsp_subtree_ranges", header)
        self.assertIn("sm64_saturn_bob_bsp_leaf_ranges", header)
        self.assertIn("sm64_saturn_bob_bsp_octant_child_order", header)
        self.assertEqual(header, header_bob_bsp(scene))

    def test_bob_bsp_node_spans_are_deterministic_complete_and_unique(self) -> None:
        """The runtime work producer must not need an all-primitive scan.

        A span is the deterministic, deduplicated local reference list for
        one generated BSP node.  A primitive may conservatively occur in more
        than one node after an exact BSP split, but it must occur at most once
        in any individual span; the runtime keeps the predecessor traversal's
        first-reference-wins order while it appends accepted spans.
        """
        root = TOOLS.parents[1]
        scene = json.loads((root / "build/saturn/sourceboot/generated/bob_area1_compiled.json").read_text(encoding="utf-8"))
        first = compile_bob_bsp(scene)
        second = compile_bob_bsp(scene)
        spans = first["node_spans"]
        self.assertEqual(spans, second["node_spans"])
        self.assertEqual(len(spans["node_first_ref"]), first["node_count"])
        self.assertEqual(len(spans["node_ref_count"]), first["node_count"])
        packed = spans["primitive_refs"]
        self.assertEqual(spans["primitive_ref_count"], len(packed))
        for start, count in zip(spans["node_first_ref"],
                                spans["node_ref_count"]):
            self.assertGreaterEqual(start, 0)
            self.assertGreaterEqual(count, 0)
            self.assertLessEqual(start + count, len(packed))
            refs = packed[start:start + count]
            self.assertEqual(len(refs), len(set(refs)))
            self.assertTrue(all(0 <= ref < first["input_render_polygons"]
                                for ref in refs))
        # The packed spans are a conservative replacement for the legacy
        # node-local reference stream: no compiled primitive can disappear.
        self.assertEqual(set(packed), set(range(first["input_render_polygons"])))
        # Compare deterministic representative admitted-node masks against
        # the predecessor's node-local references. The compact stream must
        # retain both first-reference-wins membership and source order.
        legacy_root, _stats = build_bsp(bob_bsp_polygons(scene))
        _nodes, legacy_refs, _children, legacy_ranges, _subtree, _octants, _leaves = \
            flatten_bob_bsp(legacy_root)
        for admitted_nodes in (range(first["node_count"]),
                               range(0, first["node_count"], 3),
                               range(1, first["node_count"], 7)):
            legacy_order: list[int] = []
            compact_order: list[int] = []
            legacy_seen: set[int] = set()
            compact_seen: set[int] = set()
            for node in admitted_nodes:
                start, count = legacy_ranges[node]
                for primitive in legacy_refs[start:start + count]:
                    if primitive not in legacy_seen:
                        legacy_seen.add(primitive)
                        legacy_order.append(primitive)
                start = spans["node_first_ref"][node]
                count = spans["node_ref_count"][node]
                for primitive in packed[start:start + count]:
                    if primitive not in compact_seen:
                        compact_seen.add(primitive)
                        compact_order.append(primitive)
            self.assertEqual(compact_order, legacy_order)
        header = header_bob_bsp(scene)
        self.assertIn("sm64_saturn_bob_node_first_ref", header)
        self.assertIn("sm64_saturn_bob_node_ref_count", header)
        self.assertIn("sm64_saturn_bob_primitive_refs", header)
        self.assertIn("SM64_SATURN_BOB_BSP_CONTENT_ID", header)

    def test_bob_bsp_fragment_texture_cost_exposes_budget_gap(self) -> None:
        root = TOOLS.parents[1]
        scene = json.loads((root / "build/saturn/sourceboot/generated/bob_area1_compiled.json").read_text(encoding="utf-8"))
        manifest = json.loads((root / "build/saturn/sourceboot/generated/bob_tiles_manifest.json").read_text(encoding="utf-8"))
        report = compile_bob_bsp(scene, manifest=manifest)
        self.assertEqual(report["fragment_tile_classes"],
                         {"16x16": 597, "32x32": 793, "flat": 35})
        self.assertEqual(report["triangulated_fragment_tile_classes"],
                         {"16x16": 876, "32x32": 1165, "flat": 67})
        self.assertEqual(report["triangulated_fragment_count"], 2108)
        self.assertEqual(report["estimated_fragment_resident_bytes"], 773920)
        self.assertEqual(report["all_16x16_fragment_resident_bytes"], 337280)
        self.assertGreater(report["estimated_fragment_resident_bytes"],
                           report["vdp1_texture_budget_bytes"])

    def test_bob_bsp_fragment_bake_is_deterministic_and_budgeted(self) -> None:
        root = TOOLS.parents[1]
        scene = json.loads((root / "build/saturn/sourceboot/generated/bob_area1_compiled.json").read_text(encoding="utf-8"))
        first = bake_bob_bsp_fragments(scene, root)
        second = bake_bob_bsp_fragments(scene, root)
        self.assertEqual(first, second)
        bank, clut, manifest, fragment_scene = first
        self.assertEqual(len(bank), 261248)
        self.assertEqual(len(clut), 65312)
        self.assertEqual(len(bank) + len(clut), 326560)
        self.assertEqual(manifest["fragment_count"], 2108)
        self.assertEqual(manifest["textured_fragment_count"], 2108 - 67)
        self.assertEqual(len(fragment_scene["fragments"]), 2108)
        self.assertEqual(fragment_scene["uv_mapping"], {
            "tile_size": 16,
            "source_scale": 1,
            "weights": "vdp1_repeated_c",
            "corner_order": "A/B/C/C",
            "uv_interpolation": "exact_rational_before_source_tile_state",
        })
        for fragment in fragment_scene["fragments"]:
            self.assertEqual(len(fragment["positions"]), 3)
            self.assertEqual(len(fragment["uv"]), 3)
            if fragment["textured"]:
                self.assertEqual(int(fragment["tile_offset"]) % (16 * 16 // 2), 0)
                self.assertEqual(int(fragment["clut_offset"]) % 32, 0)
        bsp = fragment_scene["bsp"]
        self.assertEqual(bsp["node_count"], 1183)
        self.assertEqual(bsp["ref_count"], 2108)
        self.assertEqual(sorted(bsp["refs"]), list(range(2108)))

    def test_bob_bsp_fragment_emitter_preserves_mesh_primitive_identity(self) -> None:
        root = TOOLS.parents[1]
        scene = json.loads((root / "build/saturn/sourceboot/generated/bob_area1_compiled.json").read_text(encoding="utf-8"))
        _bank, _clut, _manifest, fragment_scene = bake_bob_bsp_fragments(scene, root)
        header = emit_bob_bsp_fragments(scene, fragment_scene)
        self.assertIn("SM64_SATURN_BOB_FRAGMENT_PRIMITIVE_COUNT 2108U", header)
        # The BSP header references compiled primitive IDs. A source-triangle
        # ordinal would silently place split fragments in the wrong order.
        first = fragment_scene["fragments"][0]
        self.assertIn("}, %d, 65535," % int(first["source_primitive"]), header)

    def test_bob_v2_is_attribute_exact_and_pairs_textured_quads(self) -> None:
        intake = intake_bob_area(self.AREA)
        document = mesh_ir_bob_area(intake)
        validate_mesh_ir(document)
        self.assertEqual(document["version"], 2)
        self.assertTrue(document["static_world_space"])
        self.assertEqual(len(document["triangles"]), 1101)
        self.assertLess(len(document["positions"]), 3303)
        self.assertEqual(len(document["positions"]), len(document["vertex_attributes"]["uv"]))
        compiled, _primitives, report = compile_mesh_ir(document)
        self.assertGreaterEqual(report["quad_count"], 220)
        self.assertLessEqual(report["quad_count"], 285)
        self.assertEqual(report["source_triangle_count"], 1101)
        self.assertEqual(report["render_primitive_count"], len(compiled["primitives"]))
        self.assertTrue(report["textured_pairing"])

    def test_bob_v2_generation_is_deterministic_and_within_lwram_budget(self) -> None:
        first = mesh_ir_bob_area(intake_bob_area(self.AREA))
        second = mesh_ir_bob_area(intake_bob_area(self.AREA))
        self.assertEqual(
            json.dumps(first, sort_keys=True, separators=(",", ":")),
            json.dumps(second, sort_keys=True, separators=(",", ":")),
        )
        compiled, _primitives, _report = compile_mesh_ir(first)
        # Conservative packed-bank estimate: positions + UVs + 16-byte VDP1
        # primitive records + materials. Keep the 576 KiB LWRAM ceiling
        # explicit even before the binary bank writer lands.
        estimate = (
            len(first["positions"]) * (6 + 4)
            + len(compiled["primitives"]) * 16
            + len(first["materials"]) * 16
        )
        self.assertLessEqual(estimate, 576 * 1024)

    def test_bob_textured_pairing_mutation_tile_state_is_killed(self) -> None:
        document = {
            "schema": "sm64-saturn-mesh-ir",
            "version": 2,
            "static_world_space": True,
            "name": "mutation_tile_state",
            "positions": [[0, 0, 0], [10, 0, 0], [10, 10, 0], [0, 10, 0]],
            "materials": [{"id": 0, "rgb555": [31, 31, 31]}],
            "triangles": [
                {"source": 0, "material": 0, "indices": [0, 1, 2],
                 "texture_tile": {"texture": "tile", "state": {"tile": 0}}},
                {"source": 1, "material": 0, "indices": [0, 2, 3],
                 "texture_tile": {"texture": "tile", "state": {"tile": 1}}},
            ],
            "vertex_attributes": {"uv": [[0, 0], [32, 0], [32, 32], [0, 32]]},
        }
        _compiled, _primitives, report = compile_mesh_ir(document)
        self.assertEqual(report["quad_count"], 0)
        self.assertIn("texture_tile_state_mismatch", report["rejection_reasons"])

    def test_bob_textured_pairing_mutation_affine_gate_is_killed(self) -> None:
        document = mesh_ir_bob_area(intake_bob_area(self.AREA))
        # A large boundary-UV perturbation must not silently turn a textured
        # quad into a VDP1 affine mapping.
        document["vertex_attributes"]["uv"][document["triangles"][0]["indices"][0]][0] += 100000
        _compiled, _primitives, report = compile_mesh_ir(document)
        self.assertIn("uv_affine_error", report["rejection_reasons"])

    def test_bob_attribute_exact_weld_mutation_is_killed(self) -> None:
        document = mesh_ir_bob_area(intake_bob_area(self.AREA))
        by_position: dict[tuple[int, int, int], set[tuple[int, int]]] = {}
        for position, uv in zip(document["positions"], document["vertex_attributes"]["uv"]):
            by_position.setdefault(tuple(position), set()).add(tuple(uv))
        hard_edge_positions = sum(len(uvs) > 1 for uvs in by_position.values())
        self.assertGreater(hard_edge_positions, 0)
        self.assertGreater(len(document["positions"]), len(by_position))


class YmirInputTests(unittest.TestCase):
    def test_capture_timeout_matches_slow_route_guidance(self) -> None:
        self.assertGreaterEqual(DEFAULT_CAPTURE_TIMEOUT_SECONDS, 1500.0)

    def test_artifact_identity_records_hash_size_and_mtime(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "artifact.bin"
            path.write_bytes(b"saturn-artifact")
            identity = artifact_identity(path)
            self.assertIsNotNone(identity)
            self.assertEqual(identity["size"], len(b"saturn-artifact"))
            self.assertEqual(identity["sha256"], __import__("hashlib").sha256(
                b"saturn-artifact").hexdigest())
            self.assertIsInstance(identity["mtime"], float)

    def test_pressed_mask_is_converted_to_active_low_pad_report(self) -> None:
        message = input_pulse_request(7, 0x0400)
        self.assertEqual(message["id"], 7)
        self.assertEqual(message["params"]["buttons"], 0xFBF8)
        self.assertEqual(message["params"]["frames"], 1)

    def test_hold_duration_is_forwarded(self) -> None:
        message = input_pulse_request(8, 0x8000, 12)
        self.assertEqual(message["params"], {"buttons": 0x7FF8, "frames": 12})

    def test_frame_sample_contract_is_probe_safe(self) -> None:
        header = (TOOLS / ".." / ".." / "src" / "port" / "saturn" / "platform" /
                  "saturn_frame_sample.h").resolve().read_text(encoding="utf-8")
        fields = [line.strip() for line in header.splitlines()
                  if line.strip().startswith("uint16_t ")]
        self.assertEqual(len(fields), 21)
        self.assertIn("#define SM64_SATURN_FRAME_SAMPLE_MAGIC 0x4653U", header)
        self.assertIn("uint16_t sequence;", header)

    def test_short_stderr_is_returned_unchanged(self) -> None:
        text = "a short boot log\n"
        capped, original_length = cap_stderr(text, limit=1024)
        self.assertEqual(capped, text)
        self.assertEqual(original_length, len(text))

    def test_long_stderr_is_truncated_to_its_tail(self) -> None:
        # Regression test for a real incident (2026-07-22): a crash-looping
        # emulated target made Ymir emit unbounded repeating diagnostics,
        # and capture_hwtest.py used to embed completed.stderr verbatim --
        # producing multi-gigabyte report files that filled the host disk.
        head = "boot diagnostics that must not appear in the capped output\n"
        tail = "crash-loop diagnostic that must survive capping\n"
        text = head + ("filler\n" * 1000) + tail
        capped, original_length = cap_stderr(text, limit=64)
        self.assertEqual(original_length, len(text))
        self.assertLessEqual(len(capped), 64)
        self.assertTrue(text.endswith(capped))
        self.assertNotIn("boot diagnostics", capped)

    def test_default_limit_is_reasonable(self) -> None:
        # Not so small that a real boot's ordinary diagnostic output would
        # itself get truncated, not so large that a crash-loop can still
        # produce a multi-megabyte report.
        capped, original_length = cap_stderr("x" * 200_000)
        self.assertEqual(original_length, 200_000)
        self.assertEqual(len(capped), 64 * 1024)

    def test_stale_game_image_detects_newer_sibling_elf(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            game = root / "disc.cue"
            obj = root / "obj"
            obj.mkdir()
            elf = obj / "game.elf"
            game.write_bytes(b"cue")
            elf.write_bytes(b"elf")
            os.utime(game, (100, 100))
            os.utime(elf, (200, 200))
            result = stale_game_image(game)
            self.assertIsNotNone(result)
            self.assertEqual(result[0], elf)

    def test_stale_game_image_accepts_newer_cue(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            game = root / "disc.cue"
            obj = root / "obj"
            obj.mkdir()
            elf = obj / "game.elf"
            game.write_bytes(b"cue")
            elf.write_bytes(b"elf")
            os.utime(game, (200, 200))
            os.utime(elf, (100, 100))
            self.assertIsNone(stale_game_image(game))

    def test_emulation_timing_reports_guest_rate_and_speed_ratio(self) -> None:
        timing = emulation_timing(3600, 120.0)
        self.assertEqual(timing["emulated_frames"], 3600)
        self.assertEqual(timing["emulated_vblank_fps"], 30.0)
        self.assertEqual(timing["emulation_speed_ratio"], 0.5)

    def test_emulation_timing_rejects_nonpositive_inputs(self) -> None:
        with self.assertRaises(ValueError):
            emulation_timing(0, 1.0)
        with self.assertRaises(ValueError):
            emulation_timing(1, 0.0)

    def test_cadence_summary_reports_median_and_one_percent_low(self) -> None:
        summary = cadence_summary(
            [
                {"emulated_frame": 0, "frame_serial": 0},
                {"emulated_frame": 60, "frame_serial": 10},
                {"emulated_frame": 120, "frame_serial": 20},
                {"emulated_frame": 180, "frame_serial": 21},
            ]
        )
        self.assertEqual(summary["sample_count"], 4)
        self.assertEqual(summary["interval_count"], 3)
        self.assertAlmostEqual(summary["guest_fps_median"], 10.0)
        self.assertAlmostEqual(summary["guest_fps_1pct_low"], 1.0)

    def test_cadence_summary_rejects_nonmonotonic_samples(self) -> None:
        with self.assertRaises(ValueError):
            cadence_summary(
                [
                    {"emulated_frame": 60, "frame_serial": 1},
                    {"emulated_frame": 60, "frame_serial": 2},
                ]
            )


class BobParityRouteTests(unittest.TestCase):
    SBR4_FIELDS = (
        "magic", "version", "atan2_variant", "replay_ticks", "global_timer", "mario_action",
        "mario_pos_x_bits", "mario_pos_y_bits", "mario_pos_z_bits",
        "mario_face_angle_x", "mario_face_angle_y", "mario_face_angle_z",
        "camera_pos_x_bits", "camera_pos_y_bits", "camera_pos_z_bits",
        "camera_mode", "triangles_transformed", "triangles_emitted",
        "triangles_vdp1_emitted", "reject_near_far", "reject_backface",
        "reject_degenerate", "reject_vertex_range", "reject_command_capacity",
        "reject_vdp1_arena_capacity", "reject_w_nonpositive", "reject_z_near",
        "reject_z_far", "reject_offscreen", "reject_span",
        "reject_w_nonpositive_overflow_suspect", "fault_flags", "frame_serial",
        "sim_frt_ticks_accum", "render_frt_ticks_accum",
        "render_frt_ticks_last", "master_wait_ticks", "slave_busy_ticks",
        "slave_jobs_completed", "slave_timeouts",
    )

    @staticmethod
    def _float_bits(value: float) -> int:
        return struct.unpack(">I", struct.pack(">f", value))[0]

    @staticmethod
    def _probe(**overrides: int) -> dict[str, int]:
        probe = {
            "magic": 0x53425234,
            "version": 4,
            "atan2_variant": 1,
            "replay_ticks": 2000,
            "global_timer": 2001,
            "mario_action": 0x04000440,
            "mario_pos_x_bits": BobParityRouteTests._float_bits(-123.5),
            "mario_pos_y_bits": BobParityRouteTests._float_bits(0.0),
            "mario_pos_z_bits": BobParityRouteTests._float_bits(456.25),
            "mario_face_angle_x": 0x0010,
            "mario_face_angle_y": 0x8000,
            "mario_face_angle_z": 0xFFF0,
            "camera_pos_x_bits": BobParityRouteTests._float_bits(10.0),
            "camera_pos_y_bits": BobParityRouteTests._float_bits(20.0),
            "camera_pos_z_bits": BobParityRouteTests._float_bits(30.0),
            "camera_mode": 1,
            "triangles_transformed": 2311,
            "triangles_emitted": 913,
            "triangles_vdp1_emitted": 829,
            "reject_near_far": 100,
            "reject_backface": 200,
            "reject_degenerate": 300,
            "reject_vertex_range": 400,
            "reject_command_capacity": 0,
            "reject_vdp1_arena_capacity": 0,
            "reject_w_nonpositive": 500,
            "reject_z_near": 600,
            "reject_z_far": 700,
            "reject_offscreen": 800,
            "reject_span": 900,
            "reject_w_nonpositive_overflow_suspect": 1000,
            "fault_flags": 0,
            "frame_serial": 500,
            "sim_frt_ticks_accum": 10000,
            "render_frt_ticks_accum": 20000,
            "render_frt_ticks_last": 40,
            "master_wait_ticks": 50,
            "slave_busy_ticks": 60,
            "slave_jobs_completed": 500,
            "slave_timeouts": 0,
        }
        probe.update(overrides)
        return probe

    @staticmethod
    def _report(
        probe: dict[str, int],
        capture_role: str = "legacy",
    ) -> dict[str, object]:
        bound_probe = dict(probe)
        bound_probe["atan2_variant"] = 1 if capture_role == "legacy" else 2
        words = [bound_probe[field] for field in BobParityRouteTests.SBR4_FIELDS]
        raw = list(struct.pack(">40I", *words))
        artifact_digit = "1" if capture_role == "legacy" else "3"
        image_digit = "2" if capture_role == "legacy" else "4"
        return {
            "evidence_kind": "ymir-emulator",
            "capture_role": capture_role,
            "game": "sourceboot.cue",
            "frames": 240,
            "post_poke_frames": 36000,
            "probe_window": {"data": raw, "decoded": bound_probe},
            "protocol": {"ready": True},
            "degradation": {"view_radius": 6000, "poly_tier": 0},
            "artifacts": {
                "elf": {"sha256": artifact_digit * 64, "size": 100},
                "image": {"sha256": image_digit * 64, "size": 200},
            },
        }

    def test_route_is_2000_ticks_and_has_movement_jump_and_camera_input(self) -> None:
        route = load_route(TOOLS / "routes" / "bob_parity_v1.json")
        self.assertEqual(route["simulation_ticks"], 2000)
        self.assertEqual(sum(sample["ticks"] for sample in route["samples"]), 2000)
        self.assertTrue(any(sample["buttons"] & 0x8000 for sample in route["samples"]))
        self.assertTrue(any(sample["buttons"] & 0x0003 for sample in route["samples"]))
        self.assertTrue(any(sample["stick_x"] or sample["stick_y"] for sample in route["samples"]))

    def test_renderer_view_manifest_is_route_bound_and_reproducible(self) -> None:
        manifest = json.loads(
            (TOOLS / "routes" / "bob_renderer_views_v1.json").read_text(encoding="utf-8")
        )
        route = load_route(TOOLS / "routes" / "bob_parity_v1.json")
        self.assertEqual(manifest["source_route"], "tools/saturn/routes/bob_parity_v1.json")
        self.assertEqual(manifest["reproduction"]["required_checkpoint_tick"], route["checkpoint_tick"])
        self.assertEqual(manifest["reproduction"]["repeat_count"], 2)
        self.assertEqual(
            [view["route_tick"] for view in manifest["viewpoints"]],
            [360, 504, 2000],
        )
        self.assertTrue(all(view["route_tick"] <= route["checkpoint_tick"] for view in manifest["viewpoints"]))

    def test_stage2_comparator_accepts_subunit_mario_drift_and_one_percent_reject_drift(self) -> None:
        route = load_route(TOOLS / "routes" / "bob_parity_v1.json")
        before = self._probe()
        after = self._probe(
            mario_pos_x_bits=self._float_bits(-122.5001),
            reject_near_far=101,
            reject_w_nonpositive_overflow_suspect=1010,
        )
        result = compare_reports(
            self._report(before, "legacy"),
            self._report(after, "q16"),
            route,
        )
        self.assertTrue(result["deterministic"])
        self.assertLess(result["behavioral_gate"]["mario_position_linf"], 1.0)

    def test_stage2_comparator_rejects_one_world_unit_mario_drift(self) -> None:
        route = load_route(TOOLS / "routes" / "bob_parity_v1.json")
        before = self._probe()
        after = self._probe(mario_pos_z_bits=self._float_bits(457.25))
        result = compare_reports(self._report(before), self._report(after), route)
        self.assertFalse(result["deterministic"])
        self.assertIn("Mario position L-infinity divergence is not < 1.0", result["errors"])

    def test_comparator_accepts_route_in_paired_extra_probe_window(self) -> None:
        route = load_route(TOOLS / "routes" / "bob_parity_v1.json")
        probe = self._probe()
        report = self._report(probe)
        report["probe_window"] = {"data": [0] * (40 * 4)}
        q16_probe = dict(probe)
        q16_probe["atan2_variant"] = 2
        report["extra_probe_window"] = {
            "data": list(struct.pack(">40I", *(probe[field] for field in self.SBR4_FIELDS))),
            "decoded": dict(probe),
        }
        result = compare_reports(
            report,
            self._report(q16_probe, "q16"),
            route,
        )
        self.assertTrue(result["deterministic"])

    def test_stage2_comparator_requires_identical_face_camera_and_triangles_emitted(self) -> None:
        route = load_route(TOOLS / "routes" / "bob_parity_v1.json")
        for field, value in (
            ("mario_face_angle_y", 0x8001),
            ("camera_pos_x_bits", self._float_bits(10.25)),
            ("triangles_emitted", 914),
        ):
            with self.subTest(field=field):
                result = compare_reports(
                    self._report(self._probe()),
                    self._report(self._probe(**{field: value})),
                    route,
                )
                self.assertFalse(result["deterministic"])
                self.assertTrue(any(field in error for error in result["errors"]))

    def test_stage2_comparator_rejects_over_one_percent_and_zero_baseline_reject_drift(self) -> None:
        route = load_route(TOOLS / "routes" / "bob_parity_v1.json")
        for field, value in (
            ("reject_backface", 203),
            ("reject_command_capacity", 1),
        ):
            with self.subTest(field=field):
                result = compare_reports(
                    self._report(self._probe()),
                    self._report(self._probe(**{field: value})),
                    route,
                )
                self.assertFalse(result["deterministic"])
                self.assertTrue(any(field in error for error in result["errors"]))

    def test_stage2_comparator_rejects_missing_camera_sentinels_even_when_equal(self) -> None:
        route = load_route(TOOLS / "routes" / "bob_parity_v1.json")
        missing_camera = self._probe(
            camera_pos_x_bits=0xFFFFFFFF,
            camera_pos_y_bits=0xFFFFFFFF,
            camera_pos_z_bits=0xFFFFFFFF,
            camera_mode=0xFFFFFFFF,
        )
        result = compare_reports(
            self._report(missing_camera),
            self._report(missing_camera),
            route,
        )
        self.assertFalse(result["deterministic"])
        self.assertTrue(any("camera" in error.lower() for error in result["errors"]))

    def test_stage2_comparator_hard_rejects_vdp1_arena_capacity_on_both_sides(self) -> None:
        route = load_route(TOOLS / "routes" / "bob_parity_v1.json")
        capacity_reject = self._probe(reject_vdp1_arena_capacity=1)
        result = compare_reports(
            self._report(capacity_reject),
            self._report(capacity_reject),
            route,
        )
        self.assertFalse(result["deterministic"])
        self.assertIn("left reject_vdp1_arena_capacity=1", result["errors"])
        self.assertIn("right reject_vdp1_arena_capacity=1", result["errors"])

    def test_stage2_comparator_requires_ordered_legacy_and_q16_roles(self) -> None:
        route = load_route(TOOLS / "routes" / "bob_parity_v1.json")
        left = self._report(self._probe(), "q16")
        right = self._report(self._probe(), "legacy")
        result = compare_reports(left, right, route)
        self.assertFalse(result["deterministic"])
        self.assertTrue(any("legacy" in error and "q16" in error for error in result["errors"]))

    def test_stage2_comparator_rejects_same_artifacts_for_both_roles(self) -> None:
        route = load_route(TOOLS / "routes" / "bob_parity_v1.json")
        left = self._report(self._probe(), "legacy")
        right = self._report(self._probe(), "q16")
        right["artifacts"] = json.loads(json.dumps(left["artifacts"]))
        result = compare_reports(left, right, route)
        self.assertFalse(result["deterministic"])
        self.assertTrue(any("artifact" in error.lower() for error in result["errors"]))

    def test_stage2_comparator_rejects_role_that_disagrees_with_raw_variant(self) -> None:
        route = load_route(TOOLS / "routes" / "bob_parity_v1.json")
        left = self._report(self._probe(), "legacy")
        right = self._report(self._probe(), "q16")
        left_probe = dict(left["probe_window"]["decoded"])  # type: ignore[index]
        left_probe["atan2_variant"] = 2
        left["probe_window"] = {
            "data": list(struct.pack(
                ">40I",
                *(left_probe[field] for field in self.SBR4_FIELDS),
            )),
            "decoded": left_probe,
        }
        try:
            result = compare_reports(left, right, route)
        except ValueError as error:
            self.fail(f"SBR4 raw atan2 variant is unsupported: {error}")
        self.assertFalse(result["deterministic"])
        self.assertTrue(any("raw atan2 variant" in error for error in result["errors"]))

    def test_comparator_rejects_missing_required_report_schema_field(self) -> None:
        route = load_route(TOOLS / "routes" / "bob_parity_v1.json")
        report = self._report(self._probe())
        del report["protocol"]
        with self.assertRaisesRegex(ValueError, "required schema fields: protocol"):
            compare_reports(report, report, route)

    def test_comparator_rejects_mismatched_degradation_settings(self) -> None:
        route = load_route(TOOLS / "routes" / "bob_parity_v1.json")
        left = self._report(self._probe())
        right = self._report(self._probe())
        right["degradation"] = {"view_radius": 2048, "poly_tier": 1}
        with self.assertRaisesRegex(ValueError, "degradation settings differ"):
            compare_reports(left, right, route)

    def test_comparator_rejects_unstated_degradation_settings(self) -> None:
        route = load_route(TOOLS / "routes" / "bob_parity_v1.json")
        left = self._report(self._probe())
        right = self._report(self._probe())
        left["degradation"] = {"view_radius": None, "poly_tier": None}
        with self.assertRaisesRegex(ValueError, "explicitly declared"):
            compare_reports(left, right, route)

    def test_comparator_rejects_decoded_probe_that_does_not_match_raw_bytes(self) -> None:
        route = load_route(TOOLS / "routes" / "bob_parity_v1.json")
        report = self._report(self._probe())
        report["probe_window"]["decoded"]["global_timer"] += 1  # type: ignore[index]
        with self.assertRaisesRegex(ValueError, "decoded route probe does not match raw bytes"):
            compare_reports(report, report, route)

    def test_comparator_rejects_malformed_artifact_hash(self) -> None:
        route = load_route(TOOLS / "routes" / "bob_parity_v1.json")
        report = self._report(self._probe())
        report["artifacts"]["elf"]["sha256"] = "not-a-sha256"  # type: ignore[index]
        with self.assertRaisesRegex(ValueError, "artifact elf sha256"):
            compare_reports(report, report, route)


class BobSkyBakeTests(unittest.TestCase):
    def test_bob_sky_bake_is_vdp2_sized_and_deterministic(self) -> None:
        source = TOOLS.parents[1] / "textures" / "skyboxes" / "water.png"
        first, manifest = bake_bob_sky(source)
        second, manifest_again = bake_bob_sky(source)
        self.assertEqual(first, second)
        self.assertEqual(manifest, manifest_again)
        self.assertEqual(len(first), 512 * 256 * 2)
        self.assertEqual(manifest["source_dimensions"], [248, 248])
        self.assertEqual(manifest["bitmap_dimensions"], [512, 256])
        self.assertEqual(manifest["format"], "RGB1555")
        self.assertTrue(manifest["edge_replication"])


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

    def test_sat0_magic_error_points_at_the_profile_decoder(self) -> None:
        # A sourceboot capture legitimately fails this check -- 0x06030000 is
        # unstamped HWRAM there. Two independent investigations read that
        # message as a broken tool and hand-decoded probe_window instead, so
        # the message must name the tool that does decode the renderer profile.
        with self.assertRaisesRegex(ValueError, r"fast3d_profile_decode\.py"):
            decode([0x21, 0x18, 0xDE, 0x5A] + [0] * 116, require_complete=False)


REPO_ROOT = TOOLS.parents[1]
EVIDENCE_REPORTS = REPO_ROOT / "docs" / "saturn" / "evidence" / "reports"
# Two committed captures, deliberately from different builds. The 2026-07-26
# one's counters were independently verified by hand; the 2026-07-25 one
# predates the quad-merge fields and is the version-mismatch case.
QUADMERGE_CAPTURE = EVIDENCE_REPORTS / "e2-sourceboot-quadmerge-freeroam-2026-07-26.json"
MARIO_CAPTURE = EVIDENCE_REPORTS / "e2-sourceboot-mario-freeroam-2026-07-25.json"
MARIO_CAPTURE_BYTES = 228


def _host_c_compiler() -> str | None:
    for candidate in (os.environ.get("HOST_CC"), os.environ.get("CC"), "cc", "gcc", "clang"):
        if candidate and shutil.which(candidate):
            # Yaul's build environment exports the SH-2 cross compiler as CC.
            # This probe compiles native host code and must never inherit that
            # target driver (which rejects the host compiler's march/mtune
            # defaults on Windows).
            compiler_name = Path(candidate).name.lower()
            if "sh-elf" in compiler_name or compiler_name.startswith("sh2"):
                continue
            try:
                target = subprocess.check_output(
                    [candidate, "-dumpmachine"], text=True,
                    stderr=subprocess.DEVNULL).strip().lower()
            except (OSError, subprocess.CalledProcessError):
                target = ""
            if "sh-elf" in target or target.startswith("sh2"):
                continue
            return candidate
    return None


def _load_capture(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


class Fast3dProfileLayoutTests(unittest.TestCase):
    """The layout must be derived from the real header, not written down.

    Counters are appended to sm64_saturn_fast3d_profile_t as features land
    (five in one week for quad merging alone). A decoder carrying its own copy
    of the offsets misreads silently the next time that happens, which is how
    the profile ended up being hand-decoded byte-by-byte twice.
    """

    def test_offsets_match_a_compiled_offsetof_probe(self) -> None:
        compiler = _host_c_compiler()
        if compiler is None:
            self.skipTest("no host C compiler on PATH (set CC to enable this cross-check)")
        layout = profile_layout()
        lines = [
            "#include <stddef.h>",
            "#include <stdio.h>",
            '#include "saturn_fast3d_frontend.h"',
            "int main(void) {",
            f'    printf("sizeof %zu\\n", sizeof({STRUCT_TYPEDEF}));',
        ]
        for field in layout.fields:
            lines.append(
                f'    printf("{field.name} %zu %zu\\n", '
                f"offsetof({STRUCT_TYPEDEF}, {field.name}), "
                f"sizeof((({STRUCT_TYPEDEF} *)0)->{field.name}));"
            )
        lines.extend(["    return 0;", "}"])
        with tempfile.TemporaryDirectory() as work_dir:
            work = Path(work_dir)
            source = work / "profile_offsets_probe.c"
            source.write_text("\n".join(lines) + "\n", encoding="utf-8")
            binary = work / ("probe.exe" if sys.platform == "win32" else "probe")
            compile_command = [
                compiler,
                "-std=c11",
                "-DNON_MATCHING=1",
                "-DAVOID_UB=1",
                "-D_LANGUAGE_C=1",
                "-DF3DEX_GBI_2E=1",
                f"-I{REPO_ROOT / 'include'}",
                f"-I{REPO_ROOT / 'src'}",
                f"-I{REPO_ROOT / 'src' / 'port' / 'saturn' / 'gfx'}",
                f"-I{REPO_ROOT / 'src' / 'port' / 'saturn' / 'platform'}",
                str(source),
                "-o",
                str(binary),
            ]
            host_env = os.environ.copy()
            # Yaul's cross-build exports GCC search-path variables that make a
            # native gcc driver load sh-elf's cc1.  The layout probe is a host
            # contract test, so remove target-toolchain injection explicitly.
            for variable in (
                "GCC_EXEC_PREFIX", "COMPILER_PATH", "LIBRARY_PATH",
                "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "CFLAGS",
                "CPPFLAGS", "LDFLAGS",
            ):
                host_env.pop(variable, None)
            compiled = subprocess.run(
                compile_command, capture_output=True, text=True, env=host_env)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            probe = subprocess.run(
                [str(binary)], capture_output=True, text=True,
                check=True, env=host_env)
        reported: dict[str, tuple[int, int]] = {}
        probe_size = None
        for line in probe.stdout.split("\n"):
            parts = line.split()
            if not parts:
                continue
            if parts[0] == "sizeof":
                probe_size = int(parts[1])
            else:
                reported[parts[0]] = (int(parts[1]), int(parts[2]))
        self.assertEqual(probe_size, layout.size)
        self.assertEqual(len(reported), len(layout.fields))
        for field in layout.fields:
            self.assertEqual(
                reported[field.name], (field.offset, field.size), f"offset drift at {field.name}"
            )

    def test_fields_are_ordered_naturally_aligned_and_non_overlapping(self) -> None:
        layout = profile_layout()
        previous_end = 0
        for field in layout.fields:
            self.assertGreaterEqual(field.offset, previous_end, field.name)
            self.assertEqual(field.offset % field.size, 0, field.name)
            self.assertLess(field.offset - previous_end, field.size, f"over-padded at {field.name}")
            previous_end = field.end
        self.assertGreaterEqual(layout.size, previous_end)
        self.assertEqual(layout.size % layout.alignment, 0)
        self.assertEqual(layout.fields[0].offset, 0)
        self.assertEqual(layout.fields[0].name, "frame_serial")

    def test_committed_capture_sizes_land_on_field_boundaries(self) -> None:
        # Every committed capture must be either exactly today's struct or an
        # exact prefix of it. A field INSERTED rather than appended would break
        # this, which is the drift this whole module is defending against.
        layout = profile_layout()
        boundaries = {field.offset for field in layout.fields} | {layout.size}
        for capture in (QUADMERGE_CAPTURE, MARIO_CAPTURE):
            captured = len(_load_capture(capture)["probe_window"]["data"])
            self.assertIn(captured, boundaries, f"{capture.name} is not a prefix of the struct")

    def test_layout_derives_from_the_real_header_path(self) -> None:
        self.assertTrue(PROFILE_HEADER.is_file())
        self.assertEqual(profile_layout().header, PROFILE_HEADER.resolve())

    def test_unsupported_members_are_a_loud_parse_error(self) -> None:
        # Each case pairs the bad member with two good ones, so a parser that
        # silently SKIPS what it cannot model -- shifting every later offset --
        # cannot pass by falling through to the "zero members" guard.
        for member, expected in (
            ("uint32_t counters[4]", r"counters\[4\]"),
            ("uint64_t wide_counter", "unmodelled type"),
            ("uint32_t *pointer", r"\*pointer"),
            ("struct nested inner", "unsupported member"),
            ("unsigned int plain", "unsupported member"),
        ):
            body = f"uint32_t frame_serial; {member}; uint16_t fault_flags;"
            with self.assertRaisesRegex(ValueError, expected, msg=member):
                parse_members(strip_c_comments(body))

    def test_a_body_with_no_members_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "zero members"):
            parse_members(strip_c_comments("/* nothing but a comment */"))

    def test_comment_text_cannot_be_parsed_as_members(self) -> None:
        body = """
            uint32_t frame_serial;
            /* a comment mentioning uint32_t ghost_counter; and more */
            uint16_t fault_flags;
        """
        self.assertEqual(
            parse_members(strip_c_comments(body)),
            (("uint32_t", "frame_serial"), ("uint16_t", "fault_flags")),
        )

    def test_alignment_padding_is_modelled(self) -> None:
        layout = layout_members(
            (("uint8_t", "a"), ("uint32_t", "b"), ("uint16_t", "c"))
        )
        self.assertEqual([field.offset for field in layout.fields], [0, 4, 8])
        self.assertEqual(layout.size, 12)  # tail padded to the 4-byte alignment
        self.assertEqual(layout.alignment, 4)


class Fast3dProfileDecodeTests(unittest.TestCase):
    def test_dual_pipeline_counters_append_and_decode(self) -> None:
        """The ownership evidence remains an appended, big-endian profile ABI."""
        layout = profile_layout()
        appended = (
            "master_worker_started",
            "slave_worker_started",
            "vdp1_commands",
            "vdp2_active_layers",
            "pipeline_faults",
            "gouraud_tables_saved",
            "gouraud_bytes_saved",
            "master_transform_count",
            "slave_transform_count",
            "ordering_count",
            "dma_wait_ticks_last",
            "dma_wait_ticks_accum",
            "vdp1_wait_ticks_last",
            "vdp1_wait_ticks_accum",
            "scene_graph_walks",
            "scene_graph_walks_suppressed",
            "vblank_presentation_generation",
            "sim_vblank_credit_dropped",
            "demo_render_clusters_tested",
            "demo_render_clusters_admitted",
            "demo_positions_admitted",
            "demo_positions_transformed",
            "demo_actor_meshlets_tested",
            "demo_actor_meshlets_admitted",
            "demo_actor_meshlets_culled",
            "demo_actor_positions_admitted",
        )
        self.assertEqual(tuple(field.name for field in layout.fields[-26:]), appended)
        self.assertEqual(
            layout.field("master_worker_started").offset,
            layout.fields[-27].end,
        )

        data = bytearray(layout.size)
        expected = {
            "master_worker_started": 1,
            "slave_worker_started": 1,
            "vdp1_commands": 0x1234,
            # VDP2 NBG1 (bit 1) plus the dbgio HUD on NBG3 (bit 3).
            "vdp2_active_layers": (1 << 1) | (1 << 3),
            "pipeline_faults": 3,
            "gouraud_tables_saved": 21,
            "gouraud_bytes_saved": 21 * 8,
            "master_transform_count": 22,
            "slave_transform_count": 23,
            "ordering_count": 0x4567,
            "dma_wait_ticks_last": 29,
            "dma_wait_ticks_accum": 31,
            "vdp1_wait_ticks_last": 37,
            "vdp1_wait_ticks_accum": 41,
            "scene_graph_walks": 43,
            "scene_graph_walks_suppressed": 47,
            "vblank_presentation_generation": 53,
            "sim_vblank_credit_dropped": 59,
            "demo_render_clusters_tested": 61,
            "demo_render_clusters_admitted": 67,
            "demo_positions_admitted": 71,
            "demo_positions_transformed": 73,
            "demo_actor_meshlets_tested": 79,
            "demo_actor_meshlets_admitted": 83,
            "demo_actor_meshlets_culled": 89,
            "demo_actor_positions_admitted": 97,
        }
        for name, value in expected.items():
            field = layout.field(name)
            self.assertEqual(field.size, 4, name)
            data[field.offset : field.end] = value.to_bytes(4, "big")
        fields = decode_profile(bytes(data))["fields"]
        for name, value in expected.items():
            self.assertEqual(fields[name], value)

    def test_quadmerge_capture_reproduces_its_verified_counters(self) -> None:
        # Independently hand-verified figures from
        # docs/saturn/PERFORMANCE_DIAGNOSIS_2026-07-26.md. partial=True so
        # appending a counter to the struct does not invalidate this fixture --
        # but INSERTING one shifts these values and fails the test, loudly.
        capture = _load_capture(QUADMERGE_CAPTURE)
        data, meta = load_probe(capture)
        self.assertEqual(len(data), 248)
        self.assertEqual(meta["probe_address"], 0x060BE918)
        decoded = decode_profile(data, partial=True)
        fields = decoded["fields"]
        self.assertEqual(fields["frame_serial"], 143)
        self.assertEqual(fields["triangles_emitted"], 913)
        self.assertEqual(fields["quads_merged"], 86)
        self.assertEqual(fields["triangles_vdp1_emitted"], 827)
        self.assertEqual(fields["quad_map_mismatch"], 0)
        self.assertEqual(fields["fault_flags"], 0)
        # Cross-checks on neighbouring fields, so a whole-struct shift that
        # happened to preserve one value cannot pass.
        self.assertEqual(fields["triangles_transformed"], 2311)
        self.assertEqual(fields["triangle_count"], 2311)
        self.assertEqual(fields["command_count"], 3453)

    def test_quadmerge_capture_decodes_strictly_against_todays_header(self) -> None:
        layout = profile_layout()
        data, _ = load_probe(_load_capture(QUADMERGE_CAPTURE))
        if len(data) != layout.size:
            self.skipTest(
                f"struct has moved on from this capture ({layout.size} vs {len(data)} bytes)"
            )
        decoded = decode_profile(data)
        self.assertTrue(decoded["complete"])
        self.assertEqual(decoded["fields_missing"], [])
        self.assertEqual(decoded["captured_bytes"], decoded["layout_bytes"])

    def test_older_capture_is_detectably_older_not_silently_misread(self) -> None:
        layout = profile_layout()
        data, _ = load_probe(_load_capture(MARIO_CAPTURE))
        self.assertEqual(len(data), MARIO_CAPTURE_BYTES)
        with self.assertRaises(ProfileSizeMismatch) as raised:
            decode_profile(data)
        message = str(raised.exception)
        self.assertIn(str(MARIO_CAPTURE_BYTES), message)
        self.assertIn(str(layout.size), message)
        self.assertIn("quads_merged", message)
        self.assertIn(STRUCT_TYPEDEF, message)

    def test_older_capture_partial_decode_names_what_is_absent(self) -> None:
        data, _ = load_probe(_load_capture(MARIO_CAPTURE))
        decoded = decode_profile(data, partial=True)
        self.assertFalse(decoded["complete"])
        self.assertEqual(
            decoded["fields_missing"],
            [
                "quads_merged",
                "quad_map_mismatch",
                "quad_pair_not_adjacent",
                "quad_ordinal_past_row",
                "quad_pairs_declined",
                "sim_frt_ticks_last",
                "sim_frt_ticks_accum",
                "sim_tick_count",
                "render_frt_ticks_last",
                "demo_actor_vertices_valid",
                "demo_actor_primitives_emitted",
                "demo_actor_snapshot_valid",
                "demo_actor_pose_vertices",
                "slave_jobs_completed",
                "slave_busy_ticks",
                "master_wait_ticks",
                "slave_timeouts",
                "render_frt_ticks_accum",
                "vdp1_commands_last",
                "vdp1_vram_bytes",
                "vdp2_display_mask",
                "vdp2_vram_bytes",
                "demo_bob_primitives_visible",
                "demo_bob_primitives_radius_rejected",
                "demo_bob_primitives_near_rejected",
                "demo_bob_primitives_degenerate",
                "demo_bob_nodes_visited",
                "demo_bob_nodes_inside",
                "demo_bob_nodes_intersecting",
                "demo_bob_nodes_outside",
                "demo_bob_primitives_spatial_admitted",
                "demo_bob_primitives_spatial_dropped",
                "demo_bob_clip_away",
                "demo_bob_clip_to_one",
                "demo_bob_clip_to_two",
                "demo_bob_clip_recovery",
                "demo_bob_clip_overflow",
                "demo_bob_results_master",
                "demo_bob_results_slave",
                "demo_bob_result_reserve_rejects",
                "vdp1_bank_generation",
                "vdp1_bank_submitted",
                "vdp1_bank_displayed",
                "vdp1_bank_overwrite_attempts",
                "vdp1_bank_late_dma",
                "vdp1_command_highwater",
                "vdp1_gouraud_highwater",
                "demo_lod_tier_near",
                "demo_lod_tier_mid",
                "demo_lod_tier_far",
                "demo_lod_transitions",
                "demo_lod_primitives_suppressed",
                "demo_lod_texture_downgrades",
                "demo_lod_resident_bytes",
                "flat_primitives",
                "gouraud_primitives",
                "demo_bob_terrain_descriptor_bytes_written",
                "demo_bob_terrain_descriptor_bytes_read",
                "demo_bob_terrain_legacy_fallbacks",
                "demo_bob_terrain_sequence_rejects",
                "master_worker_started",
                "slave_worker_started",
                "vdp1_commands",
                "vdp2_active_layers",
                "pipeline_faults",
                "gouraud_tables_saved",
                "gouraud_bytes_saved",
                "master_transform_count",
                "slave_transform_count",
                "ordering_count",
                "dma_wait_ticks_last",
                "dma_wait_ticks_accum",
                "vdp1_wait_ticks_last",
                "vdp1_wait_ticks_accum",
                "scene_graph_walks",
                "scene_graph_walks_suppressed",
                "vblank_presentation_generation",
                "sim_vblank_credit_dropped",
                "demo_render_clusters_tested",
                "demo_render_clusters_admitted",
                "demo_positions_admitted",
                "demo_positions_transformed",
                "demo_actor_meshlets_tested",
                "demo_actor_meshlets_admitted",
                "demo_actor_meshlets_culled",
                "demo_actor_positions_admitted",
            ],
        )
        # Fields the older build did have still read correctly.
        self.assertEqual(decoded["fields"]["frame_serial"], 143)
        self.assertEqual(decoded["fields"]["triangles_emitted"], 771)
        self.assertEqual(decoded["fields"]["triangles_transformed"], 2011)
        self.assertNotIn("quads_merged", decoded["fields"])

    def test_trailing_bytes_are_rejected_and_counted(self) -> None:
        layout = profile_layout()
        with self.assertRaises(ProfileSizeMismatch) as raised:
            decode_profile(bytes(layout.size + 8))
        message = str(raised.exception)
        self.assertIn(str(layout.size + 8), message)
        self.assertIn(str(layout.size), message)
        self.assertIn("8 trailing byte", message)

    def test_empty_capture_is_rejected(self) -> None:
        with self.assertRaises(ProfileSizeMismatch):
            decode_profile(b"")

    def test_words_are_big_endian(self) -> None:
        layout = profile_layout()
        data = bytearray(layout.size)
        offset = layout.field("frame_serial").offset
        data[offset : offset + 4] = b"\x00\x00\x01\x00"
        # 256 big-endian; 65536 little-endian. Both are plausible frame
        # serials, so this distinguishes the two without relying on garbage.
        self.assertEqual(decode_profile(bytes(data))["fields"]["frame_serial"], 256)

    def test_field_types_are_honoured_not_flattened_to_uint32(self) -> None:
        layout = profile_layout()
        data = bytearray(layout.size)

        def poke(name: str, payload: bytes) -> None:
            field = layout.field(name)
            self.assertEqual(field.size, len(payload), name)
            data[field.offset : field.end] = payload

        poke("max_call_depth", b"\xff\xff")  # uint16_t
        poke("dbg_first_reject_min_x", b"\xff\xff")  # int16_t
        poke("dbg_first_reject_min_z", b"\xff\xff\xff\xff")  # int32_t
        poke("dbg_first_w_reject_mx", b"\x3f\x80\x00\x00")  # float
        poke("dbg_bad_mtx_params", b"\xff")  # uint8_t
        poke("command_count", b"\xff\xff\xff\xff")  # uint32_t
        fields = decode_profile(bytes(data))["fields"]
        self.assertEqual(fields["max_call_depth"], 65535)
        self.assertEqual(fields["dbg_first_reject_min_x"], -1)
        self.assertEqual(fields["dbg_first_reject_min_z"], -1)
        self.assertEqual(fields["dbg_first_w_reject_mx"], 1.0)
        self.assertEqual(fields["dbg_bad_mtx_params"], 255)
        self.assertEqual(fields["command_count"], 4294967295)

    def test_every_header_field_is_reported(self) -> None:
        layout = profile_layout()
        decoded = decode_profile(bytes(layout.size))
        self.assertEqual(set(decoded["fields"]), {field.name for field in layout.fields})
        self.assertEqual(len(decoded["fields"]), len(layout.fields))

    def test_load_probe_accepts_every_capture_shape(self) -> None:
        payload = list(range(8))
        self.assertEqual(load_probe(payload)[0], payload)
        report, meta = load_probe(
            {"probe_window": {"address": 0x060BE918, "data": payload, "target": "sh2.master"}}
        )
        self.assertEqual(report, payload)
        self.assertEqual(meta["probe_address"], 0x060BE918)
        self.assertEqual(meta["probe_target"], "sh2.master")
        peek, _ = load_probe({"jsonrpc": "2.0", "result": {"data": payload}})
        self.assertEqual(peek, payload)

    def test_capture_without_a_probe_window_is_explained(self) -> None:
        with self.assertRaisesRegex(ValueError, "--probe-address"):
            load_probe({"probe_window": None, "telemetry": {}})

    def test_out_of_range_bytes_are_rejected(self) -> None:
        layout = profile_layout()
        with self.assertRaisesRegex(ValueError, "0\\.\\.255"):
            decode_profile([300] * layout.size)


class DisplayListRigidGroupTests(unittest.TestCase):
    def test_single_list_all_one_rigid_group(self) -> None:
        lists = {
            "body": [
                ("gsSPVertex", "v_body, 3, 0"),
                ("gsSP1Triangle", "0, 1, 2, 0"),
                ("gsSP1Triangle", "0, 2, 1, 0"),
            ]
        }
        sites = walk_display_lists(lists, "body")
        self.assertEqual([s.ordinal for s in sites], [0, 1])
        self.assertEqual(sites[0].rigid_group, sites[1].rigid_group)

    def test_push_pop_creates_distinct_groups(self) -> None:
        """Triangles under different matrix pushes must never share a group."""
        lists = {
            "root": [
                ("gsSPVertex", "v_a, 3, 0"),
                ("gsSP1Triangle", "0, 1, 2, 0"),
                ("gsSPMatrix", "arm_mtx, G_MTX_MODELVIEW | G_MTX_PUSH"),
                ("gsSPVertex", "v_b, 3, 0"),
                ("gsSP1Triangle", "0, 1, 2, 0"),
                ("gsSPPopMatrix", "G_MTX_MODELVIEW"),
                ("gsSP1Triangle", "0, 2, 1, 0"),
            ]
        }
        sites = walk_display_lists(lists, "root")
        self.assertEqual([s.ordinal for s in sites], [0, 1, 2])
        self.assertNotEqual(sites[0].rigid_group, sites[1].rigid_group)
        self.assertEqual(sites[0].rigid_group, sites[2].rigid_group,
                         "pop must restore the prior group")

    def test_gssp2triangles_emits_two_ordinals(self) -> None:
        lists = {
            "body": [
                ("gsSPVertex", "v, 4, 0"),
                ("gsSP2Triangles", "0, 1, 2, 0, 0, 2, 3, 0"),
            ]
        }
        sites = walk_display_lists(lists, "body")
        self.assertEqual([s.ordinal for s in sites], [0, 1])
        self.assertEqual(sites[0].indices, (0, 1, 2))
        self.assertEqual(sites[1].indices, (0, 2, 3))

    def test_nested_display_list_continues_ordinals_and_inherits_group(self) -> None:
        lists = {
            "root": [
                ("gsSPVertex", "v, 3, 0"),
                ("gsSP1Triangle", "0, 1, 2, 0"),
                ("gsSPDisplayList", "child"),
            ],
            "child": [
                ("gsSPVertex", "v2, 3, 0"),
                ("gsSP1Triangle", "0, 1, 2, 0"),
            ],
        }
        sites = walk_display_lists(lists, "root")
        self.assertEqual([s.ordinal for s in sites], [0, 1])
        self.assertEqual(sites[0].rigid_group, sites[1].rigid_group)

    def test_unknown_macro_marks_sites_unsafe(self) -> None:
        """An unmodelled construct must poison the group, never be ignored."""
        lists = {
            "root": [
                ("gsSPVertex", "v, 3, 0"),
                ("gsSPBranchList", "somewhere_else"),
                ("gsSP1Triangle", "0, 1, 2, 0"),
            ]
        }
        sites = walk_display_lists(lists, "root")
        self.assertIs(sites[0].unsafe, True)
        self.assertIn(REASON_UNKNOWN_MACRO, sites[0].reasons)

    def test_matrix_without_push_does_not_restore_on_pop(self) -> None:
        """gsSPMatrix without G_MTX_PUSH must not push the walker's stack.

        Treating every gsSPMatrix as a push desyncs the walker from the
        hardware by one entry, which lets a later gsSPPopMatrix hand a
        triangle the group of a matrix that is no longer loaded.
        """
        lists = {
            "root": [
                ("gsSPVertex", "v_a, 3, 0"),
                ("gsSP1Triangle", "0, 1, 2, 0"),
                ("gsSPMatrix", "arm_mtx, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH"),
                ("gsSP1Triangle", "0, 1, 2, 0"),
                ("gsSPPopMatrix", "G_MTX_MODELVIEW"),
                ("gsSP1Triangle", "0, 2, 1, 0"),
            ]
        }
        sites = walk_display_lists(lists, "root")
        self.assertEqual([s.ordinal for s in sites], [0, 1, 2])
        self.assertNotEqual(sites[0].rigid_group, sites[1].rigid_group,
                            "a loaded matrix still changes the transform")
        self.assertNotEqual(sites[2].rigid_group, sites[0].rigid_group,
                            "a non-pushing matrix must not be restorable")
        self.assertIs(sites[2].unsafe, True)

    def test_pop_matrix_on_empty_stack_poisons(self) -> None:
        lists = {
            "root": [
                ("gsSPVertex", "v, 3, 0"),
                ("gsSPPopMatrix", "G_MTX_MODELVIEW"),
                ("gsSP1Triangle", "0, 1, 2, 0"),
            ]
        }
        sites = walk_display_lists(lists, "root")
        self.assertIs(sites[0].unsafe, True)
        self.assertIn(REASON_UNBALANCED_POP, sites[0].reasons)

    def test_render_state_macros_are_rigid_group_neutral(self) -> None:
        """Texture-load macros do not touch the modelview matrix."""
        lists = {
            "root": [
                ("gsDPPipeSync", ""),
                ("gsDPSetTextureImage", "G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, mario_texture_eye"),
                ("gsDPSetTile", "G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0"),
                ("gsDPLoadSync", ""),
                ("gsDPLoadBlock", "G_TX_LOADTILE, 0, 0, 1023, 256"),
                ("gsDPTileSync", ""),
                ("gsDPSetTileSize", "0, 0, 0, 124, 124"),
                ("gsDPSetEnvColor", "255, 255, 255, 255"),
                ("gsDPSetAlphaCompare", "G_AC_THRESHOLD"),
                ("gsSPTexture", "0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF"),
                ("gsSPVertex", "v, 3, 0"),
                ("gsSP1Triangle", "0, 1, 2, 0"),
            ]
        }
        sites = walk_display_lists(lists, "root")
        self.assertNotIn(REASON_UNKNOWN_MACRO, sites[0].reasons)
        self.assertIs(sites[0].unsafe, False)

    def test_textured_triangles_are_unsafe_for_their_own_reason(self) -> None:
        """Textured sites are excluded, but not because a macro was unknown."""
        lists = {
            "root": [
                ("gsSPVertex", "v, 3, 0"),
                ("gsSP1Triangle", "0, 1, 2, 0"),
                ("gsDPSetTextureImage", "G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, mario_texture_eye"),
                ("gsSPTexture", "0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON"),
                ("gsSP1Triangle", "0, 1, 2, 0"),
                ("gsSPTexture", "0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF"),
                ("gsSP1Triangle", "0, 2, 1, 0"),
            ]
        }
        sites = walk_display_lists(lists, "root")
        self.assertIs(sites[0].unsafe, False)
        self.assertIs(sites[1].unsafe, True)
        self.assertEqual(sites[1].reasons, frozenset({REASON_TEXTURED}))
        self.assertIs(sites[2].unsafe, False)

    def test_load_texture_block_also_marks_triangles_textured(self) -> None:
        """The compound macro takes its image as the FIRST argument."""
        lists = {
            "root": [
                ("gsDPLoadTextureBlock", "mario_texture_metal, G_IM_FMT_RGBA, "
                                         "G_IM_SIZ_16b, 64, 32, 0"),
                ("gsSPVertex", "v, 3, 0"),
                ("gsSP1Triangle", "0, 1, 2, 0"),
            ]
        }
        site, = walk_display_lists(lists, "root")
        self.assertIs(site.unsafe, True)
        self.assertIn(REASON_TEXTURED, site.reasons)

    def test_recursive_display_list_raises(self) -> None:
        lists = {
            "root": [("gsSPDisplayList", "child")],
            "child": [("gsSPDisplayList", "root")],
        }
        with self.assertRaisesRegex(ValueError, "recursive display list"):
            walk_display_lists(lists, "root")


_ONE_TRIANGLE = [("gsSPVertex", "v, 3, 0"), ("gsSP1Triangle", "0, 1, 2, 0")]


class GeoLayoutRigidGroupTests(unittest.TestCase):
    """Mario's joints live in the geo layout; actors contain no gsSPMatrix."""

    LISTS = {
        "dl_a": list(_ONE_TRIANGLE),
        "dl_b": list(_ONE_TRIANGLE),
        "dl_c": list(_ONE_TRIANGLE),
        "dl_d": list(_ONE_TRIANGLE),
    }

    def test_display_lists_under_one_animated_part_share_a_group(self) -> None:
        layouts = {
            "root": [
                ("GEO_ANIMATED_PART", "LAYER_OPAQUE, 0, 0, 0, NULL"),
                ("GEO_OPEN_NODE", ""),
                ("GEO_DISPLAY_LIST", "LAYER_OPAQUE, dl_a"),
                ("GEO_DISPLAY_LIST", "LAYER_OPAQUE, dl_b"),
                ("GEO_CLOSE_NODE", ""),
            ]
        }
        sites = walk_geo_layout(layouts, self.LISTS, "root")
        self.assertEqual([s.ordinal for s in sites], [0, 1])
        self.assertEqual(sites[0].rigid_group, sites[1].rigid_group)
        self.assertFalse(any(s.unsafe for s in sites))

    def test_display_lists_under_different_animated_parts_differ(self) -> None:
        layouts = {
            "root": [
                ("GEO_ANIMATED_PART", "LAYER_OPAQUE, 0, 0, 0, NULL"),
                ("GEO_OPEN_NODE", ""),
                ("GEO_DISPLAY_LIST", "LAYER_OPAQUE, dl_a"),
                ("GEO_ANIMATED_PART", "LAYER_OPAQUE, 65, 0, 0, NULL"),
                ("GEO_OPEN_NODE", ""),
                ("GEO_DISPLAY_LIST", "LAYER_OPAQUE, dl_b"),
                ("GEO_CLOSE_NODE", ""),
                ("GEO_CLOSE_NODE", ""),
            ]
        }
        sites = walk_geo_layout(layouts, self.LISTS, "root")
        self.assertNotEqual(sites[0].rigid_group, sites[1].rigid_group)

    def test_close_node_restores_the_enclosing_group(self) -> None:
        """A sibling after GEO_CLOSE_NODE rejoins the parent joint's group."""
        layouts = {
            "root": [
                ("GEO_ANIMATED_PART", "LAYER_OPAQUE, 0, 0, 0, NULL"),
                ("GEO_OPEN_NODE", ""),
                ("GEO_DISPLAY_LIST", "LAYER_OPAQUE, dl_a"),
                ("GEO_ANIMATED_PART", "LAYER_OPAQUE, 65, 0, 0, NULL"),
                ("GEO_OPEN_NODE", ""),
                ("GEO_DISPLAY_LIST", "LAYER_OPAQUE, dl_b"),
                ("GEO_CLOSE_NODE", ""),
                ("GEO_DISPLAY_LIST", "LAYER_OPAQUE, dl_c"),
                ("GEO_CLOSE_NODE", ""),
            ]
        }
        outer, inner, after = walk_geo_layout(layouts, self.LISTS, "root")
        self.assertNotEqual(outer.rigid_group, inner.rigid_group)
        self.assertEqual(outer.rigid_group, after.rigid_group,
                         "GEO_CLOSE_NODE must restore the prior group")

    def test_animated_part_bound_list_uses_its_own_group(self) -> None:
        layouts = {
            "root": [
                ("GEO_ANIMATED_PART", "LAYER_OPAQUE, 0, 0, 0, dl_a"),
                ("GEO_OPEN_NODE", ""),
                ("GEO_ANIMATED_PART", "LAYER_OPAQUE, 65, 0, 0, dl_b"),
                ("GEO_CLOSE_NODE", ""),
            ]
        }
        sites = walk_geo_layout(layouts, self.LISTS, "root")
        self.assertEqual(len(sites), 2)
        self.assertNotEqual(sites[0].rigid_group, sites[1].rigid_group)

    def test_each_switch_case_gets_its_own_rigid_group(self) -> None:
        """Exactly one direct child of a switch renders (and its siblings do
        not -- rendering_graph_node.c:1288). So a case's own triangles are
        always drawn together and share the parent transform, while two cases
        are kept apart by group separation rather than by poisoning."""
        layouts = {
            "root": [
                ("GEO_SWITCH_CASE", "0, geo_switch_mario_eyes"),
                ("GEO_OPEN_NODE", ""),
                ("GEO_DISPLAY_LIST", "LAYER_OPAQUE, dl_a"),   # case 0
                ("GEO_OPEN_NODE", ""),
                ("GEO_DISPLAY_LIST", "LAYER_OPAQUE, dl_b"),   # child of case 0
                ("GEO_CLOSE_NODE", ""),
                ("GEO_DISPLAY_LIST", "LAYER_OPAQUE, dl_c"),   # case 1
                ("GEO_CLOSE_NODE", ""),
            ]
        }
        first, within, other = walk_geo_layout(layouts, self.LISTS, "root")
        self.assertFalse(any(s.unsafe for s in (first, within, other)),
                         "a switch case is no longer poisoned")
        self.assertEqual(first.rigid_group, within.rigid_group,
                         "one case's own subtree shares its group")
        self.assertNotEqual(first.rigid_group, other.rigid_group,
                            "two cases must never merge")

    def test_nested_switch_cases_stay_separated(self) -> None:
        layouts = {
            "root": [
                ("GEO_SWITCH_CASE", "0, geo_switch_mario_cap_on_off"),
                ("GEO_OPEN_NODE", ""),
                ("GEO_SWITCH_CASE", "0, geo_switch_mario_eyes"),  # outer case 0
                ("GEO_OPEN_NODE", ""),
                ("GEO_DISPLAY_LIST", "LAYER_OPAQUE, dl_a"),       # inner case 0
                ("GEO_DISPLAY_LIST", "LAYER_OPAQUE, dl_b"),       # inner case 1
                ("GEO_CLOSE_NODE", ""),
                ("GEO_DISPLAY_LIST", "LAYER_OPAQUE, dl_c"),       # outer case 1
                ("GEO_CLOSE_NODE", ""),
            ]
        }
        sites = walk_geo_layout(layouts, self.LISTS, "root")
        self.assertFalse(any(s.unsafe for s in sites))
        self.assertEqual(len({s.rigid_group for s in sites}), 3,
                         "every case at every depth is its own group")

    def test_group_returns_to_the_parent_joint_after_a_switch(self) -> None:
        layouts = {
            "root": [
                ("GEO_ANIMATED_PART", "LAYER_OPAQUE, 0, 0, 0, dl_a"),
                ("GEO_OPEN_NODE", ""),
                ("GEO_SWITCH_CASE", "0, geo_switch_mario_eyes"),
                ("GEO_OPEN_NODE", ""),
                ("GEO_DISPLAY_LIST", "LAYER_OPAQUE, dl_b"),
                ("GEO_CLOSE_NODE", ""),
                ("GEO_DISPLAY_LIST", "LAYER_OPAQUE, dl_c"),
                ("GEO_CLOSE_NODE", ""),
            ]
        }
        joint, case, after = walk_geo_layout(layouts, self.LISTS, "root")
        self.assertNotEqual(joint.rigid_group, case.rigid_group)
        self.assertEqual(joint.rigid_group, after.rigid_group,
                         "the switch must not consume the parent's group")

    def test_geo_asm_still_poisons_inside_a_switch_case(self) -> None:
        """Per-case grouping does not extend to arbitrary runtime callbacks."""
        layouts = {
            "root": [
                ("GEO_SWITCH_CASE", "0, geo_switch_mario_hand"),
                ("GEO_OPEN_NODE", ""),
                ("GEO_ASM", "0, geo_mario_hand_foot_scaler"),
                ("GEO_OPEN_NODE", ""),
                ("GEO_DISPLAY_LIST", "LAYER_OPAQUE, dl_a"),
                ("GEO_CLOSE_NODE", ""),
                ("GEO_DISPLAY_LIST", "LAYER_OPAQUE, dl_b"),
                ("GEO_CLOSE_NODE", ""),
            ]
        }
        inside, sibling_case = walk_geo_layout(layouts, self.LISTS, "root")
        self.assertIs(inside.unsafe, True)
        self.assertIn(REASON_GEO_ASM, inside.reasons)
        self.assertIs(sibling_case.unsafe, False)
        self.assertNotEqual(inside.rigid_group, sibling_case.rigid_group)

    def test_geo_asm_subtree_is_unsafe(self) -> None:
        layouts = {
            "root": [
                ("GEO_ANIMATED_PART", "LAYER_OPAQUE, 0, 0, 0, NULL"),
                ("GEO_OPEN_NODE", ""),
                ("GEO_ASM", "0, geo_mario_hand_foot_scaler"),
                ("GEO_OPEN_NODE", ""),
                ("GEO_DISPLAY_LIST", "LAYER_OPAQUE, dl_a"),
                ("GEO_CLOSE_NODE", ""),
                ("GEO_DISPLAY_LIST", "LAYER_OPAQUE, dl_b"),
                ("GEO_CLOSE_NODE", ""),
            ]
        }
        inside, outside = walk_geo_layout(layouts, self.LISTS, "root")
        self.assertIs(inside.unsafe, True)
        self.assertIn(REASON_GEO_ASM, inside.reasons)
        self.assertIs(outside.unsafe, False)

    def test_unknown_geo_node_poisons_its_subtree(self) -> None:
        layouts = {
            "root": [
                ("GEO_RENDER_RANGE", "-2048, 600"),
                ("GEO_OPEN_NODE", ""),
                ("GEO_DISPLAY_LIST", "LAYER_OPAQUE, dl_a"),
                ("GEO_CLOSE_NODE", ""),
            ]
        }
        site, = walk_geo_layout(layouts, self.LISTS, "root")
        self.assertIs(site.unsafe, True)
        self.assertIn(REASON_UNKNOWN_GEO_NODE, site.reasons)

    def test_ordinals_are_contiguous_in_walk_order(self) -> None:
        layouts = {
            "root": [
                ("GEO_ANIMATED_PART", "LAYER_OPAQUE, 0, 0, 0, dl_a"),
                ("GEO_OPEN_NODE", ""),
                ("GEO_DISPLAY_LIST", "LAYER_OPAQUE, dl_b"),
                ("GEO_ANIMATED_PART", "LAYER_OPAQUE, 65, 0, 0, dl_c"),
                ("GEO_CLOSE_NODE", ""),
            ]
        }
        lists = {
            "dl_a": [("gsSPVertex", "v, 4, 0"),
                     ("gsSP2Triangles", "0, 1, 2, 0, 0, 2, 3, 0")],
            "dl_b": list(_ONE_TRIANGLE),
            "dl_c": list(_ONE_TRIANGLE),
        }
        sites = walk_geo_layout(layouts, lists, "root")
        self.assertEqual([s.ordinal for s in sites], [0, 1, 2, 3])
        self.assertEqual([s.display_list for s in sites],
                         ["dl_a", "dl_a", "dl_b", "dl_c"])
        # list_ordinal restarts per display list: it is the only key that
        # survives switch/LOD selection and per-layer master-list bucketing.
        self.assertEqual([s.list_ordinal for s in sites], [0, 1, 0, 0])

    def test_branch_returns_to_the_calling_layout(self) -> None:
        layouts = {
            "root": [
                ("GEO_ANIMATED_PART", "LAYER_OPAQUE, 0, 0, 0, NULL"),
                ("GEO_OPEN_NODE", ""),
                ("GEO_BRANCH", "1, sub"),
                ("GEO_DISPLAY_LIST", "LAYER_OPAQUE, dl_c"),
                ("GEO_CLOSE_NODE", ""),
            ],
            "sub": [
                ("GEO_DISPLAY_LIST", "LAYER_OPAQUE, dl_a"),
                ("GEO_ANIMATED_PART", "LAYER_OPAQUE, 65, 0, 0, dl_b"),
                ("GEO_RETURN", ""),
            ],
        }
        shared, joint, after = walk_geo_layout(layouts, self.LISTS, "root")
        self.assertEqual([s.display_list for s in (shared, joint, after)],
                         ["dl_a", "dl_b", "dl_c"])
        self.assertEqual(shared.rigid_group, after.rigid_group,
                         "a branched layout is inlined at the call site")
        self.assertNotEqual(joint.rigid_group, shared.rigid_group)

    def test_recursive_geo_layout_raises(self) -> None:
        layouts = {
            "root": [("GEO_BRANCH", "1, sub")],
            "sub": [("GEO_BRANCH", "1, root")],
        }
        with self.assertRaisesRegex(ValueError, "recursive geo layout"):
            walk_geo_layout(layouts, self.LISTS, "root")

    def test_stats_separate_unsafe_reasons(self) -> None:
        layouts = {
            "root": [
                ("GEO_ANIMATED_PART", "LAYER_OPAQUE, 0, 0, 0, NULL"),
                ("GEO_OPEN_NODE", ""),
                ("GEO_DISPLAY_LIST", "LAYER_OPAQUE, dl_a"),
                ("GEO_ASM", "0, geo_mario_head_rotation"),
                ("GEO_OPEN_NODE", ""),
                ("GEO_DISPLAY_LIST", "LAYER_OPAQUE, dl_b"),
                ("GEO_CLOSE_NODE", ""),
                ("GEO_CLOSE_NODE", ""),
            ]
        }
        stats = rigid_group_stats(walk_geo_layout(layouts, self.LISTS, "root"))
        self.assertEqual(stats["triangle_sites"], 2)
        self.assertEqual(stats["rigid_groups"], 1)
        self.assertEqual(stats["unsafe_sites"], 1)
        self.assertEqual(stats["unsafe_by_reason"][REASON_GEO_ASM], 1)


class QuadMapTests(unittest.TestCase):
    """The quad map is the intersection of a structural and a geometric gate.

    Structural: both triangles carry the same rigid group in every
    instantiation of their display list, and neither is unsafe. Geometric:
    quad_pairing's shared-edge/winding/normal/convexity checks.
    """

    #: A unit square split along its diagonal: ids 0..3.
    SQUARE = [(0, 0, 0), (10, 0, 0), (10, 10, 0), (0, 10, 0)]

    @staticmethod
    def _site(
        display_list: str,
        list_ordinal: int,
        rigid_group: int,
        indices: tuple[int, int, int] = (0, 1, 2),
        unsafe: bool = False,
        reasons: frozenset[str] = frozenset(),
        ordinal: int = 0,
    ) -> TriangleSite:
        return TriangleSite(
            ordinal=ordinal,
            rigid_group=rigid_group,
            indices=indices,
            unsafe=unsafe,
            reasons=reasons,
            display_list=display_list,
            list_ordinal=list_ordinal,
        )

    @staticmethod
    def _row(
        x: int, y: int, z: int,
        normal: tuple[int, int, int, int] = (0, 0, 0, 0),
        uv: tuple[int, int] = (0, 0),
        flag: int = 0,
    ) -> tuple[int, ...]:
        """One complete Vtx row: {{{x,y,z}, flag, {u,v}, {n0,n1,n2,n3}}}."""
        return (x, y, z, flag, uv[0], uv[1], *normal)

    @staticmethod
    def _entry(entries: list[QuadMapEntry], display_list: str, list_ordinal: int) -> QuadMapEntry:
        found = [
            entry for entry in entries
            if entry.display_list == display_list and entry.list_ordinal == list_ordinal
        ]
        if len(found) != 1:
            raise AssertionError(
                f"expected exactly one entry for {(display_list, list_ordinal)}, got {len(found)}"
            )
        return found[0]

    @staticmethod
    def _partner(entries: list[QuadMapEntry], display_list: str, list_ordinal: int) -> int | None:
        found = [
            entry for entry in entries
            if entry.display_list == display_list and entry.list_ordinal == list_ordinal
        ]
        if len(found) != 1:
            raise AssertionError(
                f"expected exactly one entry for {(display_list, list_ordinal)}, got {len(found)}"
            )
        return found[0].partner_list_ordinal

    def _split_square(self, display_list: str = "dl", groups: tuple[int, int] = (1, 1)):
        """Two coplanar triangles sharing the 0-2 diagonal of SQUARE."""
        sites = [
            self._site(display_list, 0, groups[0], ordinal=0),
            self._site(display_list, 1, groups[1], ordinal=1),
        ]
        triangle_vertices = {
            (display_list, 0): (0, 1, 2),
            (display_list, 1): (0, 2, 3),
        }
        return sites, triangle_vertices

    def test_same_rigid_group_coplanar_pair_merges(self) -> None:
        sites, triangle_vertices = self._split_square()
        entries, stats = build_quad_map(sites, triangle_vertices, self.SQUARE)
        self.assertEqual(stats["quad_count"], 1)
        self.assertEqual(stats["commands_saved"], 1)
        self.assertEqual(self._partner(entries, "dl", 0), 1)
        self.assertEqual(self._partner(entries, "dl", 1), 0)

    def test_identical_geometry_in_different_rigid_groups_never_merges(self) -> None:
        """The core safety property: two joints can pull the quad apart.

        The geometry here is byte-identical to the merging case above; only
        the rigid group differs. If this ever merges, animation tears it.
        """
        merged, triangle_vertices = self._split_square(groups=(1, 1))
        split, _ = self._split_square(groups=(1, 2))
        self.assertEqual(
            [s.indices for s in merged], [s.indices for s in split],
            "the two fixtures must differ only in rigid group",
        )
        _, merged_stats = build_quad_map(merged, triangle_vertices, self.SQUARE)
        entries, stats = build_quad_map(split, triangle_vertices, self.SQUARE)
        self.assertEqual(merged_stats["quad_count"], 1, "control case must merge")
        self.assertEqual(stats["quad_count"], 0)
        self.assertEqual(stats["commands_saved"], 0)
        self.assertIsNone(self._partner(entries, "dl", 0))
        self.assertIsNone(self._partner(entries, "dl", 1))

    def test_unsafe_site_is_never_paired(self) -> None:
        sites, triangle_vertices = self._split_square()
        sites[1] = self._site("dl", 1, 1, unsafe=True,
                              reasons=frozenset({REASON_TEXTURED}), ordinal=1)
        entries, stats = build_quad_map(sites, triangle_vertices, self.SQUARE)
        self.assertEqual(stats["quad_count"], 0)
        self.assertIsNone(self._partner(entries, "dl", 0))
        self.assertIsNone(self._partner(entries, "dl", 1))
        self.assertEqual(stats["ineligible_by_reason"][REASON_TEXTURED], 1)

    def test_unsafe_in_any_instantiation_blocks_the_key(self) -> None:
        """One display list bound under two geo nodes: unsafe once is unsafe."""
        sites, triangle_vertices = self._split_square()
        sites.append(self._site("dl", 0, 7, unsafe=True,
                                reasons=frozenset({REASON_GEO_ASM}), ordinal=2))
        sites.append(self._site("dl", 1, 7, ordinal=3))
        _, stats = build_quad_map(sites, triangle_vertices, self.SQUARE)
        self.assertEqual(stats["quad_count"], 0)
        self.assertEqual(stats["ineligible_by_reason"][REASON_GEO_ASM], 1)

    def test_rigid_groups_must_agree_in_every_instantiation(self) -> None:
        """Same groups per instantiation merges; a divergent one does not."""
        agree = [
            self._site("dl", 0, 1, ordinal=0), self._site("dl", 1, 1, ordinal=1),
            self._site("dl", 0, 5, ordinal=2), self._site("dl", 1, 5, ordinal=3),
        ]
        diverge = [
            self._site("dl", 0, 1, ordinal=0), self._site("dl", 1, 1, ordinal=1),
            self._site("dl", 0, 5, ordinal=2), self._site("dl", 1, 6, ordinal=3),
        ]
        _, triangle_vertices = self._split_square()
        _, agree_stats = build_quad_map(agree, triangle_vertices, self.SQUARE)
        _, diverge_stats = build_quad_map(diverge, triangle_vertices, self.SQUARE)
        self.assertEqual(agree_stats["quad_count"], 1)
        self.assertEqual(diverge_stats["quad_count"], 0)

    def test_every_site_gets_exactly_one_entry(self) -> None:
        sites = [
            self._site("dl_a", 0, 1, ordinal=0),
            self._site("dl_a", 1, 1, ordinal=1),
            self._site("dl_b", 0, 2, ordinal=2),
            self._site("dl_b", 1, 2, ordinal=3),
            # dl_a bound a second time under another joint: same keys again.
            self._site("dl_a", 0, 9, ordinal=4),
            self._site("dl_a", 1, 9, ordinal=5),
        ]
        triangle_vertices = {
            ("dl_a", 0): (0, 1, 2), ("dl_a", 1): (0, 2, 3),
            ("dl_b", 0): (0, 1, 2), ("dl_b", 1): (0, 2, 3),
        }
        entries, stats = build_quad_map(sites, triangle_vertices, self.SQUARE)
        keys = [(entry.display_list, entry.list_ordinal) for entry in entries]
        self.assertEqual(len(keys), len(set(keys)), "entries must be unique per key")
        for site in sites:
            self.assertEqual(
                sum(1 for key in keys
                    if key == (site.display_list, site.list_ordinal)), 1,
                f"site {(site.display_list, site.list_ordinal)} needs exactly one entry",
            )
        self.assertEqual(stats["keys"], 4)

    def test_pairing_is_symmetric(self) -> None:
        sites = [
            self._site("dl", 0, 1, ordinal=0),
            self._site("dl", 1, 1, ordinal=1),
            self._site("dl", 2, 1, ordinal=2),
            self._site("dl", 3, 1, ordinal=3),
        ]
        vertices = self.SQUARE + [(20, 0, 0), (20, 10, 0)]
        triangle_vertices = {
            ("dl", 0): (0, 1, 2), ("dl", 1): (0, 2, 3),
            ("dl", 2): (1, 4, 5), ("dl", 3): (1, 5, 2),
        }
        entries, stats = build_quad_map(sites, triangle_vertices, vertices)
        self.assertEqual(stats["quad_count"], 2)
        by_key = {(e.display_list, e.list_ordinal): e.partner_list_ordinal for e in entries}
        for (display_list, list_ordinal), partner in by_key.items():
            if partner is None:
                continue
            self.assertEqual(
                by_key[(display_list, partner)], list_ordinal,
                f"{(display_list, list_ordinal)} -> {partner} is not reciprocated",
            )

    def test_pair_never_spans_two_display_lists(self) -> None:
        """Same rigid group, perfect shared-edge geometry, different lists.

        The runtime buffers triangles within one display list, so a pair
        across lists could never be resolved even though both gates pass.
        """
        sites = [
            self._site("dl_a", 0, 1, ordinal=0),
            self._site("dl_b", 0, 1, ordinal=1),
        ]
        triangle_vertices = {("dl_a", 0): (0, 1, 2), ("dl_b", 0): (0, 2, 3)}
        entries, stats = build_quad_map(sites, triangle_vertices, self.SQUARE)
        self.assertEqual(stats["quad_count"], 0)
        self.assertIsNone(self._partner(entries, "dl_a", 0))
        self.assertIsNone(self._partner(entries, "dl_b", 0))
        for entry in entries:
            if entry.partner_list_ordinal is not None:
                partners = [
                    other for other in entries
                    if other.display_list == entry.display_list
                    and other.list_ordinal == entry.partner_list_ordinal
                ]
                self.assertEqual(len(partners), 1)

    def test_same_list_ordinal_in_two_lists_does_not_collide(self) -> None:
        """Keys are (display_list, list_ordinal), not list_ordinal alone."""
        sites = [
            self._site("dl_a", 0, 1, ordinal=0),
            self._site("dl_a", 1, 1, ordinal=1),
            self._site("dl_b", 0, 2, ordinal=2),
            self._site("dl_b", 1, 2, ordinal=3),
        ]
        vertices = self.SQUARE + [(0, 0, 50), (10, 0, 50), (40, 40, 90)]
        triangle_vertices = {
            ("dl_a", 0): (0, 1, 2), ("dl_a", 1): (0, 2, 3),
            # dl_b's two triangles share no edge, so they must stay unpaired
            # even though their list_ordinals match dl_a's merging pair.
            ("dl_b", 0): (4, 5, 6), ("dl_b", 1): (0, 1, 6),
        }
        entries, stats = build_quad_map(sites, triangle_vertices, vertices)
        self.assertEqual(stats["quad_count"], 1)
        self.assertEqual(self._partner(entries, "dl_a", 0), 1)
        self.assertEqual(self._partner(entries, "dl_a", 1), 0)
        self.assertIsNone(self._partner(entries, "dl_b", 0))
        self.assertIsNone(self._partner(entries, "dl_b", 1))

    def test_geometric_gate_still_rejects_bad_winding(self) -> None:
        sites, triangle_vertices = self._split_square()
        triangle_vertices[("dl", 1)] = (0, 3, 2)  # reversed winding
        _, stats = build_quad_map(sites, triangle_vertices, self.SQUARE)
        self.assertEqual(stats["quad_count"], 0)
        self.assertEqual(stats["geometric_rejection_reasons"], {"winding_or_topology": 1})

    def test_unresolved_vertices_are_never_paired(self) -> None:
        sites, triangle_vertices = self._split_square()
        del triangle_vertices[("dl", 1)]
        entries, stats = build_quad_map(sites, triangle_vertices, self.SQUARE)
        self.assertEqual(stats["quad_count"], 0)
        self.assertEqual(stats["ineligible_by_reason"][REASON_UNRESOLVED_VERTICES], 1)
        self.assertEqual(len(entries), 2)

    def test_vertex_resolution_tracks_the_fast3d_cache(self) -> None:
        lists = {
            "dl": [
                ("gsSPVertex", "v_a, 3, 0"),
                ("gsSP1Triangle", "0, 1, 2, 0"),
                ("gsSPVertex", "v_b, 1, 1"),  # overwrite slot 1 only
                ("gsSP1Triangle", "0, 1, 2, 0"),
            ]
        }
        vertex_groups = {
            "v_a": [self._row(0, 0, 0), self._row(10, 0, 0), self._row(10, 10, 0)],
            "v_b": [self._row(0, 10, 0)],
        }
        vertices, triangle_vertices = resolve_display_list_vertices(lists, vertex_groups)
        first = [vertices[index] for index in triangle_vertices[("dl", 0)]]
        second = [vertices[index] for index in triangle_vertices[("dl", 1)]]
        self.assertEqual(first, [(0, 0, 0), (10, 0, 0), (10, 10, 0)])
        self.assertEqual(second, [(0, 0, 0), (0, 10, 0), (10, 10, 0)])

    def test_resolver_and_walker_agree_on_every_list_ordinal(self) -> None:
        """The two walks must count triangle commands identically.

        dl_rigid_groups advances list_ordinal for every triangle command. If
        the resolver skipped the ones it cannot resolve, the two walks would
        drift and the map would pair the wrong triangles -- a silent
        corrupted-geometry bug rather than a loud failure.
        """
        lists = {
            "dl": [
                ("gsSP1Triangle", "0, 1, 2, 0"),  # slots never loaded
                ("gsSPVertex", "v_a, 4, 0"),
                ("gsSP2Triangles", "0, 1, 2, 0x0, 0, 2, 3, 0x0"),
                ("gsSP1Triangle", "0, 1, 3, 0"),
            ]
        }
        vertex_groups = {
            "v_a": [self._row(0, 0, 0), self._row(10, 0, 0),
                    self._row(10, 10, 0), self._row(0, 10, 0)],
        }
        _, triangle_vertices = resolve_display_list_vertices(lists, vertex_groups)
        walked = [site.list_ordinal for site in walk_display_lists(lists, "dl")]
        self.assertEqual(walked, [0, 1, 2, 3])
        self.assertNotIn(("dl", 0), triangle_vertices, "unresolved, but still counted")
        self.assertEqual(sorted(triangle_vertices), [("dl", 1), ("dl", 2), ("dl", 3)])
        self.assertEqual(triangle_vertices[("dl", 3)], (0, 1, 3))

    def test_vertex_resolution_leaves_unloaded_slots_unresolved(self) -> None:
        lists = {"dl": [("gsSP1Triangle", "0, 1, 2, 0")]}
        _, triangle_vertices = resolve_display_list_vertices(lists, {})
        self.assertNotIn(("dl", 0), triangle_vertices)

    # -- attribute-exact weld -------------------------------------------
    #
    # SM64 re-uploads the same physical vertex into several gsSPVertex
    # batches, so triangles either side of a batch boundary never share a
    # source row and never look adjacent. Welding two rows into one vertex is
    # shading-neutral only when the *whole* row matches -- same position, uv,
    # flag and normal/colour. Position-only welding would silently change
    # Gouraud output.

    _TWO_BATCH_SQUARE = {
        "dl": [
            ("gsSPVertex", "v_a, 3, 0"),
            ("gsSP1Triangle", "0, 1, 2, 0"),
            ("gsSPVertex", "v_b, 3, 0"),
            ("gsSP1Triangle", "0, 1, 2, 0"),
        ]
    }

    def _two_batch_rows(self, corner_normal: tuple[int, int, int, int]):
        """A square split across two vertex batches.

        v_a holds the first triangle, v_b the second. The (10,10,0) corner is
        uploaded in both batches; ``corner_normal`` is v_b's copy of it.
        """
        return {
            "v_a": [
                self._row(0, 0, 0, normal=(0x7e, 0, 0, 0)),
                self._row(10, 0, 0),
                self._row(10, 10, 0, normal=(0x7e, 0, 0, 0)),
            ],
            "v_b": [
                self._row(0, 0, 0, normal=(0x7e, 0, 0, 0)),
                self._row(10, 10, 0, normal=corner_normal),
                self._row(0, 10, 0),
            ],
        }

    def test_byte_identical_rows_in_two_batches_weld(self) -> None:
        rows = self._two_batch_rows((0x7e, 0, 0, 0))
        vertices, triangle_vertices = resolve_display_list_vertices(
            self._TWO_BATCH_SQUARE, rows)
        self.assertEqual(len(vertices), 4, "6 rows, 2 welded pairs -> 4 vertices")
        self.assertEqual(triangle_vertices[("dl", 0)], (0, 1, 2))
        self.assertEqual(triangle_vertices[("dl", 1)], (0, 2, 3),
                         "the welded corners must reuse the first batch's ids")

    def test_same_position_different_normal_never_welds(self) -> None:
        """The entire safety property of the weld, stated as a contrast.

        Both fixtures place a vertex at exactly (10,10,0) in both batches.
        They differ in one byte -- that copy's normal. Position-only welding
        would merge both; only the byte-identical one may merge, because the
        differing normal shades differently under Gouraud.
        """
        identical = self._two_batch_rows((0x7e, 0, 0, 0))
        differing = self._two_batch_rows((0x40, 0, 0, 0))
        self.assertEqual(
            [row[0:3] for row in identical["v_b"]],
            [row[0:3] for row in differing["v_b"]],
            "the two fixtures must differ only in the normal, not in position",
        )

        sites = [
            self._site("dl", 0, 1, ordinal=0),
            self._site("dl", 1, 1, ordinal=1),
        ]
        results = {}
        for label, rows in (("identical", identical), ("differing", differing)):
            vertices, triangle_vertices = resolve_display_list_vertices(
                self._TWO_BATCH_SQUARE, rows)
            _, stats = build_quad_map(sites, triangle_vertices, vertices)
            results[label] = (len(vertices), stats["quad_count"])

        self.assertEqual(results["identical"], (4, 1),
                         "byte-identical rows weld and the cross-batch pair merges")
        self.assertEqual(results["differing"], (5, 0),
                         "one differing normal byte must block the weld and the merge")

    def test_weld_enables_a_cross_batch_quad(self) -> None:
        """Welding is what makes the pair reachable at all.

        The control passes the same geometry with per-upload vertex identity,
        which is what the resolver produced before the weld: no shared edge,
        so no candidate and no quad.
        """
        vertices, triangle_vertices = resolve_display_list_vertices(
            self._TWO_BATCH_SQUARE, self._two_batch_rows((0x7e, 0, 0, 0)))
        sites = [
            self._site("dl", 0, 1, ordinal=0),
            self._site("dl", 1, 1, ordinal=1),
        ]
        _, welded = build_quad_map(sites, triangle_vertices, vertices)

        unwelded_vertices = [
            (0, 0, 0), (10, 0, 0), (10, 10, 0),      # v_a's uploads
            (0, 0, 0), (10, 10, 0), (0, 10, 0),      # v_b's uploads, same places
        ]
        _, unwelded = build_quad_map(
            sites,
            {("dl", 0): (0, 1, 2), ("dl", 1): (3, 4, 5)},
            unwelded_vertices,
        )
        self.assertEqual(unwelded["quad_count"], 0, "pre-weld control must not merge")
        self.assertEqual(unwelded["candidate_count"], 0, "and must find no shared edge")
        self.assertEqual(welded["quad_count"], 1, "the weld must unlock the pair")

    def test_vertex_rows_must_be_complete(self) -> None:
        """A truncated row cannot prove two vertices are interchangeable."""
        rows = {"v_a": [(0, 0, 0, 0, 0)]}  # vertex_groups()'s 5-field shape
        with self.assertRaisesRegex(ValueError, "complete Vtx row"):
            resolve_display_list_vertices(
                {"dl": [("gsSPVertex", "v_a, 1, 0")]}, rows)

    # -- 4-corner cycle --------------------------------------------------

    def test_quad_corner_cycle_records_winding_order(self) -> None:
        """The emit stage needs a real 4th corner in the right order.

        `corners` codes 0-2 as this triangle's own corners in source order and
        3-5 as the partner's (value - 3). Decoding either entry must trace the
        same perimeter, in order -- not merely name the same four points.
        """
        sites, triangle_vertices = self._split_square()
        entries, stats = build_quad_map(sites, triangle_vertices, self.SQUARE)
        self.assertEqual(stats["quad_count"], 1)

        first = self._entry(entries, "dl", 0)
        second = self._entry(entries, "dl", 1)
        self.assertEqual(first.corners, (0, 1, 2, 5))
        self.assertEqual(second.corners, (0, 4, 1, 2))

        def perimeter(entry: QuadMapEntry) -> list[tuple[int, int, int]]:
            own = triangle_vertices[(entry.display_list, entry.list_ordinal)]
            mate = triangle_vertices[
                (entry.display_list, entry.partner_list_ordinal)]
            return [
                self.SQUARE[own[code] if code < 3 else mate[code - 3]]
                for code in entry.corners
            ]

        expected = [(0, 0, 0), (10, 0, 0), (10, 10, 0), (0, 10, 0)]
        self.assertEqual(perimeter(first), expected)
        self.assertEqual(perimeter(second), expected,
                         "both entries must describe the same cycle in the same order")

    def test_unpaired_triangles_have_no_corner_cycle(self) -> None:
        sites, triangle_vertices = self._split_square(groups=(1, 2))
        entries, stats = build_quad_map(sites, triangle_vertices, self.SQUARE)
        self.assertEqual(stats["quad_count"], 0)
        self.assertTrue(all(entry.corners is None for entry in entries))

    def test_distinct_source_vertices_at_one_position_do_not_share_an_edge(self) -> None:
        """Two distinct vertex ids never form a shared edge, same place or not.

        The weld happens in the resolver, on byte-identical rows only. By the
        time build_quad_map sees ids, two different ids are two different
        vertices regardless of where they sit.
        """
        sites, triangle_vertices = self._split_square()
        vertices = self.SQUARE + [(0, 0, 0), (10, 10, 0)]  # duplicates of 0 and 2
        triangle_vertices[("dl", 1)] = (4, 5, 3)
        _, stats = build_quad_map(sites, triangle_vertices, vertices)
        self.assertEqual(stats["quad_count"], 0)


class QuadMapCRendererTests(unittest.TestCase):
    """The generated C table is what the SH-2 actually reads.

    Every assertion here is about the *emitted text*, not about an internal
    helper, because the text is what ships. The safety invariant the whole
    encoding is built around: anything the runtime cannot positively decode
    as a merge -- a zero word, an absent display list, an ordinal past the
    recorded length -- must mean "emit exactly as today".
    """

    #: dl_a: ordinal 0 pairs with 3; 1 and 2 stay standalone. The interior
    #: unpaired ordinals are the point -- trailing ones would be truncated
    #: away and could not witness the sentinel.
    FIXTURE = [
        QuadMapEntry("dl_a", 0, 3, (0, 1, 2, 5)),
        QuadMapEntry("dl_a", 1, None, None),
        QuadMapEntry("dl_a", 2, None, None),
        QuadMapEntry("dl_a", 3, 0, (0, 4, 1, 2)),
        QuadMapEntry("dl_b", 0, 1, (1, 2, 0, 4)),
        QuadMapEntry("dl_b", 1, 0, (4, 1, 2, 0)),
    ]

    @staticmethod
    def _arrays(text: str) -> dict[str, list[int]]:
        """Every emitted per-list entry array, by display-list symbol."""
        found: dict[str, list[int]] = {}
        pattern = re.compile(
            r"sm64_saturn_quad_map_entries_(\w+)\[(\d+)\]\s*=\s*\{(.*?)\};",
            re.DOTALL,
        )
        for match in pattern.finditer(text):
            words = [
                int(token, 16)
                for token in re.findall(r"0x([0-9A-Fa-f]{8})U", match.group(3))
            ]
            found[match.group(1)] = words
            if len(words) != int(match.group(2)):
                raise AssertionError(
                    f"{match.group(1)}: declared dimension {match.group(2)} "
                    f"but {len(words)} initialisers"
                )
        return found

    @staticmethod
    def _rows(text: str) -> list[tuple[str, str, int]]:
        """The lists table, as (display_list symbol, array symbol, count)."""
        body = re.search(
            r"sm64_saturn_quad_map_lists\[\d+\]\s*=\s*\{(.*?)\n\};",
            text, re.DOTALL,
        )
        if body is None:
            raise AssertionError("no sm64_saturn_quad_map_lists definition")
        return [
            (match.group(1), match.group(2), int(match.group(3)))
            for match in re.finditer(
                r"\{\s*(\w+),\s*sm64_saturn_quad_map_entries_(\w+),\s*(\d+)U",
                body.group(1),
            )
        ]

    @staticmethod
    def _list_count(text: str) -> int:
        match = re.search(
            r"sm64_saturn_quad_map_list_count\s*=\s*(\d+)U;", text)
        if match is None:
            raise AssertionError("the table does not carry its own length")
        return int(match.group(1))

    def test_unpaired_ordinal_never_decodes_as_a_merge(self) -> None:
        """The sentinel must be distinguishable from "pairs with ordinal 0".

        Ordinal 0 is a perfectly legal partner, so an encoding that wrote a
        bare partner index would make "unpaired" and "merge with the first
        triangle of this list" the same word -- silent corrupted geometry.
        """
        words = self._arrays(render_quad_map_c(self.FIXTURE))["dl_a"]
        self.assertEqual(words[1], 0x00000000)
        self.assertEqual(words[2], 0x00000000)
        self.assertEqual(words[1] & 0x1, 0, "unpaired must clear the paired bit")
        self.assertEqual(words[0] & 0x1, 1, "paired must set the paired bit")
        self.assertEqual(words[3] & 0x1, 1)

    def test_a_zero_filled_word_is_the_do_not_merge_state(self) -> None:
        """Absence of data decodes as "no merge", never as a merge.

        A truncated map, a zeroed page or an unwritten slot all read as 0.
        That has to be the safe state, not a merge with ordinal 0.
        """
        header = render_quad_map_h()
        self.assertIn("#define SM64_SATURN_QUAD_MAP_NONE 0x00000000U", header)
        words = self._arrays(render_quad_map_c(self.FIXTURE))["dl_a"]
        self.assertIn(0x00000000, words)

    def test_header_carries_the_longest_row_as_a_bound(self) -> None:
        """The runtime sizes an ordinal-keyed array by this constant.

        dl_a trims to 4 words and dl_b to 2, so the bound is 4. Deriving it
        from the same tables the C rows come from is what keeps the runtime
        array from being sized by a guess that the data can outgrow.
        """
        header = render_quad_map_h(self.FIXTURE)
        self.assertIn("#define SM64_SATURN_QUAD_MAP_MAX_ENTRIES 4U", header)

    def test_header_bound_covers_every_emitted_row(self) -> None:
        header = render_quad_map_h(self.FIXTURE)
        match = re.search(
            r"#define SM64_SATURN_QUAD_MAP_MAX_ENTRIES (\d+)U", header)
        assert match is not None
        bound = int(match.group(1))
        for words in self._arrays(render_quad_map_c(self.FIXTURE)).values():
            self.assertLessEqual(len(words), bound)

    def test_header_bound_is_never_zero(self) -> None:
        """An empty table still has to yield a legal C array size."""
        header = render_quad_map_h()
        self.assertIn("#define SM64_SATURN_QUAD_MAP_MAX_ENTRIES 1U", header)

    def test_table_and_every_list_carry_their_own_length(self) -> None:
        text = render_quad_map_c(self.FIXTURE)
        self.assertEqual(self._list_count(text), 2)
        rows = self._rows(text)
        self.assertEqual(len(rows), 2)
        arrays = self._arrays(text)
        counts = {row[0]: row[2] for row in rows}
        self.assertEqual(counts, {"dl_a": 4, "dl_b": 2})
        for symbol, count in counts.items():
            self.assertEqual(
                count, len(arrays[symbol]),
                f"{symbol}: recorded length must equal the array it guards",
            )

    def test_corner_cycle_round_trips_in_order(self) -> None:
        """Order is the whole content: a rotated cycle is a different quad."""
        words = self._arrays(render_quad_map_c(self.FIXTURE))["dl_a"]

        def corners(word: int) -> tuple[int, ...]:
            return tuple((word >> (16 + 4 * index)) & 0xF for index in range(4))

        self.assertEqual(corners(words[0]), (0, 1, 2, 5))
        self.assertEqual(corners(words[3]), (0, 4, 1, 2))
        self.assertEqual((words[0] >> 1) & 0x7FFF, 3)
        self.assertEqual((words[3] >> 1) & 0x7FFF, 0)

    def test_two_display_lists_with_the_same_ordinals_do_not_collide(self) -> None:
        text = render_quad_map_c(self.FIXTURE)
        arrays = self._arrays(text)
        self.assertEqual(sorted(arrays), ["dl_a", "dl_b"])
        self.assertNotEqual(arrays["dl_a"][0], arrays["dl_b"][0],
                            "ordinal 0 of two lists must encode independently")
        for display_list, array, _count in self._rows(text):
            self.assertEqual(display_list, array,
                             "each row must point at its own list's array")

    def test_a_list_with_no_pair_at_all_is_omitted(self) -> None:
        """Omission is safe by construction: absent means "do not merge"."""
        entries = list(self.FIXTURE) + [
            QuadMapEntry("dl_c", 0, None, None),
            QuadMapEntry("dl_c", 1, None, None),
        ]
        text = render_quad_map_c(entries)
        self.assertNotIn("dl_c", text)
        self.assertEqual(self._list_count(text), 2)

    def test_trailing_unpaired_ordinals_are_truncated_not_padded(self) -> None:
        entries = list(self.FIXTURE) + [QuadMapEntry("dl_b", 2, None, None)]
        text = render_quad_map_c(entries)
        self.assertEqual(len(self._arrays(text)["dl_b"]), 2)

    def test_every_referenced_display_list_is_declared(self) -> None:
        """The generated file names real C symbols; the linker checks them."""
        text = render_quad_map_c(self.FIXTURE)
        self.assertIn("extern const Gfx dl_a[];", text)
        self.assertIn("extern const Gfx dl_b[];", text)

    def test_asymmetric_pairing_is_refused(self) -> None:
        """A one-way pair would merge one triangle into a stranger."""
        broken = [
            QuadMapEntry("dl_a", 0, 1, (0, 1, 2, 5)),
            QuadMapEntry("dl_a", 1, None, None),
        ]
        with self.assertRaisesRegex(ValueError, "not reciprocated"):
            render_quad_map_c(broken)

    def test_a_triangle_paired_with_itself_is_refused(self) -> None:
        """Partners are per-list ordinals, so a stray self-pair is expressible.

        Two display lists each naming ordinal 0 as ordinal 0's partner would
        otherwise look reciprocated: the key is (list, ordinal), and each
        entry reciprocates itself.
        """
        broken = [
            QuadMapEntry("dl_a", 0, 0, (0, 1, 2, 5)),
            QuadMapEntry("dl_b", 0, 0, (0, 1, 2, 5)),
        ]
        with self.assertRaisesRegex(ValueError, "itself"):
            render_quad_map_c(broken)

    def test_an_out_of_range_corner_code_is_refused(self) -> None:
        broken = [
            QuadMapEntry("dl_a", 0, 1, (0, 1, 2, 6)),
            QuadMapEntry("dl_a", 1, 0, (0, 1, 2, 5)),
        ]
        with self.assertRaisesRegex(ValueError, "corner code"):
            render_quad_map_c(broken)

    def test_a_paired_entry_without_a_corner_cycle_is_refused(self) -> None:
        broken = [
            QuadMapEntry("dl_a", 0, 1, None),
            QuadMapEntry("dl_a", 1, 0, (0, 1, 2, 5)),
        ]
        with self.assertRaisesRegex(ValueError, "corner cycle"):
            render_quad_map_c(broken)

    def test_a_display_list_name_that_is_not_a_c_identifier_is_refused(self) -> None:
        broken = [
            QuadMapEntry("dl_a[0]; evil()", 0, 1, (0, 1, 2, 5)),
            QuadMapEntry("dl_a[0]; evil()", 1, 0, (0, 4, 1, 2)),
        ]
        with self.assertRaisesRegex(ValueError, "identifier"):
            render_quad_map_c(broken)

    def test_a_duplicate_key_is_refused(self) -> None:
        broken = [
            QuadMapEntry("dl_a", 0, 1, (0, 1, 2, 5)),
            QuadMapEntry("dl_a", 1, 0, (0, 4, 1, 2)),
            QuadMapEntry("dl_a", 1, None, None),
        ]
        with self.assertRaisesRegex(ValueError, "duplicate"):
            render_quad_map_c(broken)

    def test_header_accessors_decode_the_emitted_words(self) -> None:
        """The header's macros and the encoder must agree bit for bit.

        Decoding is done with the shifts and masks *parsed out of the
        generated header*, so a drift between the two files fails here rather
        than silently on hardware.
        """
        header = render_quad_map_h()
        paired_mask = int(re.search(
            r"SM64_SATURN_QUAD_MAP_IS_PAIRED\(entry\) "
            r"\(\(\(entry\) & (0x[0-9A-Fa-f]+)U\) != 0U\)", header).group(1), 16)
        partner = re.search(
            r"SM64_SATURN_QUAD_MAP_PARTNER\(entry\) "
            r"\(\(\(entry\) >> (\d+)U\) & (0x[0-9A-Fa-f]+)U\)", header)
        corner = re.search(
            r"SM64_SATURN_QUAD_MAP_CORNER\(entry, index\) "
            r"\(\(\(entry\) >> \((\d+)U \+ (\d+)U \* \(index\)\)\) "
            r"& (0x[0-9A-Fa-f]+)U\)", header)
        self.assertIsNotNone(partner)
        self.assertIsNotNone(corner)
        partner_shift, partner_mask = int(partner.group(1)), int(partner.group(2), 16)
        base, stride, corner_mask = (
            int(corner.group(1)), int(corner.group(2)), int(corner.group(3), 16))

        words = self._arrays(render_quad_map_c(self.FIXTURE))["dl_a"]
        self.assertTrue(words[0] & paired_mask)
        self.assertFalse(words[1] & paired_mask)
        self.assertEqual((words[0] >> partner_shift) & partner_mask, 3)
        self.assertEqual(
            tuple((words[0] >> (base + stride * index)) & corner_mask
                  for index in range(4)),
            (0, 1, 2, 5),
        )

    def test_header_and_source_agree_on_the_include_name(self) -> None:
        header = render_quad_map_h()
        self.assertIn("SM64_SATURN_QUAD_MAP_H", header)
        self.assertIn('#include "saturn_quad_map.h"',
                      render_quad_map_c(self.FIXTURE))

    def test_both_generated_files_are_marked_generated(self) -> None:
        for text in (render_quad_map_h(), render_quad_map_c(self.FIXTURE)):
            self.assertIn("tools/saturn/quad_map.py", text)
            self.assertIn("do not edit", text)

    def test_provenance_is_recorded_in_the_generated_source(self) -> None:
        text = render_quad_map_c(
            self.FIXTURE, provenance=["mario_geo_body from actors/mario/geo.inc.c"])
        self.assertIn("mario_geo_body from actors/mario/geo.inc.c", text)


if __name__ == "__main__":
    unittest.main(verbosity=2)
