#!/usr/bin/env python3
"""Warm a capture up to a fixed point in *simulation* time, not wall time.

Sprint 2 T2.19d.

Every capture in this tree used to warm up by free-running a fixed number of
VBlanks after the ELF identity match (``--warmup-vblanks``, typically 1,800).
A VBlank is wall-clock: a faster build advances further along the deterministic
input-replay route in the same 1,800 VBlanks, so two builds warmed up that way
are **not looking at the same scene**.  T2.17 hit this directly -- its VDP1
plot time appeared to fall from ~9.86 VB to ~8.37 VB purely because the faster
build was sampled further along the route
(``docs/saturn/evidence/reports/sprint2-t2_17-epoch-stall.md`` section 6.2).

The counter that actually indexes route position is the input replay's own
``ticks_consumed``:

  * ``sm64_saturn_input_replay_apply()``
    (``src/port/saturn/runtime/saturn_input_replay.h:52-77``) is the single
    writer.  Each call copies one route sample into the pad, then advances
    ``ticks_in_sample``/``ticks_consumed`` and, at a sample boundary,
    ``sample_index``.  ``ticks_consumed`` and the route sample cursor are
    incremented by the same statement, so it *is* the route index, not a
    correlate of it.
  * ``sm64_saturn_source_runtime_read_controllers()``
    (``src/port/saturn/runtime/saturn_source_runtime.c:86-127``) is the single
    caller, once per ``game_loop_one_iteration()``, and mirrors the value into
    ``sState.input_replay_ticks`` (offset 24 of
    ``sm64_saturn_source_runtime_state_t``,
    ``src/port/saturn/runtime/saturn_source_runtime.h:36``).
  * That same function documents why the *simulation* tick counters are not
    usable for this: it deliberately withholds route samples while
    ``gMarioState == NULL`` because "renderer profiles can make this bootstrap
    interval longer; consuming input there makes the same route begin at
    different gameplay ticks in the interpreted and demo builds"
    (``saturn_source_runtime.c:92-100``).  ``sourceboot_sim_tick_count`` and
    ``gGlobalTimer`` therefore carry a build-dependent offset; the replay tick
    does not.

Two failure modes this module refuses to paper over:

  * The replay tick **saturates**.  ``sm64_saturn_input_replay_apply`` returns
    early once ``complete`` is set, and live-input images stop applying route
    samples at ``SATURN_SOURCEBOOT_LIVE_INPUT_BOOTSTRAP_TICKS``
    (``saturn_source_runtime.c:117-127``).  Past that point the counter is
    frozen forever, so an over-large target would otherwise spin until the
    wall-clock timeout, or -- worse in a chunked loop -- appear to "arrive".
    A target above the build's own bootstrap cap is rejected before the
    emulator is even started, and a runtime plateau raises immediately.
  * A rejected ``exec.run_for``.  Ymir hard-caps a single call at 3,600 frames
    and returns a JSON-RPC error above it; commit ``c28980a`` fixed a silent
    no-op caused by not validating that response.  Every advance here is
    chunked below the cap and issued through ``client.call``, which raises on
    a JSON-RPC error.

**A warm-up that does not reach its target raises.  It never samples early and
returns.**
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Any

# sm64_saturn_source_runtime_state_t, saturn_source_runtime.h.
RUNTIME_STATE_SYMBOL = "sState"
RUNTIME_STATE_BYTES = 44
REPLAY_TICKS_OFFSET = 24

# game_init.c:89; the vanilla simulation frame counter, used only to tell
# "the game has not started ticking yet" apart from "the replay is exhausted".
GLOBAL_TIMER_SYMBOL = "gGlobalTimer"
GLOBAL_TIMER_BYTES = 4

# Ymir's headless debug service hard-caps one exec.run_for call (see
# capture_hwtest.YMIR_MAX_RUN_FOR_FRAMES and commit c28980a).
YMIR_MAX_RUN_FOR_FRAMES = 3600

# At the accepted 5.3538 FPS (id-a61d5203793986e7) and 6.7181 FPS
# (id-c0352f297034f653), the historical 1,800-VBlank warm-up landed on replay
# ticks 25 and 31 respectively -- measured, see the T2.19d report.  30 sits
# inside that band, so tick-warmed captures stay comparable with the VBlank-
# warmed evidence they replace, and it is reachable on every shipped build
# (bootstrap cap 600).
DEFAULT_WARMUP_TICKS = 30

# The gameplay stage does not begin ticking until ~1,530 VBlanks after the
# identity match, so the budget has to cover a long dead boot stretch before
# the first tick plus the route itself.
DEFAULT_MAX_WARMUP_VBLANKS = 20000

# Coarse step used while the simulation has not started; capped by the target
# so a hypothetical fast build cannot jump past a small target during boot.
BOOT_CHUNK_VBLANKS = 30
# Below this many remaining ticks, advance one VBlank at a time so the warm-up
# lands on the target tick exactly.
FINE_TICKS = 4
# Politeness cap on a single coarse step; the *correctness* bound is
# `remaining - FINE_TICKS` VBlanks, which cannot advance more than
# `remaining - FINE_TICKS` ticks even at one VBlank per tick, so no estimate of
# the build's frame length can make the warm-up overshoot.  An earlier revision
# projected the step from a measured VBlank-per-tick rate and overshot by 4
# ticks the first time it ran, because the rate sample that straddled the
# boot/gameplay transition was ~3x too high.
MAX_COARSE_VBLANKS = 600
# Simulation ticks that may elapse with no replay tick before the replay is
# declared exhausted.  Only armed after the first replay tick is observed, so
# the ``gMarioState == NULL`` bootstrap of non-live-input builds (where the
# simulation ticks but route samples are deliberately withheld) is not
# mistaken for saturation.
SATURATION_SIM_TICKS = 8


class WarmupError(RuntimeError):
    """Fail-closed warm-up error carrying the observed target state."""

    def __init__(self, message: str, observation: dict[str, Any]) -> None:
        super().__init__(message)
        self.observation = observation


def _tools_dir() -> str:
    return str(Path(__file__).resolve().parent)


def resolve_warmup_symbols(elf: Path) -> dict[str, int]:
    """Resolve the replay-tick and simulation-clock addresses from the ELF."""
    if _tools_dir() not in sys.path:
        sys.path.insert(0, _tools_dir())
    # Imported lazily: capture_sourceboot_throughput imports this module.
    from capture_sourceboot_throughput import _resolve_symbols_from_bytes

    resolved = _resolve_symbols_from_bytes(
        elf.read_bytes(),
        {
            RUNTIME_STATE_SYMBOL: RUNTIME_STATE_BYTES,
            GLOBAL_TIMER_SYMBOL: GLOBAL_TIMER_BYTES,
        },
    )
    return {
        "replay_ticks": int(resolved[RUNTIME_STATE_SYMBOL]["address"])
        + REPLAY_TICKS_OFFSET,
        "global_timer": int(resolved[GLOBAL_TIMER_SYMBOL]["address"]),
    }


def resolve_tick_cap(build_identity: dict[str, Any] | None) -> int | None:
    """Highest replay tick this build can ever reach, when that is knowable.

    Live-input images stop applying route samples at ``bootstrap_ticks``
    (``saturn_source_runtime.c:117-127``), so the counter cannot exceed it.
    Replay-only images are capped by the route table's own total, which is not
    published in the build identity; those rely on runtime plateau detection.
    """
    if not build_identity:
        return None
    if int(build_identity.get("live_input_mode", 0)) != 1:
        return None
    return int(build_identity.get("bootstrap_ticks", 0))


def validate_warmup_ticks(
    target: int, *, build_identity: dict[str, Any] | None = None
) -> int:
    """Reject an unreachable target before a single emulator frame is spent."""
    if target < 0:
        raise ValueError("warm-up tick target must not be negative")
    cap = resolve_tick_cap(build_identity)
    if cap is not None and target > cap:
        raise ValueError(
            f"warm-up tick target {target} is unreachable: this build applies "
            f"route samples only for its first {cap} ticks "
            f"(live_input_mode=1, bootstrap_ticks={cap})"
        )
    return target


def _peek_u32(client: Any, address: int) -> int:
    window = client.call("mem.peek", {"address": address, "count": 4})
    data = window.get("data", [])
    if not isinstance(data, list) or len(data) < 4:
        raise WarmupError(
            f"short peek at {address:#010x}: {len(data)}/4 bytes",
            {"address": address},
        )
    return int.from_bytes(bytes(data[:4]), "big")


def read_route_position(client: Any, addresses: dict[str, int]) -> dict[str, int]:
    """One (replay tick, simulation tick) observation of route position."""
    return {
        "replay_ticks": _peek_u32(client, addresses["replay_ticks"]),
        "global_timer": _peek_u32(client, addresses["global_timer"]),
    }


def run_vblanks(client: Any, frames: int) -> int:
    """Advance `frames` VBlanks in validated chunks below Ymir's call cap."""
    if frames < 0:
        raise ValueError("VBlank count must not be negative")
    remaining = frames
    while remaining > 0:
        chunk = min(remaining, YMIR_MAX_RUN_FOR_FRAMES)
        # client.call raises on a JSON-RPC error, so a rejected chunk can never
        # be mistaken for a completed advance (commit c28980a).
        client.call("exec.run_for", {"frames": chunk})
        remaining -= chunk
    return frames


