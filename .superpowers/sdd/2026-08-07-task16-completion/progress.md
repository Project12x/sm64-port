# SDD ledger — plan: docs/superpowers/plans/2026-08-07-task16-completion.md

## 2026-08-11 activation

- Transition HEAD: `5a72a3aa` (`docs(saturn): record blocked integrated smoke`).
- Trigger: the release-bound 20,100-frame smoke verified exact manifest
  `9110b40d...b99`, identity, cart completion, exception-clear state, VBlank
  delivery, and pool peak 138/208 with zero allocation failures, then observed
  render generation 1 terminate `WORLD_ADMIT DONE`, `WORLD_LOWER DONE`,
  `ACTOR_ADMIT FAILED`, `ACTOR_LOWER QUARANTINED`. The source loop stopped
  after two initial credits.
- Root cause: `SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=1` is sealed, while the
  production feature-on compatibility wrapper deliberately returns failure
  because the generic actor drain/cutover is unfinished. This is a product
  completeness prerequisite, not a BIOS, emulator, VBlank, snapshot, probe,
  or stale-artifact failure.
- Decision: execute the production generic actor cutover; do not disable the
  feature. Any target-byte change reopens the hermetic Task 9 clean A/B build,
  reproducibility, v4, capacity/package, staging, and review gates before
  Task 10 resumes.
- Reusable reviewed infrastructure: actor arena/queue/batch ABI `d4efe0e9`,
  feature-off wrapper polarity `8192572f`, lifecycle handoff `e82759ce`, and
  source-pool identity/capacity work recorded in the governing plan ledger.
- Status: Task 1 active. Tasks 2–5, independent review per task, feature-off
  byte identity, target/P2 evidence, hermetic reseal, smoke, visual, desktop,
  owner manual play, and total-game evidence remain open.

## 2026-08-11 Task 1 execution

- Isolated worktree/branch: `sm64-port/.worktrees/sh2-native-math-purge`,
  `sh2/native-math-purge`; transition HEAD reconciled at `ec8ef844`. Existing
  tracked and untracked work outside Task 1 is preserved.
- Status: `source-complete`; behavior commit subject
  `feat(saturn): generalize actor meshlet preparation to bank instances` is
  commit `3d5e6ff8`. Controller-owned independent two-stage review remains open,
  so Task 2 has not started and no target-complete claim is made.
- Design correction: `sm64_saturn_actor_meshlets_prepare_bank` accepts the
  complete validated `sm64_saturn_actor_bank_view_t`, while its Task-1-owned
  output binding supplies draw capacity and quarantine reason. The header-only
  bank member cannot expose pose/geometry spans. S64F v2 records remain family
  registration/capability metadata, not meshlet geometry. Bank
  identity/hash/family/model mismatches reject before writes; package-generation
  freshness remains an open Task 2 handoff gate. The generalized sink is the
  queue-compatible single contiguous record span, partitioned opaque-first then
  translucent, plus explicit caller uniqueness scratch. Neither evaluated pose
  lights nor joint matrices are scratch. The legacy Mario sink/struct remains
  split and ABI-frozen.
- Reference-code-first record: same repository
  `https://github.com/Project12x/sm64-port.git`, transition `ec8ef844`; inspected
  Mario meshlet implementation at last path commit `c5944bac`, validated S64B
  parser/56-byte S64F v2 consumer at last path commit `2222c375`, queue ABI at
  last path commit `d4efe0e9`, plus the generated S64B/S64F layouts and governing
  plan sections. Root license is absent per `docs/saturn/PROVENANCE.md`; reuse is
  a same-repository close-port/shared-core refactor, with no external source.
- TDD evidence: the bank-driven fixture first failed to compile because the
  six-argument `prepare_bank` API and capacity/quarantine binding did not
  exist. GREEN covers S64B-derived counts/bounds, deterministic contiguous
  partitions, overflow and stale identity/generation no-write failures, and
  preservation of evaluated light and joint-matrix outputs. An explicit
  stale-generation mutation was caught at the atomic no-write assertion; the
  existing invalid-span mutation remains caught.
