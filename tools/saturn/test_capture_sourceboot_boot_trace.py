#!/usr/bin/env python3
"""Host-contract checks for the bounded sourceboot boot-trace reader."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

try:
    import capture_sourceboot_boot_trace as boot_trace
    from capture_sourceboot_boot_trace import (
        SOURCEBOOT_BOOT_TRACE_MAGIC,
        SOURCEBOOT_BOOT_TRACE_VERSION,
        decode_boot_trace,
        parse_symbol_address,
    )
except ModuleNotFoundError as error:
    raise AssertionError("sourceboot boot-trace reader is missing") from error


def words_to_bytes(words: list[int]) -> list[int]:
    data: list[int] = []
    for word in words:
        data.extend(word.to_bytes(4, byteorder="big"))
    return data


class SourcebootBootTraceReaderTests(unittest.TestCase):
    def test_decodes_last_boundary_with_raw_words(self) -> None:
        words = [
            SOURCEBOOT_BOOT_TRACE_MAGIC,
            SOURCEBOOT_BOOT_TRACE_VERSION,
            71,
            15,
            42,
            1,
            41,
            42,
        ]
        decoded = decode_boot_trace(words_to_bytes(words))
        self.assertEqual(decoded["stage"], 71)
        self.assertEqual(decoded["stage_id"], 15)
        self.assertEqual(decoded["observed_vblank_generation"], 42)
        self.assertEqual(decoded["scheduler_credit"], 1)
        self.assertEqual(decoded["vdp1_presentation_generation"], 41)
        self.assertEqual(decoded["vdp2_presentation_generation"], 42)
        self.assertEqual(decoded["raw_words"], words)

    def test_rejects_invalid_record_and_resolves_exact_symbol(self) -> None:
        invalid = words_to_bytes([0, SOURCEBOOT_BOOT_TRACE_VERSION, 0, 0, 0, 0, 0, 0])
        with self.assertRaisesRegex(ValueError, "magic"):
            decode_boot_trace(invalid)

        nm_output = "0601a2b0 B sourceboot_boot_trace\n0601a2d0 B another_symbol\n"
        self.assertEqual(parse_symbol_address(nm_output), 0x0601A2B0)
        with self.assertRaisesRegex(ValueError, "sourceboot_boot_trace"):
            parse_symbol_address("0601a2d0 B another_symbol\n")

    def test_rejects_zero_post_bios_frames_before_ymir_rpc(self) -> None:
        validate = getattr(boot_trace, "validate_post_bios_frames", None)
        self.assertTrue(callable(validate), "reader must validate post-BIOS frames")
        with self.assertRaisesRegex(ValueError, "between 1 and"):
            validate(0)
        self.assertEqual(validate(1), 1)


if __name__ == "__main__":
    unittest.main()
