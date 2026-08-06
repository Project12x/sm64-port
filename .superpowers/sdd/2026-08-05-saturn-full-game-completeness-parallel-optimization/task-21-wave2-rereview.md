# Task 21 scoped rereview — Wave 2 completion/ack ABI

**Reviewed range:** `29d3fb83..d23ec834`
**Reviewed commit:** `d23ec834 feat(saturn): add bounded audio completion ABI`
**Binding scope:** the host-only completion/ack ABI and SH-2 transport. No
MC68000 publisher, sourceboot service, package transaction, target, Ymir,
manual, audible, or FPS gate is reviewed or promoted.

## Verdict

**SPEC: FAIL**
**QUALITY: FAIL**
**Findings: C0 / I5 / M0**

The layout, capability gate, PLAY_REFRESH checked conversion, and bounded host
test wave are useful groundwork, but the committed transport does not yet
satisfy its exact ticket-retirement, stable-publication, saturation, or
status/opcode claims. Two independent paths can retire or reuse a pending
cursor without the originally issued ticket, and the advertised
write-in-progress/stable-flags read is not a sequence lock.

## Important findings

### I1 — A wrong same-class opcode retires a pending ticket

The transport stores only one boolean per two-lap cursor
(`src/port/saturn/audio/saturn_pcm_transport.h:48-49`). Completion validation
checks that the received opcode belongs to the reported ring, but never checks
it against the opcode originally enqueued
(`src/port/saturn/audio/saturn_pcm_transport.c:258-296`). Poll then clears the
cursor's pending bit before the caller can compare the returned completion to
its ticket (`saturn_pcm_transport.c:369-382`).

Consequently, a pending control `SEQ_START` at cursor 0 can be retired by an
otherwise valid `MUTE`/cursor-0 completion. The public
`sm64_saturn_audio_completion_matches_ticket()` will report the mismatch only
after retirement, when the cursor is already reusable. This contradicts the
brief/report claim of exact ring/opcode/lap validation. Store the expected
opcode (and any required generation identity) per pending cursor and validate
it before advancing the completion consumer or clearing pending state. Add a
RED poll test using the right ring/lap but wrong same-class opcode.

### I2 — Unticketed enqueue bypasses the ABA-retirement guard

The cursor-busy check is conditional on `ticket != NULL`
(`src/port/saturn/audio/saturn_pcm_transport.c:147-154`). The still-public
legacy wrappers intentionally pass `NULL`
(`saturn_pcm_transport.c:181-196`). Therefore, after a ticketed command is
consumed and the command producer wraps, any unticketed enqueue can reuse and
overwrite the same cursor while its terminal completion is still pending.

The existing ABA test exercises only ticketed enqueue and misses this mixed
API path. Pending cursor ownership must block every enqueue, or the ABI must
make ticketed and unticketed modes mutually exclusive with a fail-closed mode
transition. Add a RED test that creates one pending ticket, advances/wraps the
command consumer, then attempts reuse through the legacy wrapper.

### I3 — The stable-flags read can accept a torn 32-bit generation

`sm64_saturn_audio_status_snapshot()` reads flags, the split 16-bit fields,
then flags again and accepts equal values
(`src/port/saturn/audio/saturn_pcm_transport.c:438-470`). A writer can change
the flag `COMPLETION -> COMPLETION|STATUS_WRITING -> COMPLETION` and update the
generation halves entirely between those two reads. Both observed flag values
are then equal even though the snapshot may mix old and new halves. The current
tests cover only a flag that remains writing and a missing capability; they do
not prove concurrent publication.

Use a monotonically changing even/odd publication sequence (or an equivalent
versioned stable-read protocol), not a reusable boolean writing bit. The
reader must require the same even sequence before and after. Add an observer
seam RED that performs an off/on/off publication between high- and low-word
reads and proves the mixed generation is rejected.

### I4 — Live wire saturation counters still wrap to zero

