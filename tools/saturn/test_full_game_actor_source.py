#!/usr/bin/env python3
"""Static source/runtime boundary checks for generic actor families."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class FullGameActorSourceContract(unittest.TestCase):
    def test_feature_vocabulary_is_shared_and_stable(self) -> None:
        source = (ROOT / "tools/saturn/actor_source.py").read_text(encoding="utf-8")
        for name in ("ANIMATED", "SWITCH", "BILLBOARD", "ALPHA", "TRANSLUCENT",
                     "SHADOW", "PARENTED", "HELD", "MODEL_MUTATION", "SURFACE",
                     "LOD", "PARTICLE", "EFFECT"):
            self.assertIn(f'"{name}"', source)

    def test_family_runtime_has_no_model_name_branch(self) -> None:
        source = (ROOT / "src/port/saturn/gfx/saturn_actor_bank.c").read_text(encoding="utf-8")
        self.assertNotRegex(source, r"MODEL_GOOMBA|Goomba|MODEL_BOBOMB|Bobomb")
        self.assertIn("sm64_saturn_actor_family_bank_select", source)

    def test_public_family_records_are_offset_only(self) -> None:
        header = (ROOT / "src/port/saturn/gfx/saturn_actor_bank.h").read_text(encoding="utf-8")
        body = re.search(r"typedef struct sm64_saturn_actor_family_record \{(.*?)\}", header, re.S)
        self.assertIsNotNone(body)
        self.assertNotIn("*", body.group(1))
        self.assertNotIn("uintptr_t", body.group(1))
        for field in ("name_offset", "source_offset", "unsupported_offset", "metadata_offset"):
            self.assertIn(field, body.group(1))

    def test_make_target_and_closure_binding_are_generic(self) -> None:
        make = (ROOT / "Makefile.saturn.mk").read_text(encoding="utf-8")
        self.assertIn("compile-actor-banks", make)
        self.assertIn("SCENE_CLOSURE_OUTPUT", make)
        self.assertNotRegex(make, r"compile-actor-banks[^\n]*Goomba")


if __name__ == "__main__":
    unittest.main()
