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

## 2026-08-11 generic actor bundle Task 1 — shared SHA-256

- Status: `source-complete; independent review pending` at behavior commit
  `1faa2ffb` (`refactor(saturn): share target SHA-256 validation`), with evidence
  documentation at `eba1e1ba` (`docs(saturn): record shared SHA-256 task evidence`). Task 2 must not start until its independent
  review passes. No target, P2, Ymir, manual, release, or byte-on-hardware
  evidence is claimed.
- Implementation: close-ported the private S64P SHA block/update/final path
  and the S64F v2 segmented-zeroing path into one freestanding incremental
  `sm64_saturn_sha256_*` API. Null nonempty input and total-byte overflow fail
  explicitly. S64P and historical S64F v2 fixture/payload hashes are pinned.
- Design correction: the plan's original file list omitted
  `src/port/saturn/sourceboot/Makefile`; the shared source is now in `SH_SRCS`
  beside both consumers. The focused source-list assertion passes. The Yaul
  dry-run cannot reach link expansion because its existing generated
  source-closure prerequisite is absent, so that remains an open target gate.
- Reference record: same-repository close-port from base
  `0baac1a225d512f0f7eb95c36f2766fcef723c15`, inspected
  `runtime/saturn_scene_package.c:6-118` and
  `gfx/saturn_actor_bank.c:534-657`; no external source or license boundary.
- Tests: RED was the missing `saturn_sha256.h`/`.c` compile failure. GREEN via
  the MSYS/DLL wrapper with native forward-slash `SATURN_REPO_ROOT` passes
  `verify-saturn-sha256 verify-scene-package-runtime verify-actor-family-bank`:
  actor report 47 families, 13 unsupported representatives across 14 records.
  Scoped `git diff --check` passes for Task 1 files; independent review and
  every later target/release gate remain open.

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
  generated Mario is 5,520 bytes/lane and 11,040 usable for both lanes (round 2
  below corrects the larger reserved total). The type-safe binder
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

## 2026-08-11 Task 1 independent-review repair round 2

- Rereview verdict on `c4cefaad` plus `a99fded7`: `CHANGES REQUIRED C0/I1/M0`;
  all original C0/I4/M2 findings remain addressed, but Task 2 stays blocked.
- Finding: residency hands consumers the raw byte address immediately after a
  dependency payload. Round 1 incorrectly required that address itself to be
  four-byte aligned, so valid future payload sizes (specifically a two-mod-four
  end) could not bind despite the package's advertised scratch capacity.
- TDD RED: the fixture models residency as `payload + byte_count`, exercises
  raw bases modulo four, and requires literal sizes of 5,520 bytes/lane,
  11,040 usable bytes, and 11,043 reserved bytes. Before production edits it
  failed to compile because the query did not expose usable versus reserved
  totals; the old binder would also reject residues one through three.
- Design correction: S64B `maximum_scratch` includes three bytes of worst-case
  leading headroom before two aligned lanes. The binder aligns upward within
  that reservation with overflow-safe pointer arithmetic. Exact advertised
  capacity must succeed at residues 0-3; one-byte-short, invalid lane, lane
  overlap, and output/scratch overlap fail closed. The fixed arena remains
  65,536 bytes/2,718 output records. Package kind, stable ID, residency
  allocator, and Task 2 paths remain unchanged.
- Verification: the forced MSYS/DLL run of `compile-mario-actor-bank
  verify-actor-meshlets verify-actor-pose-bank verify-actor-instance-queue
  verify-actor-batches verify-actor-feature-off-wrapper` passes. It covers
  generated S64B `max_scratch=11043`, raw residues 0-3, exact/short/overlap
  bindings, invalid-span mutation, the round-1 exact fixture/no-rescan gates,
  pose bank, actual queue/batch storage, neutrality 2/2, legacy hashes, and
  feature-off 6/6. Python bytecode syntax, scoped diff check, and unchanged
  renderer/main/residency/package/wrapper paths also pass.
