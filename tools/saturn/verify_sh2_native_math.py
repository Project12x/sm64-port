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
from bisect import bisect_right
from collections import Counter, defaultdict, deque
from dataclasses import dataclass
from hashlib import sha256
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from typing import Any, Iterable


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
class AuditContract:
    version: int
    expected_root: str
    expected_total: int
    forbidden_callers: frozenset[str]


@dataclass(frozen=True)
class CensusRow:
    heat: str
    caller: str
    count: int
    helpers: tuple[tuple[str, int], ...]


class _Unknown:
    def __repr__(self) -> str:
        return "UNKNOWN"


UNKNOWN = _Unknown()
UNREACHED = object()


@dataclass(frozen=True)
class SymbolAtom:
    name: str
    address: int


@dataclass(frozen=True)
class ConstSet:
    kind: str
    values: frozenset[int | SymbolAtom]


@dataclass(frozen=True)
class Interval:
    kind: str
    lo: int
    hi: int


@dataclass(frozen=True)
class ComparisonPredicate:
    register: str
    true_range: tuple[str, int, int]
    false_range: tuple[str, int, int]


AbstractValue = _Unknown | ConstSet | Interval | ComparisonPredicate


@dataclass(frozen=True)
class ExecutableSection:
    index: int
    name: str
    start: int
    end: int


@dataclass(frozen=True)
class RawFunction:
    name: str
    start: int
    size: int
    section: int | str
    binding: str
    visibility: str
    symbol_type: str = "FUNC"


@dataclass(frozen=True)
class FunctionOwner:
    name: str
    start: int
    end: int
    section: int
    aliases: tuple[str, ...] = ()


@dataclass(frozen=True)
class LocalIsland:
    name: str
    start: int
    code_start: int
    end: int
    section: int


@dataclass(frozen=True)
class Instruction:
    address: int
    raw_bytes: bytes
    mnemonic: str
    operands: str
    annotation: str


@dataclass(frozen=True)
class DirectCallFact:
    caller: str
    caller_offset: int
    callee: str
    callee_offset: int
    count: int = 1
    caller_region: str = "owner"
    caller_island: str | None = None


@dataclass(frozen=True)
class ImplementationTransferFact:
    caller: str
    caller_offset: int
    target_island: str
    target_offset: int
    count: int = 1
    caller_region: str = "owner"
    caller_island: str | None = None


@dataclass(frozen=True)
class UnresolvedTransfer:
    caller: str
    address: int
    mnemonic: str


@dataclass(frozen=True)
class UnresolvedEffect:
    function: str
    address: int
    mnemonic: str
    operands: str
    reason: str


@dataclass
class CodeAnalysis:
    calls: list[CallSite]
    direct_calls: list[DirectCallFact]
    implementation_transfers: list[ImplementationTransferFact]
    code_addresses: set[int]
    unresolved_transfers: list[UnresolvedTransfer]
    unresolved_effects: list[UnresolvedEffect]
    owners: tuple[FunctionOwner, ...]


def merge_code_analyses(left: CodeAnalysis, right: CodeAnalysis) -> CodeAnalysis:
    calls = {(x.caller, x.address, x.helper): x for x in [*left.calls, *right.calls]}
    facts = {
        (
            x.caller, x.caller_region, x.caller_island, x.caller_offset,
            x.callee, x.callee_offset
        ): x
        for x in [*left.direct_calls, *right.direct_calls]
    }
    implementation = {
        (
            x.caller, x.caller_region, x.caller_island, x.caller_offset,
            x.target_island, x.target_offset
        ): x
        for x in [*left.implementation_transfers, *right.implementation_transfers]
    }
    resolved = {(x.caller, x.address) for x in calls.values()}
    transfers = {
        (x.caller, x.address, x.mnemonic): x
        for x in [*left.unresolved_transfers, *right.unresolved_transfers]
        if (x.caller, x.address) not in resolved
    }
    effects = {
        (x.function, x.address, x.mnemonic): x
        for x in [*left.unresolved_effects, *right.unresolved_effects]
    }
    return CodeAnalysis(
        sorted(calls.values(), key=lambda x: (x.caller, x.address, x.helper)),
        sorted(facts.values(), key=lambda x: (
            x.caller, x.caller_region, x.caller_island or "", x.caller_offset,
            x.callee, x.callee_offset
        )),
        sorted(implementation.values(), key=lambda x: (
            x.caller, x.caller_region, x.caller_island or "", x.caller_offset,
            x.target_island, x.target_offset
        )),
        left.code_addresses | right.code_addresses,
        sorted(transfers.values(), key=lambda x: (x.caller, x.address, x.mnemonic)),
        sorted(effects.values(), key=lambda x: (x.function, x.address, x.mnemonic)),
        left.owners,
    )


def join_value(left: AbstractValue | object, right: AbstractValue | object) -> AbstractValue | object:
    if left is UNREACHED:
        return right
    if right is UNREACHED:
        return left
    if left is UNKNOWN or right is UNKNOWN:
        return UNKNOWN
    if left == right:
        return left
    if isinstance(left, ConstSet) and isinstance(right, ConstSet):
        if left.kind != right.kind:
            return UNKNOWN
        values = left.values | right.values
        if len(values) <= 256:
            return ConstSet(left.kind, frozenset(values))
        if left.kind in {"signed", "unsigned"} and all(isinstance(x, int) for x in values):
            ints = [int(x) for x in values]
            return Interval(left.kind, min(ints), max(ints))
        return UNKNOWN
    if isinstance(left, Interval) and isinstance(right, Interval) and left.kind == right.kind:
        return Interval(left.kind, min(left.lo, right.lo), max(left.hi, right.hi))
    if isinstance(left, Interval) and isinstance(right, ConstSet):
        left, right = right, left
    if isinstance(left, ConstSet) and isinstance(right, Interval):
        if left.kind == right.kind and all(isinstance(x, int) for x in left.values):
            values = [int(x) for x in left.values]
            return Interval(right.kind, min([right.lo, *values]), max([right.hi, *values]))
    return UNKNOWN


def refine_value(value: AbstractValue, kind: str, lo: int, hi: int) -> AbstractValue | None:
    if value is UNKNOWN:
        return Interval(kind, lo, hi)
    if isinstance(value, ConstSet):
        if value.kind != kind:
            return UNKNOWN
        kept = frozenset(x for x in value.values if isinstance(x, int) and lo <= x <= hi)
        return ConstSet(kind, kept) if kept else None
    if isinstance(value, Interval):
        if value.kind != kind:
            return UNKNOWN
        nlo, nhi = max(value.lo, lo), min(value.hi, hi)
        return Interval(kind, nlo, nhi) if nlo <= nhi else None
    return UNKNOWN


def widen_interval(previous: Interval, current: Interval,
                   widened_lower: bool = False,
                   widened_upper: bool = False) -> tuple[Interval, bool, bool]:
    if previous.kind != current.kind:
        raise ValueError("cannot widen intervals of different signedness")
    minimum, maximum = ((-0x80000000, 0x7FFFFFFF)
                        if previous.kind == "signed" else (0, 0xFFFFFFFF))
    lower_expands = current.lo < previous.lo
    upper_expands = current.hi > previous.hi
    lo = minimum if lower_expands and widened_lower else current.lo
    hi = maximum if upper_expands and widened_upper else current.hi
    return Interval(previous.kind, lo, hi), widened_lower or lower_expands, widened_upper or upper_expands


def enumerate_computed_targets(
    value: AbstractValue, owners: Iterable[FunctionOwner]
) -> tuple[int, ...] | None:
    if isinstance(value, ConstSet) and all(isinstance(x, int) for x in value.values):
        values = sorted(int(x) for x in value.values)
    elif isinstance(value, Interval) and value.hi - value.lo + 1 <= 256:
        values = list(range(value.lo, value.hi + 1))
    else:
        return None
    owner_list = tuple(owners)
    if len(values) > 256 or any(value & 1 or _owner_at(owner_list, value) is None for value in values):
        return None
    return tuple(values)


SECTION_RE = re.compile(
    r"^\s*\[\s*(\d+)\]\s+(\S+)\s+\S+\s+([0-9A-Fa-f]+)\s+[0-9A-Fa-f]+\s+"
    r"([0-9A-Fa-f]+)\s+\S+\s+(\S+)"
)
SYMBOL_RE = re.compile(
    r"^\s*\d+:\s+([0-9A-Fa-f]+)\s+(\d+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)"
    r"(?:\s+(.*?))?\s*$"
)


def parse_readelf_sections(text: str) -> dict[int, ExecutableSection]:
    result: dict[int, ExecutableSection] = {}
    for line in text.splitlines():
        match = SECTION_RE.match(line)
        if match and "X" in match.group(5):
            start = int(match.group(3), 16)
            result[int(match.group(1))] = ExecutableSection(
                int(match.group(1)), match.group(2), start, start + int(match.group(4), 16)
            )
    if not result:
        raise ValueError("readelf has no executable section")
    return result


