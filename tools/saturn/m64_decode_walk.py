#!/usr/bin/env python3
"""Validation-only decode walk for m64 sequence-level scripts.

Ports the traversal structure -- not the timing/voice semantics -- of the
real 68k sequence VM, `src/port/saturn/audio68k/sequence_vm.c` (line ranges
verified against the current file, 2026-08-09):

  - vm_flow (:130-220): flow-control opcode set, absolute/relative target
    validation (vm_read_target :93-104 rejects raw big-endian targets that
    are negative as s16 or >= the sequence length), loop/call stack shape.
  - vm_tick_sequence (:222-386): the sequence-level dispatch tables this
    walker mirrors exactly -- including the US vs EU/SH format splits
    (:235-268: US 0xf2 reserve-notes + u8 / 0xf1 bare / 0xf0 invalid;
    EU/SH 0xf1 + u8 / 0xf0 bare / 0xf2-0xf4 relative branches; :240-242
    EU/SH rejects 0xda/0xdc) and the low-opcode family gates (:353-382).
  - vm_read_var_u16 (:77-91): the 1-or-2-byte compressed operand format.
  - vm_tick_layer (:388-502) is the layer-mode reference; layer scripts are
    only reachable through channel scripts, which the VM does not interpret
    (sequence-level 0x90 emits a CHANNEL_START event for a consumer), so
    this walker deliberately treats channel-script bodies -- and therefore
    layer scripts -- as opaque: 0x90-family targets are range-validated per
    vm_read_target but not decoded.  Inventing channel semantics from the
    N64 reference would exceed this validator's scope (Task 15 owns
    interpretation).

The walk is a static reachability analysis from offset 0: it follows both
outcomes of every conditional branch, follows calls and their return points,
deduplicates work with a visited set, and is backstopped by a bounded
instruction budget.  A reachable path may legally terminate three ways --
an 0xFF end opcode, a validated unconditional jump, or a merge into
already-decoded code (looping music never reaches 0xFF at the sequence
level: verified on 19 of the repo's 34 real US m64s, which all end in an
intentional 0xfb jump-back loop).  Falling off EOF, dying mid-opcode, or
decoding an opcode outside the VM's accepted set is a finding.

A loop terminator is only legal when the cycle it closes contains a delay
opcode (0xfd/0xfe): vm_tick_sequence executes at most
SM64_SATURN_SEQUENCE_VM_INSTRUCTION_BUDGET (64) instructions per tick and
returns false -- a fault -- when the budget exhausts without a delay
stopping the tick (:231 loop bound, :386 fall-out), so a reachable
delay-free control-flow cycle faults the VM on its first tick.  All 19
real looping sequences carry an 0xfd delay inside their loop.  The walk
therefore records the control-flow graph it decodes and reports a
`delay-free-loop` finding for any reachable cycle with no delay on it; a
call instruction counts as delay-bearing when its callee's reachable code
contains a delay, because the cycle through the call's return point
dynamically executes the callee body each iteration.

One deliberate strictness deviation from vm_flow: this walker rejects an
out-of-range EU/SH relative-branch displacement (0xf2/0xf3/0xf4)
unconditionally, while the real vm_flow (:198-218) checks the not-taken
condition *before* bounds-checking the displacement, so a never-taken
conditional relative branch carrying a bad displacement is dynamically
legal.  Static validation cannot evaluate the condition, and no real
sequence relies on the loophole (the US set never uses these opcodes), so
the walker fails such branches closed.

Dynamic-only properties are out of scope by design: call/loop stack depth
(SM64_SATURN_SEQUENCE_VM_STACK_DEPTH), loop iteration counts, event
capacity, and tempo/delay values are runtime state the static walk cannot
evaluate without interpreting -- Task 15's job, not this validator's.

Emits (ok, findings[]); it never mutates or produces artifacts, so it
cannot perturb packaging determinism.
"""
from __future__ import annotations

