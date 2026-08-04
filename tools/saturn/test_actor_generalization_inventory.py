#!/usr/bin/env python3
"""Host-only guard for the documented first generic animated actor.

This is deliberately a source inventory, not a renderer test.  It makes the
Goomba selection reproducible and fails if the in-tree source stops matching
the assumptions that the future actor-bank manifest must declare.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class GoombaActorInventoryTest(unittest.TestCase):
    def read(self, relative: str) -> str:
        return (ROOT / relative).read_text(encoding="utf-8")

    def test_bob_has_bounded_goomba_instances(self) -> None:
        macro = self.read("levels/bob/areas/1/macro.inc.c")
        self.assertEqual(len(re.findall(r"macro_goomba\b", macro)), 2)
        self.assertEqual(len(re.findall(r"macro_goomba_triplet_spawner\b", macro)), 3)
        # bhv_goomba_triplet_spawner_update creates three source Goombas per
        # active spawner. The future manifest must budget all 11 instances.
        behavior = self.read("src/game/behaviors/goomba.inc.c")
        self.assertIn("+ 3", behavior)
        self.assertIn("MODEL_GOOMBA, bhvGoomba", behavior)

    def test_geo_declares_all_first_proof_feature_classes(self) -> None:
        geo = self.read("actors/goomba/geo.inc.c")
        for required in (
            "GEO_SHADOW",
            "GEO_ANIMATED_PART",
            "GEO_SWITCH_CASE(2, geo_switch_anim_state)",
            "GEO_BILLBOARD",
            "GEO_DISPLAY_LIST(LAYER_ALPHA, goomba_seg8_dl_0801B690)",
        ):
            self.assertIn(required, geo)

    def test_animation_and_model_binding_are_source_owned(self) -> None:
        animation = self.read("actors/goomba/anims/anim_0801DA34.inc.c")
        self.assertRegex(animation, r"struct Animation goomba_seg8_anim_0801DA34")
        self.assertRegex(animation, r"\n\s*0x1E,\s*\n\s*ANIMINDEX_NUMPARTS")
        model_ids = self.read("include/model_ids.h")
        self.assertRegex(model_ids, r"#define MODEL_GOOMBA\s+0xC0\s+// goomba_geo")
        behavior_data = self.read("data/behavior_data.c")
        self.assertIn("LOAD_ANIMATIONS(oAnimations, goomba_seg8_anims_0801DA4C)", behavior_data)


if __name__ == "__main__":
    unittest.main()
