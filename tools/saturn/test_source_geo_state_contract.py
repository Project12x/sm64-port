#!/usr/bin/env python3
"""Executable evidence gate for the deferred source-geo state seam."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
AREA_C = REPO_ROOT / "src/game/area.c"
GRAPH_C = REPO_ROOT / "src/game/rendering_graph_node.c"
MARIO_C = REPO_ROOT / "src/game/mario_misc.c"
PAINTINGS_C = REPO_ROOT / "src/game/paintings.c"
MOVTEX_C = REPO_ROOT / "src/game/moving_texture.c"
CAMERA_C = REPO_ROOT / "src/game/camera.c"
GEO_STATE_C = REPO_ROOT / "src/port/saturn/runtime/saturn_source_geo_state.c"


def extract_c_function(path: Path, name: str) -> str:
    text = path.read_text(encoding="utf-8")
    match = re.search(rf"\b{name}\s*\([^;]*?\)\s*\{{", text, re.S)
    if match is None:
        raise AssertionError(f"function {name} not found in {path}")
    depth = 0
    for index in range(match.end() - 1, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[match.end() : index]
    raise AssertionError(f"function {name} is not terminated in {path}")


def extract_if_block(body: str, condition: str) -> str:
    match = re.search(rf"if\s*\(\s*{re.escape(condition)}\s*\)\s*\{{", body)
    if match is None:
        raise AssertionError(f"guard if ({condition}) not found")
    depth = 0
    for index in range(match.end() - 1, len(body)):
        if body[index] == "{":
            depth += 1
        elif body[index] == "}":
            depth -= 1
            if depth == 0:
                return body[match.end() : index]
    raise AssertionError(f"guard if ({condition}) is not terminated")


def compact(text: str) -> str:
    return re.sub(
        r"\s+", " ", re.sub(r"/\*.*?\*/|//[^\r\n]*", "", text, flags=re.S)
    ).strip()


class SourceGeoStateContractTests(unittest.TestCase):
    def test_old_root_suppression_omits_the_only_render_walk(self) -> None:
        raw_body = extract_c_function(AREA_C, "render_game")
        body = compact(raw_body)
        guarded = compact(extract_if_block(raw_body, "!scene_graph_suppressed"))
        self.assertRegex(
            body,
            r"if \(!scene_graph_suppressed\) \{ geo_process_root\(",
        )
        self.assertIn("geo_process_root(", guarded)
        self.assertIn("render_screen_transition(", body)
        self.assertNotIn("render_screen_transition(", guarded)

    def test_animation_and_visibility_mutate_during_render_callbacks(self) -> None:
        attack = compact(extract_c_function(MARIO_C, "geo_mario_hand_foot_scaler"))
        hand = compact(extract_c_function(MARIO_C, "geo_switch_mario_hand"))
        self.assertIn("callContext == GEO_CONTEXT_RENDER", attack)
        self.assertIn("bodyState->punchState -= 1", attack)
        self.assertIn("scaleNode->scale =", attack)
        self.assertIn("callContext == GEO_CONTEXT_RENDER", hand)
        self.assertIn("switchCase->selectedCase =", hand)

    def test_painting_update_and_display_construction_share_one_callback(self) -> None:
        body = compact(extract_c_function(PAINTINGS_C, "geo_painting_draw"))
        self.assertIn("callContext == GEO_CONTEXT_RENDER", body)
        self.assertIn("paintingDlist = display_painting(painting)", body)
        self.assertIn("painting_update_floors(painting)", body)
        self.assertIn("wall_painting_update(painting, paintingGroup)", body)
        self.assertIn("floor_painting_update(painting, paintingGroup)", body)

    def test_water_and_moving_texture_state_mutate_during_render_callbacks(self) -> None:
        water = compact(extract_c_function(MOVTEX_C, "geo_wdw_set_initial_water_level"))
        pause = compact(extract_c_function(MOVTEX_C, "geo_movtex_pause_control"))
        self.assertIn("callContext == GEO_CONTEXT_RENDER", water)
        self.assertIn("gEnvironmentRegions[i * 6 + 6] = wdwWaterHeight", water)
        self.assertIn("gWdwWaterLevelSet = TRUE", water)
        self.assertIn("gMovtexCounterPrev = gMovtexCounter", pause)
        self.assertIn("gMovtexCounter = gAreaUpdateCounter", pause)

    def test_camera_matrix_object_and_visibility_state_require_the_walk(self) -> None:
        camera = compact(extract_c_function(CAMERA_C, "geo_camera_main"))
        obj = compact(extract_c_function(GRAPH_C, "geo_process_object"))
        self.assertIn("case GEO_CONTEXT_RENDER", camera)
        self.assertIn("update_graph_node_camera(gc)", camera)
        for axis in range(3):
            self.assertIn(
                f"node->header.gfx.cameraToObject[{axis}] = gMatStack[gMatStackIndex][3][{axis}]",
                obj,
            )
        self.assertIn("obj_is_in_view(&node->header.gfx, gMatStack[gMatStackIndex])", obj)
        self.assertGreaterEqual(obj.count("node->header.gfx.throwMatrix = NULL"), 1)

    def test_generic_generated_list_seam_is_after_callback_construction(self) -> None:
        generated = compact(extract_c_function(GRAPH_C, "geo_process_generated_list"))
        callback = generated.index("node->fnNode.func(GEO_CONTEXT_RENDER")
        append = generated.index("geo_append_display_list(")
        self.assertLess(callback, append)

    def test_root_matrix_setup_and_display_construction_are_interleaved(self) -> None:
        root = compact(extract_c_function(GRAPH_C, "geo_process_root"))
        self.assertLess(root.index("alloc_display_list(sizeof(*initialMatrix))"), root.index("geo_process_node_and_siblings("))
        self.assertLess(root.index("gSPMatrix("), root.index("geo_process_node_and_siblings("))

    def test_unproven_api_is_not_integrated_into_the_source_tick(self) -> None:
        extract_c_function(GEO_STATE_C, "sm64_saturn_source_geo_update_state")
        callers = []
        for path in (REPO_ROOT / "src").rglob("*.c"):
            if path == GEO_STATE_C:
                continue
            if "sm64_saturn_source_geo_update_state(" in path.read_text(encoding="utf-8"):
                callers.append(path.relative_to(REPO_ROOT).as_posix())
        self.assertEqual(callers, [])


if __name__ == "__main__":
    unittest.main()