def parse_readelf_symbols(text: str, sections: dict[int, ExecutableSection]) -> list[RawFunction]:
    result: list[RawFunction] = []
    for line in text.splitlines():
        match = SYMBOL_RE.match(line)
        if match:
            section_text = match.group(6)
            section: int | str = int(section_text) if section_text.isdigit() else section_text
            result.append(RawFunction(
                (match.group(7) or "").strip(),
                int(match.group(1), 16),
                int(match.group(2)),
                section,
                match.group(4),
                match.group(5),
                match.group(3),
            ))
    if not any(
        symbol.symbol_type == "FUNC"
        and isinstance(symbol.section, int)
        and symbol.section in sections
        for symbol in result
    ):
        raise ValueError("readelf has no executable STT_FUNC symbols")
    return result


def _alias_rank(symbol: RawFunction) -> tuple[int, int, int, bytes]:
    binding = {"GLOBAL": 0, "WEAK": 1, "LOCAL": 2}.get(symbol.binding, 3)
    visibility = 0 if symbol.visibility in {"DEFAULT", "PROTECTED"} else 1
    encoded = symbol.name.encode("utf-8")
    return binding, visibility, len(encoded), encoded


def resolve_function_owners(
    symbols: list[RawFunction], sections: dict[int, ExecutableSection]
) -> tuple[FunctionOwner, ...]:
    by_section: dict[int, list[RawFunction]] = defaultdict(list)
    for symbol in symbols:
        if symbol.symbol_type == "FUNC" \
                and isinstance(symbol.section, int) \
                and symbol.section in sections:
            by_section[symbol.section].append(symbol)
    owners: list[FunctionOwner] = []
    for section_index, members in by_section.items():
        section = sections[section_index]
        starts = sorted({x.start for x in members})
        groups: dict[int, list[tuple[RawFunction, int]]] = defaultdict(list)
        for symbol in members:
            next_index = bisect_right(starts, symbol.start)
            next_start = starts[next_index] if next_index < len(starts) else section.end
            end = symbol.start + symbol.size if symbol.size else next_start
            if symbol.start < section.start or end > section.end or end <= symbol.start:
                raise ValueError(f"function range outside executable section: {symbol.name}")
            groups[symbol.start].append((symbol, end))
        for start, aliases in groups.items():
            ends = {end for _, end in aliases}
            if len(ends) != 1:
                raise ValueError(f"same-start aliases have different ends at 0x{start:x}")
            canonical = min((x for x, _ in aliases), key=_alias_rank)
            names = tuple(sorted(x.name for x, _ in aliases if x.name != canonical.name))
            owners.append(FunctionOwner(canonical.name, start, ends.pop(), section_index, names))
    owners.sort(key=lambda x: (x.start, x.end, x.name))
    active: list[FunctionOwner] = []
    active_section = -1
    for current in owners:
        if current.section != active_section:
            active.clear()
            active_section = current.section
        active = [owner for owner in active if owner.end > current.start]
        for previous in active:
            # SH runtime assembly exposes deliberate contained alternate-entry
            # functions (for example the shift and divide ladders). They are
            # ownership refinements, not ambiguous crossing ranges.
            if current.end > previous.end:
                raise ValueError(f"partial function overlap: {previous.name} and {current.name}")
        active.append(current)
    return tuple(owners)


def resolve_local_islands(
    symbols: list[RawFunction],
    sections: dict[int, ExecutableSection],
    owners: Iterable[FunctionOwner],
) -> tuple[LocalIsland, ...]:
    owner_list = tuple(owners)
    defined_by_section: dict[int, set[int]] = defaultdict(set)
    candidates_by_section: dict[int, list[RawFunction]] = defaultdict(list)
    for symbol in symbols:
        if isinstance(symbol.section, int) and symbol.section in sections:
            defined_by_section[symbol.section].add(symbol.start)
            if symbol.symbol_type == "NOTYPE" and symbol.binding == "LOCAL" and symbol.name:
                candidates_by_section[symbol.section].append(symbol)

    result: list[LocalIsland] = []
    for section_index, members in candidates_by_section.items():
        section = sections[section_index]
        duplicate_rows = {
            (item.name, item.start, item.size, item.visibility): item for item in members
        }
        members = list(duplicate_rows.values())
        names_by_start: dict[int, set[str]] = defaultdict(set)
        for member in members:
            names_by_start[member.start].add(member.name)
        ambiguous = [start for start, names in names_by_start.items() if len(names) != 1]
        if ambiguous:
            raise ValueError(f"ambiguous local-label aliases at 0x{min(ambiguous):x}")

        all_starts = sorted(defined_by_section[section_index])
        function_starts = sorted(
            owner.start for owner in owner_list if owner.section == section_index
        )
        for member in sorted(members, key=lambda item: (item.start, item.name)):
            next_index = bisect_right(all_starts, member.start)
            end = all_starts[next_index] if next_index < len(all_starts) else section.end
            function_index = bisect_right(function_starts, member.start)
            if function_index < len(function_starts):
                end = min(end, function_starts[function_index])
            if member.start < section.start or end > section.end or end <= member.start:
                continue
            overlapping = [
                owner for owner in owner_list
                if owner.section == section_index
                and member.start < owner.end and end > owner.start
            ]
            if any(owner.start > member.start for owner in overlapping):
                continue
            code_start = max(
                [member.start, *(owner.end for owner in overlapping)]
            )
            if code_start >= end:
                continue
            result.append(
                LocalIsland(member.name, member.start, code_start, end, section_index)
            )

    result.sort(key=lambda item: (item.section, item.start, item.end, item.name))
    for previous, current in zip(result, result[1:]):
        if previous.section == current.section and current.start < previous.end:
            raise ValueError(
                f"overlapping local-label islands: {previous.name} and {current.name}"
            )
    return tuple(result)


DECODED_LINE_RE = re.compile(r"\b(0x[0-9A-Fa-f]+)\b")


def parse_decoded_lines(
    text: str,
    owners: Iterable[FunctionOwner],
    owner_address_map: dict[int, FunctionOwner] | None = None,
) -> dict[str, set[int]]:
    owner_list = tuple(owners)
    owner_by_address = (
        owner_address_map
        if owner_address_map is not None
        else build_owner_address_map(owner_list)
    )
    result: dict[str, set[int]] = defaultdict(set)
    for line in text.splitlines():
        if "end_sequence" in line.lower():
            continue
        match = DECODED_LINE_RE.search(line)
        if not match:
            continue
        address = int(match.group(1), 16)
        if address & 1:
            continue
        owner = owner_by_address.get(address)
        if owner is not None and address == owner.start:
            owner = None
        if owner is not None:
            result[owner.name].add(address)
    return result


OBJDUMP_ROW_RE = re.compile(
    r"^\s*([0-9A-Fa-f]+):\s+((?:[0-9A-Fa-f]{2}\s+){1,4})([A-Za-z0-9_./]+)\s*(.*?)\s*$"
)


def parse_instructions(text: str) -> dict[int, Instruction]:
    result: dict[int, Instruction] = {}
    for line in text.splitlines():
        match = OBJDUMP_ROW_RE.match(line)
        if not match:
            continue
        mnemonic = match.group(3).lower()
        if mnemonic in {"bt/s", "bf/s"}:
            raise ValueError(f"unsupported delayed-branch spelling: {mnemonic}")
        tail = match.group(4)
        operands, _, annotation = tail.partition("!")
        result[int(match.group(1), 16)] = Instruction(
            int(match.group(1), 16),
            bytes.fromhex(match.group(2)),
            mnemonic,
            operands.strip(),
            annotation.strip(),
        )
    return result


def _owner_at(owners: tuple[FunctionOwner, ...], address: int) -> FunctionOwner | None:
    matches = [owner for owner in owners if owner.start <= address < owner.end]
    return max(matches, key=lambda owner: (owner.start, -owner.end, owner.name)) if matches else None


def build_owner_address_map(owners: tuple[FunctionOwner, ...]) -> dict[int, FunctionOwner]:
    result: dict[int, FunctionOwner] = {}
    # Owners are start-sorted; contained alternate entries overwrite their
    # enclosing assembly helper and therefore own their most-specific bytes.
    for owner in owners:
        for address in range(owner.start, owner.end, 2):
            result[address] = owner
    return result


def _target_from_text(text: str) -> int | None:
    match = re.search(r"(?:^|,\s*)(?:0x)?([0-9A-Fa-f]{6,8})(?:\s|$|<)", text)
    return int(match.group(1), 16) if match else None


def _island_for_direct_target(
    target: int,
    operands: str,
    islands: Iterable[LocalIsland],
) -> LocalIsland | None:
    matches = [
        island for island in islands if island.code_start <= target < island.end
    ]
    if len(matches) != 1:
        return None
    island = matches[0]
    annotation = re.search(r"<([^>]+)>", operands)
    if island.name.startswith(".L"):
        if annotation is None:
            return None
        displayed = annotation.group(1)
        offset = target - island.start
        expected = island.name if offset == 0 else f"{island.name}+0x{offset:x}"
        if displayed != expected:
            return None
    return island


