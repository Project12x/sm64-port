# Saturn source-audio policy provenance

## Source boundary

Task 9 imports no new third-party audio engine or driver code.  It adapts the
SM64 policy already inherited by this repository into a Saturn-only SH-2
semantic layer while keeping `src/audio/external.h` unchanged.  This record
does not make a new license claim about the inherited SM64 source.

| Field | Record |
| --- | --- |
| Source | This repository's `src/audio/external.c` |
| Revision inspected | Task 9 base `db7c9569` |
| Reuse mode | Close adaptation within the same repository |
| Destination | `src/port/saturn/audio/saturn_audio_policy.{h,c}`, `saturn_audio_spatial.{h,c}`, and `src/port/saturn/sourceboot/source_audio_semantics.c` |
| External code copied | None |

## Exact source regions adapted

- Lines 24--58: six-entry background queue, one-channel-per-bank shape, and
  continuous/discrete freshness constants.
- Lines 833--930: `play_sound()` request identity, same-source duplicate and
  requested-priority replacement rules, and freshness refresh.
- Lines 976--1168: per-bank selection, requested-priority ordering, stale
  continuous removal, and one published sound per bank.
- Lines 1171--1297: camera-space pan, distance volume, and distance/moving
  pitch behavior.  The Saturn adapter quantizes the resulting values before
  transport; it does not send the source pointer.
- Lines 2031--2118: lower/unlower and combined background-volume constraints.
- Lines 2143--2340: initialization, getters, source/handle/bank stops, bank
  enable/disable, and moving-speed policy.
- Lines 2354--2710: dialog routing, background priority queue, duplicate
  sequence behavior, fades, secondary music, and jingle constraints/resume.

Line numbers refer to the Task 9 base and are paired with function names so
later source movement does not erase the provenance.

## Saturn changes

- The source-facing translation unit remains the sole semantic owner of every
  symbol declared by `src/audio/external.h` when the feature is enabled.  The
  existing silent stub remains the complete, mutually exclusive feature-off
  rollback owner.
- The SH-2 holds a bounded 64-entry pointer-identity table.  A 16-bit token
  encodes both slot and reuse generation.  A slot retires when its 9-bit
  generation is exhausted instead of wrapping, and package-generation
  mismatches fail closed, so a delayed stop cannot target a later occupant.
- `PLAY_REFRESH` contains the original 32-bit `soundBits`, source token,
  package generation, packed quantized volume/pan, quantized pitch, and
  freshness generation in the seven protocol-v2 payload words.  No pointer or
  native-width C structure crosses CPUs.
- The non-wire admission score retains the source `distance + positive-z/6 +
  0x4c * (0xff - requestedPriority)` rule (or requested priority alone for
  `SOUND_NO_PRIORITY_LOSS`).  Level acoustic reach, bank volume range,
  moving-speed, constant-frequency, and vibrato inputs are applied on SH-2
  before volume/pan/pitch quantization.  Distance calls the existing
  Saturn-target `sqrtf` service used by inherited source paths rather than
  embedding a second iterative implementation.  Active source pointers are resolved
  and reevaluated every source-audio tick; they remain SH-2-local.
- The inherited US catalog limits and 38 usable list nodes per bank are
  retained.  Same-frame requests first enter a 256-record bounded queue and
  are admitted before a single per-bank selection, matching the source
  request queue and its control-operation interleavings.  Waiting discrete requests use the exact ten-count post-decrement
  lifetime; published discrete requests retire on preemption or
  generation-matched driver completion.  Continuous requests retain their
  two-frame refresh grace.
- ENV completion feedback is sequence- and generation-matched.  It clears the
  jingle constraint only after the Saturn/SH two-tick guard, restores the
  aggregate background fade (including the `0xFF` normal-volume sentinel), and resumes only
  the inherited Merry-Go-Round or Piranha Plant secondary sequences.  An
  early one-shot completion is retained and retried after the guard decrement,
  so tick two—not tick three—can complete it.  A later ENV generation
  invalidates the retained completion before the adapter polls again.
- SFX-driven lowering begins only when a sound is published and emits the
  inherited 50-frame aggregate background adjustment.  Global fade carries a
  single non-menu SFX bank mask, keeping the whole transition to three bounded
  control records.

The host fixtures pin their oracle provenance to the source ranges above.
Expected state/event traces are transcribed independently from the inherited
implementation and are not calculated by the policy under test.
- Background/music policy and SFX admission remain on the SH-2.  Sequence
  bytecode timing, notes, envelopes, and SCSP voice ownership remain outside
  this task and belong to the MC68000 lane.
