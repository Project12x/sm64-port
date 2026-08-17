#!/usr/bin/env python3
"""Host contracts for the tick-based capture warm-up (Sprint 2 T2.19d).

The property under test is narrow and load-bearing: a warm-up either lands on
its exact target replay tick, or it raises.  There is no third outcome -- in
particular it must never sample early and return, which is the failure mode
commit c28980a paid for once already.
"""

from __future__ import annotations

import argparse
import sys
import unittest
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import route_warmup  # noqa: E402


REPLAY_ADDRESS = 0x060F0D28 + route_warmup.REPLAY_TICKS_OFFSET
TIMER_ADDRESS = 0x060BCD64
ADDRESSES = {"replay_ticks": REPLAY_ADDRESS, "global_timer": TIMER_ADDRESS}


class FakeTarget:
    """A target whose simulation starts late and then ticks at a fixed rate.

    `boot_vblanks` reproduces the measured ~1,530-VBlank dead stretch between
    the ELF identity match and the first gameplay tick; `vblanks_per_tick`
    reproduces the per-build frame length (11.0 for id-a61d5203793986e7, 9.0
    for id-c0352f297034f653).  `replay_cap` reproduces the live-input
    bootstrap ceiling past which `ticks_consumed` is frozen forever.
    """

    def __init__(
        self,
        *,
        boot_vblanks: int = 1530,
        vblanks_per_tick: float = 11.0,
        replay_cap: int | None = None,
        replay_starts_at_sim_tick: int = 0,
        ticks_per_step: int = 1,
    ) -> None:
        self.boot_vblanks = boot_vblanks
        self.vblanks_per_tick = vblanks_per_tick
        self.replay_cap = replay_cap
        self.replay_starts_at_sim_tick = replay_starts_at_sim_tick
        # >1 models a target whose counter advances by more than one per
        # observable step -- the only way the loop can land past its target.
        self.ticks_per_step = ticks_per_step
        self.vblanks = 0
        self.run_for_calls: list[int] = []
        self.peeks = 0

    @property
    def global_timer(self) -> int:
        if self.vblanks <= self.boot_vblanks:
            return 0
        return (
            int((self.vblanks - self.boot_vblanks) / self.vblanks_per_tick)
            * self.ticks_per_step
        )

    @property
    def replay_ticks(self) -> int:
        ticks = max(0, self.global_timer - self.replay_starts_at_sim_tick)
        if self.replay_cap is not None:
            ticks = min(ticks, self.replay_cap)
        return ticks

    def call(self, method: str, params: dict[str, int]) -> dict[str, object]:
        if method == "exec.run_for":
            frames = params["frames"]
            if not 1 <= frames <= route_warmup.YMIR_MAX_RUN_FOR_FRAMES:
                raise RuntimeError(f"Ymir rejects exec.run_for frames={frames}")
            self.run_for_calls.append(frames)
            self.vblanks += frames
            return {}
        if method == "mem.peek":
            self.peeks += 1
            address = params["address"]
            if address == REPLAY_ADDRESS:
                value = self.replay_ticks
            elif address == TIMER_ADDRESS:
                value = self.global_timer
            else:
                raise AssertionError(f"unexpected peek at {address:#x}")
            return {"data": list(value.to_bytes(4, "big"))}
        raise AssertionError(method)


