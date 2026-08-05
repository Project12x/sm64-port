#!/usr/bin/env python3
"""Capture native VDP performance counters from a visible desktop Ymir window."""

from __future__ import annotations

import argparse
import ctypes
import json
import re
import statistics
import subprocess
import sys
import time
from collections.abc import Callable
from datetime import UTC, datetime
from pathlib import Path
from typing import Any
from ctypes import wintypes

import launch_ymir_desktop as desktop


_PERFORMANCE_TITLE = re.compile(
    r"\| Speed: (?P<speed>\d+)% / (?P<target>\d+)%"
    r"(?: \(alt\))? \| VDP2: (?P<vdp2>\d+) fps"
    r" \| VDP1: (?P<vdp1>\d+) fps, (?P<draws>\d+) draws"
    r" \| GUI: (?P<gui>\d+) fps"
)


class DesktopCaptureError(RuntimeError):
    def __init__(self, message: str, *, stage: str, pid: int | None) -> None:
        super().__init__(message)
        self.stage = stage
        self.pid = pid


def parse_performance_title(title: str) -> dict[str, int] | None:
    """Decode the counters emitted by Ymir's own one-second FPS sampler."""
    match = _PERFORMANCE_TITLE.search(title)
    if match is None:
        return None
    return {
        "speed_percent": int(match.group("speed")),
        "target_speed_percent": int(match.group("target")),
        "vdp2_fps": int(match.group("vdp2")),
        "vdp1_fps": int(match.group("vdp1")),
        "vdp1_draws": int(match.group("draws")),
        "gui_fps": int(match.group("gui")),
    }


def summarize_samples(samples: list[dict[str, Any]]) -> dict[str, int | float]:
    """Summarize running samples, excluding Ymir's startup zero interval."""
    running = [sample for sample in samples if int(sample["vdp2_fps"]) > 0]
    if not running:
        raise ValueError("no running Ymir performance samples")
    vdp2 = [int(sample["vdp2_fps"]) for sample in running]
    vdp1 = [int(sample["vdp1_fps"]) for sample in running]
    draws = [int(sample["vdp1_draws"]) for sample in running]
    return {
        "running_snapshots": len(running),
        "vdp2_fps_median": statistics.median(vdp2),
        "vdp1_fps_median": statistics.median(vdp1),
        "vdp1_fps_min": min(vdp1),
        "vdp1_fps_max": max(vdp1),
        "vdp1_draws_median": statistics.median(draws),
    }


def collect_performance_samples(
    process_id: int,
    *,
    sample_count: int,
    interval_seconds: float,
    title_reader: Callable[[int], str],
    sleeper: Callable[[float], None] = time.sleep,
    clock: Callable[[], datetime] = lambda: datetime.now(UTC),
) -> list[dict[str, Any]]:
    """Read bounded native title updates from one exact desktop process."""
    if sample_count < 1:
        raise ValueError("sample count must be positive")
    if interval_seconds <= 0:
        raise ValueError("sample interval must be positive")
    samples: list[dict[str, Any]] = []
    for sample_index in range(sample_count):
        sleeper(interval_seconds)
        title = title_reader(process_id)
        parsed = parse_performance_title(title)
        if parsed is not None:
            samples.append(
                {
                    "sample_index": sample_index,
                    "observed_utc": clock().isoformat(),
                    "title": title,
                    **parsed,
                }
            )
    return samples


def select_process_title(process_id: int, windows: list[tuple[int, str]]) -> str:
    """Select the sole visible top-level window owned by ``process_id``."""
    titles = [title for pid, title in windows if pid == process_id and title]
    if not titles:
        raise RuntimeError(f"no visible window found for process {process_id}")
    if len(titles) != 1:
        raise RuntimeError(f"multiple visible windows found for process {process_id}")
    return titles[0]


def enumerate_visible_window_titles() -> list[tuple[int, str]]:
    """Return ``(pid, title)`` pairs for visible Win32 top-level windows."""
    if sys.platform != "win32":
        raise RuntimeError("desktop Ymir title capture requires Windows")
    user32 = ctypes.windll.user32
    windows: list[tuple[int, str]] = []
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.IsWindowVisible.argtypes = [wintypes.HWND]
    user32.IsWindowVisible.restype = wintypes.BOOL
    user32.GetWindowTextLengthW.argtypes = [wintypes.HWND]
    user32.GetWindowTextLengthW.restype = ctypes.c_int
    user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user32.GetWindowTextW.restype = ctypes.c_int
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.GetWindowThreadProcessId.restype = wintypes.DWORD
    user32.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    user32.EnumWindows.restype = wintypes.BOOL

    @callback_type
    def visit(window: int, _context: int) -> int:
        if not user32.IsWindowVisible(window):
            return True
        length = user32.GetWindowTextLengthW(window)
        if length <= 0:
            return True
        buffer = ctypes.create_unicode_buffer(length + 1)
        user32.GetWindowTextW(window, buffer, length + 1)
        process_id = wintypes.DWORD()
        user32.GetWindowThreadProcessId(window, ctypes.byref(process_id))
        windows.append((int(process_id.value), buffer.value))
        return True

    if not user32.EnumWindows(visit, 0):
        raise ctypes.WinError()
    return windows


