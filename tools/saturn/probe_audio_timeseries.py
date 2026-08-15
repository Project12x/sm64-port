#!/usr/bin/env python3
"""Sample the sourceboot audio mailbox and SCSP slot registers as a TIME SERIES.

`probe_audio_mailbox.py` takes one snapshot at a fixed depth. This tool boots
the same way, runs to a gameplay depth, confirms the music voice is live, then
repeatedly peeks the whole audio mailbox (status header + control/SFX/completion
rings), the music diagnostic words, and the SCSP slot registers the 68K driver
writes. It emits a JSON time series plus a human-readable table that suppresses
unchanged words, so anything moving on a repeating cadence is visible.

Read-only: no target memory is written, no source file is touched.
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path
from typing import Any

from capture_route_views import YmirClient
from capture_sourceboot_boot_trace import run_bios_handoff
from probe_audio_mailbox import be16, peek

# Ymir's headless debug service hard-caps one exec.run_for at this many frames.
YMIR_MAX_RUN_FOR_FRAMES = 3600
VBLANK_HZ = 60.0

SOUND_RAM_BASE = 0x25A00000
MAILBOX_ADDRESS = SOUND_RAM_BASE + 0x4000
MAILBOX_BYTES = 0x440  # status header + control ring + SFX ring + completion ring
DIAGNOSTIC_ADDRESS = SOUND_RAM_BASE + 0x7F00
DIAGNOSTIC_BYTES = 0x22
SCSP_SLOT_ADDRESS = 0x25B00000
# The driver only programs slots 0..3 (SM64_SATURN_SCSP_VOICE_COUNT), but the
# probe covers the whole 32-slot file so a voice nobody in this codebase keyed
# -- a BIOS leftover, say -- cannot hide from the series.
SCSP_SLOT_COUNT = 32
SCSP_SLOT_BYTES = 0x20  # SM64_SATURN_SCSP_SLOT_BYTES
SCSP_MASTER_ADDRESS = 0x25B00400
SCSP_MONITOR_ADDRESS = 0x25B00408  # MSLC/CA + SGC/EG readback (pure read)

# Status header word names, byte offset from the mailbox base
# (src/port/saturn/audio/saturn_pcm_protocol.h).
MAILBOX_HEADER_FIELDS = (
    (0x00, "magic"),
    (0x02, "version"),
    (0x04, "status"),
    (0x06, "heartbeat"),
    (0x08, "control_producer"),
    (0x0A, "control_consumer"),
    (0x0C, "sfx_producer"),
    (0x0E, "sfx_consumer"),
    (0x10, "commands_consumed"),
    (0x12, "voices_started"),
    (0x14, "unknown_opcodes"),
    (0x16, "last_opcode"),
    (0x18, "active_slot"),
    (0x1A, "invalid_samples"),
    (0x1C, "protocol_faults"),
    (0x1E, "control_saturated"),
    (0x20, "sfx_saturated"),
    (0x22, "control_consumed"),
    (0x24, "sfx_consumed"),
    (0x26, "completion_producer"),
    (0x28, "completion_consumer"),
    (0x2A, "completion_saturated"),
    (0x2C, "completion_protocol_faults"),
    (0x2E, "active_generation_high"),
    (0x30, "active_generation_low"),
    (0x32, "prepared_generation_high"),
    (0x34, "prepared_generation_low"),
    (0x36, "last_completion_status"),
    (0x38, "last_completion_detail"),
    (0x3A, "sound_service_tick"),
    (0x3C, "active_voice_count"),
    (0x3E, "abi_flags"),
)

MUSIC_DIAGNOSTIC_FIELDS = (
    (0, "starts"),
    (2, "faults"),
    (4, "vm_ticks"),
    (6, "active"),
    (8, "notes"),
    (10, "sequence_offset"),
    (12, "sequence_bytes"),
    (14, "reject_mask"),
    (16, "malformed"),
    (18, "dropped"),
    (20, "consume_fail"),
    (22, "scsp_fail"),
    (24, "last_note"),
    (26, "last_arg1"),
    (28, "last_arg0"),
    (30, "last_failure"),
    # Appended after the music words; see SM64_SATURN_PCM_SFX_REFRESHES_OFFSET.
    (32, "sfx_refreshes"),
)

# src/port/saturn/audio68k/scsp_pcm8.h
SCSP_SLOT_FIELDS = {
    0x00: "keys",
    0x02: "sa_low",
    0x04: "lsa",
    0x06: "lea",
    0x08: "eg",
    0x0A: "release",
    0x0C: "attenuation",
    0x10: "pitch",
    0x16: "pan_send",
}

RINGS = (
    ("ctl", 0x040, 8),
    ("sfx", 0x0C0, 24),
    ("cmp", 0x240, 32),
)

# Fields carried in full for every sample so the series is directly plottable.
KEY_FIELDS = (
    "mb.heartbeat",
    "mb.sound_service_tick",
    "mb.commands_consumed",
    "mb.control_consumed",
    "mb.sfx_consumed",
    "mb.voices_started",
    "mb.active_voice_count",
    "mb.active_slot",
    "mb.last_opcode",
    "mb.control_producer",
    "mb.control_consumer",
    "mb.sfx_producer",
    "mb.sfx_consumer",
    "mb.completion_producer",
    "mb.completion_consumer",
    "music.starts",
    "music.active",
    "music.notes",
    "music.vm_ticks",
    "music.faults",
    "scsp0.keys",
    "scsp0.attenuation",
    "scsp0.pan_send",
    "scsp0.pitch",
    "scsp0.eg",
    "scsp0.sa_low",
    "scsp0.lsa",
    "scsp0.lea",
    "scsp.master",
    "scsp.monitor",
)


def flatten_sample(mailbox: list[int], diagnostics: list[int],
                   slots: list[int], master: list[int],
                   monitor: list[int]) -> dict[str, int]:
    """Name every peeked 16-bit word so samples can be diffed field by field."""
    values: dict[str, int] = {}
    for offset, name in MAILBOX_HEADER_FIELDS:
        values[f"mb.{name}"] = be16(mailbox, offset)
    for name, base, count in RINGS:
        for entry in range(count):
            entry_base = base + entry * 16
            for word in range(8):
                label = "op" if (name != "cmp" and word == 0) else f"w{word}"
                values[f"{name}{entry}.{label}"] = be16(mailbox,
                                                        entry_base + word * 2)
    for offset, name in MUSIC_DIAGNOSTIC_FIELDS:
        values[f"music.{name}"] = be16(diagnostics, offset)
    for slot in range(SCSP_SLOT_COUNT):
        base = slot * SCSP_SLOT_BYTES
        for word in range(SCSP_SLOT_BYTES // 2):
            offset = word * 2
            name = SCSP_SLOT_FIELDS.get(offset, f"w{offset:02x}")
            values[f"scsp{slot}.{name}"] = be16(slots, base + offset)
    values["scsp.master"] = be16(master, 0)
    values["scsp.monitor"] = be16(monitor, 0)
    return values


def sample_once(client: YmirClient, *, capture_video: bool = False
                ) -> dict[str, int]:
    mailbox = peek(client, MAILBOX_ADDRESS, MAILBOX_BYTES)
    diagnostics = peek(client, DIAGNOSTIC_ADDRESS, DIAGNOSTIC_BYTES)
    slots = peek(client, SCSP_SLOT_ADDRESS, SCSP_SLOT_COUNT * SCSP_SLOT_BYTES)
    master = peek(client, SCSP_MASTER_ADDRESS, 2)
    monitor = peek(client, SCSP_MONITOR_ADDRESS, 2)
    values = flatten_sample(mailbox, diagnostics, slots, master, monitor)
    if capture_video:
        # Liveness witness: a static audio path only means something if the
        # game loop is demonstrably presenting new frames in the same window.
        capture = client.call("video.capture")
        values["video.sequence"] = int(capture["sequence"])
        values["video.hash"] = capture["hash"]
    return values


def change_periods(samples: list[dict[str, Any]]) -> dict[str, Any]:
    """For every field that moved, report where it moved and the gap cadence."""
    report: dict[str, Any] = {}
    if len(samples) < 2:
        return report
    names = samples[0]["values"].keys()
    for name in names:
        events: list[int] = []
        deltas: list[int] = []
        previous = samples[0]["values"][name]
        for sample in samples[1:]:
            current = sample["values"][name]
            if current != previous:
                events.append(sample["frame"])
                deltas.append(current - previous
                              if isinstance(current, int)
                              and isinstance(previous, int) else None)
                previous = current
        if not events:
            continue
        gaps = [events[i + 1] - events[i] for i in range(len(events) - 1)]
        report[name] = {
            "first": samples[0]["values"][name],
            "last": previous,
            "change_count": len(events),
            "change_frames": events,
            "step_values": deltas,
            "gap_frames": gaps,
            "gap_min": min(gaps) if gaps else None,
            "gap_max": max(gaps) if gaps else None,
            "gap_mean": (sum(gaps) / len(gaps)) if gaps else None,
            "gap_mean_seconds": (sum(gaps) / len(gaps) / VBLANK_HZ)
            if gaps else None,
        }
    return report


def format_table(samples: list[dict[str, Any]], base_frame: int) -> str:
    lines = [
        "frame     seconds  changed fields (previous -> current)",
        "-------  --------  ------------------------------------------------",
    ]
    previous = samples[0]["values"]
    lines.append(
        f"{samples[0]['frame']:7d}  {(samples[0]['frame'] - base_frame) / VBLANK_HZ:8.2f}  "
        "<baseline>"
    )
    for sample in samples[1:]:
        current = sample["values"]
        changed = [
            f"{name} {previous[name]}->{current[name]}"
            for name in current
            if current[name] != previous[name]
        ]
        seconds = (sample["frame"] - base_frame) / VBLANK_HZ
        text = ", ".join(changed) if changed else "(no change)"
        lines.append(f"{sample['frame']:7d}  {seconds:8.2f}  {text}")
        previous = current
    return "\n".join(lines)


def probe_audio_timeseries(client: YmirClient, *, warmup_frames: int,
                           interval_frames: int, samples: int,
                           gameplay_poll_frames: int,
                           gameplay_max_frames: int,
                           capture_video: bool = False) -> dict[str, Any]:
    frame = 0

    def run_for(count: int) -> None:
        nonlocal frame
        remaining = count
        while remaining > 0:
            chunk = min(remaining, YMIR_MAX_RUN_FOR_FRAMES)
            client.call("exec.run_for", {"frames": chunk})
            remaining -= chunk
            frame += chunk

    started = time.perf_counter()
    run_bios_handoff(client, run_for, lambda _label: None)
    handoff_frame = frame
    print(f"[timeseries] BIOS handoff done at frame {frame}", file=sys.stderr,
          flush=True)

    run_for(warmup_frames)
    print(f"[timeseries] warmup done at frame {frame} "
          f"({time.perf_counter() - started:.1f}s wall)", file=sys.stderr,
          flush=True)

    gameplay_frame = None
    waited = 0
    while True:
        probe = sample_once(client)
        if probe["music.active"] == 1 and probe["mb.voices_started"] >= 1:
            gameplay_frame = frame
            break
        if waited >= gameplay_max_frames:
            break
        run_for(gameplay_poll_frames)
        waited += gameplay_poll_frames
    print(f"[timeseries] music active at frame {gameplay_frame} "
          f"(waited {waited} extra frames)", file=sys.stderr, flush=True)

    series: list[dict[str, Any]] = []
    for index in range(samples):
        values = sample_once(client, capture_video=capture_video)
        series.append({
            "index": index,
            "frame": frame,
            "wall_seconds": round(time.perf_counter() - started, 3),
            "values": values,
        })
        if index % 5 == 0:
            print(f"[timeseries] sample {index}/{samples} at frame {frame} "
                  f"({time.perf_counter() - started:.1f}s wall)",
                  file=sys.stderr, flush=True)
        if index + 1 < samples:
            run_for(interval_frames)

    base_frame = series[0]["frame"]
    for sample in series:
        sample["seconds"] = round((sample["frame"] - base_frame) / VBLANK_HZ, 4)

    key_series = {
        name: [sample["values"][name] for sample in series]
        for name in KEY_FIELDS if name in series[0]["values"]
    }
    return {
        "evidence_kind": "sm64-saturn-audio-mailbox-timeseries",
        "vblank_hz": VBLANK_HZ,
        "handoff_frame": handoff_frame,
        "warmup_frames": warmup_frames,
        "gameplay_confirmed_frame": gameplay_frame,
        "interval_frames": interval_frames,
        "interval_seconds": interval_frames / VBLANK_HZ,
        "sample_count": len(series),
        "base_frame": base_frame,
        "span_seconds": series[-1]["seconds"] if series else 0.0,
        "baseline": series[0]["values"] if series else {},
        "key_series": key_series,
        "samples": [
            {"index": s["index"], "frame": s["frame"], "seconds": s["seconds"],
             "wall_seconds": s["wall_seconds"], "values": s["values"]}
            for s in series
        ],
        "changes": change_periods(series),
        "table": format_table(series, base_frame),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ymir", type=Path, required=True)
    parser.add_argument("--ipl", type=Path, required=True)
    parser.add_argument("--cue", type=Path, required=True,
                        help="absolute path to the CUE (YmirClient changes cwd)")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--warmup-frames", type=int, default=3600,
                        help="frames after the BIOS handoff before sampling")
    parser.add_argument("--interval-frames", type=int, default=120,
                        help="frames between samples (<= 3600)")
    parser.add_argument("--samples", type=int, default=50)
    parser.add_argument("--capture-video", action="store_true",
                        help="record the presented frame sequence/hash at each "
                             "sample as a game-loop liveness witness")
    parser.add_argument("--gameplay-poll-frames", type=int, default=300)
    parser.add_argument("--gameplay-max-frames", type=int, default=7200)
    parser.add_argument("--timeout", type=float, default=5400.0)
    args = parser.parse_args(argv)

    if not 0 < args.interval_frames <= YMIR_MAX_RUN_FOR_FRAMES:
        parser.error("--interval-frames must be in 1..3600")
    if args.samples < 2:
        parser.error("--samples must be at least 2")
    for label, path in (("Ymir", args.ymir), ("IPL", args.ipl),
                        ("CUE", args.cue)):
        if not path.is_file():
            parser.error(f"{label} is not a file: {path}")

    client = YmirClient(args.ymir.resolve(), args.ipl.resolve(),
                        args.cue.resolve(), args.timeout)
    try:
        report = probe_audio_timeseries(
            client,
            warmup_frames=args.warmup_frames,
            interval_frames=args.interval_frames,
            samples=args.samples,
            gameplay_poll_frames=args.gameplay_poll_frames,
            gameplay_max_frames=args.gameplay_max_frames,
            capture_video=args.capture_video,
        )
    finally:
        try:
            client.shutdown()
        except (RuntimeError, TimeoutError):
            client.abort()

    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2, sort_keys=True)
                               + "\n", encoding="utf-8")
    print(report["table"])
    print()
    print("== fields that changed during the window ==")
    for name, info in sorted(report["changes"].items()):
        print(f"{name}: {info['first']} -> {info['last']} "
              f"({info['change_count']} changes, gaps {info['gap_frames']}, "
              f"mean gap {info['gap_mean_seconds']} s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
