#!/usr/bin/env python3
"""Host contracts for the bounded sourceboot queue/cadence capture."""

from __future__ import annotations

import sys
import tempfile
import unittest
import json
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

try:
    import capture_sourceboot_throughput as capture
except ModuleNotFoundError as error:
    raise AssertionError("sourceboot throughput capture helper is missing") from error


BOOT_ADDRESS = 0x06010000
RUNTIME_ADDRESS = 0x06010100
QUEUE_ADDRESS = 0x06010200


def be_words(size: int, values: dict[int, int]) -> bytes:
    raw = bytearray(size)
    for offset, value in values.items():
        raw[offset : offset + 4] = value.to_bytes(4, "big")
    return bytes(raw)


def trace(vblank: int, presentation: int) -> bytes:
    return be_words(32, {16: vblank, 28: presentation})


def runtime(
    *, qn: int, qr: int, notify: int, retired: int,
    qm: tuple[int, int, int, int] = (1, 2, 3, 4),
    qs: tuple[int, int, int, int] = (5, 6, 7, 8),
    qw: int = 9, master_failures: int = 10, slave_failures: int = 11, qq: int = 12,
) -> bytes:
    values = {28: qn, 32: qr, 36: notify, 40: retired, 76: qw, 80: master_failures, 84: slave_failures, 88: qq}
    for index, value in enumerate(qm):
        values[44 + 4 * index] = value
    for index, value in enumerate(qs):
        values[60 + 4 * index] = value
    return be_words(92, values)


def queue(generation: int) -> bytes:
    return be_words(232, {224: generation})


def elf32_with_symbols(path: Path, symbols: list[tuple[str, int, int]], *, duplicate: bool = False) -> None:
    """Create a tiny big-endian ELF32 with one executable and one symbol table section."""
    names = b"\x00" + b"\x00".join(name.encode("ascii") for name, _, _ in symbols) + b"\x00"
    name_offsets: dict[str, int] = {}
    cursor = 1
    for name, _, _ in symbols:
        name_offsets[name] = cursor
        cursor += len(name) + 1
    entries = [(0, 0, 0)] + [(name_offsets[name], address, size) for name, address, size in symbols]
    if duplicate:
        name, address, size = symbols[0]
        entries.append((name_offsets[name], address + 4, size))
    section_offset = 52
    section_count = 4
    text_offset = 0x200
    symtab_offset = 0x240
    strtab_offset = symtab_offset + 16 * len(entries)
    image = bytearray(strtab_offset + len(names))
    image[:4] = b"\x7fELF"
    image[4] = 1
    image[5] = 2
    image[32:36] = section_offset.to_bytes(4, "big")
    image[46:48] = (40).to_bytes(2, "big")
    image[48:50] = section_count.to_bytes(2, "big")
    # text
    text = memoryview(image)[section_offset + 40 : section_offset + 80]
    text[4:8] = (1).to_bytes(4, "big")
    text[8:12] = (0x6).to_bytes(4, "big")
    text[12:16] = BOOT_ADDRESS.to_bytes(4, "big")
    text[16:20] = text_offset.to_bytes(4, "big")
    text[20:24] = (32).to_bytes(4, "big")
    image[text_offset : text_offset + 32] = bytes(range(32))
    # symtab
    symtab = memoryview(image)[section_offset + 80 : section_offset + 120]
    symtab[4:8] = (2).to_bytes(4, "big")
    symtab[16:20] = symtab_offset.to_bytes(4, "big")
    symtab[20:24] = (16 * len(entries)).to_bytes(4, "big")
    symtab[24:28] = (3).to_bytes(4, "big")
    symtab[36:40] = (16).to_bytes(4, "big")
    # strtab
    strtab = memoryview(image)[section_offset + 120 : section_offset + 160]
    strtab[4:8] = (3).to_bytes(4, "big")
    strtab[16:20] = strtab_offset.to_bytes(4, "big")
    strtab[20:24] = len(names).to_bytes(4, "big")
    for index, (name_offset, address, size) in enumerate(entries):
        start = symtab_offset + index * 16
        image[start : start + 4] = name_offset.to_bytes(4, "big")
        image[start + 4 : start + 8] = address.to_bytes(4, "big")
        image[start + 8 : start + 12] = size.to_bytes(4, "big")
        image[start + 12] = 0x11
        image[start + 14 : start + 16] = (1).to_bytes(2, "big")
    image[strtab_offset : strtab_offset + len(names)] = names
    path.write_bytes(image)


