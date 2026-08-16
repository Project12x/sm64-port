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
PROFILE_VERSION = 3
PROFILE_NODES = 24
# T2.8 appended a 66-word present-path / VDP1-fence section.
PROFILE_COPR_RING = 32
PROFILE_WORDS = 179
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
    # T2.5 sub-nodes, all nested under prepare_mario.
    "mario_setup",
    "meshlet_prepare",
    "meshlet_admit",
    "meshlet_depth_admit",
    "meshlet_prefix",
    "meshlet_emit",
    "meshlet_depth_emit",
    "mario_draw_order",
    "spare",
)
assert len(NODE_NAMES) == PROFILE_NODES

# Nodes whose self time nests inside prepare_mario.  Used to reconstruct the
# stage total and to report prepare_mario's own unattributed remainder.
PREPARE_MARIO_CHILDREN = (
    "mario_setup",
    "meshlet_prepare",
    "meshlet_admit",
    "meshlet_depth_admit",
    "meshlet_prefix",
    "meshlet_emit",
    "meshlet_depth_emit",
    "mario_draw_order",
)

# actor_meshlet_live_depth_bounds walks the whole tier-0 position span of
# every meshlet, unconditionally, once per pass.  That span total is a
# compile-time property of sm64_mario_meshlet_lod_position_offsets in
# src/port/saturn/gfx/saturn_mario_actor_mesh.h, not a runtime quantity, so
# per-vertex cost can be derived by division without a per-vertex probe.
MARIO_TIER0_POSITION_VISITS_PER_PASS = 704
MARIO_VERTEX_COUNT = 424
MARIO_PRIMITIVE_COUNT = 644
MARIO_MESHLET_COUNT = 31

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
    nodes_accum = w[11:11 + PROFILE_NODES]
    nodes_max = w[11 + PROFILE_NODES:11 + 2 * PROFILE_NODES]
    nodes_last = w[11 + 2 * PROFILE_NODES:11 + 3 * PROFILE_NODES]
    nodes_calls = w[11 + 3 * PROFILE_NODES:11 + 4 * PROFILE_NODES]
    tail = w[11 + 4 * PROFILE_NODES:]
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
        "end_depth_max": w[10],
        "node_ticks_accum": dict(zip(NODE_NAMES, nodes_accum)),
        "node_ticks_max": dict(zip(NODE_NAMES, nodes_max)),
        "node_ticks_last": dict(zip(NODE_NAMES, nodes_last)),
        "node_calls_last": dict(zip(NODE_NAMES, nodes_calls)),
        "slave_entries": tail[0],
        "slave_busy_accum": tail[1],
        "slave_busy_last": tail[2],
        "slave_busy_max": tail[3],
        "slave_frt_tcr": tail[4],
        # --- T2.8 present path / VDP1 draw fence -----------------------
        "present_windows": tail[5],
        "present_ticks_last": tail[6],
        "present_ticks_accum": tail[7],
        "present_ticks_max": tail[8],
        "vdp1_render_ticks_last": tail[9],
        "vdp1_render_ticks_accum": tail[10],
        "vdp1_render_ticks_max": tail[11],
        "vdp1_sync_ticks_last": tail[12],
        "vdp1_sync_ticks_accum": tail[13],
        "vdp2_commit_ticks_last": tail[14],
        "vdp2_commit_ticks_accum": tail[15],
        "vdp1_fence_events": tail[16],
        "vdp1_fence_waits": tail[17],
        "vdp1_fence_ticks_last": tail[18],
        "vdp1_fence_ticks_accum": tail[19],
        "vdp1_fence_ticks_max": tail[20],
        "vdp1_fence_iterations_last": tail[21],
        "vdp1_fence_iterations_accum": tail[22],
        "vdp1_fence_max_raw": tail[23],
        "vdp1_edsr_entry_last": tail[24],
        "vdp1_edsr_cef_entry_count": tail[25],
        "vdp1_copr_entry_last": tail[26],
        "vdp1_copr_exit_last": tail[27],
        "vdp1_lopr_last": tail[28],
        "vdp1_vblank_samples": tail[29],
        "vdp1_vblank_cef_count": tail[30],
        "vdp1_copr_retired_accum": tail[31],
        "vdp1_copr_retired_intervals": tail[32],
        "vdp1_copr_retired_max": tail[33],
        "vdp1_copr_vblank_ring": list(tail[34:34 + PROFILE_COPR_RING]),
        "vdp1_copr_vblank_ring_cursor": tail[34 + PROFILE_COPR_RING],
        "commands_total_last": tail[35 + PROFILE_COPR_RING],
        "commands_total_accum": tail[36 + PROFILE_COPR_RING],
        "commands_actor_accum": tail[37 + PROFILE_COPR_RING],
        "commands_texture_accum": tail[38 + PROFILE_COPR_RING],
        "sequence_end": tail[39 + PROFILE_COPR_RING],
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


