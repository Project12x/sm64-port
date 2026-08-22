#!/usr/bin/env python3
"""Equivalence oracle: the quarter-wave fold must reproduce the full table.

include/trig_tables_quarter.inc.c holds 1025 literals copied verbatim from the
first 1025 entries of include/trig_tables.inc.c. This test re-derives every
one of the 4,096 sine indices and all 4,096 cosine indices from the quarter
table using the same fold math as math_util.h, and compares the IEEE-754
bit patterns against the full shipped table.
"""

from __future__ import annotations

import re
import struct
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
FULL = REPO / "include" / "trig_tables.inc.c"
QUARTER = REPO / "include" / "trig_tables_quarter.inc.c"
LITERAL = re.compile(r"-?\d*\.\d+(?:[eE][-+]?\d+)?f")


def _literals(path: Path) -> list[str]:
    text = path.read_text(encoding="utf-8")
    body = text[text.index("{") + 1: text.rindex("}")]
    return LITERAL.findall(body)


def _bits(value: float) -> int:
    return struct.unpack(">I", struct.pack(">f", value))[0]


def _fold(quarter: list[float], index: int) -> float:
    """The exact arithmetic of saturn_sine_index() in src/engine/math_util.h."""
    offset = index & 0x3FF
    value = quarter[(1024 - offset) if (index & 0x400) else offset]
    return -value if (index & 0x800) else value


class QuarterSineEquivalenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.full = [float(v.rstrip("f")) for v in _literals(FULL)]
        cls.quarter = [float(v.rstrip("f")) for v in _literals(QUARTER)]

    def test_table_sizes_are_the_documented_ones(self) -> None:
        self.assertEqual(len(self.full), 5120)
        self.assertEqual(len(self.quarter), 1025)

    def test_quarter_literals_are_copied_verbatim_not_recomputed(self) -> None:
        """A re-printed float could round-trip differently; require the text."""
        self.assertEqual(_literals(QUARTER), _literals(FULL)[:1025])

    def test_every_sine_index_folds_bit_exactly_except_signed_zero(self) -> None:
        mismatches = [
            index for index in range(4096)
            if _bits(_fold(self.quarter, index)) != _bits(self.full[index])
        ]
        self.assertEqual(mismatches, [2048])
        # ...and that one is +0.0 versus -0.0, which compares equal.
        self.assertEqual(self.full[2048], 0.0)
        self.assertEqual(_fold(self.quarter, 2048), 0.0)

    def test_every_cosine_index_folds_bit_exactly_except_signed_zero(self) -> None:
        """coss(x) reads gSineTable[(x>>4) + 0x400]; the fold advances a
        quarter turn instead. Cosine index 1024 lands on sine index 2048, so
        it reaches the same single +0.0/-0.0 difference and no other."""
        mismatches = [
            index for index in range(4096)
            if _bits(_fold(self.quarter, (index + 0x400) & 0xFFF))
            != _bits(self.full[index + 0x400])
        ]
        self.assertEqual(mismatches, [1024])
        self.assertEqual(self.full[1024 + 0x400], 0.0)
        self.assertEqual(_fold(self.quarter, (1024 + 0x400) & 0xFFF), 0.0)

    def test_the_full_table_is_periodic_which_is_what_lets_coss_shift(self) -> None:
        for index in range(1024):
            self.assertEqual(_bits(self.full[4096 + index]),
                             _bits(self.full[index]))

    def test_quadrant_endpoints_are_exact(self) -> None:
        self.assertEqual(self.quarter[0], 0.0)
        self.assertEqual(self.quarter[1024], 1.0)
        self.assertEqual(_fold(self.quarter, 3072), -1.0)

    def test_fold_never_indexes_outside_the_quarter_table(self) -> None:
        for index in range(4096):
            offset = index & 0x3FF
            probe = (1024 - offset) if (index & 0x400) else offset
            self.assertTrue(0 <= probe <= 1024)

    def test_saved_bytes_match_the_documented_figure(self) -> None:
        self.assertEqual((5120 - 1025) * 4, 16380)


if __name__ == "__main__":
    raise SystemExit(unittest.main())