- Status: `source-complete-review-repair-round-2` at behavior commit
  `16cd2570` (`fix(saturn): align actor dependency workspaces`) from
  `a99fded7`; this scoped follow-up records the exact SHA. Controller-owned
  independent rereview remains, while Task 2 and all target evidence stay
  blocked/open.

## 2026-08-11 Task 1 final rereview closure

- Independent rereview of `16cd2570` plus `40cd970e`: `PASS C0/I0/M1`. The
  original C0/I4/M2 and round-2 I1 remain closed.
- M1 was valid and test-only: after deliberately selecting a residue-one raw
  residency pointer for the worst-case headroom test, the overlap case cast
  that same misaligned address to `sm64_saturn_actor_output_record_t *`.
  Production never dereferenced it, but the conversion itself did not satisfy
  the pointed-to type's alignment contract.
- RED/closure: an explicit fixture-validity assertion failed with `overlap
  fixture formed a misaligned record pointer`. The overlap probe now uses the
  aligned `scratch + 3` address inside the same reservation, asserts alignment,
  and still receives the expected overlap rejection. No production, compiler,
  package, residency, queue, or Task 2 file changed.
- Verification: focused `verify-actor-meshlets` passes, including the invalid-
  span mutation and permanent legacy hashes. A fresh forced five-target wave
  (`verify-actor-meshlets verify-actor-pose-bank verify-actor-instance-queue
  verify-actor-batches verify-actor-feature-off-wrapper`) passes, including
  pose bank, actual queue/batch storage, neutrality 2/2, and feature-off 6/6.
- Status: `source-complete-review-passed`; scoped test/docs closure commit
  `90fdfa43` (`test(saturn): align actor overlap fixture`) records the M1 fix.
  Task 2 was not started. Tasks 2-5 and all target, P2, Ymir, manual, reseal,
  and smoke evidence remain open.

## 2026-08-11 Task 2 prerequisite reconciliation

- Status: `blocked before RED/production edits`. The authoritative Task 2
  brief requires the reviewed Task 14 registry to resolve every nonzero
  numeric `actor_bank_id` together with the exact active
  `scene_package_generation` to an immutable `sm64_saturn_actor_bank_view_t`,
  its dependency payload scratch reservation, and the matching generation.
- Evidence: at approved base `a189820f`, `rg --files src/port/saturn | rg
  "actor.*registry|registry.*actor"` returns no registry source/header. The
  production lifecycle only directly calls
  `sm64_saturn_actor_instance_bank_acquire` at `sourceboot/main.c:375` and
  direct `complete`/`retire` at `:1279-1281`; it has neither a registry call
  nor the required scene-package-generation mapping. The separately
  discoverable unreachable `2d28214c` is explicitly titled `WIP: Task14 Task3
  actor identity registry (incomplete, unreviewed)` and changes only
  `src/game/rendering_graph_node.c`, so it is not an eligible reference or
  dependency.
- References inspected: reviewed same-repository lifecycle handoff commits
  `963e10d4` and `e82759ce`, current
  `src/port/saturn/gfx/saturn_actor_runtime_handoff.h/.c`, Task 1 handoff
  state at `a189820f`, `saturn_actor_batch.h` fixed storage contract, and
  `runtime/saturn_scene_residency.h/.c`. Reuse mode would be direct use of the
  reviewed handoff state machine; no lifecycle logic was duplicated.
- RED/GREEN/tests: no test was added or run because its mandatory
  registry-resolved nonzero fixture has no approved implementation contract.
  No production files, `CHANGELOG.md`, or behavior commit were changed.
- Remaining gates: approved Task 14 registry handoff (interface, source,
  build wiring, and review); Task 2 RED/GREEN and host gates; independent
  two-stage review; Tasks 3-5; feature-off identity; all target/P2/Ymir,
  visual/manual, Task 9 reseal, and Task 10 smoke gates. No target evidence
  is claimed.

## 2026-08-11 Task 2 resumed downstream-binding audit

