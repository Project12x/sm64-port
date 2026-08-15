#!/usr/bin/env python3
"""Sprint 2 T2.4 -- decompose the pre-notification window.

Reads the diagnostic FRT sub-stage profiler
(``src/port/saturn/runtime/saturn_prenotify_profile.h``, published by a
``SATURN_DIAGNOSTIC_MODE=2`` build into NOLOAD ``.lwram_bss``) over a full
scripted-route headless Ymir run, and reports per-sub-stage mean/max cost in
FRT ticks, SH-2 cycles, and VBlank-equivalents.

This tool measures; it changes nothing on the target and launches no GUI.

Cross-check discipline: the profiler's window total and the cadence rig's
``construction - master_finalize`` VBlank crossings describe the *same*
interval (both are stamped at the NOTIFIED marker), so this capture also
derives ticks-per-VBlank empirically from the two and compares it against
the value implied by libyaul's own NTSC-320 FRT constants.  A large
disagreement means one of the two instruments is lying and the numbers must
not be used.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import time
from pathlib import Path
from typing import Any

from capture_route_views import YmirClient
from capture_sourceboot_throughput import (
    BOOT_TRACE_BYTES,
    CADENCE_TRACE_BYTES,
    build_elf_identity_probe,
    decode_boot_trace,
    decode_cadence_trace,
)
from capture_sourceboot_boot_trace import (
    NM,
    bind_capture_artifacts,
    cpu_cache_through_alias,
    protocol_and_diagnostics,
    resolve_probe_symbol,
    run_bios_handoff,
    wrapped_nm_command,
)
from capture_object_pool_occupancy import (
    STARTUP_IDENTITY_VBLANKS,
    read_smoke_bytes,
    resolve_release_binding,
    wait_for_sealed_target_identity,
)
from capture_sprint2_peaks import (
    MARIO_SNAPSHOT_SYMBOL,
    MARIO_SNAPSHOT_BYTES,
    decode_mario_snapshot,
    resolve_local_symbol,
    wrapped_nm_all_command,
)

ROOT = Path(__file__).resolve().parents[2]

PROFILE_SYMBOL = "g_sm64_saturn_prenotify_profile"
PROFILE_MAGIC = 0x46505246  # 'FPRF'
PROFILE_VERSION = 1
PROFILE_NODES = 16
PROFILE_WORDS = 72
PROFILE_BYTES = PROFILE_WORDS * 4

# Mirrors the enum in saturn_prenotify_profile.h, in order.  Node 0 is the
# window itself: its self time is the unattributed remainder.
NODE_NAMES = (
    "window_residue",
    "snapshot_acquire",
    "actor_pose",
    "bank_open",
    "spatial_admit",
    "work_order",
    "position_set",
    "frame_reset",
    "prepare_mario",
    "actor_closure",
    "mario_ctx",
    "queue_reset",
    "graph_publish",
    "queue_contexts",
    "notify",
    "spare",
)
assert len(NODE_NAMES) == PROFILE_NODES

# FRT internal-clock select (TCR bits 1:0).
FRT_DIVIDERS = {0: 8, 1: 32, 2: 128, 3: 0}

# libyaul's own NTSC 320-mode constants (third_party/libyaul/libyaul/scu/
# bus/cpu/cpu/frt.h): 0x0D1F ticks/ms at phi/8 => 3359 * 8 = 26,872,000
# SH-2 cycles per second in 320-mode.
SH2_CYCLES_PER_SECOND = 26_872_000
# NTSC field rate.
VBLANKS_PER_SECOND = 59.82609

SMOKE_SYMBOLS = {
    "sourceboot_exception_record": 4,
    "sourceboot_boot_trace": BOOT_TRACE_BYTES,
    "sourceboot_cadence_trace": CADENCE_TRACE_BYTES,
}

RUN_FOR_CHUNK_FRAMES = 300
DEFAULT_SAMPLE_INTERVAL_FRAMES = 300
DEFAULT_POST_BIOS_FRAMES = 24000
MIN_POST_BIOS_FRAMES = 7200  # >= two emulated minutes of gameplay


def be_words(raw: bytes) -> list[int]:
    if len(raw) % 4 != 0:
        raise ValueError("word decode requires a multiple of four bytes")
    return [int.from_bytes(raw[i:i + 4], "big") for i in range(0, len(raw), 4)]


def decode_profile(raw: bytes) -> dict[str, Any]:
    """Decode the published record.  Never raises on garbage: pre-handoff
    samples legitimately read RAM the ELF image has not reached yet."""
    if len(raw) != PROFILE_BYTES:
        raise ValueError(f"profile record must be {PROFILE_BYTES} bytes")
    w = be_words(raw)
    nodes_accum = w[10:10 + PROFILE_NODES]
    nodes_max = w[10 + PROFILE_NODES:10 + 2 * PROFILE_NODES]
    nodes_last = w[10 + 2 * PROFILE_NODES:10 + 3 * PROFILE_NODES]
    tail = w[10 + 3 * PROFILE_NODES:]
    record = {
        "magic": w[0],
        "magic_valid": w[0] == PROFILE_MAGIC,
        "version": w[1],
        "sequence_begin": w[2],
        "windows": w[3],
        "frt_tcr": w[4],
        "window_ticks_last": w[5],
        "window_ticks_accum": w[6],
        "window_ticks_max": w[7],
        "max_raw_interval": w[8],
        "faults": w[9],
        "node_ticks_accum": dict(zip(NODE_NAMES, nodes_accum)),
        "node_ticks_max": dict(zip(NODE_NAMES, nodes_max)),
        "node_ticks_last": dict(zip(NODE_NAMES, nodes_last)),
        "retire_events": tail[0],
        "notify_to_retire_accum": tail[1],
        "notify_to_retire_last": tail[2],
        "notify_to_retire_max": tail[3],
        "finalize_events": tail[4],
        "finalize_ticks_accum": tail[5],
        "finalize_ticks_last": tail[6],
        "finalize_ticks_max": tail[7],
        "slave_entries": tail[8],
        "slave_busy_accum": tail[9],
        "slave_busy_last": tail[10],
        "slave_busy_max": tail[11],
        "slave_frt_tcr": tail[12],
        "sequence_end": tail[13],
    }
    record["stable"] = (
        record["magic_valid"]
        and record["sequence_begin"] == record["sequence_end"]
    )
    record["version_valid"] = record["version"] == PROFILE_VERSION
    return record


def divider_from_tcr(tcr_field: int) -> int | None:
    """The target publishes 0x100 | TCR so that 'never written' (0) is
    distinguishable from a legitimately zero TCR."""
    if tcr_field == 0:
        return None
    return FRT_DIVIDERS.get(tcr_field & 0x03)


def resolve_capture_addresses(
    elf: Path, *, nm: Path = NM, run: Any = subprocess.run
) -> dict[str, int]:
    globals_listing = run(
        wrapped_nm_command(elf, nm=nm), check=False, capture_output=True, text=True
    )
    if globals_listing.returncode != 0:
        raise ValueError(
            f"DLL-safe sh-elf-nm wrapper failed for {elf}: "
            f"{globals_listing.stderr.strip()}"
        )
    all_listing = run(
        wrapped_nm_all_command(elf, nm=nm), check=False, capture_output=True, text=True
    )
    if all_listing.returncode != 0:
        raise ValueError(
            f"DLL-safe sh-elf-nm (locals) wrapper failed for {elf}: "
            f"{all_listing.stderr.strip()}"
        )
    addresses = {
        symbol: resolve_probe_symbol(globals_listing.stdout, symbol)
        for symbol in (PROFILE_SYMBOL, *SMOKE_SYMBOLS)
    }
    addresses[MARIO_SNAPSHOT_SYMBOL] = resolve_local_symbol(
        all_listing.stdout, MARIO_SNAPSHOT_SYMBOL
    )
    return addresses


def read_sample(client: YmirClient, addresses: dict[str, int]) -> dict[str, Any]:
    profile_raw = read_smoke_bytes(
        client, cpu_cache_through_alias(addresses[PROFILE_SYMBOL]), PROFILE_BYTES
    )
    mario_raw = read_smoke_bytes(
        client, cpu_cache_through_alias(addresses[MARIO_SNAPSHOT_SYMBOL]),
        MARIO_SNAPSHOT_BYTES,
    )
    smoke_raw = {
        symbol: read_smoke_bytes(
            client, cpu_cache_through_alias(addresses[symbol]), size
        )
        for symbol, size in SMOKE_SYMBOLS.items()
    }
    try:
        cadence: dict[str, Any] = decode_cadence_trace(
            smoke_raw["sourceboot_cadence_trace"]
        )
    except ValueError as error:
        cadence = {"error": str(error)}
    return {
        "profile": decode_profile(profile_raw),
        "mario": decode_mario_snapshot(mario_raw),
        "exception_magic": int.from_bytes(
            smoke_raw["sourceboot_exception_record"], "big"
        ),
        "boot": decode_boot_trace(smoke_raw["sourceboot_boot_trace"]),
        "cadence": cadence,
    }


def summarize(final: dict[str, Any], cadence: dict[str, Any]) -> dict[str, Any]:
    """Turn one terminal profiler record into the ranked cost table."""
    windows = final["windows"]
    if windows == 0:
        return {"error": "no completed pre-notification windows"}
    divider = divider_from_tcr(final["frt_tcr"])
    slave_divider = divider_from_tcr(final["slave_frt_tcr"])
    cycles_per_tick = divider or 0
    cycles_per_vblank = SH2_CYCLES_PER_SECOND / VBLANKS_PER_SECOND
    ticks_per_vblank_nominal = (
        cycles_per_vblank / cycles_per_tick if cycles_per_tick else None
    )

    window_mean = final["window_ticks_accum"] / windows

    # Empirical cross-check against the VBlank-quantised cadence rig.  The
    # rig's pre-notification crossings are construction minus master
    # finalization (saturn_render_overlap_phase.c: construction =
    # start_construction + finalization).
    ticks_per_vblank_measured = None
    rig = {}
    if isinstance(cadence, dict) and "error" not in cadence:
        record = cadence.get("record", cadence)
        construction = record.get("construction_vblank_crossings")
        finalize = record.get("master_finalize_vblank_crossings")
        construction_count = record.get("construction_count")
        if None not in (construction, finalize, construction_count) \
                and construction_count:
            prenotify_crossings = construction - finalize
            rig = {
                "construction_vblank_crossings": construction,
                "master_finalize_vblank_crossings": finalize,
                "construction_count": construction_count,
                "slave_work_vblank_crossings":
                    record.get("slave_work_vblank_crossings"),
                "simulation_vblank_crossings":
                    record.get("simulation_vblank_crossings"),
                "simulation_count": record.get("simulation_count"),
                "prenotification_vblank_crossings": prenotify_crossings,
                "prenotification_vblanks_per_frame":
                    prenotify_crossings / construction_count,
            }
            if prenotify_crossings:
                ticks_per_vblank_measured = (
                    final["window_ticks_accum"] / prenotify_crossings
                )

    def row(name: str) -> dict[str, Any]:
        accum = final["node_ticks_accum"][name]
        mean = accum / windows
        return {
            "node": name,
            "mean_ticks": mean,
            "mean_cycles": mean * cycles_per_tick,
            "mean_vblank_equiv": (
                mean / ticks_per_vblank_nominal
                if ticks_per_vblank_nominal else None
            ),
            "share_of_window": mean / window_mean if window_mean else None,
            "max_ticks": final["node_ticks_max"][name],
            "max_cycles": final["node_ticks_max"][name] * cycles_per_tick,
        }

    rows = [row(name) for name in NODE_NAMES]
    attributed = sum(
        r["mean_ticks"] for r in rows
        if r["node"] not in ("window_residue", "spare")
    )
    ranked = sorted(rows, key=lambda r: r["mean_ticks"], reverse=True)

    retire_events = final["retire_events"] or 0
    finalize_events = final["finalize_events"] or 0
    slave_entries = final["slave_entries"] or 0
    slave_cycles_per_tick = slave_divider or 0

    return {
        "windows": windows,
        "frt_tcr": final["frt_tcr"],
        "frt_divider": divider,
        "slave_frt_tcr": final["slave_frt_tcr"],
        "slave_frt_divider": slave_divider,
        "cycles_per_tick": cycles_per_tick,
        "sh2_cycles_per_second": SH2_CYCLES_PER_SECOND,
        "cycles_per_vblank": cycles_per_vblank,
        "ticks_per_vblank_nominal": ticks_per_vblank_nominal,
        "ticks_per_vblank_measured": ticks_per_vblank_measured,
        "ticks_per_vblank_agreement": (
            ticks_per_vblank_measured / ticks_per_vblank_nominal
            if ticks_per_vblank_measured and ticks_per_vblank_nominal else None
        ),
        "cadence_rig": rig,
        "window_mean_ticks": window_mean,
        "window_mean_cycles": window_mean * cycles_per_tick,
        "window_mean_vblank_equiv": (
            window_mean / ticks_per_vblank_nominal
            if ticks_per_vblank_nominal else None
        ),
        "window_max_ticks": final["window_ticks_max"],
        # Wrap-safety witness.  The 16-bit FRT wraps at 65,536 ticks; a
        # value near that means an inter-probe interval nearly aliased and
        # the totals must not be trusted.
        "max_raw_interval": final["max_raw_interval"],
        "max_raw_interval_headroom": 65535 - final["max_raw_interval"],
        "faults": final["faults"],
        "attributed_mean_ticks": attributed,
        "unattributed_mean_ticks": window_mean - attributed,
        "unattributed_share": (
            (window_mean - attributed) / window_mean if window_mean else None
        ),
        "ranked": ranked,
        "master_notify_to_retire": {
            "events": retire_events,
            "mean_ticks": (
                final["notify_to_retire_accum"] / retire_events
                if retire_events else None
            ),
            "mean_vblank_equiv": (
                final["notify_to_retire_accum"] / retire_events
                / ticks_per_vblank_nominal
                if retire_events and ticks_per_vblank_nominal else None
            ),
            "max_ticks": final["notify_to_retire_max"],
        },
        "master_finalization": {
            "events": finalize_events,
            "mean_ticks": (
                final["finalize_ticks_accum"] / finalize_events
                if finalize_events else None
            ),
            "mean_vblank_equiv": (
                final["finalize_ticks_accum"] / finalize_events
                / ticks_per_vblank_nominal
                if finalize_events and ticks_per_vblank_nominal else None
            ),
            "max_ticks": final["finalize_ticks_max"],
        },
        "slave_busy": {
            "entries": slave_entries,
            "cycles_per_tick": slave_cycles_per_tick,
            "mean_ticks": (
                final["slave_busy_accum"] / slave_entries
                if slave_entries else None
            ),
            "mean_cycles": (
                final["slave_busy_accum"] / slave_entries * slave_cycles_per_tick
                if slave_entries else None
            ),
            "max_ticks": final["slave_busy_max"],
        },
    }


def acceptance(samples: list[dict[str, Any]], failure: bool) -> dict[str, Any]:
    valid = [s for s in samples if s["profile"]["magic_valid"]]
    positions = {
        tuple(s["mario"]["position"]) for s in valid if s["mario"]["valid"]
    }
    vdp = [s["boot"]["vdp2_presentation_generation"] for s in valid]
    windows = [s["profile"]["windows"] for s in valid]
    checks = {
        "capture_completed": not failure,
        "profile_seen": bool(valid),
        "profile_version_ok": bool(valid) and all(
            s["profile"]["version_valid"] for s in valid
        ),
        "profile_stable_sample": bool(valid) and valid[-1]["profile"]["stable"],
        "windows_accumulated": bool(windows) and windows[-1] > 0,
        "exception_record_clear": bool(valid) and all(
            s["exception_magic"] == 0 for s in valid
        ),
        "vdp_generations_climbing": len(vdp) >= 2 and vdp[-1] > vdp[0],
        "route_movement_observed": len(positions) >= 2,
        # No inter-probe interval came within 4,096 ticks of the 16-bit
        # wrap: positive evidence that the extended clock never aliased.
        "frt_wrap_headroom_ok": bool(valid) and (
            valid[-1]["profile"]["max_raw_interval"] < 61440
        ),
        "no_profiler_faults": bool(valid) and valid[-1]["profile"]["faults"] == 0,
    }
    return {**checks, "pass": all(checks.values())}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ymir", type=Path, required=True)
    parser.add_argument("--ipl", type=Path, required=True)
    parser.add_argument("--game", type=Path, required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--release-manifest", type=Path, required=True)
    parser.add_argument("--identity-spec", type=Path, required=False)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--post-bios-frames", type=int, default=DEFAULT_POST_BIOS_FRAMES
    )
    parser.add_argument(
        "--sample-interval", type=int, default=DEFAULT_SAMPLE_INTERVAL_FRAMES
    )
    parser.add_argument("--timeout", type=float, default=7200.0)
    args = parser.parse_args(argv)

    for label, path in (
        ("Ymir", args.ymir),
        ("IPL", args.ipl),
        ("game", args.game),
        ("ELF", args.elf),
        ("release manifest", args.release_manifest),
    ):
        if not path.is_file():
            parser.error(f"{label} is not a file: {path}")
    if args.post_bios_frames < MIN_POST_BIOS_FRAMES:
        parser.error(
            f"--post-bios-frames must be >= {MIN_POST_BIOS_FRAMES} "
            "(two emulated minutes of gameplay)"
        )
    if args.sample_interval <= 0 or args.sample_interval > 600:
        parser.error("--sample-interval must be positive and <= 600")
    if args.post_bios_frames % args.sample_interval != 0:
        parser.error(
            "--post-bios-frames must be an exact multiple of --sample-interval"
        )
    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    args.ymir = args.ymir.resolve()
    args.ipl = args.ipl.resolve()
    args.game = args.game.resolve()
    args.elf = args.elf.resolve()
    args.release_manifest = args.release_manifest.resolve()
    if args.identity_spec is not None:
        args.identity_spec = args.identity_spec.resolve()
    args.output = args.output.resolve()

    try:
        release_binding = resolve_release_binding(
            args.release_manifest, args.game, args.elf, args.identity_spec
        )
        capture_cue = release_binding.get("cue", args.game)
        capture_elf = release_binding.get("elf", args.elf)
        artifacts = bind_capture_artifacts(capture_cue, capture_elf)
        target_identity_probe = build_elf_identity_probe(capture_elf)
        addresses = resolve_capture_addresses(capture_elf)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(str(error))

    wall_start = time.perf_counter()
    emulated_frames = 0
    client: YmirClient | None = None
    samples: list[dict[str, Any]] = []
    target_identity: dict[str, Any] | None = None
    failure: BaseException | None = None
    try:
        client = YmirClient(args.ymir, args.ipl, capture_cue, args.timeout)

        def run_for(frames: int) -> None:
            nonlocal emulated_frames
            remaining = frames
            while remaining > 0:
                chunk = min(remaining, RUN_FOR_CHUNK_FRAMES)
                client.call("exec.run_for", {"frames": chunk})
                emulated_frames += chunk
                remaining -= chunk

        def sample(label: str) -> None:
            samples.append({
                "label": label,
                "emulated_frames": emulated_frames,
                **read_sample(client, addresses),
            })

        run_bios_handoff(client, run_for, sample)
        target_identity = wait_for_sealed_target_identity(
            client,
            target_identity_probe,
            release_binding["probe"],
            release_binding["sealed_identity"],
            startup_vblanks=STARTUP_IDENTITY_VBLANKS,
            run_for=run_for,
        )

        elapsed = 0
        while elapsed < args.post_bios_frames:
            run_for(args.sample_interval)
            elapsed += args.sample_interval
            sample(f"post-bios-{elapsed}")
        client.shutdown()
    except BaseException as error:  # noqa: BLE001 - report, never mask
        failure = error
        if client is not None:
            client.abort()

    valid = [s for s in samples if s["profile"]["magic_valid"]]
    summary: dict[str, Any] = {}
    if valid:
        summary = summarize(valid[-1]["profile"], valid[-1]["cadence"])

    report: dict[str, Any] = {
        "evidence_kind": "ymir-sprint2-t2_4-prenotification-profile",
        "diagnostic_only": True,
        "manual_gui_launch": False,
        "target_build": False,
        "performance_measurement": True,
        "ymir": str(args.ymir),
        "ipl": str(args.ipl),
        "artifacts": artifacts,
        "release_manifest_sha256": release_binding["release_manifest_sha256"],
        "identity_values": release_binding["identity_values"],
        "target_identity": target_identity,
        "addresses": addresses,
        "cache_through_addresses": {
            symbol: cpu_cache_through_alias(address)
            for symbol, address in addresses.items()
        },
        "node_names": list(NODE_NAMES),
        "sample_interval_frames": args.sample_interval,
        "requested_post_bios_frames": args.post_bios_frames,
        "emulated_frames": emulated_frames,
        "sample_count": len(samples),
        "valid_sample_count": len(valid),
        "samples": samples,
        "summary": summary,
        "acceptance": acceptance(samples, failure is not None),
        "wall_seconds": time.perf_counter() - wall_start,
    }
    if client is not None:
        report.update(protocol_and_diagnostics(client))
    if failure is not None:
        report["failure"] = {
            "message": str(failure), "type": type(failure).__name__
        }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps(
        {"summary": summary, "acceptance": report["acceptance"]},
        indent=2, sort_keys=True,
    ))
    return 1 if failure is not None or not report["acceptance"]["pass"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
