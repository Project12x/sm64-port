#!/usr/bin/env python3
"""Decide why Mario faces the wrong way, by measurement rather than argument.

Reported 2026-09-03: Mario faces the wrong direction moving forward or
backward, but the correct direction moving left or right.

Four candidates were ruled out first, each by checking rather than reasoning:

  * the yaw rotation in demo_transform_mario_vertex is term-for-term identical
    to the generic-actor kernel (saturn_actor_meshlets.c actor_depth_fast) and
    to castleviewer's mario_world_point -- x' = x*cos + z*sin,
    z' = z*cos - x*sin in all three;
  * gSaturnSineTableQ16 matches true sin/cos to 1e-5 across the whole circle
    (5120 entries, cosine at +0x400);
  * marioturntable's +0x8000 is its own camera framing (its camera sits on -Z),
    not a model correction -- castleviewer applies no offset;
  * flipping the Mario back-face cull sign changed nothing observable, and a
    cull cannot rotate a model anyway.

That leaves two questions source cannot answer, which this probe separates:

  1. Is the SIMULATION self-consistent?  SM64 defines forward as
     (sins(yaw), coss(yaw)), so atan2s(vel_x, vel_z) must equal faceAngle
     whenever Mario is moving. A mismatch means the port's game state is
     wrong and rendering is innocent.
  2. Does the RENDER angle match the simulation?  The snapshot feeds the
     renderer faceAngle[1], but SM64 draws the graph object's gfx.angle[1].
     A mismatch means the renderer is faithfully drawing the wrong angle.

Read-only: no target memory is written.
"""
from __future__ import annotations

import argparse
import json
import math
import subprocess
import sys
from pathlib import Path
from typing import Any

from capture_route_views import YmirClient
from capture_sourceboot_boot_trace import run_bios_handoff
from probe_audio_mailbox import peek

PROBE_SYMBOL = "g_sm64_saturn_mario_orient_probe"
PROBE_MAGIC = 0x4D4F5249

FIELDS = ("magic", "samples", "face_angle", "gfx_angle", "camera_yaw",
          "vel_x", "vel_z", "forward_vel", "action",
          "pos_x", "pos_z", "cam_pos_x", "cam_pos_z",
          "cam_focus_x", "cam_focus_z")
PROBE_BYTES = len(FIELDS) * 4
MAX_FRAMES_PER_CALL = 3600  # Ymir silently no-ops exec.run_for above this


def resolve_symbol(nm: Path, elf: Path, symbol: str) -> int:
    out = subprocess.run([str(nm), str(elf)], capture_output=True, text=True,
                         check=True).stdout
    wanted = {symbol, f"_{symbol}"}       # sh-elf prefixes C symbols
    matches = [line.split()[0] for line in out.splitlines()
               if line.split() and line.split()[-1] in wanted]
    if not matches:
        raise SystemExit(f"{symbol} is not in {elf}; build with the probe applied.")
    return int(matches[0], 16)


def be32(data: list[int], off: int) -> int:
    return ((data[off] << 24) | (data[off + 1] << 16) |
            (data[off + 2] << 8) | data[off + 3])


def s32(value: int) -> int:
    return value - (1 << 32) if value >= (1 << 31) else value


def read_probe(client: YmirClient, address: int) -> dict[str, int]:
    raw = peek(client, address, PROBE_BYTES)
    return {name: be32(raw, i * 4) for i, name in enumerate(FIELDS)}


def advance(client: YmirClient, frames: int) -> None:
    while frames > 0:
        step = min(frames, MAX_FRAMES_PER_CALL)
        client.call("exec.run_for", {"frames": step})
        frames -= step


def deg(angle_u16: int) -> float:
    return (angle_u16 & 0xFFFF) * 360.0 / 65536.0


def atan2s_deg(x: int, z: int) -> float | None:
    """SM64's forward is (sins(yaw), coss(yaw)), so the heading implied by a
    velocity is atan2(x, z) -- x first, not the usual y first."""
    if x == 0 and z == 0:
        return None
    return math.degrees(math.atan2(x, z)) % 360.0


