# Task 8 implementation report — canonical actor scene-bundle owner

## Status

`source-complete-pending-rereview` from approved Task 7 base `dfa8b286` after
initial behavior `b84103cd`, evidence `0a180e39`, repair `dc81808b`, and
generation-transaction repair `3b81456b`. Reviews returned C0/I3/M0 then
C0/I1/M2; the same-reviewer round-2 verdict is open. This report claims
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
  source init-from 96 B, resolve 44 B; scene begin 92 B, commit 88 B, and load
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

- Same-reviewer Task 8 repair verdict and final status commit.
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

## Independent review and repair round 1

- Initial review of `dfa8b286..0a180e39` found C0/I3/M0. It proved the borrowed
  command-bank prefix could remain overwritten into the first frame, resealed
  dependency metadata could change stable ID/lifetime and still activate, and
  final generated files were overwritten despite the documented no-clobber
  boundary.
- Repair `dc81808b` initializes both VDP1 command prefixes only after cold-stage
  retirement; requires the exact `bob-area1-actors-v3` scene dependency; and
  centralizes exclusive publish-or-verify for the S64P, metadata, payload
  manifest, assembly, validation report, generated header, and ABI header.
  The compiler owns assembly bytes; Make no longer redirects over them.
- Fresh repair evidence: schema 19/19, determinism 3/3, source scene bundle
  PASS, complete focused actor/package/scene host wave PASS, Python compileall,
  and exact ELF32 big-endian SuperH object compilation PASS. An unchanged
  second exact wrapper invocation preserves hashes and timestamps for all seven
  outputs. Assembly remains `bde84bb...15fb`; root/S64F identities remain
  `9b0a0a4a...d101` / `3eee00fd...a523`.
- The first repaired run encountered two legacy JSON sidecars whose semantic
  content was identical but whose 160/137 line endings were CRLF. They were
  moved recoverably to `.superseded-crlf-*`, canonical LF copies were
  published, and subsequent runs were verify-only. This was not a DLL error.

## Repair round 2 — generation transaction

- Rereview of `0a180e39..c8a2fdf8` passed the two runtime repairs but returned
  C0/I1/M2 because per-file links were not one generation transaction. A late
  divergent payload manifest or ABI left an earlier root/header behind. It
  also corrected init-from stack evidence to 96 B and narrowed the MSYS claim
  to the tested repository-wrapper route.
- Repair `3b81456b` computes all seven bytes first, preflights the complete
  target set, stages private files, and links report/ready last. On any late
  error it removes only outputs whose exact device/inode still belongs to that
  transaction. Identical concurrent publishers converge; divergent contenders
  fail without mixing. Standalone header+ABI uses the same pair transaction.
- RED/GREEN covers static conflicts at all seven positions, injected late-link
  conflicts and ownership rollback at every position, header-before-ABI, eight
  identical concurrent publishers, and mixed divergent contenders. Schema is
  22/22, determinism 3/3, the full focused Make wave passes, output hash+mtime
  inventory remains unchanged 7/7, Python compileall passes, and exact SH-2
  object/stack evidence remains 220/96/44 B.
- The exact repository wrapper validated required DLL presence and ran MSYS
  GNU Make, host compiler/Python, and installed SH-2 tools without a loader
  failure. Direct/manual MSYS routes and the full hermetic link are not claimed.