- Status supersedes the earlier missing-registry blocker: Task 14 is landed
  through `0d765ccb` and independently approved `PASS C0/I0/M0`, but Task 2 is
  still `needs-context before RED/production edits` on the next, distinct
  boundary. The reviewed registry owns source-object `(resolved model,
  behavior)` to nonzero family/bank/hash/package-generation identity; Task 2
  must not duplicate it.
- Concrete missing input: the registry maps BOB actors such as
  `bhvKingBobomb` to numeric bank `0x00E5754C`, hash
  `00e5754c80762a15...`, and scene-package generation 1. The generated BOB
  outputs are seven `.s64f` family/capability metadata files. A scoped artifact
  inventory finds exactly one `.s64b`,
  `build/saturn/actors/mario/mario.s64b` (596,896 bytes), whose model/family
  identity is Mario-only. The brief forbids silently substituting that bank.
- Residency evidence: the only BOB package is provisional; its
  `ANIMATION_DEPENDENCIES` section is a four-byte placeholder with
  `max_scratch=0`. Sourceboot only validates a local `scene_package_view` and
  discards it; no production `sm64_saturn_scene_residency_t` owner/active
  generation is initialized. Thus no matching immutable bank view, dependency
  payload, or Task 1 scratch reservation exists for the captured numeric IDs.
- Reviewed references/reuse mode: inspected the landed registry at
  `8a9ff531` plus repair `d0a9868b`, final prerequisite docs `0d765ccb`, the
  existing residency/package APIs, Task 1 bank workspace binding, and the
  reviewed handoff state machine (`963e10d4` plus `e82759ce`). Intended reuse
  remains direct use of those in-tree interfaces. No alternate registry,
  lifecycle, or external code was created.
- RED/GREEN/tests: no RED fixture, production edit, host test, target test, or
  CHANGELOG entry was made. The mandatory RED requires a real
  registry-resolved nonzero identity and matching immutable S64B/residency
  span; fabricating that fixture would hide the missing production contract.
- Required context: name or land the owner and interface/artifacts that expose
  generic-actor S64B banks keyed by the registry's numeric bank IDs and exact
  package generation, and initialize/retain the corresponding active scene
  residency and scratch spans. Then run the required Task 2 production-shaped
  RED, minimal GREEN, three host gates plus extended handoff gate, independent
  two-stage review, and feature-off identity proof. Tasks 3-5 and every target,
  P2, Ymir, visual/manual, Task 9 reseal, and Task 10 smoke gate remain open and
  unclaimed.

## 2026-08-11 generic actor bundle design resolution

- Status: `implementation-plan-written; source-not-started`. Task 16 Task 2 stays
  blocked before RED/production edits. The missing generic bank owner is now
  specified at
  `docs/superpowers/specs/2026-08-11-saturn-generic-actor-bundle-design.md`;
  the complete design/status transition is commit `0e87f7ac`
  (`docs(saturn): specify generic actor scene bundles`).
  No format writer, runtime loader, bank, registry regeneration, target byte,
  or CHANGELOG-visible behavior changed in this transition.
- Approved architecture: one scene-local S64F v3 bundle in the fixed unused
  region of the 32-Mbit DRAM cart; fixed big-endian offset tables capped at 64
  families and 128 drawable variants; one embedded S64B per unique supported
  `(family ordinal, model ID)`; no serialized pointers, heap, global all-game
  actor blob, or per-family HWRAM cache.
- Identity correction: S64P/S64F hashes bind the package and bundle, while
  actor descriptors bind the selected S64B source SHA-256. `actor_bank_id` is
  the nonzero leading big-endian source-hash word with collision failure, and
  Task 14's family ID remains the one-based S64F record ordinal. The identity
  registry must be regenerated after the v3 bundle exists rather than reusing
  the outer bundle hash for every actor.
- SH-2/runtime correction: the master validates S64P, S64F v3, every embedded
  S64B, hashes, spans, registry rows, and capacities before generation-last
  publication. Cross-CPU records are scalar and pointer-free. Two fixed LWRAM
  lanes use the bundle-wide maximum stride so master and slave may process
  heterogeneous banks safely. This supersedes the provisional same-dependency
  scratch ownership recorded during Task 1; the reviewed binder API is
  unchanged, while production supplies its raw span from the fixed bundle
  workspace and keeps immutable actor data in cart.
