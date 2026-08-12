# SDD ledger — plan: docs/superpowers/plans/2026-08-11-saturn-generic-actor-bundle.md

## 2026-08-11 execution start

- Controller HEAD: `0baac1a225d512f0f7eb95c36f2766fcef723c15` on linked worktree
  `sm64-port/.worktrees/sh2-native-math-purge`, branch
  `sh2/native-math-purge`.
- Workflow: `superpowers:subagent-driven-development`, selected by the owner.
  Each task receives a fresh implementer, task-scoped spec/quality review, and
  any required bounded fix/re-review loop; a whole-branch review follows Task
  11.
- Preflight: the plan has 11 serialized tasks and 59 unchecked steps. No
  contradiction was found between tasks or the Global Constraints. Immutable
  actor bytes remain in fixed DRAM-cart residency; mutable state remains in
  generated fixed LWRAM; all cross-SH-2 publication is scalar and
  generation-last; CD work is master-owned and bounded to 16 sectors/step.
- Open gates: all implementation, review, target, release, smoke, visual,
  desktop, manual, and total-game gates remain open. Task 16 Task 2 remains
  blocked until this prerequisite is source-complete and independently
  approved.
- Next: Task 1 shared freestanding SHA-256 RED/GREEN implementation.

## Task 1: active

- Base: `0baac1a225d512f0f7eb95c36f2766fcef723c15`.
- Baseline: `verify-scene-package-runtime verify-actor-family-bank` initially
  reached package runtime PASS but the family gate's native Python consumed
  the MSYS `/d/...` report spelling and failed before assertions. Repeating
  with command-line `SATURN_REPO_ROOT` set to the same absolute native
  forward-slash worktree path exited 0: actor family report PASS (47 families,
  13 unsupported representatives / 14 records) and C actor family bank PASS.
  This is a harness path spelling, not a source/test baseline failure.
- Dispatch: fresh Task 1 implementer; task-scoped review required before Task
  2.
- Material plan correction during RED: both refactored consumers are direct
  sourceboot `SH_SRCS`, so the new `saturn_sha256.c` must also be added to
  `src/port/saturn/sourceboot/Makefile`. The original Task 1 file list omitted
  that necessary target link input. Controller authorized only the minimal
  source-list addition plus a focused Make/dry-run assertion; the implementer
  must record the correction in the tracked plan/Task 16 ledger/report. A
  known target-link break may not be deferred.
- Review: independent task reviewer verdict spec compliant / task quality
  approved, C0/I0/M0. The target-link expansion item is confirmed as an open
  later target gate rather than a missing Task 1 host/source requirement.
- Task 1: complete (commits `0baac1a..43789630`, review clean).
- Next: Task 2 canonical S64F-v3 host and target validation.

## Task 2: active

- Base: `e7b1c2bcb04705e5454684cea4487b3ff9c8692f`.
- Inputs from Task 1: public `sm64_saturn_sha256_*` API is approved; existing
  S64P and historical S64F-v2 hash semantics remain pinned. Task 2 may consume
  that API but must not reopen or duplicate it.
- Dispatch: fresh Task 2 implementer; task-scoped spec/quality review required
  before Task 3.
- Initial implementation commits: `b1133026` behavior and `bdc90eae` evidence.
  Review verdict: spec issues / quality needs fixes, C0/I2/M1.
- Task 2: minor (deferred): `source_identity` sorts `item.path` before complete
  record/type validation, so mixed malformed path types may raise `TypeError`
  instead of the documented `ValueError`. Final whole-branch review must
  triage it; it does not enter the task fix loop.
- Fix round 1/5 active from reviewer head `bdc90eae`: host validation must
  check every tier-0 reference in target-contiguous order, and C variant lookup
  must decode locally then publish only after complete success.
- Task 2: fix round 1/5 (2 addressed, 0 open; commits
  `bdc90eae..c8f8c24f`). Scoped rereview found no new Critical/Important
  breakage; the deferred Minor remains recorded for final-review triage.
- Task 2: complete (commits `e7b1c2bc..c8f8c24f`, review clean with 1 deferred
  Minor).
- Next: Task 3 deterministic generic S64B variant compilation.

## Task 3: active

- Base: `891a77a43d3587f62e21631acd23d6b584a78b58`.
- Inputs: approved canonical S64F-v3/source-identity module and unchanged
  historical S64B validator/pose/meshlet contracts. Task 3 must compile exact
  source-selected generic variants without yet orchestrating a scene bundle.
