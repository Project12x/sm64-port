# Task 8 implementation report — canonical actor scene-bundle owner

## Status

`source-complete-pending-review` from approved Task 7 base `dfa8b286` at
behavior commit `b84103cd`. Evidence/status commit and independent verdict are
open. This report claims
package/source ownership plus host and freestanding SH-2 module evidence only;
Task 9 production cutover, linked target, Ymir, release, and manual gates remain
unchecked.

## Delivered boundary

- Root Make emits one canonical generation-14 S64P, one exact S64F-v3 actor
  dependency, canonical payload metadata/header, and one relocation-neutral
  assembly input. Sourceboot consumes/seals those bytes and does not rebuild or
  duplicate them inside the identity boundary.
- `saturn_actor_bundle_runtime` owns a 64-byte generation-last scalar
  publication, two bounded lane claims, validated generic variant/bank
  resolution, and release. `saturn_actor_meshlets` accepts validated v1 and v2
  bank views through the same workspace binder.
- `source_scene_bundle` owns package validation, Task 7 texture activation,
  runtime publication, generic resolve/prepare, probe state, and failure
  invalidation. It is initialized after VDP1 frame-bank and checked DMA queue
  setup, before the game loop.
- Scene residency now retains exact dependency size/destination scalars in its
  staging/active identity slots, eliminating publication-sized transition
  locals.

## Exact artifact facts

- S64P: 740 bytes, file SHA-256
  `9b0a0a4a5ce1f059417175a7ad76e8ec6141a96c26af61065d48475f4a40d101`.
- Embedded canonical package identity:
  `d249a76c0444d876b2f9577e9002d9834e46e7eacf2d6d572cae043ec05b042c`.
- S64F-v3: 160,928 bytes, SHA-256
  `3eee00fd9a7440ba669c694d947696b8989192d54b0084decd832a95def6a523`.
- Assembly source SHA-256
  `bde84bbbf71e201d3ff6a62f5f3ee1182b2583a75c84ece45285a72ef24f15fb`;
  SH assembly yields `.rodata` size `0x277a4`, root `0x0..0x2e4`, and bundle
  `0x300..0x277a0`.
- Inventory: 47 families, 14 S64B-v2 variants, 20 named unsupported drawable
  selections, two `MODEL_NONE` objects; 16,640 texture bytes, 2,816 CLUT bytes,
  1,091 workspace bytes in a 1,280-byte two-lane capacity.

## Memory and Saturn/SH-2 shape

- Persistent source owner: 5,556 LWRAM bytes. The 2,208-byte owner contains
  bundle/runtime/texture publications; one 3,348-byte union overlaps boot-only
  root validation with runtime workspace.
- Scene residency adds 320 LWRAM bytes of fixed scalar dependency metadata.
- Cold stage: zero new persistent HWRAM. Sourceboot borrows the first 2,560
  bytes of the idle VDP1 command bank during boot, waits after each checked DMA,
  and returns it before the first frame. Actor texture/CLUT regions leave
  33,216 bytes of Yaul remaining capacity.
- Exact GCC 14.3 `-m2 -mb -Os -ffreestanding -fstack-usage`: source init 220 B,
  source init-from 92 B, resolve 44 B; scene begin 92 B, commit 88 B, and load
  section 112 B. All transition frames meet the 256-byte ceiling.

## Generic-path proof

- The new production files contain no Cannon, model-ID, behavior-ID, or
  family-specific branch. Cannon remains one package/test member only.
- The source-owner fixture selects an ordinary real v2 variant from the real
  BOB S64F, resolves it through the common publication, runs generic
  pose/meshlet preparation, and obtains nonzero bounded draw output without
  quarantine.
- This does not buy complete BOB yet: 20 drawable selections still need common
  Saturn reductions and all 34 need Task 9 production job/emitter wiring.

## Verification

- Wrapper/toolchain: repository MSYS wrapper, GNU Make 4.4.1, and installed
  `sh-elf-gcc` 14.3.0 load normally; no DLL-loader error.
- Host PASS: package schema 16/16; determinism 3/3; actor texture residency;
  bundle runtime; source owner; scene residency; S64B-v2 86 mutations; mixed
  S64F 54 mutations; pose; meshlets plus invalid-span mutation; instance queue;
  batches and neutrality 2/2; feature-off 6/6.
- Python compileall passes for package compiler/validator/header/tests.
- Exact SH-2 syntax/object/stack passes for source owner, actor bundle runtime,
  meshlets, actor texture residency, and scene residency. Generated assembly
  compiles with exact root/bundle symbols.
- Repeating the package target preserves exact S64P/S64F/assembly hashes.

## Honest open gates

- Independent Task 8 spec/quality review and behavior/evidence commits.
- Full hermetic sourceboot candidate link: this development worktree's
  pre-existing `build/us_pc` fixture resolves outside the worktree root, so
  identity-assets correctly fails before link. A clean candidate must own the
  ordinary 1,977-file copy.
- Task 9 common support for 20 remaining drawable source states, all-34 actor
  job/emitter cutover, and first identity-bound Ymir smoke.
- Capacity/release reproduction, reseal, staging, visual/manual, retail, and
  total-game evidence.

## Reference/reuse record

- libyaul gitlink `6012f79f237773378c8014e70d8998ad95a38d98`, MIT:
  VDP1 `vram.h`, `cmdt.h`, and build definitions; dependency/API adaptation.
- Same-repository source-cart, scene residency, Task 7 residency, generic actor
  bank/bundle/pose/meshlet, and generated-assembly patterns were close-ported
  or extended. No new external code was copied.
