#!/usr/bin/env python3
"""Structural regression guard for the Saturn object-pool occupancy probe.

Task 2 of the memory-residency campaign (docs/superpowers/plans/
2026-08-09-memory-residency-campaign.md) instruments ``gObjectPool``
(src/game/object_list_processor.c) so the port can measure real occupancy
before an owner decides on a capacity cut. The probe itself
(``sm64_saturn_object_pool_probe_t`` / ``g_sm64_saturn_object_pool_probe``,
src/port/saturn/runtime/saturn_object_pool_probe.h) is trivial, but the
counters are only useful if they are wired at the *real* allocate/free
sites -- ``try_allocate_object()`` (the free-list pop that hands an object
out) and ``deallocate_object()`` (the free-list push that returns one) in
src/game/spawn_object.c, plus the true pool-exhaustion path inside
``allocate_object()`` (the ``find_unimportant_object() == NULL`` branch,
which otherwise hangs in an infinite loop -- "We've met with a terrible
fate.").

This is a source-text contract, not a compiled/behavioral test: like
test_divu_overflow_clear_contract.py, it extracts each named function's
brace-balanced body, then parses ``#ifdef TARGET_SATURN`` /
``#if defined(TARGET_SATURN)`` true-branches while tracking directive nesting
depth. It cannot prove the counters are numerically correct on real hardware
or in Ymir -- only that the wiring exists in the sites the plan named, under
the right compile-time gate, with the right volatile qualification for a
target-visible probe Ymir's debugger reads out of live target RAM.
"""

from __future__ import annotations

import re
import shutil
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

PROBE_HEADER = "src/port/saturn/runtime/saturn_object_pool_probe.h"
SPAWN_OBJECT_C = "src/game/spawn_object.c"
OBJECT_LIST_PROCESSOR_C = "src/game/object_list_processor.c"
OBJECT_LIST_PROCESSOR_H = "src/game/object_list_processor.h"
SOURCEBOOT_MAIN_C = "src/port/saturn/sourceboot/main.c"

FIELD_NAMES = (
    "magic",
    "current_allocated",
    "peak_allocated",
    "alloc_failures",
    "frames_sampled",
)

STRUCT_RE = re.compile(
    r"typedef\s+struct\s*(?:\w+\s*)?\{(.*?)\}\s*sm64_saturn_object_pool_probe_t\s*;",
    re.DOTALL,
)
MAGIC_DEFINE_RE = re.compile(
    r"#define\s+SM64_SATURN_OBJECT_POOL_PROBE_MAGIC\s+0[xX]4[Ff]504[Ff]4[Cc]u?"
)
EXTERN_GLOBAL_RE = re.compile(
    r"extern\s+volatile\s+sm64_saturn_object_pool_probe_t\s+"
    r"g_sm64_saturn_object_pool_probe\s*;"
)

TARGET_SATURN_IF_RE = re.compile(
    r"^[ \t]*#[ \t]*(?:ifdef[ \t]+TARGET_SATURN\b"
    r"|if[ \t]+defined\(TARGET_SATURN\))[ \t]*$"
)
DIRECTIVE_RE = re.compile(r"^[ \t]*#[ \t]*(if|ifdef|ifndef|else|elif|endif)\b")
OPENING_KINDS = frozenset({"if", "ifdef", "ifndef"})

CURRENT_ALLOCATED_INCREMENT_RE = re.compile(
    r"g_sm64_saturn_object_pool_probe\.current_allocated\s*(\+\+|\+=\s*1\s*;)"
)
CURRENT_ALLOCATED_DECREMENT_RE = re.compile(
    r"g_sm64_saturn_object_pool_probe\.current_allocated\s*(--|-=\s*1\s*;)"
)
PEAK_ALLOCATED_UPDATE_RE = re.compile(
    r"g_sm64_saturn_object_pool_probe\.peak_allocated"
)
ALLOC_FAILURES_INCREMENT_RE = re.compile(
    r"g_sm64_saturn_object_pool_probe\.alloc_failures\s*(\+\+|\+=\s*1\s*;)"
)
FRAMES_SAMPLED_INCREMENT_RE = re.compile(
    r"g_sm64_saturn_object_pool_probe\.frames_sampled\s*(\+\+|\+=\s*1\s*;)"
)
PROBE_DEFINITION_RE = re.compile(
    r"\bg_sm64_saturn_object_pool_probe\s*=\s*\{"
)