class WarmupToTickTests(unittest.TestCase):
    def test_lands_on_the_exact_target_tick(self) -> None:
        target = FakeTarget()
        record = route_warmup.warm_up_to_replay_tick(
            target, ADDRESSES, target_tick=30
        )
        self.assertEqual(record["replay_ticks"], 30)
        self.assertEqual(record["target_tick"], 30)
        self.assertEqual(target.replay_ticks, 30)

    def test_two_speeds_land_on_the_same_route_tick_at_different_vblanks(self) -> None:
        """The whole point: same route position, different wall clock."""
        slow = FakeTarget(vblanks_per_tick=11.0)
        fast = FakeTarget(vblanks_per_tick=9.0)
        slow_record = route_warmup.warm_up_to_replay_tick(
            slow, ADDRESSES, target_tick=30
        )
        fast_record = route_warmup.warm_up_to_replay_tick(
            fast, ADDRESSES, target_tick=30
        )
        self.assertEqual(slow_record["replay_ticks"], fast_record["replay_ticks"])
        self.assertNotEqual(
            slow_record["vblanks_advanced"], fast_record["vblanks_advanced"]
        )

    def test_fixed_vblank_warmup_puts_the_two_speeds_at_different_ticks(self) -> None:
        """The defect this replaces, reproduced on the same fake targets."""
        slow = FakeTarget(vblanks_per_tick=11.0)
        fast = FakeTarget(vblanks_per_tick=9.0)
        slow_record = route_warmup.warm_up_vblanks(slow, 1800, addresses=ADDRESSES)
        fast_record = route_warmup.warm_up_vblanks(fast, 1800, addresses=ADDRESSES)
        self.assertEqual(
            slow_record["vblanks_advanced"], fast_record["vblanks_advanced"]
        )
        self.assertNotEqual(
            slow_record["replay_ticks"], fast_record["replay_ticks"]
        )

    def test_target_beyond_the_route_raises_instead_of_sampling_early(self) -> None:
        target = FakeTarget(replay_cap=40)
        with self.assertRaises(route_warmup.WarmupError) as caught:
            route_warmup.warm_up_to_replay_tick(target, ADDRESSES, target_tick=100)
        self.assertIn("frozen at 40", str(caught.exception))
        self.assertEqual(caught.exception.observation["target_tick"], 100)

    def test_exhausted_vblank_budget_raises(self) -> None:
        target = FakeTarget()
        with self.assertRaises(route_warmup.WarmupError) as caught:
            route_warmup.warm_up_to_replay_tick(
                target, ADDRESSES, target_tick=30, max_vblanks=200
            )
        self.assertIn("within its 200-VBlank budget", str(caught.exception))
        self.assertLessEqual(target.vblanks, 200)

    def test_never_returns_short_of_the_target(self) -> None:
        """Mutation guard: any early return must be a raise, at every target."""
        for tick in (1, 2, 5, 17, 30, 64):
            with self.subTest(tick=tick):
                target = FakeTarget()
                record = route_warmup.warm_up_to_replay_tick(
                    target, ADDRESSES, target_tick=tick
                )
                self.assertEqual(record["replay_ticks"], tick)

    def test_off_by_one_targets_are_distinguishable(self) -> None:
        first = FakeTarget()
        second = FakeTarget()
        low = route_warmup.warm_up_to_replay_tick(first, ADDRESSES, target_tick=29)
        high = route_warmup.warm_up_to_replay_tick(second, ADDRESSES, target_tick=30)
        self.assertEqual(low["replay_ticks"], 29)
        self.assertEqual(high["replay_ticks"], 30)
        self.assertLess(low["vblanks_advanced"], high["vblanks_advanced"])

    def test_overshoot_raises_rather_than_reporting_the_wrong_tick(self) -> None:
        """A counter that steps by 3 cannot land on 29; that must not pass."""
        target = FakeTarget(vblanks_per_tick=1.0, ticks_per_step=3)
        with self.assertRaisesRegex(route_warmup.WarmupError, "overshot"):
            route_warmup.warm_up_to_replay_tick(target, ADDRESSES, target_tick=29)

    def test_starting_past_the_target_raises(self) -> None:
        target = FakeTarget()
        route_warmup.run_vblanks(target, 1900)
        with self.assertRaisesRegex(route_warmup.WarmupError, "already past"):
            route_warmup.warm_up_to_replay_tick(target, ADDRESSES, target_tick=1)

    def test_zero_target_is_a_no_op_that_still_reports_position(self) -> None:
        target = FakeTarget()
        record = route_warmup.warm_up_to_replay_tick(target, ADDRESSES, target_tick=0)
        self.assertEqual(record["replay_ticks"], 0)
        self.assertEqual(record["vblanks_advanced"], 0)
        self.assertEqual(target.run_for_calls, [])

    def test_late_replay_start_is_not_mistaken_for_saturation(self) -> None:
        """Replay-only builds withhold route samples while gMarioState is NULL."""
        target = FakeTarget(replay_starts_at_sim_tick=40)
        record = route_warmup.warm_up_to_replay_tick(
            target, ADDRESSES, target_tick=10
        )
        self.assertEqual(record["replay_ticks"], 10)
        self.assertGreaterEqual(record["global_timer"], 50)

    def test_every_advance_stays_inside_ymirs_run_for_cap(self) -> None:
        target = FakeTarget(boot_vblanks=12000)
        route_warmup.warm_up_to_replay_tick(
            target, ADDRESSES, target_tick=5, max_vblanks=40000
        )
        self.assertTrue(target.run_for_calls)
        self.assertLessEqual(
            max(target.run_for_calls), route_warmup.YMIR_MAX_RUN_FOR_FRAMES
        )

    def test_a_rejected_run_for_chunk_propagates(self) -> None:
        class Rejecting(FakeTarget):
            def call(self, method: str, params: dict[str, int]):
                if method == "exec.run_for":
                    raise RuntimeError("Ymir request 7 failed: frames out of range")
                return super().call(method, params)

        with self.assertRaisesRegex(RuntimeError, "frames out of range"):
            route_warmup.warm_up_to_replay_tick(
                Rejecting(), ADDRESSES, target_tick=30
            )

    def test_short_peek_raises(self) -> None:
        class Short(FakeTarget):
            def call(self, method: str, params: dict[str, int]):
                if method == "mem.peek":
                    return {"data": [0, 0]}
                return super().call(method, params)

        with self.assertRaisesRegex(route_warmup.WarmupError, "short peek"):
            route_warmup.warm_up_to_replay_tick(Short(), ADDRESSES, target_tick=30)