- Deferred prior Minor: malformed source-path type normalization belongs to
  final-review triage unless Task 3 directly depends on the same public input
  boundary; it is not an implicit scope expansion.
- Dispatch: fresh Task 3 implementer; independent task review required before
  Task 4.
- Initial commits: `031e1620` behavior and `cdd9637d` evidence. Review verdict:
  spec issues / quality needs fixes, C3/I2/M0.
- Fix round 1/5 active from reviewer head `cdd9637d`: exact model-ID-to-variant
  selection; rejection of unrepresentable texture/material state;
  coverage-preserving fail-closed Geo/Fast3D/animation tokenization; strict
  provenance-declared root source; and translation/range validation into named
  `ActorVariantError` boundaries.
- Task 3: fix round 1/5 (3 addressed, 2 open; commits
  `cdd9637d..4e32ca2e`). Open: validate the actual attested
  `LOAD_MODEL_FROM_GEO` binding rather than provenance metadata alone; reject
  empty/double-comma tokens in selected animation table/header parsing.
- Fix round 2/5 active from `4e32ca2e`.
- Task 3: fix round 2/5 (2 addressed, 0 open; commits
  `4e32ca2e..034f3c05`). Scoped rereview found no new Critical/Important
  breakage.
- Task 3: complete (commits `891a77a4..034f3c05`, review clean).
- Next: Task 4 real BOB S64F-v3 bundle orchestration.
- Task 3 real-source repair round 3: complete at behavior `16e61b63` and
  evidence `e72d7127`; independent scoped rereview PASS, C0/I0/M0. Fresh
  review repeated 28/28 focused tests, 25/25 rigid-group tests, the combined
  variant/pose/meshlet Make wave, exact BOB area-1 selection, direct-DL
  alpha/opaque semantic comparison, historical Mario hashes, and diff checks.
  The selected signpost GeoLayout is no longer poisoned by the following valid
  three-argument direct-DL binding. Task 4 is unblocked.

## Task 4: blocked-before-RED

- Base: `c4c0973ebe935a731c4bac98bdde2db8d7d3c530`.
- Inputs: independently approved v3 canonical pack/validate boundary and
  strict generic S64B compiler. Task 4 must expose real BOB source limitations
  rather than weaken or bypass Task 3's named fail-closed boundary.
- Dispatch: fresh Task 4 implementer; independent task review required before
  Task 5.
- Pre-edit real-BOB stop: supported key `(family ordinal 3, model ID 0x007C)`
  / `bhvMessagePanel` failed because Task 3's exact binding scanner treated a
  neighboring valid three-argument `LOAD_MODEL_FROM_DL(..., LAYER_ALPHA)` in
  `levels/scripts.c` as malformed, even though the selected signpost binding is
  the preceding two-argument GEO command. Task 4 made no edits.
- Decision: reopen Task 3 for a narrow TDD repair and scoped rereview. The
  scanner must coverage-parse both valid GEO and DL source forms, retain/check
  the direct-DL layer semantic, and continue rejecting malformed, duplicate,
  conflicting, or trailing tokens. Repair commits `16e61b63` / `e72d7127`
  passed independent scoped rereview with C0/I0/M0. Task 4 made no preflight
  edits and is now resumed from the reviewed boundary.
- Second zero-edit BOB probe after the PASS: selection reached
  `wooden_signpost_geo` but failed closed at `missing Gfx source:
  wooden_signpost_seg3_dl_0302DA48`. The closure record does not attest the
  reached model/display-list definition source. Task 3 is reopened for a
  narrow hash-bound provenance repair; ad hoc repository search/consumption is
  forbidden. Task 3 round 5 completed through behavior commits `416c3a34`,
  `56155516`, and `2d8c4479`, with final evidence `d902438e`. Same-reviewer
  rereview PASS C0/I0/M0 after 33/33 closure, 2/2 BOB, 28/28 variant/source,
  25/25 rigid, 4/4 generic, Make, compileall, and exact Mario gates. Task 4
  resumes; named `GEO_SHADOW` remains explicit unsupported evidence.
- Task 4 replay after the provenance PASS correctly inventoried signpost
  `GEO_SHADOW`, then stopped before edits on `bhvExplosion`: a real display
  list uses terminal `gsSPBranchList` without `gsSPEndDisplayList`. Task 3 is
  reopened narrowly for exact tail-transfer semantics; Task 4 remains paused
  with zero edits.