def warm_up_vblanks(
    client: Any, vblanks: int, *, addresses: dict[str, int] | None = None
) -> dict[str, Any]:
    """Deprecated wall-clock warm-up, retained so old commands reproduce.

    It still records the replay tick it happened to land on, so every report
    written through the legacy path documents its own route position instead
    of leaving it unknowable.
    """
    run_vblanks(client, vblanks)
    record: dict[str, Any] = {
        "mode": "vblanks-deprecated",
        "warmup_vblanks": vblanks,
        "vblanks_advanced": vblanks,
        "deprecation": (
            "VBlank warm-up is wall-clock: builds of different speed land on "
            "different route positions. Prefer --warmup-ticks (T2.19d)."
        ),
    }
    if addresses is not None:
        record.update(read_route_position(client, addresses))
    return record


def warm_up_to_replay_tick(
    client: Any,
    addresses: dict[str, int],
    *,
    target_tick: int,
    max_vblanks: int = DEFAULT_MAX_WARMUP_VBLANKS,
) -> dict[str, Any]:
    """Advance until the replay tick reaches `target_tick`, or raise.

    Never returns having sampled early: every exit that is not an exact
    arrival raises WarmupError with the observed counters attached.
    """
    if target_tick < 0:
        raise ValueError("warm-up tick target must not be negative")
    if max_vblanks < 0:
        raise ValueError("warm-up VBlank budget must not be negative")

    position = read_route_position(client, addresses)
    start = dict(position)
    if position["replay_ticks"] > target_tick:
        raise WarmupError(
            f"replay tick {position['replay_ticks']} is already past the "
            f"warm-up target {target_tick} before the warm-up began",
            {"target_tick": target_tick, "vblanks_advanced": 0, **position},
        )

    advanced = 0
    steps = 0
    # Armed only once a replay tick has actually been consumed; see
    # SATURATION_SIM_TICKS.
    saturation_anchor: int | None = None
    last = dict(position)

    while position["replay_ticks"] < target_tick:
        remaining = target_tick - position["replay_ticks"]
        if position["replay_ticks"] == 0 and position["global_timer"] == 0:
            # Nothing can be reached while the simulation has not started;
            # bounded by the target so even a one-VBlank-per-tick build cannot
            # jump past it here.
            chunk = min(BOOT_CHUNK_VBLANKS, max(1, remaining))
        elif remaining <= FINE_TICKS:
            chunk = 1
        else:
            chunk = max(1, min(MAX_COARSE_VBLANKS, remaining - FINE_TICKS))
        if advanced + chunk > max_vblanks:
            chunk = max_vblanks - advanced
        if chunk <= 0:
            raise WarmupError(
                f"warm-up did not reach replay tick {target_tick} within its "
                f"{max_vblanks}-VBlank budget (reached tick "
                f"{position['replay_ticks']}, simulation tick "
                f"{position['global_timer']})",
                {
                    "target_tick": target_tick,
                    "vblanks_advanced": advanced,
                    "steps": steps,
                    **position,
                },
            )

        run_vblanks(client, chunk)
        advanced += chunk
        steps += 1
        position = read_route_position(client, addresses)

        tick_delta = position["replay_ticks"] - last["replay_ticks"]
        if tick_delta < 0:
            raise WarmupError(
                f"replay tick went backwards ({last['replay_ticks']} -> "
                f"{position['replay_ticks']}); the target was reset or the "
                f"address is wrong",
                {
                    "target_tick": target_tick,
                    "vblanks_advanced": advanced,
                    "steps": steps,
                    **position,
                },
            )
        if tick_delta > 0:
            saturation_anchor = position["global_timer"]
        elif position["replay_ticks"] > 0:
            if saturation_anchor is None:
                saturation_anchor = last["global_timer"]
            if position["global_timer"] - saturation_anchor >= SATURATION_SIM_TICKS:
                raise WarmupError(
                    f"replay tick is frozen at {position['replay_ticks']} while "
                    f"the simulation kept ticking "
                    f"({position['global_timer'] - saturation_anchor} ticks "
                    f"with no route sample consumed): the route is exhausted, "
                    f"so warm-up target {target_tick} is unreachable",
                    {
                        "target_tick": target_tick,
                        "vblanks_advanced": advanced,
                        "steps": steps,
                        **position,
                    },
                )
        last = dict(position)

    if position["replay_ticks"] != target_tick:
        raise WarmupError(
            f"warm-up overshot its target: landed on replay tick "
            f"{position['replay_ticks']}, wanted {target_tick}",
            {
                "target_tick": target_tick,
                "vblanks_advanced": advanced,
                "steps": steps,
                **position,
            },
        )

    return {
        "mode": "ticks",
        "target_tick": target_tick,
        "replay_ticks": position["replay_ticks"],
        "global_timer": position["global_timer"],
        "vblanks_advanced": advanced,
        "steps": steps,
        "start_replay_ticks": start["replay_ticks"],
        "start_global_timer": start["global_timer"],
        "max_vblanks": max_vblanks,
        "addresses": {name: hex(value) for name, value in addresses.items()},
    }


