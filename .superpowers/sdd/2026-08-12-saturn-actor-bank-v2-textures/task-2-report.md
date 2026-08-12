# Task 2 implementation report — canonical host S64B v2

## Status

Source-complete at behavior commit `83cfc1ad` and evidence commit `671ad31d`, pending independent
spec/code-quality review. Task 3 and all
target/runtime/demo/release gates remain unopened; no target evidence is
claimed.

## Reconciliation and scope

- Reconciled starting HEAD `c6b6885f`, the active plan, Task 1 ledger, and the
  existing unrelated dirty/untracked work before RED.
- Changed only Task 2 host format/packer/tests and the required active
  CHANGELOG/plan/ledger/report. No target/runtime/Task 3+ source was modified.
- Preserved every unrelated dirty/untracked path; no reset, clean, stash, or
  broad staging was used.

## RED evidence

The first bare `python` attempt reached the inaccessible WindowsApps launcher
and stopped before discovery; it is discarded environmental evidence, not the
RED. The established worktree runtime then produced the required feature RED:

```text
.venv-saturn-tools\Scripts\python.exe -m unittest \
  tools.saturn.test_actor_bank_v2 tools.saturn.test_actor_bank_format -v

ERROR: ModuleNotFoundError: No module named 'actor_bank_v2'
FAIL: v2 dispatch still raised 'S64B v2 contract is not implemented'
Ran 5 tests ... FAILED (failures=1, errors=1)
```

Two self-review regressions also followed independent RED/GREEN cycles:

- a casefold collision separated by another case-sensitive path was accepted;
- checkout-root strings in the policy document and a nested drive component
  in a source path were accepted.

Each failed under its focused unittest before set-wide collision and host-path
rejection were implemented.

## Implementation

- Added immutable `RenderBindingV2`, `TargetMaterialV2`, `TextureTileV2`, and
  `ActorBankResourcesV2` records plus domain-separated `source_identity_v2`.
- Promotes only a production-validated v1 core. The common body moves by the
  exact 88-byte delta; animation value/index addresses and all common absolute
  span offsets move by that delta, while GEO1-relative offsets are unchanged.
- Packs the exact 192-byte big-endian header and 8/8/16-byte binding/material/
  tile records. Every reserved/alignment byte is zero and every serialized
  span is canonical and checked.
- Assigns tile and CLUT ordinals by first semantic use with hash-map exact-byte
  deduplication. No quadratic content comparison is added to validation;
  distinct non-overlapping target records with identical bytes remain legal.
- Validates dense ordinals, material/primitive identity, recipe/layer/alpha/
  tile-format compatibility, dimensions, format-derived payload equations,
  zero padding, CLUT/RGB1555 transparency words, aggregate draw/texture/
  Gouraud counts, residency totals, overflow, and exact end-of-file.
- Emits only digest/scalar/offset/count report data. Source inputs must be
  normalized root-relative UTF-8 without casefold collision or host-path
  components; checkout roots are forbidden in policy strings. No path,
  pointer, Fast3D stream, VDP1 address, timestamp, or temporary name is
  serialized.
- The historical v1 packer now reparses its finished bytes, making the
  validated-core producer/consumer boundary explicit without changing output.

## GREEN evidence

Focused production-equivalent host suites:

```text
.venv-saturn-tools\Scripts\python.exe -m unittest \
  tools.saturn.test_actor_bank_v2 tools.saturn.test_actor_bank_format -v
# Ran 14 tests in 0.797s — OK
```

Broader actor-bank/family-bundle Python coverage:

```text
.venv-saturn-tools\Scripts\python.exe -m unittest \
  tools.saturn.test_actor_bank_v2 tools.saturn.test_actor_bank_format \
  tools.saturn.test_actor_bank tools.saturn.test_actor_family_bundle \
  tools.saturn.test_actor_variant_bank tools.saturn.test_generic_actor_bank -v
# Final rerun: Ran 60 tests in 30.056s — OK
```

Required Make host gates were rerun with the approved filesystem permission
after the sandbox rewrote an MSYS fixture path as `\d\...`:

```text
make -f Makefile.saturn.mk verify-actor-family-bundle \
  verify-actor-variant-bank verify-actor-pose-bank verify-actor-meshlets
```

- actor family bundle: `PASS (53 mutations)`;
- actor variant/source: `Ran 37 tests ... OK`;
- actor pose bank: `PASS`;
- actor meshlets and invalid-span mutation: `PASS`.

`compileall` for all five affected Python modules and scoped `git diff --check`
also passed.

## Deterministic fixture and historical-byte proof

- Untextured v2 fixture: 504 bytes, SHA-256
  `e25fecda219459956bb39d11bd2d39f3775b7a310c5fbf3cbc791d285b406156`.
