"""Static regression coverage for Task 8's Sourceboot scene boundary."""

from __future__ import annotations

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCEBOOT_MAIN = ROOT / "src" / "port" / "saturn" / "sourceboot" / "main.c"
TICK = "game_loop_one_iteration();"
OBSERVER = "sm64_saturn_demo_render_scene_observe"


def _balanced_function_body(source: str, signature: str) -> str:
    """Return one C function body without trusting later preprocessor lines."""
    start = source.index(signature)
    open_brace = source.index("{", start)
    depth = 0
    index = open_brace
    state = "code"
    while index < len(source):
        char = source[index]
        next_char = source[index + 1] if index + 1 < len(source) else ""
        if state == "code":
            if char == "/" and next_char == "*":
                state = "block_comment"
                index += 1
            elif char == "/" and next_char == "/":
                state = "line_comment"
                index += 1
            elif char == '"':
                state = "string"
            elif char == "'":
                state = "character"
            elif char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    return source[open_brace + 1:index]
        elif state == "block_comment" and char == "*" and next_char == "/":
            state = "code"
            index += 1
        elif state == "line_comment" and char == "\n":
            state = "code"
        elif state in {"string", "character"}:
            if char == "\\":
                index += 1
            elif (state == "string" and char == '"') or (
                state == "character" and char == "'"
            ):
                state = "code"
        index += 1
    raise ValueError(f"unclosed function body for {signature}")


def _observer_immediately_follows_tick(body: str) -> bool:
    if body.count(OBSERVER) != 1:
        return False
    try:
        tick_end = body.index(TICK) + len(TICK)
        observer = body.index(OBSERVER, tick_end)
    except ValueError:
        return False
    # The current accepted path explicitly re-enables display submission
    # before observing the scene. “Immediate” means same authoritative tick,
    # before sim_end, rather than literally adjacent source statements.
    return observer < body.index("const uint16_t sim_end", observer)


class Task8SourcebootLodPlacementTests(unittest.TestCase):
    def test_scene_observer_runs_immediately_in_every_authoritative_tick(self) -> None:
        source = SOURCEBOOT_MAIN.read_text(encoding="utf-8")
        tick = _balanced_function_body(source, "static void sourceboot_run_source_tick(void)")

        self.assertTrue(_observer_immediately_follows_tick(tick))
        observer = tick.index(OBSERVER)
        sim_end = tick.index("const uint16_t sim_end")
        self.assertLess(observer, sim_end)

        # A catch-up batch must not retain a second observer after the loop:
        # exit + same-ID re-entry would otherwise collapse to one final state.
        self.assertEqual(source.count(OBSERVER), 1)

    def test_duplicate_observer_and_post_tick_observer_are_rejected(self) -> None:
        source = SOURCEBOOT_MAIN.read_text(encoding="utf-8")
        tick = _balanced_function_body(source, "static void sourceboot_run_source_tick(void)")
        duplicate = tick.replace(OBSERVER, OBSERVER + "\n    " + OBSERVER, 1)
        self.assertEqual(duplicate.count(OBSERVER), 2)
        self.assertFalse(_observer_immediately_follows_tick(duplicate))

        post_tick = tick.replace(
            "const uint16_t sim_end",
            f"{OBSERVER};\n    const uint16_t sim_end",
            1,
        )
        self.assertFalse(_observer_immediately_follows_tick(post_tick))


if __name__ == "__main__":
    unittest.main()
