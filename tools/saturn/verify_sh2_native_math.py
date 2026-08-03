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
from dataclasses import dataclass, field
from hashlib import sha256
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from time import monotonic
from typing import Any, Callable, Iterable


@dataclass(frozen=True)
class CallSite:
    """One resolved direct call in the linked image."""

    caller: str
    address: int
    helper: str
    caller_identity: str | None = field(default=None, compare=False, repr=False)
    helper_identity: str | None = field(default=None, compare=False, repr=False)


@dataclass(frozen=True)
class RouteOracle:
    version: int
    roots: frozenset[str]
    static_manifest_edges: frozenset[tuple[str, str]]
    indirect_edges: frozenset[tuple[str, str]]


@dataclass(frozen=True)
class IndirectEdgeAudit:
    closure: frozenset[str]
    unlisted_transfers: tuple[UnresolvedTransfer, ...]


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
    owner_identity: str | None = field(default=None, compare=False, repr=False)


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


@dataclass(frozen=True)
class StackPtr:
    offset: int


@dataclass(frozen=True)
class StackPtrRange:
    """A monotone range of frame-relative addresses (None is unbounded)."""

    lo: int | None
    hi: int | None


@dataclass(frozen=True)
class MaybeStackPtr:
    pass


MAYBE_STACK_PTR = MaybeStackPtr()


@dataclass(frozen=True)
class StackAlias:
    """Whether a register value may address this function's local frame."""

    may_alias: bool
    frame_derived: bool = False


MAY_ALIAS_STACK = StackAlias(True)
FRAME_DERIVED_STACK_ALIAS = StackAlias(True, True)
NON_STACK_ALIAS = StackAlias(False)


@dataclass(frozen=True)
class ExternalStackAliases:
    """External storage locations that may contain this frame's address."""

    addresses: frozenset[int] = frozenset()
    unknown_address: bool = False


@dataclass(frozen=True)
class StackSlot:
    value: AbstractValue
    store_addresses: tuple[int, ...] = ()
    exact_symbols: frozenset[SymbolAtom] = frozenset()
    unknown_store: bool = False
    may_alias_stack: bool = True
    frame_derived_stack_alias: bool = False


@dataclass(frozen=True)
class StackMemory:
    slots: tuple[tuple[int, StackSlot], ...] = ()


@dataclass(frozen=True)
class StackOrigin:
    offsets: tuple[int, ...] = ()
    store_addresses: tuple[int, ...] = ()
    exact_symbols: frozenset[SymbolAtom] = frozenset()
    unknown_store: bool = False


AbstractValue = (
    _Unknown | ConstSet | Interval | ComparisonPredicate | StackPtr | StackPtrRange
    | MaybeStackPtr
    | StackAlias | ExternalStackAliases | StackMemory | StackOrigin
)


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
class LeafSummary:
    gpr_clobbers: frozenset[str]
    writes_memory: bool


@dataclass(frozen=True)
class DirectCallFact:
    caller: str
    caller_offset: int
    callee: str
    callee_offset: int
    count: int = 1
    caller_region: str = "owner"
    caller_island: str | None = None
    caller_identity: str | None = field(default=None, compare=False, repr=False)
    callee_identity: str | None = field(default=None, compare=False, repr=False)


@dataclass(frozen=True)
class ImplementationTransferFact:
    caller: str
    caller_offset: int
    target_island: str
    target_offset: int
    count: int = 1
    caller_region: str = "owner"
    caller_island: str | None = None
    caller_identity: str | None = field(default=None, compare=False, repr=False)


@dataclass(frozen=True)
class UnresolvedTransfer:
    caller: str
    address: int
    mnemonic: str
    operand_register: str | None = None
    final_value: AbstractValue | None = None
    contributing_seeds: tuple[tuple[int, str], ...] = ()
    contributing_seeds_truncated: bool = False
    predecessor_addresses: tuple[int, ...] = ()
    predecessor_addresses_truncated: bool = False
    state_changed_at_entry_covered_join: bool = False
    stack_source_offsets: tuple[int, ...] = ()
    stack_store_addresses: tuple[int, ...] = ()
    # The parser must classify an unresolved indirect transfer before the
    # dispatcher declaration gate sees it. Declarations model callback
    # dispatch only; a static stack-derived target remains an audit failure.
    provenance: str = "dynamic"
    caller_identity: str | None = field(default=None, compare=False, repr=False)


@dataclass(frozen=True)
class UnresolvedEffect:
    function: str
    address: int
    mnemonic: str
    operands: str
    reason: str
    function_identity: str | None = field(default=None, compare=False, repr=False)


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
    calls = {
        (
            x.caller_identity or x.caller,
            x.address,
            x.helper_identity or x.helper,
        ): x
        for x in [*left.calls, *right.calls]
    }
    facts = {
        (
            x.caller_identity or x.caller,
            x.callee_identity or x.callee,
            x.caller, x.caller_region, x.caller_island, x.caller_offset,
            x.callee, x.callee_offset
        ): x
        for x in [*left.direct_calls, *right.direct_calls]
    }
    implementation = {
        (
            x.caller_identity or x.caller,
            x.caller, x.caller_region, x.caller_island, x.caller_offset,
            x.target_island, x.target_offset
        ): x
        for x in [*left.implementation_transfers, *right.implementation_transfers]
    }
    left_unresolved = {
        (x.caller_identity or x.caller, x.address)
        for x in left.unresolved_transfers
    }
    right_unresolved = {
        (x.caller_identity or x.caller, x.address)
        for x in right.unresolved_transfers
    }
    resolved = {
        (x.caller_identity or x.caller, x.address) for x in calls.values()
    }
    # Same-owner computed jumps can resolve entirely inside the CFG and emit no
    # call fact. If the other isolated lane evaluated that transfer point
    # without an unresolved diagnostic, it is nevertheless proven resolved.
    resolved.update(
        (x.caller_identity or x.caller, x.address)
        for x in right.unresolved_transfers
        if x.address in left.code_addresses
        and (x.caller_identity or x.caller, x.address) not in left_unresolved
    )
    resolved.update(
        (x.caller_identity or x.caller, x.address)
        for x in left.unresolved_transfers
        if x.address in right.code_addresses
        and (x.caller_identity or x.caller, x.address) not in right_unresolved
    )
    transfers = {
        (x.caller_identity or x.caller, x.address, x.mnemonic): x
        for x in [*left.unresolved_transfers, *right.unresolved_transfers]
        if (x.caller_identity or x.caller, x.address) not in resolved
    }
    effects = {
        (x.function_identity or x.function, x.address, x.mnemonic): x
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
    if left == right:
        return left
    if isinstance(left, MaybeStackPtr) or isinstance(right, MaybeStackPtr):
        return MAYBE_STACK_PTR
    if isinstance(left, (StackPtr, StackPtrRange)) \
            or isinstance(right, (StackPtr, StackPtrRange)):
        if not isinstance(left, (StackPtr, StackPtrRange)) \
                or not isinstance(right, (StackPtr, StackPtrRange)):
            return MAYBE_STACK_PTR

        def bounds(value: StackPtr | StackPtrRange) -> tuple[int | None, int | None]:
            return (
                (value.offset, value.offset)
                if isinstance(value, StackPtr)
                else (value.lo, value.hi)
            )

        left_lo, left_hi = bounds(left)
        right_lo, right_hi = bounds(right)
        lo = None if left_lo is None or right_lo is None else min(left_lo, right_lo)
        hi = None if left_hi is None or right_hi is None else max(left_hi, right_hi)
        return StackPtrRange(lo, hi)
    if isinstance(left, StackAlias) and isinstance(right, StackAlias):
        return StackAlias(
            left.may_alias or right.may_alias,
            left.frame_derived or right.frame_derived,
        )
    if isinstance(left, ExternalStackAliases) \
            and isinstance(right, ExternalStackAliases):
        return ExternalStackAliases(
            left.addresses | right.addresses,
            left.unknown_address or right.unknown_address,
        )
    if left is UNKNOWN or right is UNKNOWN:
        return UNKNOWN
    if isinstance(left, StackMemory) and isinstance(right, StackMemory):
        left_slots, right_slots = dict(left.slots), dict(right.slots)
        merged_slots: list[tuple[int, StackSlot]] = []
        for offset in sorted(left_slots.keys() | right_slots.keys()):
            absent_slot = StackSlot(UNKNOWN, unknown_store=True)
            left_slot = left_slots.get(offset, absent_slot)
            right_slot = right_slots.get(offset, absent_slot)
            pointer_slot = isinstance(
                left_slot.value, (StackPtr, StackPtrRange, MaybeStackPtr)
            ) or isinstance(
                right_slot.value, (StackPtr, StackPtrRange, MaybeStackPtr)
            )
            if pointer_slot:
                value = join_value(left_slot.value, right_slot.value)
            else:
                value = left_slot.value if left_slot.value == right_slot.value else UNKNOWN
            stores = tuple(sorted(set(
                (*left_slot.store_addresses, *right_slot.store_addresses)
            ))[:16])
            symbols = left_slot.exact_symbols | right_slot.exact_symbols
            merged_slots.append((offset, StackSlot(
                value,
                stores,
                symbols if len(symbols) <= 2 else frozenset(),
                left_slot.unknown_store or right_slot.unknown_store
                or len(symbols) > 1,
                left_slot.may_alias_stack or right_slot.may_alias_stack,
                left_slot.frame_derived_stack_alias
                or right_slot.frame_derived_stack_alias,
            )))
        return StackMemory(tuple(merged_slots))
    if isinstance(left, StackOrigin) and isinstance(right, StackOrigin):
        return StackOrigin(
            tuple(sorted(set((*left.offsets, *right.offsets)))[:16]),
            tuple(sorted(set(
                (*left.store_addresses, *right.store_addresses)
            ))[:16]),
            left.exact_symbols | right.exact_symbols,
            left.unknown_store or right.unknown_store,
        )
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
    selected_names: set[str] | None = None,
    selected_owner_identities: set[str] | None = None,
) -> dict[str, set[int]]:
    if selected_names is not None and selected_owner_identities is not None:
        raise ValueError("decoded-line owner selection cannot mix names and identities")
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
        if owner is None:
            continue
        identity = _bounded_owner_identity(owner)
        if selected_owner_identities is not None:
            if identity in selected_owner_identities:
                result[identity].add(address)
        elif selected_names is None or owner.name in selected_names:
            result[owner.name].add(address)
    return result