- Transition contract: stop new work, drain all old render/bank/audio leases,
  reclaim the single fixed cart region, perform bounded master-owned CD reads
  on a loading screen, validate completely, then publish. No blocking gameplay
  reads and no seamless double-buffer claim. Failure after reclaim leaves no
  active generation and never reuses stale pointers.
- Implementation plan: `docs/superpowers/plans/2026-08-11-saturn-generic-actor-bundle.md`
  maps the approved design to 11 serialized, TDD-first tasks: shared target
  SHA-256; host/target v3 validation; generic S64B compilation; real BOB
  bundle; whole-game capacity inventory; per-variant registry identities;
  non-provisional S64P/CD inputs; bundle-stride dual-SH-2 runtime; lease-drained
  bounded streaming; sourceboot integration/map proof; and final handoff.
  It preserves v2 historical bytes and Task 1's standalone ABI while requiring
  the new heterogeneous-bank global-stride binder. No implementation or
  CHANGELOG-visible behavior has begun. Planning commit:
  `a74a30808db9efb39bf953f8a910308196241489`
  (`docs(saturn): plan generic actor scene bundles`). Planning verification:
  11 tasks, 59 unchecked TDD/verification steps, balanced code fences, no
  unresolved implementation placeholders, and scoped `git diff --check`
  PASS. Independent source/code review is inapplicable until implementation;
  the plan remains owner-workflow-selection ready.
- Compatibility and gates: S64F v2 stays historical tooling only; feature-on
  production requires v3. Whole-game inventory, real BOB bank compilation,
  strict mutation tests, target cart/LWRAM/HWRAM map proof, heterogeneous
  dual-SH-2 lanes, feature-off byte identity, CD scene transition, Task 9
  rebuild/repro/v4/staging, and Task 10 smoke/visual/desktop/manual all remain
  open. The next authorized action is Task 1 of the written implementation
  plan after the owner chooses subagent-driven or inline execution; source
  implementation has not started.

## 2026-08-11 generic actor bundle Task 1 completion

- Status: `complete; independent review PASS C0/I0/M0`. Behavior commit
  `1faa2ffb` shares one freestanding incremental SHA-256 implementation across
  the existing S64P and S64F-v2 validators; evidence/status commits are
  `eba1e1ba` and `43789630`. No v3 format or target behavior is introduced.
- TDD/verification: missing public header/source RED; GREEN
  `verify-saturn-sha256 verify-scene-package-runtime
  verify-actor-family-bank` through the MSYS wrapper with the same native
  forward-slash worktree root. Vectors, null/overflow failures, pinned S64P
  fixture hash, historical S64F-v2 payload hash, 47-family report, and C bank
  gate pass. The sourceboot source-list assertion proves the shared unit
  precedes both consumers.
- Review: fresh task reviewer found no Critical, Important, or Minor issue and
  approved spec compliance/code quality. It could not verify the real Yaul
  target link because the dry-run stops before link expansion on an absent
  generated source-closure input; that remains an explicit target gate, not a
  Task 1 host/source failure or target claim.
- Design correction: Task 1's original file list omitted
  `src/port/saturn/sourceboot/Makefile`; the tracked plan now records the
  necessary minimal `SH_SRCS` addition. Reference reuse is same-repository
  close-port/shared-core from the two former private SHA implementations; no
  external source used.
- Next: generic actor bundle Task 2 canonical S64F-v3 host/target validation.
  Tasks 2-11 and every target/release/smoke/visual/desktop/manual gate remain
  open; Task 16 Task 2 stays blocked.

## 2026-08-11 generic actor bundle Task 2 — S64F v3 boundary

- Status: `source-complete; independent review pending` at `b1133026`
  (`feat(saturn): define generic actor family bundle v3`). This prerequisite
  does not resume Task 16 production handoff: sourceboot selection and Task 16
  Tasks 2-5 remain blocked/open until the whole prerequisite lane is reviewed.
