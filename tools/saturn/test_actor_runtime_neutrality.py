#!/usr/bin/env python3
"""Source-neutrality checks for the generic actor-instance runtime."""
from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RUNTIME_FILES = (
    ROOT / "src/port/saturn/gfx/saturn_actor_instance_queue.h",
    ROOT / "src/port/saturn/gfx/saturn_actor_instance_queue.c",
    ROOT / "src/port/saturn/gfx/saturn_actor_batch.h",
    ROOT / "src/port/saturn/gfx/saturn_actor_batch.c",
    ROOT / "src/port/saturn/gfx/saturn_actor_runtime_handoff.h",
    ROOT / "src/port/saturn/gfx/saturn_actor_runtime_handoff.c",
)


def code_only(text: str) -> str:
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
    return re.sub(r'"(?:\\.|[^"\\])*"', '""', text)


class ActorRuntimeNeutralityTest(unittest.TestCase):
    def test_runtime_has_no_level_or_family_specific_control_flow(self) -> None:
        forbidden = re.compile(
            r"\b(?:bob|wf|goomba|king_bobomb|chain_chomp)\b", re.I
        )
        for path in RUNTIME_FILES:
            with self.subTest(path=path.name):
                source = code_only(path.read_text(encoding="utf-8"))
                self.assertIsNone(forbidden.search(source))

    def test_actor_queue_is_separate_from_the_eight_entry_world_graph(self) -> None:
        for path in RUNTIME_FILES:
            with self.subTest(path=path.name):
                source = code_only(path.read_text(encoding="utf-8"))
                self.assertNotIn("saturn_render_job_graph", source)
                self.assertNotIn("SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY", source)


if __name__ == "__main__":
    unittest.main()