- Final implementation base: `44c903950632df17de24ec0b4781ca8d13d14563`
  after Task 3 round 7 passed same-reviewer C0/I0/M0. No Task 4 edit exists.
- Final read-only inventory: 47 canonical v2 families; 13 v2-unsupported
  families; 34 additional nonzero drawable keys, all named unsupported; two
  exact `MODEL_NONE` entries; zero compiler-supported drawable banks. The 34
  first-hit reasons are 18 `GEO_SHADOW`, 14 unrepresentable textured
  rigid/material state, one `GEO_SCALE`, and one `GEO_ASM`.
- Secondary diagnostics: none of the 18 shadow-first keys is shadow-only;
  removing only shadow in isolated copied-source roots yields 12 scale and six
  textured blockers. The sole scale-first key is family 14/model `0x006a`,
  `bhvKoopaFlag`, `actors/koopa_flag/geo.inc.c`, exact
  `GEO_SCALE(0x00, 16384)`; removing only scale next reaches textured state.
- Blocking contradiction: canonical `pack_bundle` requires variant count
  `1..128`. The truthful BOB document has zero variants, so Task 4 cannot
  create or validate the required real v3 payload/report/dependency. Missing-
  module RED, production/test/Make/CLI changes, GREEN, relocation, atomic
  publication, host/target-C validation, and generated hashes/counts were not
  run. `CHANGELOG.md` is unchanged because behavior did not change.
- Decision: recommend revising the plan around an additive, exact texture/
  material-capable Saturn/SH-2 actor-bank format while preserving S64B-v1.
  Permitting an empty bundle is a format change and does not unblock the
  full-port path; otherwise stop the prerequisite lane.
- Owner-approved architecture: BOB-first but full-game-shaped S64B v2, recorded
  normatively in
  `docs/superpowers/specs/2026-08-11-saturn-actor-bank-v2-textures-design.md`.
  V1 remains byte-exact. V2 carries target-level recipes, per-primitive baked
  VDP1 tile records, and cold CLUT16/RGB1555 payloads; it uses no runtime
  Fast3D interpreter, heap, pointers, or serialized VRAM addresses. Scene
  activation proves aggregate texture, CLUT, cart, command, Gouraud, output,
  and scratch budgets before master-only upload/generation publication;
  workers keep the existing scalar draw-record ABI. Textured triangles remain
  unpaired in v2 and unapproved later-game states fail offline by name.
- Required terminal evidence: at least one normally spawned recognizable BOB
  non-Mario actor textured through the mixed v1/v2 S64F path in Ymir, followed
  by reopened reproducibility/release/native-math/capacity/stage/smoke/visual/
  manual gates. A synthetic actor or empty bundle is not accepted.
- Design transition: the written specification passed scoped self-review,
  including contiguous byte-104..191 extension arithmetic, shared-prefix
  comparison with the production v1 parser, in-tree reference existence, and
  reconciliation of the older v1-only bundle clause. It was committed as
  `62f16de8` (`docs(saturn): design textured actor banks`).
- Owner approved the committed written specification on 2026-08-12. The
  dependency-ordered replacement plan was written and self-reviewed at
  `docs/superpowers/plans/2026-08-12-saturn-actor-bank-v2-textures.md`; it
  includes format/compiler, aggregate budgets, dual-SH-2 residency and queue
  cutover, a normal-spawn Cannon Ymir demo, and mandatory release/smoke
  reopenings.
- Plan self-review corrections: the real generic BOB S64F may be v2-only while
  the scene runs historical v1 Mario plus v2 generic actors; S64P alignment
  remains 4; texture and CLUT use independent bounded VDP1 upload regions;
  bundle-wide heterogeneous workspace stride and active texture generation are
  explicit APIs.
- Plan commit: `3338de20` (`docs(saturn): plan textured actor bank
  implementation`), exact five documentation/status paths. Scoped
  self-review passed 13-task/65-step coverage, placeholder, type/API, existing
  path/Make-target, hardware-budget, and whitespace checks.
- Current status: `ready-for-Task-1-RED` under the owner-selected
  subagent-driven workflow. No v2 production/test/Make/CHANGELOG edit or
  target gate had started at this transition.
- Open: Task 4, Tasks 5-11, Task 16 Tasks 2-5, all target/release/reseal,
  sourceboot, map/capacity, P2/Ymir, transition, smoke/visual/desktop/manual,
  and total-game gates remain unchecked.

