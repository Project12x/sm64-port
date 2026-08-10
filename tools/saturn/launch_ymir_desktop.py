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
import threading
import time
from datetime import UTC, datetime
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable

from release_manifest import ReleaseManifestVerification, verify_release_manifest


WORKTREE_ROOT = Path(__file__).resolve().parents[2]
PROJECT_ROOT = WORKTREE_ROOT.parent.parent
DEFAULT_PROFILE = PROJECT_ROOT / ".ymir-profile"
DEFAULT_YMIR = (
    PROJECT_ROOT.parent / "ymir-agent" / "build-agent" / "apps" / "ymir-sdl3"
    / "Release" / "ymir-sdl3.exe"
)


@dataclass(frozen=True)
class DesktopReleaseBinding:
    cue: Path
    manifest_sha256: str
    verification: ReleaseManifestVerification

    def __iter__(self):
        yield self.cue
        yield self.manifest_sha256


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
        "command": [str(executable), "--profile", str(profile), "--disc", str(cue)],
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


def resolve_release_cue(
    release_manifest: Path, cue: Path | None
) -> DesktopReleaseBinding:
    """Select only the CUE whose bytes were verified by the release manifest."""
    verified = verify_release_manifest(release_manifest)
    source = verified.outputs["cue"]
    if cue is not None and cue.resolve() != source:
        verified.close()
        raise ValueError("separate CUE differs from verified release manifest")
    selected = getattr(verified, "snapshot_outputs", verified.outputs)["cue"]
    return DesktopReleaseBinding(selected, verified.manifest_sha256, verified)


def _cleanup_snapshot_after_exit(
    process: subprocess.Popen[str], cleanup: Callable[[], None]
) -> None:
    try:
        process.wait()
    finally:
        cleanup()


def launch_and_monitor(
    plan: dict[str, Any],
    monitor_seconds: float,
    report_output: Path,
    *,
    snapshot_cleanup: Callable[[], None] | None = None,
    snapshot_root: Path | None = None,
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
    result = {
        "requested": True,
        "pid": process.pid,
        "started_utc": started.isoformat(),
        "monitor_seconds": monitor_seconds,
        "exit_code": exit_code,
        "alive_after_monitor": exit_code is None,
        "stdout_log": str(stdout_log),
        "stderr_log": str(stderr_log),
    }
    if snapshot_cleanup is not None:
        if exit_code is None:
            watcher = threading.Thread(
                target=_cleanup_snapshot_after_exit,
                args=(process, snapshot_cleanup),
                name=f"ymir-snapshot-cleanup-{process.pid}",
                daemon=False,
            )
            watcher.start()
            state = "scheduled-after-process-exit"
        else:
            snapshot_cleanup()
            state = "removed-after-process-exit"
        result["verified_snapshot"] = {
            "root": str(snapshot_root.resolve()) if snapshot_root is not None else None,
            "cleanup_state": state,
            "owner_pid": process.pid,
        }
    return result


def write_report(report: dict[str, Any], output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--release-manifest", type=Path, required=True)
    parser.add_argument("--cue", type=Path, help="optional matching staged CUE")
    parser.add_argument("--ymir", type=Path, default=DEFAULT_YMIR)
    parser.add_argument("--profile", type=Path, default=DEFAULT_PROFILE)
    parser.add_argument("--launch", action="store_true", help="start the visible GUI after preflight")
    parser.add_argument("--monitor-seconds", type=float, default=20.0)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)

    release_binding = resolve_release_cue(args.release_manifest, args.cue)
    snapshot_transferred = False
    try:
        cue, manifest_sha256 = release_binding
        plan = build_launch_plan(args.ymir, args.profile, cue)
        timestamp = datetime.now(UTC).strftime("%Y%m%dT%H%M%SZ")
        output = args.output or (WORKTREE_ROOT / "build" / "saturn" / "ymir-desktop-launches" / f"ymir-desktop-launch-{timestamp}.json")
        report: dict[str, Any] = {
            "created_utc": datetime.now(UTC).isoformat(),
            "release_manifest_sha256": manifest_sha256,
            "plan": plan,
            "execution": {"requested": False, "reason": "dry-run; pass --launch to start GUI"},
        }
        if args.launch:
            report["execution"] = launch_and_monitor(
                plan,
                args.monitor_seconds,
                output,
                snapshot_cleanup=release_binding.verification.close,
                snapshot_root=release_binding.verification.snapshot_root,
            )
            snapshot_transferred = (
                report["execution"]["verified_snapshot"]["cleanup_state"]
                == "scheduled-after-process-exit"
            )
        write_report(report, output)
        print(output.resolve())
        return 0
    finally:
        if not snapshot_transferred:
            release_binding.verification.close()


if __name__ == "__main__":
    raise SystemExit(main())
