#!/usr/bin/env python3
"""Host contracts for the bounded sourceboot queue/cadence capture."""

from __future__ import annotations

import hashlib
import sys
import tempfile
import unittest
import json
from pathlib import Path
from types import SimpleNamespace
from unittest import mock


TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

try:
    import capture_sourceboot_throughput as capture
except ModuleNotFoundError as error:
    raise AssertionError("sourceboot throughput capture helper is missing") from error
import gen_build_identity as identity
import test_release_manifest as release_fixtures


BOOT_ADDRESS = 0x06010000
RUNTIME_ADDRESS = 0x06010100
QUEUE_ADDRESS = 0x06010200
CADENCE_ADDRESS = 0x06010300


REPO_ROOT = TOOLS_DIR.parents[1]
EVIDENCE_DIR = REPO_ROOT / "docs" / "saturn" / "evidence" / "reports"
# The two runs T2.10 retained when `summarize_cadence` aborted on build
# `id-b46f60d0a6d129dd`.  Both are deterministic replays of that same build, so
# their retained cumulative cadence records are two samples of one trajectory.
T2_10_ABORT_REPORTS = (
    EVIDENCE_DIR / "sprint2-t2_10-throughput-10events.json",
    EVIDENCE_DIR / "sprint2-t2_10-throughput.json",
)


def _retained_t2_10_abort_records() -> tuple[dict[str, int], dict[str, int]]:
    records: list[dict[str, int]] = []
    for path in T2_10_ABORT_REPORTS:
        report = json.loads(path.read_text(encoding="utf-8"))
        if report["status"] != "failed":
            raise AssertionError(f"{path.name} is no longer a retained abort")
        records.append(
            dict(report["observation_diagnostics"]["last_cadence_trace"]["record"])
        )
    return records[0], records[1]


def _retained_t2_10_abort_events() -> list[dict[str, object]]:
    return [
        {
            "vblank_generation": int(record["presentation_generation"]),
            "presentation_generation": int(record["presentation_generation"]),
            "cadence": record,
        }
        for record in _retained_t2_10_abort_records()
    ]


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


def marker_runtime(
    *, qn: int, qr: int, notify: int, retired: int,
    qm: tuple[int, int, int, int] = (1, 2, 3, 4),
    qs: tuple[int, int, int, int] = (5, 6, 7, 8),
    qw: int = 9, master_failures: int = 10, slave_failures: int = 11,
    qq: int = 12,
) -> bytes:
    values = {
        40: qn, 44: qr, 48: notify, 52: retired, 88: qw,
        92: master_failures, 96: slave_failures, 100: qq,
    }
    for index, value in enumerate(qm):
        values[56 + 4 * index] = value
    for index, value in enumerate(qs):
        values[72 + 4 * index] = value
    return be_words(104, values)


def queue(generation: int) -> bytes:
    return be_words(232, {224: generation})


def cadence_trace(*, sequence: int = 2, record: dict[str, int] | None = None) -> bytes:
    record = record or {}
    raw = bytearray(capture.CADENCE_TRACE_BYTES)
    raw[0:4] = capture.CADENCE_TRACE_MAGIC.to_bytes(4, "big")
    raw[4:8] = capture.CADENCE_TRACE_VERSION.to_bytes(4, "big")
    raw[8:12] = sequence.to_bytes(4, "big")
    for field_index, field in enumerate(capture.CADENCE_RECORD_FIELDS):
        base = 12 + field_index * 4
        raw[base : base + 4] = record.get(field, 0).to_bytes(4, "big")
    raw[-4:] = sequence.to_bytes(4, "big")
    return bytes(raw)


def cadence_trace_v1(*, sequence: int = 2,
                     record: dict[str, int] | None = None) -> bytes:
    record = record or {}
    fields = (
        "observed_vblank_generation", "frame_generation", "build_generation",
        "presentation_generation", "dropped_vblank_credit",
        "simulation_vblank_crossings", "simulation_count",
        "construction_vblank_crossings", "construction_count",
        "transport_presentation_vblank_crossings",
        "transport_presentation_count",
    )
    raw = bytearray(60)
    raw[0:4] = capture.CADENCE_TRACE_MAGIC.to_bytes(4, "big")
    raw[4:8] = (1).to_bytes(4, "big")
    raw[8:12] = sequence.to_bytes(4, "big")
    for field_index, field in enumerate(fields):
        base = 12 + field_index * 4
        raw[base : base + 4] = record.get(field, 0).to_bytes(4, "big")
    raw[-4:] = sequence.to_bytes(4, "big")
    return bytes(raw)


def elf32_with_symbols(
    path: Path,
    symbols: list[tuple[str, int, int]],
    *,
    duplicate: bool = False,
    text_type: int = 1,
    text_flags: int = 0x6,
    loadable: bool = True,
    text_payload: bytes | None = None,
) -> None:
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
    text_payload = bytes(range(32)) if text_payload is None else text_payload
    symtab_offset = (text_offset + len(text_payload) + 15) & ~15
    strtab_offset = symtab_offset + 16 * len(entries)
    image = bytearray(strtab_offset + len(names))
    image[:4] = b"\x7fELF"
    image[4] = 1
    image[5] = 2
    image[28:32] = (0xE0).to_bytes(4, "big")
    image[32:36] = section_offset.to_bytes(4, "big")
    image[42:44] = (32).to_bytes(2, "big")
    image[44:46] = (1 if loadable else 0).to_bytes(2, "big")
    image[46:48] = (40).to_bytes(2, "big")
    image[48:50] = section_count.to_bytes(2, "big")
    # text
    text = memoryview(image)[section_offset + 40 : section_offset + 80]
    text[4:8] = text_type.to_bytes(4, "big")
    text[8:12] = text_flags.to_bytes(4, "big")
    text[12:16] = BOOT_ADDRESS.to_bytes(4, "big")
    text[16:20] = text_offset.to_bytes(4, "big")
    text[20:24] = len(text_payload).to_bytes(4, "big")
    image[text_offset : text_offset + len(text_payload)] = text_payload
    if loadable:
        segment = memoryview(image)[0xE0 : 0xE0 + 32]
        segment[0:4] = (1).to_bytes(4, "big")  # PT_LOAD
        segment[4:8] = text_offset.to_bytes(4, "big")
        segment[8:12] = BOOT_ADDRESS.to_bytes(4, "big")
        segment[16:20] = len(text_payload).to_bytes(4, "big")
        segment[20:24] = len(text_payload).to_bytes(4, "big")
        segment[24:28] = (0x5).to_bytes(4, "big")
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


