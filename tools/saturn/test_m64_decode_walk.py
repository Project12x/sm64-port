#!/usr/bin/env python3
"""RED/GREEN tests for the m64 sequence-level decode-walk validator.

Every committed fixture is synthetic (hand-assembled from the sequence VM's
opcode tables in src/port/saturn/audio68k/sequence_vm.c) -- no Nintendo
sequence bytes are committed.  The real-repo regression test at the bottom
reads the user-extracted `sound/sequences/us/*.m64` set from disk at test
time, mirroring how test_compile_saturn_audio.py consumes the same inputs.

Fixture policy notes (verified against the real US m64 set, 2026-08-09):
19 of 34 real sequences -- every looping level-music script -- terminate in
an intentional `0xfb` jump-back loop and never reach a sequence-level 0xFF.
The walker therefore treats a loop (merge into already-decoded code) as a
legal path terminator when the cycle carries a delay opcode (0xfd/0xfe), as
every real looper does; a delay-free cycle exhausts the VM's per-tick
instruction budget and faults on the first tick, so it is a finding.
Falling off EOF or dying mid-opcode is a termination defect.
"""
from __future__ import annotations

import struct
import unittest
from pathlib import Path

from m64_decode_walk import (FINDING_BUDGET, FINDING_DELAY_FREE_LOOP,
                             FINDING_EMPTY, FINDING_OVERLAP,
                             FINDING_OVERSIZED, FINDING_RUNS_PAST_END,
                             FINDING_TARGET_MID_INSTRUCTION,
                             FINDING_TARGET_OUT_OF_RANGE, FINDING_TRUNCATED,
                             FINDING_UNKNOWN_OPCODE, FORMAT_EU_SH, FORMAT_US,
                             walk_sequence)

ROOT = Path(__file__).resolve().parents[2]


def valid_sequence() -> bytes:
    """Minimal structurally valid US sequence script with one channel."""
    return bytes([
        0xd3, 0x20,        # 0x00 mutebhv
        0xd5, 0x32,        # 0x02 mutescale
        0xd7, 0x00, 0x01,  # 0x04 initchan(ch0)
        0x90, 0x00, 0x14,  # 0x07 startchannel 0 -> 0x14 (opaque channel bytes)
        0xdd, 0x78,        # 0x0a tempo
        0xdb, 0x66,        # 0x0c vol
        0xfd, 0x40,        # 0x0e delay 0x40
        0xd6, 0x00, 0x01,  # 0x10 disablechan
        0xff,              # 0x13 end
        0x01, 0x02, 0x03, 0x04,  # 0x14 opaque channel-script bytes
    ])


def looping_sequence() -> bytes:
    """Music-style script: 0xfb jump-back loop, end opcode never reached."""
    return bytes([
        0xd3, 0x20,        # 0x00 mutebhv
        0x90, 0x00, 0x0a,  # 0x02 startchannel 0 -> 0x0a
        0xfd, 0x40,        # 0x05 delay
        0xfb, 0x00, 0x05,  # 0x07 jump -> 0x05 (intentional infinite loop)
        0x01, 0x02,        # 0x0a opaque channel-script bytes
    ])


def kinds(findings) -> list[str]:
    return [finding.kind for finding in findings]


class ValidSequenceTest(unittest.TestCase):
    def test_valid_minimal_sequence_passes(self) -> None:
        ok, findings = walk_sequence(valid_sequence(), FORMAT_US)
        self.assertTrue(ok)
        self.assertEqual(findings, [])

    def test_looping_music_shape_passes(self) -> None:
        # Real level music loops forever at the sequence level (verified:
        # 19/34 real US m64s); a loop terminator whose cycle carries a
        # delay -- as every real looper's does -- must not be a finding.
        ok, findings = walk_sequence(looping_sequence(), FORMAT_US)
        self.assertTrue(ok)
        self.assertEqual(findings, [])

    def test_call_and_return_pass(self) -> None:
        payload = bytes([
            0xfc, 0x00, 0x06,  # 0x00 call -> 0x06
            0xfd, 0x10,        # 0x03 delay (return point)
            0xff,              # 0x05 end
            0xd3, 0x20,        # 0x06 subroutine body
            0xff,              # 0x08 subroutine return
        ])
        ok, findings = walk_sequence(payload, FORMAT_US)
        self.assertTrue(ok)
        self.assertEqual(findings, [])

    def test_conditional_branch_explores_both_paths(self) -> None:
        # Fall-through path is clean; the taken path lands on an opcode the
        # VM rejects.  A one-path walker would miss it.
        payload = bytes([
            0xfa, 0x00, 0x05,  # 0x00 beqz -> 0x05
            0xff,              # 0x03 fall-through end
            0x00,              # 0x04 padding (unreachable)
            0xe0,              # 0x05 invalid sequence-level opcode
        ])
        ok, findings = walk_sequence(payload, FORMAT_US)
        self.assertFalse(ok)
        self.assertIn(FINDING_UNKNOWN_OPCODE, kinds(findings))
        self.assertEqual(findings[0].offset, 0x05)


