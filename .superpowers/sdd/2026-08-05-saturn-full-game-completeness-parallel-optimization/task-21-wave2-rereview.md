# Task 21 scoped rerereview — Wave 2 fix round 1

**Reviewed range:** `d23ec834..59c2a014`

**Reviewed commit:** `59c2a014 fix(saturn): close audio completion ticket gaps`

**Binding scope:** the host-only completion/ack ABI and SH-2 transport. No
MC68000 publisher, sourceboot service, package transaction, target, Ymir,
manual, audible, or FPS gate is reviewed or promoted.

## Verdict

**SPEC: FAIL**

**QUALITY: FAIL**

**Findings: C0 / I1 / M0**

Four of the five findings from the `d23ec834` review are resolved: exact
opcode retirement, bounded sequenced status publication, live wire counter
saturation, and the closed status/opcode matrix. The direct pending-cursor
bypass is also closed. One ABA defect remains because the replacement
"no-ack" mode is local SH-2 state with no representation on the unchanged
wire.

## Important finding

### I1 — Local no-ack state cannot prevent a late wire-ACK ABA

The legacy APIs are documented as explicit no-ack calls, but both write the
same 16-byte command record as ticketed calls
(`src/port/saturn/audio/saturn_pcm_transport.c:188-218`). The decision not to
reserve a pending entry exists only in the SH-2 transport
(`saturn_pcm_transport.c:165-170`); the MC68000 sees no ack-request bit,
sidecar, distinct opcode, or capability mode that tells it not to acknowledge
that command.

This creates two failures once a completion producer exists:

1. An ACK for a no-ack command has no pending entry, so poll rejects it without
   advancing the completion consumer and head-blocks all later ACKs
   (`saturn_pcm_transport.c:359-378`).
2. If that ACK is delayed until the command cursor wraps and a newer ticketed
   command with the same opcode reserves the same cursor, the old ACK matches
   `{ring,cursor,opcode}` and retires the newer ticket.

The new mutation proves only that a no-ack enqueue cannot overwrite a cursor
that is already pending. It does not cover no-ack first, wrap, new
same-opcode ticket, then late old ACK.

The smallest safe correction is to reject legacy/no-ack enqueue whenever the
completion capability is advertised, requiring every completion-mode command
to reserve a ticket. Alternatively, add a producer-visible ack-request/issue
identity through a versioned record or sidecar. Add RED cases for an ACK with
no pending ticket and the delayed no-ack ACK attempting to retire a newer
same-cursor/same-opcode ticket.

## Prior findings rechecked

- **Exact opcode retirement: resolved.** Pending entries store the expected
  nonzero opcode; poll compares it before consumer advancement or retirement.
  The wrong-same-class mutation leaves the pending entry intact.
- **Direct pending-cursor bypass: resolved.** Every enqueue checks an existing
  pending reservation, so a legacy call cannot overwrite an already-pending
  cursor. This does not solve the producer-visible no-ack ambiguity above.
- **Torn status publication: resolved at bounded host-contract scope.** Stable
  flags advance by four per publication, bits 2..15 provide 16384 stable
  sequence values, bit 1 marks an in-progress write, and the reader requires
  the same stable value before and after. The observer mutation demonstrates
  rejection of a mixed generation and a coherent retry. The explicit bound
  forbids wrapping all 16384 values during one three-attempt read; no producer
  is linked or claimed here.
- **Wire saturation wrap: resolved.** The live full-ring path uses the
  saturating helper, and direct control/SFX tests retain `0xffff`.
- **Status/opcode legality: resolved.** Poll and required-capacity
  classification share one matrix. FINISHED is illegal in this command-ACK
  wave; DROPPED_SFX is PLAY_REFRESH-only; PREPARED/COMMITTED require exact
  package opcodes; package commands reject generic ACCEPTED.

## Fresh evidence

The same serialized DLL/MSYS-preflight host wave passed in 4.1 seconds:

```text
verify-pcm-protocol
verify-audio-protocol-v2
verify-audio-completion-abi
verify-pcm-transport
verify-audio-sound-cpu-boot
verify-soundtest-boot
```

The sound-CPU ownership subtest passed 2/2. PLAY_REFRESH checked no-write,
completion capability, control-reserve policy, and the absence of any
MC68000/sourceboot/audible claim remain correct. The green tests do not cover
the remaining no-ack wire ABA.

## Evidence inspected

- `review-task21-wave2-fix-d23ec834..59c2a014.diff`
- `docs/superpowers/plans/2026-08-05-saturn-full-game-completeness-parallel-optimization.md`
- `task-21-brief.md`, `task-21-report.md`, and the original Wave 2 rereview
- `src/port/saturn/audio/saturn_pcm_protocol.h`
- `src/port/saturn/audio/saturn_pcm_transport.h/.c`
- `tools/saturn/audio_protocol_v2_test.c`
- `tools/saturn/audio_completion_abi_test.c`
- `tools/saturn/pcm_transport_test.c`
- `Makefile.saturn.mk` and `CHANGELOG.md`

## Required fix round 2

Make ACK expectation producer-visible or prohibit no-ack commands whenever
completion capability is active, add the two late/no-pending ACK mutations,
and rerun the same six serial gates. All production and audible gates remain
unchecked.
