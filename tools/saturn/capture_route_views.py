#!/usr/bin/env python3
"""Capture exact deterministic-route viewpoints through Ymir's video API.

Unlike a fixed-depth capture, this runner pauses on the sourceboot route
checkpoint itself.  Every PNG is therefore paired with the exact replay tick
that selected it rather than an inferred emulator-frame depth.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import queue
import subprocess
import threading
import time
from collections import deque
from pathlib import Path
from typing import Any

from capture_hwtest import (
    STDERR_CAPTURE_LIMIT,
    artifact_identity,
    emulation_timing,
    newest_sibling_elf,
    request,
    stale_game_image,
)
from compare_route_reports import MAGIC as ROUTE_MAGIC
from compare_route_reports import PROBE_BYTES as ROUTE_PROBE_BYTES
from compare_route_reports import VERSION as ROUTE_VERSION
from compare_route_reports import decode_probe


class YmirClient:
    """Small synchronous client over Ymir's newline-delimited JSON-RPC."""

    def __init__(
        self,
        executable: Path,
        ipl: Path,
        game: Path,
        timeout: float,
    ) -> None:
        self._deadline = time.monotonic() + timeout
        self._next_id = 1
        self._messages: queue.Queue[dict[str, Any] | BaseException | None] = queue.Queue()
        self._stderr: deque[str] = deque()
        self._stderr_bytes = 0
        self.notifications: list[dict[str, Any]] = []
        self.process = subprocess.Popen(
            [
                str(executable),
                "--ipl",
                str(ipl),
                "--game",
                str(game),
                "--dram-cart",
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
            cwd=game.parent,
        )
        assert self.process.stdin is not None
        assert self.process.stdout is not None
        assert self.process.stderr is not None
        threading.Thread(target=self._read_stdout, daemon=True).start()
        threading.Thread(target=self._read_stderr, daemon=True).start()
        self._wait_ready()

    def _remaining(self) -> float:
        remaining = self._deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("Ymir route-view capture exceeded its wall-clock budget")
        return remaining

    def _read_stdout(self) -> None:
        assert self.process.stdout is not None
        try:
            for line in self.process.stdout:
                if line.strip():
                    self._messages.put(json.loads(line))
        except BaseException as error:
            self._messages.put(error)
        finally:
            self._messages.put(None)

    def _read_stderr(self) -> None:
        assert self.process.stderr is not None
        for line in self.process.stderr:
            self._stderr.append(line)
            self._stderr_bytes += len(line.encode("utf-8", errors="replace"))
            while self._stderr and self._stderr_bytes > STDERR_CAPTURE_LIMIT:
                removed = self._stderr.popleft()
                self._stderr_bytes -= len(removed.encode("utf-8", errors="replace"))

    def _next_message(self) -> dict[str, Any]:
        item = self._messages.get(timeout=self._remaining())
        if item is None:
            raise RuntimeError(
                f"Ymir closed its protocol stream with exit status {self.process.poll()}"
            )
        if isinstance(item, BaseException):
            raise RuntimeError(f"failed to decode Ymir protocol output: {item}") from item
        return item

    def _wait_ready(self) -> None:
        while True:
            message = self._next_message()
            if message.get("method") == "instance.ready":
                self.notifications.append(message)
                return
            self.notifications.append(message)

    def call(self, method: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
        request_id = self._next_id
        self._next_id += 1
        assert self.process.stdin is not None
        self.process.stdin.write(json.dumps(request(method, request_id, params)) + "\n")
        self.process.stdin.flush()
        while True:
            message = self._next_message()
            if message.get("id") != request_id:
                self.notifications.append(message)
                continue
            if "error" in message:
                raise RuntimeError(f"Ymir request {request_id} failed: {message['error']}")
            return message.get("result", {})

    def shutdown(self) -> None:
        if self.process.poll() is None:
            try:
                self.call("instance.shutdown")
            except (RuntimeError, TimeoutError):
                self.process.terminate()
        try:
            self.process.wait(timeout=min(10.0, max(0.1, self._remaining())))
        except (subprocess.TimeoutExpired, TimeoutError):
            self.process.kill()
            self.process.wait(timeout=5)

    def abort(self) -> None:
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait(timeout=5)

    @property
    def stderr(self) -> str:
        return "".join(self._stderr)


def parse_view_manifest(path: Path) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    manifest = json.loads(path.read_text(encoding="utf-8"))
    if manifest.get("schema") != "sm64-saturn-bob-renderer-views":
        raise ValueError("view manifest has an unsupported schema")
    views = manifest.get("viewpoints")
    if not isinstance(views, list) or not views:
        raise ValueError("view manifest must contain viewpoints")
    previous = -1
    for view in views:
        tick = view.get("route_tick")
        if not isinstance(tick, int) or tick <= previous:
            raise ValueError("viewpoint route ticks must be positive and strictly increasing")
        previous = tick
    return manifest, views


def peek_route(client: YmirClient, address: int) -> tuple[dict[str, Any] | None, list[int]]:
    window = client.call("mem.peek", {"address": address, "count": ROUTE_PROBE_BYTES})
    data = window.get("data", [])
    if not isinstance(data, list) or len(data) < ROUTE_PROBE_BYTES:
        raise RuntimeError(
            f"route probe returned fewer than {ROUTE_PROBE_BYTES} bytes"
        )
    magic = int.from_bytes(bytes(data[:4]), byteorder="big")
    version = int.from_bytes(bytes(data[4:8]), byteorder="big")
    if magic != ROUTE_MAGIC or version != ROUTE_VERSION:
        return None, data
    return decode_probe({"probe_window": {"data": data}}), data


def screenshot_identity(result: dict[str, Any], path: Path) -> dict[str, Any]:
    if result.get("mime_type") != "image/png":
        raise RuntimeError("Ymir video.capture did not return a PNG")
    png = base64.b64decode(result["data"], validate=True)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)
    return {
        "path": str(path),
        "bytes": len(png),
        "sha256": hashlib.sha256(png).hexdigest(),
        "sequence": result["sequence"],
        "width": result["width"],
        "height": result["height"],
        "frame_hash": result["hash"],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ymir", type=Path, required=True)
    parser.add_argument("--ipl", type=Path, required=True)
    parser.add_argument("--game", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--profile-address", type=lambda value: int(value, 0), required=True)
    parser.add_argument("--route-address", type=lambda value: int(value, 0), required=True)
    parser.add_argument("--profile-count", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--screenshot-dir", type=Path, required=True)
    parser.add_argument("--name-suffix", required=True)
    parser.add_argument("--timeout", type=float, default=1700.0)
    parser.add_argument("--degradation-view-radius", type=int, required=True)
    parser.add_argument("--degradation-poly-tier", type=int, required=True)
    parser.add_argument("--build-slave-render", type=int, choices=(0, 1), required=True)
    parser.add_argument("--build-hot-promotion", type=int, choices=(0, 1), required=True)
    args = parser.parse_args()

    args.ymir = args.ymir.resolve()
    args.ipl = args.ipl.resolve()
    args.game = args.game.resolve()
    args.manifest = args.manifest.resolve()
    args.output = args.output.resolve()
    args.screenshot_dir = args.screenshot_dir.resolve()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.profile_count <= 0:
        parser.error("--profile-count must be positive")
    stale = stale_game_image(args.game)
    if stale is not None:
        elf, game_mtime, elf_mtime = stale
        parser.error(
            "game image is older than its build ELF "
            f"({args.game.name} {game_mtime:.3f} < {elf.name} {elf_mtime:.3f})"
        )
    try:
        manifest, viewpoints = parse_view_manifest(args.manifest)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(str(error))

    wall_start = time.perf_counter()
    emulated_frames = 0
    client: YmirClient | None = None
    captures: list[dict[str, Any]] = []
    try:
        client = YmirClient(args.ymir, args.ipl, args.game, args.timeout)

        def run_for(frames: int) -> None:
            nonlocal emulated_frames
            client.call("exec.run_for", {"frames": frames})
            emulated_frames += frames

        # The proven USA-BIOS boot macro from capture_hwtest.py, including
        # the final all-buttons release before game execution.
        run_for(120)
        client.call("input.pulse", {"buttons": 0x4000})
        run_for(30)
        client.call("input.pulse", {"buttons": 0x0400})
        run_for(1200)
        for _ in range(5):
            client.call("input.pulse", {"buttons": 0x4000})
            run_for(30)
        client.call("input.pulse", {"buttons": 0xFFF8})

        route_probe: dict[str, Any] | None = None
        route_data: list[int] = []
        while route_probe is None:
            run_for(30)
            route_probe, route_data = peek_route(client, args.route_address)

        for viewpoint in viewpoints:
            target = int(viewpoint["route_tick"])
            while int(route_probe["replay_ticks"]) < target:
                gap = target - int(route_probe["replay_ticks"])
                run_for(1 if gap <= 16 else 30)
                route_probe, route_data = peek_route(client, args.route_address)
                if route_probe is None:
                    raise RuntimeError("route checkpoint disappeared after becoming valid")
                if int(route_probe["replay_ticks"]) > target:
                    raise RuntimeError(
                        f"route stepped past requested tick {target} to "
                        f"{route_probe['replay_ticks']}"
                    )

            profile_window = client.call(
                "mem.peek",
                {"address": args.profile_address, "count": args.profile_count},
            )
            capture_result = client.call("video.capture")
            image_path = (
                args.screenshot_dir
                / f"ymir-bob-pipe8-{viewpoint['id']}-tick{target}-{args.name_suffix}.png"
            )
            captures.append(
                {
                    **viewpoint,
                    "emulated_frame": emulated_frames,
                    "route_probe": route_probe,
                    "route_window": {
                        "address": args.route_address,
                        "data": route_data,
                        "target": "sh2.master",
                    },
                    "profile_window": profile_window,
                    "screenshot": screenshot_identity(capture_result, image_path),
                }
            )
        client.shutdown()
    except BaseException:
        if client is not None:
            client.abort()
        raise

    wall_seconds = time.perf_counter() - wall_start
    elf = newest_sibling_elf(args.game)
    report = {
        "evidence_kind": "ymir-emulator-route-views",
        "ymir": str(args.ymir),
        "ipl": str(args.ipl),
        "game": str(args.game),
        "artifacts": {
            "game": artifact_identity(args.game),
            "elf": artifact_identity(elf),
        },
        "view_manifest": str(args.manifest),
        "route_version": manifest.get("route_version"),
        "capture_contract": {
            **manifest.get("capture_contract", {}),
            "exact_route_tick": True,
            "internal_video_capture": True,
        },
        "profile_address": args.profile_address,
        "route_address": args.route_address,
        "profile_count": args.profile_count,
        "dram_cart": True,
        "bios_input": True,
        "degradation": {
            "view_radius": args.degradation_view_radius,
            "poly_tier": args.degradation_poly_tier,
        },
        "build_profile": {
            "demo_path": 1,
            "slave_render": args.build_slave_render,
            "hot_promotion": args.build_hot_promotion,
            "renderer_pipeline": 8,
        },
        "emulation_timing": emulation_timing(emulated_frames, wall_seconds),
        "views": captures,
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
        "diagnostics": {
            "stderr": client.stderr if client else "",
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    for view in captures:
        paired_path = args.output.parent / f"{Path(view['screenshot']['path']).stem}.json"
        view["paired_report"] = str(paired_path)
        paired_report = {
            **report,
            "paired_from": str(args.output),
            "views": [view],
        }
        paired_path.write_text(
            json.dumps(paired_report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