- Implementation: canonical big-endian 96/64/88-byte S64F v3 host writer and
  validator; bounded 64-family/128-variant target validator; full one-pass S64B
  validation; and binary-search scalar resolver with zeroed failure outputs.
  Historical S64F v2 remains tooling-only and byte-compatible.
- TDD/evidence: missing Python module and C header/source REDs; seven Python
  tests pass with a pinned 1,580-byte digest and 46 malformed-input assertions.
  The C fixture passes 53 resealed mutations. The required Make regression wave
  passes v3, historical v2 (47/13/14), Mario pose-bank, and S64P runtime gates
  using the command-line native forward-slash worktree root.
- Reference record: same-repository direct use/close-port of
  `gfx/saturn_actor_bank.h/.c`, `runtime/saturn_scene_package.c`, and
  `tools/saturn/compile_actor_bank.py` from base `e7b1c2bc`; no external code or
  new license obligation. The repository has no root license file.
- Remaining: independent task review; generic bank compiler/bundle/registry;
  cart residency and fixed workspace; Task 16 Tasks 2-5; feature-off identity;
  target/P2/Ymir/map/capacity; Task 9 clean rebuild/repro/v4/staging; Task 10
  smoke/visual/desktop/owner-manual; release and total-game gates.

## 2026-08-11 generic actor bundle Task 2 review repair round 1

- Review verdict: initial `CHANGES REQUIRED C0/I2/M1`; the malformed-path type
  Minor was explicitly deferred and out of scope for this round. Final status
  is `complete; repair round 1 rereview PASS` at `f1799118`
  (`fix(saturn): align actor bundle host target validation`). Task 16 Tasks 2-5
  and Task 3 of the prerequisite remain blocked.
- I1 repair: host S64B validation now requires every tier-0 primitive reference
  to equal `source_ordinal + local`, matching the approved target validator.
  The focused resealed `[0,2,1]` tier-0/tier-1 mutation first failed with
  `ValueError not raised`, then passed after the parity correction.
- I2 hardening: the static target variant decoder now fills a local candidate
  and assigns its destination only after complete scalar/span success. The
  public lookup already used its own local `found`, so the shortened-view
  zero-output regression passed before and after this internal hardening; no
  contrary pre-fix RED is claimed.
- Verification: eight Python tests pass with 47 malformed-input assertions and
  pinned 1,688-byte fixture digest
  `4b3334a61f8ce7c8b2c4548a112b0c7354c444b42659ec7943941de5529e4dbc`.
  The C fixture passes 53 mutations, for 100 host/target rejection assertions.
  The native-root combined wave passes v3, historical v2 (47/13/14), Mario
  pose-bank, and S64P runtime gates.
- Scoped rereview: both Important findings ADDRESSED with no new breakage. The
  host now checks every contiguous tier-0 reference and the C helper publishes
  only a fully validated local candidate. The deferred malformed-path
  `TypeError` versus documented `ValueError` Minor remains recorded for final
  whole-branch review; it does not block Task 3.
- Remaining: deferred Minor disposition, prerequisite Tasks 3-11, Task 16
  Tasks 2-5, and every target/P2/Ymir/map/capacity,
  feature-off, transition, release/reseal, smoke, visual, desktop, manual, and
  total-game gate remain open.

## 2026-08-11 generic actor bundle Task 3 — generic S64B variant banks

- Status: `source-complete; independent review pending`. The behavior commit
  is `031e1620` (`feat(saturn): compile source-selected actor variant banks`),
  followed by this evidence/status transition. Task 4 has not started, and
  Task 16 Tasks 2-5 remain blocked/open on the rest of the prerequisite lane.
- TDD: the required variant RED failed at import with
  `ModuleNotFoundError: No module named 'actor_variant_bank'`; the animation
  parser RED separately failed because `parse_animation_table_text` did not
  exist. GREEN is 14 focused variant/source tests, 25 historical rigid-group
  tests, the existing Mario pose/meshlet Make gates, and Python compileall.