class StructLayoutTests(unittest.TestCase):
    """Pin the replay-tick offset to the struct, not to a comment.

    Every field before `input_replay_ticks` in
    `sm64_saturn_source_runtime_state_t` is a uint32_t, so its offset is
    4 * (field index).  If the struct gains, loses or reorders a field, this
    fails instead of the capture silently peeking the wrong counter.
    """

    HEADER = (
        TOOLS_DIR.parents[1]
        / "src" / "port" / "saturn" / "runtime" / "saturn_source_runtime.h"
    )

    def test_replay_tick_offset_matches_the_runtime_state_struct(self) -> None:
        text = self.HEADER.read_text(encoding="utf-8")
        start = text.index("typedef struct sm64_saturn_source_runtime_state {")
        body = text[start:text.index("}", start)]
        fields: list[tuple[str, str]] = []
        for line in body.splitlines()[1:]:
            stripped = line.strip()
            if not stripped.endswith(";") or stripped.startswith(("/*", "*")):
                continue
            parts = stripped[:-1].split()
            if len(parts) != 2:
                continue
            fields.append((parts[0], parts[1]))
        names = [name for _type, name in fields]
        self.assertIn("input_replay_ticks", names)
        index = names.index("input_replay_ticks")
        self.assertTrue(
            all(field_type == "uint32_t" for field_type, _ in fields[:index]),
            "a non-uint32_t field appeared before input_replay_ticks; the "
            "offset must be recomputed",
        )
        self.assertEqual(route_warmup.REPLAY_TICKS_OFFSET, 4 * index)
        self.assertGreaterEqual(route_warmup.RUNTIME_STATE_BYTES, 4 * (index + 1))


class TickCapTests(unittest.TestCase):
    LIVE = {"live_input_mode": 1, "bootstrap_ticks": 600}
    REPLAY_ONLY = {"live_input_mode": 0, "bootstrap_ticks": 600}

    def test_live_input_cap_rejects_unreachable_target_before_emulating(self) -> None:
        with self.assertRaisesRegex(ValueError, "unreachable"):
            route_warmup.validate_warmup_ticks(601, build_identity=self.LIVE)

    def test_target_at_the_cap_is_accepted(self) -> None:
        self.assertEqual(
            route_warmup.validate_warmup_ticks(600, build_identity=self.LIVE), 600
        )

    def test_replay_only_builds_have_no_host_visible_cap(self) -> None:
        self.assertIsNone(route_warmup.resolve_tick_cap(self.REPLAY_ONLY))
        self.assertEqual(
            route_warmup.validate_warmup_ticks(5000, build_identity=self.REPLAY_ONLY),
            5000,
        )

    def test_negative_target_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "must not be negative"):
            route_warmup.validate_warmup_ticks(-1)