OBJDUMP_ROW_RE = re.compile(
    r"^\s*([0-9A-Fa-f]+):\s+((?:[0-9A-Fa-f]{2}\s+){2})([A-Za-z0-9_./]+)\s*(.*?)\s*$"
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


def _local_island_identity(island: LocalIsland) -> str:
    """Return a collision-proof raw-closure node for validated local code."""
    return f"@island:{island.name}@{island.start:08x}"


def _island_at_code_address(
    islands: Iterable[LocalIsland], address: int,
) -> LocalIsland | None:
    matches = [
        island for island in islands if island.code_start <= address < island.end
    ]
    return matches[0] if len(matches) == 1 else None


def _symbol_from_annotation(annotation: str) -> SymbolAtom | None:
    match = re.search(r"(?:0x)?([0-9A-Fa-f]{6,8})\s+<([^>]+)>", annotation)
    if not match:
        return None
    return SymbolAtom(_symbol_base(match.group(2)), int(match.group(1), 16))


def _unknown_state(
    *, entry_arguments_nonstack: bool = True
) -> dict[str, AbstractValue]:
    return {f"r{x}": UNKNOWN for x in range(15)} | {
        "r15": StackPtr(0),
        "mach": UNKNOWN, "macl": UNKNOWN, "t_predicate": UNKNOWN,
        "stack_memory": StackMemory(),
    } | {f"r{x}_stack_origin": StackOrigin() for x in range(16)} | {
        # Incoming argument registers cannot name this callee's fresh frame.
        # Other unknown registers and decoded-only seeds stay fail-closed.
        f"r{x}_stack_alias": (
            FRAME_DERIVED_STACK_ALIAS
            if x == 15 or not entry_arguments_nonstack
            else NON_STACK_ALIAS if 4 <= x <= 7
            else MAY_ALIAS_STACK
        )
        for x in range(16)
    } | {
        "external_stack_aliases": ExternalStackAliases(
            unknown_address=not entry_arguments_nonstack
        ),
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
            if isinstance(previous, (StackPtr, StackPtrRange)) \
                    and isinstance(value, StackPtrRange):
                previous_lo = (
                    previous.offset if isinstance(previous, StackPtr) else previous.lo
                )
                previous_hi = (
                    previous.offset if isinstance(previous, StackPtr) else previous.hi
                )
                lower_expands = (
                    previous_lo is not None
                    and (value.lo is None or value.lo < previous_lo)
                )
                upper_expands = (
                    previous_hi is not None
                    and (value.hi is None or value.hi > previous_hi)
                )
                lower, upper = widening.get(register, (False, False))
                merged[register] = StackPtrRange(
                    None if lower_expands and lower else value.lo,
                    None if upper_expands and upper else value.hi,
                )
                widening[register] = (
                    lower or lower_expands, upper or upper_expands
                )
                continue
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


def prove_sourceboot_null_task_submit(
    instructions: dict[int, Instruction],
    owners: Iterable[FunctionOwner],
) -> frozenset[int]:
    """Return statically proven-null sourceboot task-submit storage slots."""
    owner_list = tuple(owners)
    by_name = {owner.name: owner for owner in owner_list}
    main = by_name.get("_main")
    configure = by_name.get("_sm64_saturn_source_runtime_configure")
    dispatcher = by_name.get("_exec_display_list")
    if main is None or configure is None or dispatcher is None:
        return frozenset()

    def body(owner: FunctionOwner) -> list[Instruction]:
        return [
            instructions[address]
            for address in sorted(instructions)
            if owner.start <= address < owner.end
        ]

    configure_rows = body(configure)
    slot_loads = [
        (index, _symbol_from_annotation(row.annotation))
        for index, row in enumerate(configure_rows)
        if row.mnemonic == "mov.l"
        and _symbol_from_annotation(row.annotation) is not None
        and _symbol_from_annotation(row.annotation).name == "_sTaskSubmit"
    ]
    if len(slot_loads) != 1:
        return frozenset()
    load_index, slot = slot_loads[0]
    assert slot is not None
    load_destination = re.search(r",\s*(r(?:1[0-5]|\d))\s*$", configure_rows[load_index].operands)
    if load_destination is None or load_index + 1 >= len(configure_rows):
        return frozenset()
    if configure_rows[load_index + 1].mnemonic != "mov.l" or not re.fullmatch(
        rf"r4,@{re.escape(load_destination.group(1))}",
        configure_rows[load_index + 1].operands.replace(" ", ""),
    ):
        return frozenset()
    if any(owner.start <= slot.address < owner.end for owner in owner_list):
        return frozenset()

    all_slot_references: list[tuple[str, Instruction]] = []
    for owner in owner_list:
        for row in body(owner):
            atom = _symbol_from_annotation(row.annotation)
            if atom is not None and atom.address == slot.address:
                all_slot_references.append((owner.name, row))
    if len(all_slot_references) != 2 or {
        name for name, _ in all_slot_references
    } != {configure.name, dispatcher.name}:
        return frozenset()

    configure_references: list[tuple[str, Instruction]] = []
    for owner in owner_list:
        for row in body(owner):
            atom = _symbol_from_annotation(row.annotation)
            if atom == SymbolAtom(configure.name, configure.start):
                configure_references.append((owner.name, row))
    if len(configure_references) != 1 \
            or configure_references[0][0] != main.name:
        return frozenset()

    main_rows = body(main)
    configure_calls: list[Instruction] = []
    register_atoms: dict[str, SymbolAtom] = {}
    for row in main_rows:
        atom = _symbol_from_annotation(row.annotation)
        destination = re.search(r",\s*(r(?:1[0-5]|\d))\s*$", row.operands)
        if row.mnemonic in {"mov.l", "mov.w"} and atom is not None and destination:
            register_atoms[destination.group(1)] = atom
        if row.mnemonic == "jsr":
            register = re.fullmatch(r"@(r(?:1[0-5]|\d))", row.operands)
            if register and register_atoms.get(register.group(1)) == SymbolAtom(
                configure.name, configure.start
            ):
                configure_calls.append(row)
        if destination and not (
            row.mnemonic in {"mov.l", "mov.w"} and atom is not None
        ):
            register_atoms.pop(destination.group(1), None)
    if not configure_calls or any(
        (slot_row := instructions.get(call.address + 2)) is None
        or slot_row.mnemonic != "mov"
        or not re.fullmatch(r"#(?:0x)?0,\s*r4", slot_row.operands)
        for call in configure_calls
    ):
        return frozenset()

    dispatcher_rows = body(dispatcher)
    for index, row in enumerate(dispatcher_rows):
        atom = _symbol_from_annotation(row.annotation)
        base = re.search(r",\s*(r(?:1[0-5]|\d))\s*$", row.operands)
        if row.mnemonic != "mov.l" or atom != slot or base is None:
            continue
        if index + 6 >= len(dispatcher_rows):
            continue
        window = dispatcher_rows[index:index + 7]
        if any(
            item.address != row.address + (window_index * 2)
            for window_index, item in enumerate(window)
        ):
            continue
        load, test, branch, context_load, transfer, delay = window[1:]
        target = re.fullmatch(
            rf"@{re.escape(base.group(1))},\s*(r(?:1[0-5]|\d))", load.operands
        )
        if load.mnemonic != "mov.l" or target is None:
            continue
        target_register = target.group(1)
        if test.mnemonic != "tst" or test.operands.replace(" ", "") != (
            f"{target_register},{target_register}"
        ):
            continue
        branch_target = _target_from_text(branch.operands)
        if branch.mnemonic != "bt" or branch_target is None:
            continue
        context_atom = _symbol_from_annotation(context_load.annotation)
        context_destination = re.search(
            r",\s*(r(?:1[0-5]|\d))\s*$", context_load.operands
        )
        if context_load.mnemonic != "mov.l" or context_atom is None \
                or context_atom.name != "_sTaskSubmitContext" \
                or context_destination is None:
            continue
        context_register = context_destination.group(1)
        dead_addresses = {
            context_load.address, transfer.address, delay.address
        }
        if branch_target in dead_addresses:
            continue
        if transfer.mnemonic != "jmp" \
                or transfer.operands != f"@{target_register}":
            continue
        if delay.mnemonic != "mov.l" or delay.operands.replace(" ", "") != (
            f"@{context_register},r5"
        ):
            continue
        guarded_exit_is_return = False
        for exit_address in range(branch_target, branch_target + 8, 2):
            guarded_exit = instructions.get(exit_address)
            if guarded_exit is None:
                break
            if guarded_exit.mnemonic == "rts":
                guarded_exit_is_return = True
                break
            if guarded_exit.mnemonic in DELAY_SLOT_CONTROL \
                    or guarded_exit.mnemonic.startswith(("bt", "bf")):
                break
        if not guarded_exit_is_return:
            continue
        return frozenset({slot.address})
    return frozenset()


def sourceboot_null_task_submit_dead_nodes(
    instructions: dict[int, Instruction],
    owners: Iterable[FunctionOwner],
    known_null_addresses: frozenset[int],
) -> frozenset[tuple[str, int]]:
    """Return the exact decoded-only callback block killed by the null proof."""
    if not known_null_addresses:
        return frozenset()
    dispatcher = next(
        (owner for owner in owners if owner.name == "_exec_display_list"), None
    )
    if dispatcher is None:
        return frozenset()
    rows = [
        instructions[address]
        for address in sorted(instructions)
        if dispatcher.start <= address < dispatcher.end
    ]
    for index, row in enumerate(rows):
        atom = _symbol_from_annotation(row.annotation)
        base = re.search(r",\s*(r(?:1[0-5]|\d))\s*$", row.operands)
        if row.mnemonic != "mov.l" or atom is None \
                or atom.address not in known_null_addresses or base is None:
            continue
        if index + 6 >= len(rows):
            continue
        window = rows[index:index + 7]
        if any(
            item.address != row.address + (window_index * 2)
            for window_index, item in enumerate(window)
        ):
            continue
        load, test, branch, context_load, transfer, delay = window[1:]
        target = re.fullmatch(
            rf"@{re.escape(base.group(1))},\s*(r(?:1[0-5]|\d))",
            load.operands,
        )
        if load.mnemonic != "mov.l" or target is None:
            continue
        target_register = target.group(1)
        if test.mnemonic != "tst" or test.operands.replace(" ", "") != (
            f"{target_register},{target_register}"
        ):
            continue
        branch_target = _target_from_text(branch.operands)
        if branch.mnemonic != "bt" or branch_target is None:
            continue
        context_atom = _symbol_from_annotation(context_load.annotation)
        context_destination = re.search(
            r",\s*(r(?:1[0-5]|\d))\s*$", context_load.operands
        )
        if context_load.mnemonic != "mov.l" or context_atom is None \
                or context_atom.name != "_sTaskSubmitContext" \
                or context_destination is None:
            continue
        context_register = context_destination.group(1)
        dead_addresses = {
            context_load.address, transfer.address, delay.address
        }
        if branch_target in dead_addresses:
            continue
        if transfer.mnemonic != "jmp" \
                or transfer.operands != f"@{target_register}":
            continue
        if delay.mnemonic != "mov.l" or delay.operands.replace(" ", "") != (
            f"@{context_register},r5"
        ):
            continue
        return frozenset(
            (dispatcher.name, address) for address in sorted(dead_addresses)
        )
    return frozenset()


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
    elif instruction.mnemonic == "tst" and binary \
            and binary.group(1) == binary.group(2):
        candidate = state[binary.group(1)]
        if isinstance(candidate, (ConstSet, Interval)) \
                and candidate.kind == "unsigned":
            register = binary.group(1)
            true_range = ("unsigned", 0, 0)
            false_range = ("unsigned", 1, 0xFFFFFFFF)
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


def _stack_slots(state: dict[str, AbstractValue]) -> dict[int, StackSlot]:
    memory = state.get("stack_memory")
    return dict(memory.slots) if isinstance(memory, StackMemory) else {}


def _register_stack_alias(
    state: dict[str, AbstractValue], register: str
) -> StackAlias:
    alias = state.get(f"{register}_stack_alias", MAY_ALIAS_STACK)
    return alias if isinstance(alias, StackAlias) else MAY_ALIAS_STACK


def _register_may_alias_stack(
    state: dict[str, AbstractValue], register: str
) -> bool:
    return _register_stack_alias(state, register).may_alias


def _register_frame_derived_stack_alias(
    state: dict[str, AbstractValue], register: str
) -> bool:
    return _register_stack_alias(state, register).frame_derived


def _register_value_is_frame_derived(
    state: dict[str, AbstractValue], register: str
) -> bool:
    return isinstance(state[register], (StackPtr, StackPtrRange, MaybeStackPtr)) \
        or _register_frame_derived_stack_alias(state, register)


def _set_register_stack_alias(
    state: dict[str, AbstractValue], register: str,
    alias: bool | StackAlias, frame_derived: bool = False,
) -> None:
    state[f"{register}_stack_alias"] = (
        alias if isinstance(alias, StackAlias)
        else StackAlias(alias, frame_derived)
    )


def _external_stack_aliases(
    state: dict[str, AbstractValue]
) -> ExternalStackAliases:
    aliases = state.get("external_stack_aliases")
    return (
        aliases if isinstance(aliases, ExternalStackAliases)
        else ExternalStackAliases(unknown_address=True)
    )


def _external_address_may_alias_stack(
    state: dict[str, AbstractValue], address: int
) -> bool:
    aliases = _external_stack_aliases(state)
    return aliases.unknown_address or address in aliases.addresses


def _mark_external_stack_alias(
    state: dict[str, AbstractValue], addresses: Iterable[int] = ()
) -> None:
    aliases = _external_stack_aliases(state)
    exact = frozenset(addresses)
    state["external_stack_aliases"] = ExternalStackAliases(
        aliases.addresses | exact,
        aliases.unknown_address or not exact,
    )


def _concrete_symbol_addresses(
    value: AbstractValue, displacement: int = 0
) -> frozenset[int]:
    if not isinstance(value, ConstSet) or value.kind != "symbol":
        return frozenset()
    atoms = tuple(value.values)
    if not atoms or not all(
        isinstance(atom, SymbolAtom)
        and not atom.name.endswith(("*", "[]"))
        for atom in atoms
    ):
        return frozenset()
    return frozenset(
        atom.address + displacement
        for atom in atoms
        if isinstance(atom, SymbolAtom)
    )


def _indexed_external_may_alias_stack(
    state: dict[str, AbstractValue], base_address: int,
    indexes: AbstractValue,
) -> bool:
    aliases = _external_stack_aliases(state)
    if aliases.unknown_address:
        return True
    if isinstance(indexes, ConstSet) and all(
        isinstance(item, int) for item in indexes.values
    ):
        return any(
            base_address + int(item) in aliases.addresses
            for item in indexes.values
        )
    if isinstance(indexes, Interval):
        return any(
            indexes.lo <= address - base_address <= indexes.hi
            for address in aliases.addresses
        )
    return bool(aliases.addresses)


def _replace_stack_slots(
    state: dict[str, AbstractValue], slots: dict[int, StackSlot]
) -> None:
    bounded = sorted(slots.items())[:64]
    state["stack_memory"] = StackMemory(tuple(bounded))


def _stack_store(
    state: dict[str, AbstractValue], offset: int, value: AbstractValue, address: int,
    stack_alias: StackAlias = MAY_ALIAS_STACK,
) -> None:
    slots = _stack_slots(state)
    atoms = (
        value.values
        if isinstance(value, ConstSet) and value.kind == "symbol"
        and len(value.values) == 1
        and all(isinstance(item, SymbolAtom) for item in value.values)
        else frozenset()
    )
    slots[offset] = StackSlot(
        value, (address,),
        frozenset(item for item in atoms if isinstance(item, SymbolAtom)),
        not bool(atoms),
        stack_alias.may_alias,
        stack_alias.frame_derived,
    )
    _replace_stack_slots(state, slots)


def _stack_load(state: dict[str, AbstractValue], offset: int) -> StackSlot:
    return _stack_slots(state).get(offset, StackSlot(UNKNOWN))


def _stack_slot_value(slot: StackSlot) -> AbstractValue:
    if slot.value is UNKNOWN and not slot.unknown_store \
            and len(slot.exact_symbols) == 1:
        return ConstSet("symbol", slot.exact_symbols)
    return slot.value


def _poison_stack_slot(slot: StackSlot) -> StackSlot:
    return StackSlot(
        UNKNOWN, slot.store_addresses, slot.exact_symbols, True,
        slot.may_alias_stack, slot.frame_derived_stack_alias,
    )


def _invalidate_stack(state: dict[str, AbstractValue]) -> None:
    slots = {
        offset: _poison_stack_slot(slot)
        for offset, slot in _stack_slots(state).items()
    }
    _replace_stack_slots(state, slots)


def _invalidate_stack_range(
    state: dict[str, AbstractValue], offset: int, width: int
) -> None:
    """Forget modeled longword slots overlapped by a bounded frame store."""
    slots = {}
    for slot_offset, slot in _stack_slots(state).items():
        slots[slot_offset] = (
            slot
            if slot_offset + 4 <= offset or offset + width <= slot_offset
            else _poison_stack_slot(slot)
        )
    _replace_stack_slots(state, slots)


def _invalidate_escaped_argument_slots(state: dict[str, AbstractValue]) -> None:
    """Forget slots whose address may escape through an argument register."""
    arguments = tuple(state[register] for register in ("r4", "r5", "r6", "r7"))
    if any(isinstance(argument, MaybeStackPtr) for argument in arguments):
        _invalidate_stack(state)
        return
    escaped_offsets = {
        argument.offset
        for argument in arguments
        if isinstance(argument, StackPtr)
    }
    escaped_ranges = tuple(
        argument for argument in arguments if isinstance(argument, StackPtrRange)
    )
    if not escaped_offsets and not escaped_ranges:
        return
    slots = {}
    for slot_offset, slot in _stack_slots(state).items():
        escaped = any(
            slot_offset <= escaped_offset < slot_offset + 4
            for escaped_offset in escaped_offsets
        ) or any(
            _bounded_pointer_store_may_overlap_slot(
                escaped_range, 0, 1, slot_offset
            )
            for escaped_range in escaped_ranges
        )
        slots[slot_offset] = _poison_stack_slot(slot) if escaped else slot
    _replace_stack_slots(state, slots)


def _bounded_pointer_store_may_overlap_slot(
    pointer: StackPtrRange, displacement: int, width: int, slot_offset: int
) -> bool:
    store_lo = None if pointer.lo is None else pointer.lo + displacement
    store_hi = None if pointer.hi is None else pointer.hi + displacement + width
    return (store_hi is None or slot_offset < store_hi) \
        and (store_lo is None or store_lo < slot_offset + 4)


def _invalidate_stack_pointer_range(
    state: dict[str, AbstractValue], pointer: StackPtrRange,
    displacement: int, width: int,
) -> None:
    slots = {
        slot_offset: (
            _poison_stack_slot(slot)
            if _bounded_pointer_store_may_overlap_slot(
                pointer, displacement, width, slot_offset
            )
            else slot
        )
        for slot_offset, slot in _stack_slots(state).items()
    }
    _replace_stack_slots(state, slots)


def _exact_integer(value: AbstractValue) -> int | None:
    if isinstance(value, ConstSet) and value.kind in {"signed", "unsigned"} \
            and len(value.values) == 1:
        item = next(iter(value.values))
        return int(item) if isinstance(item, int) else None
    return None


def _indexed_stack_offset(
    left: AbstractValue, right: AbstractValue
) -> int | None:
    for pointer, index in ((left, right), (right, left)):
        integer = _exact_integer(index)
        if isinstance(pointer, StackPtr) and integer is not None:
            offset = pointer.offset + integer
            return offset if abs(offset) <= 4096 else None
    return None


def _instruction_writes_memory(instruction: Instruction) -> bool:
    return instruction.mnemonic in {"mov.l", "mov.w", "mov.b", "sts.l"} \
        and ",@" in instruction.operands


DELAY_SLOT_CONTROL = frozenset({
    "bf", "bf.s", "bra", "braf", "bsr", "bsrf", "bt", "bt.s",
    "jmp", "jsr", "rte", "rts", "sleep", "trapa", ".word",
})


def _write_effect(instruction: Instruction, state: dict[str, AbstractValue],
                  effects: list[UnresolvedEffect], owner: FunctionOwner,
                  memory: dict[int, int] | None = None,
                  profile: Counter[str] | None = None,
                  known_null_addresses: frozenset[int] = frozenset(),
                  function_identity: str | None = None) -> None:
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
    if mnemonic == "div0s" and re.fullmatch(
        r"r(?:1[0-5]|\d),\s*r(?:1[0-5]|\d)", text
    ):
        # DIV0S changes Q/M/T only. Q/M are outside this call-target domain;
        # conservatively forget T while preserving both GPR operands.
        state["t_predicate"] = UNKNOWN
        return

    destination_register = re.search(r",\s*(r(?:1[0-5]|\d))\s*$", text)
    single_register = re.fullmatch(r"(r(?:1[0-5]|\d))", text)
    written_register = (
        destination_register.group(1) if destination_register
        else single_register.group(1) if single_register else None
    )
    if written_register is not None:
        predicate = state["t_predicate"]
        if isinstance(predicate, ComparisonPredicate) \
                and predicate.register == written_register:
            # T itself survives flag-neutral moves, but its saved abstract
            # relationship describes the value that was compared. Once that
            # register is overwritten, refining the replacement value with the
            # stale comparison would be unsound.
            state["t_predicate"] = UNKNOWN
        state[f"{written_register}_stack_origin"] = StackOrigin()

    push = re.fullmatch(r"(r(?:1[0-5]|\d)),\s*@-r15", text)
    if mnemonic == "mov.l" and push:
        pointer = state["r15"]
        if isinstance(pointer, StackPtr):
            pointer = StackPtr(pointer.offset - 4)
            state["r15"] = pointer
            source_register = push.group(1)
            _stack_store(
                state, pointer.offset, state[source_register], instruction.address,
                _register_stack_alias(state, source_register),
            )
        else:
            _invalidate_stack(state)
        return
    pop = re.fullmatch(r"@r15\+,\s*(r(?:1[0-5]|\d))", text)
    if mnemonic == "mov.l" and pop:
        pointer = state["r15"]
        if isinstance(pointer, StackPtr):
            slot = _stack_load(state, pointer.offset)
            state[pop.group(1)] = _stack_slot_value(slot)
            state[f"{pop.group(1)}_stack_origin"] = StackOrigin(
                (pointer.offset,), slot.store_addresses, slot.exact_symbols,
                slot.unknown_store,
            )
            _set_register_stack_alias(
                state, pop.group(1), StackAlias(
                    slot.may_alias_stack, slot.frame_derived_stack_alias
                )
            )
            state["r15"] = StackPtr(pointer.offset + 4)
        else:
            state[pop.group(1)] = UNKNOWN
            _set_register_stack_alias(state, pop.group(1), True)
            _invalidate_stack(state)
        return
    stack_store = re.fullmatch(
        r"(r(?:1[0-5]|\d)),\s*@(?:\((\d+),r15\)|r15)", text
    )
    if mnemonic == "mov.l" and stack_store:
        pointer = state["r15"]
        if isinstance(pointer, StackPtr):
            displacement = int(stack_store.group(2) or 0)
            _stack_store(
                state, pointer.offset + displacement,
                state[stack_store.group(1)], instruction.address,
                _register_stack_alias(state, stack_store.group(1)),
            )
        else:
            _invalidate_stack(state)
        return
    stack_load = re.fullmatch(
        r"@(?:\((\d+),r15\)|r15),\s*(r(?:1[0-5]|\d))", text
    )
    if mnemonic == "mov.l" and stack_load:
        pointer = state["r15"]
        if isinstance(pointer, StackPtr):
            displacement = int(stack_load.group(1) or 0)
            offset = pointer.offset + displacement
            slot = _stack_load(state, offset)
            state[stack_load.group(2)] = _stack_slot_value(slot)
            state[f"{stack_load.group(2)}_stack_origin"] = StackOrigin(
                (offset,), slot.store_addresses, slot.exact_symbols,
                slot.unknown_store,
            )
            _set_register_stack_alias(
                state, stack_load.group(2), StackAlias(
                    slot.may_alias_stack, slot.frame_derived_stack_alias
                )
            )
        else:
            state[stack_load.group(2)] = UNKNOWN
            _set_register_stack_alias(state, stack_load.group(2), True)
        return

    predecrement_store = re.fullmatch(
        r"(r(?:1[0-5]|\d)|pr|mach|macl),\s*@-(r(?:1[0-5]|\d))", text
    )
    if _instruction_writes_memory(instruction) and predecrement_store:
        source_register, base_register = predecrement_store.groups()
        width = {"mov.b": 1, "mov.w": 2, "mov.l": 4, "sts.l": 4}[mnemonic]
        base = state[base_register]
        if isinstance(base, StackPtr):
            offset = base.offset - width
            state[base_register] = StackPtr(offset)
            state[f"{base_register}_stack_origin"] = StackOrigin()
            _invalidate_stack_range(state, offset, width)
            if mnemonic == "mov.l" and offset % 4 == 0:
                _stack_store(
                    state, offset, state[source_register], instruction.address,
                    _register_stack_alias(state, source_register),
                )
        elif isinstance(base, StackPtrRange):
            state[base_register] = StackPtrRange(
                None if base.lo is None else base.lo - width,
                None if base.hi is None else base.hi - width,
            )
            state[f"{base_register}_stack_origin"] = StackOrigin()
            _invalidate_stack_pointer_range(state, base, -width, width)
        elif isinstance(base, MaybeStackPtr) \
                or _register_may_alias_stack(state, base_register):
            state[base_register] = UNKNOWN
            state[f"{base_register}_stack_origin"] = StackOrigin()
            _invalidate_stack(state)
        else:
            if source_register.startswith("r") \
                    and _register_value_is_frame_derived(
                        state, source_register
                    ):
                _mark_external_stack_alias(
                    state, _concrete_symbol_addresses(base, -width)
                )
            state[base_register] = UNKNOWN
            state[f"{base_register}_stack_origin"] = StackOrigin()
            _set_register_stack_alias(state, base_register, False)
        return

    indexed_stack_store = re.fullmatch(
        r"(r(?:1[0-5]|\d)),\s*@\((r(?:1[0-5]|\d)),(r(?:1[0-5]|\d))\)",
        text,
    )
    if mnemonic in {"mov.l", "mov.w", "mov.b"} and indexed_stack_store:
        address_registers = (
            indexed_stack_store.group(2), indexed_stack_store.group(3)
        )
        offset = _indexed_stack_offset(
            state[address_registers[0]],
            state[address_registers[1]],
        )
        if offset is None:
            address_may_alias_stack = any(
                _register_may_alias_stack(state, register)
                for register in address_registers
            )
            known_nonstack_base = any(
                isinstance(state[register], ConstSet)
                and state[register].kind == "symbol"
                for register in address_registers
            ) and not any(
                isinstance(
                    state[register], (StackPtr, StackPtrRange, MaybeStackPtr)
                )
                for register in address_registers
            ) and not address_may_alias_stack
            if known_nonstack_base:
                if _register_value_is_frame_derived(
                    state, indexed_stack_store.group(1)
                ):
                    external_addresses: set[int] = set()
                    for base_register, index_register in (
                        address_registers, address_registers[::-1]
                    ):
                        index = _exact_integer(state[index_register])
                        if index is not None:
                            external_addresses.update(
                                _concrete_symbol_addresses(
                                    state[base_register], index
                                )
                            )
                    _mark_external_stack_alias(state, external_addresses)
            elif address_may_alias_stack:
                _invalidate_stack(state)
        else:
            width = {"mov.b": 1, "mov.w": 2, "mov.l": 4}[mnemonic]
            _invalidate_stack_range(state, offset, width)
            if mnemonic == "mov.l" and offset % 4 == 0:
                _stack_store(
                    state, offset, state[indexed_stack_store.group(1)],
                    instruction.address,
                    _register_stack_alias(state, indexed_stack_store.group(1)),
                )
        return

    aliased_stack_store = re.fullmatch(
        r"(r(?:1[0-5]|\d)),\s*@(?:\((\d+),(r(?:1[0-5]|\d))\)|(r(?:1[0-5]|\d)))",
        text,
    )
    if mnemonic in {"mov.l", "mov.w", "mov.b"} and aliased_stack_store:
        source_register = aliased_stack_store.group(1)
        base_register = aliased_stack_store.group(3) or aliased_stack_store.group(4)
        base = state[base_register]
        if isinstance(base, StackPtr):
            displacement = int(aliased_stack_store.group(2) or 0)
            offset = base.offset + displacement
            width = {"mov.b": 1, "mov.w": 2, "mov.l": 4}[mnemonic]
            _invalidate_stack_range(state, offset, width)
            if mnemonic == "mov.l" and offset % 4 == 0:
                _stack_store(
                    state, offset, state[aliased_stack_store.group(1)],
                    instruction.address,
                    _register_stack_alias(state, aliased_stack_store.group(1)),
                )
            return
        if isinstance(base, StackPtrRange):
            displacement = int(aliased_stack_store.group(2) or 0)
            width = {"mov.b": 1, "mov.w": 2, "mov.l": 4}[mnemonic]
            _invalidate_stack_pointer_range(
                state, base, displacement, width
            )
            return
        if isinstance(base, MaybeStackPtr):
            _invalidate_stack(state)
            return
        if _register_may_alias_stack(state, base_register):
            _invalidate_stack(state)
            return
        if _register_value_is_frame_derived(state, source_register):
            displacement = int(aliased_stack_store.group(2) or 0)
            _mark_external_stack_alias(
                state, _concrete_symbol_addresses(base, displacement)
            )
        return

    aliased_stack_load = re.fullmatch(
        r"@(?:\((\d+),(r(?:1[0-5]|\d))\)|(r(?:1[0-5]|\d))),\s*(r(?:1[0-5]|\d))",
        text,
    )
    if mnemonic in {"mov.l", "mov.w", "mov.b"} and aliased_stack_load:
        base_register = aliased_stack_load.group(2) or aliased_stack_load.group(3)
        base = state[base_register]
        if isinstance(base, StackPtr):
            displacement = int(aliased_stack_load.group(1) or 0)
            offset = base.offset + displacement
            destination_name = aliased_stack_load.group(4)
            if mnemonic == "mov.l" and offset % 4 == 0:
                slot = _stack_load(state, offset)
                state[destination_name] = _stack_slot_value(slot)
                state[f"{destination_name}_stack_origin"] = StackOrigin(
                    (offset,), slot.store_addresses, slot.exact_symbols,
                    slot.unknown_store,
                )
                _set_register_stack_alias(
                    state, destination_name, StackAlias(
                        slot.may_alias_stack, slot.frame_derived_stack_alias
                    )
                )
            elif mnemonic == "mov.w":
                state[destination_name] = Interval("signed", -0x8000, 0x7FFF)
                _set_register_stack_alias(state, destination_name, True)
            else:
                state[destination_name] = Interval("signed", -0x80, 0x7F)
                _set_register_stack_alias(state, destination_name, True)
            return

    literal = _symbol_from_annotation(instruction.annotation)
    destination = destination_register
    single_destination = single_register
    if mnemonic in {"mov.l", "mov.w"} and literal and destination:
        state[destination.group(1)] = ConstSet("symbol", frozenset({literal}))
        _set_register_stack_alias(state, destination.group(1), False)
        return
    numeric_literal = re.fullmatch(r"(?:0x)?([0-9A-Fa-f]+)", instruction.annotation)
    if mnemonic in {"mov.l", "mov.w"} and numeric_literal and destination:
        value = int(numeric_literal.group(1), 16)
        bits = 16 if mnemonic == "mov.w" else 32
        if value & (1 << (bits - 1)):
            value -= 1 << bits
        state[destination.group(1)] = ConstSet("signed", frozenset({value}))
        _set_register_stack_alias(state, destination.group(1), False)
        return
    dereference = re.fullmatch(
        r"@(r(?:1[0-5]|\d)),\s*(r(?:1[0-5]|\d))", text
    )
    if mnemonic in {"mov.l", "mov.w", "mov.b"} and dereference:
        base = state[dereference.group(1)]
        if mnemonic == "mov.b":
            state[dereference.group(2)] = Interval("signed", -0x80, 0x7F)
            _set_register_stack_alias(state, dereference.group(2), True)
            return
        if mnemonic == "mov.w":
            state[dereference.group(2)] = Interval("signed", -0x8000, 0x7FFF)
            _set_register_stack_alias(state, dereference.group(2), True)
            return
        atoms = base.values if isinstance(base, ConstSet) and base.kind == "symbol" else ()
        if len(atoms) == 1:
            atom = next(iter(atoms))
            if isinstance(atom, SymbolAtom):
                if mnemonic == "mov.l" and atom.address in known_null_addresses:
                    state[dereference.group(2)] = ConstSet(
                        "unsigned", frozenset({0})
                    )
                    _set_register_stack_alias(state, dereference.group(2), False)
                    return
                state[dereference.group(2)] = ConstSet(
                    "symbol", frozenset({SymbolAtom(f"{atom.name}*", atom.address)})
                )
                _set_register_stack_alias(
                    state, dereference.group(2),
                    _external_address_may_alias_stack(state, atom.address),
                )
                return
        state[dereference.group(2)] = UNKNOWN
        _set_register_stack_alias(state, dereference.group(2), True)
        return
    memory_load = re.fullmatch(
        r"@\((\d+),(r(?:1[0-5]|\d))\),\s*(r(?:1[0-5]|\d))", text
    )
    if mnemonic in {"mov.l", "mov.w", "mov.b"} and memory_load:
        base = state[memory_load.group(2)]
        if mnemonic == "mov.b":
            state[memory_load.group(3)] = Interval("signed", -0x80, 0x7F)
            _set_register_stack_alias(state, memory_load.group(3), True)
            return
        if mnemonic == "mov.w":
            state[memory_load.group(3)] = Interval("signed", -0x8000, 0x7FFF)
            _set_register_stack_alias(state, memory_load.group(3), True)
            return
        atoms = base.values if isinstance(base, ConstSet) and base.kind == "symbol" else ()
        if len(atoms) == 1:
            atom = next(iter(atoms))
            if isinstance(atom, SymbolAtom):
                offset = int(memory_load.group(1))
                derived = SymbolAtom(f"{atom.name}+{offset}", atom.address + offset)
                state[memory_load.group(3)] = ConstSet("symbol", frozenset({derived}))
                _set_register_stack_alias(
                    state, memory_load.group(3),
                    _external_address_may_alias_stack(
                        state, atom.address + offset
                    ),
                )
                return
        state[memory_load.group(3)] = UNKNOWN
        _set_register_stack_alias(state, memory_load.group(3), True)
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
                    _set_register_stack_alias(state, indexed_load.group(3), False)
                    return
            atoms = bases.values if isinstance(bases, ConstSet) and bases.kind == "symbol" else ()
            if len(atoms) == 1:
                atom = next(iter(atoms))
                if isinstance(atom, SymbolAtom):
                    if mnemonic == "mov.b":
                        state[indexed_load.group(3)] = Interval("signed", -0x80, 0x7F)
                        _set_register_stack_alias(state, indexed_load.group(3), True)
                    elif mnemonic == "mov.w":
                        state[indexed_load.group(3)] = Interval(
                            "signed", -0x8000, 0x7FFF
                        )
                        _set_register_stack_alias(state, indexed_load.group(3), True)
                    else:
                        derived = SymbolAtom(f"{atom.name}[]", atom.address)
                        state[indexed_load.group(3)] = ConstSet(
                            "symbol", frozenset({derived})
                        )
                        _set_register_stack_alias(
                            state, indexed_load.group(3),
                            _indexed_external_may_alias_stack(
                                state, atom.address, indexes
                            ),
                        )
                    return
        state[indexed_load.group(3)] = UNKNOWN
        _set_register_stack_alias(state, indexed_load.group(3), True)
        return
    immediate = re.fullmatch(r"#(-?(?:0x[0-9a-fA-F]+|\d+)),\s*(r(?:1[0-5]|\d))", text)
    if mnemonic == "mov" and immediate:
        state[immediate.group(2)] = ConstSet("signed", frozenset({int(immediate.group(1), 0)}))
        _set_register_stack_alias(state, immediate.group(2), False)
        return
    if mnemonic == "and" and immediate:
        mask = int(immediate.group(1), 0)
        state[immediate.group(2)] = Interval("unsigned", 0, mask)
        _set_register_stack_alias(state, immediate.group(2), False)
        return
    if mnemonic == "add" and immediate:
        delta = int(immediate.group(1), 0)
        value = state[immediate.group(2)]
        if isinstance(value, StackPtr):
            next_offset = value.offset + delta
            if abs(next_offset) <= 4096 and next_offset % 4 == 0:
                state[immediate.group(2)] = StackPtr(next_offset)
            else:
                state[immediate.group(2)] = UNKNOWN
                if immediate.group(2) == "r15":
                    _invalidate_stack(state)
        elif isinstance(value, StackPtrRange):
            state[immediate.group(2)] = StackPtrRange(
                None if value.lo is None else value.lo + delta,
                None if value.hi is None else value.hi + delta,
            )
        elif isinstance(value, MaybeStackPtr):
            state[immediate.group(2)] = MAYBE_STACK_PTR
        elif isinstance(value, ConstSet) and all(isinstance(x, int) for x in value.values):
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
        state[f"{move.group(2)}_stack_origin"] = state[
            f"{move.group(1)}_stack_origin"
        ]
        state[f"{move.group(2)}_stack_alias"] = state[
            f"{move.group(1)}_stack_alias"
        ]
        if move.group(2) == "r15" and not isinstance(state["r15"], StackPtr):
            _invalidate_stack(state)
        return
    binary = re.fullmatch(
        r"(r(?:1[0-5]|\d)),\s*(r(?:1[0-5]|\d))", text
    )
    if mnemonic == "add" and binary:
        source, target = state[binary.group(1)], state[binary.group(2)]
        has_symbol_base = any(
            isinstance(value, ConstSet) and value.kind == "symbol"
            for value in (source, target)
        )
        has_stack_operand = any(
            isinstance(value, (StackPtr, StackPtrRange, MaybeStackPtr))
            for value in (source, target)
        )
        result_may_alias_stack = (
            has_stack_operand
            or not has_symbol_base and (
                _register_may_alias_stack(state, binary.group(1))
                or _register_may_alias_stack(state, binary.group(2))
            )
        )
        source_integers = (
            source.values if isinstance(source, ConstSet)
            and source.kind in {"signed", "unsigned"}
            and all(isinstance(item, int) for item in source.values) else ()
        )
        target_integers = (
            target.values if isinstance(target, ConstSet)
            and target.kind in {"signed", "unsigned"}
            and all(isinstance(item, int) for item in target.values) else ()
        )
        stack_offset: int | None = None
        if isinstance(source, StackPtr) and len(target_integers) == 1:
            stack_offset = source.offset + int(next(iter(target_integers)))
        elif isinstance(target, StackPtr) and len(source_integers) == 1:
            stack_offset = target.offset + int(next(iter(source_integers)))
        stack_alias_result = isinstance(
            source, (StackPtr, StackPtrRange, MaybeStackPtr)
        ) or isinstance(target, (StackPtr, StackPtrRange, MaybeStackPtr))
        if stack_offset is not None:
            state[binary.group(2)] = (
                StackPtr(stack_offset)
                if abs(stack_offset) <= 4096 and stack_offset % 4 == 0
                else MAYBE_STACK_PTR
            )
        elif stack_alias_result:
            state[binary.group(2)] = MAYBE_STACK_PTR
        elif isinstance(source, ConstSet) and isinstance(target, ConstSet) \
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
                _set_register_stack_alias(
                    state, binary.group(2), result_may_alias_stack
                )
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
        _set_register_stack_alias(
            state, binary.group(2), result_may_alias_stack
        )
        return
    if mnemonic == "and" and binary:
        source, target = state[binary.group(1)], state[binary.group(2)]
        result_may_alias_stack = (
            _register_may_alias_stack(state, binary.group(1))
            or _register_may_alias_stack(state, binary.group(2))
        )
        if isinstance(source, ConstSet) and len(source.values) == 1:
            mask = next(iter(source.values))
            if isinstance(mask, int) and mask >= 0:
                state[binary.group(2)] = Interval("unsigned", 0, mask)
                _set_register_stack_alias(
                    state, binary.group(2), result_may_alias_stack
                )
                return
        state[binary.group(2)] = UNKNOWN
        _set_register_stack_alias(
            state, binary.group(2), result_may_alias_stack
        )
        return
    if mnemonic in {"extu.b", "extu.w", "exts.b", "exts.w"} and binary:
        source = state[binary.group(1)]
        if isinstance(source, ConstSet) and all(isinstance(x, int) for x in source.values):
            bits = 8 if mnemonic.endswith(".b") else 16
            mask = (1 << bits) - 1
            sign = 1 << (bits - 1)
            values = []
            for item in source.values:
                value = int(item) & mask
                if mnemonic.startswith("exts") and value & sign:
                    value -= 1 << bits
                values.append(value)
            state[binary.group(2)] = ConstSet(
                "unsigned" if mnemonic.startswith("extu") else "signed",
                frozenset(values),
            )
        else:
            if mnemonic == "extu.b":
                state[binary.group(2)] = Interval("unsigned", 0, 0xFF)
            elif mnemonic == "extu.w":
                state[binary.group(2)] = Interval("unsigned", 0, 0xFFFF)
            elif mnemonic == "exts.b":
                state[binary.group(2)] = Interval("signed", -0x80, 0x7F)
            else:
                state[binary.group(2)] = Interval("signed", -0x8000, 0x7FFF)
        state[f"{binary.group(2)}_stack_alias"] = state[
            f"{binary.group(1)}_stack_alias"
        ]
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
        _set_register_stack_alias(state, destination.group(1), False)
        return
    if destination:
        state[destination.group(1)] = UNKNOWN
        _set_register_stack_alias(state, destination.group(1), True)
        known_destination_effects = {
            "add", "addc", "addv", "and", "exts.b", "exts.w", "extu.b", "extu.w",
            "lds", "mov", "mov.b", "mov.l", "mov.w", "mova", "movt", "neg", "negc",
            "not", "or", "rotcl", "rotcr", "rotl", "rotr", "shad", "shal", "shar",
            "shld", "shll", "shll2", "shll8", "shll16", "shlr", "shlr2", "shlr8",
            "shlr16", "sts", "sub", "subc", "subv", "swap.b", "swap.w", "xor",
            "xtrct", "div1", "dmuls.l", "dmulu.l", "mul.l", "muls.w", "mulu.w",
        }
        if mnemonic not in known_destination_effects:
            effects.append(UnresolvedEffect(
                owner.name, instruction.address, mnemonic, text,
                "unparseable register effect", function_identity,
            ))
        return
    if single_destination and mnemonic not in {"cmp/pl", "cmp/pz"}:
        state[single_destination.group(1)] = UNKNOWN
        _set_register_stack_alias(state, single_destination.group(1), True)
        known_single_register_effects = {
            "dt", "movt", "rotcl", "rotcr", "rotl", "rotr", "shal", "shar",
            "shll", "shll2", "shll8", "shll16", "shlr", "shlr2", "shlr8",
            "shlr16",
        }
        if mnemonic not in known_single_register_effects:
            effects.append(UnresolvedEffect(
                owner.name, instruction.address, mnemonic, text,
                "unparseable register effect", function_identity,
            ))
        return
    if mnemonic in {"mov.l", "mov.w", "mov.b", "sts.l"} and ",@" in text:
        base_match = re.search(r"@(?!-)(?:\(\d+,(r(?:1[0-5]|\d))\)|(r(?:1[0-5]|\d)))", text)
        if base_match:
            base_register = base_match.group(1) or base_match.group(2)
            base = state.get(base_register, UNKNOWN)
            if base is UNKNOWN and _register_may_alias_stack(
                state, base_register
            ):
                _invalidate_stack(state)
            return
        _invalidate_stack(state)
        effects.append(UnresolvedEffect(
            owner.name, instruction.address, mnemonic, text,
            "unparseable memory effect", function_identity,
        ))
        return
    if mnemonic == "lds.l" and text.endswith(",pr"):
        return
    known_no_write = {
        "nop", "tst", "cmp/eq", "cmp/hs", "cmp/hi", "cmp/ge", "cmp/gt",
        "cmp/pl", "cmp/pz",
        "bt", "bf", "bt.s", "bf.s", "bra", "bsr", "bsrf", "braf",
        "jsr", "jmp", "rts", "rte", ".word",
        "clrt", "sett", "clrmac", "div0u",
    }
    if mnemonic not in known_no_write:
        effects.append(UnresolvedEffect(
            owner.name, instruction.address, mnemonic, text,
            "unparseable register effect", function_identity,
        ))


def _straight_line_leaf_gpr_clobbers(
    target: int,
    instructions: dict[int, Instruction],
    owner: FunctionOwner,
    memory: dict[int, int],
    *,
    max_instructions: int = 256,
) -> LeafSummary | None:
    """Prove caller-saved GPR writes for a bounded, fully decoded leaf CFG.

    GCC's SH runtime and small compiled C leaves can contain conditional
    branches or bounded ``braf`` dispatches while still preserving most
    caller-saved registers.  Blanket ABI clobbering then loses live call
    targets that emitted callers deliberately keep in those registers.  This
    summary is deliberately fail-closed: every reachable instruction and
    delay slot must be modeled, every transfer must stay in the resolved
    owner, nested calls and unbounded computed transfers are rejected, and
    every reachable exit must restore the initial stack pointer.
    """
    initial_state = _unknown_state()
    for index in range(15):
        initial_state[f"r{index}"] = ConstSet(
            "symbol", frozenset({SymbolAtom(f"<leaf-probe:r{index}>", index)})
        )
    initial_sp = initial_state["r15"]
    clobbers: set[str] = set()
    writes_memory = False
    forbidden_control = {"bsr", "bsrf", "jmp", "jsr", "rte", "sleep", "trapa", ".word"}
    states: dict[int, dict[str, AbstractValue]] = {}
    queue: deque[tuple[int, dict[str, AbstractValue]]] = deque([(target, initial_state)])
    reached_return = False
    steps = 0

    def in_owner(address: int) -> bool:
        return owner.start <= address < owner.end and address in instructions

    def apply(
        instruction: Instruction, state: dict[str, AbstractValue]
    ) -> dict[str, AbstractValue] | None:
        nonlocal writes_memory
        if _instruction_writes_memory(instruction):
            writes_memory = True
        result = dict(state)
        before = {f"r{index}": result[f"r{index}"] for index in range(8)}
        effects: list[UnresolvedEffect] = []
        _write_effect(instruction, result, effects, owner, memory)
        if effects:
            return None
        clobbers.update(
            register for register, value in before.items()
            if result[register] != value
        )
        return result

    def delayed_state(
        address: int, state: dict[str, AbstractValue]
    ) -> dict[str, AbstractValue] | None:
        delay_address = address + 2
        delay = instructions.get(delay_address)
        if not in_owner(delay_address) or delay is None \
                or delay.mnemonic in DELAY_SLOT_CONTROL:
            return None
        return apply(delay, state)

    def schedule(address: int, state: dict[str, AbstractValue]) -> bool:
        if not in_owner(address):
            return False
        previous = states.get(address)
        merged, changed = _join_state(previous, state)
        if changed:
            states[address] = merged
            queue.append((address, merged))
        return True

    def braf_targets(address: int, value: AbstractValue) -> tuple[int, ...] | None:
        if isinstance(value, ConstSet) and all(isinstance(item, int) for item in value.values):
            offsets = sorted(int(item) for item in value.values)
        elif isinstance(value, Interval) and value.hi - value.lo + 1 <= 256:
            offsets = list(range(value.lo, value.hi + 1))
        else:
            return None
        targets = tuple(sorted({address + 4 + offset for offset in offsets}))
        if not targets or len(targets) > 256 or any(
            target & 1 or not in_owner(target) for target in targets
        ):
            return None
        return targets

    while queue:
        address, incoming = queue.popleft()
        # Ignore stale queued states superseded by a later join.
        state = states.get(address)
        if state is None:
            state = dict(incoming)
            states[address] = state
        elif state != incoming:
            continue
        steps += 1
        if steps > max_instructions:
            return None
        instruction = instructions.get(address)
        if instruction is None:
            return None
        mnemonic = instruction.mnemonic
        direct_target = _target_from_text(instruction.operands)

        if mnemonic == "rts":
            post = delayed_state(address, state)
            if post is None or post["r15"] != initial_sp:
                return None
            reached_return = True
            continue
        if mnemonic in forbidden_control:
            return None
        if mnemonic in {"bt", "bf"}:
            if direct_target is None or not in_owner(direct_target):
                return None
            true_state, false_state = _predicate_refined_states(
                state["t_predicate"], state
            )
            taken, fallthrough = (
                (true_state, false_state) if mnemonic == "bt"
                else (false_state, true_state)
            )
            if taken is not None and not schedule(direct_target, taken):
                return None
            if fallthrough is not None and not schedule(address + 2, fallthrough):
                return None
            continue
        if mnemonic in {"bt.s", "bf.s"}:
            if direct_target is None or not in_owner(direct_target):
                return None
            true_state, false_state = _predicate_refined_states(
                state["t_predicate"], state
            )
            taken, fallthrough = (
                (true_state, false_state) if mnemonic == "bt.s"
                else (false_state, true_state)
            )
            if taken is not None:
                post = delayed_state(address, taken)
                if post is None or not schedule(direct_target, post):
                    return None
            if fallthrough is not None:
                post = delayed_state(address, fallthrough)
                if post is None or not schedule(address + 4, post):
                    return None
            continue
        if mnemonic == "bra":
            if direct_target is None or not in_owner(direct_target):
                return None
            post = delayed_state(address, state)
            if post is None or not schedule(direct_target, post):
                return None
            continue
        if mnemonic == "braf":
            register = re.fullmatch(r"@?(r(?:1[0-5]|\d))", instruction.operands)
            targets = braf_targets(
                address, state.get(register.group(1), UNKNOWN) if register else UNKNOWN
            )
            post = delayed_state(address, state)
            if targets is None or post is None:
                return None
            if any(not schedule(next_address, post) for next_address in targets):
                return None
            continue

        post = apply(instruction, state)
        if post is None or not schedule(address + 2, post):
            return None

    return LeafSummary(frozenset(clobbers), writes_memory) if reached_return else None


def _analyze_code_only_pass(
    instructions: dict[int, Instruction],
    owners: Iterable[FunctionOwner],
    decoded_lines: dict[str, set[int]] | None = None,
    selected_names: set[str] | None = None,
    instruction_memory: dict[int, int] | None = None,
    max_steps_per_seed: int = 100000,
    include_owner_entry: bool = True,
    owner_address_map: dict[int, FunctionOwner] | None = None,
    local_islands: Iterable[LocalIsland] = (),
    profile_by_owner: dict[str, Counter[str]] | None = None,
    edge_sink: set[tuple[str, int, int]] | None = None,
    node_sink: set[tuple[str, int]] | None = None,
    evaluated_state_sink: set[tuple[str, int]] | None = None,
    frozen_edges: frozenset[tuple[str, int, int]] | None = None,
    new_edge_sink: set[tuple[str, int, int]] | None = None,
    interleave_initial_seeds: bool = False,
    phase: str = "acceptance",
    discovery_iteration: int = 0,
    progress_callback: Callable[[dict[str, object]], None] | None = None,
    progress_counter: Counter[str] | None = None,
    protected_entry_nodes: frozenset[tuple[str, int]] = frozenset(),
    known_null_addresses: frozenset[int] = frozenset(),
    island_origins: dict[str, frozenset[str]] | None = None,
    selected_owner_identities: set[str] | None = None,
) -> CodeAnalysis:
    if selected_names is not None and selected_owner_identities is not None:
        raise ValueError("code-only owner selection cannot mix names and identities")
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
    island_by_identity = {
        _local_island_identity(island): island for island in island_list
    }
    islands_by_owner: defaultdict[str, list[tuple[str, LocalIsland]]] = defaultdict(
        list
    )
    for identity, origin_names in (island_origins or {}).items():
        island = island_by_identity.get(identity)
        if island is None:
            continue
        for origin_name in origin_names:
            islands_by_owner[origin_name].append((identity, island))
    code_addresses: set[int] = set()
    call_emissions: dict[tuple[object, ...], list[CallSite]] = {}
    fact_emissions: dict[tuple[object, ...], list[DirectCallFact]] = {}
    implementation_emissions: dict[
        tuple[object, ...], list[ImplementationTransferFact]
    ] = {}
    unresolved_emissions: dict[tuple[object, ...], list[UnresolvedTransfer]] = {}
    effect_emissions: dict[tuple[object, ...], list[UnresolvedEffect]] = {}
    leaf_clobber_cache: dict[int, LeafSummary | None] = {}

    for owner in owner_list:
        owner_identity = _bounded_owner_identity(owner)
        result_owner_identity = (
            owner_identity if selected_owner_identities is not None else None
        )
        owner_key = (
            owner_identity
            if selected_owner_identities is not None
            else owner.name
        )
        if selected_owner_identities is not None:
            if owner_identity not in selected_owner_identities:
                continue
        elif selected_names is not None and owner.name not in selected_names:
            continue
        seed_rows: list[tuple[tuple[int, str], LocalIsland | None]] = [
            *((((owner.start, "entry"), None),) if include_owner_entry else ()),
            *(
                ((address, "decodedline"), None)
                for address in sorted(lines.get(owner_key, set()))
            ),
            *(
                ((address, "decodedline"), island)
                for identity, island in sorted(
                    islands_by_owner.get(owner_key, ()),
                    key=lambda item: item[0],
                )
                for address in sorted(lines.get(identity, set()))
            ),
        ]
        seeds = [seed for seed, _island in seed_rows]
        owner_states = 0
        if progress_callback is not None:
            progress_callback({
                "event": "owner_start",
                "phase": phase,
                "iteration": discovery_iteration,
                "owner": owner.name,
                "seeds": len(seeds),
                "states": 0,
                "edges": len(edge_sink or frozen_edges or ()),
            })
        profile = (
            profile_by_owner.setdefault(owner.name, Counter())
            if profile_by_owner is not None else None
        )
        states: dict[tuple[object, ...], dict[str, AbstractValue]] = {}
        evaluated_addresses: set[int] = set()
        widening: dict[tuple[object, ...], dict[str, tuple[bool, bool]]] = {}
        provenance_by_key: dict[tuple[object, ...], tuple[tuple[int, str], ...]] = {}
        provenance_truncated_by_key: dict[tuple[object, ...], bool] = {}
        predecessors_by_key: dict[tuple[object, ...], tuple[int, ...]] = {}
        predecessors_truncated_by_key: dict[tuple[object, ...], bool] = {}
        entry_join_changed_by_key: dict[tuple[object, ...], bool] = {}
        queue: deque[
            tuple[
                tuple[int, str], LocalIsland | None, int, int,
                dict[str, AbstractValue], bool, tuple[tuple[int, str], ...], bool,
                int | None,
            ]
        ] = deque()
        pending_seeds = deque(
            (seed, island)
            for seed, island in seed_rows
            if seed[0] in instructions
        )
        if interleave_initial_seeds:
            while pending_seeds:
                seed, seed_island = pending_seeds.popleft()
                queue.append((
                    seed, seed_island, seed[0], seed[0], _unknown_state(
                        entry_arguments_nonstack=seed[1] == "entry"
                    ), False,
                    (seed,), False, None,
                ))
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
        steps_by_seed: defaultdict[tuple[int, str], int] = defaultdict(int)
        while queue or pending_seeds:
            if not queue:
                while pending_seeds and not queue:
                    seed, seed_island = pending_seeds.popleft()
                    if seed[1] == "decodedline" and seed[0] in evaluated_addresses:
                        if profile is not None:
                            profile["covered_decoded_seeds_skipped"] += 1
                        continue
                    queue.append((
                        seed, seed_island, seed[0], seed[0], _unknown_state(
                            entry_arguments_nonstack=seed[1] == "entry"
                        ), False,
                        (seed,), False, None,
                    ))
                if not queue:
                    continue
            (
                seed, island, island_entry, address, incoming, is_backedge,
                incoming_provenance, incoming_provenance_truncated, predecessor,
            ) = queue.popleft()
            owner_states += 1
            if progress_counter is not None:
                progress_counter["states"] += 1
            if progress_callback is not None and owner_states % 4096 == 0:
                progress_callback({
                    "event": "owner_progress",
                    "phase": phase,
                    "iteration": discovery_iteration,
                    "owner": owner.name,
                    "seeds": len(seeds),
                    "states": owner_states,
                    "edges": len(edge_sink or frozen_edges or ()),
                })
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
            key = (
                ("island", island.name, address)
                if island is not None
                else ("owner", address)
            )
            if profile is not None and key in states:
                profile["revisits"] += 1
            previous_state = states.get(key)
            state, state_changed = _join_state(
                previous_state,
                incoming,
                backedge=is_backedge,
                widening=widening.setdefault(key, {}),
            )
            if phase == "discovery":
                # Discovery state and diagnostics are disposable. Re-evaluating a
                # program point solely because another decoded-line seed added
                # provenance or a predecessor multiplies work without discovering
                # a new instruction or edge. Acceptance retains the full metadata.
                provenance = ()
                provenance_truncated = False
                provenance_changed = False
                predecessors = ()
                predecessors_truncated = False
                predecessors_changed = False
                entry_join_changed = False
            else:
                provenance_values = sorted(set(
                    (*provenance_by_key.get(key, ()), *incoming_provenance)
                ))
                provenance_truncated = (
                    provenance_truncated_by_key.get(key, False)
                    or incoming_provenance_truncated
                    or len(provenance_values) > 16
                )
                provenance = tuple(provenance_values[:16])
                provenance_changed = (
                    provenance != provenance_by_key.get(key, ())
                    or provenance_truncated != provenance_truncated_by_key.get(key, False)
                )
                predecessor_values = sorted(set((
                    *predecessors_by_key.get(key, ()),
                    *(() if predecessor is None else (predecessor,)),
                )))
                predecessors_truncated = (
                    predecessors_truncated_by_key.get(key, False)
                    or len(predecessor_values) > 16
                )
                predecessors = tuple(predecessor_values[:16])
                predecessors_changed = (
                    predecessors != predecessors_by_key.get(key, ())
                    or predecessors_truncated != predecessors_truncated_by_key.get(key, False)
                )
                previous_provenance = provenance_by_key.get(key, ())
                mixed_entry_decoded_join = (
                    previous_state is not None
                    and state_changed
                    and any(kind == "entry" for _, kind in previous_provenance)
                    and any(kind == "decodedline" for _, kind in incoming_provenance)
                )
                entry_join_changed = (
                    entry_join_changed_by_key.get(key, False) or mixed_entry_decoded_join
                )
            if not (state_changed or provenance_changed or predecessors_changed):
                continue
            states[key] = state
            evaluated_addresses.add(address)
            provenance_by_key[key] = provenance
            provenance_truncated_by_key[key] = provenance_truncated
            predecessors_by_key[key] = predecessors
            predecessors_truncated_by_key[key] = predecessors_truncated
            entry_join_changed_by_key[key] = entry_join_changed
            call_emissions[key] = []
            fact_emissions[key] = []
            implementation_emissions[key] = []
            unresolved_emissions[key] = []
            effect_emissions[key] = []
            code_addresses.add(address)
            if node_sink is not None:
                node_sink.add((owner_key, address))
            if evaluated_state_sink is not None:
                evaluated_state_sink.add((owner_key, address))
            ins = instructions[address]
            mnemonic = ins.mnemonic
            caller_region = "island" if island is not None else "owner"
            caller_island = island.name if island is not None else None
            caller_offset = address - (island.start if island is not None else owner.start)

            def unresolved_transfer(
                register_name: str | None = None,
                register_value: AbstractValue | None = None,
            ) -> UnresolvedTransfer:
                stack_origin = state.get(
                    f"{register_name}_stack_origin", StackOrigin()
                )
                if not isinstance(stack_origin, StackOrigin):
                    stack_origin = StackOrigin()
                return UnresolvedTransfer(
                    owner.name,
                    address,
                    mnemonic,
                    register_name,
                    register_value,
                    provenance,
                    provenance_truncated,
                    predecessors,
                    predecessors_truncated,
                    entry_join_changed,
                    stack_origin.offsets,
                    stack_origin.store_addresses,
                    (
                        "static"
                        if not stack_origin.unknown_store
                        and len(stack_origin.exact_symbols) == 1
                        and next(iter(stack_origin.exact_symbols)).address
                        in owner_by_address
                        else "dynamic"
                    ),
                    result_owner_identity,
                )

            def schedule(target: int, next_state: dict[str, AbstractValue]) -> None:
                if region_start <= target < region_end and target in instructions:
                    if seed[1] == "decodedline" and (
                        owner_key, target
                    ) in protected_entry_nodes:
                        return
                    edge = (owner_key, address, target)
                    if edge_sink is not None:
                        edge_sink.add(edge)
                    if frozen_edges is not None and edge not in frozen_edges:
                        if new_edge_sink is not None:
                            new_edge_sink.add(edge)
                        return
                    queue.append(
                        (
                            seed, island, island_entry, target, next_state,
                            target <= address, provenance, provenance_truncated, address,
                        )
                    )

            def schedule_island(
                target_island: LocalIsland,
                entry: int,
                next_state: dict[str, AbstractValue],
            ) -> None:
                if entry in instructions:
                    if seed[1] == "decodedline" and (
                        owner_key, entry
                    ) in protected_entry_nodes:
                        return
                    edge = (owner_key, address, entry)
                    if edge_sink is not None:
                        edge_sink.add(edge)
                    if frozen_edges is not None and edge not in frozen_edges:
                        if new_edge_sink is not None:
                            new_edge_sink.add(edge)
                        return
                    queue.append((
                        seed, target_island, entry, entry, next_state, False,
                        provenance, provenance_truncated, address,
                    ))

            def after_slot(
                base_state: dict[str, AbstractValue]
            ) -> dict[str, AbstractValue] | None:
                slot_address = address + 2
                if slot_address not in instructions \
                        or not region_start <= slot_address < region_end:
                    unresolved_emissions[key].append(unresolved_transfer())
                    return None
                if instructions[slot_address].mnemonic in DELAY_SLOT_CONTROL:
                    unresolved_emissions[key].append(unresolved_transfer())
                    return None
                result = dict(base_state)
                code_addresses.add(slot_address)
                if node_sink is not None:
                    node_sink.add((owner_key, slot_address))
                slot_effects: list[UnresolvedEffect] = []
                _write_effect(
                    instructions[slot_address], result, slot_effects, owner, memory,
                    profile, known_null_addresses, result_owner_identity,
                )
                for effect in slot_effects:
                    effect_emissions[key].append(effect)
                return result

            direct_target = _target_from_text(ins.operands)
            if mnemonic in {"bsr", "bsrf", "jsr"}:
                if profile is not None:
                    profile["call_transfer_evaluations"] += 1
                register = None
                value: AbstractValue | None = None
                target_atoms: list[tuple[int, SymbolAtom | None]] = []
                if direct_target is not None:
                    target_atoms.append((direct_target, None))
                if mnemonic in {"jsr", "bsrf"}:
                    register = re.search(r"@?(r(?:1[0-5]|\d))", ins.operands)
                    value = state.get(register.group(1), UNKNOWN) if register else UNKNOWN
                    atoms = value.values if isinstance(value, ConstSet) and value.kind == "symbol" else ()
                    if atoms and all(isinstance(atom, SymbolAtom) for atom in atoms):
                        target_atoms = sorted(
                            ((atom.address, atom) for atom in atoms),
                            key=lambda item: (item[0], item[1].name),
                        )
                    elif mnemonic == "bsrf" and isinstance(value, ConstSet) and value.kind != "symbol":
                        ints = [x for x in value.values if isinstance(x, int)]
                        target_atoms = (
                            [(address + 4 + ints[0], None)] if len(ints) == 1 else []
                        )
                    else:
                        target_atoms = []

                resolved_targets: dict[tuple[object, ...], tuple[object, ...]] = {}
                invalid_target = not target_atoms
                for target_address, _resolved_atom in target_atoms:
                    callee = owner_by_address.get(target_address)
                    target_island = (
                        None if callee is not None
                        else _island_for_direct_target(target_address, ins.operands, island_list)
                    )
                    if target_island is not None:
                        canonical = (
                            "island", target_island.name,
                            target_address - target_island.start,
                        )
                        resolved_targets[canonical] = (
                            "island", target_island, target_address,
                        )
                    elif callee is None:
                        invalid_target = True
                    else:
                        canonical = (
                            "callee", _bounded_owner_identity(callee),
                            target_address - callee.start,
                        )
                        resolved_targets[canonical] = (
                            "callee", callee, target_address,
                        )

                if invalid_target:
                    unresolved_emissions[key].append(unresolved_transfer(
                        register.group(1)
                        if mnemonic in {"bsrf", "jsr"} and register else None,
                        value if mnemonic in {"bsrf", "jsr"} else None,
                    ))

                callee_entry = after_slot(state)
                if callee_entry is None:
                    continue
                continuation = address + 4
                if not region_start <= continuation < region_end \
                        or continuation not in instructions:
                    unresolved_emissions[key].append(unresolved_transfer())
                    continue
                if not invalid_target:
                    for canonical in sorted(resolved_targets):
                        target = resolved_targets[canonical]
                        if target[0] == "island":
                            target_island = target[1]
                            target_address = target[2]
                            implementation_emissions[key].append(
                                ImplementationTransferFact(
                                    owner.name,
                                    caller_offset,
                                    target_island.name,
                                    target_address - target_island.start,
                                    1,
                                    caller_region,
                                    caller_island,
                                    result_owner_identity,
                                )
                            )
                            schedule_island(target[1], target[2], callee_entry)
                        else:
                            callee = target[1]
                            target_address = target[2]
                            call_emissions[key].append(
                                CallSite(
                                    owner.name,
                                    address,
                                    callee.name,
                                    result_owner_identity,
                                    (
                                        _bounded_owner_identity(callee)
                                        if result_owner_identity is not None
                                        else None
                                    ),
                                )
                            )
                            fact_emissions[key].append(DirectCallFact(
                                owner.name, caller_offset, callee.name,
                                target_address - callee.start, 1,
                                caller_region, caller_island,
                                result_owner_identity,
                                (
                                    _bounded_owner_identity(callee)
                                    if result_owner_identity is not None
                                    else None
                                ),
                            ))
                post = dict(callee_entry)
                gpr_clobbers = {f"r{x}" for x in range(8)}
                all_proven = False
                proven_leaf_writes_memory = False
                if not invalid_target and resolved_targets and all(
                    target[0] == "callee" for target in resolved_targets.values()
                ):
                    proven_clobbers: set[str] = set()
                    all_proven = True
                    for target in resolved_targets.values():
                        callee = target[1]
                        target_address = target[2]
                        if target_address not in leaf_clobber_cache:
                            leaf_clobber_cache[target_address] = (
                                _straight_line_leaf_gpr_clobbers(
                                    target_address, instructions, callee, memory
                                )
                            )
                        summary = leaf_clobber_cache[target_address]
                        if summary is None:
                            all_proven = False
                            break
                        proven_clobbers.update(summary.gpr_clobbers)
                        proven_leaf_writes_memory |= summary.writes_memory
                    if all_proven:
                        gpr_clobbers = proven_clobbers
                if not all_proven or proven_leaf_writes_memory:
                    call_may_alias_stack = any(
                        _register_may_alias_stack(post, register)
                        for register in ("r4", "r5", "r6", "r7")
                    ) or bool(_external_stack_aliases(post).addresses) \
                        or _external_stack_aliases(post).unknown_address
                    _invalidate_escaped_argument_slots(post)
                    if _external_stack_aliases(post).addresses \
                            or _external_stack_aliases(post).unknown_address:
                        _invalidate_stack(post)
                else:
                    call_may_alias_stack = False
                for abi_register in [
                    *sorted(gpr_clobbers), "mach", "macl", "t_predicate"
                ]:
                    post[abi_register] = UNKNOWN
                    if abi_register.startswith("r"):
                        post[f"{abi_register}_stack_origin"] = StackOrigin()
                        post[f"{abi_register}_stack_alias"] = StackAlias(
                            call_may_alias_stack
                        )
                if profile is not None:
                    profile["abi_continuations_scheduled"] += 1
                schedule(continuation, post)
                continue
            if mnemonic in {"jmp", "braf"}:
                register = re.search(r"@?(r(?:1[0-5]|\d))", ins.operands)
                value = state.get(register.group(1), UNKNOWN) if register else UNKNOWN
                targets: list[int] = []
                if isinstance(value, ConstSet) and all(isinstance(x, int) for x in value.values):
                    targets = [int(x) + (address + 4 if mnemonic == "braf" else 0) for x in value.values]
                elif isinstance(value, ConstSet) and value.kind == "symbol":
                    symbol_atoms = [x for x in value.values if isinstance(x, SymbolAtom)]
                    targets = [x.address for x in symbol_atoms]
                elif isinstance(value, Interval) and value.hi - value.lo + 1 <= 256:
                    targets = list(range(value.lo, value.hi + 1))
                validated_targets = (
                    tuple(sorted(set(targets))) if targets and len(set(targets)) <= 256
                    and all(not (target & 1) and target in owner_by_address for target in targets)
                    else None
                )
                if validated_targets is None:
                    unresolved_emissions[key].append(unresolved_transfer(
                        register.group(1) if register else None,
                        value,
                    ))
                else:
                    post = after_slot(state)
                    if post is None:
                        continue
                    for target in validated_targets:
                        callee = owner_by_address.get(target)
                        if callee == owner:
                            schedule(target, post)
                        elif callee:
                            call_emissions[key].append(
                                CallSite(
                                    owner.name,
                                    address,
                                    callee.name,
                                    result_owner_identity,
                                    (
                                        _bounded_owner_identity(callee)
                                        if result_owner_identity is not None
                                        else None
                                    ),
                                )
                            )
                            fact_emissions[key].append(DirectCallFact(
                                owner.name, caller_offset, callee.name,
                                target - callee.start, 1,
                                caller_region, caller_island,
                                result_owner_identity,
                                (
                                    _bounded_owner_identity(callee)
                                    if result_owner_identity is not None
                                    else None
                                ),
                            ))
                continue
            if mnemonic in {"rts", "rte"}:
                after_slot(state)
                continue
            if mnemonic == "bra":
                post = after_slot(state)
                if post is None:
                    continue
                if direct_target is None:
                    unresolved_emissions[key].append(unresolved_transfer())
                else:
                    direct_owner = owner_by_address.get(direct_target)
                    target_island = (
                        None if direct_owner is not None
                        else _island_for_direct_target(direct_target, ins.operands, island_list)
                    )
                    if target_island is not None:
                        implementation_emissions[key].append(ImplementationTransferFact(
                            owner.name,
                            caller_offset,
                            target_island.name,
                            direct_target - target_island.start,
                            1,
                            caller_region,
                            caller_island,
                            result_owner_identity,
                        ))
                        schedule_island(target_island, direct_target, post)
                    elif direct_owner is None:
                        unresolved_emissions[key].append(unresolved_transfer())
                    elif direct_owner != owner:
                        call_emissions[key].append(
                            CallSite(
                                owner.name,
                                address,
                                direct_owner.name,
                                result_owner_identity,
                                (
                                    _bounded_owner_identity(direct_owner)
                                    if result_owner_identity is not None
                                    else None
                                ),
                            )
                        )
                        fact_emissions[key].append(DirectCallFact(
                            owner.name, caller_offset, direct_owner.name,
                            direct_target - direct_owner.start, 1,
                            caller_region, caller_island,
                            result_owner_identity,
                            (
                                _bounded_owner_identity(direct_owner)
                                if result_owner_identity is not None
                                else None
                            ),
                        ))
                    else:
                        schedule(direct_target, post)
                continue
            if mnemonic in {"bt", "bf"}:
                if direct_target is None \
                        or not region_start <= direct_target < region_end \
                        or direct_target not in instructions:
                    unresolved_emissions[key].append(unresolved_transfer())
                    continue
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
                fallthrough_address = address + 2
                if fallthrough is not None and (
                    not region_start <= fallthrough_address < region_end
                    or fallthrough_address not in instructions
                ):
                    unresolved_emissions[key].append(unresolved_transfer())
                    continue
                if taken is not None:
                    schedule(direct_target, taken)
                if fallthrough is not None:
                    schedule(fallthrough_address, fallthrough)
                continue
            if mnemonic in {"bt.s", "bf.s"}:
                if direct_target is None \
                        or not region_start <= direct_target < region_end \
                        or direct_target not in instructions:
                    unresolved_emissions[key].append(unresolved_transfer())
                    continue
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
                fallthrough_address = address + 4
                if fallthrough is not None and (
                    not region_start <= fallthrough_address < region_end
                    or fallthrough_address not in instructions
                ):
                    unresolved_emissions[key].append(unresolved_transfer())
                    continue
                if taken is not None:
                    post = after_slot(taken)
                    if post is not None:
                        schedule(direct_target, post)
                if fallthrough is not None:
                    post = after_slot(fallthrough)
                    if post is not None:
                        schedule(fallthrough_address, post)
                continue
            next_state = dict(state)
            new_effects: list[UnresolvedEffect] = []
            _write_effect(
                ins, next_state, new_effects, owner, memory, profile,
                known_null_addresses, result_owner_identity,
            )
            for effect in new_effects:
                effect_emissions[key].append(effect)
            fallthrough_address = address + 2
            if region_start <= fallthrough_address < region_end \
                    and fallthrough_address in instructions:
                schedule(fallthrough_address, next_state)
            else:
                unresolved_emissions[key].append(unresolved_transfer())

        if progress_callback is not None:
            progress_callback({
                "event": "owner_complete",
                "phase": phase,
                "iteration": discovery_iteration,
                "owner": owner.name,
                "seeds": len(seeds),
                "states": owner_states,
                "edges": len(edge_sink or frozen_edges or ()),
            })

    calls_by_site = {
        (
            item.caller_identity or item.caller,
            item.address,
            item.helper_identity or item.helper,
        ): item
        for rows in call_emissions.values() for item in rows
    }
    facts_by_site = {
        (
            item.caller_identity or item.caller,
            item.callee_identity or item.callee,
            item.caller, item.caller_region, item.caller_island,
            item.caller_offset, item.callee, item.callee_offset,
        ): item
        for rows in fact_emissions.values() for item in rows
    }
    implementation_by_site = {
        (
            item.caller_identity or item.caller,
            item.caller, item.caller_region, item.caller_island,
            item.caller_offset, item.target_island, item.target_offset,
        ): item
        for rows in implementation_emissions.values() for item in rows
    }
    unresolved = {
        (item.caller_identity or item.caller, item.address, item.mnemonic): item
        for rows in unresolved_emissions.values() for item in rows
    }
    effects = {
        (item.function_identity or item.function, item.address, item.mnemonic): item
        for rows in effect_emissions.values() for item in rows
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


def _acceptance_component_seeds(
    owner: FunctionOwner,
    owner_key: str,
    decoded_addresses: set[int],
    nodes: set[tuple[str, int]],
    edges: frozenset[tuple[str, int, int]],
    entry_evaluated_nodes: set[tuple[str, int]],
    metrics: Counter[str] | None = None,
) -> set[int]:
    """Return decoded candidates in components not evaluated from owner entry.

    The acceptance pass processes these serially and skips a candidate once an
    earlier seed actually evaluated it. Keeping every candidate here is needed
    because weakly connected sibling arms can converge without either arm being
    directionally reachable from the other.
    """
    owner_nodes = {address for name, address in nodes if name == owner_key}
    owner_edges = {
        (source, target)
        for name, source, target in edges
        if name == owner_key
    }
    neighbors: defaultdict[int, set[int]] = defaultdict(set)
    for source, target in owner_edges:
        neighbors[source].add(target)
        neighbors[target].add(source)
        owner_nodes.update((source, target))

    entry_evaluated = {
        address
        for name, address in entry_evaluated_nodes
        if name == owner_key
    }

    seeds: set[int] = set()
    unreachable_nodes = owner_nodes - entry_evaluated
    remaining = set(unreachable_nodes)
    if metrics is not None and entry_evaluated:
        metrics["weak_components"] += 1
    while remaining:
        if metrics is not None:
            metrics["weak_components"] += 1
        first = min(remaining)
        component: set[int] = set()
        pending = [first]
        while pending:
            address = pending.pop()
            if address in component:
                continue
            component.add(address)
            pending.extend(sorted(
                (neighbor for neighbor in neighbors[address]
                 if neighbor in unreachable_nodes),
                reverse=True,
            ))
        remaining.difference_update(component)
        candidates = sorted(component & decoded_addresses)
        if candidates:
            seeds.update(candidates)
            if metrics is not None:
                metrics["disconnected_components"] += 1
    return seeds


def analyze_code_only(
    instructions: dict[int, Instruction],
    owners: Iterable[FunctionOwner],
    decoded_lines: dict[str, set[int]] | None = None,
    selected_names: set[str] | None = None,
    instruction_memory: dict[int, int] | None = None,
    max_steps_per_seed: int = 100000,
    include_owner_entry: bool = True,
    owner_address_map: dict[int, FunctionOwner] | None = None,
    local_islands: Iterable[LocalIsland] = (),
    profile_by_owner: dict[str, Counter[str]] | None = None,
    max_discovery_restarts: int = 8,
    progress_callback: Callable[[dict[str, object]], None] | None = None,
    known_null_addresses: frozenset[int] = frozenset(),
    island_origins: dict[str, frozenset[str]] | None = None,
    selected_owner_identities: set[str] | None = None,
) -> CodeAnalysis:
    """Discover a frozen CFG, then run acceptance dataflow from fresh states."""
    if selected_names is not None and selected_owner_identities is not None:
        raise ValueError("code-only owner selection cannot mix names and identities")
    owner_list = tuple(owners)

    def owner_key(owner: FunctionOwner) -> str:
        return (
            _bounded_owner_identity(owner)
            if selected_owner_identities is not None
            else owner.name
        )

    def owner_is_selected(owner: FunctionOwner) -> bool:
        if selected_owner_identities is not None:
            return owner_key(owner) in selected_owner_identities
        return selected_names is None or owner.name in selected_names

    island_list = tuple(local_islands)
    owner_map = (
        owner_address_map
        if owner_address_map is not None
        else build_owner_address_map(owner_list)
    )
    memory = (
        instruction_memory
        if instruction_memory is not None
        else build_instruction_memory(instructions)
    )
    source_lines = {
        name: set(addresses)
        for name, addresses in (decoded_lines or {}).items()
    }
    selected_owner_keys = {
        owner_key(owner) for owner in owner_list if owner_is_selected(owner)
    }
    owner_display_by_key = {
        owner_key(owner): owner.name for owner in owner_list
    }
    island_by_identity = {
        _local_island_identity(island): island for island in island_list
    }
    normalized_island_origins: dict[str, frozenset[str]] = {}
    for identity, origins in (island_origins or {}).items():
        if identity not in island_by_identity:
            raise ValueError(f"code-only island seed has no validated island: {identity}")
        origin_keys = frozenset(origins)
        invalid_origins = origin_keys - selected_owner_keys
        if not origin_keys or invalid_origins:
            detail = ", ".join(
                sorted(
                    owner_display_by_key.get(origin, origin)
                    for origin in invalid_origins
                )
            ) or "<none>"
            raise ValueError(
                f"code-only island seed has invalid owner origin: {identity}: {detail}"
            )
        normalized_island_origins[identity] = origin_keys
    decoded_island_identities = {
        name for name in source_lines if name.startswith("@island:")
    }
    for identity in sorted(decoded_island_identities):
        island = island_by_identity.get(identity)
        if island is None:
            raise ValueError(f"code-only decoded rows have no validated island: {identity}")
        if identity not in normalized_island_origins:
            raise ValueError(f"code-only decoded island has no owner origin: {identity}")
        if any(
            not island.code_start <= address < island.end
            for address in source_lines[identity]
        ):
            raise ValueError(f"code-only decoded row is outside island: {identity}")
    display_dead_nodes = sourceboot_null_task_submit_dead_nodes(
        instructions, owner_list, known_null_addresses
    )
    known_dead_nodes = {
        (owner_key(owner), address)
        for _display_name, address in display_dead_nodes
        for owner in (owner_map.get(address),)
        if owner is not None
    }
    learned_edges: set[tuple[str, int, int]] = set()
    analysis_started = monotonic()

    def emit_progress(event: dict[str, object]) -> None:
        if progress_callback is None:
            return
        row = dict(event)
        row["elapsed_ms"] = int((monotonic() - analysis_started) * 1000)
        progress_callback(row)

    for restart_count in range(max_discovery_restarts + 1):
        discovery_lines = {
            name: set(addresses) for name, addresses in source_lines.items()
        }
        for source_owner_key, _source, target in learned_edges:
            target_identities = [
                identity
                for identity, island in island_by_identity.items()
                if source_owner_key in normalized_island_origins.get(identity, ())
                and island.code_start <= target < island.end
            ]
            if len(target_identities) > 1:
                raise ValueError(
                    "code-only learned edge has ambiguous island origin: "
                    f"{owner_display_by_key.get(source_owner_key, source_owner_key)} "
                    f"at 0x{target:08x}"
                )
            discovery_lines.setdefault(
                target_identities[0] if target_identities else source_owner_key,
                set(),
            ).add(target)
        discovered_edges = set(learned_edges)
        discovered_nodes: set[tuple[str, int]] = set()
        discovery_counter: Counter[str] = Counter()
        discovery_seed_count = sum(
            int(include_owner_entry) + len(discovery_lines.get(owner_key(owner), set()))
            for owner in owner_list
            if owner_is_selected(owner)
        )
        discovery_seed_count += sum(
            len(discovery_lines.get(identity, set())) * len(origins)
            for identity, origins in normalized_island_origins.items()
        )
        phase_started = monotonic()
        emit_progress({
            "event": "phase_start",
            "phase": "discovery",
            "iteration": restart_count,
            "seeds": discovery_seed_count,
            "states": 0,
            "edges": len(discovered_edges),
        })
        _analyze_code_only_pass(
            instructions,
            owner_list,
            discovery_lines,
            selected_names,
            memory,
            max_steps_per_seed,
            include_owner_entry,
            owner_map,
            island_list,
            None,
            discovered_edges,
            discovered_nodes,
            interleave_initial_seeds=True,
            phase="discovery",
            discovery_iteration=restart_count,
            progress_callback=emit_progress,
            progress_counter=discovery_counter,
            known_null_addresses=known_null_addresses,
            island_origins=normalized_island_origins,
            selected_owner_identities=selected_owner_identities,
        )
        frozen_edges = frozenset(discovered_edges)
        emit_progress({
            "event": "phase_complete",
            "phase": "discovery",
            "iteration": restart_count,
            "seeds": discovery_seed_count,
            "states": discovery_counter["states"],
            "edges": len(frozen_edges),
            "nodes": len(discovered_nodes),
            "phase_elapsed_ms": int((monotonic() - phase_started) * 1000),
        })
        new_edges: set[tuple[str, int, int]] = set()
        acceptance_counter: Counter[str] = Counter()
        phase_started = monotonic()
        emit_progress({
            "event": "phase_start",
            "phase": "acceptance",
            "iteration": restart_count,
            "seeds": int(include_owner_entry) * sum(
                1 for owner in owner_list if owner_is_selected(owner)
            ),
            "states": 0,
            "edges": len(frozen_edges),
        })
        entry_evaluated_nodes: set[tuple[str, int]] = set()
        accepted_entry = _analyze_code_only_pass(
            instructions,
            owner_list,
            {},
            selected_names,
            memory,
            max_steps_per_seed,
            include_owner_entry,
            owner_map,
            island_list,
            profile_by_owner,
            frozen_edges=frozen_edges,
            new_edge_sink=new_edges,
            phase="acceptance",
            discovery_iteration=restart_count,
            progress_callback=emit_progress,
            progress_counter=acceptance_counter,
            evaluated_state_sink=entry_evaluated_nodes,
            known_null_addresses=known_null_addresses,
            island_origins=normalized_island_origins,
            selected_owner_identities=selected_owner_identities,
        )
        acceptance_lines: dict[str, set[int]] = {}
        component_metrics: Counter[str] = Counter()
        live_discovered_nodes = discovered_nodes - known_dead_nodes
        for owner in owner_list:
            if not owner_is_selected(owner):
                continue
            current_owner_key = owner_key(owner)
            dead_owner_addresses = {
                address for name, address in known_dead_nodes
                if name == current_owner_key
            }
            decoded_addresses = set(source_lines.get(current_owner_key, set()))
            for identity, origins in normalized_island_origins.items():
                if current_owner_key in origins:
                    decoded_addresses.update(source_lines.get(identity, set()))
            component_seeds = _acceptance_component_seeds(
                owner,
                current_owner_key,
                decoded_addresses - dead_owner_addresses,
                live_discovered_nodes,
                frozen_edges,
                entry_evaluated_nodes,
                component_metrics,
            )
            for address in component_seeds:
                target_identities = [
                    identity
                    for identity, island in island_by_identity.items()
                    if current_owner_key in normalized_island_origins.get(identity, ())
                    and island.code_start <= address < island.end
                ]
                if len(target_identities) > 1:
                    raise ValueError(
                        "code-only component seed has ambiguous island origin: "
                        f"{owner.name} at 0x{address:08x}"
                    )
                acceptance_lines.setdefault(
                    target_identities[0] if target_identities else current_owner_key,
                    set(),
                ).add(address)
        acceptance_seed_count = sum(len(value) for value in acceptance_lines.values())
        emit_progress({
            "event": "components_complete",
            "phase": "components",
            "iteration": restart_count,
            "seeds": acceptance_seed_count,
            "acceptance_seeds": acceptance_seed_count,
            "states": acceptance_counter["states"],
            "edges": len(frozen_edges),
            "weak_components": component_metrics["weak_components"],
            "disconnected_components": component_metrics["disconnected_components"],
        })
        accepted_components = _analyze_code_only_pass(
            instructions,
            owner_list,
            acceptance_lines,
            selected_names,
            memory,
            max_steps_per_seed,
            False,
            owner_map,
            island_list,
            profile_by_owner,
            frozen_edges=frozen_edges,
            new_edge_sink=new_edges,
            phase="acceptance",
            discovery_iteration=restart_count,
            progress_callback=emit_progress,
            progress_counter=acceptance_counter,
            # Component acceptance has its own state map. Let a decoded-only
            # lane reach an entry-evaluated join so it can contribute proven
            # may-call targets; merge_code_analyses keeps the entry lane intact.
            known_null_addresses=known_null_addresses,
            island_origins=normalized_island_origins,
            selected_owner_identities=selected_owner_identities,
        )
        accepted = merge_code_analyses(accepted_entry, accepted_components)
        missing_edges = new_edges - frozen_edges
        emit_progress({
            "event": "phase_complete",
            "phase": "acceptance",
            "iteration": restart_count,
            "seeds": acceptance_seed_count + int(include_owner_entry) * sum(
                1 for owner in owner_list if owner_is_selected(owner)
            ),
            "states": acceptance_counter["states"],
            "edges": len(frozen_edges),
            "new_edges": len(missing_edges),
            "phase_elapsed_ms": int((monotonic() - phase_started) * 1000),
        })
        if not missing_edges:
            return accepted
        if restart_count >= max_discovery_restarts:
            raise ValueError("code-only discovery restart cap exceeded")
        learned_edges.update(missing_edges)
        emit_progress({
            "event": "rediscovery_restart",
            "phase": "rediscovery",
            "iteration": restart_count + 1,
            "seeds": len(missing_edges),
            "states": 0,
            "edges": len(learned_edges),
            "owners": sorted({
                owner_display_by_key.get(name, name)
                for name, _source, _target in missing_edges
            }),
        })
        if profile_by_owner is not None:
            for owner_name in sorted({
                owner_display_by_key.get(name, name)
                for name, _source, _target in missing_edges
            }):
                profile_by_owner.setdefault(owner_name, Counter())[
                    "phase2_discovery_restarts"
                ] += 1

    raise AssertionError("unreachable discovery restart loop")


FUNCTION_RE = re.compile(r"^\s*([0-9A-Fa-f]+)\s+<([^>]+)>:$")
INSTRUCTION_RE = re.compile(r"^\s*([0-9A-Fa-f]+):\s+(?:[0-9A-Fa-f]{2}\s+){1,4}(.+)$")
LITERAL_LOAD_RE = re.compile(
    r"\bmov\.l\s+[^\n]*,r(\d+)\s*!\s*((?:0x)?[0-9A-Fa-f]+)\s+<([^>]+)>"
)
PC_LITERAL_DATA_RE = re.compile(
    r"\bmov\.(l|w)\s+((?:0x)?[0-9A-Fa-f]+)\s+<[^>]+>,"
    r"r(?:1[0-5]|\d)\b"
)
JSR_RE = re.compile(r"\bjsr\s+@r(\d+)\b")
JMP_RE = re.compile(r"\bjmp\s+@r(\d+)\b")
BSR_RE = re.compile(r"\bbsr\s+((?:0x)?[0-9A-Fa-f]+)\s+<([^>]+)>")
BSR_OPCODE_RE = re.compile(r"\bbsr\b")
DESTINATION_RE = re.compile(r",r(\d+)\s*(?:!.*)?$")
STACK_STORE_RE = re.compile(r"\bmov\.l\s+r(\d+),@\((\d+),r15\)")
STACK_LOAD_RE = re.compile(r"\bmov\.l\s+@\((\d+),r15\),r(\d+)")
AUTO_UPDATE_REGISTER_RE = re.compile(
    r"@(?:-(r(?:1[0-5]|\d))|(r(?:1[0-5]|\d))\+)"
)
BOUNDED_SINGLE_REGISTER_WRITERS = frozenset({
    "dt", "movt", "rotcl", "rotcr", "rotl", "rotr", "shal", "shar",
    "shll", "shll2", "shll8", "shll16", "shlr", "shlr2", "shlr8",
    "shlr16",
})

# Pinned digests deliberately make the route and helper ceilings append-only
# contracts. Updating either requires an explicit v2 implementation change,
# not a quiet edit to a text allowlist.
ROUTE_ORACLE_V1_SHA256 = "a9cfea12e749495ec13e31d9c3b732691215acde99a94abe656b82c7c8d69c72"
BASELINE_V1_SHA256 = "dfe6e5f494ad3ec103ce0024e5038174c9c18bf8ae42c2d65365cdc2c2fcf57a"
SIM_ROUTE_ORACLE_V1_SHA256 = "084313eeeb16ace7a05b252a0519bfbc86cc2f43db1260292d1db77da388af44"
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


def scan_direct_calls(
    disassembly: str,
    owners: Iterable[FunctionOwner] | None = None,
    local_islands: Iterable[LocalIsland] = (),
    proven_source_addresses: frozenset[int] | None = None,
    deferred_errors: list[tuple[str, int, str]] | None = None,
    qualify_owner_identities: bool = False,
    known_literal_pool_addresses: frozenset[int] = frozenset(),
    proven_literal_targets: dict[int, tuple[str, int]] | None = None,
) -> list[CallSite]:
    """Return literal-pool jsr and PC-relative bsr calls with linked targets.

    The optional provenance gate is used only by bounded preparsing. It keeps
    raw data halfwords inside broad function-symbol ranges from fabricating
    edges, while deferring selected-caller diagnostics until closure is known.
    """
    owner_list = None if owners is None else tuple(owners)
    island_list = tuple(local_islands) if owner_list is not None else ()
    owner_by_symbol: defaultdict[str, set[FunctionOwner]] = defaultdict(set)
    for owner in owner_list or ():
        for name in (owner.name, *owner.aliases):
            owner_by_symbol[name].add(owner)
    active_region: tuple[str, str, int] | None = None
    caller = "<outside-function>"
    caller_display = caller
    registers: dict[str, tuple[str, int]] = {}
    stack_slots: dict[int, tuple[str, int]] = {}
    unproven_registers: dict[str, tuple[str, int]] = {}
    unproven_stack_slots: dict[int, tuple[str, int]] = {}
    calls: list[CallSite] = []

    def source_is_proven(address: int) -> bool:
        return (
            proven_source_addresses is None
            or address in proven_source_addresses
        )

    def reject_or_defer(address: int, message: str) -> None:
        if deferred_errors is None:
            raise ValueError(message)
        deferred_errors.append((caller, address, message))

    def bounded_target(
        source_address: int, displayed: str, target_address: int,
    ) -> str | None:
        if not source_is_proven(source_address):
            if source_address not in known_literal_pool_addresses:
                assert active_region is not None
                reject_or_defer(
                    source_address,
                    "bounded direct-call source has no decoded code provenance: "
                    f"{caller_display}+0x{source_address - active_region[2]:x}",
                )
            return None
        try:
            return executable_target(displayed, target_address)
        except ValueError as error:
            reject_or_defer(source_address, str(error))
            return None

    def code_region(address: int) -> tuple[str, str, int] | None:
        assert owner_list is not None
        address_owner = _owner_at(owner_list, address)
        if address_owner is not None:
            identity = (
                _bounded_owner_identity(address_owner)
                if qualify_owner_identities
                else address_owner.name
            )
            return identity, address_owner.name, address_owner.start
        island = _island_at_code_address(island_list, address)
        return (
            None
            if island is None
            else (
                _local_island_identity(island),
                island.name,
                island.start,
            )
        )

    def executable_target(displayed: str, address: int) -> str:
        assert owner_list is not None
        symbol = _symbol_base(displayed)
        address_owner = _owner_at(owner_list, address)
        named_owners = owner_by_symbol.get(symbol, set())
        if address_owner is not None:
            if named_owners and address_owner not in named_owners:
                raise ValueError(
                    "bounded executable direct call has no linked owner: "
                    f"{symbol} at 0x{address:08x}"
                )
            return (
                _bounded_owner_identity(address_owner)
                if qualify_owner_identities
                else address_owner.name
            )
        if named_owners:
            raise ValueError(
                "bounded executable direct call has no linked owner: "
                f"{symbol} at 0x{address:08x}"
            )
        island = _island_for_direct_target(
            address, f"<{displayed}>", island_list
        )
        if island is not None:
            return _local_island_identity(island)
        raise ValueError(
            "bounded executable direct call has no linked owner: "
            f"{symbol} at 0x{address:08x}"
        )

    for line in disassembly.splitlines():
        function = FUNCTION_RE.match(line)
        if function:
            next_region = (
                None
                if owner_list is None
                else code_region(int(function.group(1), 16))
            )
            caller = function.group(2)
            caller_display = caller
            if owner_list is None or next_region != active_region:
                registers.clear()
                stack_slots.clear()
                unproven_registers.clear()
                unproven_stack_slots.clear()
            active_region = next_region
            continue

        instruction = INSTRUCTION_RE.match(line)
        if instruction is None:
            continue
        address = int(instruction.group(1), 16)
        text = instruction.group(2).strip()

        if owner_list is not None:
            source_region = code_region(address)
            if source_region is None:
                registers.clear()
                stack_slots.clear()
                unproven_registers.clear()
                unproven_stack_slots.clear()
                active_region = None
                continue
            if source_region != active_region:
                registers.clear()
                stack_slots.clear()
                unproven_registers.clear()
                unproven_stack_slots.clear()
            active_region = source_region
            caller = source_region[0]
            caller_display = source_region[1]

        row_is_proven = source_is_proven(address)
        if proven_source_addresses is not None and not row_is_proven:
            row_registers = unproven_registers
            row_stack_slots = unproven_stack_slots
        else:
            unproven_registers.clear()
            unproven_stack_slots.clear()
            row_registers = registers
            row_stack_slots = stack_slots

        load = LITERAL_LOAD_RE.search(text)
        if load:
            row_registers[load.group(1)] = (
                load.group(3), int(load.group(2), 16)
            )
            continue

        # GCC spills a literal-pool target around a call in some large
        # functions. Preserve only fixed r15-relative slots; they describe the
        # current function frame and are safe to invalidate if r15 changes.
        stack_store = STACK_STORE_RE.search(text)
        if stack_store:
            slot = int(stack_store.group(2), 10)
            target = row_registers.get(stack_store.group(1))
            if target is None:
                row_stack_slots.pop(slot, None)
            else:
                row_stack_slots[slot] = target
            continue

        stack_load = STACK_LOAD_RE.search(text)
        if stack_load:
            slot = int(stack_load.group(1), 10)
            target_register = stack_load.group(2)
            target = row_stack_slots.get(slot)
            if target is None:
                row_registers.pop(target_register, None)
            else:
                row_registers[target_register] = target
            continue

        jsr = JSR_RE.search(text)
        jmp = JMP_RE.search(text) if proven_literal_targets is not None else None
        indirect = jsr or jmp
        if indirect:
            target = (
                proven_literal_targets.get(address)
                if owner_list is not None and proven_literal_targets is not None
                else row_registers.get(indirect.group(1))
            )
            if target is not None:
                displayed, target_address = target
                symbol = _symbol_base(displayed)
                if owner_list is not None:
                    symbol = bounded_target(address, displayed, target_address)
                if symbol is not None:
                    calls.append(CallSite(caller, address, symbol))
            continue

        bsr = BSR_RE.search(text)
        if bsr:
            target_address = int(bsr.group(1), 16)
            displayed = bsr.group(2)
            symbol = _symbol_base(displayed)
            if owner_list is not None:
                symbol = bounded_target(address, displayed, target_address)
            if symbol is not None:
                calls.append(CallSite(caller, address, symbol))
            continue
        if owner_list is not None and BSR_OPCODE_RE.search(text):
            assert active_region is not None
            if not source_is_proven(address):
                if address not in known_literal_pool_addresses:
                    reject_or_defer(
                        address,
                        "bounded direct-call source has no decoded code provenance: "
                        f"{caller_display}+0x{address - active_region[2]:x}",
                    )
                continue
            raw_target = _target_from_text(text)
            reject_or_defer(
                address,
                "bounded executable direct call has unresolved direct call target: "
                f"{caller_display}+0x{address - active_region[2]:x}",
            )
            continue

        destination = DESTINATION_RE.search(text)
        if destination:
            destination_register = destination.group(1)
            row_registers.pop(destination_register, None)
            if destination_register == "15":
                row_stack_slots.clear()
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


def analysis_call_graph(
    calls: Iterable[CallSite], *, owner_identities: bool = False,
) -> dict[str, set[str]]:
    """Build a post-analysis graph without discarding bounded owner identity."""
    graph: dict[str, set[str]] = defaultdict(set)
    for call in calls:
        if is_native_math_helper(call.helper):
            continue
        if owner_identities:
            if call.caller_identity is None or call.helper_identity is None:
                raise ValueError(
                    "bounded analyzed call is missing stable owner identity: "
                    f"{call.caller} -> {call.helper} at 0x{call.address:08x}"
                )
            caller = call.caller_identity
            helper = call.helper_identity
        else:
            caller = call.caller
            helper = call.helper
        graph[caller].add(helper)
    return graph


def parse_route_oracle(text: str) -> RouteOracle:
    """Parse a versioned, checked-in replay-route root fixture."""
    version: int | None = None
    roots: set[str] = set()
    static_manifest_edges: set[tuple[str, str]] = set()
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
        elif parts[0] == "STATIC_MANIFEST_EDGE" and len(parts) == 3:
            edge = (parts[1], parts[2])
            if edge in static_manifest_edges:
                raise ValueError(
                    f"route oracle line {line_number}: duplicate STATIC_MANIFEST_EDGE "
                    f"{parts[1]} {parts[2]}"
                )
            static_manifest_edges.add(edge)
        elif parts[0] == "INDIRECT_EDGE" and len(parts) == 3:
            edge = (parts[1], parts[2])
            if edge in indirect_edges:
                raise ValueError(
                    f"route oracle line {line_number}: duplicate INDIRECT_EDGE "
                    f"{parts[1]} {parts[2]}"
                )
            indirect_edges.add(edge)
        else:
            raise ValueError(
                f"route oracle line {line_number}: expected ROOT <linked-symbol> or "
                "STATIC_MANIFEST_EDGE <dispatch-symbol> <callback-symbol> or "
                "INDIRECT_EDGE <dispatch-symbol> <callback-symbol>"
            )
    if version != 1:
        raise ValueError(f"unsupported route oracle version {version!r}")
    if not roots:
        raise ValueError("route oracle has no ROOT")
    return RouteOracle(
        version,
        frozenset(roots),
        frozenset(static_manifest_edges),
        frozenset(indirect_edges),
    )


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


def is_structurally_dynamic_callback_transfer(
    transfer: UnresolvedTransfer,
) -> bool:
    """Return whether a parser-classified callback transfer is dynamic."""
    return transfer.provenance == "dynamic"


def audit_indirect_edges(
    graph: dict[str, set[str]],
    oracle: RouteOracle,
    owners: tuple[FunctionOwner, ...],
    unresolved_transfers: Iterable[UnresolvedTransfer],
) -> IndirectEdgeAudit:
    """Validate and apply dispatcher-granular indirect callback declarations."""
    edges = set(oracle.indirect_edges)
    transfers = tuple(unresolved_transfers)
    closure = route_reachable_functions(graph, oracle.roots, edges)
    identity_mode = any(
        item.startswith("@owner:")
        for item in [*oracle.roots, *graph.keys()]
    )
    display_by_identity = {
        _bounded_owner_identity(owner): owner.name for owner in owners
    }

    def display(owner_key: str) -> str:
        return display_by_identity.get(owner_key, owner_key)

    def transfer_owner_key(transfer: UnresolvedTransfer) -> str:
        if not identity_mode:
            return transfer.caller
        if transfer.caller_identity is None:
            raise ValueError(
                "bounded unresolved transfer is missing stable owner identity: "
                f"{transfer.caller} at 0x{transfer.address:08x}"
            )
        return transfer.caller_identity

    owner_names = {
        name
        for owner in owners
        for name in (
            owner.name,
            *owner.aliases,
            _bounded_owner_identity(owner),
        )
    }
    dynamic_transfers = tuple(
        item
        for item in transfers
        if is_structurally_dynamic_callback_transfer(item)
    )
    dispatchers_with_dynamic_transfers = {
        transfer_owner_key(item) for item in dynamic_transfers
    }

    for dispatcher, callback in sorted(edges):
        if oracle.static_manifest_edges and (
            dispatcher, callback
        ) not in oracle.static_manifest_edges:
            raise ValueError(
                "stale INDIRECT_EDGE target is absent from the static route manifest: "
                f"{display(dispatcher)} -> {display(callback)}"
            )
        if dispatcher not in closure:
            raise ValueError(
                "INDIRECT_EDGE has unreachable dispatcher: "
                f"{display(dispatcher)} -> {display(callback)}"
            )
        if callback not in owner_names:
            raise ValueError(
                "INDIRECT_EDGE has missing callback owner: "
                f"{display(dispatcher)} -> {display(callback)}"
            )
        if dispatcher not in dispatchers_with_dynamic_transfers:
            raise ValueError(
                "unconsumed INDIRECT_EDGE has no structurally dynamic unresolved "
                f"transfer in {display(dispatcher)}: "
                f"{display(dispatcher)} -> {display(callback)}"
            )
        closure_without_edge = route_reachable_functions(
            graph, oracle.roots, edges - {(dispatcher, callback)}
        )
        if callback in closure_without_edge:
            # A serial recovery may make a real dynamic callback redundant in
            # the graph. Only an independently source-derived exact manifest
            # edge distinguishes that case from a stale declaration.
            source_manifest_confirms_edge = (
                dispatcher, callback
            ) in oracle.static_manifest_edges
            if not source_manifest_confirms_edge:
                raise ValueError(
                    "INDIRECT_EDGE has no closure contribution: "
                    f"{display(dispatcher)} -> {display(callback)}"
                )

    declared_dispatchers = {dispatcher for dispatcher, _ in edges}
    unlisted = tuple(
        item
        for item in transfers
        if transfer_owner_key(item) in closure
        and (
            transfer_owner_key(item) not in declared_dispatchers
            or item not in dynamic_transfers
        )
    )
    return IndirectEdgeAudit(frozenset(closure), unlisted)


def _call_owner_key(call: CallSite) -> str:
    return call.caller_identity or call.caller


def _route_call_counts(
    calls: Iterable[CallSite], route_functions: set[str],
) -> Counter[tuple[str, str, str]]:
    return Counter(
        (_call_owner_key(call), call.caller, call.helper)
        for call in calls
        if _call_owner_key(call) in route_functions
    )


def baseline_failures(calls: Iterable[CallSite], route_functions: set[str], baseline: BaselineContract) -> list[str]:
    """Enforce immutable maximums for every math call in the derived HOT route."""
    observed = _route_call_counts(calls, route_functions)
    failures: list[str] = []
    for (_owner_key, caller, helper), actual in sorted(observed.items()):
        expected = baseline.entries.get((caller, helper))
        if expected is None:
            failures.append(
                "unallowlisted helper in HOT function: "
                f"{caller} {helper} found {actual}"
            )
        elif actual > expected:
            failures.append(
                f"HOT baseline exceeded: {caller} {helper} "
                f"ceiling {expected}, found {actual}"
            )
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
    observed = _route_call_counts(calls, route_functions)
    actual_total = sum(observed.values())
    if actual_total != contract.expected_total:
        failures.append(
            "audit total differs from fixed post-conversion baseline "
            f"{contract.expected_total}, found {actual_total}"
        )
    for (_owner_key, caller, helper), actual in sorted(observed.items()):
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
    resolved_tool, environment, startup_directory = sh_tool_launch(addr2line)
    resolved_elf = elf.resolve()
    for batch in address_batches(addresses):
        command = [
            resolved_tool, "-e", str(resolved_elf), "-f", "-C",
            *[f"0x{address:X}" for address in batch],
        ]
        result = subprocess.run(
            command,
            check=True,
            capture_output=True,
            text=True,
            env=environment,
            cwd=startup_directory,
        )
        lines = result.stdout.splitlines()
        for index, address in enumerate(batch):
            source_index = index * 2 + 1
            locations[address] = lines[source_index] if source_index < len(lines) else "??:0"
    return locations


MSYS_RUNTIME_DLLS = ("msys-2.0.dll", "msys-gcc_s-seh-1.dll")


def _resolved_executable(executable: str | Path) -> Path:
    path = Path(executable)
    if path.is_absolute():
        return path
    found = shutil.which(str(executable))
    return Path(found) if found is not None else path


def _msys_runtime_directory(executable: str | Path) -> Path | None:
    tool = _resolved_executable(executable)
    if not tool.is_file() or b"msys-2.0.dll" not in tool.read_bytes().lower():
        return None
    candidates = [
        tool.parent,
        Path(r"C:\msys64\usr\bin"),
        *(Path(item) for item in os.environ.get("PATH", "").split(os.pathsep) if item),
    ]
    searched: list[Path] = []
    for candidate in candidates:
        if candidate in searched:
            continue
        searched.append(candidate)
        if all((candidate / dependency).is_file() for dependency in MSYS_RUNTIME_DLLS):
            return candidate
    required = ", ".join(MSYS_RUNTIME_DLLS)
    locations = ", ".join(str(path) for path in searched)
    raise ValueError(
        f"MSYS runtime for {tool} is incomplete; required DLLs not found "
        f"together: {required}; searched: {locations}"
    )


def sh_tool_launch(
    executable: str | Path,
) -> tuple[str, dict[str, str] | None, Path | None]:
    tool = _resolved_executable(executable)
    runtime = _msys_runtime_directory(executable)
    if runtime is None:
        return str(executable), None, None
    environment = os.environ.copy()
    environment["PATH"] = str(runtime) + os.pathsep + environment.get("PATH", "")
    return str(tool), environment, runtime


def sh_tool_environment(executable: str | Path) -> dict[str, str] | None:
    return sh_tool_launch(executable)[1]


def run_command(command: list[str]) -> str:
    executable, environment, startup_directory = sh_tool_launch(command[0])
    return subprocess.run(
        [executable, *command[1:]], check=True, capture_output=True, text=True,
        env=environment, cwd=startup_directory,
    ).stdout


def file_digest(path: Path) -> str:
    return sha256(path.read_bytes()).hexdigest()


def abstract_value_json(value: AbstractValue | None) -> dict[str, Any]:
    """Return a bounded, deterministic diagnostic representation."""
    if value is None:
        return {"kind": "NOT_APPLICABLE"}
    if value is UNKNOWN:
        return {"kind": "UNKNOWN"}
    if isinstance(value, MaybeStackPtr):
        return {"kind": "MaybeStackPtr"}
    if isinstance(value, StackPtr):
        return {"kind": "StackPtr", "offset": value.offset}
    if isinstance(value, StackPtrRange):
        return {"kind": "StackPtrRange", "lo": value.lo, "hi": value.hi}
    if isinstance(value, Interval):
        return {
            "kind": "Interval", "value_kind": value.kind,
            "lo": value.lo, "hi": value.hi,
        }
    if isinstance(value, ComparisonPredicate):
        return {
            "kind": "ComparisonPredicate",
            "register": value.register,
            "true_range": list(value.true_range),
            "false_range": list(value.false_range),
        }
    values = sorted(
        value.values,
        key=lambda item: (
            0, int(item), "" if isinstance(item, int) else ""
        ) if isinstance(item, int) else (1, item.address, item.name),
    )
    rows = [
        item if isinstance(item, int) else {"name": item.name, "address": item.address}
        for item in values[:16]
    ]
    return {
        "kind": "ConstSet", "value_kind": value.kind, "values": rows,
        "values_truncated": len(values) > 16,
    }


def _legacy_observation_facts(
    calls: list[CallSite], closure: set[str], owners: tuple[FunctionOwner, ...]
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    owner_by_address = build_owner_address_map(owners)
    direct = Counter()
    helper = Counter()
    for call in calls:
        owner_key = _call_owner_key(call)
        if owner_key not in closure:
            continue
        owner = owner_by_address.get(call.address)
        caller_offset = call.address - owner.start if owner else 0
        row = (
            owner_key,
            call.helper_identity or call.helper,
            call.caller,
            caller_offset,
            call.helper,
            0,
        )
        direct[row] += 1
        if is_native_math_helper(call.helper):
            helper[row] += 1
    def rows(
        values: Counter[tuple[str, str, str, int, str, int]],
        callee_key: str,
    ) -> list[dict[str, Any]]:
        return [
            {
                "caller": caller, "caller_region": "owner", "caller_island": None,
                "caller_offset": caller_offset,
                callee_key: callee, f"{callee_key}_offset": callee_offset, "count": count,
            }
            for (
                _owner_key, _callee_owner_key, caller,
                caller_offset, callee, callee_offset,
            ), count in sorted(values.items())
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
                x.caller_identity or x.caller,
                x.callee_identity or x.callee,
                x.caller, x.caller_region, x.caller_island, x.caller_offset,
                x.callee, x.callee_offset
            )
            for x in direct_facts
            if (x.caller_identity or x.caller) in closure
        )
        helper_counter = Counter(
            (
                x.caller_identity or x.caller,
                x.callee_identity or x.callee,
                x.caller, x.caller_region, x.caller_island, x.caller_offset,
                x.callee, x.callee_offset
            )
            for x in direct_facts
            if (x.caller_identity or x.caller) in closure
            and is_native_math_helper(x.callee)
        )
        direct_rows = [
            {
                "caller": a, "caller_region": b, "caller_island": c,
                "caller_offset": d, "callee": e, "callee_offset": f, "count": n,
            }
            for (_caller_key, _callee_key, a, b, c, d, e, f), n in sorted(
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
            for (_caller_key, _helper_key, a, b, c, d, e, f), n in sorted(
                helper_counter.items(), key=lambda item: tuple(
                    "" if value is None else value for value in item[0]
                )
            )
        ]
    implementation_counter = Counter(
        (
            x.caller_identity or x.caller,
            x.caller, x.caller_region, x.caller_island, x.caller_offset,
            x.target_island, x.target_offset
        )
        for x in (implementation_facts or [])
        if (x.caller_identity or x.caller) in closure
    )
    implementation_rows = [
        {
            "caller": a, "caller_region": b, "caller_island": c,
            "caller_offset": d, "target_island": e, "target_offset": f, "count": n,
        }
        for (_caller_key, a, b, c, d, e, f), n in sorted(
            implementation_counter.items(), key=lambda item: tuple(
                "" if value is None else value for value in item[0]
            )
        )
    ]
    owner_by_name = {owner.name: owner for owner in owners}
    owner_by_identity = {
        _bounded_owner_identity(owner): owner for owner in owners
    }
    owner_by_address = build_owner_address_map(owners)

    def site(
        caller: str,
        caller_identity: str | None,
        address: int,
    ) -> tuple[str, str | None, int]:
        island = next(
            (item for item in islands if item.start <= address < item.end), None
        )
        if island is not None:
            return "island", island.name, address - island.start
        owner = (
            owner_by_identity.get(caller_identity)
            if caller_identity is not None
            else owner_by_address.get(address) or owner_by_name.get(caller)
        )
        if owner is None:
            raise ValueError(
                f"observation site has no linked owner: {caller} at 0x{address:08x}"
            )
        return "owner", None, address - owner.start

    unresolved_rows = [
        ({
            "caller": x.caller,
            "caller_region": site(x.caller, x.caller_identity, x.address)[0],
            "caller_island": site(x.caller, x.caller_identity, x.address)[1],
            "caller_offset": site(x.caller, x.caller_identity, x.address)[2],
            "instruction_address": x.address,
            "mnemonic": x.mnemonic,
            "operand_register": x.operand_register,
            "final_abstract_value": abstract_value_json(x.final_value),
            "contributing_seeds": [
                {"address": address, "kind": kind}
                for address, kind in x.contributing_seeds
            ],
            "contributing_seeds_truncated": x.contributing_seeds_truncated,
            "predecessor_addresses": list(x.predecessor_addresses),
            "predecessor_addresses_truncated": x.predecessor_addresses_truncated,
            "state_changed_at_entry_covered_join": (
                x.state_changed_at_entry_covered_join
            ),
            "provenance": x.provenance,
        } | ({
            "stack_source_offsets": list(x.stack_source_offsets),
            "stack_store_addresses": list(x.stack_store_addresses),
        } if x.stack_source_offsets or x.stack_store_addresses else {}))
        for x in unresolved_transfers
        if (x.caller_identity or x.caller) in closure
    ]
    effect_rows = [
        {
            "caller": x.function,
            "caller_region": site(
                x.function, x.function_identity, x.address
            )[0],
            "caller_island": site(
                x.function, x.function_identity, x.address
            )[1],
            "caller_offset": site(
                x.function, x.function_identity, x.address
            )[2],
            "mnemonic": x.mnemonic, "operands": x.operands, "reason": x.reason,
        }
        for x in unresolved_effects
        if (x.function_identity or x.function) in closure
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
        "closure_functions": sorted(
            owner_by_identity.get(owner_key).name
            if owner_key in owner_by_identity else owner_key
            for owner_key in closure
        ),
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


@dataclass
class RouteBoundedCode:
    """Linked-ELF inputs materialized only for pinned route code regions."""

    graph: dict[str, set[str]]
    closure: frozenset[str]
    selected_identities: frozenset[str]
    selected_names: frozenset[str]
    selected_islands: tuple[LocalIsland, ...]
    island_origins: dict[str, frozenset[str]]
    disassembly: str
    instructions: dict[int, Instruction]
    decoded_lines: dict[str, set[int]]


def _validated_bounded_local_islands(
    islands: Iterable[LocalIsland], owners: tuple[FunctionOwner, ...],
) -> tuple[LocalIsland, ...]:
    """Reject externally fabricated or ambiguous bounded island ranges."""
    result = tuple(sorted(
        islands, key=lambda item: (item.section, item.start, item.end, item.name)
    ))
    identities: set[str] = set()
    for island in result:
        identity = _local_island_identity(island)
        if identity in identities:
            raise ValueError(f"duplicate bounded local island: {identity}")
        identities.add(identity)
        if not island.start <= island.code_start < island.end:
            raise ValueError(f"invalid bounded local island range: {identity}")
        if any(
            owner.section == island.section
            and owner.start < island.end and island.code_start < owner.end
            for owner in owners
        ):
            raise ValueError(f"bounded local island overlaps function owner: {identity}")
    for previous, current in zip(result, result[1:]):
        if previous.section == current.section \
                and previous.code_start < current.end \
                and current.code_start < previous.end:
            raise ValueError(
                "overlapping bounded local islands: "
                f"{_local_island_identity(previous)} and "
                f"{_local_island_identity(current)}"
            )
    return result


def _bounded_code_identity(
    owners: tuple[FunctionOwner, ...],
    islands: tuple[LocalIsland, ...],
    address: int,
) -> str | None:
    owner = _owner_at(owners, address)
    if owner is not None:
        return _bounded_owner_identity(owner)
    island = _island_at_code_address(islands, address)
    return None if island is None else _local_island_identity(island)


def _bounded_owner_identity(owner: FunctionOwner) -> str:
    """Return a stable bounded-mode identity for one canonical owner range."""
    return f"@owner:{owner.section}:{owner.start:08x}:{owner.name}"


def _route_oracle_owner_identities(
    oracle: RouteOracle,
    owners: Iterable[FunctionOwner],
) -> RouteOracle:
    """Canonicalize a validated bounded oracle to address-qualified owners."""
    identities_by_symbol: defaultdict[str, set[str]] = defaultdict(set)
    for owner in owners:
        identity = _bounded_owner_identity(owner)
        for symbol in (owner.name, *owner.aliases):
            identities_by_symbol[symbol].add(identity)

    def canonical(symbol: str) -> str:
        identities = identities_by_symbol.get(symbol, set())
        if not identities:
            raise ValueError(
                f"bounded route closure has no linked owner: {symbol}"
            )
        if len(identities) != 1:
            raise ValueError(
                f"bounded route closure has ambiguous linked owner: {symbol}"
            )
        return next(iter(identities))

    def canonical_edges(
        edges: Iterable[tuple[str, str]],
    ) -> frozenset[tuple[str, str]]:
        return frozenset(
            (canonical(dispatcher), canonical(callback))
            for dispatcher, callback in edges
        )

    return RouteOracle(
        oracle.version,
        frozenset(canonical(root) for root in oracle.roots),
        canonical_edges(oracle.static_manifest_edges),
        canonical_edges(oracle.indirect_edges),
    )


def _validated_bounded_owner_components(
    owners: tuple[FunctionOwner, ...],
) -> dict[str, frozenset[str]]:
    """Group laminar same-section function ranges into overlap components."""
    parent = {
        _bounded_owner_identity(owner): _bounded_owner_identity(owner)
        for owner in owners
    }
    if len(parent) != len(owners):
        raise ValueError("duplicate bounded function owner identity")

    def find(name: str) -> str:
        while parent[name] != name:
            parent[name] = parent[parent[name]]
            name = parent[name]
        return name

    def union(left: str, right: str) -> None:
        left_root = find(left)
        right_root = find(right)
        if left_root != right_root:
            parent[right_root] = left_root

    by_section: defaultdict[int, list[FunctionOwner]] = defaultdict(list)
    for owner in owners:
        if owner.end <= owner.start:
            raise ValueError(f"invalid bounded function range: {owner.name}")
        by_section[owner.section].append(owner)

    for members in by_section.values():
        active: list[FunctionOwner] = []
        for current in sorted(
            members, key=lambda owner: (owner.start, -owner.end, owner.name)
        ):
            active = [owner for owner in active if owner.end > current.start]
            for previous in active:
                if current.start == previous.start:
                    raise ValueError(
                        "same-start bounded function owners must be aliases: "
                        f"{previous.name} and {current.name}"
                    )
                if current.end > previous.end:
                    raise ValueError(
                        "partial function overlap: "
                        f"{previous.name} and {current.name}"
                    )
                union(
                    _bounded_owner_identity(previous),
                    _bounded_owner_identity(current),
                )
            active.append(current)

    members_by_root: defaultdict[str, set[str]] = defaultdict(set)
    for owner in owners:
        identity = _bounded_owner_identity(owner)
        members_by_root[find(identity)].add(identity)
    return {
        name: frozenset(members_by_root[find(name)])
        for name in parent
    }


def _bounded_disassembly_blocks(
    disassembly: str,
    owners: tuple[FunctionOwner, ...],
    islands: tuple[LocalIsland, ...],
) -> dict[str, str]:
    """Group decoded rows only by validated executable code-region address."""
    rows: dict[str, list[str]] = {}
    for line in disassembly.splitlines(keepends=True):
        stripped = line.rstrip("\r\n")
        header = FUNCTION_RE.match(stripped)
        instruction = INSTRUCTION_RE.match(stripped)
        match = header if header is not None else instruction
        if match is None:
            continue
        identity = _bounded_code_identity(
            owners, islands, int(match.group(1), 16)
        )
        if identity is not None:
            rows.setdefault(identity, []).append(line)
    return {name: "".join(block) for name, block in rows.items()}


def _selected_island_decoded_lines(
    text: str, islands: tuple[LocalIsland, ...],
) -> dict[str, set[int]]:
    result: dict[str, set[int]] = defaultdict(set)
    for line in text.splitlines():
        if "end_sequence" in line.lower():
            continue
        match = DECODED_LINE_RE.search(line)
        if match is None:
            continue
        address = int(match.group(1), 16)
        if address & 1:
            continue
        island = _island_at_code_address(islands, address)
        if island is not None:
            result[_local_island_identity(island)].add(address)
    return result


def _bounded_decoded_code_seeds(
    text: str,
    owners: tuple[FunctionOwner, ...],
    islands: tuple[LocalIsland, ...],
    owner_address_map: dict[int, FunctionOwner] | None = None,
) -> frozenset[int]:
    """Return aligned non-terminal DWARF roots in validated code regions."""
    owner_by_address = (
        owner_address_map
        if owner_address_map is not None
        else build_owner_address_map(owners)
    )
    result: set[int] = set()
    for line in text.splitlines():
        if "end_sequence" in line.lower():
            continue
        match = DECODED_LINE_RE.search(line)
        if match is None:
            continue
        address = int(match.group(1), 16)
        if not address & 1 and (
            address in owner_by_address
            or _island_at_code_address(islands, address) is not None
        ):
            result.add(address)
    return frozenset(result)


def _bounded_control_flow_sources(
    blocks: dict[str, str],
    owners: tuple[FunctionOwner, ...],
    islands: tuple[LocalIsland, ...],
    owner_components: dict[str, frozenset[str]],
    decoded_text: str,
    owner_address_map: dict[int, FunctionOwner] | None = None,
    exact_entry_seeds: frozenset[int] = frozenset(),
) -> tuple[
    frozenset[int],
    frozenset[int],
    list[tuple[str, int, str]],
    dict[int, tuple[str, int]],
]:
    """Prove code sources and trailing literal pools from bounded SH CFGs."""
    owner_by_identity = {
        _bounded_owner_identity(owner): owner for owner in owners
    }
    island_by_identity = {
        _local_island_identity(island): island for island in islands
    }
    decoded_seeds = _bounded_decoded_code_seeds(
        decoded_text, owners, islands, owner_address_map
    )
    proven: set[int] = set()
    literal_pool_candidates: defaultdict[frozenset[str], set[int]] = defaultdict(set)
    positive_literal_pool_addresses: set[int] = set()
    unsafe_pool_components: set[frozenset[str]] = set()
    errors: list[tuple[str, int, str]] = []

    rows_by_identity: dict[str, dict[int, tuple[str, str]]] = {}
    raw_operations_by_identity: dict[str, dict[int, str]] = {}
    literal_loads_by_identity: dict[str, dict[int, tuple[str, int]]] = {}
    literal_data_refs_by_identity: dict[str, dict[int, frozenset[int]]] = {}
    for identity, block in blocks.items():
        rows: dict[int, tuple[str, str]] = {}
        raw_operations: dict[int, str] = {}
        literal_loads: dict[int, tuple[str, int]] = {}
        literal_data_refs: dict[int, frozenset[int]] = {}
        for line in block.splitlines():
            match = INSTRUCTION_RE.match(line)
            if match is None:
                continue
            address = int(match.group(1), 16)
            raw_operation = match.group(2).strip()
            operation = raw_operation.split("!", 1)[0].strip()
            parts = operation.split(None, 1)
            mnemonic = parts[0]
            operands = "" if len(parts) == 1 else parts[1]
            rows[address] = (mnemonic.lower(), operands)
            raw_operations[address] = raw_operation
            literal = LITERAL_LOAD_RE.search(raw_operation)
            if literal is not None:
                literal_loads[address] = (
                    f"r{literal.group(1)}", int(literal.group(2), 16)
                )
            data_ref = PC_LITERAL_DATA_RE.search(raw_operation)
            if data_ref is not None:
                pool_address = int(data_ref.group(2), 16)
                width = 4 if data_ref.group(1) == "l" else 2
                literal_data_refs[address] = frozenset(
                    range(pool_address, pool_address + width, 2)
                )
        rows_by_identity[identity] = rows
        raw_operations_by_identity[identity] = raw_operations
        literal_loads_by_identity[identity] = literal_loads
        literal_data_refs_by_identity[identity] = literal_data_refs

    Target = tuple[str, int]
    FlowState = tuple[dict[str, Target], dict[int, Target]]

    def merge_state(old: FlowState | None, new: FlowState) -> tuple[FlowState, bool]:
        if old is None:
            return (dict(new[0]), dict(new[1])), True
        registers = {
            name: value for name, value in old[0].items()
            if new[0].get(name) == value
        }
        slots = {
            offset: value for offset, value in old[1].items()
            if new[1].get(offset) == value
        }
        merged = (registers, slots)
        return merged, merged != old

    def execute_literal_state(raw_operation: str, state: FlowState) -> FlowState:
        registers, slots = dict(state[0]), dict(state[1])
        load = LITERAL_LOAD_RE.search(raw_operation)
        if load is not None:
            destination = f"r{load.group(1)}"
            registers[destination] = (
                load.group(3), int(load.group(2), 16)
            )
            if destination == "r15":
                slots.clear()
            return registers, slots
        stack_store = STACK_STORE_RE.search(raw_operation)
        if stack_store is not None:
            offset = int(stack_store.group(2), 10)
            target = registers.get(f"r{stack_store.group(1)}")
            if target is None:
                slots.pop(offset, None)
            else:
                slots[offset] = target
            return registers, slots
        stack_load = STACK_LOAD_RE.search(raw_operation)
        if stack_load is not None:
            destination = f"r{stack_load.group(2)}"
            target = slots.get(int(stack_load.group(1), 10))
            if target is None:
                registers.pop(destination, None)
            else:
                registers[destination] = target
            if destination == "r15":
                slots.clear()
            return registers, slots
        if re.search(r"\bmov\.[bwl]\s+[^,]+,\s*@", raw_operation):
            slots.clear()
        operation = raw_operation.split("!", 1)[0].strip()
        operation_parts = operation.split(None, 1)
        mnemonic = operation_parts[0].lower() if operation_parts else ""
        operands = operation_parts[1].strip() if len(operation_parts) > 1 else ""
        for update in AUTO_UPDATE_REGISTER_RE.finditer(operands):
            register = update.group(1) or update.group(2)
            registers.pop(register, None)
            if register == "r15":
                slots.clear()
        single_operand = re.fullmatch(r"(r(?:1[0-5]|\d))", operands)
        if mnemonic in BOUNDED_SINGLE_REGISTER_WRITERS and single_operand:
            register = single_operand.group(1)
            registers.pop(register, None)
            if register == "r15":
                slots.clear()
        destination = DESTINATION_RE.search(raw_operation)
        if destination is not None:
            register = f"r{destination.group(1)}"
            registers.pop(register, None)
            if register == "r15":
                slots.clear()
        return registers, slots

    def after_call(state: FlowState) -> FlowState:
        # The SH C ABI preserves r8-r14. A fixed r15-relative spill therefore
        # remains valid across a call, while all caller-saved register facts
        # are discarded.
        return (
            {
                register: target for register, target in state[0].items()
                if register.startswith("r") and int(register[1:]) >= 8
            },
            dict(state[1]),
        )

    def target_is_code(address: int) -> bool:
        target_owner = _owner_at(owners, address)
        if target_owner is not None:
            return not is_native_math_helper(target_owner.name)
        return _island_at_code_address(islands, address) is not None

    propagated_seeds = set(exact_entry_seeds)
    while True:
        proven = set()
        literal_pool_candidates = defaultdict(set)
        positive_literal_pool_addresses = set()
        unsafe_pool_components = set()
        errors = []
        discovered_seeds: set[int] = set()
        proven_literal_targets: dict[int, Target] = {}
        ambiguous_literal_targets: set[int] = set()

        for identity, identity_rows in rows_by_identity.items():
            owner = owner_by_identity.get(identity)
            if owner is None:
                rows = identity_rows
                raw_operations = raw_operations_by_identity[identity]
                literal_data_refs = literal_data_refs_by_identity[identity]
            else:
                rows = {
                    address: row
                    for member in owner_components[identity]
                    for address, row in rows_by_identity.get(member, {}).items()
                }
                raw_operations = {
                    address: operation
                    for member in owner_components[identity]
                    for address, operation in raw_operations_by_identity.get(
                        member, {}
                    ).items()
                }
                literal_data_refs = {
                    address: targets
                    for member in owner_components[identity]
                    for address, targets in literal_data_refs_by_identity.get(
                        member, {}
                    ).items()
                }
            if not rows:
                continue

            island = island_by_identity.get(identity)
            if owner is not None:
                entry = owner.start
                region_base = owner.start
                region_display = owner.name
            elif island is not None:
                candidates = [
                    address for address in rows if address >= island.code_start
                ]
                if not candidates:
                    continue
                entry = min(candidates)
                region_base = island.start
                region_display = island.name
            else:
                continue
            component = (
                owner_components[identity]
                if owner is not None else frozenset({identity})
            )
            delayed_mnemonics = {
                "rts", "rte", "jmp", "braf", "bra", "bt.s", "bf.s",
                "bt/s", "bf/s", "bsr", "jsr", "bsrf",
            }
            delay_slots = {
                address + 2
                for address, (mnemonic, _operands) in rows.items()
                if mnemonic in delayed_mnemonics
            }
            seed_addresses = {
                *([entry] if entry in rows else []),
                *(propagated_seeds.intersection(rows) - delay_slots),
            }
            fallback_decoded_seeds = (
                decoded_seeds.intersection(rows) - delay_slots
            )
            states: dict[int, FlowState] = {}
            pending: deque[int] = deque()
            visited: set[int] = set()
            error_keys: set[tuple[int, str]] = set()

            def structural_error(address: int, detail: str) -> None:
                key = (address, detail)
                if key in error_keys:
                    return
                error_keys.add(key)
                errors.append((
                    identity,
                    address,
                    "bounded code provenance has unresolved direct control flow: "
                    f"{region_display}+0x{address - region_base:x}: {detail}",
                ))

            def schedule(
                source: int, address: int, edge: str, state: FlowState,
            ) -> None:
                if address not in rows:
                    structural_error(
                        source,
                        f"{edge} successor leaves bounded block at "
                        f"0x{address:08x}",
                    )
                    return
                merged, changed = merge_state(states.get(address), state)
                if changed:
                    states[address] = merged
                    pending.append(address)

            for seed in sorted(seed_addresses):
                merged, changed = merge_state(states.get(seed), ({}, {}))
                if changed:
                    states[seed] = merged
                    pending.append(seed)

            decoded_fallbacks_added = False
            while True:
                if not pending:
                    if decoded_fallbacks_added:
                        break
                    decoded_fallbacks_added = True
                    for seed in sorted(fallback_decoded_seeds - visited):
                        merged, changed = merge_state(
                            states.get(seed), ({}, {})
                        )
                        if changed:
                            states[seed] = merged
                            pending.append(seed)
                    if not pending:
                        break
                address = pending.popleft()
                state = states[address]
                visited.add(address)
                proven.add(address)
                mnemonic, operands = rows[address]
                target = _target_from_text(operands)
                operation_state = execute_literal_state(
                    raw_operations[address], state
                )

                if mnemonic in delayed_mnemonics:
                    slot = address + 2
                    if slot not in rows:
                        structural_error(
                            address,
                            f"delay slot leaves bounded block at 0x{slot:08x}",
                        )
                        continue
                    proven.add(slot)
                    slot_state = execute_literal_state(
                        raw_operations[slot], operation_state
                    )
                    states[slot] = merge_state(states.get(slot), operation_state)[0]
                    if mnemonic in {"rts", "rte", "jmp", "braf"}:
                        continue
                    if mnemonic == "bra":
                        if target is None:
                            structural_error(address, "unknown bra target")
                        else:
                            schedule(address, target, "bra", slot_state)
                        continue
                    if mnemonic in {"bt.s", "bf.s", "bt/s", "bf/s"}:
                        if target is None:
                            structural_error(
                                address, "unknown conditional branch target"
                            )
                        else:
                            schedule(
                                address, target, "conditional branch", slot_state
                            )
                        schedule(
                            address, address + 4, "fallthrough", slot_state
                        )
                        continue
                    if mnemonic == "bsr":
                        if target is not None and target_is_code(target):
                            discovered_seeds.add(target)
                        schedule(
                            address, address + 4, "fallthrough",
                            after_call(slot_state),
                        )
                        continue
                    if mnemonic in {"jsr", "bsrf"}:
                        schedule(
                            address, address + 4, "fallthrough",
                            after_call(slot_state),
                        )
                        continue

                if mnemonic in {"bt", "bf"}:
                    if target is None:
                        structural_error(
                            address, "unknown conditional branch target"
                        )
                    else:
                        schedule(
                            address, target, "conditional branch", operation_state
                        )
                    schedule(
                        address, address + 2, "fallthrough", operation_state
                    )
                    continue
                schedule(address, address + 2, "fallthrough", operation_state)

            region_literal_targets: dict[int, Target] = {}
            for address in sorted(visited):
                mnemonic, operands = rows[address]
                if mnemonic not in {"jmp", "jsr"}:
                    continue
                register = re.fullmatch(r"@(r(?:1[0-5]|\d))", operands)
                target = (
                    None if register is None
                    else states[address][0].get(register.group(1))
                )
                if target is None:
                    unsafe_pool_components.add(component)
                    continue
                region_literal_targets[address] = target
                if target_is_code(target[1]):
                    discovered_seeds.add(target[1])

            if any(
                rows[address][0] in {"braf", "bsrf"}
                for address in visited
            ):
                unsafe_pool_components.add(component)

            for address, target in region_literal_targets.items():
                previous = proven_literal_targets.get(address)
                if previous is not None and previous != target:
                    ambiguous_literal_targets.add(address)
                else:
                    proven_literal_targets[address] = target

            # A PC-relative load from reached code is positive ISA-level
            # evidence that its referenced halfwords are data.
            for source, targets in literal_data_refs.items():
                if source in proven:
                    positive_literal_pool_addresses.update(
                        target for target in targets if target in rows
                    )

            for terminal in visited:
                mnemonic, operands = rows[terminal]
                if mnemonic not in {"bra", "rts", "rte"}:
                    continue
                tail_start = terminal + 4
                target = (
                    _target_from_text(operands) if mnemonic == "bra" else None
                )
                if target is not None and target > tail_start:
                    suffix = {
                        address for address in rows
                        if tail_start <= address < target
                    }
                elif target is None or target <= terminal:
                    suffix = {
                        address for address in rows if address >= tail_start
                    }
                else:
                    suffix = set()
                if suffix and not suffix.intersection(visited):
                    literal_pool_candidates[component].update(suffix)

        for address in ambiguous_literal_targets:
            proven_literal_targets.pop(address, None)
        new_seeds = discovered_seeds - propagated_seeds
        if not new_seeds:
            break
        propagated_seeds.update(new_seeds)

    literal_pool_addresses = frozenset(
        positive_literal_pool_addresses.union(
            address
            for component, candidates in literal_pool_candidates.items()
            if component not in unsafe_pool_components
            for address in candidates
        )
    )
    return (
        frozenset(proven), literal_pool_addresses, errors,
        proven_literal_targets,
    )


def _bounded_island_origins(
    graph: dict[str, set[str]],
    selected_identities: set[str],
    selected_islands: tuple[LocalIsland, ...],
) -> dict[str, frozenset[str]]:
    """Map selected island code to each canonical function-owner origin."""
    island_identities = {
        _local_island_identity(island) for island in selected_islands
    }
    origins: defaultdict[str, set[str]] = defaultdict(set)
    for owner_identity in sorted(selected_identities):
        pending = [
            target for target in graph.get(owner_identity, ())
            if target in island_identities
        ]
        visited: set[str] = set()
        while pending:
            identity = pending.pop()
            if identity in visited:
                continue
            visited.add(identity)
            origins[identity].add(owner_identity)
            pending.extend(
                target for target in graph.get(identity, ())
                if target in island_identities and target not in visited
            )
    missing = island_identities - origins.keys()
    if missing:
        raise ValueError(
            "bounded island has no canonical function-owner origin: "
            + ", ".join(sorted(missing))
        )
    return {
        identity: frozenset(origin_names)
        for identity, origin_names in origins.items()
    }


def prepare_route_bounded_code_only(
    disassembly: str,
    decoded_text: str,
    owners: Iterable[FunctionOwner],
    oracles: Iterable[RouteOracle],
    owner_address_map: dict[int, FunctionOwner] | None = None,
    local_islands: Iterable[LocalIsland] = (),
) -> RouteBoundedCode:
    """Derive pinned closure before allocating any Instruction objects."""
    owner_list = tuple(owners)
    owner_components = _validated_bounded_owner_components(owner_list)
    island_list = _validated_bounded_local_islands(local_islands, owner_list)
    oracle_list = tuple(oracles)
    if not oracle_list:
        raise ValueError("bounded route analysis requires a route oracle")

    owner_identities_by_symbol: defaultdict[str, set[str]] = defaultdict(set)
    for owner in owner_list:
        identity = _bounded_owner_identity(owner)
        for name in (owner.name, *owner.aliases):
            owner_identities_by_symbol[name].add(identity)
    owner_by_identity = {
        _bounded_owner_identity(owner): owner
        for owner in owner_list
    }
    island_by_identity = {
        _local_island_identity(island): island for island in island_list
    }
    collisions = set(owner_by_identity).intersection(island_by_identity)
    if collisions:
        raise ValueError(
            "bounded island identity collides with function owner: "
            + ", ".join(sorted(collisions))
        )
    blocks = _bounded_disassembly_blocks(disassembly, owner_list, island_list)
    graph: dict[str, set[str]] = {
        name: set() for name in blocks
    }
    for component in set(owner_components.values()):
        if len(component) < 2:
            continue
        for name in component:
            graph.setdefault(name, set()).update(component - {name})
    (
        proven_source_addresses,
        literal_pool_addresses,
        deferred_errors,
        proven_literal_targets,
    ) = \
        _bounded_control_flow_sources(
        blocks,
        owner_list,
        island_list,
        owner_components,
        decoded_text,
        owner_address_map,
    )
    # Closure is not known until after this text scan. Preserve potential
    # structural and executable-call failures by caller, then enforce them
    # only for route-selected normal functions or their selected islands.
    for call in scan_direct_calls(
        disassembly,
        owner_list,
        island_list,
        proven_source_addresses,
        deferred_errors,
        qualify_owner_identities=True,
        known_literal_pool_addresses=literal_pool_addresses,
        proven_literal_targets=proven_literal_targets,
    ):
        target_owner = owner_by_identity.get(call.helper)
        target_display = (
            target_owner.name if target_owner is not None else call.helper
        )
        if is_native_math_helper(target_display):
            continue
        if call.helper == call.caller:
            continue
        graph.setdefault(call.caller, set()).add(call.helper)

    closure: set[str] = set()
    for oracle in oracle_list:
        canonical_roots: set[str] = set()
        for root in oracle.roots:
            identities = owner_identities_by_symbol.get(root, set())
            if not identities:
                raise ValueError(f"bounded analysis missing route root owner: {root}")
            if len(identities) != 1:
                raise ValueError(
                    f"bounded analysis ambiguous route root owner: {root}"
                )
            identity = next(iter(identities))
            if identity not in blocks or identity not in graph:
                raise ValueError(f"bounded analysis missing route root block: {root}")
            canonical_roots.add(identity)

        canonical_edges: set[tuple[str, str]] = set()
        for dispatcher, callback in oracle.indirect_edges:
            dispatcher_identities = owner_identities_by_symbol.get(
                dispatcher, set()
            )
            callback_identities = owner_identities_by_symbol.get(callback, set())
            if not dispatcher_identities:
                raise ValueError(
                    "bounded route closure has no linked owner: " + dispatcher
                )
            if len(dispatcher_identities) != 1:
                raise ValueError(
                    "bounded route closure has ambiguous dispatcher owner: "
                    + dispatcher
                )
            if not callback_identities:
                raise ValueError(
                    "bounded route closure has no linked owner: " + callback
                )
            if len(callback_identities) != 1:
                raise ValueError(
                    "bounded route closure has ambiguous callback owner: "
                    + callback
                )
            canonical_edges.add((
                next(iter(dispatcher_identities)),
                next(iter(callback_identities)),
            ))
        closure.update(route_reachable_functions(
            graph, canonical_roots, canonical_edges
        ))

    for caller, _address, message in sorted(deferred_errors):
        if caller in closure:
            raise ValueError(message)

    for identity in sorted(closure):
        owner = owner_by_identity.get(identity)
        island = island_by_identity.get(identity)
        display = (
            owner.name
            if owner is not None
            else island.name if island is not None else identity
        )
        if owner is None and island is None:
            raise ValueError(f"bounded route closure has no linked owner: {display}")
        if identity not in blocks:
            raise ValueError(f"bounded route closure has no owned block: {display}")

    selected_identities = {
        identity for identity in closure if identity in owner_by_identity
    }
    selected_names = {
        owner_by_identity[identity].name for identity in selected_identities
    }
    selected_islands = tuple(
        island for island in island_list
        if _local_island_identity(island) in closure
    )
    island_origins = _bounded_island_origins(
        graph, selected_identities, selected_islands
    )
    bounded_disassembly = "".join(
        block for name, block in blocks.items() if name in closure
    )
    # This is the key memory boundary: parsing happens only after complete
    # linked-symbol blocks have been selected from the raw disassembly text.
    instructions = parse_instructions(bounded_disassembly)
    decoded_lines = parse_decoded_lines(
        decoded_text,
        owner_list,
        owner_address_map,
        selected_owner_identities=selected_identities,
    )
    decoded_lines.update(_selected_island_decoded_lines(
        decoded_text, selected_islands
    ))
    return RouteBoundedCode(
        graph=graph,
        closure=frozenset(closure),
        selected_identities=frozenset(selected_identities),
        selected_names=frozenset(selected_names),
        selected_islands=selected_islands,
        island_origins=island_origins,
        disassembly=bounded_disassembly,
        instructions=instructions,
        decoded_lines=decoded_lines,
    )


def census_rows(calls: Iterable[CallSite], route_functions: set[str]) -> list[CensusRow]:
    grouped: dict[tuple[str, str], Counter[str]] = defaultdict(Counter)
    for call in calls:
        grouped[(_call_owner_key(call), call.caller)][call.helper] += 1
    return [
        CensusRow(
            "HOT" if owner_key in route_functions else "COLD",
            caller,
            sum(helpers.values()),
            tuple(sorted(helpers.items())),
            owner_key if owner_key != caller else None,
        )
        for (owner_key, caller), helpers in sorted(grouped.items())
    ]


def print_census(calls: Iterable[CallSite], route_functions: set[str], locations: dict[int, str]) -> None:
    call_list = list(calls)
    location_by_owner = {
        _call_owner_key(call): locations.get(call.address, "??:0")
        for call in call_list
    }
    rows = census_rows(call_list, route_functions)
    for row in rows:
        helpers = ", ".join(f"{helper}={count}" for helper, count in row.helpers)
        location = location_by_owner[row.owner_identity or row.caller]
        print(f"{row.heat:4} {row.count:4} {row.caller} [{helpers}] ({location})")
    hot_total = sum(row.count for row in rows if row.heat == "HOT")
    print(f"SH-2 native-math census: HOT total {hot_total}; COLD total {sum(row.count for row in rows) - hot_total}")


def print_audit(calls: Iterable[CallSite], route_functions: set[str], locations: dict[int, str]) -> None:
    """Print a pinned route audit without weakening the shipped HOT ceiling."""
    call_list = list(calls)
    location_by_owner = {
        _call_owner_key(call): locations.get(call.address, "??:0")
        for call in call_list
    }
    rows = [row for row in census_rows(call_list, route_functions) if row.heat == "HOT"]
    for row in rows:
        helpers = ", ".join(f"{helper}={count}" for helper, count in row.helpers)
        location = location_by_owner[row.owner_identity or row.caller]
        print(f"AUDIT {row.count:4} {row.caller} [{helpers}] ({location})")
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
    parser.add_argument(
        "--analysis-mode",
        choices=("legacy-linear", "code-only", "code-only-route-bounded"),
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
        if not args.audit_observation_only and args.analysis_mode not in {
            "code-only", "code-only-route-bounded",
        }:
            raise ValueError(
                "normal acceptance requires analysis-mode=code-only or "
                "code-only-route-bounded"
            )
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
        elf_path = args.elf.resolve()
        disassembly = run_command([args.objdump, "-d", str(elf_path)])
        sections_text = run_command([args.readelf, "-SW", str(elf_path)])
        symbols_text = run_command([args.readelf, "-sW", str(elf_path)])
        lines_text = run_command([
            args.readelf, "--debug-dump=decodedline", str(elf_path)
        ])
        sections = parse_readelf_sections(sections_text)
        symbols = parse_readelf_symbols(symbols_text, sections)
        owners = resolve_function_owners(symbols, sections)
        local_islands = resolve_local_islands(symbols, sections, owners)
        owner_address_map = build_owner_address_map(owners)
        analysis = None
        bounded_identity_mode = args.analysis_mode == "code-only-route-bounded"
        if args.analysis_mode == "legacy-linear":
            direct_calls = scan_direct_calls(disassembly)
            calls = [call for call in direct_calls if is_native_math_helper(call.helper)]
            graph = scan_call_graph(disassembly)
        else:
            island_seed_origins = None
            candidate_owner_identities = None
            if args.analysis_mode == "code-only-route-bounded":
                bounded = prepare_route_bounded_code_only(
                    disassembly,
                    lines_text,
                    owners,
                    tuple(
                        item for item in (oracle, audit_oracle)
                        if item is not None
                    ),
                    owner_address_map,
                    local_islands,
                )
                decoded_seeds = bounded.decoded_lines
                legacy_graph = bounded.graph
                candidate_names = None
                candidate_owner_identities = set(
                    bounded.selected_identities
                )
                local_islands = bounded.selected_islands
                island_seed_origins = bounded.island_origins
                parsed_instructions = bounded.instructions
            else:
                decoded_seeds = parse_decoded_lines(
                    lines_text, owners, owner_address_map
                )
                legacy_graph = scan_call_graph(disassembly)
                candidate_names = route_reachable_functions(
                    legacy_graph, oracle.roots, oracle.indirect_edges
                )
                if audit_oracle is not None:
                    candidate_names |= route_reachable_functions(
                        legacy_graph,
                        audit_oracle.roots,
                        audit_oracle.indirect_edges,
                    )
                parsed_instructions = parse_instructions(disassembly)
            instruction_memory = build_instruction_memory(parsed_instructions)
            known_null_addresses = prove_sourceboot_null_task_submit(
                parsed_instructions, owners
            )
            analysis = analyze_code_only(
                parsed_instructions,
                owners,
                decoded_seeds,
                selected_names=candidate_names,
                instruction_memory=instruction_memory,
                owner_address_map=owner_address_map,
                local_islands=local_islands,
                known_null_addresses=known_null_addresses,
                island_origins=island_seed_origins,
                selected_owner_identities=candidate_owner_identities,
            )
            direct_calls = analysis.calls
            calls = [call for call in direct_calls if is_native_math_helper(call.helper)]
            graph = analysis_call_graph(
                direct_calls, owner_identities=bounded_identity_mode
            )
        route_edge_result = None
        if args.analysis_mode == "code-only-route-bounded":
            assert analysis is not None
            route_graph_oracle = _route_oracle_owner_identities(oracle, owners)
            route_edge_result = audit_indirect_edges(
                graph, route_graph_oracle, owners, analysis.unresolved_transfers
            )
            route_functions = set(route_edge_result.closure)
        else:
            route_functions = route_reachable_functions(
                graph, oracle.roots, oracle.indirect_edges
            )
        audit_edge_result = None
        if audit_oracle is None:
            audit_functions = None
        elif analysis is None:
            if audit_oracle.indirect_edges:
                raise ValueError(
                    "INDIRECT_EDGE validation requires code-only analysis"
                )
            audit_functions = route_reachable_functions(graph, audit_oracle.roots)
        else:
            audit_graph_oracle = (
                _route_oracle_owner_identities(audit_oracle, owners)
                if bounded_identity_mode
                else audit_oracle
            )
            audit_edge_result = audit_indirect_edges(
                graph, audit_graph_oracle, owners, analysis.unresolved_transfers
            )
            audit_functions = set(audit_edge_result.closure)
        locations = {} if args.audit_observation_only else source_locations(
            args.addr2line, elf_path, calls
        )
        if not args.audit_observation_only:
            print_census(calls, route_functions, locations)
            if audit_functions is not None:
                print_audit(calls, audit_functions, locations)
        failures = baseline_failures(calls, route_functions, baseline)
        if route_edge_result is not None:
            for transfer in route_edge_result.unlisted_transfers:
                failures.append(
                    "route has unlisted unresolved indirect transfer: "
                    f"{transfer.caller} at 0x{transfer.address:x} "
                    f"{transfer.mnemonic}"
                )
        if audit_functions is not None and audit_oracle is not None and audit_contract is not None:
            if audit_contract.expected_root not in audit_oracle.roots:
                failures.append(f"audit root missing: expected {audit_contract.expected_root}")
            observed = _route_call_counts(calls, audit_functions)
            for (_owner_key, caller, helper), count in sorted(observed.items()):
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
                assert audit_edge_result is not None
                unresolved = list(audit_edge_result.unlisted_transfers)
                effects = [
                    x for x in analysis.unresolved_effects
                    if (x.function_identity or x.function) in audit_functions
                ]
                for transfer in unresolved:
                    owner = owner_address_map.get(transfer.address)
                    site = (
                        f"+{transfer.address - owner.start}"
                        if owner is not None else f"at 0x{transfer.address:x}"
                    )
                    failures.append(
                        "audit has unlisted unresolved indirect transfer: "
                        f"{transfer.caller} {site} {transfer.mnemonic}"
                    )
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
                    unresolved_transfers=(
                        [] if audit_edge_result is None
                        else list(audit_edge_result.unlisted_transfers)
                    ),
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
