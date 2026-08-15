#!/usr/bin/env python3
"""Probe the running sourceboot audio mailbox, SFXB header, and SCSP state.

Drives Ymir headless through the BIOS handoff, runs the target for a bounded
frame count, then peeks the audio diagnostic regions (control mailbox, SFXB
staging, SCSP registers, and the 0x25A07F00 music-diagnostic words) and emits
one structured JSON report.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any

from capture_route_views import YmirClient
from capture_sourceboot_boot_trace import run_bios_handoff

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SFX_METADATA = (REPO_ROOT / "build" / "saturn" / "audio" / "generated"
                        / "sourceboot-sfx" / "bob_sfx_metadata.bin")


def peek(client, address, count):
    value = client.call("mem.peek", {"address": address, "count": count})
    data = value.get("data", value)
    if isinstance(data, dict):
        data = data.get("data", [])
    if isinstance(data, str):
        data = list(bytes.fromhex(data))
    return list(data)


def be16(data, off):
    return (data[off] << 8) | data[off + 1]


def probe_audio_state(client: YmirClient, *, frames: int,
                      sfx_metadata: Path) -> dict[str, Any]:
    run_bios_handoff(client, lambda frames: client.call("exec.run_for", {"frames": frames}), lambda _label: None)
    client.call("exec.run_for", {"frames": frames})
    mailbox = peek(client, 0x25A04000, 0x100)
    sfxb = peek(client, 0x25A05000, 0x600)
    scsp = peek(client, 0x25B00000, 0x200)
    scsp_master = peek(client, 0x25B00400, 0x2)
    diagnostics = peek(client, 0x25A07F00, 0x20)
    ring = []
    for slot in range(8):
        off = 0x40 + slot * 16
        ring.append({"slot": slot, "opcode": be16(mailbox, off), "words": [be16(mailbox, off + 2 + 2*i) for i in range(7)]})
    return {
        "mailbox_words": [be16(mailbox, 2*i) for i in range(0x40 // 2)],
        "service_tick": be16(mailbox, 0x3A),
        "active_voice_count": be16(mailbox, 0x3C),
        "music_diagnostics": {
            "starts": be16(diagnostics, 0),
            "faults": be16(diagnostics, 2),
            "vm_ticks": be16(diagnostics, 4),
            "active": be16(diagnostics, 6),
            "notes": be16(diagnostics, 8),
            "sequence_offset": be16(diagnostics, 10),
            "sequence_bytes": be16(diagnostics, 12),
            "reject_mask": be16(diagnostics, 14),
            "malformed": be16(diagnostics, 16),
            "dropped": be16(diagnostics, 18),
            "consume_fail": be16(diagnostics, 20),
            "scsp_fail": be16(diagnostics, 22),
            "last_note": be16(diagnostics, 24),
            "last_arg1": be16(diagnostics, 26),
            "last_arg0": be16(diagnostics, 28),
            "last_failure": be16(diagnostics, 30),
        },
        "sfxb_header": {
            "target_prefix_sha256": hashlib.sha256(bytes(sfxb[:1232])).hexdigest(),
            "local_prefix_sha256": hashlib.sha256(sfx_metadata.read_bytes()).hexdigest(),
            "magic": bytes(sfxb[0:4]).hex(),
            "version": be16(sfxb, 4),
            "header_bytes": be16(sfxb, 6),
            "generation": be16(sfxb, 8),
            "mapping_count": be16(sfxb, 10),
            "sample_count": be16(sfxb, 12),
            "mapping_offset": be16(sfxb, 14),
            "sample_offset": be16(sfxb, 16),
            "metadata_bytes": be16(sfxb, 18),
            "pcm_bytes": int.from_bytes(bytes(sfxb[20:24]), "big"),
            "sequence_offset": int.from_bytes(bytes(sfxb[24:28]), "big"),
            "sequence_bytes": be16(sfxb, 28),
            "music_sample_index": be16(sfxb, 30),
            "sample63": sfxb[be16(sfxb, 16) + 63*12:be16(sfxb, 16) + 64*12],
        },
        "control_ring": ring,
        "scsp_words": [be16(scsp, 2*i) for i in range(64)],
        "scsp_master": scsp_master,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ymir", type=Path, required=True, help="Ymir headless executable")
    parser.add_argument("--ipl", type=Path, required=True, help="Saturn BIOS image")
    parser.add_argument("--cue", type=Path, required=True, help="exact sourceboot CUE to boot")
    parser.add_argument(
        "--output", type=Path,
        help="JSON report path (default: print the report to stdout)",
    )
    parser.add_argument(
        "--frames", type=int, default=3600,
        help="frames to run after the BIOS handoff before peeking "
             "(Ymir caps one exec.run_for at 3600)",
    )
    parser.add_argument("--timeout", type=float, default=300.0)
    parser.add_argument(
        "--sfx-metadata", type=Path, default=DEFAULT_SFX_METADATA,
        help="local SFXB metadata blob hashed against the target's staged prefix",
    )
    args = parser.parse_args(argv)
    client = YmirClient(args.ymir, args.ipl, args.cue, args.timeout)
    try:
        report = probe_audio_state(client, frames=args.frames,
                                   sfx_metadata=args.sfx_metadata)
    finally:
        client.shutdown()
    text = json.dumps(report, sort_keys=True)
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text + "\n", encoding="utf-8")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
