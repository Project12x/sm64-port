#!/usr/bin/env python3
"""Prove that a warm-up puts two builds at the same route position -- or not.

Sprint 2 T2.19d.  The claim tick-based warm-up makes is falsifiable, so it gets
a harness rather than an argument.

For each (CUE, ELF) pair and each warm-up mode this boots Ymir headless, waits
for the exact ELF identity, warms up, and then reads the *simulation* state the
route determines -- the replay tick, the vanilla ``gGlobalTimer``, and
``gMarioState``'s action, position and facing.  Two builds warmed up to the
same route position must agree on all of those; the renderer differs between
them, the simulation does not.

Nothing is rebuilt and no emulator GUI is opened.  Mario's field offsets come
from ``include/types.h`` (``struct MarioState``: action 0x0C, faceAngle 0x2C,
pos 0x3C).
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

import route_warmup  # noqa: E402
from capture_route_counters import PROFILE_BYTES, PROFILE_U32, decode_u32  # noqa: E402
from capture_route_views import YmirClient  # noqa: E402
from capture_sourceboot_boot_trace import run_bios_handoff  # noqa: E402
from capture_sourceboot_throughput import (  # noqa: E402
    _elf32_sections,
    _resolve_symbols_from_bytes,
    build_elf_identity_probe,
    wait_for_target_identity,
)

MARIO_ACTION_OFFSET = 0x0C
MARIO_FACE_ANGLE_OFFSET = 0x2C
MARIO_POS_OFFSET = 0x3C
MARIO_WINDOW_BYTES = 0x48

# The render funnel is the quantity the warm-up defect actually corrupts: VDP1
# plot time is a function of what is on screen, so the command and triangle
# counts are the discriminator that matters, not Mario's coordinates (he is
# still stationary this early in the route).
PROFILE_FIELDS = (
    "frame_serial",
    "command_count",
    "triangle_count",
    "triangles_transformed",
    "triangles_emitted",
    "triangles_vdp1_emitted",
)


def _peek(client: YmirClient, address: int, count: int) -> bytes:
    window = client.call("mem.peek", {"address": address, "count": count})
    data = window.get("data", [])
    if not isinstance(data, list) or len(data) < count:
        raise RuntimeError(f"short peek at {address:#010x}: {len(data)}/{count}")
    return bytes(data[:count])


def read_simulation_state(
    client: YmirClient, addresses: dict[str, int]
) -> dict[str, Any]:
    """Route-determined state: what the simulation is, not how it was drawn."""
    state: dict[str, Any] = route_warmup.read_route_position(client, addresses)
    pointer = int.from_bytes(_peek(client, addresses["mario_state"], 4), "big")
    state["mario_state_pointer"] = hex(pointer)
    if pointer == 0:
        state["mario"] = None
        return state
    state["profile"] = {
        name: value
        for name, value in decode_u32(
            _peek(client, addresses["profile"], PROFILE_BYTES), PROFILE_U32
        ).items()
        if name in PROFILE_FIELDS
    }
    window = _peek(client, pointer, MARIO_WINDOW_BYTES)
    position = struct.unpack(
        ">3f", window[MARIO_POS_OFFSET:MARIO_POS_OFFSET + 12]
    )
    face = struct.unpack(
        ">3h", window[MARIO_FACE_ANGLE_OFFSET:MARIO_FACE_ANGLE_OFFSET + 6]
    )
    state["mario"] = {
        "action": hex(
            int.from_bytes(
                window[MARIO_ACTION_OFFSET:MARIO_ACTION_OFFSET + 4], "big"
            )
        ),
        # Compared as raw bits: two builds at the same route position must be
        # bit-identical here, not merely close.
        "pos_bits": [
            hex(bits)
            for bits in struct.unpack(
                ">3I", window[MARIO_POS_OFFSET:MARIO_POS_OFFSET + 12]
            )
        ],
        "pos": [round(value, 4) for value in position],
        "face_angle": list(face),
    }
    return state


def symbol_address(data: bytes, name: str) -> int:
    """Unique symbol address by name, without asserting a size.

    `sourceboot_fast3d`'s size tracks renderer capacity constants and differs
    between builds, so the size-checked resolver cannot be used for it.
    """
    endian, sections = _elf32_sections(data)
    matches: list[int] = []
    for section in sections:
        if section["type"] != 2:  # SHT_SYMTAB
            continue
        strings = sections[section["link"]]
        table = data[strings["offset"]: strings["offset"] + strings["size"]]
        for relative in range(0, section["size"], section["entry_size"]):
            entry = section["offset"] + relative
            offset = int.from_bytes(data[entry:entry + 4], endian)
            if offset >= len(table):
                continue
            end = table.find(bytes(1), offset)
            symbol = table[offset:end].decode("ascii", errors="replace")
            canonical = symbol[1:] if symbol.startswith("_") else symbol
            if canonical == name:
                matches.append(int.from_bytes(data[entry + 4:entry + 8], endian))
    if len(matches) != 1:
        raise ValueError(f"ELF has {len(matches)} symbols named {name}, expected 1")
    return matches[0]


def resolve_addresses(elf: Path) -> dict[str, int]:
    addresses = route_warmup.resolve_warmup_symbols(elf)
    data = elf.read_bytes()
    resolved = _resolve_symbols_from_bytes(data, {"gMarioState": 4})
    addresses["mario_state"] = int(resolved["gMarioState"]["address"])
    # The profile is sourceboot_fast3d's first member.
    addresses["profile"] = symbol_address(data, "sourceboot_fast3d")
    return addresses


def observe(
    *,
    ymir: Path,
    ipl: Path,
    game: Path,
    elf: Path,
    mode: str,
    amount: int,
    max_vblanks: int,
    timeout: float,
) -> dict[str, Any]:
    addresses = resolve_addresses(elf)
    client = YmirClient(ymir, ipl, game, timeout)
    started = time.time()
    try:
        run_bios_handoff(
            client,
            lambda frames: client.call("exec.run_for", {"frames": frames}),
            lambda _label: None,
        )
        identity = wait_for_target_identity(
            client, build_elf_identity_probe(elf), startup_vblanks=4096
        )
        if mode == "ticks":
            warmup = route_warmup.warm_up_to_replay_tick(
                client, addresses, target_tick=amount, max_vblanks=max_vblanks
            )
        elif mode == "vblanks":
            warmup = route_warmup.warm_up_vblanks(
                client, amount, addresses=addresses
            )
        else:
            raise ValueError(f"unknown warm-up mode {mode!r}")
        state = read_simulation_state(client, addresses)
    finally:
        try:
            client.shutdown()
        except Exception:  # noqa: BLE001 - shutdown is best-effort
            client.abort()
    return {
        "elf": str(elf),
        "game": str(game),
        "identity_vblanks_waited": identity["startup_vblanks_waited"],
        "warmup": warmup,
        "state": state,
        "wall_seconds": round(time.time() - started, 1),
    }


def _comparable(state: dict[str, Any]) -> Any:
    return {
        "replay_ticks": state["replay_ticks"],
        "global_timer": state["global_timer"],
        "mario": state["mario"],
        "profile": state.get("profile"),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ymir", type=Path, required=True)
    parser.add_argument("--ipl", type=Path, required=True)
    parser.add_argument(
        "--build",
        action="append",
        required=True,
        metavar="LABEL=CUE::ELF",
        help="a build to compare; pass at least twice",
    )
    parser.add_argument(
        "--mode",
        action="append",
        required=True,
        metavar="ticks=30|vblanks=1800",
        help="a warm-up to apply to every build; pass at least twice to contrast",
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--max-warmup-vblanks",
        type=int,
        default=route_warmup.DEFAULT_MAX_WARMUP_VBLANKS,
    )
    parser.add_argument("--timeout", type=float, default=2400.0)
    args = parser.parse_args(argv)

    builds: list[dict[str, Any]] = []
    for entry in args.build:
        label, _, paths = entry.partition("=")
        game, _, elf = paths.partition("::")
        if not label or not game or not elf:
            raise ValueError(f"--build must be LABEL=CUE::ELF, got {entry!r}")
        builds.append({"label": label, "game": Path(game), "elf": Path(elf)})
    if len(builds) < 2:
        raise ValueError("pass --build at least twice")

    modes: list[tuple[str, int]] = []
    for entry in args.mode:
        mode, _, amount = entry.partition("=")
        modes.append((mode, int(amount)))

    runs: list[dict[str, Any]] = []
    for mode, amount in modes:
        for build in builds:
            print(f"  {build['label']} @ {mode}={amount} ...", flush=True)
            observation = observe(
                ymir=args.ymir,
                ipl=args.ipl,
                game=build["game"],
                elf=build["elf"],
                mode=mode,
                amount=amount,
                max_vblanks=args.max_warmup_vblanks,
                timeout=args.timeout,
            )
            runs.append(
                {"label": build["label"], "mode": mode, "amount": amount, **observation}
            )

    verdicts = []
    for mode, amount in modes:
        selected = [
            run for run in runs if run["mode"] == mode and run["amount"] == amount
        ]
        states = [_comparable(run["state"]) for run in selected]
        agree = all(state == states[0] for state in states[1:])
        verdicts.append(
            {
                "mode": mode,
                "amount": amount,
                "builds_agree_on_route_position": agree,
                "replay_ticks": [state["replay_ticks"] for state in states],
                "global_timer": [state["global_timer"] for state in states],
                "mario_pos_bits": [
                    None if state["mario"] is None else state["mario"]["pos_bits"]
                    for state in states
                ],
                "profile": [state.get("profile") for state in states],
                "vblanks_advanced": [
                    run["warmup"]["vblanks_advanced"] for run in selected
                ],
            }
        )

    report = {
        "schema": "sm64-saturn-route-warmup-parity-v1",
        "builds": [
            {"label": build["label"], "elf": str(build["elf"])} for build in builds
        ],
        "verdicts": verdicts,
        "runs": runs,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    print()
    for verdict in verdicts:
        status = "SAME" if verdict["builds_agree_on_route_position"] else "DIFFERENT"
        print(
            f"  {verdict['mode']}={verdict['amount']:<6} -> route position "
            f"{status}: replay ticks {verdict['replay_ticks']}, "
            f"VBlanks {verdict['vblanks_advanced']}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