def build_identity_fixture(root: Path, version: int) -> bytes:
    root.mkdir(parents=True, exist_ok=True)
    artifacts: dict[str, dict[str, str]] = {}
    for index, field in enumerate(identity.ARTIFACT_HASH_FIELDS):
        path = root / f"{field}.bin"
        path.write_bytes(f"capture-artifact-{index}\n".encode("ascii"))
        artifacts[field] = {
            "path": str(path),
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        }
    spec: dict[str, object] = {
        "features": {
            "complete_mario_animation": 1,
            "dynamic_actor_closure": 1,
            "semantic_audio": 0,
        },
        "renderer_pipeline": 4,
        "level_id": 9,
        "area_id": 1,
        "route_id": 7,
        "route_replay_mode": 1,
        "live_input_mode": 1,
        "camera_route": 0,
        "camera_variant": 3,
        "diagnostic_mode": 0,
        "bootstrap_ticks": 600,
        "cart_mbit": 32,
        "cart_stage_sectors": 8,
        "hot_promotion": 1,
        "near_clip": 1,
        "bsp_order": 1,
        "polygon_tier": 2,
        "fragment_mode": 0,
        "atan2_variant": 2,
        "demo_path": 1,
        "demo_view_radius": 6000,
        "slave_render": 1,
        "camera_idle_start_tick": 0,
        "camera_idle_discovery": 0,
        "camera_range_capture": 0,
        "bsp_fragment_flat": 0,
        "fast3d_q16_trace": 0,
        "experimental_skip_geo_walk": 0,
        "object_pool_capacity": 240,
        "artifacts": artifacts,
    }
    if version == 2:
        spec["identity_version"] = 2
        for field in ("target_profile", "package_set", "toolchain_attestation"):
            path = root / f"{field}.json"
            path.write_bytes(f"{field}-capture-descriptor\n".encode("ascii"))
            spec[field] = {
                "path": str(path),
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            }
    return identity.build_identity(spec).raw