class DefectClassTest(unittest.TestCase):
    def test_out_of_range_branch_target(self) -> None:
        ok, findings = walk_sequence(bytes([0xfb, 0x7f, 0xff]), FORMAT_US)
        self.assertFalse(ok)
        self.assertEqual(kinds(findings), [FINDING_TARGET_OUT_OF_RANGE])
        self.assertEqual(findings[0].offset, 0)

    def test_negative_branch_target(self) -> None:
        # vm_read_target treats the raw big-endian value as signed and
        # rejects negatives; \xfc\xff\xff is the canonical malformed fixture.
        ok, findings = walk_sequence(bytes([0xfc, 0xff, 0xff, 0xff]), FORMAT_US)
        self.assertFalse(ok)
        self.assertIn(FINDING_TARGET_OUT_OF_RANGE, kinds(findings))
        self.assertEqual(findings[0].offset, 0)

    def test_delay_free_self_loop_is_a_finding(self) -> None:
        # Delay-free unconditional self-loop: the visited set terminates the
        # walk promptly (no hang), and the shape is a defect -- unlike real
        # looping music (whose loop always carries an 0xfd delay), a cycle
        # with no delay opcode exhausts vm_tick_sequence's 64-instruction
        # budget and faults the VM on the first tick (sequence_vm.c:231).
        ok, findings = walk_sequence(bytes([0xfb, 0x00, 0x00]), FORMAT_US)
        self.assertFalse(ok)
        self.assertEqual(kinds(findings), [FINDING_DELAY_FREE_LOOP])
        self.assertEqual(findings[0].offset, 0)

    def test_with_delay_self_loop_passes(self) -> None:
        # The real looping-music shape in miniature: the jump-back cycle
        # contains a delay, so each tick stops instead of faulting.
        ok, findings = walk_sequence(bytes([0xfd, 0x40, 0xfb, 0x00, 0x00]),
                                     FORMAT_US)
        self.assertTrue(ok)
        self.assertEqual(findings, [])
        # 0xfe (delay 1) stops the tick just the same.
        ok, findings = walk_sequence(bytes([0xfe, 0xfb, 0x00, 0x00]),
                                     FORMAT_US)
        self.assertTrue(ok)
        self.assertEqual(findings, [])

    def test_delay_free_conditional_cycle_is_a_finding(self) -> None:
        # A conditional back-branch forms a cycle too; with no delay on it
        # the first tick with a looping value faults the same way.
        ok, findings = walk_sequence(bytes([0xfa, 0x00, 0x00, 0xff]),
                                     FORMAT_US)
        self.assertFalse(ok)
        self.assertEqual(kinds(findings), [FINDING_DELAY_FREE_LOOP])

    def test_delay_inside_called_subroutine_breaks_the_cycle(self) -> None:
        # The loop body calls a subroutine that delays: dynamically every
        # iteration executes the callee's 0xfd, so the cycle through the
        # call's return point is delay-bearing and legal.
        with_delay = bytes([
            0xfc, 0x00, 0x06,  # 0x00 call -> 0x06
            0xfb, 0x00, 0x00,  # 0x03 jump -> 0x00 (loop)
            0xfd, 0x10,        # 0x06 subroutine delay
            0xff,              # 0x08 subroutine return
        ])
        ok, findings = walk_sequence(with_delay, FORMAT_US)
        self.assertTrue(ok)
        self.assertEqual(findings, [])
        # The same loop calling a delay-free subroutine still faults.
        without_delay = bytes([
            0xfc, 0x00, 0x06,  # 0x00 call -> 0x06
            0xfb, 0x00, 0x00,  # 0x03 jump -> 0x00 (loop)
            0xd3, 0x20,        # 0x06 subroutine body (no delay)
            0xff,              # 0x08 subroutine return
        ])
        ok, findings = walk_sequence(without_delay, FORMAT_US)
        self.assertFalse(ok)
        self.assertEqual(kinds(findings), [FINDING_DELAY_FREE_LOOP])

    def test_work_budget_reports_exhaustion(self) -> None:
        ok, findings = walk_sequence(valid_sequence(), FORMAT_US,
                                     max_instructions=3)
        self.assertFalse(ok)
        self.assertIn(FINDING_BUDGET, kinds(findings))

    def test_truncated_mid_opcode(self) -> None:
        ok, findings = walk_sequence(bytes([0xd3, 0x20, 0xdd]), FORMAT_US)
        self.assertFalse(ok)
        self.assertEqual(kinds(findings), [FINDING_TRUNCATED])
        self.assertEqual(findings[0].offset, 2)

    def test_truncated_variable_length_operand(self) -> None:
        # 0xfd with a set high bit needs a second operand byte.
        ok, findings = walk_sequence(bytes([0xfd, 0x81]), FORMAT_US)
        self.assertFalse(ok)
        self.assertEqual(kinds(findings), [FINDING_TRUNCATED])
        self.assertEqual(findings[0].offset, 0)

    def test_truncated_valid_fixture_fails(self) -> None:
        # The Task 1 quality-review gap: a truncated real-shaped sequence
        # sails through the byte-scan heuristic.  The walker must fail it.
        channel_free = bytes([0xd3, 0x20, 0xd5, 0x32, 0xdd, 0x78,
                              0xdb, 0x66, 0xfd, 0x40, 0xff])
        ok, findings = walk_sequence(channel_free, FORMAT_US)
        self.assertTrue(ok)
        ok, findings = walk_sequence(channel_free[:5], FORMAT_US)
        self.assertFalse(ok)
        self.assertEqual(kinds(findings), [FINDING_TRUNCATED])
        self.assertEqual(findings[0].offset, 4)
        # Truncating the channel-bearing fixture also fails closed (the cut
        # channel pointer now dangles past EOF).
        ok, findings = walk_sequence(valid_sequence()[:0x0b], FORMAT_US)
        self.assertFalse(ok)
        self.assertEqual(kinds(findings), [FINDING_TARGET_OUT_OF_RANGE])

    def test_runs_past_end_without_terminator(self) -> None:
        ok, findings = walk_sequence(bytes([0xd3, 0x20, 0xfe]), FORMAT_US)
        self.assertFalse(ok)
        self.assertEqual(kinds(findings), [FINDING_RUNS_PAST_END])
        self.assertEqual(findings[0].offset, 3)

    def test_channel_pointer_past_eof(self) -> None:
        payload = bytes([0xd3, 0x20, 0x90, 0x00, 0x40, 0xff])
        ok, findings = walk_sequence(payload, FORMAT_US)
        self.assertFalse(ok)
        self.assertEqual(kinds(findings), [FINDING_TARGET_OUT_OF_RANGE])
        self.assertEqual(findings[0].offset, 2)
        self.assertIn("channel", findings[0].detail)

    def test_unknown_opcode_families(self) -> None:
        for opcode in (0x30, 0xb0, 0xc0, 0xe7):
            ok, findings = walk_sequence(bytes([opcode, 0xff]), FORMAT_US)
            self.assertFalse(ok, f"opcode 0x{opcode:02x} must be rejected")
            self.assertEqual(kinds(findings), [FINDING_UNKNOWN_OPCODE])

    def test_branch_target_mid_instruction(self) -> None:
        payload = bytes([
            0xfa, 0x00, 0x04,  # 0x00 beqz -> 0x04 (inside 0xd3's operand)
            0xd3, 0x20,        # 0x03 mutebhv (operand at 0x04)
            0xff,              # 0x05 end
        ])
        ok, findings = walk_sequence(payload, FORMAT_US)
        self.assertFalse(ok)
        self.assertEqual(kinds(findings), [FINDING_TARGET_MID_INSTRUCTION])
        self.assertEqual(findings[0].offset, 0)

    def test_overlapping_decode(self) -> None:
        payload = bytes([
            0xfa, 0x00, 0x06,  # 0x00 beqz -> 0x06 (decoded before 0x06's use
            0xfb, 0x00, 0x07,  # 0x03 jump -> 0x07  as an operand below)
            0xd3, 0xff,        # 0x06 mutebhv consuming 0x07 (= decoded 0xff)
        ])
        ok, findings = walk_sequence(payload, FORMAT_US)
        self.assertFalse(ok)
        self.assertIn(FINDING_OVERLAP, kinds(findings))

    def test_empty_and_oversized(self) -> None:
        ok, findings = walk_sequence(b"", FORMAT_US)
        self.assertFalse(ok)
        self.assertEqual(kinds(findings), [FINDING_EMPTY])
        ok, findings = walk_sequence(bytes(0x10000), FORMAT_US)
        self.assertFalse(ok)
        self.assertEqual(kinds(findings), [FINDING_OVERSIZED])


