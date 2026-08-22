#!/usr/bin/env python3
"""Host contracts for folding G_CC_MODULATEIA shade into the baked CLUT.

The source coin textures are IA16 -- greyscale plus alpha -- so an actor's
only colour lives in the Vtx shade bytes, and G_CC_MODULATEIA is
texture-intensity times shade. Neither VDP1 recipe reproduces that multiply:
CLUT16_REPLACE ignores shade outright, and VDP1 textured Gouraud is additive.
Premultiplying the shade into the palette at bake time is exact and free.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import actor_material_v2 as material


def _rgb(value: int) -> tuple[int, int, int]:
    return value & 0x1F, (value >> 5) & 0x1F, (value >> 10) & 0x1F


class _Signature:
    def __init__(self, combine_mode):
        self.combine_mode = combine_mode


def _triangle(combine_mode, shade):
    return {"signature": _Signature(combine_mode), "shade": shade}


YELLOW = [[0xFF, 0xFF, 0x00, 0xFF]] * 3
MODULATE_IA = ("G_CC_MODULATEIA", "G_CC_MODULATEIA")
OTHER = ("G_CC_MODULATERGB", "G_CC_MODULATERGB")


class ModulateRgb1555Tests(unittest.TestCase):
    def test_white_times_yellow_is_pure_yellow_at_full_intensity(self) -> None:
        self.assertEqual(_rgb(material._modulate_rgb1555(0xFFFF, (255, 255, 0))),
                         (31, 31, 0))

    def test_grey_ramp_keeps_its_intensity_in_the_surviving_channels(self) -> None:
        """The whole point: an intensity ramp must stay a ramp, tinted."""
        for level in (1, 7, 15, 23, 31):
            texel = 0x8000 | level | (level << 5) | (level << 10)
            red, green, blue = _rgb(material._modulate_rgb1555(texel, (255, 255, 0)))
            self.assertEqual((red, green, blue), (level, level, 0))

    def test_transparent_texels_are_untouched(self) -> None:
        """Index-zero transparency must not become an opaque black texel."""
        self.assertEqual(material._modulate_rgb1555(0x0000, (255, 255, 0)), 0x0000)
        self.assertEqual(material._modulate_rgb1555(0x7FFF, (255, 255, 0)), 0x7FFF)

    def test_white_shade_is_the_identity(self) -> None:
        """Why smoke ({0xff,0xff,0xff,0xb4}) keeps a byte-identical bank."""
        for texel in (0x8000, 0x8421, 0xBDEF, 0xFFFF):
            self.assertEqual(material._modulate_rgb1555(texel, (255, 255, 255)),
                             texel)

    def test_black_shade_zeroes_colour_but_keeps_the_opaque_bit(self) -> None:
        self.assertEqual(material._modulate_rgb1555(0xFFFF, (0, 0, 0)), 0x8000)

    def test_no_channel_can_overflow_five_bits(self) -> None:
        for shade in ((255, 255, 255), (254, 200, 128), (1, 2, 3)):
            for level in range(32):
                texel = 0x8000 | level | (level << 5) | (level << 10)
                for component in _rgb(material._modulate_rgb1555(texel, shade)):
                    self.assertLessEqual(component, 0x1F)

    def test_scaling_rounds_to_nearest_rather_than_truncating(self) -> None:
        """Truncation loses the darkest surviving step: intensity 1 at half
        shade is 0.502 of a step, which must survive as 1 and not vanish."""
        self.assertEqual(material._modulate_rgb1555(0x8001, (128, 0, 0)) & 0x1F, 1)
        self.assertEqual(material._modulate_rgb1555(0x8003, (128, 0, 0)) & 0x1F, 2)

    def test_a_shade_above_the_byte_range_still_clamps_to_five_bits(self) -> None:
        """_modulate_rgb1555 is reachable independently of _folded_shade's
        range check, so its own clamp has to hold."""
        self.assertEqual(material._modulate_rgb1555(0xFFFF, (512, 512, 512)),
                         0xFFFF)

    def test_scaling_is_monotonic_in_intensity(self) -> None:
        previous = -1
        for level in range(32):
            texel = 0x8000 | level
            current = material._modulate_rgb1555(texel, (128, 128, 128)) & 0x1F
            self.assertGreaterEqual(current, previous)
            previous = current


class FoldedShadeSelectionTests(unittest.TestCase):
    def test_modulateia_returns_the_constant_shade(self) -> None:
        self.assertEqual(
            material._folded_shade(_triangle(MODULATE_IA, YELLOW)), (255, 255, 0))

    def test_other_combiners_fold_nothing(self) -> None:
        """Folding outside MODULATEIA would silently rewrite unrelated banks."""
        self.assertIsNone(material._folded_shade(_triangle(OTHER, YELLOW)))

    def test_varying_shade_fails_closed(self) -> None:
        varying = [[0xFF, 0xFF, 0x00, 0xFF], [0xFF, 0x00, 0x00, 0xFF],
                   [0xFF, 0xFF, 0x00, 0xFF]]
        with self.assertRaises(material.ActorMaterialV2Error) as caught:
            material._folded_shade(_triangle(MODULATE_IA, varying))
        self.assertIn("varies", str(caught.exception))

    def test_missing_shade_fails_closed(self) -> None:
        with self.assertRaises(material.ActorMaterialV2Error):
            material._folded_shade(_triangle(MODULATE_IA, []))

    def test_non_opaque_shade_alpha_is_accepted_and_ignored(self) -> None:
        """Smoke is {0xff,0xff,0xff,0xb4}. CLUT16 carries transparency as index
        zero against a fixed threshold, so a fractional shade alpha has nowhere
        to live; refusing to bake would regress a model that bakes today."""
        smoke = [[0xFF, 0xFF, 0xFF, 0xB4]] * 3
        self.assertEqual(
            material._folded_shade(_triangle(MODULATE_IA, smoke)), (255, 255, 255))

    def test_out_of_range_shade_fails_closed(self) -> None:
        with self.assertRaises(material.ActorMaterialV2Error):
            material._folded_shade(_triangle(MODULATE_IA, [[0x1FF, 0, 0, 0xFF]] * 3))


class SwitchSelectorTests(unittest.TestCase):
    """The Saturn bank bakes every GEO_SWITCH_CASE branch, so each primitive
    carries the case that selects it or the runtime draws all of them at once."""

    @staticmethod
    def _tri(part):
        return {"branch_ordinal": part}

    def test_case_zero_is_not_encoded_as_zero(self) -> None:
        """Zero means 'unconditional'. Without the +1 bias every case-zero
        primitive would draw in every case -- for a coin, one of its eight
        spin phases would be permanently superimposed on the others."""
        self.assertEqual(
            material._switch_selector(self._tri(0), {0: (0, 0)}), 0x0001)

    def test_case_and_switch_pack_into_their_own_bytes(self) -> None:
        self.assertEqual(
            material._switch_selector(self._tri(5), {5: (0, 7)}), 0x0008)
        self.assertEqual(
            material._switch_selector(self._tri(9), {9: (2, 3)}), 0x0204)

    def test_a_part_outside_any_switch_is_unconditional(self) -> None:
        self.assertEqual(material._switch_selector(self._tri(4), {7: (0, 1)}), 0)

    def test_a_bank_without_switches_tags_nothing(self) -> None:
        """This is what keeps switch-free banks byte-identical."""
        self.assertEqual(material._switch_selector(self._tri(0), {}), 0)

    def test_out_of_range_switch_or_case_fails_closed(self) -> None:
        with self.assertRaises(material.ActorMaterialV2Error):
            material._switch_selector(self._tri(0), {0: (0x100, 0)})
        with self.assertRaises(material.ActorMaterialV2Error):
            material._switch_selector(self._tri(0), {0: (0, 0xFF)})

    def test_every_encodable_case_keeps_a_non_zero_case_field(self) -> None:
        """The runtime and both validators treat a zero case field in a
        non-zero record as malformed, so the encoder must never emit one."""
        for switch in (0, 1, 255):
            for case in (0, 1, 127, 254):
                selector = material._switch_selector(
                    self._tri(0), {0: (switch, case)})
                self.assertNotEqual(selector & 0xFF, 0)
                self.assertEqual(selector >> 8, switch)
                self.assertEqual((selector & 0xFF) - 1, case)


if __name__ == "__main__":
    raise SystemExit(unittest.main())
