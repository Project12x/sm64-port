# Task 3 implementation report — target S64B v2 and mixed S64F

## Status

Source-complete at behavior commit `87be53b6` pending independent parser/ABI
review. Task 4 and all material/compiler/runtime/demo/release gates remain
closed.

## Reconciliation and scope

- Reconciled starting HEAD `efd70710` against the approved design, active
  plan, Task 2 report, ledger, and existing unrelated dirty/untracked work.
- Changed only the Task 3 target parser/bundle, deterministic fixtures,
  Make gate, and required CHANGELOG/plan/ledger/report paths.
- Added no material compiler, texture residency, VDP1 renderer, BOB bundle,
  Ymir path, or Task 4+ implementation.
- Preserved all unrelated dirt; used explicit-path staging only and no reset,
  clean, or stash.

## RED and TDD evidence

The approved Make rerun of `verify-actor-bank-v2 verify-actor-family-bundle`
failed on the missing v2 constants, view fields, stable record types, and
accessors. The earlier sandboxed attempt only encountered the known MSYS
`\\d\\...` generated-fixture path rewrite and is not counted as the RED.

Two regression cycles followed:

1. The historical pose gate failed after an attempted output-zeroing change.
   Root-cause tracing showed existing v1 tests intentionally retain the last
   valid view across a failed revalidation. The parser restored its established
   no-write-on-failure contract; the pose gate returned green.
2. A new C mutation set an animation value-stream word count to `0x80000000`.
   It was accepted because `value_words * 2` wrapped to zero before the span
   check. Checked multiplication and already-proven stream ends now reject it
   before pointer use.

## Implementation

- Added stable v1/v2 constants and S64B-owned recipe/layer/alpha/tile enums.
- Extended the validated target view with material/tile counts, hot/cold span
  metadata, residency bytes, draw/texture/Gouraud aggregates, and bake policy.
  Historical v1 views leave all v2-only fields zero.
- Added bounded render-binding, target-material, and texture-tile accessors.
- Added one version dispatch. Shared prefix, pose, workspace, and GEO1 checks
  run once; v2 then requires exact 192-byte/common span canonicality and a
  linear resource-tail validation.
- Validates record sizes, zero reserved/padding bytes, checked additions/
  multiplications/alignment, exact table/payload order, dimensions, format
  equations, dense first-use ordinals, transparency words, recipe/tile
  compatibility, exact aggregates, and no trailing bytes. The final pointer
  audit also proves each aligned tile-padding endpoint is inside the declared
  texture payload before scanning the padding.
- Replaced the S64F resolver's manual v1 header reconstruction with the
  version-owned expected-hash validator. S64F contains no v2 offset knowledge.
- Deterministic Python fixtures cover v1, untextured v2, CLUT16 v2, RGB1555
  v2, padded v2, and mixed v1/v2 S64F-v3.

## Verification

- `verify-actor-bank-v2`: PASS, 86 target mutations and exact view/record ABI.
- `verify-actor-family-bundle`: PASS, 54 mutations including a resealed bad v2
  extension inside an otherwise valid mixed S64F-v3.
- `verify-actor-pose-bank`: PASS.
- `verify-actor-meshlets`: PASS; invalid-span mutation caught.
- `verify-actor-feature-off-wrapper`: 6 tests PASS.
- Broader Python actor/bundle slice: 63 tests PASS.
- Freestanding target dry-run:
  `sh-elf-gcc -std=c11 -ffreestanding -Wall -Wextra -Werror -fsyntax-only`
  for `saturn_actor_bank.c`: PASS.
- Historical Mario: 596,896 bytes, SHA-256
  `242ecd7a91ddbfb49e65a0f04949168f1de9c24d66070c299b8889d6604ce539`;
  source identity
  `60f942e6f30d4a153393a47ac53626ee53d90ebeb750ee5d244d5ef2a16925c1`.
- Historical S64F-v3: 1,688 bytes, SHA-256
  `4b3334a61f8ce7c8b2c4548a112b0c7354c444b42659ec7943941de5529e4dbc`.

## Reference and reuse record

- Repository/commit/license: this repository at starting commit `efd70710`;
  destination license and existing attribution are unchanged.
- In-tree sources inspected: `actor_bank_format.py`, `actor_bank_v2.py`, their
  Task 2 tests, historical `saturn_actor_bank.c`, `saturn_actor_bundle.c`, and
  the existing pose/meshlet/family-bundle fixtures.
- Reuse mode: close-port/shared-core extension. The target v2 tail follows the
  canonical host authority while retaining the existing freestanding GEO1/
  pose validator and checked S64F arithmetic. No external source was copied.

## Remaining gates

- Independent Task 3 spec-compliance and code-quality/parser-ABI review.
- Task 4 exact measured BOB material capture/lowering and all later bundle,
  residency, runtime, Cannon/Ymir, release, smoke, visual, desktop, manual,
  retail, and total-game gates remain unchecked.
