#!/usr/bin/env python3
"""Run the Saturn hwtest disc in Ymir and decode its WRAM telemetry.

The runner speaks Ymir's newline-delimited JSON-RPC protocol over stdio. It
does not infer retail behavior: its report is explicitly tagged as emulator
evidence and should be paired with a retail capture when available.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import subprocess
from pathlib import Path
from typing import Any

from telemetry_decode import decode


def has_cd_block_copy_limitation(stderr: str) -> bool:
    """Recognize Ymir's current and historical CD-block copy diagnostics."""
    normalized = stderr.casefold()
    return (
        "cd-block copy operation" in normalized
        or "get copy error command is unimplemented" in normalized
    )


def request(method: str, request_id: int, params: dict[str, Any] | None = None) -> dict[str, Any]:
    message: dict[str, Any] = {"jsonrpc": "2.0", "method": method, "id": request_id}
    if params is not None:
        message["params"] = params
    return message


def response_for(messages: list[dict[str, Any]], request_id: int) -> dict[str, Any]:
    for message in messages:
        if message.get("id") == request_id:
            if "error" in message:
                raise RuntimeError(f"Ymir request {request_id} failed: {message['error']}")
            return message
    raise RuntimeError(f"Ymir returned no response for request {request_id}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ymir", type=Path, required=True, help="path to the ymir-headless executable")
    parser.add_argument("--ipl", type=Path, required=True, help="Saturn BIOS/IPL image")
    parser.add_argument("--game", type=Path, required=True, help="hwtest .cue/.iso path")
    parser.add_argument("--frames", type=int, default=600, help="bounded frames to execute (1..3600)")
    parser.add_argument("--output", type=Path, default=Path("ymir-hwtest-report.json"))
    parser.add_argument(
        "--raw-output",
        type=Path,
        help="optional path for the unchanged raw mem.peek byte payload",
    )
    parser.add_argument(
        "--screenshot-output",
        type=Path,
        help="optional path for a PNG captured through Ymir video.capture",
    )
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument(
        "--allow-invalid",
        action="store_true",
        help="write a diagnostic report even when the telemetry magic is absent",
    )
    parser.add_argument(
        "--bios-input",
        action="store_true",
        help="automate the USA BIOS language and clock screens through Ymir input.pulse",
    )
    parser.add_argument(
        "--event-word-poke",
        type=lambda value: int(value, 0),
        metavar="VALUE",
        help=(
            "paused-only Ymir diagnostic: overwrite BIOS event word 0x06020240 "
            "after the initial run, then continue execution"
        ),
    )
    parser.add_argument(
        "--handoff-yield",
        action="store_true",
        help=(
            "pause after the initial BIOS run and resume without mutating memory; "
            "isolates whether the Ymir handoff needs a debugger write"
        ),
    )
    parser.add_argument(
        "--post-poke-frames",
        type=int,
        default=600,
        help="frames to run after --event-word-poke (1..3600)",
    )
    args = parser.parse_args()
    if not 1 <= args.frames <= 3600 or not 1 <= args.post_poke_frames <= 3600:
        parser.error("--frames and --post-poke-frames must be between 1 and 3600")
    if args.event_word_poke is not None and not 0 <= args.event_word_poke <= 0xFFFFFFFF:
        parser.error("--event-word-poke must be an unsigned 32-bit value")
    if args.event_word_poke is not None and args.handoff_yield:
        parser.error("--event-word-poke and --handoff-yield are mutually exclusive")
    for label, path in (("Ymir executable", args.ymir), ("IPL", args.ipl), ("game", args.game)):
        if not path.is_file():
            parser.error(f"{label} not found: {path}")

    # The subprocess runs from the cue directory so relative asset paths do
    # not affect disc loading. Pass absolute paths as well; otherwise a
    # relative --game value is resolved twice (cue-dir/cue-dir/game).
    args.ymir = args.ymir.resolve()
    args.ipl = args.ipl.resolve()
    args.game = args.game.resolve()
    args.output = args.output.resolve()
    if args.raw_output:
        args.raw_output = args.raw_output.resolve()
    if args.screenshot_output:
        args.screenshot_output = args.screenshot_output.resolve()

    requests: list[dict[str, Any]] = []
    next_id = 1
    if args.bios_input:
        requests.extend(
            [
                request("exec.run_for", next_id, {"frames": 120}),
                request("input.pulse", next_id + 1, {"buttons": 0x4000}),
                request("exec.run_for", next_id + 2, {"frames": 30}),
                request("input.pulse", next_id + 3, {"buttons": 0x0400}),
                request("exec.run_for", next_id + 4, {"frames": 1200}),
            ]
        )
        next_id += 5
        for _ in range(5):
            requests.extend(
                [
                    request("input.pulse", next_id, {"buttons": 0x4000}),
                    request("exec.run_for", next_id + 1, {"frames": 30}),
                ]
            )
            next_id += 2
    run_id = next_id
    requests.append(request("exec.run_for", run_id, {"frames": args.frames}))
    next_id += 1
    pre_poke_event_id: int | None = None
    if args.event_word_poke is not None:
        pre_poke_event_id = next_id
        requests.append(request("mem.peek", next_id, {"address": "0x06020240", "count": 4}))
        next_id += 1
        requests.append(
            request(
                "mem.poke",
                next_id,
                {
                    "address": "0x06020240",
                    "data": list(args.event_word_poke.to_bytes(4, byteorder="big")),
                },
            )
        )
        next_id += 1
        requests.append(request("exec.run_for", next_id, {"frames": args.post_poke_frames}))
        next_id += 1
    elif args.handoff_yield:
        requests.append(request("exec.run_for", next_id, {"frames": args.post_poke_frames}))
        next_id += 1
    telemetry_id = next_id
    registers_id = next_id + 1
    boot_window_id = next_id + 2
    event_word_id = next_id + 3
    screenshot_id = next_id + 4 if args.screenshot_output else None
    requests.extend(
        [
            request("mem.peek", telemetry_id, {"address": "0x06030000", "count": 120}),
            request("regs.read", registers_id, {"target": "sh2.master"}),
            request("mem.peek", boot_window_id, {"address": "0x060402C0", "count": 128}),
            request("mem.peek", event_word_id, {"address": "0x06020240", "count": 32}),
        ]
    )
    if screenshot_id is not None:
        requests.append(request("video.capture", screenshot_id))
    requests.append(request("instance.shutdown", (screenshot_id or event_word_id) + 1))
    command = [str(args.ymir), "--ipl", str(args.ipl), "--game", str(args.game)]
    try:
        completed = subprocess.run(
            command,
            input="".join(json.dumps(message) + "\n" for message in requests),
            text=True,
            capture_output=True,
            cwd=args.game.parent,
            timeout=args.timeout,
            check=False,
        )
    except subprocess.TimeoutExpired:
        parser.error(f"Ymir did not finish within {args.timeout:.1f} seconds")
    except OSError as error:
        parser.error(str(error))
    if completed.returncode != 0:
        raise SystemExit(f"Ymir exited with status {completed.returncode}: {completed.stderr.strip()}")

    messages: list[dict[str, Any]] = []
    for line in completed.stdout.splitlines():
        if line.strip():
            messages.append(json.loads(line))
    telemetry_response = response_for(messages, telemetry_id)
    registers_response = response_for(messages, registers_id)
    boot_window_response = response_for(messages, boot_window_id)
    event_word_response = response_for(messages, event_word_id)
    pre_poke_event_response = (
        response_for(messages, pre_poke_event_id) if pre_poke_event_id is not None else None
    )
    screenshot: dict[str, Any] | None = None
    if screenshot_id is not None:
        screenshot_response = response_for(messages, screenshot_id)
        screenshot_result = screenshot_response["result"]
        png_bytes = base64.b64decode(screenshot_result["data"], validate=True)
        if screenshot_result.get("mime_type") != "image/png":
            raise RuntimeError("Ymir video.capture did not return a PNG")
        args.screenshot_output.parent.mkdir(parents=True, exist_ok=True)
        args.screenshot_output.write_bytes(png_bytes)
        screenshot = {
            "path": str(args.screenshot_output),
            "bytes": len(png_bytes),
            "sha256": hashlib.sha256(png_bytes).hexdigest(),
            "sequence": screenshot_result["sequence"],
            "width": screenshot_result["width"],
            "height": screenshot_result["height"],
            "frame_hash": screenshot_result["hash"],
        }
    raw_data = telemetry_response["result"]["data"]
    raw_bytes = bytes(raw_data)
    raw_telemetry = {
        "bytes": len(raw_bytes),
        "sha256": hashlib.sha256(raw_bytes).hexdigest(),
    }
    if args.raw_output:
        args.raw_output.parent.mkdir(parents=True, exist_ok=True)
        args.raw_output.write_bytes(raw_bytes)
        raw_telemetry["path"] = str(args.raw_output)
    try:
        telemetry = decode(raw_data, require_complete=True)
    except ValueError as error:
        if not args.allow_invalid:
            raise
        telemetry = {"decode_error": str(error), "raw_data": raw_data}
    stopped_reasons = [
        message.get("params", {}).get("reason")
        for message in messages
        if message.get("method") == "instance.stopped"
    ]
    report = {
        "evidence_kind": "ymir-emulator",
        "ymir": str(args.ymir),
        "ipl": str(args.ipl),
        "game": str(args.game),
        "frames": args.frames,
        "bios_input": args.bios_input,
        "event_word_poke": args.event_word_poke,
        "handoff_yield": args.handoff_yield,
        "post_poke_frames": (
            args.post_poke_frames if args.event_word_poke is not None or args.handoff_yield else None
        ),
        "protocol": {
            "ready": any(message.get("method") == "instance.ready" for message in messages),
            "stopped_reasons": stopped_reasons,
        },
        "registers_at_stop": registers_response.get("result", {}),
        "boot_window": boot_window_response.get("result", {}),
        "event_word": event_word_response.get("result", {}),
        "event_word_before_poke": (
            pre_poke_event_response.get("result", {}) if pre_poke_event_response else None
        ),
        "diagnostics": {
            "stderr": completed.stderr,
            "cd_block_copy_unimplemented": has_cd_block_copy_limitation(completed.stderr),
        },
        "raw_telemetry": raw_telemetry,
        "screenshot": screenshot,
        "telemetry": telemetry,
    }
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