The new helper correctly saturates `0xffff`, but the actual SH-2 command-ring
enqueue path does not call it. It still performs
`(uint16_t)(saturated + 1U)`
(`src/port/saturn/audio/saturn_pcm_transport.c:127-131`), so both control and
SFX wire saturation telemetry wrap from `0xffff` to zero. The protocol helper
test proves only the unused helper, while the brief, report, and changelog
claim saturating 16-bit producer telemetry.

Route every wire saturation increment through
`sm64_saturn_pcm_counter_saturating_increment()` and add direct transport REDs
for full control and SFX rings whose shared counters already equal `0xffff`.

### I5 — Status/opcode legality remains incomplete

The record validator constrains PREPARED, COMMITTED, and DROPPED_SFX only
partially (`src/port/saturn/audio/saturn_pcm_transport.c:279-294`). It still
accepts FINISHED for control commands such as RESET/PACKAGE_COMMIT, accepts
DROPPED_SFX for stop opcodes rather than only an ordinary play/refresh drop,
and accepts generic ACCEPTED for PACKAGE_PREPARE/PACKAGE_COMMIT despite the
documented distinct PREPARED/COMMITTED terminal semantics. Any such record
also clears pending state. The public required-classification helper likewise
treats arbitrary valid control/status pairs as required
(`saturn_pcm_transport.c:404-417`).

Define one explicit legality matrix and use it in both poll validation and
required-capacity classification. At minimum, FINISHED must remain unusable as
a command ACK in this wave because the brief explicitly defers its tagged
identity and source-policy meaning. Add mutations for FINISHED+control,
DROPPED_SFX+STOP_*, ACCEPTED+PACKAGE_PREPARE/COMMIT, and every PREPARED/
COMMITTED wrong-opcode pairing.

## Contract checks that pass

- Header offsets `0x4026..0x403f` and the 32-by-16-byte completion ring
  `0x4240..0x443f` fit the mailbox without changing existing command words.
- Ticketed enqueue and completion poll require the completion capability; an
  old v2 header without the capability fails closed.
- The checked PLAY_REFRESH API rejects generation zero and values above
  `0xffff` before any sound-RAM byte or cursor changes, while `0xffff` remains
  accepted as the last epoch.
- The producer-policy helper preserves the final eight FIFO entries for
  required acknowledgments: nonessential publication stops at occupancy 24,
  required publication remains allowed through 31, and 32 rejects. No live
  MC68000 producer is present or claimed, so this is correctly only an ABI
  policy assertion.
- The documentation explicitly defers asynchronous FINISHED identity,
  production MC68000 publication, sourceboot integration, package commit,
  target/Ymir/manual, and audible evidence. No broader production claim was
  found in the reviewed commit.

## Fresh evidence

The requested serialized DLL/MSYS-preflight host wave passed in 4.1 seconds:

```text
verify-pcm-protocol
verify-audio-protocol-v2
verify-audio-completion-abi
verify-pcm-transport
verify-audio-sound-cpu-boot
verify-soundtest-boot
```

The sound-CPU ownership scan passed 2/2. These green tests confirm the covered
layout and happy/fail-closed fixtures, but they do not exercise I1-I5.

## Evidence inspected

- `review-task21-wave2-29d3fb83..d23ec834.diff`
- `docs/superpowers/plans/2026-08-05-saturn-full-game-completeness-parallel-optimization.md`
- `task-21-brief.md`, `task-21-report.md`, and the completion/ack preparation
  note
- `src/port/saturn/audio/saturn_pcm_protocol.h`
- `src/port/saturn/audio/saturn_pcm_transport.h/.c`
- `tools/saturn/audio_protocol_v2_test.c`
- `tools/saturn/audio_completion_abi_test.c`
- `tools/saturn/pcm_transport_test.c`
- `Makefile.saturn.mk` and `CHANGELOG.md`

## Required fix round

Preserve expected ticket opcode/generation until validated retirement; close
the unticketed ABA bypass; replace the reusable writing flag with a real
publication sequence; use the saturating helper in live wire paths; centralize
and test the exact status/opcode matrix. Rerun the same six-target serialized
host wave and request a scoped rereview. All production and audible gates stay
unchecked.