def add_warmup_arguments(
    parser: argparse.ArgumentParser, *, default_ticks: int = DEFAULT_WARMUP_TICKS
) -> None:
    """Add the tick-based warm-up flags plus the deprecated VBlank flag."""
    parser.add_argument(
        "--warmup-ticks",
        type=int,
        default=None,
        help=(
            "advance until the input replay has consumed this many route ticks "
            f"(default: {default_ticks}). This is simulation time, so two "
            "builds of different speed are sampled at the same route position. "
            "0 disables the warm-up."
        ),
    )
    parser.add_argument(
        "--max-warmup-vblanks",
        type=int,
        default=DEFAULT_MAX_WARMUP_VBLANKS,
        help=(
            "bound on the VBlanks the tick warm-up may spend; exceeding it is "
            f"a hard failure (default: {DEFAULT_MAX_WARMUP_VBLANKS})"
        ),
    )
    parser.add_argument(
        "--warmup-vblanks",
        type=int,
        default=None,
        help=(
            "DEPRECATED (T2.19d): free-run this many VBlanks instead of warming "
            "up to a route tick. Wall-clock, so it puts different-speed builds "
            "at different route positions. Honoured unchanged for reproducing "
            "existing evidence; mutually exclusive with --warmup-ticks."
        ),
    )
    # Carried on the namespace so plan_warmup can tell "flag omitted" from
    # "flag passed with the default value" without inspecting sys.argv.
    parser.set_defaults(warmup_default_ticks=default_ticks)