def wrap180(d: float) -> float:
    return (d + 180.0) % 360.0 - 180.0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ymir", type=Path, default=Path(
        r"D:\Code\RetroDev\sm64-saturn-port\ymir-agent\build-agent2"
        r"\apps\ymir-headless\Release\ymir-headless.exe"))
    parser.add_argument("--ipl", type=Path, required=True)
    parser.add_argument("--cue", type=Path, required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--nm", type=Path, default=Path(
        r"D:\Code\RetroDev\sm64-saturn-port\work\yaul-install\bin\sh-elf-nm.exe"))
    parser.add_argument("--warmup-frames", type=int, default=3000)
    parser.add_argument("--interval-frames", type=int, default=240)
    parser.add_argument("--samples", type=int, default=24)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--timeout", type=int, default=1800)
    args = parser.parse_args()

    address = resolve_symbol(args.nm.resolve(), args.elf.resolve(), PROBE_SYMBOL)
    print(f"{PROBE_SYMBOL} @ 0x{address:08X}")

    client = YmirClient(args.ymir.resolve(), args.ipl.resolve(),
                        args.cue.resolve(), args.timeout)
    rows: list[dict[str, Any]] = []
    try:
        run_bios_handoff(
            client, lambda f: client.call("exec.run_for", {"frames": f}),
            lambda _label: None)
        advance(client, args.warmup_frames)
        frame = args.warmup_frames
        for _ in range(args.samples):
            v = read_probe(client, address)
            v["frame"] = frame
            rows.append(v)
            advance(client, args.interval_frames)
            frame += args.interval_frames
    finally:
        try:
            client.shutdown()
        except (RuntimeError, TimeoutError):
            client.abort()

    if not any(r["magic"] == PROBE_MAGIC for r in rows):
        print("\nPROBE NEVER ARMED: sm64_saturn_mario_actor_snapshot() never ran.")
        return 0

    print(f"\n{'frame':>7} {'face':>8} {'fwd':>6} {'head-err':>9} "
          f"{'look':>8} {'cam->M':>8} {'face-look':>10} {'behind?':>8}")
    sim_err: list[float] = []
    gfx_err: list[float] = []
    lead_lag: list[float] = []
    for r in rows:
        if r["magic"] != PROBE_MAGIC:
            continue
        face, gfx = deg(r["face_angle"]), r["gfx_angle"]
        vx, vz, fwd = s32(r["vel_x"]), s32(r["vel_z"]), s32(r["forward_vel"])
        px, pz = s32(r["pos_x"]), s32(r["pos_z"])
        cx, cz = s32(r["cam_pos_x"]), s32(r["cam_pos_z"])
        fx, fz = s32(r["cam_focus_x"]), s32(r["cam_focus_z"])

        # SM64: vel = forwardVel * (sins(face), coss(face)). A NEGATIVE
        # forwardVel therefore puts the heading 180 deg from the facing and is
        # perfectly normal (backing up). Fold that in rather than flagging it.
        head = atan2s_deg(vx, vz)
        expect = face + (180.0 if fwd < 0 else 0.0)
        head_err = None if head is None else wrap180(head - expect)

        look = atan2s_deg(fx - cx, fz - cz)       # where the camera looks
        cam_to_m = atan2s_deg(px - cx, pz - cz)   # where Mario is from it
        face_look = None if look is None else wrap180(face - look)

        f_s = "     -" if head_err is None else f"{head_err:9.1f}"
        l_s = "       -" if look is None else f"{look:8.1f}"
        c_s = "       -" if cam_to_m is None else f"{cam_to_m:8.1f}"
        d_s = "         -" if face_look is None else f"{face_look:10.1f}"
        # Camera TRAILS when Mario faces roughly the way the camera looks.
        b_s = "  -" if face_look is None else ("   trail" if abs(face_look) < 90 else "    LEAD")
        print(f"{r['frame']:>7} {face:8.1f} {fwd:>6} {f_s} {l_s} {c_s} {d_s} {b_s}")

        if head_err is not None and abs(fwd) > 8:
            sim_err.append(abs(head_err))
        if gfx != 0xFFFFFFFF:
            gfx_err.append(abs(wrap180(deg(gfx) - face)))
        if face_look is not None and fwd > 8:
            lead_lag.append(abs(face_look))

    print()
    if sim_err:
        worst = max(sim_err)
        print(f"(1) simulation: heading error vs forwardVel-signed facing, max "
              f"{worst:.1f} deg over {len(sim_err)} moving samples -> "
              f"{'CONSISTENT' if worst < 20 else 'INCONSISTENT: game state disagrees'}")
    else:
        print("(1) simulation: Mario never moved fast enough in the window.")
    if gfx_err:
        worst = max(gfx_err)
        print(f"(2) render angle: |gfx.angle - faceAngle| max {worst:.1f} deg -> "
              f"{'SAME ANGLE' if worst < 1 else 'DIFFERENT: the renderer is fed the wrong one'}")
        if worst > 160:
            print("    ~180 deg: SM64 draws gfx.angle; the snapshot sends faceAngle.")
    else:
        print("(2) render angle: no graph object in the sampled window.")

    if lead_lag:
        mean = sum(lead_lag) / len(lead_lag)
        print(f"(3) camera frame: |facing - camera look| mean {mean:.1f} deg over "
              f"{len(lead_lag)} forward-running samples")
        if mean < 90:
            print("    TRAILS -- running forward shows Mario's back, as SM64 does.")
            print("    The camera is not the cause; look elsewhere.")
        else:
            print("    LEADS -- the camera stands in FRONT of him, so running")
            print("    forward shows his FACE while profile still reads right.")
            print("    That is the reported symptom, and it is a camera bug.")
    else:
        print("(3) camera frame: no forward-running samples to judge.")

    if args.output:
        args.output.write_text(json.dumps(
            {"symbol": PROBE_SYMBOL, "address": address,
             "elf": str(args.elf), "samples": rows}, indent=1), encoding="utf-8")
        print(f"\nwrote {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
