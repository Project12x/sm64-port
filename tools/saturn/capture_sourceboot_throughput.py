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


SCHEMA = "sm64-saturn-sourceboot-throughput-v1"
EVIDENCE_KIND = "ymir-sourceboot-queue-throughput"
BOOT_TRACE_BYTES = 32
RUNTIME_BYTES = 92
RENDER_JOB_QUEUE_BYTES = 232
IDENTITY_PROBE_BYTES = 16
MAX_VBLANKS = 4096
P2_ALIAS_BIT = 0x20000000
MAX_DIAGNOSTIC_NOTIFICATIONS = 64
MAX_DIAGNOSTIC_NOTIFICATION_BYTES = 64 * 1024
IDENTITY_MISMATCH_MESSAGE = "running target does not contain immutable bytes from the matching ELF"
REQUIRED_SYMBOLS = {
    "sourceboot_boot_trace": BOOT_TRACE_BYTES,
    "s_runtime": RUNTIME_BYTES,
    "s_render_job_queue": RENDER_JOB_QUEUE_BYTES,
}


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


def resolve_required_symbols(elf: Path) -> dict[str, dict[str, int]]:
    """Resolve exactly one local-or-underscore target data symbol of each size."""
    data = elf.read_bytes()
    endian, sections = _elf32_sections(data)
    found: dict[str, list[dict[str, int]]] = {name: [] for name in REQUIRED_SYMBOLS}
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
    for name, expected_size in REQUIRED_SYMBOLS.items():
        matches = found[name]
        if not matches:
            raise ValueError(f"ELF is missing required symbol {name}")
        if len(matches) != 1:
            raise ValueError(f"ELF has duplicate required symbol {name}")
        symbol = matches[0]
        if symbol["size"] != expected_size:
            raise ValueError(
                f"ELF symbol {name} has wrong size {symbol['size']}, expected {expected_size}"
            )
        resolved[name] = symbol
    return resolved


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


def decode_runtime(raw: bytes) -> dict[str, Any]:
    if len(raw) != RUNTIME_BYTES:
        raise ValueError("runtime telemetry has wrong size")
    qm = [_be32(raw, 44 + 4 * index) for index in range(4)]
    qs = [_be32(raw, 60 + 4 * index) for index in range(4)]
    master_failures = _be32(raw, 80)
    slave_failures = _be32(raw, 84)
    return {
        "qn": _be32(raw, 28),
        "qr": _be32(raw, 32),
        "notify_sequence": _be32(raw, 36),
        "retired_sequence": _be32(raw, 40),
        "qm": qm,
        "qs": qs,
        "qw": _be32(raw, 76),
        "master_failures": master_failures,
        "slave_failures": slave_failures,
        "qf": master_failures + slave_failures,
        "qq": _be32(raw, 88),
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


def summarize_cadence(events: list[dict[str, Any]], *, nominal_refresh_hz: float = 60.0) -> dict[str, Any]:
    """Derive cadence from adjacent target generation edges, including wrap."""
    if len(events) < 2:
        raise ValueError("at least two presentation events are required")
    if nominal_refresh_hz <= 0:
        raise ValueError("nominal refresh rate must be positive")
    intervals: list[dict[str, Any]] = []
    for previous, current in zip(events, events[1:]):
        delta = _unsigned_delta(
            int(previous["vblank_generation"]), int(current["vblank_generation"])
        )
        if delta == 0:
            raise ValueError("adjacent presentation events have no VBlank progress")
        intervals.append({"vblank_delta": delta, "guest_fps": nominal_refresh_hz / delta})
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
    for sample_index in range(max_vblanks):
        client.call("exec.run_for", {"frames": 1})
        trace = decode_boot_trace(read_exact(client, _p2(symbols["sourceboot_boot_trace"]["address"]), BOOT_TRACE_BYTES))
        runtime = decode_runtime(read_exact(client, _p2(symbols["s_runtime"]["address"]), RUNTIME_BYTES))
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
        if coherent is not None:
            attach_queue_record(event, coherent, used_sequences)
        events.append(event)
        last_presentation = presentation
        if len(events) >= presentation_events and latest_queue is not None:
            break
    diagnostics = {
        "vblanks_advanced": sample_index + 1,
        "presentation_events_observed": len(events),
        "presentation_events_required": presentation_events,
        "last_trace": last_trace,
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
        for label, path in (("Ymir", args.ymir), ("IPL", args.ipl), ("game", args.game), ("ELF", args.elf)):
            if not path.is_file():
                raise ValueError(f"{label} is not a file: {path}")
        args.ymir, args.ipl, args.game, args.elf, args.output = (
            args.ymir.resolve(), args.ipl.resolve(), args.game.resolve(), args.elf.resolve(), args.output.resolve()
        )
        stage = "artifact-binding"
        report["artifacts"] = {**bind_capture_artifacts(args.game, args.elf), "ymir": artifact_identity(args.ymir)}
        if report["artifacts"]["ymir"] is None:
            raise ValueError("Ymir identity could not be hashed")
        stage = "symbol-resolution"
        symbols = resolve_required_symbols(args.elf)
        report["symbols"] = symbols
        identity_probe = build_elf_identity_probe(args.elf)
        report["identity_probe"] = {key: value for key, value in identity_probe.items() if key != "expected_bytes"}
        stage = "ymir-start"
        client = YmirClient(args.ymir, args.ipl, args.game, args.timeout)
        stage = "bios-handoff"
        run_bios_handoff(client, lambda frames: client.call("exec.run_for", {"frames": frames}), lambda _label: None)
        stage = "target-identity"
        report["target_identity"] = wait_for_target_identity(
            client, identity_probe, startup_vblanks=args.startup_vblanks
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