class ThroughputCaptureTests(unittest.TestCase):
    def test_resolves_exact_sized_symbols_and_rejects_wrong_missing_or_duplicate_symbols(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            elf = Path(directory) / "game.elf"
            elf32_with_symbols(elf, [
                ("_sourceboot_boot_trace", BOOT_ADDRESS, 32),
                ("_s_runtime", RUNTIME_ADDRESS, 92),
                ("_s_render_job_queue", QUEUE_ADDRESS, 232),
            ])
            self.assertEqual(
                capture.resolve_required_symbols(elf),
                {
                    "sourceboot_boot_trace": {"address": BOOT_ADDRESS, "size": 32},
                    "s_runtime": {"address": RUNTIME_ADDRESS, "size": 92},
                    "s_render_job_queue": {"address": QUEUE_ADDRESS, "size": 232},
                },
            )
            elf32_with_symbols(elf, [
                ("sourceboot_boot_trace", BOOT_ADDRESS, 31),
                ("s_runtime", RUNTIME_ADDRESS, 92),
                ("s_render_job_queue", QUEUE_ADDRESS, 232),
            ])
            with self.assertRaisesRegex(ValueError, "wrong size"):
                capture.resolve_required_symbols(elf)
            elf32_with_symbols(elf, [
                ("sourceboot_boot_trace", BOOT_ADDRESS, 32),
                ("s_runtime", RUNTIME_ADDRESS, 92),
                ("s_render_job_queue", QUEUE_ADDRESS, 232),
            ], duplicate=True)
            with self.assertRaisesRegex(ValueError, "duplicate"):
                capture.resolve_required_symbols(elf)
            elf32_with_symbols(elf, [
                ("sourceboot_boot_trace", BOOT_ADDRESS, 32),
                ("s_runtime", RUNTIME_ADDRESS, 92),
            ])
            with self.assertRaisesRegex(ValueError, "missing"):
                capture.resolve_required_symbols(elf)

    def test_proves_target_identity_with_exact_elf_bytes_before_observation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            elf = Path(directory) / "game.elf"
            elf32_with_symbols(elf, [
                ("sourceboot_boot_trace", BOOT_ADDRESS, 32),
                ("s_runtime", RUNTIME_ADDRESS, 92),
                ("s_render_job_queue", QUEUE_ADDRESS, 232),
            ])
            probe = capture.build_elf_identity_probe(elf)
            self.assertEqual(probe["address"], BOOT_ADDRESS)
            self.assertEqual(probe["expected_bytes"], list(range(16)))

            class Client:
                def call(self, method: str, params: dict[str, int]) -> dict[str, list[int]]:
                    if method != "mem.peek":
                        raise AssertionError(method)
                    return {"data": list(range(16))}

            self.assertTrue(capture.prove_target_identity(Client(), probe)["match"])
            with self.assertRaisesRegex(ValueError, "does not contain"):
                capture.prove_target_identity(type("Bad", (), {"call": lambda *_: {"data": [0] * 16}})(), probe)

    def test_decodes_every_big_endian_offset_and_phase_order(self) -> None:
        self.assertEqual(capture.decode_boot_trace(trace(0xFFFFFFFE, 7)), {"observed_vblank_generation": 0xFFFFFFFE, "vdp2_presentation_generation": 7})
        decoded = capture.decode_runtime(runtime(qn=13, qr=14, notify=15, retired=16))
        self.assertEqual(decoded["qn"], 13)
        self.assertEqual(decoded["qr"], 14)
        self.assertEqual(decoded["notify_sequence"], 15)
        self.assertEqual(decoded["retired_sequence"], 16)
        self.assertEqual(decoded["qm"], [1, 2, 3, 4])
        self.assertEqual(decoded["qs"], [5, 6, 7, 8])
        self.assertEqual(decoded["qw"], 9)
        self.assertEqual(decoded["qf"], 21)
        self.assertEqual(decoded["qq"], 12)
        self.assertEqual(capture.decode_queue_generation(queue(17)), 17)

    def test_rejects_inflight_generation_or_incoherent_sequences(self) -> None:
        good = capture.decode_runtime(runtime(qn=4, qr=4, notify=9, retired=9))
        self.assertEqual(capture.accept_coherent_queue(good, 0)["sequence"], 9)
        for bad_runtime, bad_generation in (
            (runtime(qn=4, qr=4, notify=9, retired=8), 0),
            (runtime(qn=4, qr=3, notify=9, retired=9), 0),
            (runtime(qn=0, qr=0, notify=9, retired=9), 0),
            (runtime(qn=4, qr=4, notify=0, retired=0), 0),
            (runtime(qn=4, qr=4, notify=9, retired=9), 1),
        ):
            self.assertIsNone(capture.accept_coherent_queue(capture.decode_runtime(bad_runtime), bad_generation))

    def test_rejects_attaching_one_queue_sequence_to_two_presentation_events(self) -> None:
        event = {"presentation_generation": 2, "queue": None}
        record = {"sequence": 9}
        used: set[int] = set()
        capture.attach_queue_record(event, record, used)
        with self.assertRaisesRegex(ValueError, "already attached"):
            capture.attach_queue_record({"presentation_generation": 3}, record, used)

    def test_cadence_math_handles_counter_wrap_and_skipped_vblanks(self) -> None:
        events = [
            {"vblank_generation": 0xFFFFFFFE, "presentation_generation": 8},
            {"vblank_generation": 1, "presentation_generation": 9},
            {"vblank_generation": 9, "presentation_generation": 10},
        ]
        summary = capture.summarize_cadence(events, nominal_refresh_hz=60.0)
        self.assertEqual(summary["guest_fps_mean"], 120.0 / 11.0)
        self.assertEqual(summary["guest_fps_median"], 13.75)
        self.assertEqual(summary["guest_fps_1pct_low"], 7.5)
        self.assertEqual([interval["vblank_delta"] for interval in summary["intervals"]], [3, 8])

    def test_rejects_fewer_than_two_presentation_events(self) -> None:
        with self.assertRaisesRegex(ValueError, "at least two"):
            capture.summarize_cadence([{"vblank_generation": 1, "presentation_generation": 1}])

    def test_fake_ymir_observation_skips_inflight_then_records_one_coherent_queue(self) -> None:
        class FakeYmir:
            def __init__(self) -> None:
                self.tick = 0
                self.inflight = runtime(qn=1, qr=0, notify=5, retired=4)
                self.coherent = runtime(qn=2, qr=2, notify=6, retired=6)

            def call(self, method: str, params: dict[str, int]) -> dict[str, list[int]]:
                if method == "exec.run_for":
                    if params != {"frames": 1}:
                        raise AssertionError(params)
                    self.tick += 1
                    return {}
                if method != "mem.peek":
                    raise AssertionError(method)
                address = params["address"] & ~capture.P2_ALIAS_BIT
                if address == BOOT_ADDRESS:
                    return {"data": list(trace(self.tick, self.tick // 2))}
                if address == RUNTIME_ADDRESS:
                    return {"data": list(self.inflight if self.tick == 1 else self.coherent)}
                if address == QUEUE_ADDRESS:
                    return {"data": list(queue(0))}
                raise AssertionError(address)

        observation = capture.observe_target(
            FakeYmir(),
            {
                "sourceboot_boot_trace": {"address": BOOT_ADDRESS, "size": 32},
                "s_runtime": {"address": RUNTIME_ADDRESS, "size": 92},
                "s_render_job_queue": {"address": QUEUE_ADDRESS, "size": 232},
            },
            max_vblanks=4,
            nominal_refresh_hz=60.0,
        )
        self.assertEqual(len(observation["presentation_events"]), 2)
        self.assertEqual(observation["latest_coherent_queue"]["sequence"], 6)
        self.assertEqual(observation["measurement"]["guest_fps_mean"], 30.0)

    def test_cli_writes_a_failed_report_instead_of_success_shaped_partial_output(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "failed.json"
            exit_code = capture.main([
                "--ymir", str(Path(directory) / "missing-ymir"),
                "--ipl", str(Path(directory) / "missing-ipl"),
                "--game", str(Path(directory) / "missing.cue"),
                "--elf", str(Path(directory) / "missing.elf"),
                "--output", str(output),
            ])
            report = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(exit_code, 1)
        self.assertEqual(report["schema"], "sm64-saturn-sourceboot-throughput-v1")
        self.assertEqual(report["status"], "failed")
        self.assertEqual(report["failure"]["stage"], "arguments")
        self.assertNotIn("observation", report)


if __name__ == "__main__":
    unittest.main()
