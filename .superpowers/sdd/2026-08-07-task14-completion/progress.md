# SDD ledger — plan: docs/superpowers/plans/2026-08-07-task14-completion.md

## Recovery scope — 2026-08-11

- This workspace is being created to execute the one missing load-bearing
  prerequisite: Task 3, the actor identity registry. Task 16 Task 1 is complete
  and independently approved at `a189820f`; Task 16 Task 2 stopped without
  production edits at `1559e6a0` because this registry is absent.
- Tasks 1, 2, 4, 5, and 6 in this older completion plan have historical
  execution/reconciliation elsewhere in the branch and are not being
  redispatched from this new ledger. Current downstream target evidence is
  governed by the newer Task 9/10/16 plans.
- Task 3: complete as the independently reviewed source prerequisite. Final
  scoped rereview `PASS C0/I0/M0`. Base `1559e6a0`. Two preserved, unreviewed
  WIP stashes exist:
  `stash@{0}` (Makefiles/tests) and `stash@{1}` (observer seam/generator draft).
  They are reference drafts only; the authoritative requirements are Task 3
  plus the A1 recovery/audit steps in
  `docs/superpowers/plans/2026-08-09-post-manual-gate-sprint.md`.
- Required gates remain open: TDD RED/GREEN, generator determinism and real
  manifest/hash binding, absent-not-zero misses, typed observer fields,
  nonzero registered-family admission, focused host suites, scoped commit, and
  independent spec/quality review.

## Task 3 source transition — 2026-08-11

- Behavior commit `8a9ff531` adds the generated actor-identity registry,
  sourceboot/top-level generation wiring, and observer lookup on the exact
  runtime `(model resolved from sharedChild, behavior)` pair. Registry misses
  leave all four admission fields zero; no fallback identity exists.
- Tracked plan/status reconciliation is commit `1a5b8b37`; this ignored ledger
  and the durable Task 3 report were updated in place during the same task
  transition.
- Generator and observer-seam RED were captured before production edits.
  GREEN: `verify-actor-identity-registry` passes 6/6 generator tests; the
  extended actor-snapshot and render-snapshot source scripts pass; both freshly
  compiled host binaries pass when invoked by native Windows path.
- Current authoritative inputs: closure schema
  `sm64-saturn-scene-closure-v1`, 86 records, SHA-256
  `03cc66deae092544cce7a6ec246670c1f46ce7456ea45ac4633229cb56a0f28f`;
  family schema `sm64-saturn-actor-family-bank-v2`, 47 families (34 supported,
  13 unsupported), S64F 104,840 bytes, SHA-256
  `3e86389b330f3803dcd51bd6f05f8c86b0be6f5e71f012ce5735904742554dea`;
  54 supported drawable registry pairs after `MODEL_NONE` controllers are
  excluded.
- Design corrections: the preserved model-only draft key was rejected because
  14 drawable model/geos are shared by multiple behaviors; exact behavior
  equality now disambiguates equal-model runs. The observer's `uint16_t`
  family identity is the nonzero, 1-based canonical S64F record ordinal rather
  than truncating the S64F record's 32-bit stable hash. Render-range and
  switch state are descendant decisions, so object-begin retains typed neutral
  range values and switch zero before the existing authoritative switch
  recorder runs; opacity is the safe fully-opaque typed neutral because raw
  `oOpacity` aliases unrelated behavior state outside alpha-aware geo trees.
- Open gates recorded at the initial transition (the combined Make gate is
  subsequently closed in review fix round 1 below): independent spec and
  quality review; the prescribed combined
  Make gate, which compiles both binaries but fails when native Python receives
  their MSYS `/d/...` executable paths (`WinError 2`); the governing Task 14
  broad fixture/mutation checkbox; and all downstream Task 16 Task 2,
  target/Ymir, reseal, smoke, manual, and FPS work.

## Task 3 independent-review fix round 1 — 2026-08-11

- Independent review verdict was `FAIL C0/I3/M0` plus one load-bearing
  cannot-verify gate. Correction commit `d0a9868b`; tracked plan/status commit
  `0e400b15`; controller-owned scoped
  rereview remains required before Task 3 can be marked complete.
- I1 GREEN: actual frustum, selected LOD/render-range, switch callback result,
  and alpha-aware opacity seams update the bound typed observation. Executable
  actor capture coverage proves the published values; held/parent remains
  `NO_PARENT` and generic `feature_state` remains forbidden.
- I2 GREEN: the registry generator reuses Task 11's complete S64F validator,
  cross-checks every payload scalar/span and internal digest against the
  report, and requires the same nonzero build-owned scene generation in the
  compiler report and generator invocation. Malformed identity/layout/digest/
  span, report drift, and stale generation tests fail closed.
- I3 GREEN: an executable production-shaped fixture drives generated exact
  lookup through the real observer and capture. A registered shared-geo
  behavior admits with all four identity domains and typed fields; an
  unsupported miss remains zero and is rejected.
- G1 GREEN: the two affected MSYS recipes now use the existing direct-execute
  pattern instead of passing `/d/...` paths to native Python. The exact
  prescribed combined Make command passes: registry 9/9 plus actor and render
  executable/source gates, exit 0 in 59.5 s.
- Adjacent regression: actor bank 8/8 PASS; generic actor family bank 4/4 PASS.
  Scoped diff checks pass. Both reference stashes remain present and untouched.
- Refreshed current inputs: closure 86 records / SHA `d162665a330d80cf...`;
  report 47 families (34 supported/13 unsupported), generation 1 / SHA
  `dbe721eec7a9aa61...`; S64F 104,840 bytes / SHA `00e5754c80762a15...`;
  generated registry 54 rows / 20,663 bytes / SHA `dbfd686f1eece204...`.
- Remaining gates after round 1 at that checkpoint: scoped rereview; the
  governing broad Task 14 fixture and mutation checkbox; Task 16 Task 2;
  target/Ymir, package/reseal, smoke,
  manual, concurrent-SH2, hardware, and FPS evidence. No downstream work was
  started.

## Task 3 final scoped rereview — 2026-08-11

- Independent verdict: `PASS C0/I0/M0`. Task 3 is complete as the reviewed
  source prerequisite; this verdict covers behavior `8a9ff531`, initial status
  `1a5b8b37`, repair `d0a9868b`, and repair status `0e400b15`.
- Accepted verification: the exact prescribed combined Make gate passes with
  registry 9/9 plus the actor and render executable/source suites; Task 11
  regressions pass (`test_actor_bank` 8/8 and `test_generic_actor_bank` 4/4).
- `stash@{0}` (Makefiles/tests draft) and `stash@{1}`
  (observer/generator draft) remain recoverable and untouched.
- Remaining gates are unchanged and explicitly open: target cart placement and
  HWRAM evidence for this integrated result; the governing broad Task 14
  fixture/mutation coverage; every Task 16 production/cutover gate; Ymir,
  package/reseal, smoke, manual, concurrent-SH2, hardware, and FPS evidence.
  The source-prerequisite PASS does not relabel any of those downstream gates.
