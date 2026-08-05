#!/usr/bin/env python3
"""Compatibility guard: Goomba capacity is now a generated closure fact."""

from __future__ import annotations

import unittest
from pathlib import Path

from collect_scene_closure import collect_scene_closure


ROOT = Path(__file__).resolve().parents[2]


class GoombaActorInventoryTest(unittest.TestCase):
    def test_generated_closure_has_bounded_goomba_instances(self) -> None:
        closure = collect_scene_closure(ROOT, "bob", 1, ROOT / "tools/saturn/behavior_spawn_rules.json")
        records = {record["stable_id"]: record for record in closure["records"]}
        self.assertEqual(records["bhvGoomba"]["maximum_live_instances"], 11)


if __name__ == "__main__":
    unittest.main()