- Exact fixture evidence: rigid is a 358-byte S64B with 104-byte lane,
  211-byte scratch, payload
  `3aa76aa8d63d012e5117ed28748bf21f144e31010ac8aea03038f3fedb57a043`,
  and source identity
  `129e2d833160e29caba88e00552512b739484bc8ee14b1861475b3e0da5033f1`.
  Articulated is 468 bytes with 192-byte lane, 387-byte scratch, payload
  `1e688dc471c5590c672632377c31d8c3f4906bb31f7c3d476e8cf310069c395e`,
  and source identity
  `9efc768508379b3d07c36d76b54171f9382eee42c6c6e423cb8af4c4319f5ef2`.
  Exact geometry, joints, owners, materials, meshlets, switch/billboard/layer
  metadata, neutral pose, and two articulated frame samples are asserted.
- Mutation evidence: raw source drift, unattested source, missing list target,
  missing switch case, changed joint owner, corrupt animation span, and
  material-layer changes either raise the named boundary error or change both
  exact source/payload identity and opacity. There is no first-record or Mario
  fallback and no output is written by the compiler API.
- Legacy proof: both shared-encoder outputs are sequence-equal to the
  historical generated artifacts. Mario remains 596,896 bytes at payload
  SHA-256
  `242ecd7a91ddbfb49e65a0f04949168f1de9c24d66070c299b8889d6604ce539`;
  the exact JSON report remains
  `3f0f2dd965e7fbe9e73d9b791053478d9b3fe73199087bb827b76912e4206bf0`.
- Reference reuse: at base `891a77a43d3587f62e21631acd23d6b584a78b58`,
  directly reused `actor_family_bundle.py` source identity/S64B validation,
  `dl_rigid_groups.py` structure walking, `saturn_mesh_ir.py` primitive
  compilation, and `extract_mario_actor.py` block/Vtx/Fast3D helpers; factored
  only the existing `compile_actor_bank.py` byte encoder and close-ported its
  deterministic state/meshlet shapes. No external code or new license input.
- Remaining: independent Task 3 review; real BOB/whole-game compilation and
  capacity; scene-level S64F orchestration; registry regeneration; cart and
  fixed two-lane runtime; Task 16 Tasks 2-5; target/P2/Ymir/map/capacity;
  feature-off identity; transition; release/reseal; smoke, visual, desktop,
  owner-manual, and total-game gates. None is claimed by host/source evidence.

## 2026-08-11 generic actor bundle Task 3 review repair round 1

- Status: `source-complete-review-repair-round-1; independent rereview
  pending`. The scoped behavior commit is `11c86f6a`
  (`fix(saturn): harden actor variant source boundaries`). Task 4 has not
  started; Task 16 Tasks 2-5 remain blocked/open on the rest of the
  prerequisite lane.
- Model/root corrections: requested numeric `model_id` now resolves through
  the attested model-ID source to exactly one `model_variants` row and matching
  GeoLayout provenance; alias, mismatch, and cross-record conflicts fail.
  Declared roots must exist exactly once in their declared `geo_source`; only
  reached symbols may use unique closure-attested cross-file lookup.
- Source/semantic corrections: selected GeoLayout, Gfx, Vtx, animation
  headers, and numeric arrays require coverage-preserving tokens, exact arity,
  final terminators, and encoded ranges. S64B-v1-unrepresentable texture,
  combine, culling, environment, alpha, tile/load, and alternate-light states
  raise `UnsupportedActorSourceError` rather than losing runtime semantics.
  Mesh IR, scalar/count, conversion, and packing failures cross the public API
  only as named actor-variant errors.
- RED: the combined focused boundary reproduced the review with 24 failures
  and one raw error; targeted REDs additionally caught a duplicate animation
  table that was accepted, malformed `records=None` escaping as `TypeError`,
  and invalid C octal `08` escaping as `ValueError`.