def _symbol_from_annotation(annotation: str) -> SymbolAtom | None:
    match = re.search(r"(?:0x)?([0-9A-Fa-f]{6,8})\s+<([^>]+)>", annotation)
    if not match:
        return None
    return SymbolAtom(_symbol_base(match.group(2)), int(match.group(1), 16))


def _unknown_state() -> dict[str, AbstractValue]:
    return {f"r{x}": UNKNOWN for x in range(16)} | {
        "mach": UNKNOWN, "macl": UNKNOWN, "t_predicate": UNKNOWN
    }


def _join_state(
    old: dict[str, AbstractValue] | None,
    new: dict[str, AbstractValue],
    *,
    backedge: bool = False,
    widening: dict[str, tuple[bool, bool]] | None = None,
) -> tuple[dict[str, AbstractValue], bool]:
    if old is None:
        return dict(new), True
    merged = {key: join_value(old[key], new[key]) for key in old}
    if backedge and widening is not None:
        for register, value in merged.items():
            previous = old[register]
            if (
                isinstance(previous, ConstSet)
                and isinstance(value, ConstSet)
                and previous != value
                and value.kind in {"signed", "unsigned"}
                and all(isinstance(item, int) for item in value.values)
            ):
                numeric = [int(item) for item in value.values]
                value = merged[register] = Interval(
                    value.kind, min(numeric), max(numeric)
                )
            if not isinstance(previous, Interval) or not isinstance(value, Interval):
                continue
            if previous.kind != value.kind:
                continue
            lower, upper = widening.get(register, (False, False))
            merged[register], lower, upper = widen_interval(
                previous, value, lower, upper
            )
            widening[register] = (lower, upper)
    return merged, merged != old


def _interval_add_32(value: Interval, delta: int) -> Interval:
    minimum, maximum = ((-0x80000000, 0x7FFFFFFF)
                        if value.kind == "signed" else (0, 0xFFFFFFFF))
    lo, hi = value.lo + delta, value.hi + delta
    if lo < minimum or hi > maximum:
        return Interval(value.kind, minimum, maximum)
    return Interval(value.kind, lo, hi)


def build_instruction_memory(instructions: dict[int, Instruction]) -> dict[int, int]:
    memory: dict[int, int] = {}
    for instruction in instructions.values():
        for offset, byte in enumerate(instruction.raw_bytes):
            memory[instruction.address + offset] = byte
    return memory


def comparison_predicate(
    instruction: Instruction | None,
    state: dict[str, AbstractValue],
) -> ComparisonPredicate | _Unknown:
    if instruction is None:
        return UNKNOWN
    binary = re.fullmatch(
        r"(r(?:1[0-5]|\d)),\s*(r(?:1[0-5]|\d))", instruction.operands
    )
    single = re.fullmatch(r"(r(?:1[0-5]|\d))", instruction.operands)
    register: str | None = None
    true_range: tuple[str, int, int] | None = None
    false_range: tuple[str, int, int] | None = None
    if instruction.mnemonic in {"cmp/hi", "cmp/hs", "cmp/gt", "cmp/ge"} and binary:
        threshold_value = state[binary.group(1)]
        thresholds = (
            threshold_value.values
            if isinstance(threshold_value, ConstSet)
            and threshold_value.kind in {"signed", "unsigned"}
            else ()
        )
        if len(thresholds) == 1:
            threshold = next(iter(thresholds))
            if isinstance(threshold, int):
                register = binary.group(2)
                signed = instruction.mnemonic in {"cmp/gt", "cmp/ge"}
                kind = "signed" if signed else "unsigned"
                threshold &= 0xFFFFFFFF
                if signed and threshold & 0x80000000:
                    threshold -= 0x100000000
                minimum, maximum = ((-0x80000000, 0x7FFFFFFF)
                                    if signed else (0, 0xFFFFFFFF))
                strict = instruction.mnemonic in {"cmp/hi", "cmp/gt"}
                true_range = (kind, threshold + (1 if strict else 0), maximum)
                false_range = (kind, minimum, threshold - (0 if strict else 1))
    elif instruction.mnemonic in {"cmp/pz", "cmp/pl"} and single:
        register = single.group(1)
        lower = 1 if instruction.mnemonic == "cmp/pl" else 0
        true_range = ("signed", lower, 0x7FFFFFFF)
        false_range = ("signed", -0x80000000, lower - 1)
    if register is None or true_range is None or false_range is None:
        return UNKNOWN
    return ComparisonPredicate(register, true_range, false_range)


def _predicate_refined_states(
    predicate: ComparisonPredicate | _Unknown,
    state: dict[str, AbstractValue],
) -> tuple[dict[str, AbstractValue] | None, dict[str, AbstractValue] | None]:
    true_state, false_state = dict(state), dict(state)
    if predicate is UNKNOWN:
        return true_state, false_state

    def apply_range(
        target: dict[str, AbstractValue], bounds: tuple[str, int, int]
    ) -> dict[str, AbstractValue] | None:
        if bounds[1] > bounds[2]:
            return None
        current = target[predicate.register]
        if isinstance(current, (ConstSet, Interval)) \
                and current.kind in {"signed", "unsigned"} \
                and current.kind != bounds[0]:
            shared_maximum = 0x7FFFFFFF
            if isinstance(current, ConstSet) \
                    and all(isinstance(x, int) for x in current.values) \
                    and all(0 <= int(x) <= shared_maximum for x in current.values) \
                    and bounds[1] >= 0:
                kept = frozenset(
                    x for x in current.values if bounds[1] <= int(x) <= bounds[2]
                )
                refined = ConstSet(current.kind, kept) if kept else None
            elif isinstance(current, Interval) \
                    and 0 <= current.lo <= current.hi <= shared_maximum \
                    and bounds[1] >= 0:
                lo, hi = max(current.lo, bounds[1]), min(current.hi, bounds[2])
                refined = Interval(current.kind, lo, hi) if lo <= hi else None
            elif isinstance(current, Interval) \
                    and current.kind == "signed" \
                    and bounds[0] == "unsigned" \
                    and 0 <= bounds[1] <= bounds[2] <= shared_maximum:
                # This unsigned range contains only bit patterns whose signed
                # interpretation is also nonnegative. Negative signed inputs
                # therefore cannot satisfy it, while the retained range stays
                # convex in signed order. Do not retag the complementary,
                # potentially non-convex outcome.
                lo = max(current.lo, 0, bounds[1])
                hi = min(current.hi, bounds[2])
                refined = Interval("signed", lo, hi) if lo <= hi else None
            else:
                refined = UNKNOWN
        else:
            refined = refine_value(current, *bounds)
        if refined is None:
            return None
        target[predicate.register] = refined
        return target

    return (
        apply_range(true_state, predicate.true_range),
        apply_range(false_state, predicate.false_range),
    )


def comparison_refined_states(
    instruction: Instruction | None,
    state: dict[str, AbstractValue],
) -> tuple[dict[str, AbstractValue] | None, dict[str, AbstractValue] | None]:
    """Return states for T=true and T=false after a modeled comparison."""
    return _predicate_refined_states(comparison_predicate(instruction, state), state)


def uncovered_decoded_line_seeds(
    decoded_lines: dict[str, set[int]],
    selected_names: set[str],
    covered_addresses: set[int],
) -> Iterator[tuple[str, int]]:
    """Yield seeds not yet covered; callers may extend coverage between yields."""
    for name in sorted(decoded_lines):
        if name not in selected_names:
            continue
        for address in sorted(decoded_lines[name]):
            if address not in covered_addresses:
                yield name, address