def _read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def _target_saturn_branches(source: str) -> list[str]:
    """Return every '#ifdef TARGET_SATURN'/'#if defined(TARGET_SATURN)'
    true-branch body in ``source``, in file order.

    Tracks directive nesting depth explicitly (mirrors
    test_divu_overflow_clear_contract.py's ``_sh_true_branch_with_divu_call``)
    so a nested ``#if``/``#endif`` inside the branch (e.g. an
    ``#ifdef SATURN_DEMO_PATH`` sub-block) does not truncate the branch
    early.
    """
    lines = source.splitlines(keepends=True)
    branches: list[str] = []
    index = 0
    while index < len(lines):
        if not TARGET_SATURN_IF_RE.match(lines[index]):
            index += 1
            continue
        depth = 1
        start = index + 1
        cursor = start
        end: int | None = None
        while cursor < len(lines):
            directive = DIRECTIVE_RE.match(lines[cursor])
            if directive is not None:
                kind = directive.group(1)
                if kind in OPENING_KINDS:
                    depth += 1
                elif kind == "else" and depth == 1:
                    end = cursor
                    break
                elif kind == "endif":
                    depth -= 1
                    if depth == 0:
                        end = cursor
                        break
            cursor += 1
        if end is None:
            raise AssertionError(
                "unterminated '#ifdef TARGET_SATURN' block starting at "
                f"source line {index + 1}"
            )
        branches.append("".join(lines[start:end]))
        index = end + 1
    return branches


def _function_body(source: str, name: str) -> str:
    """Return one named C function's brace-balanced body.

    The probe hooks are only meaningful at their named free-list and fatal
    allocation sites.  Searching every TARGET_SATURN branch in the file can
    accept a correctly spelled hook relocated to an unrelated function.
    """
    match = re.search(
        rf"(?m)^[^;\n]*\b{re.escape(name)}\s*\([^;\n]*?\)\s*\{{",
        source,
    )
    if match is None:
        raise AssertionError(f"function definition not found: {name}")
    opening = match.end() - 1
    depth = 0
    for index in range(opening, len(source)):
        character = source[index]
        if character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unterminated function definition: {name}")


def _preprocessed_object_pool_capacity(*defines: str) -> int:
    """Use the real host C preprocessor to observe the public macro's value."""
    compiler = shutil.which("gcc")
    if compiler is None:
        raise AssertionError("object-pool macro contract requires host gcc")
    header = ROOT / OBJECT_LIST_PROCESSOR_H
    source = (
        f'#include "{header.as_posix()}"\n'
        "enum { object_pool_capacity_probe = OBJECT_POOL_CAPACITY };\n"
    )
    completed = subprocess.run(
        [
            compiler,
            "-E",
            "-P",
            "-x",
            "c",
            "-I",
            str(ROOT / "include"),
            "-I",
            str(ROOT / "src"),
            "-I",
            str(ROOT),
            "-DNON_MATCHING=1",
            "-DAVOID_UB=1",
            *defines,
            "-",
        ],
        input=source,
        text=True,
        capture_output=True,
        check=False,
    )
    if completed.returncode != 0:
        raise AssertionError(
            "host C preprocessor failed for object-pool capacity contract: "
            + completed.stderr
        )
    match = re.search(
        r"enum\s*\{\s*object_pool_capacity_probe\s*=\s*(\d+)\s*\};",
        completed.stdout,
    )
    if match is None:
        raise AssertionError("preprocessed object-pool capacity sentinel not found")
    return int(match.group(1))


