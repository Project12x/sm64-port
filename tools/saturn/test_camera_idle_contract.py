"""SCC1 raw contract tests.

Breaks caught: accepting a malformed byte window, omitting a mapped sample
word, or treating a baseline/Q divergence as stable camera state.
"""
from __future__ import annotations

import math
import struct
import unittest

from camera_idle_contract import (
    Scc1Error, _f32_ulp, compare_camera_roles, compare_same_role, decode_scc1,
    validate_scc1,
)

SCC1_MAGIC = 0x53434331
SCC1_VERSION = 1
SCC1_HEADER_WORDS = 24
SCC1_SAMPLE_WORDS = 81
SCC1_SAMPLE_COUNT = 600
SCC1_PAYLOAD_WORDS = 48600
SCC1_BYTES = 194496
SCC1_ROUTE_ID = 2
Q_BRIDGES = (17, 19)

FLOAT_WORDS = set(range(4, 10)) | set(range(12, 30)) | set(range(32, 42)) | set(range(44, 51)) | set(range(52, 59)) | {61, 63} | set(range(65, 75)) | {78}
PACKED_WORDS = {10, 11, 30, 31, 42, 43, 51, 59, 60, 62, 64, 75, 76, 77, 79, 80}


def f32(value: float) -> int:
    return struct.unpack(">I", struct.pack(">f", value))[0]


def build_scc1(*, variant: int = 1, bridges: tuple[int, int] = (0, 0),
               generation: int | None = None, idle_start: int = 31) -> bytes:
    """Hand-written big endian SCC1 fixture; never imports production layout."""
    if generation is None:
        generation = 0 if variant == 1 else 7
    header = [
        SCC1_MAGIC, SCC1_VERSION, SCC1_HEADER_WORDS, SCC1_SAMPLE_WORDS,
        SCC1_SAMPLE_COUNT, variant, 2, 0x53425234, 4, 2000, idle_start,
        2000 + idle_start, 0, 0x3F, 0, 0, 0, 0, 0, bridges[0], bridges[1],
        generation, SCC1_PAYLOAD_WORDS, SCC1_ROUTE_ID,
    ]
    sample = [0] * SCC1_SAMPLE_WORDS
    sample[1] = 0
    sample[2] = 0x3F
    for offset in FLOAT_WORDS:
        sample[offset] = f32(1.25 + offset / 16.0)
    sample[71] = 0x43AF0000
    # Signed two's complement fixtures at every packed map position.
    for offset in PACKED_WORDS:
        sample[offset] = 0x8001FFFF if offset != 64 else 0xFFFFFFFE
    sample[77] &= 0xFFFF0000
    sample[80] &= 0xFFFF0000
    words = header[:]
    for tick in range(SCC1_SAMPLE_COUNT):
        item = sample[:]
        item[0] = header[11] + tick
        words.extend(item)
    return struct.pack(">48624I", *words)


def mutate_word(raw: bytes, word_index: int, value: int) -> bytes:
    values = list(struct.unpack(">48624I", raw))
    values[word_index] = value
    return struct.pack(">48624I", *values)


def mutate_every_sample_word(raw: bytes, offset: int, value: int) -> bytes:
    values = list(struct.unpack(">48624I", raw))
    for sample in range(SCC1_SAMPLE_COUNT):
        values[SCC1_HEADER_WORDS + sample * SCC1_SAMPLE_WORDS + offset] = value
    return struct.pack(">48624I", *values)


class Scc1DecodeTest(unittest.TestCase):
    def test_decodes_hand_written_big_endian_capture(self) -> None:
        capture = decode_scc1(build_scc1())
        self.assertEqual(capture.header[0], SCC1_MAGIC)
        self.assertEqual(len(capture.samples), 600)
        self.assertEqual(capture.samples[0].source_tick, 2031)
        self.assertEqual(capture.samples[-1].source_tick, 2630)
        self.assertEqual(capture.samples[0].state_words[67], 0x43AF0000)

    def test_rejects_wrong_byte_lengths(self) -> None:
        raw = build_scc1()
        for bad in (raw[:-1], raw + b"\0"):
            with self.assertRaises(Scc1Error):
                decode_scc1(bad)

    def test_rejects_each_header_word_mutation(self) -> None:
        raw = build_scc1()
        for offset in range(SCC1_HEADER_WORDS):
            with self.subTest(offset=offset), self.assertRaises(Scc1Error):
                decode_scc1(mutate_word(raw, offset, 0xDEADBEEF))

    def test_rejects_each_mapped_sample_word_mutation(self) -> None:
        raw = build_scc1()
        base = SCC1_HEADER_WORDS
        for offset in range(SCC1_SAMPLE_WORDS):
            with self.subTest(offset=offset), self.assertRaises(Scc1Error):
                capture = decode_scc1(mutate_word(raw, base + offset, 0xDEADBEEF))
                validate_scc1(capture, expected_role="camera-baseline",
                              expected_idle_start_tick=31, expected_route_id=2,
                              expected_bridge_counts=(0, 0))

    def test_rejects_later_state_drift_nonfinite_and_reserved_bits(self) -> None:
        raw = build_scc1()
        later = SCC1_HEADER_WORDS + SCC1_SAMPLE_WORDS + 4
        with self.assertRaises(Scc1Error):
            decode_scc1(mutate_word(raw, later, 0x7FC00000))
        with self.assertRaises(Scc1Error):
            decode_scc1(mutate_word(raw, SCC1_HEADER_WORDS + 2, 0x40))
        with self.assertRaises(Scc1Error):
            decode_scc1(mutate_word(raw, SCC1_HEADER_WORDS + 77, 1))

    def test_rejects_a_wrong_zoom_witness_even_when_every_sample_agrees(self) -> None:
        raw = mutate_every_sample_word(build_scc1(), 71, f32(349.0))
        with self.assertRaises(Scc1Error):
            decode_scc1(raw)


