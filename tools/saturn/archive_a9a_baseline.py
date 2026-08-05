#!/usr/bin/env python3
"""Preserve the accepted A9A ELF/ISO/CUE without rebuilding or replacing it."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import tomllib
from pathlib import Path
from typing import Any, Callable, Mapping, Sequence


EXPECTED_HASHES = {
    "elf": "1905ec8d42ea00ea2c000b5f53dd88f2079ffda8ceb67bcd5879e8e96acfc2e2",
    "iso": "1ccaef4f2a2d379d82879d3e823d84db135fdee1045d69aa8e0a60d150cfaf96",
    "cue": "cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7",
}
EXPECTED_MEASUREMENT_SHA256 = (
    "45685dfb2b013356e0c1e19a7b51fd621cd7a941f37b066d15b01a29bde2fb4b"
)
EXPECTED_TARGET_IDENTITY = {
    "address": 100679680,
    "expected_sha256": "7a6e2339eb6fd0135c5c4ec49d22acf47d2b68df7f5ec926d01d93a66b249f8b",
    "match": True,
    "observed_sha256": "7a6e2339eb6fd0135c5c4ec49d22acf47d2b68df7f5ec926d01d93a66b249f8b",
    "size": 16,
    "startup_identity_attempts": 540,
    "startup_vblanks_waited": 540,
}
EXPECTED_PROFILE_SHA256 = (
    "33a155e765dac9bd2871ca725ed7f444d1fbbb95876d43c955c0b688e6931566"
)
EXPECTED_LAUNCH_SHA256 = (
    "0c8e0d3617a8cec36b13818244980e6e3bdbf7c579851261f2622daea37b53a7"
)
REQUIRED_COMMITS = ("27cebc7e", "d5f70887", "d7b04d61")
EXPECTED_CONFIG_LABEL = (
    "e2-bob-demo-replay-camroute0-live-input-boot600-atan2v2-camv3-"
    "stage8-r6000-slave1-poly2-hot1-clip1-bsp1-frag0-pipe4"
)
CONFIG_RE = re.compile(
    r"^e2-bob-demo-replay-camroute(?P<camera_route>\d+)-live-input-"
    r"boot(?P<bootstrap_ticks>\d+)-atan2v(?P<atan2_variant>\d+)-"
    r"camv(?P<camera_variant>\d+)-stage(?P<cart_stage_sectors>\d+)-"
    r"r(?P<view_radius>\d+)-slave(?P<slave_render>[01])-"
    r"poly(?P<polygon_tier>\d+)-hot(?P<hot_promotion>[01])-"
    r"clip(?P<near_clip>[01])-bsp(?P<bsp_order>[01])-"
    r"frag(?P<fragment_mode>[01])-pipe(?P<renderer_pipeline>[234])$"
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def canonical_sha256(value: Any) -> str:
    encoded = json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    ).encode("ascii")
    return hashlib.sha256(encoded).hexdigest()


def git_is_ancestor(repo_root: Path, commit: str) -> bool:
    result = subprocess.run(
        ["git", "merge-base", "--is-ancestor", commit, "HEAD"],
        cwd=repo_root, check=False, capture_output=True, text=True,
    )
    return result.returncode == 0


def _read_json(path: Path, evidence_name: str) -> dict[str, Any]:
    if not path.is_file():
        raise ValueError(f"{evidence_name} evidence is not a file: {path}")
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"{evidence_name} evidence is invalid: {path}") from error
    if not isinstance(value, dict):
        raise ValueError(f"{evidence_name} evidence must be a JSON object")
    return value


def _validate_capture(
    report: Mapping[str, Any], *, expected_measurement_sha256: str,
    expected_target_identity: Mapping[str, Any],
) -> tuple[dict[str, Path], Mapping[str, Any]]:
    if (report.get("schema") != "sm64-saturn-sourceboot-throughput-v1"
            or report.get("evidence_kind") != "ymir-sourceboot-queue-throughput"
            or report.get("status") != "complete"):
        raise ValueError("capture evidence is missing its accepted schema/status")
    target = report.get("target_identity")
    if not isinstance(target, Mapping) or dict(target) != dict(expected_target_identity):
        raise ValueError(
            "capture target identity evidence differs from the exact accepted record"
        )
    try:
        measurement = report["observation"]["measurement"]
    except (KeyError, TypeError) as error:
        raise ValueError("capture evidence is missing the measured FPS mean") from error
    if (not isinstance(measurement, Mapping)
            or canonical_sha256(measurement) != expected_measurement_sha256):
        raise ValueError("cadence evidence differs from the exact accepted measurement")
    artifacts = report.get("artifacts")
    if not isinstance(artifacts, Mapping):
        raise ValueError("capture evidence is missing artifacts")
    paths: dict[str, Path] = {}
    for kind in ("elf", "iso", "cue"):
        descriptor = artifacts.get(kind)
        if not isinstance(descriptor, Mapping) or not isinstance(descriptor.get("path"), str):
            raise ValueError(f"capture evidence is missing {kind} path")
        path = Path(descriptor["path"])
        if not path.is_file():
            raise ValueError(f"capture evidence {kind} is not a file: {path}")
        actual = sha256(path)
        if descriptor.get("sha256") != actual:
            raise ValueError(f"capture evidence {kind} SHA-256 is stale")
        paths[kind] = path
    return paths, measurement


def _validate_config(paths: Mapping[str, Path]) -> dict[str, Any]:
    parents = {paths["elf"].parents[1], paths["iso"].parent, paths["cue"].parent}
    if len(parents) != 1:
        raise ValueError("config evidence artifact paths do not share one output directory")
    label = next(iter(parents)).name
    match = CONFIG_RE.fullmatch(label)
    if match is None or label != EXPECTED_CONFIG_LABEL:
        raise ValueError(f"config evidence output label is missing or unexpected: {label}")
    values = {name: int(value) for name, value in match.groupdict().items()}
    return {"output_label": label, **values, "route_replay_mode": 1,
            "live_input_mode": 1}


def _validate_profile(
    profile: Mapping[str, Any], paths: Mapping[str, Path],
    hashes: Mapping[str, str], *, expected_profile_sha256: str,
    expected_launch_sha256: str,
) -> dict[str, Any]:
    if canonical_sha256(profile) != expected_launch_sha256:
        raise ValueError("profile launch evidence differs from the exact accepted report")
    plan = profile.get("plan")
    if not isinstance(plan, Mapping):
        raise ValueError("profile evidence is missing its launch plan")
    if plan.get("launcher") != "desktop-ymir-profile":
        raise ValueError("profile evidence launcher is missing")
    profile_path = plan.get("profile")
    if not isinstance(profile_path, str) or not profile_path.endswith(".ymir-profile"):
        raise ValueError("profile evidence profile path is missing")
    if plan.get("ram_cart") != "profile-managed-32-mbit-dram":
        raise ValueError("profile evidence 32-Mbit cart binding is missing")
    for kind in ("cue", "iso"):
        descriptor = plan.get(kind)
        if (not isinstance(descriptor, Mapping)
                or descriptor.get("sha256") != hashes[kind]
                or Path(str(descriptor.get("path"))) != paths[kind]):
            raise ValueError(f"profile evidence {kind} identity does not match capture")
    profile_dir = Path(profile_path)
    config = profile_dir / "Ymir.toml"
    if not config.is_file() or sha256(config) != expected_profile_sha256:
        raise ValueError("profile evidence Ymir.toml content/hash does not match")
    try:
        parsed = tomllib.loads(config.read_text(encoding="utf-8"))
        cartridge = parsed["Cartridge"]
        dram = cartridge["DRAM"]
    except (OSError, UnicodeDecodeError, tomllib.TOMLDecodeError,
            KeyError, TypeError) as error:
        raise ValueError("profile evidence Ymir.toml cartridge content is invalid") from error
    if cartridge.get("Type") != "DRAM" or dram.get("Capacity") != "32Mbit":
        raise ValueError("profile evidence does not configure a 32-Mbit DRAM cart")

    execution = profile.get("execution")
    if not isinstance(execution, Mapping):
        raise ValueError("launch evidence is missing execution results")
    if (execution.get("requested") is not True
            or execution.get("alive_after_monitor") is not True
            or execution.get("exit_code") is not None
            or not isinstance(execution.get("monitor_seconds"), (int, float))
            or isinstance(execution.get("monitor_seconds"), bool)
            or execution["monitor_seconds"] <= 0
            or type(execution.get("pid")) is not int or execution["pid"] <= 0
            or not isinstance(execution.get("started_utc"), str)
            or not execution["started_utc"]):
        raise ValueError("launch evidence does not prove a successful monitored launch")
    command = plan.get("command")
    expected_tail = ["--profile", profile_path, "--disc", str(paths["cue"])]
    if (not isinstance(command, list) or len(command) != 5
            or not isinstance(command[0], str) or not Path(command[0]).is_file()
            or command[1:] != expected_tail):
        raise ValueError("launch evidence command does not match profile and CUE")
    logs: dict[str, dict[str, Any]] = {}
    for stream in ("stdout", "stderr"):
        log_value = execution.get(f"{stream}_log")
        if not isinstance(log_value, str) or not Path(log_value).is_file():
            raise ValueError(f"launch evidence is missing {stream} log")
        log_path = Path(log_value)
        logs[stream] = {
            "path": str(log_path), "sha256": sha256(log_path),
            "size": log_path.stat().st_size,
        }
    return {
        "launcher": plan["launcher"], "profile": profile_path,
        "profile_config": str(config),
        "profile_config_sha256": expected_profile_sha256,
        "cartridge_type": cartridge["Type"],
        "ram_cart": plan["ram_cart"],
        "dram_capacity": dram["Capacity"],
        "launch_report_sha256": expected_launch_sha256,
        "execution": {
            "requested": True, "alive_after_monitor": True,
            "exit_code": None, "monitor_seconds": execution["monitor_seconds"],
            "pid": execution["pid"], "started_utc": execution["started_utc"],
            "logs": logs,
        },
    }


def _cue_iso_name(cue: Path) -> str:
    try:
        text = cue.read_text(encoding="ascii")
    except (OSError, UnicodeDecodeError) as error:
        raise ValueError("CUE is not valid ASCII") from error
    names = re.findall(r'^FILE\s+"([^"]+)"\s+BINARY\s*$', text,
                       flags=re.MULTILINE | re.IGNORECASE)
    if len(names) != 1:
        raise ValueError("CUE must name exactly one ISO")
    return names[0]


def archive_baseline(
    *, capture_report: Path, profile_report: Path, archive_dir: Path,
    manifest_path: Path, repo_root: Path,
    expected_hashes: Mapping[str, str] = EXPECTED_HASHES,
    required_commits: Sequence[str] = REQUIRED_COMMITS,
    ancestry_check: Callable[[Path, str], bool] = git_is_ancestor,
    expected_measurement_sha256: str = EXPECTED_MEASUREMENT_SHA256,
    expected_target_identity: Mapping[str, Any] = EXPECTED_TARGET_IDENTITY,
    expected_profile_sha256: str = EXPECTED_PROFILE_SHA256,
    expected_launch_sha256: str = EXPECTED_LAUNCH_SHA256,
) -> dict[str, Any]:
    """Validate every evidence edge before copying any accepted artifact."""
    capture = _read_json(capture_report, "capture")
    profile = _read_json(profile_report, "profile")
    paths, measurement = _validate_capture(
        capture,
        expected_measurement_sha256=expected_measurement_sha256,
        expected_target_identity=expected_target_identity,
    )
    config_evidence = _validate_config(paths)
    actual_hashes = {kind: sha256(path) for kind, path in paths.items()}
    for kind in ("elf", "iso", "cue"):
        if actual_hashes[kind] != expected_hashes.get(kind):
            raise ValueError(
                f"{kind} SHA-256 differs from accepted baseline: "
                f"{actual_hashes[kind]}"
            )
    if _cue_iso_name(paths["cue"]) != paths["iso"].name:
        raise ValueError("CUE names an ISO different from the accepted ISO")
    profile_evidence = _validate_profile(
        profile, paths, actual_hashes,
        expected_profile_sha256=expected_profile_sha256,
        expected_launch_sha256=expected_launch_sha256,
    )
    ancestry: dict[str, dict[str, Any]] = {}
    for commit in required_commits:
        if not ancestry_check(repo_root, commit):
            raise ValueError(f"preserved-commit ancestry check failed for {commit}")
        ancestry[commit] = {"is_ancestor_of_head": True}

    destinations = {
        kind: archive_dir / paths[kind].name for kind in ("elf", "iso", "cue")
    }
    for kind, destination in destinations.items():
        if destination.exists() and (
                not destination.is_file() or sha256(destination) != actual_hashes[kind]):
            raise ValueError(f"refuse to overwrite different archived {kind}: {destination}")

    manifest: dict[str, Any] = {
        "schema": "sm64-saturn-a9a-baseline-v1",
        "status": "immutable-historical-rollback",
        "capture_evidence": {
            "report_path": str(capture_report),
            "schema": capture["schema"],
            "target_identity": capture["target_identity"],
            "target_identity_sha256": canonical_sha256(capture["target_identity"]),
            "measurement_sha256": canonical_sha256(measurement),
        },
        "profile_evidence": profile_evidence,
        "config_evidence": config_evidence,
        "measurement": {
            name: measurement[name]
            for name in (
                "guest_fps_1pct_low", "guest_fps_mean", "guest_fps_median",
                "interval_count", "nominal_refresh_hz",
                "presentation_event_count", "target_vblank_delta",
            )
        },
        "artifacts": {
            kind: {
                "original_path": str(paths[kind]),
                "archive_path": str(destinations[kind]),
                "sha256": actual_hashes[kind],
                "size": paths[kind].stat().st_size,
            }
            for kind in ("elf", "iso", "cue")
        },
        "ancestry": ancestry,
    }
    encoded = json.dumps(manifest, indent=2, sort_keys=True) + "\n"
    if manifest_path.exists() and manifest_path.read_text(encoding="utf-8") != encoded:
        raise ValueError(f"refuse to overwrite different baseline manifest: {manifest_path}")

    archive_dir.mkdir(parents=True, exist_ok=True)
    for kind, destination in destinations.items():
        if not destination.exists():
            shutil.copy2(paths[kind], destination)
        if sha256(destination) != actual_hashes[kind]:
            raise ValueError(f"archived {kind} failed post-copy SHA-256 verification")

    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(encoded, encoding="utf-8", newline="\n")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture-report", type=Path, required=True)
    parser.add_argument("--profile-report", type=Path, required=True)
    parser.add_argument("--archive-dir", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--repo-root", type=Path, default=Path.cwd())
    args = parser.parse_args()
    archive_baseline(
        capture_report=args.capture_report,
        profile_report=args.profile_report,
        archive_dir=args.archive_dir,
        manifest_path=args.manifest,
        repo_root=args.repo_root,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