def plan_warmup(args: argparse.Namespace, elf: Path) -> dict[str, Any]:
    """Resolve flags to a warm-up plan and reject unreachable targets early.

    Called before Ymir is started so an impossible target costs no emulation.
    """
    if args.warmup_vblanks is not None and args.warmup_ticks is not None:
        raise ValueError(
            "--warmup-vblanks (deprecated) and --warmup-ticks are mutually "
            "exclusive; pass only one"
        )

    addresses: dict[str, int] | None
    symbol_error: str | None = None
    try:
        addresses = resolve_warmup_symbols(elf)
    except (ValueError, OSError) as error:
        addresses = None
        symbol_error = str(error)

    if args.warmup_vblanks is not None:
        if args.warmup_vblanks < 0:
            raise ValueError("--warmup-vblanks must not be negative")
        print(
            "warning: --warmup-vblanks is deprecated (T2.19d). It warms up by "
            "wall clock, so a faster build lands further along the route. Use "
            "--warmup-ticks for cross-build comparisons.",
            file=sys.stderr,
        )
        return {
            "mode": "vblanks-deprecated",
            "warmup_vblanks": args.warmup_vblanks,
            "addresses": addresses,
            "symbol_error": symbol_error,
        }

    target = (
        int(args.warmup_ticks)
        if args.warmup_ticks is not None
        else int(getattr(args, "warmup_default_ticks", DEFAULT_WARMUP_TICKS))
    )
    if target and addresses is None:
        raise ValueError(
            f"tick-based warm-up needs the {RUNTIME_STATE_SYMBOL}/"
            f"{GLOBAL_TIMER_SYMBOL} symbols from {elf}: {symbol_error}"
        )
    build_identity = _build_identity(elf)
    validate_warmup_ticks(target, build_identity=build_identity)
    return {
        "mode": "ticks",
        "target_tick": target,
        "max_vblanks": int(getattr(args, "max_warmup_vblanks", DEFAULT_MAX_WARMUP_VBLANKS)),
        "addresses": addresses,
        "symbol_error": symbol_error,
        "tick_cap": resolve_tick_cap(build_identity),
    }


def _build_identity(elf: Path) -> dict[str, Any] | None:
    if _tools_dir() not in sys.path:
        sys.path.insert(0, _tools_dir())
    try:
        from capture_sourceboot_throughput import build_elf_build_identity_probe

        return build_elf_build_identity_probe(elf)["identity"]
    except Exception:  # noqa: BLE001 - an unreadable identity only loses a pre-flight check
        return None


def execute_warmup(client: Any, plan: dict[str, Any]) -> dict[str, Any]:
    """Run the planned warm-up; raise rather than sample at the wrong place."""
    if plan["mode"] == "vblanks-deprecated":
        return warm_up_vblanks(
            client, plan["warmup_vblanks"], addresses=plan["addresses"]
        )
    if plan["target_tick"] == 0:
        record: dict[str, Any] = {
            "mode": "ticks",
            "target_tick": 0,
            "vblanks_advanced": 0,
            "steps": 0,
            "note": "warm-up disabled (--warmup-ticks 0)",
        }
        if plan["addresses"] is not None:
            record.update(read_route_position(client, plan["addresses"]))
        return record
    return warm_up_to_replay_tick(
        client,
        plan["addresses"],
        target_tick=plan["target_tick"],
        max_vblanks=plan["max_vblanks"],
    )
