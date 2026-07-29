#!/usr/bin/env python3
"""Census linked SH-2 calls that require non-native arithmetic.

The SH-2 has no FPU and no native 64-bit divide. The compiler reaches those
operations through a PC-relative literal-pool load followed by ``jsr @rN``.
This verifier reads the *linked* ELF's objdump output and reports the source
owner through addr2line. HOT is derived from an immutable replay-route root
fixture expanded through the linked ELF's direct-call graph; it is never a
manually maintained list of caller names.
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict, deque
from dataclasses import dataclass
from hashlib import sha256
from pathlib import Path
import re
import subprocess
import sys
from typing import Iterable


@dataclass(frozen=True)
class CallSite:
    """One resolved direct call in the linked image."""

    caller: str
    address: int
    helper: str


@dataclass(frozen=True)
class RouteOracle:
    version: int
    roots: frozenset[str]
    indirect_edges: frozenset[tuple[str, str]]


@dataclass(frozen=True)
class BaselineContract:
    version: int
    hot_ceiling: int
    entries: dict[tuple[str, str], int]


@dataclass(frozen=True)
class CensusRow:
    heat: str
    caller: str
    count: int
    helpers: tuple[tuple[str, int], ...]


FUNCTION_RE = re.compile(r"^\s*([0-9A-Fa-f]+)\s+<([^>]+)>:$")
INSTRUCTION_RE = re.compile(r"^\s*([0-9A-Fa-f]+):\s+(?:[0-9A-Fa-f]{2}\s+){1,4}(.+)$")
LITERAL_LOAD_RE = re.compile(r"\bmov\.l\s+[^\n]*,r(\d+)\s*!\s*[0-9A-Fa-f]+\s+<([^>]+)>")
JSR_RE = re.compile(r"\bjsr\s+@r(\d+)\b")
BSR_RE = re.compile(r"\bbsr\s+(?:0x)?[0-9A-Fa-f]+\s+<([^>]+)>")
DESTINATION_RE = re.compile(r",r(\d+)\s*(?:!.*)?$")
STACK_STORE_RE = re.compile(r"\bmov\.l\s+r(\d+),@\((\d+),r15\)")
STACK_LOAD_RE = re.compile(r"\bmov\.l\s+@\((\d+),r15\),r(\d+)")

# Pinned digests deliberately make the route and helper ceilings append-only
# contracts. Updating either requires an explicit v2 implementation change,
# not a quiet edit to a text allowlist.
ROUTE_ORACLE_V1_SHA256 = "f683fc1b507a6630d12d47d625ec59deabacd5d4d55e5b0a2113ac8c6ef92f4e"
BASELINE_V1_SHA256 = "dfe6e5f494ad3ec103ce0024e5038174c9c18bf8ae42c2d65365cdc2c2fcf57a"
SIM_ROUTE_ORACLE_V1_SHA256 = "3bde797d9f07323b112b297c49ff4debd2a786c81d1e1be382feaf857f278a2f"

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


def _symbol_base(symbol: str) -> str:
    """Drop objdump's intra-symbol offset so graph keys are function names."""
    return symbol.split("+", 1)[0]


