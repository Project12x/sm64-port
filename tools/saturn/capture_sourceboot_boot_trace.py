#!/usr/bin/env python3
"""Read sourceboot's post-BIOS boundary trace through headless Ymir.

This is a bounded diagnostic capture, not a target build, GUI launch, or
performance measurement.  It resolves the target-RAM record from the sibling
ELF and emits both its decoded last boundary and the raw 32-bit words.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import time
from pathlib import Path
from typing import Any

from capture_hwtest import artifact_identity
from capture_route_views import YmirClient


NM = Path(
    "D:/Code/RetroDev/sm64-saturn-port/work/yaul-install/bin/sh-elf-nm.exe"
)
MSYS_TOOLCHAIN_WRAPPER = Path(__file__).with_name("with-msys-toolchain.ps1")
POWERSHELL = "powershell.exe"
SOURCEBOOT_BOOT_TRACE_SYMBOL = "sourceboot_boot_trace"
SOURCEBOOT_BOOT_TRACE_MAGIC = 0x53394254
SOURCEBOOT_BOOT_TRACE_VERSION = 1
SOURCEBOOT_BOOT_TRACE_WORD_COUNT = 8
SOURCEBOOT_BOOT_TRACE_BYTES = SOURCEBOOT_BOOT_TRACE_WORD_COUNT * 4
YMIR_MAX_RUN_FOR_FRAMES = 3600

STAGE_NAMES = {
    1: "main-entry",
    2: "bootstrap-before",
    3: "bootstrap-retired",
    4: "thread5-before",
    5: "thread5-after",
    6: "stale-wait-before",
    7: "stale-wait-after",
    8: "source-tick-before",
    9: "source-tick-after",
    10: "vdp1-render-before",
    11: "vdp1-render-after",
    12: "vdp1-sync-before",
    13: "vdp1-sync-after",
    14: "vdp2-commit-before",
    15: "vdp2-commit-after",
}


def parse_symbol_address(nm_output: str) -> int:
    """Return the exact globally exported address for the trace record."""
    for line in nm_output.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[2] in (
            SOURCEBOOT_BOOT_TRACE_SYMBOL,
            f"_{SOURCEBOOT_BOOT_TRACE_SYMBOL}",
        ):
            return int(fields[0], 16)
    raise ValueError(f"ELF does not export {SOURCEBOOT_BOOT_TRACE_SYMBOL}")


def validate_post_bios_frames(frames: int) -> int:
    if not 1 <= frames <= YMIR_MAX_RUN_FOR_FRAMES:
        raise ValueError(
            f"post-BIOS frames must be between 1 and {YMIR_MAX_RUN_FOR_FRAMES}"
        )
    return frames


def wrapped_nm_command(elf: Path, *, nm: Path = NM) -> list[str]:
    """Run nm only through the DLL-safe project MSYS environment."""
    return [
        POWERSHELL,
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        str(MSYS_TOOLCHAIN_WRAPPER),
        str(nm),
        "-g",
        "--defined-only",
        str(elf),
    ]


def resolve_trace_symbol(elf: Path, *, nm: Path = NM, run: Any = subprocess.run) -> int:
    completed = run(
        wrapped_nm_command(elf, nm=nm),
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        raise ValueError(
            f"DLL-safe sh-elf-nm wrapper failed for {elf}: {completed.stderr.strip()}"
        )
    return parse_symbol_address(completed.stdout)


def decode_boot_trace(data: list[int]) -> dict[str, Any]:
    if len(data) != SOURCEBOOT_BOOT_TRACE_BYTES:
        raise ValueError(
            f"boot trace returned {len(data)} bytes, expected {SOURCEBOOT_BOOT_TRACE_BYTES}"
        )
    raw_words = [
        int.from_bytes(bytes(data[index : index + 4]), byteorder="big")
        for index in range(0, SOURCEBOOT_BOOT_TRACE_BYTES, 4)
    ]
    if raw_words[0] != SOURCEBOOT_BOOT_TRACE_MAGIC:
        raise ValueError(f"boot trace magic is 0x{raw_words[0]:08x}")
    if raw_words[1] != SOURCEBOOT_BOOT_TRACE_VERSION:
        raise ValueError(f"boot trace version is {raw_words[1]}")
    return {
        "magic": raw_words[0],
        "version": raw_words[1],
        "stage": raw_words[2],
        "stage_id": raw_words[3],
        "last_stage": STAGE_NAMES.get(raw_words[3], "unknown"),
        "observed_vblank_generation": raw_words[4],
        "scheduler_credit": raw_words[5],
        "vdp1_presentation_generation": raw_words[6],
        "vdp2_presentation_generation": raw_words[7],
        "raw_words": raw_words,
    }


def read_boot_trace(client: YmirClient, address: int) -> dict[str, Any]:
    result = client.call(
        "mem.peek", {"address": address, "count": SOURCEBOOT_BOOT_TRACE_BYTES}
    )
    data = result.get("data")
    if not isinstance(data, list):
        raise ValueError("Ymir boot trace capture has no byte data")
    return decode_boot_trace(data)


def run_bios_handoff(client: YmirClient, run_for: Any) -> None:
    """Use the proven USA BIOS input sequence, then release all buttons."""
    run_for(120)
    client.call("input.pulse", {"buttons": 0x4000})
    run_for(30)
    client.call("input.pulse", {"buttons": 0x0400})
    run_for(1200)
    for _ in range(5):
        client.call("input.pulse", {"buttons": 0x4000})
        run_for(30)
    client.call("input.pulse", {"buttons": 0xFFF8})


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ymir", type=Path, required=True, help="ymir-headless executable")
    parser.add_argument("--ipl", type=Path, required=True, help="Saturn BIOS image")
    parser.add_argument("--game", type=Path, required=True, help="trace CUE image")
    parser.add_argument("--elf", type=Path, required=True, help="matching sourceboot ELF")
    parser.add_argument("--output", type=Path, required=True, help="JSON diagnostic report")
    parser.add_argument(
        "--post-bios-frames",
        type=int,
        default=180,
        help="bounded frames to run after BIOS handoff (default: 180)",
    )
    parser.add_argument("--timeout", type=float, default=120.0)
    args = parser.parse_args()

    for label, path in (("Ymir", args.ymir), ("IPL", args.ipl), ("game", args.game), ("ELF", args.elf)):
        if not path.is_file():
            parser.error(f"{label} is not a file: {path}")
    try:
        args.post_bios_frames = validate_post_bios_frames(args.post_bios_frames)
    except ValueError as error:
        parser.error(str(error))
    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    args.ymir = args.ymir.resolve()
    args.ipl = args.ipl.resolve()
    args.game = args.game.resolve()
    args.elf = args.elf.resolve()
    args.output = args.output.resolve()
    trace_address = resolve_trace_symbol(args.elf)

    wall_start = time.perf_counter()
    emulated_frames = 0
    client: YmirClient | None = None
    try:
        client = YmirClient(args.ymir, args.ipl, args.game, args.timeout)

        def run_for(frames: int) -> None:
            nonlocal emulated_frames
            client.call("exec.run_for", {"frames": frames})
            emulated_frames += frames

        run_bios_handoff(client, run_for)
        run_for(args.post_bios_frames)
        trace = read_boot_trace(client, trace_address)
        client.shutdown()
    except BaseException:
        if client is not None:
            client.abort()
        raise

    report = {
        "evidence_kind": "ymir-sourceboot-post-bios-boot-trace",
        "diagnostic_only": True,
        "manual_gui_launch": False,
        "target_build": False,
        "performance_measurement": False,
        "ymir": str(args.ymir),
        "ipl": str(args.ipl),
        "game": artifact_identity(args.game),
        "elf": artifact_identity(args.elf),
        "trace_symbol": SOURCEBOOT_BOOT_TRACE_SYMBOL,
        "trace_address": trace_address,
        "emulated_frames": emulated_frames,
        "wall_seconds": time.perf_counter() - wall_start,
        "trace": trace,
        "protocol": {
            "ready": any(
                message.get("method") == "instance.ready"
                for message in (client.notifications if client else [])
            ),
            "stopped_reasons": [
                message.get("params", {}).get("reason")
                for message in (client.notifications if client else [])
                if message.get("method") == "instance.stopped"
            ],
        },
        "diagnostics": {"stderr": client.stderr if client else ""},
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