class Scc1ValidationTest(unittest.TestCase):
    def test_validates_baseline_and_q_role_contracts(self) -> None:
        baseline = decode_scc1(build_scc1())
        q = decode_scc1(build_scc1(variant=2, bridges=Q_BRIDGES))
        validate_scc1(baseline, expected_role="camera-baseline", expected_idle_start_tick=31,
                      expected_route_id=2, expected_bridge_counts=(0, 0))
        validate_scc1(q, expected_role="camera-q", expected_idle_start_tick=31,
                      expected_route_id=2, expected_bridge_counts=Q_BRIDGES)

    def test_rejects_nonneutral_input_missing_flag_route_zoom_generation_and_bridges(self) -> None:
        cases = [
            (SCC1_HEADER_WORDS + 1, 1), (SCC1_HEADER_WORDS + 2, 0x3D),
            (23, 3), (SCC1_HEADER_WORDS + 71, f32(349.0)), (21, 0), (19, 0),
        ]
        raw = build_scc1(variant=2, bridges=Q_BRIDGES)
        for index, value in cases:
            with self.subTest(word=index), self.assertRaises(Scc1Error):
                validate_scc1(decode_scc1(mutate_word(raw, index, value)),
                              expected_role="camera-q", expected_idle_start_tick=31,
                              expected_route_id=2, expected_bridge_counts=Q_BRIDGES)

    def test_same_role_requires_raw_byte_identity(self) -> None:
        raw = build_scc1()
        compare_same_role(decode_scc1(raw), decode_scc1(raw))
        changed = mutate_word(raw, SCC1_HEADER_WORDS + SCC1_SAMPLE_WORDS + 10, f32(3.0))
        with self.assertRaises(Scc1Error):
            compare_same_role(decode_scc1(raw), decode_scc1(changed))

    def test_cross_role_checks_packed_exactness_and_float_tolerance(self) -> None:
        baseline = decode_scc1(build_scc1())
        qraw = build_scc1(variant=2, bridges=Q_BRIDGES)
        compare_camera_roles(baseline, decode_scc1(qraw), q_fraction_bits=12)
        packed = mutate_word(qraw, SCC1_HEADER_WORDS + 10, 0)
        with self.assertRaises(Scc1Error):
            compare_camera_roles(baseline, decode_scc1(packed), q_fraction_bits=12)
        far = mutate_word(qraw, SCC1_HEADER_WORDS + 4, f32(3.0))
        with self.assertRaises(Scc1Error):
            compare_camera_roles(baseline, decode_scc1(far), q_fraction_bits=12)

    def test_cross_role_allows_one_binary32_ulp_when_q_tolerance_is_smaller(self) -> None:
        baseline_raw = build_scc1()
        baseline = decode_scc1(baseline_raw)
        qraw = build_scc1(variant=2, bridges=Q_BRIDGES)
        baseline_bits = struct.unpack(">48624I", baseline_raw)[SCC1_HEADER_WORDS + 4]
        q = decode_scc1(mutate_every_sample_word(qraw, 4, baseline_bits + 1))
        compare_camera_roles(baseline, q, q_fraction_bits=30)

    def test_binary32_ulp_at_finite_extremes_is_finite_and_signedness_independent(self) -> None:
        maximum = struct.unpack(">f", bytes.fromhex("7f7fffff"))[0]
        self.assertEqual(_f32_ulp(maximum), 2.0 ** 104)
        self.assertEqual(_f32_ulp(-maximum), 2.0 ** 104)


if __name__ == "__main__":
    unittest.main()
