#!/usr/bin/env python3
"""Map the deterministic route to wall clock, render state and Mario state.

Sprint 2 T2.20.  T2.19d established that ``sState.input_replay_ticks`` indexes
the input-replay route and converted the capture warm-ups to it.  It did not
establish *what is on screen* at any given tick, and every attribution capture
in this sprint warms up to a tick inside the route's opening block.

This tool answers the prerequisite question directly: it boots once, then walks
the route recording, at each sample,

  * ``sState.input_replay_ticks`` (route index), ``gGlobalTimer`` (simulation
    clock) and the VBlanks spent since the ELF identity match (wall clock);
  * ``sState.last_applied_{buttons,stick_x,stick_y}`` -- the route sample the
    runtime actually handed the game this tick, so "is the route driving Mario"
    is observed rather than inferred from the compiled table;
  * ``gMarioState``'s action, position, velocity and facing -- so "Mario moves"
    is a measured displacement, not a spawn pose;
  * ``s_actor_command_count`` / ``s_actor_texture_count`` -- the Mario draw
    slots reserved for the frame currently being built
    (``saturn_demo_render.c:demo_finalize_mario_draws``).  Non-zero *is* Mario
    being plotted; this is the "is Mario rendered" witness; and
  * the cumulative render funnel (``triangles_vdp1_emitted``,
    ``demo_actor_primitives_emitted``, ``demo_bob_results_master/_slave``) so
    terrain and actor command populations can be differenced per route tick
    rather than per wall-clock second.

Sampling is coarse while the simulation has not started (the boot stretch is
~1,530 VBlanks of CD load in which nothing ticks) and fine afterwards.  A frame
is 9-11 VBlanks on the sealed builds, so a 3-VBlank fine stride observes every
route tick at least twice and cannot skip one.

Read-only: no rebuild, no probe, no target source change, no GUI.  Every value
is a ``mem.peek`` of a global that already exists in the shipped ELF.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
import time
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))

from capture_route_views import YmirClient  # noqa: E402
from capture_sourceboot_boot_trace import run_bios_handoff  # noqa: E402
from capture_sourceboot_throughput import (  # noqa: E402
    build_elf_identity_probe,
    wait_for_target_identity,
)

# sm64_saturn_source_runtime_state_t (saturn_source_runtime.h).
STATE_BYTES = 44
STATE_REPLAY_TICKS = 24
STATE_REPLAY_SAMPLE = 28
STATE_BUTTONS = 30
STATE_STICK_X = 32
STATE_STICK_Y = 33

# struct MarioState (include/types.h).  Offsets are the documented ones in the
# declaration comments; the window covers through slideVelZ.
MARIO_BYTES = 0x78
MARIO_ACTION = 0x0C
MARIO_FACE_ANGLE = 0x2C
MARIO_POS = 0x3C
MARIO_VEL = 0x48
MARIO_FORWARD_VEL = 0x54
MARIO_FLOOR_HEIGHT = 0x70

# sm64_saturn_fast3d_profile_t (saturn_fast3d_frontend.h); offsets as parsed by
# capture_terrain_lod_census.py, whose first six reproduce T2.14's exactly.
PROFILE_U32: dict[str, int] = {
    "frame_serial": 0,
    "triangles_transformed": 44,
    "triangles_emitted": 48,
    "triangles_vdp1_emitted": 80,
    "demo_actor_primitives_emitted": 268,
    "vdp1_commands_last": 300,
    "demo_bob_primitives_visible": 316,
    "demo_bob_results_master": 376,
    "demo_bob_results_slave": 380,
    "flat_primitives": 444,
    "gouraud_primitives": 448,
}
PROFILE_BYTES = 452

# saturn_demo_render.c file-scope statics; adjacent, one aligned peek.
ACTOR_BYTES = 4


def peek(client: YmirClient, address: int, count: int) -> bytes:
    window = client.call("mem.peek", {"address": address, "count": count})
    data = window.get("data", [])
    if not isinstance(data, list) or len(data) < count:
        raise RuntimeError(f"short peek at {address:#010x}: {len(data)}/{count} bytes")
    return bytes(data[:count])


def sample(client: YmirClient, addr: dict[str, int]) -> dict[str, Any]:
    state = peek(client, addr["state"], STATE_BYTES)
    row: dict[str, Any] = {
        "replay_ticks": int.from_bytes(
            state[STATE_REPLAY_TICKS:STATE_REPLAY_TICKS + 4], "big"),
        "replay_sample": int.from_bytes(
            state[STATE_REPLAY_SAMPLE:STATE_REPLAY_SAMPLE + 2], "big"),
        "applied_buttons": int.from_bytes(
            state[STATE_BUTTONS:STATE_BUTTONS + 2], "big"),
        "applied_stick_x": struct.unpack(">b", state[STATE_STICK_X:STATE_STICK_X + 1])[0],
        "applied_stick_y": struct.unpack(">b", state[STATE_STICK_Y:STATE_STICK_Y + 1])[0],
        "global_timer": int.from_bytes(peek(client, addr["global_timer"], 4), "big"),
    }
    actor = peek(client, addr["actor"], ACTOR_BYTES)
    row["s_actor_command_count"] = int.from_bytes(actor[0:2], "big")
    row["s_actor_texture_count"] = int.from_bytes(actor[2:4], "big")
    profile = peek(client, addr["profile"], PROFILE_BYTES)
    row["profile"] = {
        name: int.from_bytes(profile[offset:offset + 4], "big")
        for name, offset in PROFILE_U32.items()
    }
    pointer = int.from_bytes(peek(client, addr["mario_state"], 4), "big")
    row["mario_state_pointer"] = hex(pointer)
    if pointer == 0:
        row["mario"] = None
        return row
    window = peek(client, pointer, MARIO_BYTES)
    row["mario"] = {
        "action": hex(int.from_bytes(window[MARIO_ACTION:MARIO_ACTION + 4], "big")),
        "pos": [round(v, 3) for v in struct.unpack(">3f", window[MARIO_POS:MARIO_POS + 12])],
        "vel": [round(v, 4) for v in struct.unpack(">3f", window[MARIO_VEL:MARIO_VEL + 12])],
        "forward_vel": round(
            struct.unpack(">f", window[MARIO_FORWARD_VEL:MARIO_FORWARD_VEL + 4])[0], 4),
        "face_angle": list(
            struct.unpack(">3h", window[MARIO_FACE_ANGLE:MARIO_FACE_ANGLE + 6])),
        "floor_height": round(
            struct.unpack(">f", window[MARIO_FLOOR_HEIGHT:MARIO_FLOOR_HEIGHT + 4])[0], 3),
    }
    return row


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ymir", type=Path, required=True)
    parser.add_argument("--ipl", type=Path, required=True)
    parser.add_argument("--game", type=Path, required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--state-address", type=lambda v: int(v, 0), required=True)
    parser.add_argument("--global-timer-address", type=lambda v: int(v, 0), required=True)
    parser.add_argument("--mario-state-address", type=lambda v: int(v, 0), required=True)
    parser.add_argument("--profile-address", type=lambda v: int(v, 0), required=True)
    parser.add_argument("--actor-address", type=lambda v: int(v, 0), required=True)
    parser.add_argument("--startup-vblanks", type=int, default=4096)
    parser.add_argument("--until-tick", type=int, default=200)
    parser.add_argument("--boot-step", type=int, default=60)
    parser.add_argument("--fine-step", type=int, default=3)
    parser.add_argument("--max-vblanks", type=int, default=20000)
    parser.add_argument("--timeout", type=float, default=3600.0)
    args = parser.parse_args(argv)
    # YmirClient sets cwd=game.parent while still passing the original --game
    # string, so a relative path would be unresolvable from the child's cwd.
    args.ymir = args.ymir.resolve()
    args.ipl = args.ipl.resolve()
    args.game = args.game.resolve()
    args.elf = args.elf.resolve()

    addr = {
        "state": args.state_address,
        "global_timer": args.global_timer_address,
        "mario_state": args.mario_state_address,
        "profile": args.profile_address,
        "actor": args.actor_address,
    }
    started = time.time()
    rows: list[dict[str, Any]] = []
    client = YmirClient(args.ymir, args.ipl, args.game, args.timeout)
    try:
        run_bios_handoff(
            client,
            lambda frames: client.call("exec.run_for", {"frames": frames}),
            lambda _label: None,
        )
        identity = wait_for_target_identity(
            client, build_elf_identity_probe(args.elf),
            startup_vblanks=args.startup_vblanks)
        if not identity.get("match"):
            print("identity mismatch; refusing to report", file=sys.stderr)
            return 2
        vblanks = 0
        row = sample(client, addr)
        row["vblanks_since_identity"] = 0
        rows.append(row)
        while row["replay_ticks"] < args.until_tick and vblanks < args.max_vblanks:
            step = args.boot_step if row["global_timer"] == 0 else args.fine_step
            step = min(step, args.max_vblanks - vblanks)
            if step <= 0:
                break
            client.call("exec.run_for", {"frames": step})
            vblanks += step
            row = sample(client, addr)
            row["vblanks_since_identity"] = vblanks
            rows.append(row)
    finally:
        try:
            client.shutdown()
        except Exception:  # noqa: BLE001 - shutdown is best-effort
            client.abort()

    report = {
        "schema": "sm64-saturn-route-timeline-v1",
        "elf": str(args.elf),
        "game": str(args.game),
        "identity": identity,
        "addresses": {name: hex(value) for name, value in addr.items()},
        "sampling": {
            "startup_vblanks": args.startup_vblanks,
            "until_tick": args.until_tick,
            "boot_step": args.boot_step,
            "fine_step": args.fine_step,
        },
        "wall_seconds": round(time.time() - started, 1),
        "rows": rows,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"rows={len(rows)} vblanks={rows[-1]['vblanks_since_identity']} "
          f"final_tick={rows[-1]['replay_ticks']} wall={report['wall_seconds']}s")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
