import re
import unittest
from pathlib import Path

import gen_trig_q16

REPO = Path(__file__).resolve().parents[2]


class GenTrigQ16Test(unittest.TestCase):
    def test_parses_all_f32_entries(self):
        floats = gen_trig_q16.parse_trig_tables(
            REPO / "include" / "trig_tables.inc.c")
        # gSineTable[0x400] + gCosineTable[0x1000] = 0x1400 entries total,
        # matching math_util.h's overlapped-adjacent layout.
        self.assertEqual(len(floats), 0x1400)
        self.assertEqual(floats[0], 0.0)          # sin(0)
        self.assertEqual(floats[0x400], 1.0)      # cos(0)

    def test_q16_conversion_is_round_to_nearest(self):
        self.assertEqual(gen_trig_q16.to_q16(0.0), 0)
        self.assertEqual(gen_trig_q16.to_q16(1.0), 65536)
        self.assertEqual(gen_trig_q16.to_q16(-1.0), -65536)
        self.assertEqual(gen_trig_q16.to_q16(0.5), 32768)
        # round-half-away-from-zero, symmetric
        self.assertEqual(gen_trig_q16.to_q16(1.52587890625e-05), 1)

    def test_generated_file_matches_source_table(self):
        generated = REPO / "src" / "port" / "saturn" / "gfx" / \
            "saturn_trig_q16.inc.c"
        self.assertTrue(generated.exists(),
                        "run gen_trig_q16.py before this test")
        values = [int(v) for v in re.findall(
            r"(-?\d+),", generated.read_text())]
        floats = gen_trig_q16.parse_trig_tables(
            REPO / "include" / "trig_tables.inc.c")
        self.assertEqual(len(values), 0x1400)
        for i, f in enumerate(floats):
            self.assertEqual(values[i], gen_trig_q16.to_q16(f),
                             f"entry {i} stale -- regenerate")


if __name__ == "__main__":
    unittest.main(verbosity=2)