class ObjectPoolProbeContractTest(unittest.TestCase):
    def test_probe_struct_fields_are_volatile_uint32(self) -> None:
        source = _read(PROBE_HEADER)
        match = STRUCT_RE.search(source)
        self.assertIsNotNone(
            match, f"{PROBE_HEADER}: sm64_saturn_object_pool_probe_t struct not found"
        )
        body = match.group(1)
        for field in FIELD_NAMES:
            self.assertRegex(
                body,
                re.compile(rf"volatile\s+uint32_t\s+{field}\s*;"),
                f"{PROBE_HEADER}: field {field!r} must be declared "
                "'volatile uint32_t' inside the probe struct",
            )

    def test_probe_magic_constant_matches_spec(self) -> None:
        source = _read(PROBE_HEADER)
        self.assertRegex(
            source,
            MAGIC_DEFINE_RE,
            f"{PROBE_HEADER}: expected "
            "'#define SM64_SATURN_OBJECT_POOL_PROBE_MAGIC 0x4F504F4Cu' (or "
            "equivalent hex casing), the 'OPOL' magic from the plan",
        )

    def test_probe_global_is_volatile_extern(self) -> None:
        source = _read(PROBE_HEADER)
        self.assertRegex(
            source,
            EXTERN_GLOBAL_RE,
            f"{PROBE_HEADER}: expected 'extern volatile "
            "sm64_saturn_object_pool_probe_t g_sm64_saturn_object_pool_probe;'",
        )

    def test_probe_storage_is_target_saturn_gated(self) -> None:
        """The non-extern definition (the actual storage) must live inside a
        TARGET_SATURN branch -- this is a portable-tree file (game/), not a
        Saturn-only one, so an ungated definition would break every other
        platform's link."""
        found_in: list[str] = []
        for relative in (OBJECT_LIST_PROCESSOR_C, SPAWN_OBJECT_C):
            source = _read(relative)
            for branch in _target_saturn_branches(source):
                if PROBE_DEFINITION_RE.search(branch):
                    found_in.append(relative)
        self.assertTrue(
            found_in,
            "g_sm64_saturn_object_pool_probe's storage definition "
            f"('{PROBE_DEFINITION_RE.pattern}') must appear inside a "
            "TARGET_SATURN branch in one of: "
            f"{OBJECT_LIST_PROCESSOR_C}, {SPAWN_OBJECT_C}",
        )

    def test_allocate_site_updates_current_and_peak(self) -> None:
        """try_allocate_object() is the real free-list-pop site (Step 2 of
        the plan). Every successful allocation must count once and peak must
        track the observed maximum."""
        source = _read(SPAWN_OBJECT_C)
        branches = _target_saturn_branches(_function_body(source, "try_allocate_object"))
        self.assertTrue(
            any(
                CURRENT_ALLOCATED_INCREMENT_RE.search(branch)
                and PEAK_ALLOCATED_UPDATE_RE.search(branch)
                for branch in branches
            ),
            f"{SPAWN_OBJECT_C}: expected a TARGET_SATURN branch (at the real "
            "allocation site, try_allocate_object's successful return) that "
            "increments current_allocated and updates peak_allocated",
        )

    def test_free_site_decrements_current_allocated(self) -> None:
        """deallocate_object() is the real free-list-push site (Step 2 of
        the plan)."""
        source = _read(SPAWN_OBJECT_C)
        branches = _target_saturn_branches(_function_body(source, "deallocate_object"))
        self.assertTrue(
            any(CURRENT_ALLOCATED_DECREMENT_RE.search(branch) for branch in branches),
            f"{SPAWN_OBJECT_C}: expected a TARGET_SATURN branch (at the real "
            "free site, deallocate_object) that decrements current_allocated",
        )

    def test_exhaustion_path_increments_alloc_failures(self) -> None:
        """allocate_object()'s find_unimportant_object() == NULL branch is
        the true pool-exhaustion path (it hangs forever afterward -- 'We've
        met with a terrible fate.'); alloc_failures must count reaching it,
        not merely a recoverable eviction."""
        source = _read(SPAWN_OBJECT_C)
        body = _function_body(source, "allocate_object")
        branches = _target_saturn_branches(body)
        self.assertTrue(
            any(
                ALLOC_FAILURES_INCREMENT_RE.search(branch)
                for branch in branches
            ),
            f"{SPAWN_OBJECT_C}: expected a TARGET_SATURN branch (at the true "
            "pool-exhaustion path in allocate_object) that increments "
            "alloc_failures",
        )
        self.assertRegex(
            body,
            re.compile(
                r"g_sm64_saturn_object_pool_probe\.alloc_failures"
                r"\s*(?:\+\+\s*;|\+=\s*1\s*;)\s*#endif\s*"
                r"// We've met with a terrible fate\.\s*while\s*\(\s*TRUE\s*\)",
                re.DOTALL,
            ),
            f"{SPAWN_OBJECT_C}: allocate_object() must leave the nonzero "
            "alloc_failures counter latched immediately before its fatal loop",
        )

    def test_object_pool_capacity_override_changes_the_compiled_header_value(self) -> None:
        """A sourceboot-only override must change the capacity consumers see;
        an unset build retains the portable 240-slot default."""
        self.assertEqual(_preprocessed_object_pool_capacity(), 240)
        self.assertEqual(
            _preprocessed_object_pool_capacity(
                "-DSATURN_OBJECT_POOL_CAPACITY_OVERRIDE=208"
            ),
            208,
        )

    def test_frames_sampled_incremented_once_per_game_loop_tick(self) -> None:
        """sourceboot's main.c is Saturn-only already (no TARGET_SATURN gate
        needed there); frames_sampled must be bumped from the real per-tick
        site, the same place sourceboot_boot_trace publishes its own
        SOURCE_TICK boundary."""
        source = _read(SOURCEBOOT_MAIN_C)
        self.assertRegex(
            source,
            FRAMES_SAMPLED_INCREMENT_RE,
            f"{SOURCEBOOT_MAIN_C}: expected "
            "'g_sm64_saturn_object_pool_probe.frames_sampled++;' at the "
            "per-game-loop-tick site (sourceboot_run_source_tick)",
        )

    def test_guard_detects_a_missing_allocate_increment(self) -> None:
        """Prove the allocate-site guard is not a tautology: strip the
        current_allocated increment from a copy of spawn_object.c and
        confirm the detection now fails."""
        source = _read(SPAWN_OBJECT_C)
        mutated = CURRENT_ALLOCATED_INCREMENT_RE.sub("/* removed */", source, count=1)
        self.assertNotEqual(mutated, source)
        branches = _target_saturn_branches(_function_body(mutated, "try_allocate_object"))
        self.assertFalse(
            any(
                CURRENT_ALLOCATED_INCREMENT_RE.search(branch)
                and PEAK_ALLOCATED_UPDATE_RE.search(branch)
                for branch in branches
            ),
            "mutated fixture unexpectedly still contains the allocate-site "
            "current_allocated increment",
        )

    def test_guard_detects_a_missing_free_decrement(self) -> None:
        """Prove the free-site guard is not a tautology: strip the
        current_allocated decrement and confirm the detection now fails."""
        source = _read(SPAWN_OBJECT_C)
        mutated = CURRENT_ALLOCATED_DECREMENT_RE.sub("/* removed */", source, count=1)
        self.assertNotEqual(mutated, source)
        branches = _target_saturn_branches(_function_body(mutated, "deallocate_object"))
        self.assertFalse(
            any(CURRENT_ALLOCATED_DECREMENT_RE.search(branch) for branch in branches),
            "mutated fixture unexpectedly still contains the free-site "
            "current_allocated decrement",
        )

    def test_guard_detects_a_missing_alloc_failure_increment(self) -> None:
        """Prove the exhaustion-path guard is not a tautology: strip the
        alloc_failures increment and confirm the detection now fails."""
        source = _read(SPAWN_OBJECT_C)
        mutated = ALLOC_FAILURES_INCREMENT_RE.sub("/* removed */", source, count=1)
        self.assertNotEqual(mutated, source)
        branches = _target_saturn_branches(_function_body(mutated, "allocate_object"))
        self.assertFalse(
            any(ALLOC_FAILURES_INCREMENT_RE.search(branch) for branch in branches),
            "mutated fixture unexpectedly still contains the exhaustion-path "
            "alloc_failures increment",
        )


if __name__ == "__main__":
    unittest.main()