- Verification: the MSYS/DLL wrapper ran `verify-actor-meshlets`,
  `verify-actor-pose-bank`, and `verify-actor-feature-off-wrapper` together
  with a forced rebuild: PASS (meshlets and pose-bank; feature-off 6/6). Python
  bytecode compilation via the same wrapper, scoped `git diff --check`, and
  unchanged diffs for `saturn_demo_render.c`, `sourceboot/main.c`, and the
  feature-off wrapper test also pass. Legacy output ABI offsets/sizes are
  compile-time asserted and its production call sites are unchanged. A
  differential base-`ec8ef844`/current harness hashed accepted, rotated,
  fully-culled, and capacity-failure legacy outputs (records, positions,
  counts, return, and telemetry); all four byte hashes match, including the
  legacy failure-side effects, while generalized failures remain atomic.
- Remaining gates: controller-owned independent two-stage review, then all
  Tasks 2–5 production wiring and target/P2/Ymir/manual/reseal/smoke evidence.
  Scene-package-generation freshness is specifically a Task 2 handoff gate.
  No target, P2, Ymir, manual, FPS, Task 2 wiring, or target-byte claim is made.

## 2026-08-11 Task 1 independent-review repair round 1

- Review verdict on behavior `3d5e6ff8` plus status `863faa8d`: `CHANGES
  REQUIRED C0/I4/M2`; Task 2 remains blocked. The status commit's actual subject
  is `docs(saturn): record actor meshlet source status` (the original report's
  different subject text was incorrect).
- Status: `source-complete-review-repair` at behavior commit `c4cefaad`
  (`fix(saturn): close actor meshlet review gaps`) from base `ec8ef844`. RED
  compile evidence reports the missing
  workspace query/binder/types and an incompatible assignment from actual
  `sm64_saturn_actor_runtime_storage_t.outputs` to the distinct meshlet draw-ref
  pointer. Production edits followed that RED.
- Design decision: one renderer-neutral header owns the eight-byte record ABI
  and quarantine enum. Meshlet names are compatibility aliases. The 65,536-byte
  actor runtime arena remains queue/batch/output-only with all 2,718 records.
- Scratch ownership: residency already reserves each dependency's
  `maximum_scratch` immediately after its payload. The registry-selected S64B
  dependency owns two non-overlapping four-byte-aligned claimant lanes because
  master and slave may process separate descriptors concurrently. Per-lane
  bytes are `6V + V + align4 + 64J + align2 + 2V + V`, rounded to four bytes;
  generated Mario is 5,520 bytes/lane and 11,040 total. The type-safe binder
  takes that raw residency span, lane, and the real arena output pointer; exact
  fit succeeds while short/invalid-lane/output-overlap fails.
- Boundary handoff: Task 2 must map numeric `actor_bank_id` and exact
  scene-package generation to the immutable validated S64B view and expose the
  corresponding dependency scratch. Task 3 must gate simultaneous claimant
  lane ownership. Task 5 target map/capacity evidence remains open. Current
  combined Mario S64B metadata calls the payload `ANIMATION_DEPENDENCIES`; the
  Task 1 binder is payload-kind-neutral and does not rename/reseal it.
- Validation policy: `prepare_bank` no longer repeats full-bank validation; it
  consumes the immutable validated view and performs O(1) family/model/hash/
  generation and bounded geometry checks plus only the selected pose traversal.
  Numeric bank ID is intentionally absent because S64B has no such field.
- Fixture proof: the derived non-Mario bank uses distinct family/model/source
  identity, binds all vertices to one joint/branch (rigid), and marks every
  meshlet opaque. Its exact tier-0 sequence/partition oracle rejects duplicate,
  omitted, and wrongly-associated meshlet/primitive records. Numeric bank ID
  zero succeeds because Task 2, not S64B, owns that mapping.
- No-rescan mutation: temporarily restoring full
  `sm64_saturn_actor_bank_validate` made the fixture fail with `prepare
  traversed an unrelated animation record`; restoring the immutable-view path
  returns GREEN. This proves the regression detects traversal of unrelated
  animation records.
- Verification: the forced MSYS/DLL wrapper run of `verify-actor-meshlets
  verify-actor-pose-bank verify-actor-instance-queue verify-actor-batches
  verify-actor-feature-off-wrapper` passes, including invalid-span mutation,
  queue/batch gates, neutrality 2/2, and feature-off 6/6. Python bytecode
  compilation for the actor compiler/wrapper/neutrality scripts passes;
  scoped `git diff --check` and unchanged Task 2 production paths pass. The
  repair behavior SHA is `c4cefaad`; this scoped follow-up records it.
- Remaining gates: controller-owned independent rereview; then Tasks 2-5 and
  all target/P2/Ymir/manual/reseal/smoke evidence. Task 2 is not authorized
  before rereview and no target-complete claim is made.