def _write_effect(instruction: Instruction, state: dict[str, AbstractValue],
                  effects: list[UnresolvedEffect], owner: FunctionOwner,
                  memory: dict[int, int] | None = None,
                  profile: Counter[str] | None = None) -> None:
    text = instruction.operands
    mnemonic = instruction.mnemonic
    if mnemonic.startswith("cmp/") or mnemonic == "tst":
        state["t_predicate"] = comparison_predicate(instruction, state)
        return
    t_neutral = {
        "nop", "mov", "mov.l", "mov.w", "mov.b", "mova",
        "add", "and", "extu.b", "extu.w", "exts.b", "exts.w",
        "lds", "lds.l", "sts", "sts.l",
    }
    if mnemonic not in t_neutral:
        state["t_predicate"] = UNKNOWN
    literal = _symbol_from_annotation(instruction.annotation)
    destination = re.search(r",\s*(r(?:1[0-5]|\d))\s*$", text)
    single_destination = re.fullmatch(r"(r(?:1[0-5]|\d))", text)
    if mnemonic in {"mov.l", "mov.w"} and literal and destination:
        state[destination.group(1)] = ConstSet("symbol", frozenset({literal}))
        return
    numeric_literal = re.fullmatch(r"(?:0x)?([0-9A-Fa-f]+)", instruction.annotation)
    if mnemonic in {"mov.l", "mov.w"} and numeric_literal and destination:
        value = int(numeric_literal.group(1), 16)
        bits = 16 if mnemonic == "mov.w" else 32
        if value & (1 << (bits - 1)):
            value -= 1 << bits
        state[destination.group(1)] = ConstSet("signed", frozenset({value}))
        return
    dereference = re.fullmatch(
        r"@(r(?:1[0-5]|\d)),\s*(r(?:1[0-5]|\d))", text
    )
    if mnemonic in {"mov.l", "mov.w", "mov.b"} and dereference:
        base = state[dereference.group(1)]
        if mnemonic == "mov.b":
            state[dereference.group(2)] = Interval("signed", -0x80, 0x7F)
            return
        if mnemonic == "mov.w":
            state[dereference.group(2)] = Interval("signed", -0x8000, 0x7FFF)
            return
        atoms = base.values if isinstance(base, ConstSet) and base.kind == "symbol" else ()
        if len(atoms) == 1:
            atom = next(iter(atoms))
            if isinstance(atom, SymbolAtom):
                state[dereference.group(2)] = ConstSet(
                    "symbol", frozenset({SymbolAtom(f"{atom.name}*", atom.address)})
                )
                return
        state[dereference.group(2)] = UNKNOWN
        return
    memory_load = re.fullmatch(
        r"@\((\d+),(r(?:1[0-5]|\d))\),\s*(r(?:1[0-5]|\d))", text
    )
    if mnemonic in {"mov.l", "mov.w", "mov.b"} and memory_load:
        base = state[memory_load.group(2)]
        if mnemonic == "mov.b":
            state[memory_load.group(3)] = Interval("signed", -0x80, 0x7F)
            return
        if mnemonic == "mov.w":
            state[memory_load.group(3)] = Interval("signed", -0x8000, 0x7FFF)
            return
        atoms = base.values if isinstance(base, ConstSet) and base.kind == "symbol" else ()
        if len(atoms) == 1:
            atom = next(iter(atoms))
            if isinstance(atom, SymbolAtom):
                offset = int(memory_load.group(1))
                derived = SymbolAtom(f"{atom.name}+{offset}", atom.address + offset)
                state[memory_load.group(3)] = ConstSet("symbol", frozenset({derived}))
                return
        state[memory_load.group(3)] = UNKNOWN
        return
    indexed_load = re.fullmatch(
        r"@\((r(?:1[0-5]|\d)),(r(?:1[0-5]|\d))\),\s*(r(?:1[0-5]|\d))", text
    )
    if mnemonic in {"mov.l", "mov.w", "mov.b"} and indexed_load:
        operands = (state[indexed_load.group(1)], state[indexed_load.group(2)])
        for bases, indexes in (operands, operands[::-1]):
            if isinstance(bases, ConstSet) and bases.kind in {"signed", "unsigned"} \
                    and len(bases.values) == 1 and memory is not None:
                base_value = next(iter(bases.values))
                index_values: list[int] = []
                if isinstance(indexes, ConstSet) \
                        and all(isinstance(x, int) for x in indexes.values):
                    index_values = [int(x) for x in indexes.values]
                elif isinstance(indexes, Interval) and indexes.hi - indexes.lo + 1 <= 256:
                    index_values = list(range(indexes.lo, indexes.hi + 1))
                width = {"mov.b": 1, "mov.w": 2, "mov.l": 4}[mnemonic]
                loaded: list[int] = []
                for index in index_values:
                    address = int(base_value) + index
                    raw = [memory.get(address + offset) for offset in range(width)]
                    if any(value is None for value in raw):
                        loaded = []
                        break
                    value = 0
                    for byte in raw:
                        value = (value << 8) | int(byte)
                    if mnemonic == "mov.b" and value & 0x80:
                        value -= 0x100
                    if mnemonic == "mov.w" and value & 0x8000:
                        value -= 0x10000
                    loaded.append(value)
                if loaded:
                    state[indexed_load.group(3)] = ConstSet("signed", frozenset(loaded))
                    return
            atoms = bases.values if isinstance(bases, ConstSet) and bases.kind == "symbol" else ()
            if len(atoms) == 1:
                atom = next(iter(atoms))
                if isinstance(atom, SymbolAtom):
                    if mnemonic == "mov.b":
                        state[indexed_load.group(3)] = Interval("signed", -0x80, 0x7F)
                    elif mnemonic == "mov.w":
                        state[indexed_load.group(3)] = Interval(
                            "signed", -0x8000, 0x7FFF
                        )
                    else:
                        derived = SymbolAtom(f"{atom.name}[]", atom.address)
                        state[indexed_load.group(3)] = ConstSet(
                            "symbol", frozenset({derived})
                        )
                    return
        state[indexed_load.group(3)] = UNKNOWN
        return
    immediate = re.fullmatch(r"#(-?(?:0x[0-9a-fA-F]+|\d+)),\s*(r(?:1[0-5]|\d))", text)
    if mnemonic == "mov" and immediate:
        state[immediate.group(2)] = ConstSet("signed", frozenset({int(immediate.group(1), 0)}))
        return
    if mnemonic == "and" and immediate:
        mask = int(immediate.group(1), 0)
        state[immediate.group(2)] = Interval("unsigned", 0, mask)
        return
    if mnemonic == "add" and immediate:
        delta = int(immediate.group(1), 0)
        value = state[immediate.group(2)]
        if isinstance(value, ConstSet) and all(isinstance(x, int) for x in value.values):
            state[immediate.group(2)] = ConstSet(
                value.kind, frozenset(int(x) + delta for x in value.values)
            )
        elif isinstance(value, Interval):
            state[immediate.group(2)] = _interval_add_32(value, delta)
        else:
            state[immediate.group(2)] = UNKNOWN
        return
    move = re.fullmatch(r"(r(?:1[0-5]|\d)),\s*(r(?:1[0-5]|\d))", text)
    if mnemonic == "mov" and move:
        state[move.group(2)] = state[move.group(1)]
        return
    binary = re.fullmatch(
        r"(r(?:1[0-5]|\d)),\s*(r(?:1[0-5]|\d))", text
    )
    if mnemonic == "add" and binary:
        source, target = state[binary.group(1)], state[binary.group(2)]
        if isinstance(source, ConstSet) and isinstance(target, ConstSet) \
                and source.kind == target.kind \
                and source.kind in {"signed", "unsigned"} \
                and all(isinstance(x, int) for x in source.values | target.values):
            values: set[int] = set()
            for left in source.values:
                for right in target.values:
                    value = (int(left) + int(right)) & 0xFFFFFFFF
                    if source.kind == "signed" and value & 0x80000000:
                        value -= 0x100000000
                    values.add(value)
            if len(values) <= 256:
                state[binary.group(2)] = ConstSet(source.kind, frozenset(values))
            else:
                state[binary.group(2)] = Interval(source.kind, min(values), max(values))
        elif isinstance(source, Interval) and isinstance(target, Interval) \
                and source.kind == target.kind:
            if binary.group(1) == binary.group(2) \
                    and source.hi - source.lo + 1 <= 256:
                if profile is not None:
                    profile["doubling_enumerations"] += 1
                doubled: set[int] = set()
                for item in range(source.lo, source.hi + 1):
                    result = (item + item) & 0xFFFFFFFF
                    if source.kind == "signed" and result & 0x80000000:
                        result -= 0x100000000
                    doubled.add(result)
                values = frozenset(doubled)
                state[binary.group(2)] = ConstSet(source.kind, values)
                return
            minimum, maximum = ((-0x80000000, 0x7FFFFFFF)
                                if source.kind == "signed" else (0, 0xFFFFFFFF))
            lo, hi = source.lo + target.lo, source.hi + target.hi
            state[binary.group(2)] = (
                Interval(source.kind, lo, hi)
                if minimum <= lo <= hi <= maximum
                else Interval(source.kind, minimum, maximum)
            )
        else:
            state[binary.group(2)] = UNKNOWN
        return
    if mnemonic == "and" and binary:
        source, target = state[binary.group(1)], state[binary.group(2)]
        if isinstance(source, ConstSet) and len(source.values) == 1:
            mask = next(iter(source.values))
            if isinstance(mask, int) and mask >= 0:
                state[binary.group(2)] = Interval("unsigned", 0, mask)
                return
        state[binary.group(2)] = UNKNOWN
        return
    if mnemonic == "extu.b" and binary:
        source = state[binary.group(1)]
        if isinstance(source, ConstSet) and all(isinstance(x, int) for x in source.values):
            state[binary.group(2)] = ConstSet(
                "unsigned", frozenset(int(x) & 0xFF for x in source.values)
            )
        else:
            state[binary.group(2)] = Interval("unsigned", 0, 0xFF)
        return
    if mnemonic in {"shll", "shll2", "shll8", "shll16"} and single_destination:
        value = state[single_destination.group(1)]
        shift = {"shll": 1, "shll2": 2, "shll8": 8, "shll16": 16}[mnemonic]
        if isinstance(value, ConstSet) and all(isinstance(x, int) for x in value.values):
            state[single_destination.group(1)] = ConstSet(
                value.kind, frozenset((int(x) << shift) & 0xFFFFFFFF for x in value.values)
            )
        elif isinstance(value, Interval):
            minimum, maximum = ((-0x80000000, 0x7FFFFFFF)
                                if value.kind == "signed" else (0, 0xFFFFFFFF))
            if value.hi - value.lo + 1 <= 256:
                shifted = frozenset(
                    (item << shift) & 0xFFFFFFFF
                    for item in range(value.lo, value.hi + 1)
                )
                state[single_destination.group(1)] = ConstSet(value.kind, shifted)
            else:
                lo, hi = value.lo << shift, value.hi << shift
                state[single_destination.group(1)] = (
                    Interval(value.kind, lo, hi)
                    if minimum <= lo <= hi <= maximum
                    else Interval(value.kind, minimum, maximum)
                )
        else:
            state[single_destination.group(1)] = UNKNOWN
        return
    if mnemonic == "mova" and destination:
        address = _target_from_text(text)
        state[destination.group(1)] = (
            ConstSet("unsigned", frozenset({address})) if address is not None else UNKNOWN
        )
        return
    if destination:
        state[destination.group(1)] = UNKNOWN
        return
    if single_destination and mnemonic not in {"cmp/pl", "cmp/pz"}:
        state[single_destination.group(1)] = UNKNOWN
        return
    if mnemonic in {"mov.l", "mov.w", "mov.b", "sts.l"} and ",@" in text:
        return
    if mnemonic == "lds.l" and text.endswith(",pr"):
        return
    known_no_write = {
        "nop", "tst", "cmp/eq", "cmp/hs", "cmp/hi", "cmp/ge", "cmp/gt",
        "cmp/pl", "cmp/pz",
        "bt", "bf", "bt.s", "bf.s", "bra", "bsr", "bsrf", "braf",
        "jsr", "jmp", "rts", "rte", ".word",
    }
    if mnemonic not in known_no_write and re.search(r"\br(?:1[0-5]|\d)\b", text):
        effects.append(UnresolvedEffect(owner.name, instruction.address, mnemonic, text,
                                        "unparseable register effect"))