- GREEN: 22/22 focused variant/source tests and 25/25 historical rigid-group
  tests pass; the combined native-root `verify-actor-variant-bank
  verify-actor-pose-bank verify-actor-meshlets` wave exits 0; compileall passes.
  Exact repaired rigid payload/source hashes are
  `d356417800cc21a0f982e18647be8ff1ffba27b4c1f4c50d52d4d21976771f30` /
  `780d1b65c6c27a8c7d1c77867f816ec07fd239f7fffcfa0c84a48a338fad68f7`;
  articulated hashes are
  `1bff9db7ae5c3cea3512f748a721706a3709b662cb9c8a88ad3b4c0528b0634e` /
  `0d617e2444ef50ce6d16e41aaac6572e47eacdff534c5aab8efcc08cdb118a60`.
- Legacy proof: Mario remains byte-identical at 596,896 bytes and SHA-256
  `242ecd7a91ddbfb49e65a0f04949168f1de9c24d66070c299b8889d6604ce539`;
  its exact JSON SHA-256 remains
  `3f0f2dd965e7fbe9e73d9b791053478d9b3fe73199087bb827b76912e4206bf0`.
- Remaining: independent Task 3 rereview; real BOB/whole-game compilation and
  capacity; scene S64F orchestration; registry regeneration; cart/fixed
  workspace; Task 16 Tasks 2-5; target/P2/Ymir/map/capacity; feature-off,
  transition, release/reseal, smoke, visual, desktop, owner-manual, and
  total-game gates. None is claimed by this source-only repair.

## 2026-08-11 generic actor bundle Task 3 review repair round 2

- Status: `complete; review-repair-round-2 scoped rereview PASS`. The behavior
  commit is `b21bb197`
  (`fix(saturn): verify actor model binding sources`). Task 4 has not started
  and Task 16 Tasks 2-5 remain blocked/open.
- Binding-source correction: selected numeric model metadata is no longer
  authoritative by itself. The compiler parses the attested
  `LOAD_MODEL_FROM_GEO/DL` source (or exact model-ID comment fallback), requires
  exactly one clean model-to-GeoLayout mapping, and rejects missing, duplicate,
  conflicting, malformed, unterminated, and trailing-token bindings. Exact
  tests include a fully consistent provenance mutation contradicted only by
  source bytes and a real two-record same-ID/different-root conflict.
- Animation correction: selected table/header comma positions are retained;
  doubled or trailing empty fields raise `ValueError` and are translated to
  the actor animation binding boundary. Historical Mario parsing is unchanged.
- TDD: the initial targeted RED ran three tests with five failures (binding
  contradiction plus table/header double/trailing fields); the additional
  command-boundary RED ran one test with the trailing-token subtest failing.
  Targeted GREEN is 3/3.
- Verification: 25/25 focused variant/source tests, 25/25 rigid-group tests,
  compileall, and the native-root combined variant/pose/meshlet Make wave pass.
  The articulated synthetic fixture is still 468 bytes with lane/scratch
  192/387 and exact geometry/pose, but its stricter source formatting repins
  payload/source SHA-256 to
  `2a4af81303a523a26f6ed3e9df2ac1a7aeb600a1b2c129158d3a6dc79231b93e` /
  `d5a472a4f4f90145e2882adf997b75912f205f82fc51d718ede704c329c3929d`.
  Mario remains byte-identical at 596,896 bytes and
  `242ecd7a91ddbfb49e65a0f04949168f1de9c24d66070c299b8889d6604ce539`;
  exact JSON remains
  `3f0f2dd965e7fbe9e73d9b791053478d9b3fe73199087bb827b76912e4206bf0`.
- Scoped rereview: both remaining findings ADDRESSED. Source-byte model
  binding now matches the selected numeric model/root exactly, and selected
  animation tables/headers reject empty comma positions. No new breakage was
  found.
- Remaining: all real BOB/whole-game,
  S64F/registry/cart/workspace, Task 16 Tasks 2-5, feature-off, target/P2/Ymir,
  transition, release/reseal, smoke/visual/desktop/owner-manual, and total-game
  gates. None is claimed by this source-only repair.