def scan_direct_calls(disassembly: str) -> list[CallSite]:
    """Return literal-pool jsr and PC-relative bsr calls with linked targets."""
    caller = "<outside-function>"
    registers: dict[str, str] = {}
    stack_slots: dict[int, str] = {}
    calls: list[CallSite] = []

    for line in disassembly.splitlines():
        function = FUNCTION_RE.match(line)
        if function:
            caller = function.group(2)
            registers.clear()
            stack_slots.clear()
            continue

        instruction = INSTRUCTION_RE.match(line)
        if instruction is None:
            continue
        address = int(instruction.group(1), 16)
        text = instruction.group(2).strip()

        load = LITERAL_LOAD_RE.search(text)
        if load:
            registers[load.group(1)] = _symbol_base(load.group(2))
            continue

        # GCC spills a literal-pool target around a call in some large
        # functions. Preserve only fixed r15-relative slots; they describe the
        # current function frame and are safe to invalidate if r15 changes.
        stack_store = STACK_STORE_RE.search(text)
        if stack_store:
            slot = int(stack_store.group(2), 10)
            target = registers.get(stack_store.group(1))
            if target is None:
                stack_slots.pop(slot, None)
            else:
                stack_slots[slot] = target
            continue

        stack_load = STACK_LOAD_RE.search(text)
        if stack_load:
            slot = int(stack_load.group(1), 10)
            target_register = stack_load.group(2)
            target = stack_slots.get(slot)
            if target is None:
                registers.pop(target_register, None)
            else:
                registers[target_register] = target
            continue

        jsr = JSR_RE.search(text)
        if jsr:
            target = registers.get(jsr.group(1))
            if target is not None:
                calls.append(CallSite(caller, address, target))
            continue

        bsr = BSR_RE.search(text)
        if bsr:
            calls.append(CallSite(caller, address, _symbol_base(bsr.group(1))))
            continue

        destination = DESTINATION_RE.search(text)
        if destination:
            destination_register = destination.group(1)
            registers.pop(destination_register, None)
            if destination_register == "15":
                stack_slots.clear()
    return calls


def scan_disassembly(disassembly: str) -> list[CallSite]:
    """Attribute direct calls to their containing symbols, retaining math only."""
    return [call for call in scan_direct_calls(disassembly) if is_native_math_helper(call.helper)]


def scan_call_graph(disassembly: str) -> dict[str, set[str]]:
    """Build the linked ELF direct-call graph used to expand route roots."""
    graph: dict[str, set[str]] = defaultdict(set)
    for call in scan_direct_calls(disassembly):
        # Helpers are terminal for route ownership: their implementation is a
        # runtime cost, not a route child that can itself own another census
        # call site.
        if not is_native_math_helper(call.helper):
            graph[call.caller].add(call.helper)
    return graph


def parse_route_oracle(text: str) -> RouteOracle:
    """Parse a versioned, checked-in replay-route root fixture."""
    version: int | None = None
    roots: set[str] = set()
    indirect_edges: set[tuple[str, str]] = set()
    for line_number, raw_line in enumerate(text.splitlines(), start=1):
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if parts[0] == "ROUTE_ORACLE_VERSION" and len(parts) == 2:
            if version is not None:
                raise ValueError(f"route oracle line {line_number}: duplicate version")
            version = int(parts[1], 10)
        elif parts[0] == "ROOT" and len(parts) == 2:
            roots.add(parts[1])
        elif parts[0] == "INDIRECT_EDGE" and len(parts) == 3:
            indirect_edges.add((parts[1], parts[2]))
        else:
            raise ValueError(
                f"route oracle line {line_number}: expected ROOT <linked-symbol> or "
                "INDIRECT_EDGE <dispatch-symbol> <callback-symbol>"
            )
    if version != 1:
        raise ValueError(f"unsupported route oracle version {version!r}")
    if not roots:
        raise ValueError("route oracle has no ROOT")
    return RouteOracle(version, frozenset(roots), frozenset(indirect_edges))