def analyze_code_only(
    instructions: dict[int, Instruction],
    owners: Iterable[FunctionOwner],
    decoded_lines: dict[str, set[int]] | None = None,
    selected_names: set[str] | None = None,
    instruction_memory: dict[int, int] | None = None,
    max_steps_per_seed: int = 100000,
    include_owner_entry: bool = True,
    owner_address_map: dict[int, FunctionOwner] | None = None,
    stop_at_addresses: set[int] | None = None,
    local_islands: Iterable[LocalIsland] = (),
    profile_by_owner: dict[str, Counter[str]] | None = None,
) -> CodeAnalysis:
    owner_list = tuple(owners)
    owner_by_address = (
        owner_address_map
        if owner_address_map is not None
        else build_owner_address_map(owner_list)
    )
    memory = (
        instruction_memory
        if instruction_memory is not None
        else build_instruction_memory(instructions)
    )
    lines = decoded_lines or {}
    island_list = tuple(local_islands)
    code_addresses: set[int] = set()
    calls_by_site: dict[tuple[str, int, str], CallSite] = {}
    facts_by_site: dict[tuple[object, ...], DirectCallFact] = {}
    implementation_by_site: dict[tuple[object, ...], ImplementationTransferFact] = {}
    unresolved: dict[tuple[str, int, str], UnresolvedTransfer] = {}
    effects: dict[tuple[str, int, str], UnresolvedEffect] = {}

    for owner in owner_list:
        if selected_names is not None and owner.name not in selected_names:
            continue
        seeds = [
            *([owner.start] if include_owner_entry else []),
            *sorted(lines.get(owner.name, set())),
        ]
        profile = (
            profile_by_owner.setdefault(owner.name, Counter())
            if profile_by_owner is not None else None
        )
        states: dict[tuple[object, ...], dict[str, AbstractValue]] = {}
        widening: dict[tuple[object, ...], dict[str, tuple[bool, bool]]] = {}
        queue: deque[
            tuple[int, LocalIsland | None, int, int, dict[str, AbstractValue], bool]
        ] = deque(
            (seed, None, seed, seed, _unknown_state(), False)
            for seed in seeds if seed in instructions
        )
        instruction_count = sum(
            1 for address in range(owner.start, owner.end, 2) if address in instructions
        )
        instruction_count += sum(
            1
            for candidate in island_list
            for address in range(candidate.code_start, candidate.end, 2)
            if address in instructions
        )
        aggregate_step_limit = max(1, instruction_count) * max(1, len(seeds)) * 512
        aggregate_steps = 0
        steps_by_seed: defaultdict[int, int] = defaultdict(int)
        while queue:
            seed, island, island_entry, address, incoming, is_backedge = queue.popleft()
            if profile is not None:
                profile["worklist_states"] += 1
                profile["max_constset_cardinality"] = max(
                    profile["max_constset_cardinality"],
                    max(
                        (
                            len(value.values)
                            for value in incoming.values()
                            if isinstance(value, ConstSet)
                        ),
                        default=0,
                    ),
                )
            aggregate_steps += 1
            if aggregate_steps > aggregate_step_limit:
                raise ValueError(
                    f"code-only aggregate fixed point did not converge: {owner.name}"
                )
            steps_by_seed[seed] += 1
            if steps_by_seed[seed] > max_steps_per_seed:
                raise ValueError(f"code-only fixed point did not converge: {owner.name}")
            region_start = island.code_start if island is not None else owner.start
            region_end = island.end if island is not None else owner.end
            if not (region_start <= address < region_end) or address not in instructions:
                continue
            if island is None and stop_at_addresses is not None and address in stop_at_addresses:
                if profile is not None:
                    profile["stop_at_hits"] += 1
                continue
            key = (
                ("island", island.name, island_entry, address)
                if island is not None
                else ("owner", seed, address)
            )
            if profile is not None and key in states:
                profile["revisits"] += 1
            state, changed = _join_state(
                states.get(key),
                incoming,
                backedge=is_backedge,
                widening=widening.setdefault(key, {}),
            )
            if not changed:
                continue
            states[key] = state
            code_addresses.add(address)
            ins = instructions[address]
            mnemonic = ins.mnemonic
            caller_region = "island" if island is not None else "owner"
            caller_island = island.name if island is not None else None
            caller_offset = address - (island.start if island is not None else owner.start)

            def schedule(target: int, next_state: dict[str, AbstractValue]) -> None:
                if region_start <= target < region_end and target in instructions:
                    queue.append(
                        (seed, island, island_entry, target, next_state, target <= address)
                    )

            def schedule_island(
                target_island: LocalIsland,
                entry: int,
                next_state: dict[str, AbstractValue],
            ) -> None:
                if entry in instructions:
                    queue.append((seed, target_island, entry, entry, next_state, False))

            def after_slot(base_state: dict[str, AbstractValue]) -> dict[str, AbstractValue]:
                slot_address = address + 2
                result = dict(base_state)
                if slot_address in instructions and region_start <= slot_address < region_end:
                    code_addresses.add(slot_address)
                    slot_effects: list[UnresolvedEffect] = []
                    _write_effect(
                        instructions[slot_address], result, slot_effects, owner, memory,
                        profile,
                    )
                    for effect in slot_effects:
                        effects[(effect.function, effect.address, effect.mnemonic)] = effect
                return result

            direct_target = _target_from_text(ins.operands)
            if mnemonic in {"bsr", "bsrf", "jsr"}:
                target_address = direct_target
                resolved_atom: SymbolAtom | None = None
                if mnemonic in {"jsr", "bsrf"}:
                    register = re.search(r"@?(r(?:1[0-5]|\d))", ins.operands)
                    value = state.get(register.group(1), UNKNOWN) if register else UNKNOWN
                    atoms = value.values if isinstance(value, ConstSet) and value.kind == "symbol" else ()
                    if len(atoms) == 1:
                        atom = next(iter(atoms))
                        resolved_atom = atom if isinstance(atom, SymbolAtom) else None
                        target_address = resolved_atom.address if resolved_atom else None
                    elif mnemonic == "bsrf" and isinstance(value, ConstSet) and value.kind != "symbol":
                        ints = [x for x in value.values if isinstance(x, int)]
                        target_address = address + 4 + ints[0] if len(ints) == 1 else None
                    else:
                        target_address = None
                if target_address is None:
                    if mnemonic == "jsr":
                        register_name = register.group(1) if register else "unknown"
                        synthetic = f"<indirect:{register_name}>"
                        calls_by_site[(owner.name, address, synthetic)] = CallSite(
                            owner.name, address, synthetic
                        )
                        facts_by_site[(owner.name, address, synthetic, 0)] = DirectCallFact(
                            owner.name, caller_offset, synthetic, 0, 1,
                            caller_region, caller_island
                        )
                    else:
                        unresolved[(owner.name, address, mnemonic)] = UnresolvedTransfer(
                            owner.name, address, mnemonic
                        )
                else:
                    callee = owner_by_address.get(target_address)
                    target_island = (
                        None if callee is not None
                        else _island_for_direct_target(target_address, ins.operands, island_list)
                    )
                    if target_island is not None:
                        implementation_by_site[(
                            owner.name, caller_region, caller_island, caller_offset,
                            target_island.name, target_address - target_island.start,
                        )] = ImplementationTransferFact(
                            owner.name,
                            caller_offset,
                            target_island.name,
                            target_address - target_island.start,
                            1,
                            caller_region,
                            caller_island,
                        )
                        callee_entry = after_slot(state)
                        schedule_island(target_island, target_address, callee_entry)
                        post = dict(callee_entry)
                        for register in [
                            *(f"r{x}" for x in range(8)),
                            "mach", "macl", "t_predicate",
                        ]:
                            post[register] = UNKNOWN
                        schedule(address + 4, post)
                        unresolved.pop((owner.name, address, mnemonic), None)
                        continue
                    if callee is None and mnemonic in {"jsr", "bsrf"} \
                            and resolved_atom is not None and (
                                "+" in resolved_atom.name or "[]" in resolved_atom.name
                                or resolved_atom.name.endswith("*")
                            ):
                        synthetic = f"<indirect:{resolved_atom.name}>"
                        calls_by_site[(owner.name, address, synthetic)] = CallSite(
                            owner.name, address, synthetic
                        )
                        facts_by_site[(owner.name, address, synthetic, 0)] = DirectCallFact(
                            owner.name, caller_offset, synthetic, 0, 1,
                            caller_region, caller_island
                        )
                        unresolved.pop((owner.name, address, mnemonic), None)
                    elif callee is None:
                        unresolved[(owner.name, address, mnemonic)] = UnresolvedTransfer(
                            owner.name, address, mnemonic
                        )
                    else:
                        calls_by_site[(owner.name, address, callee.name)] = CallSite(
                            owner.name, address, callee.name
                        )
                        facts_by_site[(owner.name, address, callee.name, target_address - callee.start)] = (
                            DirectCallFact(
                                owner.name, caller_offset, callee.name,
                                target_address - callee.start, 1,
                                caller_region, caller_island
                            )
                        )
                        unresolved.pop((owner.name, address, mnemonic), None)
                post = after_slot(state)
                for register in [
                    *(f"r{x}" for x in range(8)), "mach", "macl", "t_predicate"
                ]:
                    post[register] = UNKNOWN
                schedule(address + 4, post)
                continue
            if mnemonic in {"jmp", "braf"}:
                register = re.search(r"@?(r(?:1[0-5]|\d))", ins.operands)
                value = state.get(register.group(1), UNKNOWN) if register else UNKNOWN
                targets: list[int] = []
                if isinstance(value, ConstSet) and all(isinstance(x, int) for x in value.values):
                    targets = [int(x) + (address + 4 if mnemonic == "braf" else 0) for x in value.values]
                elif isinstance(value, ConstSet) and value.kind == "symbol":
                    symbol_atoms = [x for x in value.values if isinstance(x, SymbolAtom)]
                    if mnemonic == "jmp" and len(symbol_atoms) == 1 and (
                        symbol_atoms[0].name.endswith("*") or "[]" in symbol_atoms[0].name
                    ):
                        synthetic = f"<indirect:{symbol_atoms[0].name}>"
                        calls_by_site[(owner.name, address, synthetic)] = CallSite(
                            owner.name, address, synthetic
                        )
                        facts_by_site[(owner.name, address, synthetic, 0)] = DirectCallFact(
                            owner.name, caller_offset, synthetic, 0, 1,
                            caller_region, caller_island
                        )
                        after_slot(state)
                        continue
                    targets = [x.address for x in symbol_atoms]
                elif isinstance(value, Interval) and value.hi - value.lo + 1 <= 256:
                    targets = list(range(value.lo, value.hi + 1))
                validated_targets = (
                    tuple(sorted(set(targets))) if targets and len(set(targets)) <= 256
                    and all(not (target & 1) and target in owner_by_address for target in targets)
                    else None
                )
                if validated_targets is None:
                    unresolved[(owner.name, address, mnemonic)] = UnresolvedTransfer(
                        owner.name, address, mnemonic
                    )
                else:
                    post = after_slot(state)
                    for target in validated_targets:
                        callee = owner_by_address.get(target)
                        if callee and callee.name == owner.name:
                            schedule(target, post)
                        elif callee:
                            facts_by_site[(owner.name, address, callee.name, target - callee.start)] = (
                                DirectCallFact(
                                    owner.name, caller_offset, callee.name,
                                    target - callee.start, 1,
                                    caller_region, caller_island
                                )
                            )
                    unresolved.pop((owner.name, address, mnemonic), None)
                continue
            if mnemonic in {"rts", "rte"}:
                after_slot(state)
                continue
            if mnemonic == "bra":
                post = after_slot(state)
                if direct_target is None:
                    unresolved[(owner.name, address, mnemonic)] = UnresolvedTransfer(
                        owner.name, address, mnemonic
                    )
                else:
                    direct_owner = owner_by_address.get(direct_target)
                    target_island = (
                        None if direct_owner is not None
                        else _island_for_direct_target(direct_target, ins.operands, island_list)
                    )
                    if target_island is not None:
                        implementation_by_site[(
                            owner.name, caller_region, caller_island, caller_offset,
                            target_island.name, direct_target - target_island.start,
                        )] = ImplementationTransferFact(
                            owner.name,
                            caller_offset,
                            target_island.name,
                            direct_target - target_island.start,
                            1,
                            caller_region,
                            caller_island,
                        )
                        schedule_island(target_island, direct_target, post)
                    else:
                        schedule(direct_target, post)
                continue
            if mnemonic in {"bt", "bf"}:
                true_state, false_state = _predicate_refined_states(
                    state["t_predicate"], state
                )
                if profile is not None:
                    variants = int(true_state is not None) + int(false_state is not None)
                    profile["predicate_variants"] += variants
                    profile["interval_splits"] += int(variants == 2)
                taken, fallthrough = (
                    (true_state, false_state) if mnemonic == "bt"
                    else (false_state, true_state)
                )
                if direct_target is not None and taken is not None:
                    schedule(direct_target, taken)
                if fallthrough is not None:
                    schedule(address + 2, fallthrough)
                continue
            if mnemonic in {"bt.s", "bf.s"}:
                true_state, false_state = _predicate_refined_states(
                    state["t_predicate"], state
                )
                if profile is not None:
                    variants = int(true_state is not None) + int(false_state is not None)
                    profile["predicate_variants"] += variants
                    profile["interval_splits"] += int(variants == 2)
                taken, fallthrough = (
                    (true_state, false_state) if mnemonic == "bt.s"
                    else (false_state, true_state)
                )
                if direct_target is not None and taken is not None:
                    schedule(direct_target, after_slot(taken))
                if fallthrough is not None:
                    schedule(address + 4, after_slot(fallthrough))
                continue
            next_state = dict(state)
            new_effects: list[UnresolvedEffect] = []
            _write_effect(ins, next_state, new_effects, owner, memory, profile)
            for effect in new_effects:
                effects[(effect.function, effect.address, effect.mnemonic)] = effect
            schedule(address + 2, next_state)

    resolved_addresses = {(call.caller, call.address) for call in calls_by_site.values()}
    unresolved = {
        key: item for key, item in unresolved.items()
        if (item.caller, item.address) not in resolved_addresses
    }
    calls = sorted(calls_by_site.values(), key=lambda x: (x.caller, x.address, x.helper))
    return CodeAnalysis(
        calls, sorted(facts_by_site.values(),
                      key=lambda x: (
                          x.caller, x.caller_region, x.caller_island or "",
                          x.caller_offset, x.callee, x.callee_offset
                      )),
        sorted(implementation_by_site.values(), key=lambda x: (
            x.caller, x.caller_region, x.caller_island or "",
            x.caller_offset, x.target_island, x.target_offset
        )),
        code_addresses,
        sorted(unresolved.values(), key=lambda x: (x.caller, x.address, x.mnemonic)),
        sorted(effects.values(), key=lambda x: (x.function, x.address, x.mnemonic)),
        owner_list,
    )


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
SIM_AUDIT_CONTRACT_V2_SHA256 = "87dabb51adc1c1cb6b646a826977658de305df086d1cfb21fc2c97a0bd6127e2"

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


