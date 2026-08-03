"""Static regression coverage for Task 8's Sourceboot scene boundary."""

from __future__ import annotations

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCEBOOT_MAIN = ROOT / "src" / "port" / "saturn" / "sourceboot" / "main.c"


class Task8SourcebootLodPlacementTests(unittest.TestCase):
    def test_scene_observer_runs_in_every_authoritative_tick(self) -> None:
        source = SOURCEBOOT_MAIN.read_text(encoding="utf-8")
        start = source.index("static void sourceboot_run_source_tick(void)")
        end = source.index("#if SATURN_SOURCEBOOT_ROUTE_REPLAY", start)
        tick = source[start:end]

        game_tick = tick.index("game_loop_one_iteration();")
        observer = tick.index("sm64_saturn_demo_render_scene_observe")
        sim_end = tick.index("const uint16_t sim_end")
        self.assertLess(game_tick, observer)
        self.assertLess(observer, sim_end)
        self.assertIn("#if SATURN_DEMO_PATH", tick[game_tick:observer])

        # A catch-up batch must not retain a second observer after the loop:
        # exit + same-ID re-entry would otherwise collapse to one final state.
        self.assertEqual(source.count("sm64_saturn_demo_render_scene_observe"), 1)


if __name__ == "__main__":
    unittest.main()
