#!/usr/bin/env python3
"""Launch the desktop Ymir path with a reproducible, local report.

The 32-Mbit DRAM cart is configured by the project GUI profile.  This helper
does not rewrite that profile and deliberately never substitutes headless
arguments for it.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import time
from datetime import UTC, datetime
from pathlib import Path
from typing import Any


WORKTREE_ROOT = Path(__file__).resolve().parents[2]
PROJECT_ROOT = WORKTREE_ROOT.parent.parent
DEFAULT_PROFILE = PROJECT_ROOT / ".ymir-profile"
DEFAULT_YMIR = (
    PROJECT_ROOT.parent / "ymir-agent" / "build-agent2" / "apps" / "ymir-sdl3"
    / "Release" / "ymir-sdl3.exe"
)


def parse_cue_file_reference(cue: Path) -> Path:
    for line in cue.read_text(encoding="utf-8").splitlines():
        parts = line.strip().split('"')
        if len(parts) >= 3 and parts[0].strip().upper() == "FILE":
            return (cue.parent / parts[1]).resolve()
    raise ValueError(f"CUE has no quoted FILE target: {cue}")


def file_identity(path: Path) -> dict[str, Any]:
    resolved = path.resolve()
    data = resolved.read_bytes()
    stat = resolved.stat()
    return {
        "path": str(resolved),
        "sha256": hashlib.sha256(data).hexdigest(),
        "bytes": len(data),
        "modified_utc": datetime.fromtimestamp(stat.st_mtime, UTC).isoformat(),
    }


def build_launch_plan(executable: Path, profile: Path, cue: Path) -> dict[str, Any]:
    executable = executable.resolve()
    profile = profile.resolve()
    cue = cue.resolve()
    if not executable.is_file():
        raise ValueError(f"desktop Ymir executable does not exist: {executable}")
    if not profile.is_dir():
        raise ValueError(f"project Ymir profile does not exist: {profile}")
    if not cue.is_file():
        raise ValueError(f"CUE does not exist: {cue}")
    iso = parse_cue_file_reference(cue)
    if not iso.is_file():
        raise ValueError(f"CUE referenced ISO does not exist: {iso}")
    return {
        "launcher": "desktop-ymir-profile",
        "command": [str(executable), "-p", str(profile), "-d", str(cue)],
        "working_directory": str(executable.parent),
        "profile": str(profile),
        "ram_cart": "profile-managed-32-mbit-dram",
        "cue": file_identity(cue),
        "iso": file_identity(iso),
        "gui_stdout_limit": (
            "Ymir SDL3 is interactive. The helper captures only output emitted "
            "during its bounded monitor window; after it returns, a still-live "
            "GUI process has no attached report writer for later output or exit."
        ),
    }


def launch_and_monitor(
    plan: dict[str, Any], monitor_seconds: float, report_output: Path
) -> dict[str, Any]:
    if monitor_seconds < 0:
        raise ValueError("monitor seconds must be non-negative")
    started = datetime.now(UTC)
    stdout_log = report_output.with_suffix(".stdout.log").resolve()
    stderr_log = report_output.with_suffix(".stderr.log").resolve()
    stdout_log.parent.mkdir(parents=True, exist_ok=True)
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
        deadline = time.monotonic() + monitor_seconds
        while process.poll() is None and time.monotonic() < deadline:
            time.sleep(0.1)
        exit_code = process.poll()
    return {
        "requested": True,
        "pid": process.pid,
        "started_utc": started.isoformat(),
        "monitor_seconds": monitor_seconds,
        "exit_code": exit_code,
        "alive_after_monitor": exit_code is None,
        "stdout_log": str(stdout_log),
        "stderr_log": str(stderr_log),
    }


def write_report(report: dict[str, Any], output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cue", type=Path, required=True, help="explicit staged CUE")
    parser.add_argument("--ymir", type=Path, default=DEFAULT_YMIR)
    parser.add_argument("--profile", type=Path, default=DEFAULT_PROFILE)
    parser.add_argument("--launch", action="store_true", help="start the visible GUI after preflight")
    parser.add_argument("--monitor-seconds", type=float, default=20.0)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    plan = build_launch_plan(args.ymir, args.profile, args.cue)
    timestamp = datetime.now(UTC).strftime("%Y%m%dT%H%M%SZ")
    output = args.output or (WORKTREE_ROOT / "build" / "saturn" / "ymir-desktop-launches" / f"ymir-desktop-launch-{timestamp}.json")
    report: dict[str, Any] = {
        "created_utc": datetime.now(UTC).isoformat(),
        "plan": plan,
        "execution": {"requested": False, "reason": "dry-run; pass --launch to start GUI"},
    }
    if args.launch:
        report["execution"] = launch_and_monitor(plan, args.monitor_seconds, output)
    write_report(report, output)
    print(output.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