def parse_audit_contract(text: str) -> AuditContract:
    """Parse a fixed post-conversion total and forbidden converted callers."""
    version: int | None = None
    expected_root: str | None = None
    expected_total: int | None = None
    forbidden_callers: set[str] = set()
    for line_number, raw_line in enumerate(text.splitlines(), start=1):
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if parts[0] == "AUDIT_CONTRACT_VERSION" and len(parts) == 2:
            if version is not None:
                raise ValueError(f"audit contract line {line_number}: duplicate version")
            version = int(parts[1], 10)
        elif parts[0] == "EXPECTED_ROOT" and len(parts) == 2:
            if expected_root is not None:
                raise ValueError(f"audit contract line {line_number}: duplicate expected root")
            expected_root = parts[1]
        elif parts[0] == "EXPECTED_TOTAL" and len(parts) == 2:
            if expected_total is not None:
                raise ValueError(f"audit contract line {line_number}: duplicate expected total")
            expected_total = int(parts[1], 10)
        elif parts[0] == "FORBIDDEN_CALLER" and len(parts) == 2:
            if parts[1] in forbidden_callers:
                raise ValueError(f"audit contract line {line_number}: duplicate forbidden caller")
            forbidden_callers.add(parts[1])
        else:
            raise ValueError(f"audit contract line {line_number}: invalid directive")
    if version != 2 or expected_root is None or expected_total is None or expected_total < 0:
        raise ValueError("audit contract requires v2 root and non-negative expected total")
    if not forbidden_callers:
        raise ValueError("audit contract requires at least one forbidden caller")
    return AuditContract(version, expected_root, expected_total, frozenset(forbidden_callers))


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