from dataclasses import dataclass

FORMAT_US = "us"
FORMAT_EU_SH = "eu_sh"
_FORMATS = (FORMAT_US, FORMAT_EU_SH)

FINDING_EMPTY = "empty-sequence"
FINDING_OVERSIZED = "oversized-sequence"
FINDING_RUNS_PAST_END = "runs-past-end"
FINDING_TRUNCATED = "truncated-mid-opcode"
FINDING_UNKNOWN_OPCODE = "unknown-opcode"
FINDING_TARGET_OUT_OF_RANGE = "target-out-of-range"
FINDING_TARGET_MID_INSTRUCTION = "target-mid-instruction"
FINDING_OVERLAP = "overlapping-decode"
FINDING_BUDGET = "work-budget-exhausted"
FINDING_DELAY_FREE_LOOP = "delay-free-loop"

# The VM addresses sequences with uint16_t pc/length (sequence_vm.h).
_MAX_SEQUENCE_LENGTH = 0xFFFF
# Backstop headroom over the visited-set bound (every offset decodes at most
# once, so len(payload) instructions is already the hard ceiling).
_BUDGET_HEADROOM = 64

# vm_tick_sequence's u8-operand command set (:269-273 dispatch, u8 read at
# :298).  0xda/0xdc are format-gated (EU/SH rejects them at :240-242).
_U8_OPCODES = frozenset({0xcc, 0xc8, 0xc9, 0xdb, 0xda, 0xdd, 0xdc, 0xde,
                         0xdf, 0xd3, 0xd5, 0xd0})
# be16-operand commands (:279-281).
_BE16_OPCODES = frozenset({0xd7, 0xd6, 0xd2, 0xd1})
# Absolute-target flow opcodes (vm_flow :150-164).
_ABS_TARGET_OPCODES = frozenset({0xfc, 0xfb, 0xfa, 0xf9, 0xf5})
# Low families vm_tick_sequence accepts (:353-376): 0x90 reads a channel
# target; the rest carry no operand.  0x30/0xb0 fail closed.
_LOW_FAMILIES_NO_OPERAND = frozenset({0x00, 0x10, 0x20, 0x40, 0x50, 0x60,
                                      0x70, 0x80, 0xa0})
# Delay opcodes stop a tick (vm_flow :185-198 sets *stopped); a cycle with
# neither exhausts vm_tick_sequence's 64-instruction budget and faults.
_DELAY_OPCODES = frozenset({0xFD, 0xFE})


@dataclass(frozen=True)
class Finding:
    kind: str
    offset: int
    detail: str

    def __str__(self) -> str:
        return f"{self.kind} at offset 0x{self.offset:04x}: {self.detail}"