def parse_baseline(text: str) -> BaselineContract:
    """Parse the versioned maximum HOT helper contract."""
    version: int | None = None
    hot_ceiling: int | None = None
    entries: dict[tuple[str, str], int] = {}
    for line_number, raw_line in enumerate(text.splitlines(), start=1):
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if parts[0] == "BASELINE_VERSION" and len(parts) == 2:
            if version is not None:
                raise ValueError(f"baseline line {line_number}: duplicate version")
            version = int(parts[1], 10)
            continue
        if parts[0] == "HOT_CEILING" and len(parts) == 2:
            if hot_ceiling is not None:
                raise ValueError(f"baseline line {line_number}: duplicate HOT_CEILING")
            hot_ceiling = int(parts[1], 10)
            continue
        if len(parts) == 4 and parts[0] == "HOT":
            caller, helper, count_text = parts[1:]
            count = int(count_text, 10)
            key = (caller, helper)
            if count <= 0:
                raise ValueError(f"baseline line {line_number}: count must be positive")
            if key in entries:
                raise ValueError(f"baseline line {line_number}: duplicate {caller} {helper}")
            entries[key] = count
            continue
        raise ValueError(f"baseline line {line_number}: expected HOT <caller> <helper> <ceiling>")
    if version != 1:
        raise ValueError(f"unsupported baseline version {version!r}")
    if hot_ceiling is None or hot_ceiling < 0:
        raise ValueError("baseline missing non-negative HOT_CEILING")
    if sum(entries.values()) != hot_ceiling:
        raise ValueError("baseline HOT_CEILING must equal the sum of HOT entries")
    return BaselineContract(version, hot_ceiling, entries)


def baseline_digest(text: str) -> str:
    return sha256(text.encode("utf-8")).hexdigest()


def verify_baseline_integrity(text: str, baseline: BaselineContract, *, expected_digest: str = BASELINE_V1_SHA256) -> None:
    if baseline.version != 1:
        raise ValueError(f"unsupported baseline version {baseline.version}")
    if expected_digest == "PENDING" or baseline_digest(text) != expected_digest:
        raise ValueError("immutable baseline digest mismatch")


def verify_route_oracle_integrity(text: str, oracle: RouteOracle, *, expected_digest: str = ROUTE_ORACLE_V1_SHA256) -> None:
    if oracle.version != 1:
        raise ValueError(f"unsupported route oracle version {oracle.version}")
    if expected_digest == "PENDING" or baseline_digest(text) != expected_digest:
        raise ValueError("immutable route oracle digest mismatch")


def route_reachable_functions(
    graph: dict[str, set[str]],
    roots: Iterable[str],
    indirect_edges: Iterable[tuple[str, str]] = (),
) -> set[str]:
    """Return direct-call closure plus pinned, required indirect callback edges."""
    indirect_graph: dict[str, set[str]] = defaultdict(set)
    for caller, target in indirect_edges:
        indirect_graph[caller].add(target)
    reachable = set(roots)
    pending = deque(reachable)
    while pending:
        caller = pending.popleft()
        for target in graph.get(caller, set()) | indirect_graph.get(caller, set()):
            if target not in reachable:
                reachable.add(target)
                pending.append(target)
    return reachable


def baseline_failures(calls: Iterable[CallSite], route_functions: set[str], baseline: BaselineContract) -> list[str]:
    """Enforce immutable maximums for every math call in the derived HOT route."""
    observed = Counter((call.caller, call.helper) for call in calls if call.caller in route_functions)
    failures: list[str] = []
    for key, actual in sorted(observed.items()):
        expected = baseline.entries.get(key)
        if expected is None:
            failures.append(f"unallowlisted helper in HOT function: {key[0]} {key[1]} found {actual}")
        elif actual > expected:
            failures.append(f"HOT baseline exceeded: {key[0]} {key[1]} ceiling {expected}, found {actual}")
    actual_total = sum(observed.values())
    if actual_total > baseline.hot_ceiling:
        failures.append(f"HOT total ceiling {baseline.hot_ceiling}, found {actual_total}")
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
    addresses = sorted({call.address for call in calls})
    if not addresses:
        return {}
    locations: dict[int, str] = {}
    for batch in address_batches(addresses):
        command = [addr2line, "-e", str(elf), "-f", "-C", *[f"0x{address:X}" for address in batch]]
        result = subprocess.run(command, check=True, capture_output=True, text=True)
        lines = result.stdout.splitlines()
        for index, address in enumerate(batch):
            source_index = index * 2 + 1
            locations[address] = lines[source_index] if source_index < len(lines) else "??:0"
    return locations