def verify_audit_contract_integrity(text: str, contract: AuditContract, *, expected_digest: str = SIM_AUDIT_CONTRACT_V2_SHA256) -> None:
    if contract.version != 2:
        raise ValueError(f"unsupported audit contract version {contract.version}")
    if expected_digest == "PENDING" or baseline_digest(text) != expected_digest:
        raise ValueError("immutable audit contract digest mismatch")


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


def audit_failures(calls: Iterable[CallSite], route_functions: set[str], oracle: RouteOracle,
                   contract: AuditContract) -> list[str]:
    """Enforce the pinned post-conversion total and absence of helper regressions."""
    failures: list[str] = []
    if contract.expected_root not in oracle.roots:
        failures.append(f"audit root missing: expected {contract.expected_root}")
    observed = Counter((call.caller, call.helper) for call in calls if call.caller in route_functions)
    actual_total = sum(observed.values())
    if actual_total != contract.expected_total:
        failures.append(
            "audit total differs from fixed post-conversion baseline "
            f"{contract.expected_total}, found {actual_total}"
        )
    for (caller, helper), actual in sorted(observed.items()):
        if caller in contract.forbidden_callers:
            failures.append(
                f"audit forbidden caller uses native math: {caller} {helper} found {actual}"
            )
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
        result = subprocess.run(
            command,
            check=True,
            capture_output=True,
            text=True,
            env=sh_tool_environment() if Path(addr2line).is_absolute() else None,
        )
        lines = result.stdout.splitlines()
        for index, address in enumerate(batch):
            source_index = index * 2 + 1
            locations[address] = lines[source_index] if source_index < len(lines) else "??:0"
    return locations


def sh_tool_environment() -> dict[str, str]:
    environment = os.environ.copy()
    prefix = r"C:\msys64\usr\bin"
    environment["PATH"] = prefix + os.pathsep + environment.get("PATH", "")
    return environment


def run_command(command: list[str]) -> str:
    return subprocess.run(
        command, check=True, capture_output=True, text=True,
        env=sh_tool_environment() if Path(command[0]).is_absolute() else None,
    ).stdout


def file_digest(path: Path) -> str:
    return sha256(path.read_bytes()).hexdigest()


