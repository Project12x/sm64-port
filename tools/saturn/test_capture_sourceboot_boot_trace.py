#!/usr/bin/env python3
"""Host-contract checks for the bounded sourceboot boot-trace reader."""

from __future__ import annotations

import sys
import os
import tempfile
import unittest
from pathlib import Path
from subprocess import CompletedProcess


TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

try:
    import capture_sourceboot_boot_trace as boot_trace
    from capture_sourceboot_boot_trace import (
        SOURCEBOOT_BOOT_TRACE_MAGIC,
        SOURCEBOOT_BOOT_TRACE_VERSION,
        bind_capture_artifacts,
        capture_trace_checkpoint,
        decode_boot_trace,
        parse_cue_file_reference,
        parse_symbol_address,
        run_post_bios_window,
        validate_post_bios_checkpoint_interval,
    )
except ModuleNotFoundError as error:
    raise AssertionError("sourceboot boot-trace reader is missing") from error


def words_to_bytes(words: list[int]) -> list[int]:
    data: list[int] = []
    for word in words:
        data.extend(word.to_bytes(4, byteorder="big"))
    return data


class SourcebootBootTraceReaderTests(unittest.TestCase):
    def test_post_bios_window_preserves_single_legacy_checkpoint_without_interval(self) -> None:
        runs: list[int] = []
        checkpoints: list[str] = []

        run_post_bios_window(
            runs.append, checkpoints.append, post_bios_frames=180, checkpoint_interval=None
        )

        self.assertEqual(runs, [180])
        self.assertEqual(checkpoints, ["post-bios"])

    def test_post_bios_window_samples_every_chunk_including_final_remainder(self) -> None:
        runs: list[int] = []
        checkpoints: list[str] = []

        run_post_bios_window(
            runs.append, checkpoints.append, post_bios_frames=180, checkpoint_interval=64
        )

        self.assertEqual(runs, [64, 64, 52])
        self.assertEqual(
            checkpoints,
            ["post-bios-64", "post-bios-128", "post-bios-180"],
        )

    def test_rejects_non_positive_post_bios_checkpoint_interval(self) -> None:
        self.assertIsNone(validate_post_bios_checkpoint_interval(None))
        self.assertEqual(validate_post_bios_checkpoint_interval(1), 1)
        with self.assertRaisesRegex(ValueError, "positive"):
            validate_post_bios_checkpoint_interval(0)

    def test_checkpoint_records_raw_words_frame_and_stopped_pc(self) -> None:
        class Client:
            notifications = [
                {
                    "method": "instance.stopped",
                    "params": {"pc": 0x060402E8, "reason": "frame_limit"},
                }
            ]

            calls: list[dict[str, int]] = []

            def call(self, method: str, params: dict[str, int]) -> dict[str, list[int]]:
                self.calls.append(params)
                if params["address"] == 0x2608B43C:
                    return {"data": words_to_bytes([0x53394254, 1, 8, 8, 4, 1, 3, 4])}
                return {"data": words_to_bytes([0x045E02AA, 1, 2, 3, 4, 5, 6, 7])}

        client = Client()
        checkpoint = capture_trace_checkpoint(
            client, 0x0608B43C, "bios-initial-wait", 120
        )
        self.assertEqual(checkpoint["label"], "bios-initial-wait")
        self.assertEqual(checkpoint["emulated_frames"], 120)
        self.assertEqual(
            checkpoint["raw_words"], [0x045E02AA, 1, 2, 3, 4, 5, 6, 7]
        )
        self.assertEqual(checkpoint["stopped_pcs"], [0x060402E8])
        self.assertEqual(checkpoint["notification_count"], 1)
        self.assertEqual(checkpoint["p1"]["address"], 0x0608B43C)
        self.assertEqual(checkpoint["p1"]["raw_words"], checkpoint["raw_words"])
        self.assertEqual(checkpoint["p2"]["address"], 0x2608B43C)
        self.assertEqual(checkpoint["p2"]["raw_words"], [0x53394254, 1, 8, 8, 4, 1, 3, 4])
        self.assertEqual([call["address"] for call in client.calls], [0x0608B43C, 0x2608B43C])

    def test_cache_through_alias_preserves_existing_p2_address(self) -> None:
        alias = getattr(boot_trace, "cpu_cache_through_alias", None)
        self.assertTrue(callable(alias), "reader must derive a P2 cache-through alias")
        self.assertEqual(alias(0x0608B43C), 0x2608B43C)
        self.assertEqual(alias(0x2608B43C), 0x2608B43C)

    def test_artifact_binding_reports_cue_referenced_iso_and_elf(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cue = root / "sm64-saturn-sourceboot-e2.cue"
            iso = root / "sm64-saturn-sourceboot-e2.iso"
            elf = root / "obj" / "sm64-saturn-sourceboot-e2.elf"
            elf.parent.mkdir()
            cue.write_text('FILE "sm64-saturn-sourceboot-e2.iso" BINARY\n', encoding="utf-8")
            iso.write_bytes(b"disc-image")
            elf.write_bytes(b"linked-symbols")
            os.utime(elf, (100, 100))
            os.utime(iso, (200, 200))

            self.assertEqual(parse_cue_file_reference(cue), iso)
            artifacts = bind_capture_artifacts(cue, elf)
            self.assertEqual(artifacts["cue"]["path"], str(cue.resolve()))
            self.assertEqual(artifacts["iso"]["path"], str(iso.resolve()))
            self.assertEqual(artifacts["elf"]["path"], str(elf.resolve()))

    def test_artifact_binding_rejects_stale_or_wrapper_pair_before_ymir(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cue = root / "sm64-saturn-sourceboot-e2.cue"
            iso = root / "sm64-saturn-sourceboot-e2.iso"
            elf = root / "obj" / "sm64-saturn-sourceboot-e2.elf"
            elf.parent.mkdir()
            cue.write_text('FILE "sm64-saturn-sourceboot-e2.iso" BINARY\n', encoding="utf-8")
            iso.write_bytes(b"old-disc-image")
            elf.write_bytes(b"new-linked-symbols")
            os.utime(iso, (100, 100))
            os.utime(elf, (200, 200))
            with self.assertRaisesRegex(ValueError, "older than ELF"):
                bind_capture_artifacts(cue, elf)

            wrapper = root / "wrapper.cue"
            wrapper.write_text('FILE "sm64-saturn-sourceboot-e2.iso" BINARY\n', encoding="utf-8")
            os.utime(iso, (300, 300))
            with self.assertRaisesRegex(ValueError, "same build name"):
                bind_capture_artifacts(wrapper, elf)
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

    def test_decodes_pre_main_user_init_stages(self) -> None:
        data_initialized = decode_boot_trace(
            words_to_bytes(
                [
                    SOURCEBOOT_BOOT_TRACE_MAGIC,
                    SOURCEBOOT_BOOT_TRACE_VERSION,
                    0,
                    0,
                    0,
                    0,
                    0,
                    0,
                ]
            )
        )
        self.assertEqual(data_initialized["last_stage"], "elf-data-initialized")

        entry = decode_boot_trace(
            words_to_bytes(
                [
                    SOURCEBOOT_BOOT_TRACE_MAGIC,
                    SOURCEBOOT_BOOT_TRACE_VERSION,
                    1,
                    1,
                    0,
                    0,
                    0,
                    0,
                ]
            )
        )
        self.assertEqual(entry["last_stage"], "user-init-entry")

        callbacks_registered = decode_boot_trace(
            words_to_bytes(
                [
                    SOURCEBOOT_BOOT_TRACE_MAGIC,
                    SOURCEBOOT_BOOT_TRACE_VERSION,
                    2,
                    2,
                    0,
                    0,
                    0,
                    0,
                ]
            )
        )
        self.assertEqual(
            callbacks_registered["last_stage"], "user-init-callbacks-registered"
        )

    def test_rejects_invalid_record_and_resolves_exact_symbol(self) -> None:
        invalid = words_to_bytes([0, SOURCEBOOT_BOOT_TRACE_VERSION, 0, 0, 0, 0, 0, 0])
        with self.assertRaisesRegex(ValueError, "magic"):
            decode_boot_trace(invalid)

        nm_output = "0601a2b0 B sourceboot_boot_trace\n0601a2d0 B another_symbol\n"
        self.assertEqual(parse_symbol_address(nm_output), 0x0601A2B0)
        try:
            underscored_address = parse_symbol_address(
                "0601a2b0 B _sourceboot_boot_trace\n"
            )
        except ValueError as error:
            self.fail(f"reader must accept the target ABI's leading underscore: {error}")
        self.assertEqual(underscored_address, 0x0601A2B0)
        with self.assertRaisesRegex(ValueError, "sourceboot_boot_trace"):
            parse_symbol_address("0601a2d0 B another_symbol\n")

    def test_rejects_zero_post_bios_frames_before_ymir_rpc(self) -> None:
        validate = getattr(boot_trace, "validate_post_bios_frames", None)
        self.assertTrue(callable(validate), "reader must validate post-BIOS frames")
        with self.assertRaisesRegex(ValueError, "between 1 and"):
            validate(0)
        self.assertEqual(validate(1), 1)

    def test_resolves_symbol_from_dll_safe_wrapper_when_direct_nm_is_empty(self) -> None:
        command_builder = getattr(boot_trace, "wrapped_nm_command", None)
        self.assertTrue(callable(command_builder), "reader must use the MSYS toolchain wrapper")

        with self.assertRaisesRegex(ValueError, "sourceboot_boot_trace"):
            parse_symbol_address("")

        commands: list[list[str]] = []

        def wrapper_runner(command: list[str], **_: object) -> CompletedProcess[str]:
            commands.append(command)
            return CompletedProcess(
                command,
                0,
                stdout="0601a2b0 B sourceboot_boot_trace\n",
                stderr="",
            )

        self.assertEqual(
            boot_trace.resolve_trace_symbol(Path("trace.elf"), run=wrapper_runner),
            0x0601A2B0,
        )
        self.assertEqual(commands[0], command_builder(Path("trace.elf")))
        self.assertIn(str(boot_trace.MSYS_TOOLCHAIN_WRAPPER), commands[0])

    def test_invalid_trace_report_retains_raw_target_and_ymir_evidence(self) -> None:
        class Client:
            notifications = [
                {"method": "instance.ready", "params": {"model": "saturn"}},
                {"method": "instance.stopped", "params": {"reason": "crash"}},
            ]
            stderr = "x" * 32

        raw = words_to_bytes([0x045E02AA, 1, 2, 3, 4, 5, 6, 7])
        report_builder = getattr(boot_trace, "build_failed_trace_report", None)
        self.assertTrue(callable(report_builder), "invalid traces must still produce evidence")
        report = report_builder(
            client=Client(), raw_data=raw, error=ValueError("boot trace magic is 0x045e02aa")
        )
        self.assertEqual(report["trace"]["raw_bytes"], raw)
        self.assertEqual(report["trace"]["raw_words"], [0x045E02AA, 1, 2, 3, 4, 5, 6, 7])
        self.assertEqual(report["trace"]["decode_error"], "boot trace magic is 0x045e02aa")
        self.assertTrue(report["protocol"]["ready"])
        self.assertEqual(report["protocol"]["notifications"], Client.notifications)
        self.assertEqual(report["diagnostics"]["stderr"], Client.stderr)
        self.assertFalse(report["diagnostics"]["stderr_truncated"])

    def test_failed_trace_report_caps_ymir_stderr(self) -> None:
        class Client:
            notifications: list[dict[str, object]] = []
            stderr = "x" * (70 * 1024)

        report = boot_trace.build_failed_trace_report(
            client=Client(), raw_data=None, error=ValueError("no byte data")
        )
        self.assertTrue(report["diagnostics"]["stderr_truncated"])
        self.assertEqual(report["diagnostics"]["stderr_original_bytes"], 70 * 1024)
        self.assertLess(len(report["diagnostics"]["stderr"]), 70 * 1024)


if __name__ == "__main__":
    unittest.main()