def read_process_window_title(process_id: int) -> str:
    return select_process_title(process_id, enumerate_visible_window_titles())


def launch_and_capture(
    plan: dict[str, Any],
    *,
    warmup_seconds: float,
    sample_count: int,
    interval_seconds: float,
    report_output: Path,
    title_reader: Callable[[int], str] = read_process_window_title,
    sleeper: Callable[[float], None] = time.sleep,
) -> dict[str, Any]:
    """Launch exact desktop Ymir and capture its bounded native FPS samples."""
    if warmup_seconds < 0:
        raise ValueError("warmup seconds must be non-negative")
    stdout_log = report_output.with_suffix(".stdout.log").resolve()
    stderr_log = report_output.with_suffix(".stderr.log").resolve()
    stdout_log.parent.mkdir(parents=True, exist_ok=True)
    started = datetime.now(UTC)
    with stdout_log.open("w", encoding="utf-8", errors="replace") as stdout, stderr_log.open(
        "w", encoding="utf-8", errors="replace"
    ) as stderr:
        process = subprocess.Popen(
            plan["command"],
            cwd=plan["working_directory"],
            stdin=subprocess.DEVNULL,
            stdout=stdout,
            stderr=stderr,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        sleeper(warmup_seconds)
        if process.poll() is not None:
            raise DesktopCaptureError(
                f"desktop Ymir exited during warmup with code {process.returncode}",
                stage="warmup",
                pid=process.pid,
            )

        def live_title_reader(process_id: int) -> str:
            if process_id != process.pid:
                raise DesktopCaptureError(
                    "title reader was given the wrong process", stage="sampling", pid=process.pid
                )
            if process.poll() is not None:
                raise DesktopCaptureError(
                    f"desktop Ymir exited during capture with code {process.returncode}",
                    stage="sampling",
                    pid=process.pid,
                )
            return title_reader(process_id)

        try:
            samples = collect_performance_samples(
                process.pid,
                sample_count=sample_count,
                interval_seconds=interval_seconds,
                title_reader=live_title_reader,
                sleeper=sleeper,
            )
            summary = summarize_samples(samples)
        except DesktopCaptureError:
            raise
        except Exception as error:
            raise DesktopCaptureError(str(error), stage="sampling", pid=process.pid) from error
        exit_code = process.poll()
    return {
        "pid": process.pid,
        "started_utc": started.isoformat(),
        "warmup_seconds": warmup_seconds,
        "sample_count_requested": sample_count,
        "sample_interval_seconds": interval_seconds,
        "exit_code": exit_code,
        "alive_after_capture": exit_code is None,
        "stdout_log": str(stdout_log),
        "stderr_log": str(stderr_log),
        "samples": samples,
        "summary": summary,
    }


def capture_to_report(
    plan: dict[str, Any],
    output: Path,
    *,
    warmup_seconds: float,
    sample_count: int,
    interval_seconds: float,
    capture_runner: Callable[..., dict[str, Any]] = launch_and_capture,
) -> dict[str, Any]:
    """Persist both successful and failed capture attempts as structured JSON."""
    report: dict[str, Any] = {
        "created_utc": datetime.now(UTC).isoformat(),
        "status": "running",
        "plan": plan,
    }
    desktop.write_report(report, output)
    try:
        execution = capture_runner(
            plan,
            warmup_seconds=warmup_seconds,
            sample_count=sample_count,
            interval_seconds=interval_seconds,
            report_output=output,
        )
    except Exception as error:
        report["status"] = "failed"
        report["failure"] = {
            "type": type(error).__name__,
            "message": str(error),
            "stage": getattr(error, "stage", "capture"),
            "pid": getattr(error, "pid", None),
            "stdout_log": str(output.with_suffix(".stdout.log").resolve()),
            "stderr_log": str(output.with_suffix(".stderr.log").resolve()),
        }
        desktop.write_report(report, output)
        raise
    report["status"] = "complete"
    report["execution"] = execution
    desktop.write_report(report, output)
    return execution


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cue", type=Path, required=True, help="exact staged CUE to launch")
    parser.add_argument("--ymir", type=Path, default=desktop.DEFAULT_YMIR)
    parser.add_argument("--profile", type=Path, default=desktop.DEFAULT_PROFILE)
    parser.add_argument("--warmup-seconds", type=float, default=35.0)
    parser.add_argument("--sample-count", type=int, default=10)
    parser.add_argument("--sample-interval-seconds", type=float, default=1.1)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    plan = desktop.build_launch_plan(args.ymir, args.profile, args.cue)
    timestamp = datetime.now(UTC).strftime("%Y%m%dT%H%M%SZ")
    output = args.output or (
        desktop.WORKTREE_ROOT
        / "build"
        / "saturn"
        / "ymir-desktop-launches"
        / f"ymir-desktop-performance-{timestamp}.json"
    )
    execution = capture_to_report(
        plan,
        output,
        warmup_seconds=args.warmup_seconds,
        sample_count=args.sample_count,
        interval_seconds=args.sample_interval_seconds,
    )
    print(json.dumps(execution["summary"], sort_keys=True))
    print(output.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