def _delay_free_cycle(payload: bytes, starts: set[int],
                      edges: dict[int, set[int]],
                      call_targets: dict[int, int]) -> int | None:
    """Return the smallest offset on a reachable delay-free cycle, or None.

    Operates on the control-flow graph the walk decoded: nodes are
    instruction starts, edges are fall-throughs plus validated branch/call
    targets (0xf7 loop-ends are finite-count fall-throughs in the VM's
    dynamic stack model, so they contribute no static back-edge).  Delay
    instructions (0xfd/0xfe) break cycles; a 0xfc call breaks them too when
    any delay is reachable from its callee, since the cycle through the
    call's return point dynamically executes the callee body.
    """
    graph = {node: {t for t in edges.get(node, ()) if t in starts}
             for node in starts}

    def reaches_delay(entry: int) -> bool:
        seen: set[int] = set()
        stack = [entry]
        while stack:
            node = stack.pop()
            if node in seen:
                continue
            seen.add(node)
            if payload[node] in _DELAY_OPCODES:
                return True
            stack.extend(graph.get(node, ()))
        return False

    keep = set()
    for node in starts:
        cmd = payload[node]
        if cmd in _DELAY_OPCODES:
            continue
        if cmd == 0xFC and node in call_targets and \
                reaches_delay(call_targets[node]):
            continue
        keep.add(node)
    # Kahn peels on the induced subgraph, in both directions (in-degree-0
    # forward, then out-degree-0 backward) so the survivors are the cycles
    # themselves, not acyclic code upstream or downstream of one.
    def peel(nodes: set[int], forward: bool) -> set[int]:
        succ = {node: {t for t in graph[node] if t in nodes}
                for node in nodes}
        if not forward:
            reverse: dict[int, set[int]] = {node: set() for node in nodes}
            for node, targets in succ.items():
                for target in targets:
                    reverse[target].add(node)
            succ = reverse
        degree = {node: 0 for node in nodes}
        for targets in succ.values():
            for target in targets:
                degree[target] += 1
        queue = [node for node in nodes if degree[node] == 0]
        alive = set(nodes)
        while queue:
            node = queue.pop()
            alive.discard(node)
            for target in succ[node]:
                degree[target] -= 1
                if degree[target] == 0:
                    queue.append(target)
        return alive

    survivors = peel(peel(keep, True), False)
    if not survivors:
        return None
    return min(survivors)


