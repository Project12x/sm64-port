# Task 21 scoped rereview — Wave 2 fix round 2

**Reviewed range:** `59c2a014..0e5ac8e`

**Reviewed commit:** `0e5ac8e4 fix(saturn): separate audio completion modes`

**Binding scope:** the host-only completion/ack ABI and SH-2 transport. No
MC68000 producer, sourceboot service, package transaction, target, Ymir,
manual, audible, or FPS gate is reviewed or promoted.

## Verdict

**SPEC: PASS**

**QUALITY: PASS**

**Findings: C0 / I0 / M0**

Fix round 2 closes the remaining producer-mode ambiguity. Completion-capable
mailboxes accept ticketed commands only; capability-absent mailboxes retain
the legacy no-ack compatibility path. Invalid, duplicate, and unmatched
completion records latch a transport fault, preventing any later ticketed
enqueue and cursor reuse before explicit recovery. All five findings from the
initial Wave 2 rereview remain resolved.

## Fix-round-2 checks

### Capability-gated no-ack rejection — pass

`sm64_saturn_audio_enqueue()` reads the completion capability before any ring
cursor, command payload, saturation counter, or producer write. It rejects
both mode mismatches:

- completion capability plus `ticket == NULL`;
- no completion capability plus `ticket != NULL`.

The completion-aware legacy-control mutation snapshots all 512 KiB of sound
RAM before the call and proves byte-for-byte equality afterward, including
unchanged command bytes and producer cursor
(`tools/saturn/audio_completion_abi_test.c:229-242`). Only host-local
`protocol_faults` increments. Capability-absent transport tests continue to
exercise the legacy no-ack path.

### No-pending and delayed completion behavior — pass

Every malformed, duplicate, or unmatched record now routes through
`sm64_saturn_audio_completion_fault()`, which increments completion fault
telemetry and permanently sets `completion_faulted` on that transport instance
(`src/port/saturn/audio/saturn_pcm_transport.c:295-300`). Completion-mode
enqueue checks the latch before reading or publishing ring state
(`saturn_pcm_transport.c:112-119`).

The new no-pending fixture publishes an old RESET acknowledgment with no
reserved ticket, proves poll leaves the completion consumer unchanged, then
proves a same-opcode ticketed enqueue fails with byte-for-byte unchanged sound
RAM (`tools/saturn/audio_completion_abi_test.c:258-284`). Thus an observed old
record cannot become valid by later reusing its ring/cursor/opcode. Recovery is
deliberately outside this host slice and must reset the transport/mailbox
together before command service resumes.

### Ticket retirement and ABA reservation — pass

Pending entries retain the exact expected opcode. Poll validates ring, two-lap
cursor, opcode, generation, and the closed status matrix before it advances
the completion consumer or clears pending state. Wrong same-class,
wrong-ring/lap, duplicate/unmatched, malformed, and illegal-status records do
not retire a ticket. All completion-mode commands now reserve tickets, so the
former mixed-mode cursor hole is closed.

### Bounded split-status publication — pass

The advancing publication sequence remains intact: bits 2..15 provide 16384
stable values, bit 1 marks an in-progress write, and the reader requires the
same stable value before and after the split fields. The observer mutation
replaces the generation between halves and proves the torn value is discarded
before a coherent retry. The explicit contract forbids wrapping all 16384
sequence values during one bounded three-attempt read. No writer is linked or
claimed in this wave.

### Saturation, status legality, and PLAY_REFRESH — pass

- Live control/SFX full-ring telemetry calls the saturating helper and remains
  at `0xffff`.
- Poll and required-capacity classification share one closed matrix. FINISHED
  remains illegal in this command-ACK wave; DROPPED_SFX is PLAY_REFRESH-only;
  PREPARED/COMMITTED require their exact package opcodes; package commands
  reject generic ACCEPTED.
- Typed PLAY_REFRESH rejects zero and values above `0xffff` before any wire
  write or cursor change, while `0xffff` remains the terminal valid epoch.
- Completion capability remains mandatory for ticket enqueue and poll, and the
  final eight completion slots remain reserved by the producer-policy helper
  for required acknowledgments.

## Fresh evidence

The serialized DLL/MSYS-preflight host wave passed in 4.2 seconds:

```text
verify-pcm-protocol
verify-audio-protocol-v2
verify-audio-completion-abi
verify-pcm-transport
verify-audio-sound-cpu-boot
verify-soundtest-boot
```

The sound-CPU ownership subtest passed 2/2. The run also includes the PCM
publication-order executable.

## Scope and documentation

The brief, report, and changelog accurately limit this to a host-side ABI and
transport contract. They explicitly leave asynchronous FINISHED identity,
MC68000 completion publication, sourceboot service/loader, real package
prepare/commit and residency, target/Ymir/hardware/manual, audible behavior,
and performance evidence open. No production or audible claim was found.

The active plan still records fix round 2 and rereview as open, which is
correct before this report. The parent may now reconcile those host-only Wave
2 entries as accepted without changing any broader Task 21 gate.

## Evidence inspected

- `review-task21-wave2-fix2-59c2a014..0e5ac8e.diff`
- `docs/superpowers/plans/2026-08-05-saturn-full-game-completeness-parallel-optimization.md`
- `task-21-brief.md`, `task-21-report.md`, and prior Wave 2 rereview
- `src/port/saturn/audio/saturn_pcm_protocol.h`
- `src/port/saturn/audio/saturn_pcm_transport.h/.c`
- `tools/saturn/audio_protocol_v2_test.c`
- `tools/saturn/audio_completion_abi_test.c`
- `tools/saturn/pcm_transport_test.c`
- `Makefile.saturn.mk` and `CHANGELOG.md`

## Deferred gates

The versioned tagged FINISHED event, production MC68000 producer, sourceboot
audio service/loader, S64A/S64P transaction, real sequence 00/AUDIO.DAT,
target build, Ymir/hardware/manual and owner-heard evidence remain unchecked.