class ThroughputCaptureTests(unittest.TestCase):
    def test_build_identity_probe_uses_one_snapshot_if_elf_path_changes(self) -> None:
        class MutatingElfPath:
            def __init__(self, path: Path, replacement: bytes) -> None:
                self.path = path
                self.replacement = replacement
                self.read_count = 0

            def read_bytes(self) -> bytes:
                self.read_count += 1
                snapshot = self.path.read_bytes()
                if self.read_count == 1:
                    self.path.write_bytes(self.replacement)
                return snapshot

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            original_raw = build_identity_fixture(root / "original", 2)
            replacement_bytes = bytearray(original_raw)
            replacement_bytes[-1] ^= 1
            replacement_raw = bytes(replacement_bytes)
            identity.validate_identity(replacement_raw)
            elf = root / "mutable.elf"
            replacement_elf = root / "replacement.elf"
            symbols = [("saturn_build_identity", BOOT_ADDRESS, 500)]
            elf32_with_symbols(elf, symbols, text_payload=original_raw)
            elf32_with_symbols(
                replacement_elf,
                symbols,
                text_payload=replacement_raw,
            )
            changing_path = MutatingElfPath(elf, replacement_elf.read_bytes())
            probe = capture.build_elf_build_identity_probe(changing_path)

        self.assertEqual(changing_path.read_count, 1)
        self.assertEqual(probe["expected_bytes"], list(original_raw))
        self.assertEqual(probe["identity"]["version"], 2)

    def test_build_identity_probe_and_target_read_use_declared_v1_or_v2_size(self) -> None:
        class ExactIdentityClient:
            def __init__(self, raw: bytes) -> None:
                self.raw = raw
                self.requests: list[dict[str, int]] = []

            def call(self, method: str, parameters: dict[str, int]):
                self.requests.append({"method": method, **parameters})
                return {"data": list(self.raw)}

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for version, expected_size in ((1, 404), (2, 500)):
                with self.subTest(version=version):
                    raw = build_identity_fixture(root / f"v{version}", version)
                    elf = root / f"v{version}.elf"
                    elf32_with_symbols(
                        elf,
                        [("saturn_build_identity", BOOT_ADDRESS, expected_size)],
                        text_payload=raw,
                    )
                    probe = capture.build_elf_build_identity_probe(elf)
                    self.assertEqual(probe["size"], expected_size)
                    self.assertEqual(probe["expected_bytes"], list(raw))
                    self.assertEqual(probe["identity"]["version"], version)
                    client = ExactIdentityClient(raw)
                    result = capture.prove_loaded_build_identity(client, probe)
                    self.assertEqual(result["size"], expected_size)
                    self.assertEqual(result["identity"]["version"], version)
                    self.assertEqual(client.requests, [{
                        "method": "mem.peek",
                        "address": BOOT_ADDRESS | capture.P2_ALIAS_BIT,
                        "count": expected_size,
                    }])

    def test_build_identity_probe_rejects_every_unsupported_symbol_size(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for unsupported_size in (403, 499, 501):
                with self.subTest(size=unsupported_size):
                    elf = root / f"identity-{unsupported_size}.elf"
                    elf32_with_symbols(
                        elf,
                        [("saturn_build_identity", BOOT_ADDRESS, unsupported_size)],
                        text_payload=bytes(unsupported_size),
                    )
                    with self.assertRaisesRegex(ValueError, "wrong size"):
                        capture.build_elf_build_identity_probe(elf)

    def test_runtime_layouts_match_exact_reviewed_source_evolution(self) -> None:
        self.assertEqual(
            capture.RUNTIME_LAYOUTS,
            {
                92: {"telemetry": 28},
                104: {"telemetry": 40},
            },
        )
        source = (
            TOOLS_DIR.parent.parent
            / "src/port/saturn/gfx/saturn_render_job_runtime.c"
        ).read_text(encoding="utf-8")
        start = source.index("typedef struct sm64_saturn_render_job_runtime {")
        body = source[start:source.index("} sm64_saturn_render_job_runtime_t;", start)]
        self.assertLess(body.index("void *context;"), body.index("marker_observer;"))
        self.assertLess(body.index("marker_observer;"), body.index("marker_clock;"))
        self.assertLess(body.index("marker_clock;"), body.index("marker_context;"))
        self.assertLess(body.index("marker_context;"), body.index("uint32_t active;"))
        self.assertLess(body.index("retired_sequence;"), body.index("telemetry;"))

    def test_decodes_marker_enabled_runtime_at_shifted_offsets(self) -> None:
        decoded = capture.decode_runtime(marker_runtime(
            qn=13, qr=14, notify=15, retired=16,
        ))
        self.assertEqual(decoded["qn"], 13)
        self.assertEqual(decoded["qr"], 14)
        self.assertEqual(decoded["notify_sequence"], 15)
        self.assertEqual(decoded["retired_sequence"], 16)
        self.assertEqual(decoded["qm"], [1, 2, 3, 4])
        self.assertEqual(decoded["qs"], [5, 6, 7, 8])
        self.assertEqual(decoded["qw"], 9)
        self.assertEqual(decoded["qf"], 21)
        self.assertEqual(decoded["qq"], 12)

    def test_symbol_resolution_accepts_only_known_runtime_layout_sizes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            elf = Path(directory) / "game.elf"
            symbols = [
                ("sourceboot_boot_trace", BOOT_ADDRESS, 32),
                ("sourceboot_cadence_trace", CADENCE_ADDRESS,
                 capture.CADENCE_TRACE_BYTES),
                ("s_runtime", RUNTIME_ADDRESS, 104),
                ("s_render_job_queue", QUEUE_ADDRESS, 232),
            ]
            elf32_with_symbols(elf, symbols)
            self.assertEqual(
                capture.resolve_required_symbols(elf)["s_runtime"]["size"],
                104,
            )
            for unknown_size in (88, 96, 100, 108):
                symbols[2] = ("s_runtime", RUNTIME_ADDRESS, unknown_size)
                elf32_with_symbols(elf, symbols)
                with self.assertRaisesRegex(ValueError, "known sizes"):
                    capture.resolve_required_symbols(elf)

    def test_decodes_v2_overlap_window_and_retains_explicit_v1_support(self) -> None:
        self.assertEqual(capture.CADENCE_TRACE_BYTES, 76)
        self.assertEqual(capture.CADENCE_TRACE_VERSION, 2)
        record = {
            field: index + 1 for index, field in
            enumerate(capture.CADENCE_RECORD_FIELDS)
        }
        decoded_v2 = capture.decode_cadence_trace(
            cadence_trace(sequence=12, record=record)
        )
        self.assertEqual(decoded_v2["version"], 2)
        self.assertEqual(decoded_v2["record"], record)
        decoded_v1 = capture.decode_cadence_trace(
            cadence_trace_v1(sequence=14, record=record)
        )
        self.assertEqual(decoded_v1["version"], 1)
        self.assertNotIn("slave_work_vblank_crossings", decoded_v1["record"])
        with self.assertRaisesRegex(ValueError, "size|version"):
            capture.decode_cadence_trace(cadence_trace_v1()[:-4])
        wrong_version = bytearray(cadence_trace())
        wrong_version[4:8] = (3).to_bytes(4, "big")
        with self.assertRaisesRegex(ValueError, "version"):
            capture.decode_cadence_trace(bytes(wrong_version))

    def test_v2_phase_delta_excludes_overlapping_slave_window(self) -> None:
        previous = {field: 0 for field in capture.CADENCE_RECORD_FIELDS}
        current = {field: 0 for field in capture.CADENCE_RECORD_FIELDS}
        current.update({
            "observed_vblank_generation": 5,
            "simulation_vblank_crossings": 2,
            "simulation_count": 1,
            "construction_vblank_crossings": 2,
            "construction_count": 1,
            "transport_presentation_vblank_crossings": 1,
            "transport_presentation_count": 1,
            "slave_work_vblank_crossings": 4,
            "slave_work_count": 1,
            "master_finalize_vblank_crossings": 1,
            "master_finalize_count": 1,
        })
        delta = capture.phase_delta(previous, current)
        self.assertEqual(
            delta["slave_work_overlap_window"],
            {"vblank_crossings": 4, "count": 1},
        )
        self.assertEqual(
            delta["master_finalization"],
            {"vblank_crossings": 1, "count": 1},
        )
        self.assertEqual(delta["construction"], {
            "vblank_crossings": 2, "count": 1,
        })
        self.assertEqual(delta["attributed_vblank_crossings"], 5)
        self.assertEqual(delta["unattributed_vblank_crossings"], 0)

        impossible = dict(current)
        impossible["master_finalize_vblank_crossings"] = 3
        with self.assertRaisesRegex(ValueError, "finalization.*construction"):
            capture.phase_delta(previous, impossible)

    def test_t2_10_retained_abort_data_summarises_and_reports_negative_margin(
        self,
    ) -> None:
        """T2.11 regression: the two runs T2.10 retained must summarise.

        Both aborted runs are deterministic replays of the same build
        (`id-b46f60d0a6d129dd`, ELF `68fbdeb0...88a6`), so the cumulative
        cadence records they retained at presentation events 10 and 30 are two
        samples of one trajectory and their difference is a real 20-frame
        interval.  It is the interval that tripped the old invariant.
        """
        previous, current = _retained_t2_10_abort_records()

        delta = capture.phase_delta(previous, current)

        self.assertEqual(delta["vblank_delta"], 282)
        self.assertEqual(delta["simulation"]["vblank_crossings"], 122)
        self.assertEqual(delta["construction"]["vblank_crossings"], 162)
        self.assertEqual(
            delta["transport_presentation"]["vblank_crossings"], 0
        )
        self.assertEqual(delta["master_finalization"]["vblank_crossings"], 102)
        # The raw sum still exceeds the interval and is still reported as such:
        # the fix does not hide the overshoot, it stops treating it as fatal.
        self.assertEqual(delta["attributed_vblank_crossings"], 284)
        self.assertEqual(delta["unattributed_vblank_crossings"], -2)
        self.assertEqual(
            delta["concurrent_phase_allowance_vblank_crossings"], 102
        )

        events = _retained_t2_10_abort_events()
        summary = capture.summarize_cadence(events, nominal_refresh_hz=60.0)
        self.assertEqual(summary["interval_count"], 1)
        self.assertEqual(summary["target_vblank_delta"], 282)

    def test_transport_overshoot_has_no_concurrent_window_and_still_raises(
        self,
    ) -> None:
        """Genuinely inconsistent data must still abort.

        `transport_presentation` runs on the master with both boundaries
        stamped by the master, so no concurrency can excuse it.  Inflating it
        past the interval on otherwise real T2.10 data must still fail.
        """
        previous, current = _retained_t2_10_abort_records()
        inconsistent = dict(current)
        inconsistent["transport_presentation_vblank_crossings"] = 400

        with self.assertRaisesRegex(ValueError, "exceed the observed interval"):
            capture.phase_delta(previous, inconsistent)

    def test_overshoot_larger_than_the_finalization_window_still_raises(
        self,
    ) -> None:
        """The allowance is bounded by the finalization window, not by need."""
        previous, current = _retained_t2_10_abort_records()
        inconsistent = dict(current)
        # +103 construction against a 102-crossing finalization window: one
        # crossing more than the concurrent window can possibly account for.
        inconsistent["construction_vblank_crossings"] = (
            int(current["construction_vblank_crossings"]) + 103
        )

        with self.assertRaisesRegex(ValueError, "exceed the observed interval"):
            capture.phase_delta(previous, inconsistent)

        # The allowance is capped by the finalization window, so it does not
        # grow to meet an arbitrarily large simulation overshoot.
        runaway_simulation = dict(current)
        runaway_simulation["simulation_vblank_crossings"] = (
            int(current["simulation_vblank_crossings"]) + 1000
        )
        with self.assertRaisesRegex(ValueError, "exceed the observed interval"):
            capture.phase_delta(previous, runaway_simulation)

    def test_v1_trace_without_overlap_fields_gets_no_allowance(self) -> None:
        """No recorded concurrent window means no allowance at all."""
        previous = {field: 0 for field in capture.CADENCE_V1_RECORD_FIELDS}
        current = {field: 0 for field in capture.CADENCE_V1_RECORD_FIELDS}
        current.update({
            "observed_vblank_generation": 10,
            "simulation_vblank_crossings": 6,
            "simulation_count": 1,
            "construction_vblank_crossings": 5,
            "construction_count": 1,
        })
        # min(simulation, construction) would have excused this overshoot had
        # the allowance been applied without a recorded overlap window.
        with self.assertRaisesRegex(ValueError, "exceed the observed interval"):
            capture.phase_delta(previous, current)

    def test_failure_queue_record_preserves_nonzero_quarantine(self) -> None:
        decoded = capture.decode_runtime(runtime(
            qn=8, qr=8, notify=4, retired=4,
            master_failures=0, slave_failures=1, qq=1,
        ))
        coherent = capture.accept_coherent_queue(decoded, 0)
        self.assertIsNotNone(coherent)
        assert coherent is not None
        self.assertEqual(coherent["qf"], 1)
        self.assertEqual(coherent["qq"], 1)

    def test_decodes_fixed_seqlock_cadence_trace_and_rejects_bad_abi(self) -> None:
        record = {field: index + 1 for index, field in enumerate(capture.CADENCE_RECORD_FIELDS)}
        decoded = capture.decode_cadence_trace(cadence_trace(sequence=8, record=record))
        self.assertEqual(decoded["sequence"], 8)
        self.assertEqual(decoded["record"], record)
        with self.assertRaisesRegex(ValueError, "magic"):
            capture.decode_cadence_trace(bytes(capture.CADENCE_TRACE_BYTES))
        torn = bytearray(cadence_trace(sequence=8, record=record))
        torn[-4:] = (6).to_bytes(4, "big")
        with self.assertRaisesRegex(ValueError, "seqlock"):
            capture.decode_cadence_trace(bytes(torn))

    def test_phase_summary_uses_adjacent_wrap_safe_cumulative_deltas(self) -> None:
        previous = {field: 0 for field in capture.CADENCE_V1_RECORD_FIELDS}
        current = {field: 0 for field in capture.CADENCE_V1_RECORD_FIELDS}
        previous.update({
            "observed_vblank_generation": 0xFFFFFFFE,
            "frame_generation": 0xFFFFFFFF,
            "build_generation": 7,
            "presentation_generation": 8,
            "dropped_vblank_credit": 0xFFFFFFFF,
            "simulation_vblank_crossings": 0xFFFFFFFE,
            "simulation_count": 0xFFFFFFFF,
            "construction_vblank_crossings": 9,
            "construction_count": 10,
            "transport_presentation_vblank_crossings": 11,
            "transport_presentation_count": 12,
        })
        current.update({
            "observed_vblank_generation": 3,
            "frame_generation": 1,
            "build_generation": 9,
            "presentation_generation": 9,
            "dropped_vblank_credit": 2,
            "simulation_vblank_crossings": 1,
            "simulation_count": 1,
            "construction_vblank_crossings": 10,
            "construction_count": 12,
            "transport_presentation_vblank_crossings": 12,
            "transport_presentation_count": 15,
        })
        delta = capture.phase_delta(previous, current)
        self.assertEqual(delta["vblank_delta"], 5)
        self.assertEqual(delta["frame_delta"], 2)
        self.assertEqual(delta["dropped_vblank_credit_delta"], 3)
        self.assertEqual(delta["simulation"], {"vblank_crossings": 3, "count": 2})
        self.assertEqual(delta["construction"], {"vblank_crossings": 1, "count": 2})
        self.assertEqual(delta["transport_presentation"], {"vblank_crossings": 1, "count": 3})
        self.assertEqual(delta["attributed_vblank_crossings"], 5)
        self.assertEqual(delta["unattributed_vblank_crossings"], 0)
        impossible = dict(current)
        impossible["construction_vblank_crossings"] = 11
        with self.assertRaisesRegex(ValueError, "exceed"):
            capture.phase_delta(previous, impossible)

    def test_observation_samples_cadence_on_each_presentation_and_retains_final_snapshot(self) -> None:
        class FakeYmir:
            def __init__(self) -> None:
                self.tick = 0

            def call(self, method: str, params: dict[str, int]) -> dict[str, list[int]]:
                if method == "exec.run_for":
                    self.tick += 1
                    return {}
                address = params["address"] & ~capture.P2_ALIAS_BIT
                if address == BOOT_ADDRESS:
                    return {"data": list(trace(self.tick, self.tick))}
                if address == CADENCE_ADDRESS:
                    record = {field: 0 for field in capture.CADENCE_RECORD_FIELDS}
                    record["observed_vblank_generation"] = self.tick
                    record["frame_generation"] = self.tick
                    record["build_generation"] = self.tick
                    record["presentation_generation"] = self.tick
                    record["dropped_vblank_credit"] = self.tick
                    record["simulation_vblank_crossings"] = self.tick
                    record["simulation_count"] = self.tick
                    return {"data": list(cadence_trace(sequence=self.tick * 2, record=record))}
                if address == RUNTIME_ADDRESS:
                    return {"data": list(runtime(qn=self.tick, qr=self.tick, notify=self.tick, retired=self.tick))}
                if address == QUEUE_ADDRESS:
                    return {"data": list(queue(0))}
                raise AssertionError(address)

        observation = capture.observe_target(
            FakeYmir(),
            {
                "sourceboot_boot_trace": {"address": BOOT_ADDRESS, "size": 32},
                "sourceboot_cadence_trace": {"address": CADENCE_ADDRESS, "size": capture.CADENCE_TRACE_BYTES},
                "s_runtime": {"address": RUNTIME_ADDRESS, "size": 92},
                "s_render_job_queue": {"address": QUEUE_ADDRESS, "size": 232},
            },
            max_vblanks=3,
            nominal_refresh_hz=60.0,
        )
        events = observation["presentation_events"]
        self.assertEqual([event["cadence"]["presentation_generation"] for event in events], [2, 3])
        self.assertEqual(events[1]["cadence"]["frame_generation"], 3)
        self.assertEqual(events[1]["cadence"]["dropped_vblank_credit"], 3)
        self.assertEqual(observation["measurement"]["intervals"][0]["phases"]["simulation"]["count"], 1)

    def test_failed_observation_retains_final_cadence_seqlock_snapshot(self) -> None:
        class FakeYmir:
            def __init__(self, *, torn_cadence: bool = False) -> None:
                self.tick = 0
                self.torn_cadence = torn_cadence

            def call(self, method: str, params: dict[str, int]) -> dict[str, list[int]]:
                if method == "exec.run_for":
                    self.tick += 1
                    return {}
                address = params["address"] & ~capture.P2_ALIAS_BIT
                if address == BOOT_ADDRESS:
                    return {"data": list(trace(self.tick, 0))}
                if address == CADENCE_ADDRESS:
                    record = {field: self.tick for field in capture.CADENCE_RECORD_FIELDS}
                    record["presentation_generation"] = 0
                    raw = bytearray(cadence_trace(sequence=6, record=record))
                    if self.torn_cadence:
                        raw[-4:] = (4).to_bytes(4, "big")
                    return {"data": list(raw)}
                if address == RUNTIME_ADDRESS:
                    return {"data": list(runtime(qn=0, qr=0, notify=0, retired=0))}
                if address == QUEUE_ADDRESS:
                    return {"data": list(queue(0))}
                raise AssertionError(address)

        with self.assertRaises(capture.ObservationError) as caught:
            capture.observe_target(
                FakeYmir(),
                {
                    "sourceboot_boot_trace": {"address": BOOT_ADDRESS, "size": 32},
                    "sourceboot_cadence_trace": {"address": CADENCE_ADDRESS, "size": capture.CADENCE_TRACE_BYTES},
                    "s_runtime": {"address": RUNTIME_ADDRESS, "size": 92},
                    "s_render_job_queue": {"address": QUEUE_ADDRESS, "size": 232},
                },
                max_vblanks=2,
                nominal_refresh_hz=60.0,
            )
        final_trace = caught.exception.diagnostics["last_cadence_trace"]
        self.assertEqual(final_trace["sequence"], 6)
        self.assertEqual(final_trace["record"]["observed_vblank_generation"], 2)

        with self.assertRaises(capture.ObservationError) as torn:
            capture.observe_target(
                FakeYmir(torn_cadence=True),
                {
                    "sourceboot_boot_trace": {"address": BOOT_ADDRESS, "size": 32},
                    "sourceboot_cadence_trace": {"address": CADENCE_ADDRESS, "size": capture.CADENCE_TRACE_BYTES},
                    "s_runtime": {"address": RUNTIME_ADDRESS, "size": 92},
                    "s_render_job_queue": {"address": QUEUE_ADDRESS, "size": 232},
                },
                max_vblanks=2,
                nominal_refresh_hz=60.0,
            )
        self.assertIn("seqlock", torn.exception.diagnostics["cadence_decode_error"])
        self.assertEqual(torn.exception.diagnostics["last_runtime"]["qn"], 0)

    def test_resolves_exact_sized_symbols_and_rejects_wrong_missing_or_duplicate_symbols(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            elf = Path(directory) / "game.elf"
            elf32_with_symbols(elf, [
                ("_sourceboot_boot_trace", BOOT_ADDRESS, 32),
                ("_sourceboot_cadence_trace", CADENCE_ADDRESS, capture.CADENCE_TRACE_BYTES),
                ("_s_runtime", RUNTIME_ADDRESS, 92),
                ("_s_render_job_queue", QUEUE_ADDRESS, 232),
            ])
            self.assertEqual(
                capture.resolve_required_symbols(elf),
                {
                    "sourceboot_boot_trace": {"address": BOOT_ADDRESS, "size": 32},
                    "sourceboot_cadence_trace": {"address": CADENCE_ADDRESS, "size": capture.CADENCE_TRACE_BYTES},
                    "s_runtime": {"address": RUNTIME_ADDRESS, "size": 92},
                    "s_render_job_queue": {"address": QUEUE_ADDRESS, "size": 232},
                },
            )
            elf32_with_symbols(elf, [
                ("sourceboot_boot_trace", BOOT_ADDRESS, 31),
                ("sourceboot_cadence_trace", CADENCE_ADDRESS, capture.CADENCE_TRACE_BYTES),
                ("s_runtime", RUNTIME_ADDRESS, 92),
                ("s_render_job_queue", QUEUE_ADDRESS, 232),
            ])
            with self.assertRaisesRegex(ValueError, "wrong size"):
                capture.resolve_required_symbols(elf)
            elf32_with_symbols(elf, [
                ("sourceboot_boot_trace", BOOT_ADDRESS, 32),
                ("sourceboot_cadence_trace", CADENCE_ADDRESS, capture.CADENCE_TRACE_BYTES),
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

    def test_startup_identity_waits_one_vblank_per_mismatch_then_returns_bounded_evidence(self) -> None:
        probe = {"address": BOOT_ADDRESS, "size": 4, "expected_bytes": [1, 2, 3, 4], "expected_sha256": "expected"}

        class Client:
            def __init__(self) -> None:
                self.frames: list[int] = []

            def call(self, method: str, params: dict[str, int]) -> dict[str, list[int]]:
                if method == "exec.run_for":
                    self.frames.append(params["frames"])
                    return {}
                return {"data": [0, 0, 0, 0] if len(self.frames) < 3 else [1, 2, 3, 4]}

        client = Client()
        identity = capture.wait_for_target_identity(client, probe, startup_vblanks=5)
        self.assertEqual(client.frames, [1, 1, 1])
        self.assertEqual(identity["startup_vblanks_waited"], 3)
        self.assertEqual(identity["startup_identity_attempts"], 3)
        self.assertTrue(identity["match"])

    def test_startup_identity_timeout_fails_at_exact_bound(self) -> None:
        probe = {"address": BOOT_ADDRESS, "size": 4, "expected_bytes": [1, 2, 3, 4], "expected_sha256": "expected"}

        class Client:
            def __init__(self) -> None:
                self.frames = 0

            def call(self, method: str, _params: dict[str, int]) -> dict[str, list[int]]:
                if method == "exec.run_for":
                    self.frames += 1
                    return {}
                return {"data": [0, 0, 0, 0]}

        client = Client()
        with self.assertRaisesRegex(ValueError, "after 3 one-VBlank startup attempts"):
            capture.wait_for_target_identity(client, probe, startup_vblanks=3)
        self.assertEqual(client.frames, 3)

    def test_startup_wait_precedes_real_telemetry_observation(self) -> None:
        probe = {"address": BOOT_ADDRESS, "size": 4, "expected_bytes": [1, 2, 3, 4], "expected_sha256": "expected"}
        trace_address = BOOT_ADDRESS + 0x300
        symbols = {
            "sourceboot_boot_trace": {"address": trace_address, "size": 32},
            "s_runtime": {"address": RUNTIME_ADDRESS, "size": 92},
            "s_render_job_queue": {"address": QUEUE_ADDRESS, "size": 232},
        }

        class Client:
            def __init__(self) -> None:
                self.tick = 0

            def call(self, method: str, params: dict[str, int]) -> dict[str, list[int]]:
                if method == "exec.run_for":
                    if params != {"frames": 1}:
                        raise AssertionError(params)
                    self.tick += 1
                    return {}
                address = params["address"] & ~capture.P2_ALIAS_BIT
                if address == BOOT_ADDRESS:
                    return {"data": [0, 0, 0, 0] if self.tick < 2 else [1, 2, 3, 4]}
                if self.tick < 2:
                    raise AssertionError("telemetry read before exact identity")
                if address == trace_address:
                    return {"data": list(trace(self.tick, self.tick))}
                if address == RUNTIME_ADDRESS:
                    return {"data": list(runtime(qn=self.tick, qr=self.tick, notify=self.tick, retired=self.tick))}
                if address == QUEUE_ADDRESS:
                    return {"data": list(queue(0))}
                raise AssertionError(address)

        result = capture.capture_after_bios(
            Client(), symbols, probe, startup_vblanks=3, max_vblanks=3, nominal_refresh_hz=60.0
        )
        self.assertEqual(result["target_identity"]["startup_vblanks_waited"], 2)
        self.assertEqual(len(result["observation"]["presentation_events"]), 2)

    def test_validates_startup_vblank_bound(self) -> None:
        self.assertEqual(capture.validate_startup_vblanks(1), 1)
        self.assertEqual(capture.validate_startup_vblanks(600), 600)
        with self.assertRaisesRegex(ValueError, "between 1 and"):
            capture.validate_startup_vblanks(0)
        with self.assertRaisesRegex(ValueError, "between 1 and"):
            capture.validate_startup_vblanks(capture.MAX_VBLANKS + 1)

    def test_identity_probe_rejects_non_alloc_non_progbits_or_unloaded_executable_sections(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            elf = Path(directory) / "game.elf"
            symbols = [
                ("sourceboot_boot_trace", BOOT_ADDRESS, 32),
                ("s_runtime", RUNTIME_ADDRESS, 92),
                ("s_render_job_queue", QUEUE_ADDRESS, 232),
            ]
            for kwargs in (
                {"text_flags": 0x4},
                {"text_type": 8},
                {"loadable": False},
            ):
                elf32_with_symbols(elf, symbols, **kwargs)
                with self.assertRaisesRegex(ValueError, "loadable"):
                    capture.build_elf_identity_probe(elf)

    def test_identity_probe_rejects_offset_mapped_to_a_different_pt_load_address(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            elf = Path(directory) / "game.elf"
            elf32_with_symbols(elf, [
                ("sourceboot_boot_trace", BOOT_ADDRESS, 32),
                ("s_runtime", RUNTIME_ADDRESS, 92),
                ("s_render_job_queue", QUEUE_ADDRESS, 232),
            ])
            image = bytearray(elf.read_bytes())
            # The section's virtual and file ranges each fit the same PT_LOAD,
            # but their offsets within that segment deliberately disagree.
            image[52 + 40 + 12 : 52 + 40 + 16] = (BOOT_ADDRESS + 16).to_bytes(4, "big")
            image[0xE0 + 16 : 0xE0 + 20] = (64).to_bytes(4, "big")
            image[0xE0 + 20 : 0xE0 + 24] = (64).to_bytes(4, "big")
            elf.write_bytes(image)
            with self.assertRaisesRegex(ValueError, "loadable"):
                capture.build_elf_identity_probe(elf)

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

    def test_cadence_fps_uses_isr_fields_not_decoupled_source_generation(self) -> None:
        previous = {field: 0 for field in capture.CADENCE_RECORD_FIELDS}
        current = {field: 0 for field in capture.CADENCE_RECORD_FIELDS}
        previous.update({
            "observed_vblank_generation": 1000,
            "frame_generation": 41,
            "build_generation": 41,
            "presentation_generation": 41,
        })
        current.update({
            "observed_vblank_generation": 1013,
            "frame_generation": 42,
            "build_generation": 42,
            "presentation_generation": 42,
        })
        events = [
            {
                # The boot trace now names the source presentation generation;
                # it remains useful for edge/coherence checks, not elapsed time.
                "vblank_generation": 41,
                "presentation_generation": 41,
                "cadence": previous,
            },
            {
                "vblank_generation": 42,
                "presentation_generation": 42,
                "cadence": current,
            },
        ]

        summary = capture.summarize_cadence(events, nominal_refresh_hz=60.0)

        self.assertEqual(summary["intervals"][0]["vblank_delta"], 13)
        self.assertAlmostEqual(summary["guest_fps_mean"], 60.0 / 13.0)
        self.assertEqual(
            summary["intervals"][0]["presentation_generation_delta"], 1
        )

    def test_cadence_summary_rejects_adjacent_mixed_clock_sources(self) -> None:
        cadence = {field: 0 for field in capture.CADENCE_RECORD_FIELDS}
        cadence.update({
            "observed_vblank_generation": 13,
            "presentation_generation": 2,
        })
        events = [
            {"vblank_generation": 1, "presentation_generation": 1},
            {
                "vblank_generation": 2,
                "presentation_generation": 2,
                "cadence": cadence,
            },
        ]

        with self.assertRaisesRegex(ValueError, "mix cadence clock sources"):
            capture.summarize_cadence(events)

    def test_cadence_summary_rejects_presentation_generation_mismatch(self) -> None:
        previous = {field: 0 for field in capture.CADENCE_RECORD_FIELDS}
        current = {field: 0 for field in capture.CADENCE_RECORD_FIELDS}
        previous.update({
            "observed_vblank_generation": 20,
            "presentation_generation": 7,
        })
        current.update({
            "observed_vblank_generation": 33,
            "presentation_generation": 99,
        })
        events = [
            {
                "vblank_generation": 7,
                "presentation_generation": 7,
                "cadence": previous,
            },
            {
                "vblank_generation": 8,
                "presentation_generation": 8,
                "cadence": current,
            },
        ]

        with self.assertRaisesRegex(
            ValueError, "cadence trace does not match presentation event"
        ):
            capture.summarize_cadence(events)

    def test_rejects_fewer_than_two_presentation_events(self) -> None:
        with self.assertRaisesRegex(ValueError, "at least two"):
            capture.summarize_cadence([{"vblank_generation": 1, "presentation_generation": 1}])

    def test_fake_ymir_observation_skips_inflight_then_records_one_coherent_queue(self) -> None:
        class FakeYmir:
            def __init__(self) -> None:
                self.tick = 0
                self.inflight = runtime(qn=1, qr=0, notify=5, retired=4)
                self.coherent = runtime(qn=2, qr=2, notify=6, retired=6)
                self.next_coherent = runtime(qn=2, qr=2, notify=7, retired=7)

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
                    if self.tick == 1:
                        return {"data": list(self.inflight)}
                    return {"data": list(self.coherent if self.tick < 4 else self.next_coherent)}
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
        self.assertEqual(observation["latest_coherent_queue"]["sequence"], 7)
        self.assertEqual(observation["measurement"]["guest_fps_mean"], 30.0)

    def test_observation_waits_for_configured_presentation_event_count(self) -> None:
        class FakeYmir:
            def __init__(self) -> None:
                self.tick = 0

            def call(self, method: str, params: dict[str, int]) -> dict[str, list[int]]:
                if method == "exec.run_for":
                    self.tick += 1
                    return {}
                address = params["address"] & ~capture.P2_ALIAS_BIT
                if address == BOOT_ADDRESS:
                    return {"data": list(trace(self.tick, self.tick))}
                if address == RUNTIME_ADDRESS:
                    return {"data": list(runtime(qn=self.tick, qr=self.tick, notify=self.tick, retired=self.tick))}
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
            presentation_events=3,
        )
        self.assertEqual(observation["vblanks_advanced"], 4)
        self.assertEqual(observation["measurement"]["presentation_event_count"], 3)

    def test_repeated_coherent_sequence_fails_end_to_end_instead_of_being_silently_omitted(self) -> None:
        class FakeYmir:
            def __init__(self) -> None:
                self.tick = 0
                self.coherent = runtime(qn=2, qr=2, notify=6, retired=6)

            def call(self, method: str, params: dict[str, int]) -> dict[str, list[int]]:
                if method == "exec.run_for":
                    self.tick += 1
                    return {}
                address = params["address"] & ~capture.P2_ALIAS_BIT
                if address == BOOT_ADDRESS:
                    return {"data": list(trace(self.tick, self.tick // 2))}
                if address == RUNTIME_ADDRESS:
                    return {"data": list(self.coherent)}
                if address == QUEUE_ADDRESS:
                    return {"data": list(queue(0))}
                raise AssertionError(address)

        with self.assertRaisesRegex(ValueError, "already attached"):
            capture.observe_target(
                FakeYmir(),
                {
                    "sourceboot_boot_trace": {"address": BOOT_ADDRESS, "size": 32},
                    "s_runtime": {"address": RUNTIME_ADDRESS, "size": 92},
                    "s_render_job_queue": {"address": QUEUE_ADDRESS, "size": 232},
                },
                max_vblanks=4,
                nominal_refresh_hz=60.0,
            )

    def test_failed_cadence_retains_bounded_last_target_state(self) -> None:
        class ShortObservationYmir:
            def __init__(self) -> None:
                self.tick = 0

            def call(self, method: str, params: dict[str, int]) -> dict[str, list[int]]:
                if method == "exec.run_for":
                    self.tick += 1
                    return {}
                address = params["address"] & ~capture.P2_ALIAS_BIT
                if address == BOOT_ADDRESS:
                    return {"data": list(trace(self.tick, self.tick))}
                if address == RUNTIME_ADDRESS:
                    return {"data": list(runtime(qn=4, qr=3, notify=9, retired=8))}
                if address == QUEUE_ADDRESS:
                    return {"data": list(queue(99))}
                raise AssertionError(address)

        with self.assertRaises(capture.ObservationError) as caught:
            capture.observe_target(
                ShortObservationYmir(),
                {
                    "sourceboot_boot_trace": {"address": BOOT_ADDRESS, "size": 32},
                    "s_runtime": {"address": RUNTIME_ADDRESS, "size": 92},
                    "s_render_job_queue": {"address": QUEUE_ADDRESS, "size": 232},
                },
                max_vblanks=3,
                nominal_refresh_hz=60.0,
                presentation_events=3,
            )
        diagnostics = caught.exception.diagnostics
        self.assertEqual(diagnostics["vblanks_advanced"], 3)
        self.assertEqual(diagnostics["presentation_events_observed"], 2)
        self.assertEqual(diagnostics["presentation_events_required"], 3)
        self.assertEqual(diagnostics["last_trace"], {
            "observed_vblank_generation": 3,
            "vdp2_presentation_generation": 3,
        })
        self.assertEqual(diagnostics["last_runtime"]["qn"], 4)
        self.assertEqual(diagnostics["last_runtime"]["qr"], 3)
        self.assertEqual(diagnostics["last_queue_generation"], 99)

    def test_invalid_cadence_math_retains_last_target_state(self) -> None:
        class InvalidCadenceYmir:
            def __init__(self) -> None:
                self.tick = 0

            def call(self, method: str, params: dict[str, int]) -> dict[str, list[int]]:
                if method == "exec.run_for":
                    self.tick += 1
                    return {}
                address = params["address"] & ~capture.P2_ALIAS_BIT
                if address == BOOT_ADDRESS:
                    return {"data": list(trace(1, self.tick))}
                if address == RUNTIME_ADDRESS:
                    return {"data": list(runtime(qn=self.tick, qr=self.tick, notify=self.tick, retired=self.tick))}
                if address == QUEUE_ADDRESS:
                    return {"data": list(queue(0))}
                raise AssertionError(address)

        with self.assertRaises(capture.ObservationError) as caught:
            capture.observe_target(
                InvalidCadenceYmir(),
                {
                    "sourceboot_boot_trace": {"address": BOOT_ADDRESS, "size": 32},
                    "s_runtime": {"address": RUNTIME_ADDRESS, "size": 92},
                    "s_render_job_queue": {"address": QUEUE_ADDRESS, "size": 232},
                },
                max_vblanks=4,
                nominal_refresh_hz=60.0,
                presentation_events=3,
            )
        self.assertIn("no VBlank progress", str(caught.exception))
        self.assertEqual(caught.exception.diagnostics["last_trace"], {
            "observed_vblank_generation": 1,
            "vdp2_presentation_generation": 4,
        })

    def test_protocol_diagnostics_bounds_many_and_oversized_notifications(self) -> None:
        oversized = {"method": "instance.note", "params": {"message": "x" * (128 * 1024)}}
        client = type("Client", (), {
            "stderr": "",
            "notifications": [oversized] + [{"method": "instance.note", "params": {"index": index}} for index in range(200)],
        })()
        diagnostics = capture._protocol_diagnostics(client)
        encoded = json.dumps(diagnostics["notifications"], separators=(",", ":")).encode("utf-8")
        self.assertTrue(diagnostics["ready"] is False)
        self.assertTrue(diagnostics["notifications_truncated"])
        self.assertEqual(diagnostics["notifications_original_count"], 201)
        self.assertLessEqual(len(diagnostics["notifications"]), capture.MAX_DIAGNOSTIC_NOTIFICATIONS)
        self.assertLessEqual(len(encoded), capture.MAX_DIAGNOSTIC_NOTIFICATION_BYTES + 2)

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

    def test_cli_rejects_presentation_event_count_below_two_in_failed_report(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "failed.json"
            exit_code = capture.main([
                "--ymir", str(Path(directory) / "missing-ymir"),
                "--ipl", str(Path(directory) / "missing-ipl"),
                "--game", str(Path(directory) / "missing.cue"),
                "--elf", str(Path(directory) / "missing.elf"),
                "--output", str(output),
                "--presentation-events", "1",
            ])
            report = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(exit_code, 1)
        self.assertEqual(report["status"], "failed")
        self.assertEqual(report["failure"]["stage"], "arguments")
        self.assertIn("presentation events must be between 2 and", report["failure"]["message"])

    def test_cli_failed_observation_report_retains_last_target_state(self) -> None:
        diagnostics = {
            "vblanks_advanced": 5,
            "presentation_events_observed": 1,
            "presentation_events_required": 10,
            "last_trace": {"observed_vblank_generation": 5, "vdp2_presentation_generation": 8},
            "last_runtime": {"qn": 4, "qr": 3},
            "last_queue_generation": 99,
        }

        class FakeClient:
            stderr = ""
            notifications: list[dict[str, object]] = []

            def abort(self) -> None:
                pass

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = {name: root / name for name in ("ymir.exe", "ipl.bin", "game.cue", "game.elf")}
            for path in paths.values():
                path.write_bytes(b"fixture")
            output = root / "failed.json"
            with (
                mock.patch.object(capture, "bind_capture_artifacts", return_value={"game": {}, "elf": {}}),
                mock.patch.object(capture, "artifact_identity", return_value={"sha256": "ymir"}),
                mock.patch.object(
                    capture, "bind_release_manifest",
                    return_value=SimpleNamespace(manifest_sha256="d" * 64),
                ),
                mock.patch.object(capture, "resolve_required_symbols", return_value={}),
                mock.patch.object(capture, "build_elf_identity_probe", return_value={"expected_bytes": [1]}),
                mock.patch.object(
                    capture,
                    "build_elf_build_identity_probe",
                    return_value={"expected_bytes": [2], "label": "compiled-label"},
                ),
                mock.patch.object(capture, "validate_release_identity_probe"),
                mock.patch.object(capture, "YmirClient", return_value=FakeClient()),
                mock.patch.object(capture, "run_bios_handoff"),
                mock.patch.object(capture, "wait_for_target_identity", return_value={"matched": True}),
                mock.patch.object(
                    capture,
                    "prove_loaded_build_identity",
                    return_value={"match": True, "label": "compiled-label"},
                ),
                mock.patch.object(
                    capture,
                    "observe_target",
                    side_effect=capture.ObservationError("cadence incomplete", diagnostics),
                ),
            ):
                exit_code = capture.main([
                    "--ymir", str(paths["ymir.exe"]),
                    "--ipl", str(paths["ipl.bin"]),
                    "--game", str(paths["game.cue"]),
                    "--elf", str(paths["game.elf"]),
                    "--release-manifest", str(paths["game.cue"]),
                    "--output", str(output),
                    "--presentation-events", "10",
                ])
            report = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(exit_code, 1)
        self.assertEqual(report["status"], "failed")
        self.assertEqual(report["failure"]["stage"], "observation")
        self.assertEqual(report["observation_diagnostics"], diagnostics)
        self.assertEqual(report["release_manifest_sha256"], "d" * 64)

    def test_release_identity_binding_accepts_v1_and_v2_and_rejects_drift(self) -> None:
        validator = getattr(capture, "validate_release_identity_probe", None)
        self.assertTrue(callable(validator), "capture must bind its ELF identity to the release manifest")
        for version in (1, 2):
            with self.subTest(version=version):
                verified = SimpleNamespace(document={
                    "identity_sha256": "a" * 64,
                    "identity_version": version,
                    "effective_config_sha256": "b" * 64,
                })
                probe = {
                    "sha256": "a" * 64,
                    "identity": {"version": version, "effective_config_hash": "b" * 64},
                }
                validator(verified, probe)
                changed = {**probe, "sha256": "c" * 64}
                with self.assertRaisesRegex(ValueError, "identity.*release manifest"):
                    validator(verified, changed)

    def test_release_manifest_paths_are_checked_before_artifact_binding(self) -> None:
        binder = getattr(capture, "bind_release_manifest", None)
        self.assertTrue(callable(binder), "capture must verify a release manifest before capture I/O")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            game = root / "selected.cue"
            elf = root / "selected.elf"
            game.write_bytes(b"cue")
            elf.write_bytes(b"elf")
            verified = SimpleNamespace(
                manifest_sha256="d" * 64,
                document={},
                outputs={"cue": root / "other.cue", "elf": elf.resolve()},
            )
            with mock.patch.object(capture, "verify_release_manifest", return_value=verified):
                with self.assertRaisesRegex(ValueError, "game CUE differs"):
                    binder(root / "release.json", game, elf)

    def test_release_binding_retains_verified_snapshot_after_source_mutation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = release_fixtures.ReleaseFixture(Path(directory))
            manifest = fixture.write()
            expected = fixture.outputs["elf"].read_bytes()
            verified = capture.bind_release_manifest(
                manifest, fixture.outputs["cue"], fixture.outputs["elf"]
            )
            for name, path in fixture.outputs.items():
                path.write_bytes(f"mutated {name}".encode("ascii"))
            self.assertEqual(
                verified.snapshot_outputs["elf"].read_bytes(), expected
            )
            probe = capture.build_elf_build_identity_probe(
                verified.snapshot_outputs["elf"]
            )
            capture.validate_release_identity_probe(verified, probe)


if __name__ == "__main__":
    unittest.main()
