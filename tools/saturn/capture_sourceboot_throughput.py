#!/usr/bin/env python3
"""Capture bounded sourceboot queue ownership and target presentation cadence.

The reader only accepts samples from an explicitly matched sourceboot CUE and
ELF.  It uses Ymir's existing newline JSON-RPC boundary, advances exactly one
guest VBlank per observation, and fails closed on an in-flight queue record.
It does not build a target or alter its scheduling policy.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import statistics
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

from capture_hwtest import artifact_identity, cap_stderr
from capture_route_views import YmirClient
from capture_sourceboot_boot_trace import bind_capture_artifacts, run_bios_handoff
import gen_build_identity as build_identity
from release_manifest import ReleaseManifestVerification, verify_release_manifest


SCHEMA = "sm64-saturn-sourceboot-throughput-v1"
EVIDENCE_KIND = "ymir-sourceboot-queue-throughput"
BOOT_TRACE_BYTES = 32
CADENCE_TRACE_V1_BYTES = 60
CADENCE_TRACE_BYTES = 76
CADENCE_TRACE_MAGIC = 0x53394354
CADENCE_TRACE_VERSION = 2
CADENCE_V1_RECORD_FIELDS = (
    "observed_vblank_generation",
    "frame_generation",
    "build_generation",
    "presentation_generation",
    "dropped_vblank_credit",
    "simulation_vblank_crossings",
    "simulation_count",
    "construction_vblank_crossings",
    "construction_count",
    "transport_presentation_vblank_crossings",
    "transport_presentation_count",
)
CADENCE_RECORD_FIELDS = CADENCE_V1_RECORD_FIELDS + (
    "slave_work_vblank_crossings",
    "slave_work_count",
    "master_finalize_vblank_crossings",
    "master_finalize_count",
)
RUNTIME_LAYOUTS = {
    # Four owner pointers, active/notify/retired, then telemetry.
    92: {"telemetry": 28},
    # Fix Round 2 adds marker observer/clock/context before active.
    104: {"telemetry": 40},
}
RENDER_JOB_QUEUE_BYTES = 232
IDENTITY_PROBE_BYTES = 16
MAX_VBLANKS = 4096
P2_ALIAS_BIT = 0x20000000
MAX_DIAGNOSTIC_NOTIFICATIONS = 64
MAX_DIAGNOSTIC_NOTIFICATION_BYTES = 64 * 1024
IDENTITY_MISMATCH_MESSAGE = "running target does not contain immutable bytes from the matching ELF"
REQUIRED_SYMBOLS = {
    "sourceboot_boot_trace": BOOT_TRACE_BYTES,
    "sourceboot_cadence_trace": CADENCE_TRACE_BYTES,
    "s_runtime": tuple(RUNTIME_LAYOUTS),
    "s_render_job_queue": RENDER_JOB_QUEUE_BYTES,
}
BUILD_IDENTITY_SYMBOL = "saturn_build_identity"


class ObservationError(ValueError):
    """Fail-closed observation error with a fixed-size final target snapshot."""

    def __init__(self, message: str, diagnostics: dict[str, Any]) -> None:
        super().__init__(message)
        self.diagnostics = diagnostics


def _elf32_sections(data: bytes) -> tuple[str, list[dict[str, int]]]:
    """Return ELF32 section metadata without calling an inherited toolchain."""
    if data[:4] != b"\x7fELF" or len(data) < 52 or data[4] != 1:
        raise ValueError("expected ELF32 file")
    endian = "big" if data[5] == 2 else "little" if data[5] == 1 else None
    if endian is None:
        raise ValueError("ELF has unknown byte order")
    section_offset = int.from_bytes(data[32:36], endian)
    section_size = int.from_bytes(data[46:48], endian)
    section_count = int.from_bytes(data[48:50], endian)
    if section_size < 40 or section_count == 0:
        raise ValueError("ELF section headers are malformed")
    sections: list[dict[str, int]] = []
    for index in range(section_count):
        base = section_offset + index * section_size
        if base + 40 > len(data):
            raise ValueError("ELF section header is outside file")
        section = {
            "type": int.from_bytes(data[base + 4 : base + 8], endian),
            "flags": int.from_bytes(data[base + 8 : base + 12], endian),
            "address": int.from_bytes(data[base + 12 : base + 16], endian),
            "offset": int.from_bytes(data[base + 16 : base + 20], endian),
            "size": int.from_bytes(data[base + 20 : base + 24], endian),
            "link": int.from_bytes(data[base + 24 : base + 28], endian),
            "entry_size": int.from_bytes(data[base + 36 : base + 40], endian),
        }
        if section["offset"] + section["size"] > len(data):
            raise ValueError("ELF section contents are outside file")
        sections.append(section)
    return endian, sections


def _elf32_load_segments(data: bytes, endian: str) -> list[dict[str, int]]:
    """Return valid ELF32 PT_LOAD segments for identity-probe containment."""
    program_offset = int.from_bytes(data[28:32], endian)
    program_size = int.from_bytes(data[42:44], endian)
    program_count = int.from_bytes(data[44:46], endian)
    if program_count == 0 or program_size < 32:
        return []
    segments: list[dict[str, int]] = []
    for index in range(program_count):
        base = program_offset + index * program_size
        if base + 32 > len(data):
            raise ValueError("ELF program header is outside file")
        segment = {
            "type": int.from_bytes(data[base : base + 4], endian),
            "offset": int.from_bytes(data[base + 4 : base + 8], endian),
            "address": int.from_bytes(data[base + 8 : base + 12], endian),
            "file_size": int.from_bytes(data[base + 16 : base + 20], endian),
            "memory_size": int.from_bytes(data[base + 20 : base + 24], endian),
        }
        if segment["offset"] + segment["file_size"] > len(data):
            raise ValueError("ELF load segment is outside file")
        if segment["type"] == 1:
            segments.append(segment)
    return segments


def _resolve_symbols(
    elf: Path, requirements: dict[str, int | tuple[int, ...]]
) -> dict[str, dict[str, int]]:
    return _resolve_symbols_from_bytes(elf.read_bytes(), requirements)


def _resolve_symbols_from_bytes(
    data: bytes, requirements: dict[str, int | tuple[int, ...]]
) -> dict[str, dict[str, int]]:
    endian, sections = _elf32_sections(data)
    found: dict[str, list[dict[str, int]]] = {name: [] for name in requirements}
    for section in sections:
        if section["type"] != 2:  # SHT_SYMTAB: a stripped ELF has no usable table.
            continue
        entry_size = section["entry_size"]
        if entry_size < 16 or section["size"] % entry_size:
            raise ValueError("ELF symbol table is malformed")
        if section["link"] >= len(sections) or sections[section["link"]]["type"] != 3:
            raise ValueError("ELF symbol table string table is malformed")
        strings = sections[section["link"]]
        string_data = data[strings["offset"] : strings["offset"] + strings["size"]]
        for relative in range(0, section["size"], entry_size):
            entry = section["offset"] + relative
            string_offset = int.from_bytes(data[entry : entry + 4], endian)
            if string_offset >= len(string_data):
                continue
            end = string_data.find(b"\0", string_offset)
            if end < 0:
                raise ValueError("ELF symbol name is unterminated")
            name = string_data[string_offset:end].decode("ascii", errors="strict")
            canonical = name[1:] if name.startswith("_") else name
            if canonical not in found:
                continue
            found[canonical].append(
                {
                    "address": int.from_bytes(data[entry + 4 : entry + 8], endian),
                    "size": int.from_bytes(data[entry + 8 : entry + 12], endian),
                }
            )
    resolved: dict[str, dict[str, int]] = {}
    for name, expected_size in requirements.items():
        matches = found[name]
        if not matches:
            raise ValueError(f"ELF is missing required symbol {name}")
        if len(matches) != 1:
            raise ValueError(f"ELF has duplicate required symbol {name}")
        symbol = matches[0]
        expected_sizes = (expected_size,) if isinstance(expected_size, int) \
            else expected_size
        if symbol["size"] not in expected_sizes:
            if len(expected_sizes) == 1:
                expectation = f"expected {expected_sizes[0]}"
            else:
                expectation = "known sizes " + ", ".join(
                    str(size) for size in expected_sizes
                )
            raise ValueError(
                f"ELF symbol {name} has wrong size {symbol['size']}, {expectation}"
            )
        resolved[name] = symbol
    return resolved


def resolve_required_symbols(elf: Path) -> dict[str, dict[str, int]]:
    """Resolve exactly one local-or-underscore telemetry symbol of each size."""
    return _resolve_symbols(elf, REQUIRED_SYMBOLS)


def resolve_build_identity_symbol(
    symbols: dict[str, dict[str, int]],
) -> dict[str, int]:
    """Require the fixed ABI symbol independently of mutable output labels."""
    symbol = symbols.get(BUILD_IDENTITY_SYMBOL)
    if symbol is None:
        raise ValueError(f"ELF is missing required symbol {BUILD_IDENTITY_SYMBOL}")
    if symbol.get("size") not in build_identity.SUPPORTED_IDENTITY_SIZES:
        raise ValueError(
            f"ELF symbol {BUILD_IDENTITY_SYMBOL} has wrong size "
            f"{symbol.get('size')}, known sizes "
            + ", ".join(str(size) for size in build_identity.SUPPORTED_IDENTITY_SIZES)
        )
    return symbol


def _elf_symbol_bytes(elf: Path, symbol: dict[str, int]) -> bytes:
    return _elf_symbol_bytes_from_data(elf.read_bytes(), symbol)


def _elf_symbol_bytes_from_data(data: bytes, symbol: dict[str, int]) -> bytes:
    endian, _sections = _elf32_sections(data)
    address = int(symbol["address"])
    size = int(symbol["size"])
    for segment in _elf32_load_segments(data, endian):
        if (segment["address"] <= address
                and address + size <= segment["address"] + segment["file_size"]):
            offset = segment["offset"] + address - segment["address"]
            return data[offset:offset + size]
    raise ValueError("ELF build identity is not contained in a file-backed PT_LOAD segment")


def build_elf_build_identity_probe(elf: Path) -> dict[str, Any]:
    elf_snapshot = elf.read_bytes()
    symbols = _resolve_symbols_from_bytes(
        elf_snapshot,
        {BUILD_IDENTITY_SYMBOL: build_identity.SUPPORTED_IDENTITY_SIZES},
    )
    symbol = resolve_build_identity_symbol(symbols)
    raw = _elf_symbol_bytes_from_data(elf_snapshot, symbol)
    parsed = build_identity.validate_identity(raw)
    return {
        "address": int(symbol["address"]),
        "size": int(symbol["size"]),
        "expected_bytes": list(raw),
        "sha256": hashlib.sha256(raw).hexdigest(),
        "label": build_identity.identity_label(raw),
        "identity": parsed,
    }


def validate_release_identity_probe(
    verified: ReleaseManifestVerification, probe: dict[str, Any]
) -> None:
    """Require the ELF identity promised by the verified release manifest."""
    document = verified.document
    parsed = probe.get("identity")
    if (
        probe.get("sha256") != document.get("identity_sha256")
        or not isinstance(parsed, dict)
        or parsed.get("version") != document.get("identity_version")
        or parsed.get("effective_config_hash")
        != document.get("effective_config_sha256")
    ):
        raise ValueError("ELF build identity differs from verified release manifest")


def bind_release_manifest(
    manifest: Path, game: Path, elf: Path
) -> ReleaseManifestVerification:
    """Verify the release before any capture process or SH tool can start."""
    verified = verify_release_manifest(manifest)
    if game.resolve() != verified.outputs["cue"]:
        getattr(verified, "close", lambda: None)()
        raise ValueError("game CUE differs from verified release manifest")
    if elf.resolve() != verified.outputs["elf"]:
        getattr(verified, "close", lambda: None)()
        raise ValueError("ELF differs from verified release manifest")
    return verified


def validate_build_identity(
    elf_raw: bytes, loaded_raw: bytes, *, expected_label: str | None = None
) -> dict[str, Any]:
    parsed = build_identity.validate_identity(loaded_raw, expected=elf_raw)
    label = build_identity.identity_label(loaded_raw, expected=elf_raw)
    if expected_label is not None and label != expected_label:
        raise ValueError(
            f"compiled identity label mismatch: expected {expected_label}, got {label}"
        )
    return {
        "match": True,
        "label": label,
        "sha256": hashlib.sha256(loaded_raw).hexdigest(),
        "identity": parsed,
    }


def prove_loaded_build_identity(
    client: Any, probe: dict[str, Any], *, expected_label: str | None = None
) -> dict[str, Any]:
    loaded = read_exact(client, _p2(int(probe["address"])), int(probe["size"]))
    result = validate_build_identity(
        bytes(probe["expected_bytes"]), loaded, expected_label=expected_label
    )
    return {"address": int(probe["address"]), "size": int(probe["size"]), **result}


def build_elf_identity_probe(elf: Path) -> dict[str, Any]:
    """Bind one immutable mapped code window to the exact supplied ELF."""
    data = elf.read_bytes()
    endian, sections = _elf32_sections(data)
    load_segments = _elf32_load_segments(data, endian)
    for section in sections:
        if (
            section["type"] != 1  # SHT_PROGBITS
            or section["flags"] & 0x6 != 0x6  # SHF_ALLOC | SHF_EXECINSTR
            or section["size"] < IDENTITY_PROBE_BYTES
        ):
            continue
        section_end = section["address"] + IDENTITY_PROBE_BYTES
        file_end = section["offset"] + IDENTITY_PROBE_BYTES
        if not any(
            segment["address"] <= section["address"]
            and section_end <= segment["address"] + segment["file_size"]
            and segment["offset"] <= section["offset"]
            and file_end <= segment["offset"] + segment["file_size"]
            and section["address"] - segment["address"]
            == section["offset"] - segment["offset"]
            for segment in load_segments
        ):
            continue
        start = section["offset"]
        expected = data[start : start + IDENTITY_PROBE_BYTES]
        return {
            "address": section["address"],
            "size": IDENTITY_PROBE_BYTES,
            "expected_bytes": list(expected),
            "expected_sha256": hashlib.sha256(expected).hexdigest(),
        }
    raise ValueError("ELF has no loadable executable PROGBITS section for identity probe")


def read_exact(client: Any, address: int, count: int) -> bytes:
    """Read one bounded Ymir memory window and validate the protocol payload."""
    if count <= 0:
        raise ValueError("target read count must be positive")
    result = client.call("mem.peek", {"address": address, "count": count})
    data = result.get("data")
    if not isinstance(data, list) or len(data) != count:
        raise ValueError(f"target read at 0x{address:08x} did not return {count} bytes")
    if any(not isinstance(byte, int) or not 0 <= byte <= 0xFF for byte in data):
        raise ValueError("target read contains a non-byte value")
    return bytes(data)


def prove_target_identity(client: Any, probe: dict[str, Any]) -> dict[str, Any]:
    """Refuse to observe telemetry until target memory matches linked ELF code."""
    observed = read_exact(client, int(probe["address"]), int(probe["size"]))
    expected = bytes(probe["expected_bytes"])
    if observed != expected:
        raise ValueError(IDENTITY_MISMATCH_MESSAGE)
    return {
        "address": int(probe["address"]),
        "size": int(probe["size"]),
        "expected_sha256": probe["expected_sha256"],
        "observed_sha256": hashlib.sha256(observed).hexdigest(),
        "match": True,
    }


def validate_startup_vblanks(startup_vblanks: int) -> int:
    """Keep the sourceboot-load wait separate from observation cadence."""
    if not 1 <= startup_vblanks <= MAX_VBLANKS:
        raise ValueError(f"startup VBlanks must be between 1 and {MAX_VBLANKS}")
    return startup_vblanks


def wait_for_target_identity(
    client: Any, probe: dict[str, Any], *, startup_vblanks: int
) -> dict[str, Any]:
    """Advance one VBlank at a time until exact ELF bytes are actually loaded."""
    startup_vblanks = validate_startup_vblanks(startup_vblanks)
    for attempt in range(1, startup_vblanks + 1):
        client.call("exec.run_for", {"frames": 1})
        try:
            identity = prove_target_identity(client, probe)
        except ValueError as error:
            if str(error) != IDENTITY_MISMATCH_MESSAGE:
                raise
            continue
        return {
            **identity,
            "startup_vblanks_waited": attempt,
            "startup_identity_attempts": attempt,
        }
    raise ValueError(
        f"target identity did not match after {startup_vblanks} one-VBlank startup attempts"
    )


def _be32(raw: bytes, offset: int) -> int:
    return int.from_bytes(raw[offset : offset + 4], "big")


def decode_boot_trace(raw: bytes) -> dict[str, int]:
    if len(raw) != BOOT_TRACE_BYTES:
        raise ValueError("boot trace has wrong size")
    return {
        "observed_vblank_generation": _be32(raw, 16),
        "vdp2_presentation_generation": _be32(raw, 28),
    }


def decode_cadence_trace(raw: bytes) -> dict[str, Any]:
    """Decode one stable big-endian target seqlock snapshot."""
    if len(raw) not in (CADENCE_TRACE_V1_BYTES, CADENCE_TRACE_BYTES):
        raise ValueError("cadence trace has wrong size")
    if _be32(raw, 0) != CADENCE_TRACE_MAGIC:
        raise ValueError("cadence trace has wrong magic")
    version = _be32(raw, 4)
    expected_size = {
        1: CADENCE_TRACE_V1_BYTES,
        CADENCE_TRACE_VERSION: CADENCE_TRACE_BYTES,
    }.get(version)
    if expected_size is None:
        raise ValueError("cadence trace has wrong version")
    if len(raw) != expected_size:
        raise ValueError("cadence trace size does not match version")
    sequence_begin = _be32(raw, 8)
    sequence_end = _be32(raw, len(raw) - 4)
    if sequence_begin != sequence_end or sequence_begin & 1:
        raise ValueError("cadence trace seqlock is not stable")
    fields = CADENCE_RECORD_FIELDS if version == CADENCE_TRACE_VERSION \
        else CADENCE_V1_RECORD_FIELDS
    return {
        "version": version,
        "sequence": sequence_begin,
        "record": {
            field: _be32(raw, 12 + 4 * index)
            for index, field in enumerate(fields)
        },
    }


def decode_runtime(raw: bytes) -> dict[str, Any]:
    layout = RUNTIME_LAYOUTS.get(len(raw))
    if layout is None:
        raise ValueError("runtime telemetry has wrong size")
    telemetry = layout["telemetry"]
    qm = [_be32(raw, telemetry + 16 + 4 * index) for index in range(4)]
    qs = [_be32(raw, telemetry + 32 + 4 * index) for index in range(4)]
    master_failures = _be32(raw, telemetry + 52)
    slave_failures = _be32(raw, telemetry + 56)
    return {
        "qn": _be32(raw, telemetry),
        "qr": _be32(raw, telemetry + 4),
        "notify_sequence": _be32(raw, telemetry + 8),
        "retired_sequence": _be32(raw, telemetry + 12),
        "qm": qm,
        "qs": qs,
        "qw": _be32(raw, telemetry + 48),
        "master_failures": master_failures,
        "slave_failures": slave_failures,
        "qf": master_failures + slave_failures,
        "qq": _be32(raw, telemetry + 60),
    }


def decode_queue_generation(raw: bytes) -> int:
    if len(raw) != RENDER_JOB_QUEUE_BYTES:
        raise ValueError("render queue has wrong size")
    return _be32(raw, 224)


def accept_coherent_queue(runtime: dict[str, Any], queue_generation: int) -> dict[str, Any] | None:
    """Return one terminal queue record, otherwise deliberately discard it."""
    if queue_generation != 0:
        return None
    if runtime["notify_sequence"] == 0 or runtime["notify_sequence"] != runtime["retired_sequence"]:
        return None
    if runtime["qn"] == 0 or runtime["qn"] != runtime["qr"]:
        return None
    return {"sequence": runtime["retired_sequence"], "queue_generation": queue_generation, **runtime}


def attach_queue_record(event: dict[str, Any], record: dict[str, Any], used_sequences: set[int]) -> None:
    """Attach a terminal record to exactly one presentation edge."""
    sequence = int(record["sequence"])
    if sequence in used_sequences:
        raise ValueError(f"queue sequence {sequence} is already attached to a presentation event")
    event["queue"] = record
    used_sequences.add(sequence)


def _unsigned_delta(previous: int, current: int) -> int:
    return (current - previous) & 0xFFFFFFFF


def phase_delta(previous: dict[str, int], current: dict[str, int]) -> dict[str, Any]:
    """Calculate adjacent deltas from cumulative wrap-safe target counters."""
    delta = lambda field: _unsigned_delta(int(previous[field]), int(current[field]))
    vblank_delta = delta("observed_vblank_generation")
    simulation_crossings = delta("simulation_vblank_crossings")
    construction_crossings = delta("construction_vblank_crossings")
    transport_presentation_crossings = delta(
        "transport_presentation_vblank_crossings"
    )
    has_overlap_phases = all(
        field in previous and field in current for field in (
            "slave_work_vblank_crossings", "slave_work_count",
            "master_finalize_vblank_crossings", "master_finalize_count",
        )
    )
    master_finalize_crossings = (
        delta("master_finalize_vblank_crossings") if has_overlap_phases
        else construction_crossings
    )
    slave_work_crossings = (
        delta("slave_work_vblank_crossings") if has_overlap_phases else 0
    )
    if has_overlap_phases and master_finalize_crossings > construction_crossings:
        raise ValueError(
            "master finalization exceeds complete construction"
        )
    attributed = (
        simulation_crossings + construction_crossings +
        transport_presentation_crossings
    )
    # T2.11.  `attributed <= vblank_delta` asserts that the accounted phases are
    # wall-disjoint sub-windows of the interval.  For a single-threaded master
    # sampling one monotone VBlank counter that holds exactly -- but one phase
    # boundary in the v2 schema is not stamped by the master.
    #
    # `master_finalization` is `terminal_vblank - retirement_vblank`, and
    # `retirement_vblank` is written from the *slave* SH-2 at the instant the
    # slave retires the job (`sourceboot_render_runtime_marker`, RETIRED arm, in
    # src/port/saturn/sourceboot/main.c -- its own comment records that the
    # observer runs on the slave).  The master does not begin finalizing then;
    # it is still inside whatever action it was dispatched to run, and the frame
    # pipeline admits the next source tick while the render is in flight
    # (`sm64_saturn_frame_pipeline_step`, the sim-credit branch taken when
    # `render_active && render_service_started`).  Every whole VBlank crossing
    # between the slave's stamp and the master's first poll after it is charged
    # twice: once to `master_finalization`, hence to `construction`, and once to
    # `simulation`.
    #
    # That double charge is a sub-window of both phases, so it is bounded above
    # by their minimum.  The bound is read out of this same trace rather than
    # chosen: it is exactly zero when no finalization window exists -- a v1
    # trace with no overlap fields, or a phase aborted before notification --
    # and it shrinks with either phase.  Widening it further, or applying it
    # where no concurrent window is recorded, would stop the check failing on
    # data that really is inconsistent.
    concurrent_allowance = (
        min(simulation_crossings, master_finalize_crossings)
        if has_overlap_phases else 0
    )
    if attributed - concurrent_allowance > vblank_delta:
        raise ValueError("phase VBlank crossings exceed the observed interval")
    result = {
        "vblank_delta": vblank_delta,
        "frame_delta": delta("frame_generation"),
        "build_delta": delta("build_generation"),
        "presentation_delta": delta("presentation_generation"),
        "dropped_vblank_credit_delta": delta("dropped_vblank_credit"),
        "simulation": {
            "vblank_crossings": simulation_crossings,
            "count": delta("simulation_count"),
        },
        "construction": {
            "vblank_crossings": construction_crossings,
            "count": delta("construction_count"),
        },
        "transport_presentation": {
            "vblank_crossings": transport_presentation_crossings,
            "count": delta("transport_presentation_count"),
        },
        "attributed_vblank_crossings": attributed,
        "unattributed_vblank_crossings": vblank_delta - attributed,
    }
    if has_overlap_phases:
        result["source_tick"] = result["simulation"]
        result["slave_work_overlap_window"] = {
            "vblank_crossings": slave_work_crossings,
            "count": delta("slave_work_count"),
        }
        result["master_finalization"] = {
            "vblank_crossings": master_finalize_crossings,
            "count": delta("master_finalize_count"),
        }
        result["concurrent_phase_allowance_vblank_crossings"] = (
            concurrent_allowance
        )
    return result


def summarize_cadence(events: list[dict[str, Any]], *, nominal_refresh_hz: float = 60.0) -> dict[str, Any]:
    """Derive cadence from adjacent target presentation edges, including wrap.

    New captures carry the real ISR field clock in the coherent cadence trace.
    The boot trace's top-level generation remains an edge identifier and may
    advance once per source presentation. Legacy reports without cadence data
    retain their historical top-level-clock fallback.
    """
    if len(events) < 2:
        raise ValueError("at least two presentation events are required")
    if nominal_refresh_hz <= 0:
        raise ValueError("nominal refresh rate must be positive")
    intervals: list[dict[str, Any]] = []
    for previous, current in zip(events, events[1:]):
        previous_has_cadence = "cadence" in previous
        current_has_cadence = "cadence" in current
        if previous_has_cadence != current_has_cadence:
            raise ValueError("adjacent presentation events mix cadence clock sources")
        if previous_has_cadence:
            previous_cadence = previous["cadence"]
            current_cadence = current["cadence"]
            for event, cadence in (
                (previous, previous_cadence),
                (current, current_cadence),
            ):
                if int(cadence["presentation_generation"]) != int(
                    event["presentation_generation"]
                ):
                    raise ValueError(
                        "cadence trace does not match presentation event"
                    )
            previous_clock = int(
                previous_cadence["observed_vblank_generation"]
            )
            current_clock = int(current_cadence["observed_vblank_generation"])
        else:
            previous_clock = int(previous["vblank_generation"])
            current_clock = int(current["vblank_generation"])
        delta = _unsigned_delta(
            previous_clock, current_clock
        )
        if delta == 0:
            raise ValueError("adjacent presentation events have no VBlank progress")
        presentation_delta = _unsigned_delta(
            int(previous["presentation_generation"]),
            int(current["presentation_generation"]),
        )
        if presentation_delta == 0:
            raise ValueError("adjacent presentation events have no generation progress")
        interval: dict[str, Any] = {
            "vblank_delta": delta,
            "presentation_generation_delta": presentation_delta,
            "guest_fps": nominal_refresh_hz / delta,
        }
        if "cadence" in previous and "cadence" in current:
            interval["phases"] = phase_delta(previous["cadence"], current["cadence"])
        intervals.append(interval)
    total_vblanks = sum(int(interval["vblank_delta"]) for interval in intervals)
    rates = sorted(float(interval["guest_fps"]) for interval in intervals)
    return {
        "nominal_refresh_hz": nominal_refresh_hz,
        "presentation_event_count": len(events),
        "interval_count": len(intervals),
        "target_vblank_delta": total_vblanks,
        "guest_fps_mean": len(intervals) * nominal_refresh_hz / total_vblanks,
        "guest_fps_median": statistics.median(rates),
        "guest_fps_1pct_low": rates[max(0, math.ceil(len(rates) * 0.01) - 1)],
        "intervals": intervals,
    }


def _p2(address: int) -> int:
    return address | P2_ALIAS_BIT


def observe_target(
    client: Any,
    symbols: dict[str, dict[str, int]],
    *,
    max_vblanks: int,
    nominal_refresh_hz: float,
    presentation_events: int = 2,
) -> dict[str, Any]:
    """Advance one VBlank at a time and preserve only coherent queue records."""
    if not 1 <= max_vblanks <= MAX_VBLANKS:
        raise ValueError(f"max VBlanks must be between 1 and {MAX_VBLANKS}")
    if not 2 <= presentation_events <= MAX_VBLANKS:
        raise ValueError(f"presentation events must be between 2 and {MAX_VBLANKS}")
    events: list[dict[str, Any]] = []
    used_sequences: set[int] = set()
    last_presentation: int | None = None
    latest_queue: dict[str, Any] | None = None
    last_trace: dict[str, int] | None = None
    last_runtime: dict[str, Any] | None = None
    last_queue_generation: int | None = None
    last_cadence_trace: dict[str, Any] | None = None
    cadence_symbol = symbols.get("sourceboot_cadence_trace")
    for sample_index in range(max_vblanks):
        client.call("exec.run_for", {"frames": 1})
        trace = decode_boot_trace(read_exact(client, _p2(symbols["sourceboot_boot_trace"]["address"]), BOOT_TRACE_BYTES))
        runtime_size = symbols["s_runtime"]["size"]
        runtime = decode_runtime(read_exact(
            client, _p2(symbols["s_runtime"]["address"]), runtime_size
        ))
        queue_generation = decode_queue_generation(
            read_exact(client, _p2(symbols["s_render_job_queue"]["address"]), RENDER_JOB_QUEUE_BYTES)
        )
        last_trace = trace
        last_runtime = runtime
        last_queue_generation = queue_generation
        coherent = accept_coherent_queue(runtime, queue_generation)
        if coherent is not None:
            latest_queue = coherent
        presentation = trace["vdp2_presentation_generation"]
        if last_presentation is None:
            last_presentation = presentation
            continue
        if presentation == last_presentation:
            continue
        event: dict[str, Any] = {
            "sample_index": sample_index + 1,
            "vblank_generation": trace["observed_vblank_generation"],
            "presentation_generation": presentation,
        }
        if cadence_symbol is not None:
            try:
                last_cadence_trace = decode_cadence_trace(
                    read_exact(client, _p2(cadence_symbol["address"]), CADENCE_TRACE_BYTES)
                )
            except ValueError as error:
                raise ObservationError(
                    f"cadence trace decode failed: {error}",
                    {
                        "vblanks_advanced": sample_index + 1,
                        "presentation_events_observed": len(events),
                        "presentation_events_required": presentation_events,
                        "last_trace": trace,
                        "last_cadence_trace": last_cadence_trace,
                        "cadence_decode_error": str(error),
                        "last_runtime": runtime,
                        "last_queue_generation": queue_generation,
                    },
                ) from error
            cadence_record = last_cadence_trace["record"]
            if cadence_record["presentation_generation"] != presentation:
                raise ObservationError(
                    "cadence trace does not match presentation edge",
                    {
                        "vblanks_advanced": sample_index + 1,
                        "presentation_events_observed": len(events),
                        "presentation_events_required": presentation_events,
                        "last_trace": trace,
                        "last_cadence_trace": last_cadence_trace,
                        "last_runtime": runtime,
                        "last_queue_generation": queue_generation,
                    },
                )
            event["cadence"] = cadence_record
        if coherent is not None:
            attach_queue_record(event, coherent, used_sequences)
        events.append(event)
        last_presentation = presentation
        if len(events) >= presentation_events and latest_queue is not None:
            break
    if cadence_symbol is not None:
        try:
            last_cadence_trace = decode_cadence_trace(
                read_exact(client, _p2(cadence_symbol["address"]), CADENCE_TRACE_BYTES)
            )
        except ValueError as error:
            raise ObservationError(
                f"cadence trace decode failed: {error}",
                {
                    "vblanks_advanced": sample_index + 1,
                    "presentation_events_observed": len(events),
                    "presentation_events_required": presentation_events,
                    "last_trace": last_trace,
                    "last_cadence_trace": last_cadence_trace,
                    "cadence_decode_error": str(error),
                    "last_runtime": last_runtime,
                    "last_queue_generation": last_queue_generation,
                },
            ) from error
    diagnostics = {
        "vblanks_advanced": sample_index + 1,
        "presentation_events_observed": len(events),
        "presentation_events_required": presentation_events,
        "last_trace": last_trace,
        "last_cadence_trace": last_cadence_trace,
        "last_runtime": last_runtime,
        "last_queue_generation": last_queue_generation,
    }
    if len(events) < presentation_events:
        raise ObservationError(
            f"observed {len(events)} of {presentation_events} required presentation events",
            diagnostics,
        )
    try:
        measurement = summarize_cadence(events, nominal_refresh_hz=nominal_refresh_hz)
    except ValueError as error:
        raise ObservationError(str(error), diagnostics) from error
    if latest_queue is None:
        raise ObservationError("no coherent terminal queue record was observed", diagnostics)
    return {
        "vblanks_advanced": sample_index + 1,
        "measurement": measurement,
        "latest_coherent_queue": latest_queue,
        "presentation_events": events,
    }


def capture_after_bios(
    client: Any,
    symbols: dict[str, dict[str, int]],
    identity_probe: dict[str, Any],
    *,
    startup_vblanks: int,
    max_vblanks: int,
    nominal_refresh_hz: float,
    presentation_events: int = 2,
) -> dict[str, Any]:
    """Prove loaded identity before reading one telemetry byte."""
    identity = wait_for_target_identity(
        client, identity_probe, startup_vblanks=startup_vblanks
    )
    return {
        "target_identity": identity,
        "observation": observe_target(
            client,
            symbols,
            max_vblanks=max_vblanks,
            nominal_refresh_hz=nominal_refresh_hz,
            presentation_events=presentation_events,
        ),
    }


def _bounded_notifications(notifications: list[dict[str, Any]]) -> dict[str, Any]:
    """Retain only a byte- and count-bounded tail of JSON-RPC notifications."""
    original_bytes = 0
    retained_reversed: list[dict[str, Any]] = []
    retained_bytes = 2  # JSON list delimiters.
    for notification in notifications:
        original_bytes += len(json.dumps(notification, separators=(",", ":")).encode("utf-8"))
    for notification in reversed(notifications):
        encoded = json.dumps(notification, separators=(",", ":")).encode("utf-8")
        separator_bytes = 1 if retained_reversed else 0
        if (
            len(retained_reversed) >= MAX_DIAGNOSTIC_NOTIFICATIONS
            or retained_bytes + separator_bytes + len(encoded) > MAX_DIAGNOSTIC_NOTIFICATION_BYTES
        ):
            continue
        retained_reversed.append(notification)
        retained_bytes += separator_bytes + len(encoded)
    retained = list(reversed(retained_reversed))
    return {
        "notifications": retained,
        "notifications_original_count": len(notifications),
        "notifications_original_bytes": original_bytes,
        "notifications_truncated": len(retained) != len(notifications),
    }


def _protocol_diagnostics(client: YmirClient | None) -> dict[str, Any]:
    stderr = client.stderr if client is not None else ""
    capped, original_bytes = cap_stderr(stderr)
    notifications = client.notifications if client is not None else []
    return {
        "ready": any(message.get("method") == "instance.ready" for message in notifications),
        **_bounded_notifications(notifications),
        "stderr": capped,
        "stderr_truncated": original_bytes > len(capped),
        "stderr_original_bytes": original_bytes,
    }


def _write_report(path: Path, report: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ymir", type=Path, required=True, help="Ymir headless executable")
    parser.add_argument("--ipl", type=Path, required=True, help="Saturn BIOS image")
    parser.add_argument("--game", type=Path, required=True, help="exact matching sourceboot CUE")
    parser.add_argument("--elf", type=Path, required=True, help="exact matching sourceboot ELF")
    parser.add_argument(
        "--release-manifest", type=Path,
        help="exact release manifest required for new evidence",
    )
    parser.add_argument(
        "--expected-label",
        help="optional caller expectation checked against the compiled identity-derived label",
    )
    parser.add_argument("--output", type=Path, required=True, help="structured JSON evidence report")
    parser.add_argument(
        "--startup-vblanks",
        type=int,
        default=600,
        help="separate identity-load wait bound (1..4096)",
    )
    parser.add_argument("--max-vblanks", type=int, default=600, help="observation bound (1..4096)")
    parser.add_argument(
        "--presentation-events",
        type=int,
        default=2,
        help="presentation edges required before cadence is accepted (2..4096)",
    )
    parser.add_argument("--nominal-refresh-hz", type=float, default=60.0)
    parser.add_argument("--timeout", type=float, default=180.0)
    args = parser.parse_args(argv)
    report: dict[str, Any] = {
        "schema": SCHEMA,
        "evidence_kind": EVIDENCE_KIND,
        "created_utc": datetime.now(UTC).isoformat(),
        "status": "failed",
    }
    client: YmirClient | None = None
    stage = "arguments"
    try:
        if args.timeout <= 0:
            raise ValueError("timeout must be positive")
        if not 1 <= args.max_vblanks <= MAX_VBLANKS:
            raise ValueError(f"max VBlanks must be between 1 and {MAX_VBLANKS}")
        if not 2 <= args.presentation_events <= MAX_VBLANKS:
            raise ValueError(f"presentation events must be between 2 and {MAX_VBLANKS}")
        args.startup_vblanks = validate_startup_vblanks(args.startup_vblanks)
        if args.nominal_refresh_hz <= 0:
            raise ValueError("nominal refresh rate must be positive")
        if args.release_manifest is None:
            raise ValueError("--release-manifest is required for new evidence")
        for label, path in (("release manifest", args.release_manifest), ("Ymir", args.ymir), ("IPL", args.ipl), ("game", args.game), ("ELF", args.elf)):
            if not path.is_file():
                raise ValueError(f"{label} is not a file: {path}")
        args.release_manifest, args.ymir, args.ipl, args.game, args.elf, args.output = (
            args.release_manifest.resolve(), args.ymir.resolve(), args.ipl.resolve(), args.game.resolve(), args.elf.resolve(), args.output.resolve()
        )
        stage = "release-manifest"
        verified_release = bind_release_manifest(
            args.release_manifest, args.game, args.elf
        )
        snapshot_outputs = getattr(
            verified_release, "snapshot_outputs", {"cue": args.game, "elf": args.elf}
        )
        capture_game = snapshot_outputs["cue"]
        capture_elf = snapshot_outputs["elf"]
        report["release_manifest_sha256"] = verified_release.manifest_sha256
        stage = "artifact-binding"
        report["artifacts"] = {
            **bind_capture_artifacts(capture_game, capture_elf),
            "ymir": artifact_identity(args.ymir),
        }
        for name in ("cue", "iso", "elf"):
            if name in report["artifacts"] and hasattr(verified_release, "outputs"):
                report["artifacts"][name]["path"] = str(verified_release.outputs[name])
        if report["artifacts"]["ymir"] is None:
            raise ValueError("Ymir identity could not be hashed")
        stage = "symbol-resolution"
        symbols = resolve_required_symbols(capture_elf)
        report["symbols"] = symbols
        identity_probe = build_elf_identity_probe(capture_elf)
        report["identity_probe"] = {key: value for key, value in identity_probe.items() if key != "expected_bytes"}
        build_identity_probe = build_elf_build_identity_probe(capture_elf)
        validate_release_identity_probe(verified_release, build_identity_probe)
        report["elf_build_identity"] = {
            key: value for key, value in build_identity_probe.items()
            if key != "expected_bytes"
        }
        stage = "ymir-start"
        client = YmirClient(args.ymir, args.ipl, capture_game, args.timeout)
        stage = "bios-handoff"
        run_bios_handoff(client, lambda frames: client.call("exec.run_for", {"frames": frames}), lambda _label: None)
        stage = "target-identity"
        report["target_identity"] = wait_for_target_identity(
            client, identity_probe, startup_vblanks=args.startup_vblanks
        )
        stage = "build-identity"
        report["target_build_identity"] = prove_loaded_build_identity(
            client, build_identity_probe, expected_label=args.expected_label
        )
        stage = "observation"
        report["observation"] = observe_target(
            client,
            symbols,
            max_vblanks=args.max_vblanks,
            nominal_refresh_hz=args.nominal_refresh_hz,
            presentation_events=args.presentation_events,
        )
        client.shutdown()
        report["status"] = "complete"
    except BaseException as error:
        if client is not None:
            client.abort()
        report["failure"] = {"stage": stage, "type": type(error).__name__, "message": str(error)}
        if isinstance(error, ObservationError):
            report["observation_diagnostics"] = error.diagnostics
    report["protocol"] = _protocol_diagnostics(client)
    _write_report(args.output, report)
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report["status"] == "complete" else 1


if __name__ == "__main__":
    raise SystemExit(main())
