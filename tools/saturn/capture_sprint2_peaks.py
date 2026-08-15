#!/usr/bin/env python3
"""Sprint 2 T2.1: instrumented peak capture over the scripted BOB route.

Measures the observed peaks that gate every Sprint 2 capacity-shrink
candidate (docs/superpowers/plans/2026-08-15-sprint2-cadence-recovery.md,
Task T2.1; candidates ranked in
docs/saturn/evidence/reports/sprint2-t1-hwram-attribution.md):

1. Max per-frame VDP1 command_count      -> gates cmdt capacity 2048->1664.
   Read from ``g_sm64_saturn_peak_probe.vdp1_commands_highwater`` -- a
   run-long monotonic accumulator added for this capture
   (SATURN_DIAGNOSTIC_MODE=1 builds only; the pre-existing fast3d
   ``vdp1_command_highwater`` is wiped by the per-frame profile clear in
   sm64_saturn_fast3d_frontend_submit and holds only the latest frame).
2. libyaul TLSF ``_private_pool`` high-water -> gates 0xA000->0x4000.
   No target code: the pool is crt0-zeroed HWRAM ``.bss``; a full dump is
   analyzed host-side by (a) a stain scan (highest ever-written offset)
   and (b) a physical TLSF block walk of the live structures
   (third_party/libyaul kernel/mm/tlsf.c layout, submodule 6012f79f).
3. Max ``gGfxPool`` display-list usage    -> gates GFX_POOL_SIZE 6400->4096.
   Read from ``g_sm64_saturn_peak_probe.gfx_pool_entries_highwater``
   (accumulated at create_gfx_task_structure, src/game/game_init.c).
4. SMPC peripheral pool usage             -> gates the pool-shrink reserve.
   No target code: samples libyaul's ``_peripherals_memb`` memb_t
   (``alloc_count`` live value; ``memb_alloc`` round-robin ``next_index``
   noted for churn context) plus the 14-entry reference array.

Honesty notes baked into the report: sampled live values (memb
``alloc_count``, ``*_last`` mirrors) can miss peaks between samples; the
``*_highwater`` fields and the object-pool probe's ``peak_allocated`` are
true accumulators and cannot.  The scripted route drives input only for
the live-input bootstrap ticks; afterwards the pad is neutral, so the
capture covers the route's movement phases plus idle, not every gameplay
state.

Pattern-copied from capture_object_pool_occupancy.py (release-manifest
binding, sealed-identity wait, chunked exec.run_for <= 600 frames,
sh-elf-nm through the DLL-safe MSYS wrapper) with one addition: a
local-symbol resolver (nm without ``-g``) for libyaul statics.
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
    MSYS_TOOLCHAIN_WRAPPER,
    POWERSHELL,
    bind_capture_artifacts,
    cpu_cache_through_alias,
    protocol_and_diagnostics,
    resolve_probe_symbol,
    run_bios_handoff,
    wrapped_nm_command,
)
from capture_object_pool_occupancy import (
    PROBE_BYTES as OBJECT_POOL_PROBE_BYTES,
    PROBE_SYMBOL as OBJECT_POOL_PROBE_SYMBOL,
    STARTUP_IDENTITY_VBLANKS,
    decode_probe as decode_object_pool_probe,
    read_smoke_bytes,
    resolve_release_binding,
    wait_for_sealed_target_identity,
)

ROOT = Path(__file__).resolve().parents[2]

PEAK_PROBE_SYMBOL = "g_sm64_saturn_peak_probe"
PEAK_PROBE_MAGIC = 0x504B5042  # 'PKPB'
PEAK_PROBE_WORDS = 8
PEAK_PROBE_BYTES = PEAK_PROBE_WORDS * 4

# libyaul statics (local symbols; resolved via nm WITHOUT -g).
PRIVATE_POOL_SYMBOL = "_private_pool"          # kernel/mm/internal.c
PRIVATE_POOL_BYTES = 0xA000                    # TLSF_POOL_PRIVATE_SIZE
PERIPHERALS_MEMB_SYMBOL = "_peripherals_memb"  # smpc/smpc_peripheral.c
PERIPHERALS_MEMB_BYTES = 32                    # sizeof(memb_t), SH-2 ILP32
PERIPHERALS_REFS_SYMBOL = "_peripherals_memb_memb_ref"
PERIPHERALS_REFS_BYTES = 28                    # 14 x packed uint16 refcount

# Movement witness: sourceboot's per-tick Mario actor snapshot (static in
# src/port/saturn/sourceboot/main.c; sm64_saturn_mario_actor_snapshot_t in
# src/port/saturn/gfx/saturn_actor_bridge.h).  sAreaYaw is inert under the
# R1 tuple's fixed-camera variant, so the route-movement acceptance gate
# reads Mario's world position instead.
MARIO_SNAPSHOT_SYMBOL = "sourceboot_mario_snapshot"
MARIO_SNAPSHOT_BYTES = 56

# TLSF layout facts for the linked libyaul (submodule 6012f79f,
# kernel/mm/tlsf.c): 32-bit build => SL_INDEX_COUNT_LOG2=5,
# ALIGN_SIZE_LOG2=2, FL_INDEX_MAX=30, FL_INDEX_SHIFT=7, FL_INDEX_COUNT=24.
# control_t = block_null(16) + fl_bitmap(4) + sl_bitmap[24](96)
#           + blocks[24][32] pointers (3072) = 3188 bytes; tlsf_size()
#           returns sizeof(control_t).  tlsf_pool_create places control_t
#           at the pool base and the block arena directly after it; the
#           first block's size field sits AT the arena base (its header
#           starts 4 bytes earlier, overlapping the control tail by
#           design), and each next size field is at +4+size.  A zero-size
#           used sentinel terminates the arena.
TLSF_CONTROL_SIZE = 3188
TLSF_WALK_BLOCK_LIMIT = 4096

SMOKE_SYMBOLS = {
    "sAreaYaw": 2,
    "sourceboot_exception_record": 4,
    "sourceboot_boot_trace": BOOT_TRACE_BYTES,
    "sourceboot_cadence_trace": CADENCE_TRACE_BYTES,
}

RUN_FOR_CHUNK_FRAMES = 300
DEFAULT_SAMPLE_INTERVAL_FRAMES = 300
# 300 * 80 = 24,000 emulated vblanks (~400 s of emulated time): covers the
# route-replay movement phase (area yaw was still changing at ~9,900
# vblanks in the occupancy capture) plus an idle tail.
DEFAULT_POST_BIOS_FRAMES = 24000
MIN_POST_BIOS_FRAMES = 7200  # >= 2 minutes of emulated gameplay
DUMP_CHUNK_BYTES = 4096


def wrapped_nm_all_command(elf: Path, *, nm: Path = NM) -> list[str]:
    """nm through the DLL-safe wrapper, WITHOUT -g, so statics resolve."""
    return [
        POWERSHELL,
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        str(MSYS_TOOLCHAIN_WRAPPER),
        str(nm),
        "--defined-only",
        str(elf),
    ]


def resolve_local_symbol(nm_output: str, symbol: str) -> int:
    """Resolve one (possibly static) symbol, requiring a unique address."""
    names = (symbol, f"_{symbol}")
    addresses: set[int] = set()
    for line in nm_output.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[2] in names:
            addresses.add(int(fields[0], 16))
    if len(addresses) != 1:
        raise ValueError(
            f"symbol {symbol} resolved to {len(addresses)} addresses; "
            "need exactly one"
        )
    return addresses.pop()


def resolve_capture_addresses(
    elf: Path, *, nm: Path = NM, run: Any = subprocess.run
) -> dict[str, int]:
    """Resolve every sampled symbol from two nm listings (globals+locals)."""
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
        for symbol in (PEAK_PROBE_SYMBOL, OBJECT_POOL_PROBE_SYMBOL, *SMOKE_SYMBOLS)
    }
    for symbol in (
        PRIVATE_POOL_SYMBOL,
        PERIPHERALS_MEMB_SYMBOL,
        PERIPHERALS_REFS_SYMBOL,
        MARIO_SNAPSHOT_SYMBOL,
    ):
        addresses[symbol] = resolve_local_symbol(all_listing.stdout, symbol)
    return addresses


def be_words(raw: bytes) -> list[int]:
    if len(raw) % 4 != 0:
        raise ValueError("word decode requires a multiple of four bytes")
    return [int.from_bytes(raw[i:i + 4], "big") for i in range(0, len(raw), 4)]


def decode_peak_probe(raw: bytes) -> dict[str, Any]:
    """Decode g_sm64_saturn_peak_probe leniently (pre-init reads happen)."""
    if len(raw) != PEAK_PROBE_BYTES:
        raise ValueError(
            f"peak probe read returned {len(raw)} bytes, expected {PEAK_PROBE_BYTES}"
        )
    words = be_words(raw)
    magic_valid = words[0] == PEAK_PROBE_MAGIC
    names = (
        "gfx_pool_entries_last",
        "gfx_pool_entries_highwater",
        "gfx_pool_task_count",
        "vdp1_commands_last",
        "vdp1_commands_highwater",
        "vdp1_gouraud_last",
        "vdp1_gouraud_highwater",
    )
    decoded: dict[str, Any] = {
        "magic": words[0],
        "magic_valid": magic_valid,
        "raw_words": words,
    }
    for index, name in enumerate(names, start=1):
        decoded[name] = words[index] if magic_valid else None
    return decoded


def decode_memb(raw: bytes) -> dict[str, Any]:
    """Decode libyaul memb_t (kernel/mm/memb.h, 32-bit big-endian)."""
    if len(raw) != PERIPHERALS_MEMB_BYTES:
        raise ValueError(
            f"memb_t read returned {len(raw)} bytes, expected {PERIPHERALS_MEMB_BYTES}"
        )
    words = be_words(raw)
    return {
        "type": words[0],
        "block_size": words[1],
        "block_count": words[2],
        "refs_pointer": words[3],
        "next_index": words[4],
        "alloc_count": words[5],
        "pool_pointer": words[6],
        "free_pointer": words[7],
        "raw_words": words,
    }


def decode_mario_snapshot(raw: bytes) -> dict[str, Any]:
    """Decode position/camera/validity from the Mario actor snapshot."""
    if len(raw) != MARIO_SNAPSHOT_BYTES:
        raise ValueError(
            f"mario snapshot read returned {len(raw)} bytes, "
            f"expected {MARIO_SNAPSHOT_BYTES}"
        )

    def s32(offset: int) -> int:
        return int.from_bytes(raw[offset:offset + 4], "big", signed=True)

    return {
        "position": [s32(0), s32(4), s32(8)],
        "camera_position": [s32(12), s32(16), s32(20)],
        "yaw": int.from_bytes(raw[36:38], "big", signed=True),
        "action": int.from_bytes(raw[40:44], "big"),
        "valid": raw[55],
    }


def decode_refs(raw: bytes) -> dict[str, Any]:
    """Decode the packed uint16 refcount array behind MEMB()."""
    counts = [
        int.from_bytes(raw[i:i + 2], "big") for i in range(0, len(raw), 2)
    ]
    return {
        "counts": counts,
        "referenced_slots": sum(1 for count in counts if count != 0),
    }


def stain_scan(pool: bytes, area_start: int) -> dict[str, Any]:
    """Highest ever-written offsets in a crt0-zeroed pool dump.

    A nonzero byte proves that address was written at some point in the
    run (TLSF block headers, free-list links, or payload).  Zero bytes do
    NOT prove the address was never allocated (zero payloads leave no
    stain), so this is a lower bound on placement extent -- but every
    block ever created leaves a nonzero size/flags header, which is what
    the placement high-water needs.
    """
    highest = -1
    for index in range(len(pool) - 1, -1, -1):
        if pool[index] != 0:
            highest = index
            break
    highest_in_area = -1
    for index in range(len(pool) - 1, area_start - 1, -1):
        if pool[index] != 0:
            highest_in_area = index
            break
    return {
        "pool_bytes": len(pool),
        "area_start": area_start,
        "highest_nonzero_offset": highest,
        "highest_nonzero_offset_in_area": highest_in_area,
    }


def tlsf_walk(pool: bytes, control_size: int) -> dict[str, Any]:
    """Physically walk the TLSF block chain in a pool dump (host-side).

    Layout per third_party/libyaul kernel/mm/tlsf.c (see module comment):
    first block size field at ``control_size``; successive size fields at
    +4+size; a zero-size block terminates the arena (sentinel).  size
    low bits: bit0 = block free, bit1 = previous block free.
    """
    blocks: list[dict[str, Any]] = []
    offset = control_size
    valid = False
    sentinel_offset = None
    while len(blocks) < TLSF_WALK_BLOCK_LIMIT:
        if offset + 4 > len(pool):
            break
        size_flags = int.from_bytes(pool[offset:offset + 4], "big")
        size = size_flags & ~0x3
        if size == 0:
            sentinel_offset = offset
            valid = True
            break
        blocks.append({
            "size_field_offset": offset,
            "payload_offset": offset + 4,
            "size": size,
            "free": bool(size_flags & 0x1),
        })
        offset += size + 4
    used_blocks = [block for block in blocks if not block["free"]]
    free_blocks = [block for block in blocks if block["free"]]
    return {
        "valid": valid,
        "control_size": control_size,
        "block_count": len(blocks),
        "sentinel_offset": sentinel_offset,
        "used_block_count": len(used_blocks),
        "used_payload_bytes": sum(block["size"] for block in used_blocks),
        "free_payload_bytes": sum(block["size"] for block in free_blocks),
        "largest_free_block": max(
            (block["size"] for block in free_blocks), default=0
        ),
        "highest_used_payload_end": max(
            (block["payload_offset"] + block["size"] for block in used_blocks),
            default=None,
        ),
        "blocks": blocks[:64],
    }


def read_private_pool(client: YmirClient, address: int) -> bytes:
    """Dump the whole private pool through the P2 alias, chunked."""
    chunks: list[bytes] = []
    base = cpu_cache_through_alias(address)
    for offset in range(0, PRIVATE_POOL_BYTES, DUMP_CHUNK_BYTES):
        count = min(DUMP_CHUNK_BYTES, PRIVATE_POOL_BYTES - offset)
        chunks.append(read_smoke_bytes(client, base + offset, count))
    return b"".join(chunks)


def highest_nonzero_below(pool: bytes, limit: int) -> int:
    """Highest stained offset strictly below ``limit`` (-1 if none)."""
    for index in range(min(limit, len(pool)) - 1, -1, -1):
        if pool[index] != 0:
            return index
    return -1


def analyze_private_pool(pool: bytes) -> dict[str, Any]:
    walk = tlsf_walk(pool, TLSF_CONTROL_SIZE)
    analysis: dict[str, Any] = {
        "stain": stain_scan(pool, TLSF_CONTROL_SIZE),
        "walk": walk,
    }
    # The raw stain high-water is dominated by structural terminals that
    # exist from pool creation: the zero-size sentinel at the pool end and
    # the top free block's header/free-list links.  Recompute below each so
    # transient allocations deeper than the current block chain would show.
    if walk["valid"] and walk["sentinel_offset"] is not None:
        analysis["stain_below_sentinel"] = highest_nonzero_below(
            pool, walk["sentinel_offset"] - 4  # exclude its prev_phys too
        )
        free_blocks = [b for b in walk["blocks"] if b["free"]]
        if free_blocks:
            top_free = max(free_blocks, key=lambda b: b["size_field_offset"])
            analysis["stain_below_top_free_block"] = highest_nonzero_below(
                pool, top_free["size_field_offset"]
            )
    return analysis


def decode_s16_be(raw: bytes) -> int:
    value = int.from_bytes(raw, "big")
    return value - 0x10000 if value & 0x8000 else value


def validate_post_bios_frames(frames: int) -> int:
    if frames < MIN_POST_BIOS_FRAMES:
        raise ValueError(
            f"--post-bios-frames must be >= {MIN_POST_BIOS_FRAMES} "
            "(two emulated minutes of gameplay)"
        )
    return frames


def validate_sample_interval(interval: int) -> int:
    if interval <= 0 or interval > 600:
        raise ValueError(
            "--sample-interval must be a positive value <= 600 "
            "(exec.run_for chunk cap)"
        )
    return interval


def read_sample(client: YmirClient, addresses: dict[str, int]) -> dict[str, Any]:
    """Read every per-interval counter from one paused target instant."""
    peak_raw = read_smoke_bytes(
        client, cpu_cache_through_alias(addresses[PEAK_PROBE_SYMBOL]),
        PEAK_PROBE_BYTES,
    )
    memb_raw = read_smoke_bytes(
        client, cpu_cache_through_alias(addresses[PERIPHERALS_MEMB_SYMBOL]),
        PERIPHERALS_MEMB_BYTES,
    )
    object_pool_raw = read_smoke_bytes(
        client, cpu_cache_through_alias(addresses[OBJECT_POOL_PROBE_SYMBOL]),
        OBJECT_POOL_PROBE_BYTES,
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
    # Pre-handoff samples legitimately read garbage (the ELF image is not
    # in RAM yet); decode leniently rather than treating that as a capture
    # failure -- the occupancy capture's established convention.
    try:
        cadence: dict[str, Any] = decode_cadence_trace(
            smoke_raw["sourceboot_cadence_trace"]
        )
    except ValueError as error:
        cadence = {"error": str(error)}
    return {
        "peak_probe": decode_peak_probe(peak_raw),
        "smpc_memb": decode_memb(memb_raw),
        "object_pool": decode_object_pool_probe(list(object_pool_raw)),
        "mario": decode_mario_snapshot(mario_raw),
        "area_yaw": decode_s16_be(smoke_raw["sAreaYaw"]),
        "exception_magic": int.from_bytes(
            smoke_raw["sourceboot_exception_record"], "big"
        ),
        "boot": decode_boot_trace(smoke_raw["sourceboot_boot_trace"]),
        "cadence": cadence,
    }


def acceptance(samples: list[dict[str, Any]], failure: bool) -> dict[str, Any]:
    valid = [s for s in samples if s["peak_probe"]["magic_valid"]]
    positions = {
        tuple(s["mario"]["position"]) for s in valid if s["mario"]["valid"]
    }
    yaws = {s["area_yaw"] for s in valid}
    vdp = [s["boot"]["vdp2_presentation_generation"] for s in valid]
    checks = {
        "capture_completed": not failure,
        "peak_probe_seen": bool(valid),
        "exception_record_clear": bool(valid) and all(
            s["exception_magic"] == 0 for s in valid
        ),
        "vdp_generations_climbing": len(vdp) >= 2 and vdp[-1] > vdp[0],
        # Movement gate: distinct sampled Mario world positions.  sAreaYaw
        # is recorded but inert under the fixed-camera variant, so it is
        # not the gate.
        "route_movement_observed": len(positions) >= 2,
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
    try:
        args.post_bios_frames = validate_post_bios_frames(args.post_bios_frames)
        args.sample_interval = validate_sample_interval(args.sample_interval)
    except ValueError as error:
        parser.error(str(error))
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.post_bios_frames % args.sample_interval != 0:
        parser.error("--post-bios-frames must be an exact multiple of --sample-interval")

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
    mid_pool: dict[str, Any] | None = None
    final_pool: dict[str, Any] | None = None
    final_refs: dict[str, Any] | None = None
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

        halfway = (args.post_bios_frames // args.sample_interval // 2) \
            * args.sample_interval
        elapsed = 0
        while elapsed < args.post_bios_frames:
            run_for(args.sample_interval)
            elapsed += args.sample_interval
            sample(f"post-bios-{elapsed}")
            if elapsed == halfway:
                mid_pool = analyze_private_pool(
                    read_private_pool(client, addresses[PRIVATE_POOL_SYMBOL])
                )

        final_pool = analyze_private_pool(
            read_private_pool(client, addresses[PRIVATE_POOL_SYMBOL])
        )
        final_refs = decode_refs(
            read_smoke_bytes(
                client,
                cpu_cache_through_alias(addresses[PERIPHERALS_REFS_SYMBOL]),
                PERIPHERALS_REFS_BYTES,
            )
        )
        client.shutdown()
    except BaseException as error:
        failure = error
        if client is not None:
            client.abort()

    valid = [s for s in samples if s["peak_probe"]["magic_valid"]]

    def field_max(section: str, field: str) -> int | None:
        values = [
            s[section][field] for s in valid
            if s[section].get(field) is not None
        ]
        return max(values, default=None)

    peaks = {
        "vdp1_commands_highwater": field_max("peak_probe", "vdp1_commands_highwater"),
        "vdp1_commands_last_max_sampled": field_max("peak_probe", "vdp1_commands_last"),
        "vdp1_gouraud_highwater": field_max("peak_probe", "vdp1_gouraud_highwater"),
        "gfx_pool_entries_highwater": field_max("peak_probe", "gfx_pool_entries_highwater"),
        "gfx_pool_entries_last_max_sampled": field_max("peak_probe", "gfx_pool_entries_last"),
        "gfx_pool_task_count_final": (
            valid[-1]["peak_probe"]["gfx_pool_task_count"] if valid else None
        ),
        "smpc_alloc_count_max_sampled": field_max("smpc_memb", "alloc_count"),
        "smpc_next_index_max_sampled": field_max("smpc_memb", "next_index"),
        "object_pool_peak_allocated": field_max("object_pool", "peak_allocated"),
        "object_pool_alloc_failures": field_max("object_pool", "alloc_failures"),
    }

    report: dict[str, Any] = {
        "evidence_kind": "ymir-sprint2-t2_1-peak-capture",
        "diagnostic_only": True,
        "manual_gui_launch": False,
        "target_build": False,
        "performance_measurement": False,
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
        "sample_interval_frames": args.sample_interval,
        "requested_post_bios_frames": args.post_bios_frames,
        "emulated_frames": emulated_frames,
        "sample_count": len(samples),
        "valid_sample_count": len(valid),
        "samples": samples,
        "private_pool_mid": mid_pool,
        "private_pool_final": final_pool,
        "smpc_refs_final": final_refs,
        "tlsf_control_size": TLSF_CONTROL_SIZE,
        "private_pool_bytes": PRIVATE_POOL_BYTES,
        "peaks": peaks,
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
    print(json.dumps({"peaks": peaks, "acceptance": report["acceptance"]}, indent=2))
    return 1 if failure is not None or not report["acceptance"]["pass"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