def _legacy_observation_facts(
    calls: list[CallSite], closure: set[str], owners: tuple[FunctionOwner, ...]
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    owner_by_name = {owner.name: owner for owner in owners}
    direct = Counter()
    helper = Counter()
    for call in calls:
        if call.caller not in closure:
            continue
        owner = owner_by_name.get(call.caller)
        caller_offset = call.address - owner.start if owner else 0
        row = (call.caller, caller_offset, call.helper, 0)
        direct[row] += 1
        if is_native_math_helper(call.helper):
            helper[row] += 1
    def rows(values: Counter[tuple[str, int, str, int]], callee_key: str) -> list[dict[str, Any]]:
        return [
            {
                "caller": caller, "caller_region": "owner", "caller_island": None,
                "caller_offset": caller_offset,
                callee_key: callee, f"{callee_key}_offset": callee_offset, "count": count,
            }
            for (caller, caller_offset, callee, callee_offset), count in sorted(values.items())
        ]
    return rows(direct, "callee"), rows(helper, "helper")


def make_observation(
    *,
    mode: str,
    producer_commit: str,
    parser_path: Path,
    elf: Path,
    route_oracle_path: Path,
    contract_path: Path,
    contract: AuditContract,
    root: str,
    closure: set[str],
    calls: list[CallSite],
    owners: tuple[FunctionOwner, ...],
    direct_facts: list[DirectCallFact] | None = None,
    implementation_facts: list[ImplementationTransferFact] | None = None,
    unresolved_transfers: list[UnresolvedTransfer] = [],
    unresolved_effects: list[UnresolvedEffect] = [],
    islands: tuple[LocalIsland, ...] = (),
) -> dict[str, Any]:
    try:
        elf_relative_path = elf.resolve().relative_to(Path.cwd().resolve()).as_posix()
    except ValueError as error:
        raise ValueError("observation ELF must be inside the producer worktree") from error
    if direct_facts is None:
        direct_rows, helper_rows = _legacy_observation_facts(calls, closure, owners)
    else:
        direct_counter = Counter(
            (
                x.caller, x.caller_region, x.caller_island, x.caller_offset,
                x.callee, x.callee_offset
            )
            for x in direct_facts if x.caller in closure
        )
        helper_counter = Counter(
            (
                x.caller, x.caller_region, x.caller_island, x.caller_offset,
                x.callee, x.callee_offset
            )
            for x in direct_facts if x.caller in closure and is_native_math_helper(x.callee)
        )
        direct_rows = [
            {
                "caller": a, "caller_region": b, "caller_island": c,
                "caller_offset": d, "callee": e, "callee_offset": f, "count": n,
            }
            for (a, b, c, d, e, f), n in sorted(
                direct_counter.items(), key=lambda item: tuple(
                    "" if value is None else value for value in item[0]
                )
            )
        ]
        helper_rows = [
            {
                "caller": a, "caller_region": b, "caller_island": c,
                "caller_offset": d, "helper": e, "helper_offset": f, "count": n,
            }
            for (a, b, c, d, e, f), n in sorted(
                helper_counter.items(), key=lambda item: tuple(
                    "" if value is None else value for value in item[0]
                )
            )
        ]
    implementation_counter = Counter(
        (
            x.caller, x.caller_region, x.caller_island, x.caller_offset,
            x.target_island, x.target_offset
        )
        for x in (implementation_facts or []) if x.caller in closure
    )
    implementation_rows = [
        {
            "caller": a, "caller_region": b, "caller_island": c,
            "caller_offset": d, "target_island": e, "target_offset": f, "count": n,
        }
        for (a, b, c, d, e, f), n in sorted(
            implementation_counter.items(), key=lambda item: tuple(
                "" if value is None else value for value in item[0]
            )
        )
    ]
    owner_by_name = {owner.name: owner for owner in owners}

    def site(caller: str, address: int) -> tuple[str, str | None, int]:
        island = next(
            (item for item in islands if item.start <= address < item.end), None
        )
        if island is not None:
            return "island", island.name, address - island.start
        return "owner", None, address - owner_by_name[caller].start

    unresolved_rows = [
        {
            "caller": x.caller,
            "caller_region": site(x.caller, x.address)[0],
            "caller_island": site(x.caller, x.address)[1],
            "caller_offset": site(x.caller, x.address)[2],
            "mnemonic": x.mnemonic,
        }
        for x in unresolved_transfers if x.caller in closure
    ]
    effect_rows = [
        {
            "caller": x.function,
            "caller_region": site(x.function, x.address)[0],
            "caller_island": site(x.function, x.address)[1],
            "caller_offset": site(x.function, x.address)[2],
            "mnemonic": x.mnemonic, "operands": x.operands, "reason": x.reason,
        }
        for x in unresolved_effects if x.function in closure
    ]
    return {
        "schema_version": 1,
        "analysis_mode": mode,
        "producer_commit": producer_commit,
        "parser_sha256": file_digest(parser_path),
        "elf_relative_path": elf_relative_path,
        "elf_sha256": file_digest(elf),
        "route_oracle_sha256": file_digest(route_oracle_path),
        "contract_before_sha256": file_digest(contract_path),
        "contract_before_expected_total": contract.expected_total,
        "root": root,
        "closure_functions": sorted(closure),
        "direct_call_facts": direct_rows,
        "helper_call_facts": helper_rows,
        "implementation_transfer_facts": implementation_rows,
        "helper_total": sum(x["count"] for x in helper_rows),
        "unresolved_indirect_transfers": sorted(
            unresolved_rows, key=lambda x: (x["caller"], x["caller_offset"], x["mnemonic"])
        ),
        "unresolved_effects": sorted(
            effect_rows, key=lambda x: (
                x["caller"], x["caller_offset"], x["mnemonic"], x["operands"]
            )
        ),
    }


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
    parser.add_argument("--audit-contract", type=Path,
                        help="immutable expected root, minimum, and candidate contract for the audit route")
    parser.add_argument("--objdump", required=True, help="target objdump executable")
    parser.add_argument("--readelf", help="target readelf executable")
    parser.add_argument("--addr2line", required=True, help="target addr2line executable")
    parser.add_argument("--analysis-mode", choices=("legacy-linear", "code-only"),
                        default="code-only")
    parser.add_argument("--audit-observation-only", action="store_true")
    parser.add_argument("--json-output", type=Path)
    parser.add_argument("--producer-commit")
    parser.add_argument("--object-reference-only", action="store_true")
    args = parser.parse_args(argv)

    try:
        if args.object_reference_only:
            if args.readelf is not None:
                raise ValueError("--readelf is forbidden with --object-reference-only")
            raise ValueError("--object-reference-only requires the object manifest interface")
        if args.readelf is None:
            raise ValueError("--readelf is required for linked-ELF analysis")
        if args.audit_observation_only:
            if args.json_output is None or args.producer_commit is None:
                raise ValueError("observation-only requires --json-output and --producer-commit")
            if not re.fullmatch(r"[0-9a-f]{40}", args.producer_commit):
                raise ValueError("producer commit must be full lowercase 40-hex")
            actual_commit = run_command(["git", "rev-parse", "HEAD"]).strip()
            if actual_commit != args.producer_commit:
                raise ValueError("producer commit does not match HEAD")
        elif args.json_output is not None or args.producer_commit is not None:
            raise ValueError("--json-output/--producer-commit require observation-only")
        if not args.audit_observation_only and args.analysis_mode != "code-only":
            raise ValueError("normal acceptance requires analysis-mode=code-only")
        baseline_text = args.baseline.read_text(encoding="utf-8")
        route_text = args.route_oracle.read_text(encoding="utf-8")
        baseline = parse_baseline(baseline_text)
        oracle = parse_route_oracle(route_text)
        verify_baseline_integrity(baseline_text, baseline)
        verify_route_oracle_integrity(route_text, oracle)
        audit_oracle = None
        audit_contract = None
        if (args.audit_route_oracle is None) != (args.audit_contract is None):
            raise ValueError("--audit-route-oracle and --audit-contract must be supplied together")
        if args.audit_route_oracle is not None:
            audit_text = args.audit_route_oracle.read_text(encoding="utf-8")
            audit_oracle = parse_route_oracle(audit_text)
            verify_route_oracle_integrity(
                audit_text, audit_oracle, expected_digest=SIM_ROUTE_ORACLE_V1_SHA256
            )
            audit_contract_text = args.audit_contract.read_text(encoding="utf-8")
            audit_contract = parse_audit_contract(audit_contract_text)
            verify_audit_contract_integrity(audit_contract_text, audit_contract)
        if not args.elf.is_file():
            raise ValueError("ELF is not readable")
        disassembly = run_command([args.objdump, "-d", str(args.elf)])
        sections_text = run_command([args.readelf, "-SW", str(args.elf)])
        symbols_text = run_command([args.readelf, "-sW", str(args.elf)])
        lines_text = run_command([args.readelf, "--debug-dump=decodedline", str(args.elf)])
        sections = parse_readelf_sections(sections_text)
        symbols = parse_readelf_symbols(symbols_text, sections)
        owners = resolve_function_owners(symbols, sections)
        local_islands = resolve_local_islands(symbols, sections, owners)
        owner_address_map = build_owner_address_map(owners)
        analysis = None
        if args.analysis_mode == "legacy-linear":
            direct_calls = scan_direct_calls(disassembly)
            calls = [call for call in direct_calls if is_native_math_helper(call.helper)]
            graph = scan_call_graph(disassembly)
        else:
            decoded_seeds = parse_decoded_lines(lines_text, owners, owner_address_map)
            legacy_graph = scan_call_graph(disassembly)
            candidate_names = route_reachable_functions(
                legacy_graph, oracle.roots, oracle.indirect_edges
            )
            if audit_oracle is not None:
                candidate_names |= route_reachable_functions(
                    legacy_graph, audit_oracle.roots, audit_oracle.indirect_edges
                )
            parsed_instructions = parse_instructions(disassembly)
            instruction_memory = build_instruction_memory(parsed_instructions)
            entry_analysis = analyze_code_only(
                parsed_instructions,
                owners,
                {},
                candidate_names,
                instruction_memory,
                owner_address_map=owner_address_map,
                local_islands=local_islands,
            )
            analysis = entry_analysis
            owner_by_name = {owner.name: owner for owner in owners}
            covered_addresses = set(analysis.code_addresses)
            for name, address in uncovered_decoded_line_seeds(
                decoded_seeds, candidate_names, covered_addresses
            ):
                extra = analyze_code_only(
                    parsed_instructions,
                    (owner_by_name[name],),
                    {name: {address}},
                    {name},
                    instruction_memory,
                    include_owner_entry=False,
                    owner_address_map=owner_address_map,
                    stop_at_addresses=covered_addresses,
                    local_islands=local_islands,
                )
                covered_addresses.update(extra.code_addresses)
                analysis = merge_code_analyses(analysis, extra)
            direct_calls = analysis.calls
            calls = [call for call in direct_calls if is_native_math_helper(call.helper)]
            graph = defaultdict(set)
            for call in direct_calls:
                if not is_native_math_helper(call.helper):
                    graph[call.caller].add(call.helper)
        route_functions = route_reachable_functions(graph, oracle.roots, oracle.indirect_edges)
        audit_functions = None if audit_oracle is None else route_reachable_functions(
            graph, audit_oracle.roots, audit_oracle.indirect_edges
        )
        locations = {} if args.audit_observation_only else source_locations(
            args.addr2line, args.elf, calls
        )
        if not args.audit_observation_only:
            print_census(calls, route_functions, locations)
            if audit_functions is not None:
                print_audit(calls, audit_functions, locations)
        failures = baseline_failures(calls, route_functions, baseline)
        if audit_functions is not None and audit_oracle is not None and audit_contract is not None:
            if audit_contract.expected_root not in audit_oracle.roots:
                failures.append(f"audit root missing: expected {audit_contract.expected_root}")
            observed = Counter((x.caller, x.helper) for x in calls if x.caller in audit_functions)
            for (caller, helper), count in sorted(observed.items()):
                if caller in audit_contract.forbidden_callers:
                    failures.append(
                        f"audit forbidden caller uses native math: {caller} {helper} found {count}"
                    )
            if not args.audit_observation_only:
                actual_total = sum(observed.values())
                if actual_total != audit_contract.expected_total:
                    failures.append(
                        "audit total differs from fixed post-conversion baseline "
                        f"{audit_contract.expected_total}, found {actual_total}"
                    )
            if analysis is not None:
                unresolved = [
                    x for x in analysis.unresolved_transfers if x.caller in audit_functions
                ]
                effects = [x for x in analysis.unresolved_effects if x.function in audit_functions]
                if unresolved:
                    failures.append(f"audit has {len(unresolved)} unresolved indirect transfers")
                if effects:
                    failures.append(f"audit has {len(effects)} unresolved register effects")
            if args.audit_observation_only:
                observation = make_observation(
                    mode=args.analysis_mode,
                    producer_commit=args.producer_commit,
                    parser_path=Path(__file__),
                    elf=args.elf,
                    route_oracle_path=args.audit_route_oracle,
                    contract_path=args.audit_contract,
                    contract=audit_contract,
                    root=audit_contract.expected_root,
                    closure=audit_functions,
                    calls=direct_calls,
                    owners=owners,
                    direct_facts=None if analysis is None else analysis.direct_calls,
                    implementation_facts=(
                        None if analysis is None else analysis.implementation_transfers
                    ),
                    unresolved_transfers=[] if analysis is None else analysis.unresolved_transfers,
                    unresolved_effects=[] if analysis is None else analysis.unresolved_effects,
                    islands=local_islands,
                )
                args.json_output.parent.mkdir(parents=True, exist_ok=True)
                args.json_output.write_text(
                    json.dumps(observation, indent=2, sort_keys=True) + "\n", encoding="utf-8"
                )
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
