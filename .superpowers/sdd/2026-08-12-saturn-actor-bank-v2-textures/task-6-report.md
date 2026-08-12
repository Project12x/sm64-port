# Task 6 implementation report — stable S64B-v2 material binding

## Status

Source-complete-pending-review from reconciled base `a553b500`. Behavior commit
is `863b4646`. No target runtime, residency,
renderer, Ymir, release, or Task 7 claim exists.

## Reconciliation and provenance

- Plan, ledger, and HEAD agree after independently approved Task 5. Existing
  unrelated dirty/untracked work was inventoried and is preserved.
- Pinned dependency: libyaul gitlink
  `6012f79f237773378c8014e70d8998ad95a38d98`, MIT license. Inspected
  `libyaul/scu/bus/b/vdp/vdp1/cmdt.h` and `vdp1/vram.h` for the exact 32-byte
  command layout, Yaul enums, source/size/CLUT encoders, and VRAM partitions.
- Same-project shared-core sources inspected at `a553b500`:
  `saturn_ir_texture.*`, `saturn_actor_bank.*`, `actor_bank_format.py`,
  `actor_bank_v2.py`, and `actor_material_v2.py`.
- Reuse mode: dependency/API use plus same-repository direct extension. No
  external source is copied and no new license obligation is introduced.

## RED and implementation

- RED command: `make -k -f Makefile.saturn.mk verify-ir-texture
  verify-actor-material` (exit 1). The new IR fixture compiled and aborted at
  its 256-width assertion because the old `uint8_t` API truncated 256 to zero;
  the material fixture failed at the missing
  `src/port/saturn/gfx/saturn_actor_material.c` include. This was the expected
  width/missing-API failure before production edits.
- Both existing IR width parameters are now `uint16_t`. The binder validates
  width `8..504`, multiple-of-eight, height `1..255`, exact format-derived
  payload bytes, blend mode, aligned/nonoverflowing VDP1 address, CLUT range,
  and command-size arithmetic before mutation. 8/248 remain byte exact;
  256/504 are no longer truncated; `504x255` is exact `0x3FFF`.
- `saturn_actor_material.*` implements the approved interface. It accepts only
  a validated S64B-v2 view plus current nonzero scalar bank/generation mapping,
  checks aggregate and per-tile texture/CLUT partition arithmetic, uses copied
  S64B accessors, and explicitly translates stable recipes 1..7 at the
  master-only final-emission boundary. Flat Gouraud emits a polygon; every
  textured recipe delegates its final writes to the atomic IR binders with end
  code disabled.
- Tests use literal Yaul command-word expectations rather than serialized
  enum equality. They cover CLUT16/RGB1555 replace/Gouraud/half-transparent,
  flat Gouraud, exact source/size/CLUT/vertices, stale and zero generations,
  wrong bank, mapping tile mismatch, invalid primitive/material/tile/format,
  complete partition shortages, nulls, and pointer arithmetic overflow. Every
  failure compares the complete 32-byte command against its pre-call value.

## Verification

- Focused GREEN: `make -f Makefile.saturn.mk verify-ir-texture
  verify-actor-material` — `IR texture binding: PASS`; `actor material
  binding: PASS`.
- Broader fresh wave: `make -f Makefile.saturn.mk verify-ir-texture
  verify-actor-material verify-actor-bank-v2 verify-actor-family-bundle
  verify-actor-pose-bank verify-actor-meshlets
  verify-actor-feature-off-wrapper verify-actor-variant-bank` — PASS: IR,
  material, S64B `86` mutations, mixed S64F `54` mutations, pose, meshlet and
  invalid-span mutation, feature-off `6/6`, variant/source `40/40`.
- Exact installed cross-compiler used through
  `tools/saturn/with-msys-toolchain.ps1`:
  `sh-elf-gcc -std=c11 -m2 -mb -Os -g -DDEBUG -DNON_MATCHING=1
  -DAVOID_UB=1 -DVERSION_US=1 -DVERSION_JP_US=1 -D_LANGUAGE_C=1
  -DF3DEX_GBI_2E=1 -DTARGET_SATURN=1 -DSATURN_SOURCEBOOT=1
  -DNO_SEGMENTED_MEMORY=1 -fno-lto -ffunction-sections -fdata-sections
  -ffreestanding -Wall -Wextra -Werror -pedantic` with the real installed
  libyaul headers — PASS for both files under `-fsyntax-only` and `-c`.
  Generated object sizes: IR 20,904 bytes; actor material 24,544 bytes.
- Historical Mario remains 596,896 bytes / SHA-256
  `242ecd7a91ddbfb49e65a0f04949168f1de9c24d66070c299b8889d6604ce539`.

## Self-review

- Stable S64B enums remain separate from Yaul values; translation exists only
  in the master-owned C module. No pointer/VDP1/mapping state enters serialized
  or worker-visible structures.
- All fallible accessors, enum translation, generation/identity checks,
  multiplication/addition, aggregate and tile span checks, address alignment/
  overflow checks, and vertex validation occur before command mutation.
  Textured writes are performed only by the independently atomic IR binders.
- Existing 8/248 callers retain their encoded behavior. The wider prototype is
  source compatible, and the historical feature-off wrapper and Mario bank are
  unchanged. No Task 7 residency/publication or renderer integration was added.

## Open adjacent and downstream gates

- `verify-actor-family-bank` is explicitly not green: after overcoming its
  plain-Make `/d/...` path form with `SATURN_REPO_ROOT=D:/...`, its C oracle
  returns 1 because `actor_family_bank_test.c` expects historical SHA-256
  `00e5754c80762a15b5482fb1f2e88f4bc1fc7ab847f3463944e2e6689d412ee8`,
  while the current Task-5-attested report and file both carry
  `db611af699337f38a2284abf58cb287c70f9ab6e5df5b07555cda48aba7bf313`.
  The test-only anchor is outside Task 6's strict file list and does not cover
  Task 6 code. It remains open rather than being silently resealed.
- Fresh independent Task 6 spec/quality review remains mandatory. Task 7
  residency/publication, target link/run, runtime activation, renderer/Ymir,
  release, smoke, visual, desktop, manual, retail, and total-game gates remain
  unchecked.

## Commits

- Behavior `863b4646` (`feat(saturn): lower actor bank materials to VDP1`),
  including the CHANGELOG, active plan, ledger, tests, and initial report.
- This docs/evidence status commit records the behavior SHA and remains part of
  the exact review range `a553b500..HEAD`.
