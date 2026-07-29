#!/usr/bin/env python3
"""Census linked SH-2 calls that require non-native arithmetic.

The SH-2 has no FPU and no native 64-bit divide.  The compiler reaches those
operations through a PC-relative literal-pool load followed by ``jsr @rN``.
This verifier reads the *linked* ELF's objdump output, follows that pair, and
uses addr2line on each call instruction to report the source owner.  It is
therefore a link-time census, rather than an unreliable source-level grep.

The allowlist deliberately contains only route-HOT call sites.  Its expected
counts are exact: removing a hot call makes a row stale, while adding one (or
adding a different expensive helper to an already-hot function) fails the
gate.  All other linked call sites are reported as COLD; their reachability is
not inferred from static disassembly.
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
import re
import subprocess
import sys
from typing import Iterable


@dataclass(frozen=True)
class CallSite:
    """One direct call to a non-native arithmetic helper."""

    caller: str
    address: int
    helper: str


@dataclass(frozen=True)
class AllowlistEntry:
    heat: str
    caller: str
    helper: str
    expected_count: int


@dataclass(frozen=True)
class CensusRow:
    """Aggregate output for one calling function."""

    heat: str
    caller: str
    count: int
    helpers: tuple[tuple[str, int], ...]


FUNCTION_RE = re.compile(r"^\s*([0-9A-Fa-f]+)\s+<([^>]+)>:$")
INSTRUCTION_RE = re.compile(r"^\s*([0-9A-Fa-f]+):\s+(?:[0-9A-Fa-f]{2}\s+){1,4}(.+)$")
LITERAL_LOAD_RE = re.compile(
    r"\bmov\.l\s+[^\n]*,r(\d+)\s*!\s*[0-9A-Fa-f]+\s+<([^>]+)>"
)
JSR_RE = re.compile(r"\bjsr\s+@r(\d+)\b")
DESTINATION_RE = re.compile(r",r(\d+)\s*(?:!.*)?$")

# GCC SH targets decorate libgcc symbols with an extra leading underscore.
# Remove decoration before checking the ABI spelling, but retain the linked
# spelling in all reports and allowlist rows.
LIBM_NAMES = {
    "acos", "acosf", "asin", "asinf", "atan", "atan2", "atan2f", "atanf",
    "ceil", "ceilf", "cos", "cosf", "exp", "expf", "fabs", "fabsf",
    "floor", "floorf", "fmod", "fmodf", "log", "logf", "pow", "powf",
    "sin", "sinf", "sqrt", "sqrtf", "tan", "tanf",
}
DIV64_RE = re.compile(r"(?:u?div|u?mod)di3$")
SOFT_FLOAT_RE = re.compile(
    r"(?:add|sub|mul|div|neg|eq|ne|cmp|ge|le|gt|lt|unord)"
    r"(?:sf|df)(?:2|3)$|(?:fix|float|extend|trunc)[a-z0-9]*$"
)
SOFT_FLOAT_NAMES = {"absf", "powisf2"}


def is_native_math_helper(symbol: str) -> bool:
    """Whether *symbol* names a linked soft-fp, libm, or 64-bit-div helper."""
    canonical = symbol.lstrip("_")
    return (
        canonical in LIBM_NAMES
        or canonical in SOFT_FLOAT_NAMES
        or bool(DIV64_RE.fullmatch(canonical))
        or bool(SOFT_FLOAT_RE.fullmatch(canonical))
    )


def scan_disassembly(disassembly: str) -> list[CallSite]:
    """Attribute literal-pool ``jsr`` calls to their containing symbols."""
    caller = "<outside-function>"
    registers: dict[str, str] = {}
    calls: list[CallSite] = []

    for line in disassembly.splitlines():
        function = FUNCTION_RE.match(line)
        if function:
            caller = function.group(2)
            registers.clear()
            continue

        instruction = INSTRUCTION_RE.match(line)
        if instruction is None:
            continue
        address = int(instruction.group(1), 16)
        text = instruction.group(2).strip()

        load = LITERAL_LOAD_RE.search(text)
        if load:
            registers[load.group(1)] = load.group(2)
            continue

        jsr = JSR_RE.search(text)
        if jsr:
            helper = registers.get(jsr.group(1))
            if helper is not None and is_native_math_helper(helper):
                calls.append(CallSite(caller, address, helper))
            continue

        # A literal-pool value must not survive a later write to the register;
        # clearing it avoids attributing an unrelated indirect call to a stale
        # helper target.  Reads (which lack a trailing destination) retain it.
        destination = DESTINATION_RE.search(text)
        if destination:
            registers.pop(destination.group(1), None)

    return calls


def parse_allowlist(text: str) -> dict[tuple[str, str], AllowlistEntry]:
    """Parse the explicit route function set and exact helper-count contract."""
    entries: dict[tuple[str, str], AllowlistEntry] = {}
    for line_number, raw_line in enumerate(text.splitlines(), start=1):
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if parts[0] == "ROUTE_FUNCTION" and len(parts) == 2:
            heat, caller, helper, count_text = "ROUTE_FUNCTION", parts[1], "*", "1"
        elif parts[0] == "HOT_TOTAL" and len(parts) == 2:
            heat, caller, helper, count_text = "HOT_TOTAL", "@total", "*", parts[1]
        elif len(parts) == 4 and parts[0] in {"HOT", "COLD"}:
            heat, caller, helper, count_text = parts
        else:
            raise ValueError(
                f"allowlist line {line_number}: expected HOT|COLD "
                "<caller> <helper> <exact-count>, ROUTE_FUNCTION <caller>, "
                "or HOT_TOTAL <exact-count>"
            )
        try:
            count = int(count_text, 10)
        except ValueError as error:
            raise ValueError(
                f"allowlist line {line_number}: invalid count {count_text!r}"
            ) from error
        if count <= 0:
            raise ValueError(f"allowlist line {line_number}: count must be positive")
        entry = AllowlistEntry(heat, caller, helper, count)
        key = (entry.caller, entry.helper)
        if key in entries:
            raise ValueError(f"allowlist line {line_number}: duplicate {entry.caller} {entry.helper}")
        entries[key] = entry
    route_callers = {entry.caller for entry in entries.values() if entry.heat == "ROUTE_FUNCTION"}
    for entry in entries.values():
        if entry.heat == "HOT" and entry.caller not in route_callers:
            raise ValueError(f"HOT caller {entry.caller} is missing ROUTE_FUNCTION evidence")
    if ("@total", "*") not in entries:
        raise ValueError("allowlist missing HOT_TOTAL contract")
    return entries


def allowlist_failures(
    calls: Iterable[CallSite],
    rules: dict[tuple[str, str], AllowlistEntry],
) -> list[str]:
    """Return contract failures for declared route-HOT and COLD rows."""
    call_list = list(calls)
    observed = Counter((call.caller, call.helper) for call in call_list)
    failures: list[str] = []
    hot_callers = {entry.caller for entry in rules.values() if entry.heat == "HOT"}

    for key, entry in sorted(rules.items()):
        if entry.heat in {"ROUTE_FUNCTION", "HOT_TOTAL"}:
            continue
        actual = observed.get(key, 0)
        if actual != entry.expected_count:
            failures.append(
                f"stale allowlist entry: {entry.heat} {entry.caller} "
                f"{entry.helper} expected {entry.expected_count}, found {actual}"
            )

    for (caller, helper), count in sorted(observed.items()):
        if caller in hot_callers and (caller, helper) not in rules:
            failures.append(
                f"unallowlisted helper in HOT function: {caller} {helper} found {count}"
            )
    expected_total = rules[("@total", "*")].expected_count
    actual_total = sum(
        count for key, count in observed.items()
        if rules.get(key, AllowlistEntry("COLD", "", "", 0)).heat == "HOT"
    )
    if actual_total != expected_total:
        failures.append(f"HOT total expected {expected_total}, found {actual_total}")
    return failures


def address_batches(addresses: Iterable[int], size: int = 128) -> Iterable[tuple[int, ...]]:
    """Yield bounded addr2line argument groups (Windows has a short argv cap)."""
    if size <= 0:
        raise ValueError("batch size must be positive")
    batch: list[int] = []
    for address in addresses:
        batch.append(address)
        if len(batch) == size:
            yield tuple(batch)
            batch.clear()
    if batch:
        yield tuple(batch)


def source_locations(addr2line: str, elf: Path, calls: Iterable[CallSite]) -> dict[int, str]:
    """Resolve each call instruction through the ELF's DWARF line table."""
    addresses = sorted({call.address for call in calls})
    if not addresses:
        return {}
    locations: dict[int, str] = {}
    for batch in address_batches(addresses):
        command = [addr2line, "-e", str(elf), "-f", "-C", *[f"0x{address:X}" for address in batch]]
        result = subprocess.run(command, check=True, capture_output=True, text=True)
        lines = result.stdout.splitlines()
        for index, address in enumerate(batch):
            # -f prints one function line followed by one file:line line.
            # Preserve the path exactly: source roots differ between worktrees.
            source_index = index * 2 + 1
            locations[address] = lines[source_index] if source_index < len(lines) else "??:0"
    return locations


