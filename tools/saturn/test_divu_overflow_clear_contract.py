#!/usr/bin/env python3
"""Structural regression guard for the SH-2 DIVU DVCR sticky-overflow clear.

The SH-2's on-chip DIVU sets a sticky overflow flag in DVCR (0xFFFFFF08,
bit 0) on divide overflow. The bit does NOT auto-clear on the next
division -- it stays latched until something explicitly clears it. Two
call sites launch a `cpu_divu_64_32_set()` division without first clearing
a possible overflow left by a prior call:

  * `sm64_saturn_atan2_q16_index()` in
    src/port/saturn/runtime/saturn_engine_math_q16.h
  * `sm64_saturn_div_s64_s32()` in
    src/port/saturn/gfx/saturn_render_native_math.h

Missing the clear meant a single overflowing division could poison every
subsequent division's overflow check for the rest of the program's life.
On target this manifested as BOB's radial-hill camera yaw (`sAreaYaw`)
freezing at the atan2 lookup table's saturated max index (1024, i.e. -8192
/ 0x2000) from frame ~9580 onward, sustained through 30000+ frames.

DVCR is real hardware state -- there is no way to observe the sticky bit
from a host (non-`__sh__`) build, so this cannot be a compiled/behavioral
test. This is a source-text contract instead: it parses the `#if
defined(__sh__)` true-branch around each `cpu_divu_64_32_set(` call (using
directive nesting depth, not a naive first-`#endif` match, so it is not
fooled by the nested SM64_SATURN_TEST_MUTATE_* blocks inside those same
branches) and asserts the DVCR clear immediately precedes the launch,
matching the proven-correct pattern in gpl/slavedriver_projection.h. This
test would have failed against the pre-fix source. It cannot replace
target verification -- only real SH-2 hardware (or a cycle-accurate
emulator) can prove DVCR actually behaves this way.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

DIVU_CALL_RE = re.compile(r"cpu_divu_64_32_set\(")
DVCR_CLEAR_RE = re.compile(r"\*SM64_SATURN_DIVU_DVCR\s*&=\s*~1u;")
SH_IF_RE = re.compile(r"^[ \t]*#[ \t]*if[ \t]+defined\(__sh__\)[ \t]*$")
DIRECTIVE_RE = re.compile(r"^[ \t]*#[ \t]*(if|ifdef|ifndef|else|elif|endif)\b")
OPENING_KINDS = frozenset({"if", "ifdef", "ifndef"})


def _sh_true_branch_with_divu_call(source: str) -> str:
    """Return the '#if defined(__sh__)' true-branch body that contains a
    cpu_divu_64_32_set( call.

    Tracks directive nesting depth explicitly rather than matching the
    first '#else'/'#endif' textually, because the real branches each embed
    their own nested '#if defined(SM64_SATURN_TEST_MUTATE_*)' block.
    """
    lines = source.splitlines(keepends=True)
    index = 0
    while index < len(lines):
        if not SH_IF_RE.match(lines[index]):
            index += 1
            continue
        depth = 1
        start = index + 1
        cursor = start
        end: int | None = None
        while cursor < len(lines):
            directive = DIRECTIVE_RE.match(lines[cursor])
            if directive is not None:
                kind = directive.group(1)
                if kind in OPENING_KINDS:
                    depth += 1
                elif kind == "else" and depth == 1:
                    end = cursor
                    break
                elif kind == "endif":
                    depth -= 1
                    if depth == 0:
                        end = cursor
                        break
            cursor += 1
        if end is None:
            raise AssertionError(
                "unterminated '#if defined(__sh__)' block starting at "
                f"source line {index + 1}"
            )
        body = "".join(lines[start:end])
        if DIVU_CALL_RE.search(body):
            return body
        index = end + 1
    raise AssertionError(
        "no '#if defined(__sh__)' branch containing a cpu_divu_64_32_set( "
        "call was found"
    )


class DivuOverflowClearContractTest(unittest.TestCase):
    def _assert_clear_immediately_precedes_launch(self, relative_path: str) -> None:
        source = (ROOT / relative_path).read_text(encoding="utf-8")
        branch = _sh_true_branch_with_divu_call(source)

        call_matches = list(DIVU_CALL_RE.finditer(branch))
        self.assertEqual(
            len(call_matches), 1,
            f"{relative_path}: expected exactly one cpu_divu_64_32_set( "
            f"call in its __sh__ branch, found {len(call_matches)}",
        )
        call_start = call_matches[0].start()

        clear_matches = list(DVCR_CLEAR_RE.finditer(branch[:call_start]))
        self.assertTrue(
            clear_matches,
            f"{relative_path}: cpu_divu_64_32_set( is not preceded by "
            "'*SM64_SATURN_DIVU_DVCR &= ~1u;' anywhere in its __sh__ "
            "branch -- DVCR's overflow bit is sticky, so a prior "
            "division's overflow silently corrupts this call's result "
            "(see gpl/slavedriver_projection.h's proven-correct pattern)",
        )

        gap = branch[clear_matches[-1].end():call_start]
        self.assertEqual(
            gap.strip(), "",
            f"{relative_path}: the DVCR clear must immediately precede "
            f"cpu_divu_64_32_set( -- found intervening code: {gap!r}",
        )

    def test_atan2_q16_index_clears_dvcr_before_divu_launch(self) -> None:
        self._assert_clear_immediately_precedes_launch(
            "src/port/saturn/runtime/saturn_engine_math_q16.h"
        )

    def test_div_s64_s32_clears_dvcr_before_divu_launch(self) -> None:
        self._assert_clear_immediately_precedes_launch(
            "src/port/saturn/gfx/saturn_render_native_math.h"
        )

    def test_guard_actually_detects_a_missing_clear(self) -> None:
        """Prove the guard itself is not a tautology: strip the clear from a
        copy of the source and confirm the assertion now fails."""
        source = (ROOT / "src/port/saturn/gfx/saturn_render_native_math.h").read_text(
            encoding="utf-8"
        )
        mutated = DVCR_CLEAR_RE.sub("", source, count=1)
        self.assertNotEqual(mutated, source)
        branch = _sh_true_branch_with_divu_call(mutated)
        call_start = DIVU_CALL_RE.search(branch).start()
        self.assertFalse(
            list(DVCR_CLEAR_RE.finditer(branch[:call_start])),
            "mutated fixture unexpectedly still contains a DVCR clear",
        )


if __name__ == "__main__":
    unittest.main()
