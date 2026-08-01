#!/usr/bin/env python3
"""Capture a raw replay-only SCC1 camera window from one paused Ymir target."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import subprocess
import time
from dataclasses import asdict
from pathlib import Path
from typing import Any

from camera_idle_contract import (
    REPLAY_TICKS, ROUTE_ID, SCC1_BYTES, SCC1_MAGIC, SBR4_MAGIC, SBR4_VERSION,
    ROLE_IDS, decode_scc1, validate_scc1,
)
from capture_hwtest import artifact_identity, emulation_timing, newest_sibling_elf
from capture_route_views import YmirClient
from compare_route_reports import PROBE_BYTES, decode_probe

NM = Path(
    "D:/Code/RetroDev/sm64-saturn-port/work/yaul-install/bin/sh-elf-nm.exe"
)
MAX_PEEK_BYTES = 65536
SBR4_BYTES = PROBE_BYTES
SCAR_BYTES = 28
SYMBOL_NAMES = (
    "sourceboot_camera_idle_capture",
    "sourceboot_route_checkpoint",
    "g_sm64_saturn_source_cart_probe",
    "sm64_saturn_camera_variant_marker",
    "sm64_saturn_camera_route_marker",
)
C_SYMBOL_NAMES = frozenset(SYMBOL_NAMES[:3])


def _tool_environment() -> dict[str, str]:
    environment = os.environ.copy()
    environment["PATH"] = r"C:\msys64\usr\bin" + os.pathsep + environment.get("PATH", "")
    return environment


def resolve_symbols(elf: Path, *, nm: Path = NM) -> dict[str, int]:
    completed = subprocess.run(
        [str(nm), "-g", "--defined-only", str(elf)],
        check=False, capture_output=True, text=True, env=_tool_environment(),
    )
    if completed.returncode != 0:
        raise ValueError(f"sh-elf-nm failed for {elf}: {completed.stderr.strip()}")
    wanted = set(SYMBOL_NAMES)
    resolved: dict[str, int] = {}
    for line in completed.stdout.splitlines():
        parts = line.split()
        name = parts[-1] if len(parts) >= 3 else ""
        if name.startswith("_") and name[1:] in C_SYMBOL_NAMES:
            name = name[1:]
        if name in wanted:
            try:
                resolved[name] = int(parts[0], 16)
            except ValueError:
                continue
    missing = wanted - resolved.keys()
    if missing:
        raise ValueError(f"ELF lacks required camera symbols: {', '.join(sorted(missing))}")
    return resolved


def read_exact(client: Any, address: int, count: int,
               *, chunk_size: int = MAX_PEEK_BYTES) -> bytes:
    if count <= 0 or not 1 <= chunk_size <= MAX_PEEK_BYTES:
        raise ValueError("invalid bounded target read")
    chunks: list[bytes] = []
    remaining = count
    cursor = address
    while remaining:
        requested = min(remaining, chunk_size)
        result = client.call("mem.peek", {"address": cursor, "count": requested})
        data = result.get("data")
        if not isinstance(data, list) or len(data) != requested:
            raise ValueError(
                f"target read at 0x{cursor:08X} returned "
                f"{len(data) if isinstance(data, list) else 'no'} bytes, "
                f"expected {requested}"
            )
        if any(not isinstance(byte, int) or not 0 <= byte <= 255 for byte in data):
            raise ValueError("target read contains a non-byte value")
        chunk = bytes(data)
        if chunks and chunk == chunks[-1]:
            raise ValueError("target returned a repeated memory chunk")
        chunks.append(chunk)
        cursor += requested
        remaining -= requested
    raw = b"".join(chunks)
    if len(raw) != count:
        raise ValueError(f"target read assembled {len(raw)} bytes, expected {count}")
    return raw


def _identity(path: Path, label: str) -> dict[str, Any]:
    identity = artifact_identity(path)
    if identity is None:
        raise ValueError(f"capture {label} artifact is missing: {path}")
    return identity


def _route_manifest(path: Path) -> tuple[dict[str, Any], str]:
    raw = path.read_bytes()
    manifest = json.loads(raw)
    if manifest.get("route_version") != "bob-default-camera-v1":
        raise ValueError("capture route must be bob-default-camera-v1")
    if manifest.get("route_id") != ROUTE_ID:
        raise ValueError("capture route manifest ID is not 2")
    if manifest.get("checkpoint_tick") != REPLAY_TICKS:
        raise ValueError("capture route checkpoint is not 2000")
    if manifest.get("simulation_ticks") != REPLAY_TICKS:
        raise ValueError("capture route simulation length is not 2000")
    return manifest, hashlib.sha256(raw).hexdigest()


def _decode_scar(raw: bytes, source_identity: dict[str, Any]) -> dict[str, Any]:
    if len(raw) != SCAR_BYTES:
        raise ValueError("SCAR raw probe must be exactly 28 bytes")
    magic, stage, expected, copied, cart_id, cart_size, status = struct.unpack(">7I", raw)
    if magic != 0x53434152:
        raise ValueError("SCAR magic is absent")
    if stage != 5 or status != 0:
        raise ValueError("SCAR probe is not READY/OK")
    if expected != source_identity["size"] or copied != source_identity["size"]:
        raise ValueError("SCAR expected/copied sizes do not match SOURCE.DAT")
    return {
        "magic": "SCAR", "stage": "READY", "status": "OK",
        "expected_size": expected, "copied_size": copied,
        "cart_id": cart_id, "cart_size": cart_size,
    }


def _decoded_scc1(capture: Any) -> dict[str, Any]:
    return {
        "header": list(capture.header),
        "samples": [asdict(sample) for sample in capture.samples],
    }


def capture_target(
    *, client: Any, game: Path, route_manifest: Path, capture_role: str,
    expected_idle_start_tick: int, discovery: bool, source_data: Path | None,
    frames: int,
) -> tuple[dict[str, Any], dict[str, Any] | None]:
    started = time.perf_counter()
    try:
        elf = newest_sibling_elf(game)
        if elf is None:
            raise ValueError(f"no sibling ELF for {game}")
        image = game.with_suffix(".iso")
        artifacts = {
            "game": _identity(game, "CUE"),
            "image": _identity(image, "ISO"),
            "elf": _identity(elf, "ELF"),
        }
        manifest, route_digest = _route_manifest(route_manifest)
        symbols = resolve_symbols(elf)
        try:
            expected_variant = ROLE_IDS[capture_role]
        except KeyError:
            raise ValueError(f"unsupported capture role {capture_role!r}")
        if symbols["sm64_saturn_camera_variant_marker"] != expected_variant:
            raise ValueError("ELF camera variant marker disagrees with capture role")
        if symbols["sm64_saturn_camera_route_marker"] != 1:
            raise ValueError("ELF camera route marker is not route 1")

        scc_raw = read_exact(
            client, symbols["sourceboot_camera_idle_capture"], SCC1_BYTES
        )
        if len(scc_raw) != SCC1_BYTES:
            raise ValueError("SCC1 raw byte count drift")
        route_raw = read_exact(
            client, symbols["sourceboot_route_checkpoint"], SBR4_BYTES
        )
        if len(route_raw) != PROBE_BYTES or PROBE_BYTES != SBR4_BYTES:
            raise ValueError("SBR4 raw byte count drift")

        # Artifact and manifest identities above are intentionally complete
        # before either raw window is decoded.
        scc = decode_scc1(scc_raw)
        route = decode_probe({"probe_window": {"data": list(route_raw)}})
        if route["magic"] != SBR4_MAGIC or route["version"] != SBR4_VERSION:
            raise ValueError("raw SBR4 magic/version is invalid")
        if route["replay_ticks"] != REPLAY_TICKS:
            raise ValueError("raw SBR4 replay tick is not 2000")
        if tuple(scc.header[7:10]) != (
                route["magic"], route["version"], route["replay_ticks"]):
            raise ValueError("SCC1 route anchor disagrees with raw SBR4")
        if scc.header[23] != manifest["route_id"]:
            raise ValueError("SCC1 route ID disagrees with route manifest")
        if discovery:
            observed_idle_start = scc.header[10]
        else:
            observed_idle_start = expected_idle_start_tick
        validate_scc1(
            scc, expected_role=capture_role,
            expected_idle_start_tick=observed_idle_start,
            expected_route_id=ROUTE_ID,
        )
        if not discovery and scc.header[10] != expected_idle_start_tick:
            raise ValueError("fixed SCC1 idle start tick is wrong")

        cart_report = None
        if source_data is not None:
            source_identity = _identity(source_data, "SOURCE.DAT")
            cart_raw = read_exact(
                client, symbols["g_sm64_saturn_source_cart_probe"], SCAR_BYTES
            )
            cart_report = {
                "evidence_kind": "ymir-sourceboot-scar-camera-transport",
                "artifacts": artifacts,
                "source_data": source_identity,
                "raw_probe": {
                    "address": symbols["g_sm64_saturn_source_cart_probe"],
                    "data": list(cart_raw),
                },
                "decoded": _decode_scar(cart_raw, source_identity),
            }

        elapsed = time.perf_counter() - started
        timing_fields = (
            "frame_serial", "sim_frt_ticks_accum", "render_frt_ticks_accum",
            "render_frt_ticks_last", "master_wait_ticks", "slave_busy_ticks",
            "slave_jobs_completed", "slave_timeouts", "camera_ticks_last",
            "camera_ticks_accum", "camera_invocations", "camera_ticks_max",
        )
        report = {
            "evidence_kind": "ymir-sourceboot-scc1-camera-idle",
            "schema_version": 1,
            "capture_role": capture_role,
            "discovery": discovery,
            "expected_idle_start_tick": expected_idle_start_tick,
            "observed_idle_start_tick": scc.header[10],
            "artifacts": artifacts,
            "route_manifest": {
                "path": str(route_manifest),
                "sha256": route_digest,
                "route_version": manifest["route_version"],
                "route_id": manifest["route_id"],
            },
            "absolute_markers": {
                "camera_variant": symbols["sm64_saturn_camera_variant_marker"],
                "camera_route": symbols["sm64_saturn_camera_route_marker"],
            },
            "scc1_window": {
                "address": symbols["sourceboot_camera_idle_capture"],
                "data": list(scc_raw),
                "decoded": _decoded_scc1(scc),
            },
            "route_window": {
                "address": symbols["sourceboot_route_checkpoint"],
                "data": list(route_raw),
                "decoded": route,
            },
            "timing": {name: route[name] for name in timing_fields},
            "emulation_timing": emulation_timing(frames, elapsed),
            "same_role_equality_exclusions": [
                *timing_fields, "wall_clock_seconds", "emulated_vblank_fps",
                "emulation_speed_ratio",
            ],
        }
        client.shutdown()
        return report, cart_report
    except BaseException:
        client.abort()
        raise


def run_boot_macro(client: Any) -> int:
    frames = 0

    def run_for(count: int) -> None:
        nonlocal frames
        client.call("exec.run_for", {"frames": count})
        frames += count

    run_for(120)
    client.call("input.pulse", {"buttons": 0x4000})
    run_for(30)
    client.call("input.pulse", {"buttons": 0x0400})
    run_for(1200)
    for _ in range(5):
        client.call("input.pulse", {"buttons": 0x4000})
        run_for(30)
    client.call("input.pulse", {"buttons": 0xFFF8})
    return frames


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ymir", type=Path, required=True)
    parser.add_argument("--ipl", type=Path, required=True)
    parser.add_argument("--game", type=Path, required=True)
    parser.add_argument("--route-manifest", type=Path, required=True)
    parser.add_argument("--capture-role", choices=tuple(ROLE_IDS), required=True)
    parser.add_argument("--expected-idle-start-tick", type=int, required=True)
    parser.add_argument("--discovery", action="store_true")
    parser.add_argument("--source-data", type=Path)
    parser.add_argument("--cart-proof-output", type=Path)
    parser.add_argument("--timeout", type=float, default=1800.0)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    if (args.source_data is None) != (args.cart_proof_output is None):
        parser.error("--source-data and --cart-proof-output must be supplied together")
    client: YmirClient | None = None
    frames = 0
    try:
        client = YmirClient(
            args.ymir.resolve(), args.ipl.resolve(), args.game.resolve(), args.timeout
        )
        frames = run_boot_macro(client)
        elf = newest_sibling_elf(args.game.resolve())
        if elf is None:
            raise ValueError("capture game has no sibling ELF")
        address = resolve_symbols(elf)["sourceboot_camera_idle_capture"]
        while True:
            magic = int.from_bytes(read_exact(client, address, 4), "big")
            if magic == SCC1_MAGIC:
                break
            client.call("exec.run_for", {"frames": 30})
            frames += 30
        report, cart_report = capture_target(
            client=client, game=args.game.resolve(),
            route_manifest=args.route_manifest.resolve(),
            capture_role=args.capture_role,
            expected_idle_start_tick=args.expected_idle_start_tick,
            discovery=args.discovery,
            source_data=args.source_data.resolve() if args.source_data else None,
            frames=frames,
        )
    except BaseException:
        if client is not None:
            client.abort()
        raise
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                           encoding="utf-8")
    if args.cart_proof_output is not None and cart_report is not None:
        args.cart_proof_output.parent.mkdir(parents=True, exist_ok=True)
        args.cart_proof_output.write_text(
            json.dumps(cart_report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