class WarmupPlanTests(unittest.TestCase):
    def _parser(self, default_ticks: int = 30) -> argparse.ArgumentParser:
        parser = argparse.ArgumentParser()
        route_warmup.add_warmup_arguments(parser, default_ticks=default_ticks)
        return parser

    def test_default_is_tick_based(self) -> None:
        args = self._parser().parse_args([])
        self.assertIsNone(args.warmup_vblanks)
        self.assertIsNone(args.warmup_ticks)
        self.assertEqual(args.warmup_default_ticks, 30)

    def test_deprecated_flag_and_tick_flag_are_mutually_exclusive(self) -> None:
        args = self._parser().parse_args(["--warmup-vblanks", "1800", "--warmup-ticks", "30"])
        with self.assertRaisesRegex(ValueError, "mutually exclusive"):
            route_warmup.plan_warmup(args, Path("missing.elf"))

    def test_deprecated_flag_still_selects_the_legacy_path(self) -> None:
        args = self._parser().parse_args(["--warmup-vblanks", "1800"])
        plan = route_warmup.plan_warmup(args, Path("missing.elf"))
        self.assertEqual(plan["mode"], "vblanks-deprecated")
        self.assertEqual(plan["warmup_vblanks"], 1800)

    def test_legacy_execution_advances_exactly_the_requested_vblanks(self) -> None:
        args = self._parser().parse_args(["--warmup-vblanks", "1800"])
        plan = route_warmup.plan_warmup(args, Path("missing.elf"))
        target = FakeTarget()
        record = route_warmup.execute_warmup(target, plan)
        self.assertEqual(target.vblanks, 1800)
        self.assertEqual(record["mode"], "vblanks-deprecated")
        self.assertEqual(record["vblanks_advanced"], 1800)

    def test_legacy_execution_chunks_below_the_run_for_cap(self) -> None:
        args = self._parser().parse_args(["--warmup-vblanks", "9000"])
        plan = route_warmup.plan_warmup(args, Path("missing.elf"))
        target = FakeTarget()
        route_warmup.execute_warmup(target, plan)
        self.assertEqual(target.run_for_calls, [3600, 3600, 1800])
        self.assertEqual(target.vblanks, 9000)

    def test_tick_warmup_without_symbols_fails_loudly(self) -> None:
        args = self._parser().parse_args([])
        with self.assertRaisesRegex(ValueError, "needs the sState"):
            route_warmup.plan_warmup(args, Path("missing.elf"))

    def test_zero_tick_target_needs_no_symbols(self) -> None:
        args = self._parser(default_ticks=0).parse_args([])
        plan = route_warmup.plan_warmup(args, Path("missing.elf"))
        self.assertEqual(plan["target_tick"], 0)
        record = route_warmup.execute_warmup(FakeTarget(), plan)
        self.assertEqual(record["vblanks_advanced"], 0)

    def test_negative_deprecated_value_is_rejected(self) -> None:
        args = self._parser().parse_args(["--warmup-vblanks", "-1"])
        with self.assertRaisesRegex(ValueError, "must not be negative"):
            route_warmup.plan_warmup(args, Path("missing.elf"))


class SymbolResolutionTests(unittest.TestCase):
    """Bind the offsets to the shipped ELFs rather than to a comment."""

    CANDIDATES = (
        Path("releases/2026-08-16_t2_13-product/id-a61d5203793986e7/sm64-saturn-sourceboot-e2.elf"),
        Path("build/saturn/sourceboot/e2-bob-identity-id-c0352f297034f653/obj/sm64-saturn-sourceboot-e2.elf"),
    )

    def test_shipped_elfs_expose_both_counters(self) -> None:
        root = TOOLS_DIR.parents[1]
        found = 0
        for relative in self.CANDIDATES:
            elf = root / relative
            if not elf.is_file():
                continue
            found += 1
            addresses = route_warmup.resolve_warmup_symbols(elf)
            self.assertGreater(addresses["replay_ticks"], 0)
            self.assertGreater(addresses["global_timer"], 0)
            self.assertEqual(
                addresses["replay_ticks"] % 4, 0, "replay tick field must be aligned"
            )
        if not found:
            self.skipTest("no sealed sourceboot ELF present in this tree")


if __name__ == "__main__":
    unittest.main()