class FormatSelectionTest(unittest.TestCase):
    def test_us_reserve_notes_f2_takes_u8(self) -> None:
        ok, findings = walk_sequence(bytes([0xf2, 0x08, 0xff]), FORMAT_US)
        self.assertTrue(ok)
        self.assertEqual(findings, [])

    def test_us_f1_takes_no_operand(self) -> None:
        ok, findings = walk_sequence(bytes([0xf1, 0xff]), FORMAT_US)
        self.assertTrue(ok)

    def test_us_rejects_f0_and_relative_branches(self) -> None:
        for payload in (bytes([0xf0, 0xff]), bytes([0xf4, 0x00, 0xff]),
                        bytes([0xf3, 0x00, 0xff])):
            ok, findings = walk_sequence(payload, FORMAT_US)
            self.assertFalse(ok)
            self.assertEqual(kinds(findings), [FINDING_UNKNOWN_OPCODE])

    def test_eu_sh_reserve_notes_f1_takes_u8(self) -> None:
        ok, findings = walk_sequence(bytes([0xf1, 0x08, 0xff]), FORMAT_EU_SH)
        self.assertTrue(ok)
        ok, findings = walk_sequence(bytes([0xf0, 0xff]), FORMAT_EU_SH)
        self.assertTrue(ok)

    def test_eu_sh_rejects_da_dc(self) -> None:
        for opcode in (0xda, 0xdc):
            ok, findings = walk_sequence(bytes([opcode, 0x10, 0xff]),
                                         FORMAT_EU_SH)
            self.assertFalse(ok)
            self.assertEqual(kinds(findings), [FINDING_UNKNOWN_OPCODE])
            # The same opcodes are valid u8-operand commands in US format.
            ok, _ = walk_sequence(bytes([opcode, 0x10, 0xff]), FORMAT_US)
            self.assertTrue(ok)

    def test_eu_sh_relative_jump(self) -> None:
        # 0xf4 is an unconditional relative jump: the byte after it must be
        # skipped, so an invalid opcode there stays unreachable.
        payload = bytes([0xf4, 0x01, 0xe0, 0xff])
        ok, findings = walk_sequence(payload, FORMAT_EU_SH)
        self.assertTrue(ok)
        self.assertEqual(findings, [])

    def test_eu_sh_relative_branch_underflow(self) -> None:
        # Displacement -0x80 from pc=2 underflows; vm_flow rejects it.
        ok, findings = walk_sequence(bytes([0xf3, 0x80, 0xff]), FORMAT_EU_SH)
        self.assertFalse(ok)
        self.assertIn(FINDING_TARGET_OUT_OF_RANGE, kinds(findings))

    def test_unknown_format_rejected(self) -> None:
        with self.assertRaises(ValueError):
            walk_sequence(bytes([0xff]), "jp")


class RealRepoSequencesTest(unittest.TestCase):
    """All real user-extracted sequences must pass the walker (US format)."""

    def test_all_real_m64_sequences_pass(self) -> None:
        seq_dir = ROOT / "sound" / "sequences" / "us"
        paths = sorted(seq_dir.glob("*.m64"))
        self.assertEqual(len(paths), 34, "expected the 34 extracted US m64s")
        for path in paths:
            ok, findings = walk_sequence(path.read_bytes(), FORMAT_US)
            self.assertTrue(
                ok, f"{path.name}: {[str(finding) for finding in findings[:4]]}")

    def test_generated_seq00_passes_when_present(self) -> None:
        bank = ROOT / "build" / "saturn" / "audio" / "generated" / "sequences.bin"
        if not bank.is_file():
            self.skipTest("generated sequence bank not built "
                          "(run make compile-audio-sequences)")
        raw = bank.read_bytes()
        offset, length = struct.unpack_from(">II", raw, 4)
        ok, findings = walk_sequence(raw[offset:offset + length], FORMAT_US)
        self.assertTrue(
            ok, f"seq00: {[str(finding) for finding in findings[:4]]}")


if __name__ == "__main__":
    unittest.main()