def run_command(command: list[str]) -> str:
    return subprocess.run(command, check=True, capture_output=True, text=True).stdout


def census_rows(calls: Iterable[CallSite], route_functions: set[str]) -> list[CensusRow]:
    grouped: dict[str, Counter[str]] = defaultdict(Counter)
    for call in calls:
        grouped[call.caller][call.helper] += 1
    return [
        CensusRow("HOT" if caller in route_functions else "COLD", caller, sum(helpers.values()), tuple(sorted(helpers.items())))
        for caller, helpers in sorted(grouped.items())
    ]


def print_census(calls: Iterable[CallSite], route_functions: set[str], locations: dict[int, str]) -> None:
    call_list = list(calls)
    location_by_caller = {call.caller: locations.get(call.address, "??:0") for call in call_list}
    rows = census_rows(call_list, route_functions)
    for row in rows:
        helpers = ", ".join(f"{helper}={count}" for helper, count in row.helpers)
        print(f"{row.heat:4} {row.count:4} {row.caller} [{helpers}] ({location_by_caller[row.caller]})")
    hot_total = sum(row.count for row in rows if row.heat == "HOT")
    print(f"SH-2 native-math census: HOT total {hot_total}; COLD total {sum(row.count for row in rows) - hot_total}")


def print_audit(calls: Iterable[CallSite], route_functions: set[str], locations: dict[int, str]) -> None:
    """Print a pinned route audit without weakening the shipped HOT ceiling."""
    call_list = list(calls)
    location_by_caller = {call.caller: locations.get(call.address, "??:0") for call in call_list}
    rows = [row for row in census_rows(call_list, route_functions) if row.heat == "HOT"]
    for row in rows:
        helpers = ", ".join(f"{helper}={count}" for helper, count in row.helpers)
        print(f"AUDIT {row.count:4} {row.caller} [{helpers}] ({location_by_caller[row.caller]})")
    print(f"SH-2 native-math route audit: total {sum(row.count for row in rows)}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path, help="linked sourceboot ELF")
    parser.add_argument("baseline", type=Path, help="immutable HOT helper ceiling fixture")
    parser.add_argument("--route-oracle", type=Path, required=True, help="immutable replay-route root fixture")
    parser.add_argument("--audit-route-oracle", type=Path,
                        help="optional immutable source-simulation route to audit without changing the shipped ceiling")
    parser.add_argument("--objdump", required=True, help="target objdump executable")
    parser.add_argument("--addr2line", required=True, help="target addr2line executable")
    args = parser.parse_args(argv)

    try:
        baseline_text = args.baseline.read_text(encoding="utf-8")
        route_text = args.route_oracle.read_text(encoding="utf-8")
        baseline = parse_baseline(baseline_text)
        oracle = parse_route_oracle(route_text)
        verify_baseline_integrity(baseline_text, baseline)
        verify_route_oracle_integrity(route_text, oracle)
        audit_oracle = None
        if args.audit_route_oracle is not None:
            audit_text = args.audit_route_oracle.read_text(encoding="utf-8")
            audit_oracle = parse_route_oracle(audit_text)
            verify_route_oracle_integrity(
                audit_text, audit_oracle, expected_digest=SIM_ROUTE_ORACLE_V1_SHA256
            )
        disassembly = run_command([args.objdump, "-d", str(args.elf)])
        calls = scan_disassembly(disassembly)
        graph = scan_call_graph(disassembly)
        route_functions = route_reachable_functions(graph, oracle.roots, oracle.indirect_edges)
        audit_functions = None if audit_oracle is None else route_reachable_functions(
            graph, audit_oracle.roots, audit_oracle.indirect_edges
        )
        locations = source_locations(args.addr2line, args.elf, calls)
        print_census(calls, route_functions, locations)
        if audit_functions is not None:
            print_audit(calls, audit_functions, locations)
        failures = baseline_failures(calls, route_functions, baseline)
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