def run_command(command: list[str]) -> str:
    return subprocess.run(command, check=True, capture_output=True, text=True).stdout


def census_rows(calls: Iterable[CallSite], rules: dict[tuple[str, str], AllowlistEntry]) -> list[CensusRow]:
    """Aggregate helper totals per caller, preserving the route-derived heat."""
    grouped: dict[str, Counter[str]] = defaultdict(Counter)
    for call in calls:
        grouped[call.caller][call.helper] += 1
    route_callers = {entry.caller for entry in rules.values() if entry.heat == "ROUTE_FUNCTION"}
    return [
        CensusRow(
            "HOT" if caller in route_callers else "COLD",
            caller,
            sum(helpers.values()),
            tuple(sorted(helpers.items())),
        )
        for caller, helpers in sorted(grouped.items())
    ]


def print_census(calls: Iterable[CallSite], rules: dict[tuple[str, str], AllowlistEntry], locations: dict[int, str]) -> None:
    call_list = list(calls)
    location_by_caller = {call.caller: locations.get(call.address, "??:0") for call in call_list}
    rows = census_rows(call_list, rules)
    for row in rows:
        helpers = ", ".join(f"{helper}={count}" for helper, count in row.helpers)
        print(f"{row.heat:4} {row.count:4} {row.caller} [{helpers}] ({location_by_caller[row.caller]})")
    hot_total = sum(row.count for row in rows if row.heat == "HOT")
    print(f"SH-2 native-math census: HOT total {hot_total}; COLD total {sum(row.count for row in rows) - hot_total}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path, help="linked sourceboot ELF")
    parser.add_argument("allowlist", type=Path, help="route HOT/COLD census contract")
    parser.add_argument("--objdump", required=True, help="target objdump executable")
    parser.add_argument("--addr2line", required=True, help="target addr2line executable")
    args = parser.parse_args(argv)

    try:
        rules = parse_allowlist(args.allowlist.read_text(encoding="utf-8"))
        disassembly = run_command([args.objdump, "-d", str(args.elf)])
        calls = scan_disassembly(disassembly)
        locations = source_locations(args.addr2line, args.elf, calls)
        print_census(calls, rules, locations)
        failures = allowlist_failures(calls, rules)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"SH-2 native-math census ERROR: {error}", file=sys.stderr)
        return 2

    if failures:
        print("SH-2 native-math census FAILED:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