def walk_sequence(payload: bytes, fmt: str = FORMAT_US, *,
                  max_instructions: int | None = None
                  ) -> tuple[bool, list[Finding]]:
    """Statically validate one sequence-level script from offset 0.

    Returns (ok, findings); ok is True iff findings is empty.  Validation
    only -- no timing, voice, or state interpretation.
    """
    if fmt not in _FORMATS:
        raise ValueError(f"unknown sequence format: {fmt!r}")
    length = len(payload)
    if length == 0:
        return False, [Finding(FINDING_EMPTY, 0, "sequence has no bytes")]
    if length > _MAX_SEQUENCE_LENGTH:
        return False, [Finding(
            FINDING_OVERSIZED, 0,
            f"{length} bytes exceeds the VM's uint16 addressing limit "
            f"{_MAX_SEQUENCE_LENGTH}")]
    budget = (length + _BUDGET_HEADROOM if max_instructions is None
              else max_instructions)
    findings: list[Finding] = []
    starts: set[int] = set()          # decoded instruction boundaries
    operand_owner: dict[int, int] = {}  # operand byte -> instruction start
    # Control-flow graph of the decode (fall-throughs + validated targets),
    # consumed by the delay-free-cycle analysis after the walk.
    edges: dict[int, set[int]] = {}
    call_targets: dict[int, int] = {}   # 0xfc call site -> callee offset
    # Worklist entries: (offset, source) where source is the branching
    # instruction's offset for queued targets, or None for the entry point.
    work: list[tuple[int, int | None]] = [(0, None)]
    instructions = 0

    def read_operand(at: int, cmd: int, pc: int, count: int) -> tuple[bytes | None, int]:
        """Consume operand bytes, mirroring vm_read_u8/vm_read_be16 bounds."""
        if pc + count > length:
            findings.append(Finding(
                FINDING_TRUNCATED, at,
                f"opcode 0x{cmd:02x} needs {count} operand byte(s) past the "
                f"end of the {length}-byte sequence"))
            return None, pc
        for index in range(pc, pc + count):
            if index in starts:
                findings.append(Finding(
                    FINDING_OVERLAP, at,
                    f"opcode 0x{cmd:02x} operand byte at 0x{index:04x} was "
                    "already decoded as an instruction boundary"))
            operand_owner.setdefault(index, at)
        return payload[pc:pc + count], pc + count

    def read_var_u16(at: int, cmd: int, pc: int) -> tuple[int | None, int]:
        """vm_read_var_u16: 1 byte, or 2 when the high bit is set."""
        first, pc = read_operand(at, cmd, pc, 1)
        if first is None:
            return None, pc
        if first[0] & 0x80 == 0:
            return first[0], pc
        second, pc = read_operand(at, cmd, pc, 1)
        if second is None:
            return None, pc
        return ((first[0] & 0x7F) << 8) | second[0], pc

    def read_abs_target(at: int, cmd: int, pc: int,
                        what: str) -> tuple[int | None, int]:
        """vm_read_target: big-endian u16, signed, in [0, length)."""
        raw_bytes, pc = read_operand(at, cmd, pc, 2)
        if raw_bytes is None:
            return None, pc
        raw = (raw_bytes[0] << 8) | raw_bytes[1]
        relative = raw - 0x10000 if raw & 0x8000 else raw
        if relative < 0 or relative >= length:
            findings.append(Finding(
                FINDING_TARGET_OUT_OF_RANGE, at,
                f"opcode 0x{cmd:02x} {what} target 0x{raw:04x} is outside "
                f"the {length}-byte sequence"))
            return None, pc
        return relative, pc

    while work:
        pc, source = work.pop()
        if source is not None and pc in operand_owner and pc not in starts:
            findings.append(Finding(
                FINDING_TARGET_MID_INSTRUCTION, source,
                f"branch/call target 0x{pc:04x} lands inside the operands "
                f"of the instruction at 0x{operand_owner[pc]:04x}"))
            continue
        while pc not in starts:
            if pc >= length:
                findings.append(Finding(
                    FINDING_RUNS_PAST_END, pc,
                    "control flow runs past the end of the sequence "
                    "without an end opcode, jump, or loop"))
                break
            if pc in operand_owner:
                findings.append(Finding(
                    FINDING_OVERLAP, pc,
                    f"control flow re-enters the operand bytes of the "
                    f"instruction at 0x{operand_owner[pc]:04x}"))
                break
            instructions += 1
            if instructions > budget:
                findings.append(Finding(
                    FINDING_BUDGET, pc,
                    f"work budget of {budget} instructions exhausted"))
                return False, findings
            at = pc
            starts.add(at)
            cmd = payload[pc]
            pc += 1
            stop = False
            if cmd >= 0xC0:
                if fmt == FORMAT_EU_SH and cmd in (0xDA, 0xDC):
                    # sequence_vm.c:235-242: the EU/SH layout does not share
                    # the US meanings; the VM fails closed pre-operand.
                    findings.append(Finding(
                        FINDING_UNKNOWN_OPCODE, at,
                        f"opcode 0x{cmd:02x} is rejected by the EU/SH "
                        "sequence format"))
                    stop = True
                elif cmd == 0xF0 or cmd == 0xF1 or (cmd == 0xF2 and
                                                    fmt == FORMAT_US):
                    if fmt == FORMAT_US:
                        if cmd == 0xF0:
                            findings.append(Finding(
                                FINDING_UNKNOWN_OPCODE, at,
                                "opcode 0xf0 is invalid in the US sequence "
                                "format"))
                            stop = True
                        elif cmd == 0xF2:  # US reserve-notes + u8
                            operand, pc = read_operand(at, cmd, pc, 1)
                            stop = operand is None
                        # US 0xf1 carries no operand.
                    elif cmd == 0xF1:  # EU/SH reserve-notes + u8
                        operand, pc = read_operand(at, cmd, pc, 1)
                        stop = operand is None
                    # EU/SH 0xf0 carries no operand.
                elif cmd in _U8_OPCODES:
                    operand, pc = read_operand(at, cmd, pc, 1)
                    stop = operand is None
                elif cmd == 0xD4:  # mute: no operand
                    pass
                elif cmd in _BE16_OPCODES:
                    operand, pc = read_operand(at, cmd, pc, 2)
                    stop = operand is None
                elif cmd == 0xFF:  # end (or call return); path terminates
                    stop = True
                elif cmd in _ABS_TARGET_OPCODES:
                    target, pc = read_abs_target(at, cmd, pc, "branch/call")
                    if target is not None:
                        work.append((target, at))
                        edges.setdefault(at, set()).add(target)
                        if cmd == 0xFC:
                            call_targets[at] = target
                    # 0xfb is unconditional; a rejected target also ends the
                    # path (the VM faults there).
                    stop = cmd == 0xFB or target is None
                elif cmd == 0xF8:  # loop start: u8 count
                    operand, pc = read_operand(at, cmd, pc, 1)
                    stop = operand is None
                elif cmd == 0xF7:  # loop end: back-edge re-joins decoded code
                    pass
                elif cmd == 0xFD:  # delay: var u16 (tick stops, pc persists)
                    value, pc = read_var_u16(at, cmd, pc)
                    stop = value is None
                elif cmd == 0xFE:  # delay 1: no operand
                    pass
                elif fmt == FORMAT_EU_SH and cmd in (0xF4, 0xF3, 0xF2):
                    # vm_flow :198-218: s8 displacement relative to the
                    # post-operand pc; underflow/overflow fail closed.
                    operand, pc = read_operand(at, cmd, pc, 1)
                    if operand is None:
                        stop = True
                    else:
                        displacement = (operand[0] - 256
                                        if operand[0] & 0x80 else operand[0])
                        target = pc + displacement
                        if target < 0 or target >= length:
                            findings.append(Finding(
                                FINDING_TARGET_OUT_OF_RANGE, at,
                                f"opcode 0x{cmd:02x} relative displacement "
                                f"{displacement} escapes the {length}-byte "
                                "sequence"))
                            stop = True
                        else:
                            work.append((target, at))
                            edges.setdefault(at, set()).add(target)
                            stop = cmd == 0xF4  # unconditional relative jump
                else:
                    findings.append(Finding(
                        FINDING_UNKNOWN_OPCODE, at,
                        f"opcode 0x{cmd:02x} is not accepted by the "
                        f"sequence VM ({fmt} format)"))
                    stop = True
            else:
                family = cmd & 0xF0
                if family == 0x90:
                    # Channel start: range-validate the pointer, keep the
                    # channel script body opaque (no VM channel interpreter
                    # exists to port -- see module docstring).
                    target, pc = read_abs_target(at, cmd, pc,
                                                 "channel-script")
                    stop = target is None
                elif family in _LOW_FAMILIES_NO_OPERAND:
                    pass  # value/variation ops carry no operand
                else:  # families 0x30/0xb0 fail closed (:373-375)
                    findings.append(Finding(
                        FINDING_UNKNOWN_OPCODE, at,
                        f"opcode 0x{cmd:02x} (family 0x{family:02x}) is not "
                        "accepted by the sequence VM"))
                    stop = True
            if stop:
                break
            edges.setdefault(at, set()).add(pc)  # fall-through edge
    cycle_at = _delay_free_cycle(payload, starts, edges, call_targets)
    if cycle_at is not None:
        findings.append(Finding(
            FINDING_DELAY_FREE_LOOP, cycle_at,
            "reachable control-flow cycle contains no delay opcode "
            "(0xfd/0xfe); the VM's 64-instruction tick budget "
            "(SM64_SATURN_SEQUENCE_VM_INSTRUCTION_BUDGET) faults on the "
            "first tick"))
    return not findings, findings


__all__ = [
    "FINDING_BUDGET", "FINDING_DELAY_FREE_LOOP", "FINDING_EMPTY",
    "FINDING_OVERLAP", "FINDING_OVERSIZED", "FINDING_RUNS_PAST_END",
    "FINDING_TARGET_MID_INSTRUCTION", "FINDING_TARGET_OUT_OF_RANGE",
    "FINDING_TRUNCATED", "FINDING_UNKNOWN_OPCODE", "FORMAT_EU_SH",
    "FORMAT_US", "Finding", "walk_sequence",
]
