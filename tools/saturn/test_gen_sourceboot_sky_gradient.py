import unittest

from gen_sourceboot_sky_gradient import (
    BACKSCREEN_LINES,
    build_header,
    compute_gradient,
    rgb1555_pack,
)


class TestRgb1555Pack(unittest.TestCase):
    def test_matches_hardware_verified_bit_order(self):
        # raw = msb<<15 | blue<<10 | green<<5 | red -- see module docstring
        # for why this order (not the union's C declaration order) is the
        # one that is actually safe to trust.
        self.assertEqual(rgb1555_pack(1, 0, 0, 0), 0x8000)
        self.assertEqual(rgb1555_pack(0, 0x1F, 0, 0), 0x001F)
        self.assertEqual(rgb1555_pack(0, 0, 0x1F, 0), 0x03E0)
        self.assertEqual(rgb1555_pack(0, 0, 0, 0x1F), 0x7C00)

    def test_masks_out_of_range_inputs(self):
        # Every real caller passes in-range 0-31 channel values; the mask
        # only guards against a caller bug corrupting a neighboring field.
        self.assertEqual(rgb1555_pack(1, 0x3F, 0, 0), 0x801F)


class TestComputeGradient(unittest.TestCase):
    def test_line_count_and_no_uninitialized_entries(self):
        words = compute_gradient()
        self.assertEqual(len(words), BACKSCREEN_LINES)
        # RGB1555(1, r, g, b) always sets the msb bit and r is never 0 (its
        # floor is 1), so 0x0000 can never be a genuine entry -- if the loop
        # silently failed to run for some line, that slot would still read 0.
        self.assertNotIn(0x0000, words)

    def test_every_entry_has_msb_set(self):
        # RGB1555's first argument is a literal 1 for every line; a
        # packing-order regression that drops or mislocates the msb bit
        # would still pass a "values differ" smoke test but corrupts the
        # VDP1 transparent-code convention used elsewhere in this codebase
        # (0x0000 == transparent). Catch it directly.
        for word in compute_gradient():
            self.assertTrue(word & 0x8000, f"0x{word:04X} missing msb bit")

    def test_overhead_line_matches_hand_computed_value(self):
        # Line 0 is the brightest line (t = 223): r = 1 + 223//223 = 2,
        # g = 2 + 1784//223 = 10, b = 8 + 3345//223 = 23.
        # raw = 1<<15 | 23<<10 | 10<<5 | 2 = 0x8000 | 0x5C00 | 0x0140 | 0x0002
        words = compute_gradient()
        self.assertEqual(words[0], 0xDD42)

    def test_horizon_line_matches_hand_computed_value(self):
        # Line 223 is the darkest line (t = 0): r = 1 + 0 = 1, g = 2 + 0 = 2,
        # b = 8 + 0 = 8.
        # raw = 1<<15 | 8<<10 | 2<<5 | 1 = 0x8000 | 0x2000 | 0x0040 | 0x0001
        words = compute_gradient()
        self.assertEqual(words[BACKSCREEN_LINES - 1], 0xA041)


class TestBuildHeader(unittest.TestCase):
    def test_emits_expected_symbol_and_line_count(self):
        header = build_header()
        self.assertIn("#define SOURCEBOOT_SKY_GRADIENT_GENERATED_LINES 224U", header)
        self.assertIn(
            "static const rgb1555_t sourceboot_sky_gradient_generated[224] = {",
            header,
        )
        self.assertIn("{ .raw = 0xDD42 },", header)
        self.assertIn("{ .raw = 0xA041 },", header)

    def test_uses_designated_initializer_not_positional(self):
        # rgb1555_t is a union whose first declared member is the anonymous
        # {msb,b,g,r} bitfield struct, not `raw` -- a positional initializer
        # like `{ 0xDD42 }` would silently target the wrong member (and
        # truncate to msb's 1 bit) instead of setting the packed word.
        header = build_header()
        self.assertNotIn("{ 0xDD42 },", header)


if __name__ == "__main__":
    unittest.main()