def present_summary(
    final: dict[str, Any],
    ticks_per_vblank_nominal: float | None,
    cycles_per_tick: int,
) -> dict[str, Any]:
    """T2.8: the present path and the VDP1 draw fence.

    The fence total is accumulated one FRT difference per spin iteration on
    the target, so unlike the shipped single-span bracket it cannot alias
    across a 16-bit wrap.  ``vdp1_fence_max_raw`` is the witness: it is the
    largest single inter-probe interval the fence ever saw, and a value far
    below 65,535 is positive evidence that nothing wrapped.

    ``vdp1_vblank_cef_share`` is the decisive number.  EDSR.CEF is VDP1's
    draw-end flag; sampled once per VBlank across the whole run it is the
    fraction of fields at which VDP1 had already finished plotting.  Near 1
    means VDP1 is idle almost all the time and the frame is CPU-bound; near 0
    means VDP1 is plotting continuously and the frame is fill-bound.
    """
    presents = final["present_windows"]
    fence_events = final["vdp1_fence_events"]
    vblank_samples = final["vdp1_vblank_samples"]

    def vb(ticks: float | None) -> float | None:
        if ticks is None or not ticks_per_vblank_nominal:
            return None
        return ticks / ticks_per_vblank_nominal

    def per_present(field: str) -> float | None:
        return final[field] / presents if presents else None

    fence_mean = final["vdp1_fence_ticks_accum"] / fence_events if fence_events else None
    retired_intervals = final["vdp1_copr_retired_intervals"]
    # COPR counts VDP1 VRAM in 8-byte units; a command table is 32 bytes.
    copr_units_per_command = 4
    return {
        "present_windows": presents,
        "present_mean_ticks": per_present("present_ticks_accum"),
        "present_mean_vblank_equiv": vb(per_present("present_ticks_accum")),
        "present_max_ticks": final["present_ticks_max"],
        "vdp1_sync_render_mean_ticks": per_present("vdp1_render_ticks_accum"),
        "vdp1_sync_render_mean_vblank_equiv":
            vb(per_present("vdp1_render_ticks_accum")),
        "vdp1_sync_render_max_ticks": final["vdp1_render_ticks_max"],
        "vdp1_sync_mean_ticks": per_present("vdp1_sync_ticks_accum"),
        "vdp2_commit_mean_ticks": per_present("vdp2_commit_ticks_accum"),
        "vdp2_commit_mean_vblank_equiv":
            vb(per_present("vdp2_commit_ticks_accum")),
        "fence": {
            "events": fence_events,
            "waits": final["vdp1_fence_waits"],
            "wait_share_of_events": (
                final["vdp1_fence_waits"] / fence_events if fence_events else None
            ),
            "mean_ticks": fence_mean,
            "mean_cycles": fence_mean * cycles_per_tick if fence_mean else None,
            "mean_vblank_equiv": vb(fence_mean),
            "max_ticks": final["vdp1_fence_ticks_max"],
            "max_vblank_equiv": vb(final["vdp1_fence_ticks_max"]),
            "mean_iterations": (
                final["vdp1_fence_iterations_accum"] / fence_events
                if fence_events else None
            ),
            "max_raw_interval": final["vdp1_fence_max_raw"],
            "max_raw_interval_headroom": 65535 - final["vdp1_fence_max_raw"],
            "edsr_entry_last": final["vdp1_edsr_entry_last"],
            "edsr_cef_entry_count": final["vdp1_edsr_cef_entry_count"],
            "edsr_cef_entry_share": (
                final["vdp1_edsr_cef_entry_count"] / fence_events
                if fence_events else None
            ),
            "copr_entry_last": final["vdp1_copr_entry_last"],
            "copr_exit_last": final["vdp1_copr_exit_last"],
            "lopr_last": final["vdp1_lopr_last"],
        },
        "vdp1_vblank_samples": vblank_samples,
        "vdp1_vblank_cef_count": final["vdp1_vblank_cef_count"],
        "vdp1_vblank_cef_share": (
            final["vdp1_vblank_cef_count"] / vblank_samples
            if vblank_samples else None
        ),
        "copr_retired_intervals": retired_intervals,
        "copr_retired_mean_units_per_vblank": (
            final["vdp1_copr_retired_accum"] / retired_intervals
            if retired_intervals else None
        ),
        "copr_retired_mean_commands_per_vblank": (
            final["vdp1_copr_retired_accum"]
            / retired_intervals / copr_units_per_command
            if retired_intervals else None
        ),
        "copr_retired_max_units": final["vdp1_copr_retired_max"],
        "copr_retired_max_commands":
            final["vdp1_copr_retired_max"] / copr_units_per_command,
        "copr_vblank_ring": final["vdp1_copr_vblank_ring"],
        "copr_vblank_ring_cursor": final["vdp1_copr_vblank_ring_cursor"],
        "commands": {
            "total_last": final["commands_total_last"],
            "total_accum": final["commands_total_accum"],
            "total_mean_per_present": per_present("commands_total_accum"),
            "actor_accum": final["commands_actor_accum"],
            "texture_accum": final["commands_texture_accum"],
            "actor_mean_per_present": per_present("commands_actor_accum"),
            "texture_mean_per_present": per_present("commands_texture_accum"),
            "actor_share_of_total": (
                final["commands_actor_accum"] / final["commands_total_accum"]
                if final["commands_total_accum"] else None
            ),
        },
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
            "calls_last_window": final["node_calls_last"][name],
        }

    rows = [row(name) for name in NODE_NAMES]
    attributed = sum(
        r["mean_ticks"] for r in rows
        if r["node"] not in ("window_residue", "spare")
    )
    ranked = sorted(rows, key=lambda r: r["mean_ticks"], reverse=True)
    by_name = {r["node"]: r for r in rows}

    # prepare_mario is now a parent: its own row carries only self time, so
    # the stage total is that plus every nested child.
    prepare_children_ticks = sum(
        by_name[c]["mean_ticks"] for c in PREPARE_MARIO_CHILDREN
    )
    prepare_self_ticks = by_name["prepare_mario"]["mean_ticks"]
    prepare_total_ticks = prepare_self_ticks + prepare_children_ticks
    depth_ticks = (
        by_name["meshlet_depth_admit"]["mean_ticks"]
        + by_name["meshlet_depth_emit"]["mean_ticks"]
    )
    # Two passes, each walking every meshlet's whole tier-0 position span.
    depth_position_visits = 2 * MARIO_TIER0_POSITION_VISITS_PER_PASS
    prepare_mario_stage = {
        "self_mean_ticks": prepare_self_ticks,
        "children_mean_ticks": prepare_children_ticks,
        "total_mean_ticks": prepare_total_ticks,
        "total_mean_cycles": prepare_total_ticks * cycles_per_tick,
        "total_share_of_window": (
            prepare_total_ticks / window_mean if window_mean else None
        ),
        "self_share_of_stage": (
            prepare_self_ticks / prepare_total_ticks
            if prepare_total_ticks else None
        ),
        "depth_bounds_mean_ticks": depth_ticks,
        "depth_bounds_mean_cycles": depth_ticks * cycles_per_tick,
        "depth_bounds_share_of_stage": (
            depth_ticks / prepare_total_ticks if prepare_total_ticks else None
        ),
        "depth_bounds_position_visits": depth_position_visits,
        "depth_bounds_cycles_per_position_visit": (
            depth_ticks * cycles_per_tick / depth_position_visits
        ),
        "stage_cycles_per_mesh_vertex": (
            prepare_total_ticks * cycles_per_tick / MARIO_VERTEX_COUNT
        ),
        "mesh": {
            "vertices": MARIO_VERTEX_COUNT,
            "primitives": MARIO_PRIMITIVE_COUNT,
            "meshlets": MARIO_MESHLET_COUNT,
            "tier0_position_visits_per_pass":
                MARIO_TIER0_POSITION_VISITS_PER_PASS,
        },
    }

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
        "end_depth_max": final["end_depth_max"],
        "prepare_mario_stage": prepare_mario_stage,
        "attributed_mean_ticks": attributed,
        "unattributed_mean_ticks": window_mean - attributed,
        "unattributed_share": (
            (window_mean - attributed) / window_mean if window_mean else None
        ),
        "ranked": ranked,
        # T2.4's master_notify_to_retire / master_finalization were removed:
        # the RETIRED marker observer runs on the slave SH-2 and the FRT is a
        # per-CPU block, so both differenced two unrelated counters.  The
        # cadence rig above reports both intervals correctly in VBlanks.
        "present": present_summary(final, ticks_per_vblank_nominal,
                                   cycles_per_tick),
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
        # T2.4 shipped this check broken: end() counted the deliberately
        # still-pushed NOTIFY node as a fault, so faults == windows by
        # construction and the gate could never pass.  end() now records
        # the closing depth instead and faults only on a real imbalance.
        "no_profiler_faults": bool(valid) and valid[-1]["profile"]["faults"] == 0,
        # Depth 1 at end() is the design (NOTIFY is still pushed); anything
        # above that means a push without its pop.
        "profiler_stack_balanced": bool(valid) and (
            valid[-1]["profile"]["end_depth_max"] <= 1
        ),
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
