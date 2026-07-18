#!/usr/bin/env python3
"""Run the Saturn hwtest disc in Ymir and decode its WRAM telemetry.

The runner speaks Ymir's newline-delimited JSON-RPC protocol over stdio. It
does not infer retail behavior: its report is explicitly tagged as emulator
evidence and should be paired with a retail capture when available.
"""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path
from typing import Any

from telemetry_decode import decode


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
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument(
        "--allow-invalid",
        action="store_true",
        help="write a diagnostic report even when the telemetry magic is absent",
    )
    args = parser.parse_args()
    if not 1 <= args.frames <= 3600:
        parser.error("--frames must be between 1 and 3600")
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

    requests = [
        request("exec.run_for", 1, {"frames": args.frames}),
        request("mem.peek", 2, {"address": "0x06010000", "count": 120}),
        request("instance.shutdown", 3),
    ]
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
    telemetry_response = response_for(messages, 2)
    raw_data = telemetry_response["result"]["data"]
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
        "protocol": {
            "ready": any(message.get("method") == "instance.ready" for message in messages),
            "stopped_reasons": stopped_reasons,
        },
        "diagnostics": {
            "stderr": completed.stderr,
            "cd_block_copy_unimplemented": "CD-block copy operation" in completed.stderr,
        },
        "telemetry": telemetry,
    }
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
