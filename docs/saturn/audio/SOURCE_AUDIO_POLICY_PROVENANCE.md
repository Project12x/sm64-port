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
  encodes both slot and reuse generation, so a delayed stop cannot target a
  new pointer that reused the slot.
- `PLAY_REFRESH` contains the original 32-bit `soundBits`, source token,
  package generation, packed quantized volume/pan, quantized pitch, and
  freshness generation in the seven protocol-v2 payload words.  No pointer or
  native-width C structure crosses CPUs.
- Background/music policy and SFX admission remain on the SH-2.  Sequence
  bytecode timing, notes, envelopes, and SCSP voice ownership remain outside
  this task and belong to the MC68000 lane.
