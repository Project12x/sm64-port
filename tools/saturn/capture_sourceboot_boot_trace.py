#!/usr/bin/env python3
"""Read sourceboot's post-BIOS boundary trace through headless Ymir.

This is a bounded diagnostic capture, not a target build, GUI launch, or
performance measurement.  It resolves the target-RAM record from the sibling
ELF and emits both its decoded last boundary and the raw 32-bit words.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import time
from pathlib import Path
from typing import Any

from capture_hwtest import artifact_identity, cap_stderr
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
_CUE_FILE = re.compile(r'^\s*FILE\s+(?:"([^"]+)"|(\S+))\s+\S+\s*$', re.IGNORECASE)

STAGE_NAMES = {
    0: "elf-data-initialized",
    1: "user-init-entry",
    2: "user-init-callbacks-registered",
    3: "main-entry",
    4: "bootstrap-before",
    5: "bootstrap-retired",
    6: "thread5-before",
    7: "thread5-after",
    8: "stale-wait-before",
    9: "stale-wait-after",
    10: "source-tick-before",
    11: "source-tick-after",
    12: "vdp1-render-before",
    13: "vdp1-render-after",
    14: "vdp1-sync-before",
    15: "vdp1-sync-after",
    16: "vdp2-commit-before",
    17: "vdp2-commit-after",
}


def parse_cue_file_reference(cue: Path) -> Path:
    """Resolve the only disc image that this sourceboot CUE actually loads."""
    references: list[str] = []
    for line in cue.read_text(encoding="utf-8-sig").splitlines():
        match = _CUE_FILE.match(line)
        if match:
            references.append(match.group(1) or match.group(2))
    if len(references) != 1:
        raise ValueError("sourceboot CUE must contain exactly one FILE reference")
    reference = Path(references[0])
    if reference.is_absolute() or ".." in reference.parts:
        raise ValueError("sourceboot CUE FILE reference must stay beside the CUE")
    iso = (cue.parent / reference).resolve()
    if not iso.is_file():
        raise ValueError(f"CUE referenced ISO is not a file: {iso}")
    return iso


def bind_capture_artifacts(cue: Path, elf: Path) -> dict[str, dict[str, Any]]:
    """Fail closed unless CUE, its ISO, and ELF describe one fresh build."""
    cue = cue.resolve()
    elf = elf.resolve()
    iso = parse_cue_file_reference(cue)
    if cue.stem != iso.stem or cue.stem != elf.stem:
        raise ValueError("CUE, referenced ISO, and ELF must have the same build name")
    expected_elf = (cue.parent / "obj" / f"{cue.stem}.elf").resolve()
    if elf != expected_elf:
        raise ValueError(f"ELF must be the CUE sibling build ELF: {expected_elf}")
    if iso.stat().st_mtime < elf.stat().st_mtime:
        raise ValueError(
            f"referenced ISO is older than ELF ({iso.name} < {elf.name}); regenerate the disc image"
        )
    return {
        "cue": artifact_identity(cue),
        "iso": artifact_identity(iso),
        "elf": artifact_identity(elf),
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


def validate_post_bios_checkpoint_interval(interval: int | None) -> int | None:
    """Accept an omitted interval or a strictly positive frame chunk size."""
    if interval is not None and interval <= 0:
        raise ValueError("post-BIOS checkpoint interval must be positive")
    return interval


def run_post_bios_window(
    run_for: Any,
    checkpoint: Any,
    *,
    post_bios_frames: int,
    checkpoint_interval: int | None,
) -> None:
    """Run the bounded post-BIOS window, optionally preserving each raw chunk."""
    if checkpoint_interval is None:
        run_for(post_bios_frames)
        checkpoint("post-bios")
        return

    elapsed = 0
    remaining = post_bios_frames
    while remaining:
        chunk = min(checkpoint_interval, remaining)
        run_for(chunk)
        elapsed += chunk
        remaining -= chunk
        checkpoint(f"post-bios-{elapsed}")


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


def raw_trace_words(data: list[int] | None) -> list[int] | None:
    """Decode the words for evidence even when the trace header is invalid."""
    if (
        not isinstance(data, list)
        or len(data) != SOURCEBOOT_BOOT_TRACE_BYTES
        or any(not isinstance(byte, int) or not 0 <= byte <= 0xFF for byte in data)
    ):
        return None
    return [
        int.from_bytes(bytes(data[index : index + 4]), byteorder="big")
        for index in range(0, SOURCEBOOT_BOOT_TRACE_BYTES, 4)
    ]


def protocol_and_diagnostics(client: YmirClient | None) -> dict[str, Any]:
    """Return bounded Ymir host evidence shared by passing and failing captures."""
    stderr = client.stderr if client else ""
    capped_stderr, stderr_original_bytes = cap_stderr(stderr)
    notifications = client.notifications if client else []
    return {
        "protocol": {
            "ready": any(
                message.get("method") == "instance.ready" for message in notifications
            ),
            "stopped_reasons": [
                message.get("params", {}).get("reason")
                for message in notifications
                if message.get("method") == "instance.stopped"
            ],
            "notifications": notifications,
        },
        "diagnostics": {
            "stderr": capped_stderr,
            "stderr_truncated": stderr_original_bytes > len(capped_stderr),
            "stderr_original_bytes": stderr_original_bytes,
        },
    }


def build_failed_trace_report(
    *, client: YmirClient | None, raw_data: list[int] | None, error: BaseException
) -> dict[str, Any]:
    """Return bounded host evidence for an unsuccessful target trace capture."""
    report = protocol_and_diagnostics(client)
    report["trace"] = {
        "raw_bytes": raw_data,
        "raw_words": raw_trace_words(raw_data),
        "decode_error": str(error),
    }
    return report


def read_boot_trace(client: YmirClient, address: int) -> dict[str, Any]:
    result = client.call(
        "mem.peek", {"address": address, "count": SOURCEBOOT_BOOT_TRACE_BYTES}
    )
    data = result.get("data")
    if not isinstance(data, list):
        raise ValueError("Ymir boot trace capture has no byte data")
    return decode_boot_trace(data)


def stopped_pcs(client: YmirClient) -> list[int]:
    """Return every PC Ymir reported at a bounded run stop so far."""
    return [
        pc
        for message in client.notifications
        if message.get("method") == "instance.stopped"
        for pc in [message.get("params", {}).get("pc")]
        if isinstance(pc, int)
    ]


def capture_trace_checkpoint(
    client: YmirClient, address: int, label: str, emulated_frames: int
) -> dict[str, Any]:
    """Preserve a raw trace sample at one deterministic BIOS boundary."""
    result = client.call(
        "mem.peek", {"address": address, "count": SOURCEBOOT_BOOT_TRACE_BYTES}
    )
    data = result.get("data")
    if not isinstance(data, list):
        raise ValueError("Ymir boot trace checkpoint has no byte data")
    return {
        "label": label,
        "emulated_frames": emulated_frames,
        "raw_bytes": data,
        "raw_words": raw_trace_words(data),
        "stopped_pcs": stopped_pcs(client),
        "notification_count": len(client.notifications),
    }


def run_bios_handoff(client: YmirClient, run_for: Any, checkpoint: Any) -> None:
    """Use the proven USA BIOS input sequence, then release all buttons."""
    run_for(120)
    checkpoint("bios-initial-wait")
    client.call("input.pulse", {"buttons": 0x4000})
    run_for(30)
    checkpoint("bios-menu-pulse")
    client.call("input.pulse", {"buttons": 0x0400})
    run_for(1200)
    checkpoint("bios-disc-start")
    for index in range(5):
        client.call("input.pulse", {"buttons": 0x4000})
        run_for(30)
        checkpoint(f"bios-start-pulse-{index + 1}")
    client.call("input.pulse", {"buttons": 0xFFF8})
    checkpoint("bios-input-release")


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
    parser.add_argument(
        "--post-bios-checkpoint-interval",
        type=int,
        default=None,
        help="optional positive frame interval for post-BIOS trace checkpoints",
    )
    parser.add_argument("--timeout", type=float, default=120.0)
    args = parser.parse_args()

    for label, path in (("Ymir", args.ymir), ("IPL", args.ipl), ("game", args.game), ("ELF", args.elf)):
        if not path.is_file():
            parser.error(f"{label} is not a file: {path}")
    try:
        args.post_bios_frames = validate_post_bios_frames(args.post_bios_frames)
        args.post_bios_checkpoint_interval = validate_post_bios_checkpoint_interval(
            args.post_bios_checkpoint_interval
        )
    except ValueError as error:
        parser.error(str(error))
    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    args.ymir = args.ymir.resolve()
    args.ipl = args.ipl.resolve()
    args.game = args.game.resolve()
    args.elf = args.elf.resolve()
    args.output = args.output.resolve()
    try:
        artifacts = bind_capture_artifacts(args.game, args.elf)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    trace_address = resolve_trace_symbol(args.elf)

    wall_start = time.perf_counter()
    emulated_frames = 0
    client: YmirClient | None = None
    raw_data: list[int] | None = None
    trace_checkpoints: list[dict[str, Any]] = []
    trace: dict[str, Any] | None = None
    failure: BaseException | None = None
    try:
        client = YmirClient(args.ymir, args.ipl, args.game, args.timeout)

        def run_for(frames: int) -> None:
            nonlocal emulated_frames
            client.call("exec.run_for", {"frames": frames})
            emulated_frames += frames

        def checkpoint(label: str) -> None:
            trace_checkpoints.append(
                capture_trace_checkpoint(client, trace_address, label, emulated_frames)
            )

        checkpoint("protocol-ready")
        run_bios_handoff(client, run_for, checkpoint)
        run_post_bios_window(
            run_for,
            checkpoint,
            post_bios_frames=args.post_bios_frames,
            checkpoint_interval=args.post_bios_checkpoint_interval,
        )
        result = client.call(
            "mem.peek", {"address": trace_address, "count": SOURCEBOOT_BOOT_TRACE_BYTES}
        )
        data = result.get("data")
        if not isinstance(data, list):
            raise ValueError("Ymir boot trace capture has no byte data")
        raw_data = data
        trace = decode_boot_trace(raw_data)
        client.shutdown()
    except BaseException as error:
        failure = error
        if client is not None:
            client.abort()

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
        "artifacts": artifacts,
        "trace_symbol": SOURCEBOOT_BOOT_TRACE_SYMBOL,
        "trace_address": trace_address,
        "emulated_frames": emulated_frames,
        "post_bios_checkpoint_interval": args.post_bios_checkpoint_interval,
        "trace_checkpoints": trace_checkpoints,
        "wall_seconds": time.perf_counter() - wall_start,
    }
    if failure is None:
        report["trace"] = trace
        report.update(protocol_and_diagnostics(client))
    else:
        report.update(build_failed_trace_report(client=client, raw_data=raw_data, error=failure))
        report["failure"] = {"message": str(failure), "type": type(failure).__name__}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 1 if failure is not None else 0


if __name__ == "__main__":
    raise SystemExit(main())