## Task 3 real-source tail-branch repair round 6

- Status: `complete-review-passed`, C0/I0/M0, at behavior commit `a78db8c9`
  and evidence `80fcac06`; Task 4 resumes from zero edits.
- Repair: one final one-identifier `gsSPBranchList` is an unconditional
  closure-only tail transfer in collection, structural walking, and Fast3D
  extraction. Suffix, arity/expression, missing/duplicate source, recursion,
  and depth-over-256 cases fail named. `gsSPDisplayList` call/return and
  ordinary final-End rules are unchanged.
- Evidence: focused RED was 3 failures/5 errors and focused GREEN is 6/6.
  Complete GREEN is 32/32 variant/source, 27/27 rigid, 33/33 closure, 2/2 real
  BOB, 4/4 generic-family, native-root Make, compileall, and exact historical
  Mario JSON/S64B hashes. Real `bhvExplosion` advances to the preserved named
  `textured` unsupported boundary rather than fabricated S64B semantics.
- Review: six focused mutations plus 32/27/33/2/4 full suites, native Make,
  compileall, real explosion, exact Mario hashes, and scope/ABI checks passed.
  Open: Task 4, Task 16 Tasks 2-5, and all target/release/manual gates.
- Task 4 full-key replay then inventoried 25 named unsupported variants and
  stopped before edits at family 36/model `0x0065`: unselected non-drawable
  `MODEL_NONE` was incorrectly required to carry GeoLayout provenance before
  selecting `MODEL_METALLIC_BALL`. Task 3 round 7 is source-complete with
  scoped rereview PASS C0/I0/M0: numeric IDs are resolved first, the exact
  model-less sentinel is retained only as typed metadata, selected sentinels
  fail before geometry, and malformed drawable alternates still fail. GREEN is
  37/27/33/2/4 plus native-root Make, compileall, exact Mario, and real
  `GEO_SHADOW` advancement. Behavior commit is `6c0c4d45`; the full gate set
  was repeated from committed HEAD. Evidence commit is `c68d44fb`; the reviewer
  independently repeated the adversarial/full gates with no drift. Task 4 is
  unblocked to resume from zero edits.

## Task 2: source-complete, independent review pending

- Behavior commit: `b1133026` (`feat(saturn): define generic actor family
  bundle v3`) from approved base
  `e7b1c2bcb04705e5454684cea4487b3ff9c8692f`. It adds the canonical Python
  writer/validator and freestanding target validator/resolver without wiring
  sourceboot production selection.
- Tracked plan/Task 16 evidence commit: `bdc90eae` (`docs(saturn): record
  actor bundle v3 task evidence`). This task-local progress file and the full
  report remain intentionally SDD-ignored artifacts.
- RED: repo Python failed with `ModuleNotFoundError: No module named
  'actor_family_bundle'`; the new Make target failed with
  `fatal error: saturn_actor_bundle.h: No such file or directory` and the
  absent `.c` source. Production edits followed both failures.
- GREEN: seven Python tests pass. The pinned 1,580-byte fixture SHA-256 is
  `fa47d3342b2111e946778a1f67bb479f235ecb7be7bcaeac5e2d0d0a8fcf42e4`.
  The C fixture passes 53 resealed mutations and zero-output assertions;
  Python carries 46 malformed-input assertions, for 99 rejection checks.
- Historical wave: with command-line native forward-slash
  `SATURN_REPO_ROOT`, `verify-actor-family-bundle verify-actor-family-bank
  verify-actor-pose-bank verify-scene-package-runtime` exits 0. S64F v2 stays
  at 47 families/13 unsupported representatives/14 closure records; Mario
  pose-bank and S64P runtime fixtures pass.
- Self-review: scoped staged `git diff --check`, Python bytecode compilation,
  exact-size/endianness/hash/padding review, host-target fixture handoff, and
  resolver no-rescan review pass. Header mutations are resealed so field
  validation—not the outer hash alone—rejects them; pose and geometry mutations
  enter the real embedded S64B records.
- Reference/reuse: direct use plus close-port of same-repository
  `saturn_actor_bank`, `saturn_scene_package`, and `compile_actor_bank` patterns
  at base `e7b1c2bc`; no external source and no new license/notice obligation.
  The repository has no root license file.