- CLUT16 textured v2 fixture: 584 bytes, SHA-256
  `c58c8eaeebe8930a418812838f6090e4bc23822486e1d4f3dc66eca2a1ea876a`;
  3 bindings, 1 material, 2 deduplicated tiles, 1 deduplicated CLUT,
  16 texture-resident bytes, and 32 CLUT-resident bytes.
- Fresh historical Mario S64B-v1: 596,896 bytes, SHA-256
  `242ecd7a91ddbfb49e65a0f04949168f1de9c24d66070c299b8889d6604ce539`.
- Fresh historical Mario JSON: SHA-256
  `3f0f2dd965e7fbe9e73d9b791053478d9b3fe73199087bb827b76912e4206bf0`.
- Historical source identity remains
  `60f942e6f30d4a153393a47ac53626ee53d90ebeb750ee5d244d5ef2a16925c1`.
- The existing S64F-v3 deterministic test stayed exact at 1,688 bytes and
  SHA-256 `4b3334a61f8ce7c8b2c4548a112b0c7354c444b42659ec7943941de5529e4dbc`.

## Reference/reuse provenance

- Repository/commit/license: this repository at starting commit
  `c6b6885f3ea00b71cec5cbf0f3198beb1e303d43`; destination project license and
  attribution unchanged. No external source was copied.
- In-tree files inspected: `tools/saturn/compile_actor_bank.py` historical
  S64B header/animation/GEO1 encoder; `tools/saturn/actor_bank_format.py`
  version-owned v1 parser; `tools/saturn/actor_family_bundle.py` checked
  arithmetic, canonical source identity, and embedded-bank ownership;
  `tools/saturn/test_actor_family_bundle.py` hand-derived minimal bank fixture;
  approved design and implementation plan.
- Reuse mode: shared-core extension/close-port. The v2 parser reconstructs only
  the v1 common core and delegates its pose/GEO1 semantics to the established
  v1 authority. Checked arithmetic, canonical path encoding, and first-use
  interning follow the in-tree patterns. Architecture-mismatched texture bake
  code was intentionally not copied in Task 2; material capture/baking belongs
  to Task 4.
- External prior art: none inspected or copied for this task. The plan's
  previously recorded SlaveDriver/SONIC Z-TREME items remain pattern-only and
  do not contribute source to this implementation.

## Remaining gates

- Independent Task 2 spec-compliance and code-quality review are open.
- Target C parser/mixed S64F proof is Task 3 and remains unchecked.
- Exact BOB material capture/baking, real BOB bundle, residency/runtime,
  Cannon Ymir demo, release rebuild/reseal/restage, and all smoke/visual/
  desktop/manual/retail/total-game gates remain unchecked.

## Independent review and fix round 1/5

Initial independent verdict: Spec FAIL / Quality Needs fixes, C0/I2/M0.

1. V2 accepted a structurally valid v1 core with zero meshlets and zero
   primitives. Focused RED proved both parser and packer accepted it. GREEN
   adds a v2-only required-count boundary in both paths; the same zero-draw
   fixture is still accepted by the historical v1 parser.
2. The packer grew texture and final payload bytearrays before aggregate bounds
   were known. Focused RED injected a bound below `core + 88` and replaced the
   module's bytearray constructor with a trap; allocation occurred first.
   GREEN replaces intermediate/final growth with checked preflight for the
   promoted core, binding/material/tile tables, each texture alignment/add,
   CLUT multiplication, record and copy spans, and exact total. Reduced limits
   at total, texture, and CLUT stages now raise named `ValueError` before the
   sole exact-size output allocation.

Fix-round verification:

- focused v2/v1: 16 tests, all pass;
- broader actor/bundle slice: 62 tests in 26.172s, all pass;
- family bundle: PASS (53 mutations);
- variant/source: 37 tests, all pass;
- actor pose and meshlet/invalid-span fixtures: PASS;
- canonical v2 fixtures unchanged at 504 bytes / `e25fecda...` and 584 bytes /
  `c58c8eae...`;
- historical Mario S64B, JSON, size, and source identity remain exactly the
  hashes recorded above; S64F-v3 remains exact.

Status before rereview: fix round 1 source-complete at behavior `95de6457`,
evidence `d5914059`; Task 3 remained closed at that checkpoint.

## Scoped independent rereview

The reviewer inspected `671ad31d..d5914059`, reran the 16 focused suites, and
performed direct zero-count parser/packer probes. Both original Important
findings are **ADDRESSED**: v2 rejects zero required meshlet/primitive counts
without changing v1, and every promoted/table/alignment/texture/CLUT/final
span is checked before the sole output allocation. The reduced-bound tests
trap premature allocation. No regression or out-of-scope change was found;
Spec PASS / Quality PASS, C0/I0/M0.

Task 2 is complete and approved. Task 3 may open for target S64B-v2 and mixed
S64F validation. No target, BOB material, runtime, Ymir, release, smoke,
visual, desktop, manual, retail, or total-game claim is made here.