- Review/open gates: independent spec/code-quality verdict remains pending and
  blocks Task 3. Real BOB/all-scene banks, sourceboot selection, target
  build/P2/Ymir, cart/LWRAM/HWRAM evidence, heterogeneous lanes, feature-off
  identity, transition, release/reseal, smoke, visual, desktop, and manual
  gates remain unchecked.

## Task 2 review repair round 1

- Review: `CHANGES REQUIRED C0/I2/M1`; the malformed-path type Minor was not in
  the authorized repair scope. Repair commit `f1799118` (`fix(saturn): align actor
  bundle host target validation`); tracked evidence commit `c8f8c24f`
  (`docs(saturn): record actor bundle review repair`); independent rereview
  pending.
- Host-target parity RED: the exact focused Python test resealed matching
  tier-0/tier-1 references from `[0,1,2]` to `[0,2,1]` after the first element.
  Before production edits it failed with `ValueError not raised`. GREEN mirrors
  the target's per-reference `source_ordinal + local` check.
- Variant-output finding: current public lookup already decoded into local
  `found` and assigned output only on success. The requested decoder-local
  candidate hardening is landed anyway; the short-view public lookup test was
  green before production edits and remains an explicit characterization
  regression rather than a fabricated RED.
- Final repair wave: Python 8/8; C fixture 53 mutations; historical S64F-v2
  47/13/14, Mario pose-bank, and S64P runtime pass. Python has 47 malformed
  input assertions; combined count is 100. Pinned synthetic fixture is 1,688
  bytes with SHA-256
  `4b3334a61f8ce7c8b2c4548a112b0c7354c444b42659ec7943941de5529e4dbc`.
- Status: `source-complete-review-repair-round-1; rereview pending`. Task 3,
  Task 16 production handoff, and all target/release/manual gates remain open.

## S64B-v2 replacement Task 1 transition — 2026-08-12

- Replacement plan Task 1 is complete at evidence head `1e517bac` after fix
  behavior `9d5fc03c`. The version-owned host parser preserves v1 bytes and
  owns/rejects mutations across the full 104-byte v1 header.
- Independent scoped rereview: PASS, C0/I0/M0; original padding-ownership
  finding addressed, no new regression. Replacement Task 2 is ready for RED.
- This plan's Task 4 remains superseded/paused until replacement Tasks 1-5
  finish. Task 16 production, target, release/reseal, smoke, visual, desktop,
  manual, retail, and total-game gates remain open.

## S64B-v2 replacement Task 2 transition — 2026-08-12

- Replacement Task 2 behavior `83cfc1ad`, repair `95de6457`, evidence
  `d5914059`. Exact 192-byte pointer-free v2 packing/validation preserves the
  historical Mario/S64F bytes.
- Independent initial review C0/I2/M0 found zero-draw v2 acceptance and late
  allocation bounds. Fix round 1 added v2-only required-count rejection and
  checked reduced-limit preflight before allocation. Scoped rereview: both
  findings ADDRESSED, no new issue, Spec/Quality PASS C0/I0/M0.
- Replacement Task 3 is ready for target parser/mixed-S64F RED. This plan's
  Task 4, Task 16 production, target, release/reseal, Ymir, smoke, visual,
  desktop, manual, retail, and total-game gates remain open.

## S64B-v2 replacement Task 3 transition — 2026-08-12

- Replacement Task 3 behavior `87be53b6`, evidence `ca3318cd`: freestanding
  target S64B v1/v2 validation and opaque mixed-S64F delegation.
- Independent parser/ABI review PASS, Spec/Quality C0/I0/M0; 86 S64B and 54
  S64F mutations, historical pose/meshlet/feature-off, Python parity,
  freestanding SH-2 compile, and exact historical hashes pass.
- Replacement Task 4 is ready for exact measured BOB material RED. This plan's
  Task 4 and all runtime/target/demo/release/manual gates remain open.

## S64B-v2 replacement Task 4 transition — 2026-08-12

- Replacement Task 4 behavior `52c9c1af`, repair `86de51cc`, evidence
  `f5a03808`: exact measured BOB material lowering, including real Cannon.
- Initial C0/I2/M1 review findings were repaired by complete command/final-state
  admission, upstream closure-attested PNG inputs, and bounded scalar shifts.
  Scoped rereview PASS C0/I0/M0 with 83 focused/closure tests.
- Replacement Task 5 is ready for real BOB S64F and aggregate budget RED. This
  plan's Task 4 and all target/runtime/demo/release/manual gates remain open.
