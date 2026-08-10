# Task 9 implementer brief

Controller base is `038391a4` (`docs(saturn): close audit v4 support review`).
Execute Task 9 in
`docs/superpowers/plans/2026-08-10-hermetic-full-game-release-identity.md`.
Preserve every unrelated dirty/untracked path and use explicit Task 9 path
staging only. Task 8 is independently approved; do not reopen its design unless
real execution proves a defect.

## Binding rules

- Reconcile HEAD, active plan, SDD ledger, toolchain, and source closure before
  build. All checked-in closure inputs used by release mode must be tracked and
  clean. Stop on relevant dirt; do not hide it or copy it into candidate B.
- Use the exact BOB profile and flag tuple in the plan, `-j1`, release mode, and
  pinned Yaul toolchain. No convenience flag changes and no broad-root fallback.
- Candidate A is built in the implementation worktree. Candidate B is built in
  a fresh detached sibling worktree at the reviewed Task 8 HEAD, receiving only
  the explicitly allowed ignored prerequisites (`baserom.us.z64` and verified
  `build/us_pc` generated inputs). Do not copy identity, closure, object,
  sourceboot-generated, package, or final artifact outputs.
- Verify each release manifest before use. Reproducibility comparison must bind
  the exact profile, closure, package/classes, toolchain, identity-v2 payload,
  ELF, SOURCE.DAT, ISO, CUE, and normalized manifest output records. Any
  difference stops measurement/sealing.
- Measurement is one-shot `measured-unsealed` output from candidate B's verified
  immutable snapshot. Seal the contract only after reproducibility passes and
  both forbidden callers are absent. Pin exactly the printed lowercase digest
  in the same behavior commit as the immutable contract and integrity test;
  preserve v2/v3 bytes and pins.
- Re-run the exact v4 audit against the same candidate-B ELF and release
  manifest. Never report the measurement as acceptance.
- Record measured object-pool capacity/peak/margin, low-RAM/static facts, cart
  payload/limit/headroom, and exact ten-class package inventory. Capacity is
  208 only if the sealed v2 profile/identity says so. State the idle-boot
  coverage gap explicitly; route evidence is not idle evidence.
- Stage the reviewed candidate with Task 7's transactional tool, verify the
  staged manifest, and prove a second staging attempt refuses overwrite.
- If real execution exposes an implementation defect, use systematic diagnosis
  and TDD, inspect any named compatible in-tree/upstream reference first, record
  source/commit/license/files/reuse mode, update CHANGELOG in the same behavior
  commit, and keep the fix separately reviewable. Do not weaken an acceptance
  criterion to make the build pass.
- Host tests do not close target/emulator/smoke/visual/manual gates. Task 9 may
  close only gates backed by exact target/release evidence it actually runs.

## Required sequence

1. Reconcile HEAD/ledgers/status and prove the relevant closure is clean.
2. Build and verify release candidate A with the exact plan command.
3. Create a clean detached sibling worktree and build/verify candidate B.
4. Compare verified candidates; require byte-identical normalized evidence.
5. Measure candidate B with the verified SH tools and release manifest.
6. Seal `tools/saturn/sh2_native_math_goal_audit_contract_v4.txt`, pin its
   digest, add the integrity test, and run the exact v4 audit.
7. Measure/package capacity facts and stage the manual candidate transactionally.
8. Update the required evidence reports, CHANGELOG, STATE, ROADMAP, prior plans,
   active plan, ledger, and this task report without claiming Task 10 gates.
9. Make scoped behavior/evidence and source-status commits; run exact manifest,
   reproducibility, audit, package, staging, historical v2/v3, focused host,
   diff/show, and cleanliness checks.

## Stop conditions

Stop before sealing or staging and report exact evidence if any of these occur:
relevant dirty closure input; missing pinned tool; target build failure that is
not yet diagnosed/fixed; candidate mismatch; manifest/identity mismatch;
forbidden `_atan2_lookup` or `_atan2s` caller; capacity/package failure; output
collision; or ambiguous artifact ownership. Do not delete or overwrite material
user data or unrelated artifacts.

## Required handoff

Write
`.superpowers/sdd/2026-08-10-hermetic-full-game-release-identity/task-9-report.md`
with exact commands, artifact/report paths and hashes, target facts, commits,
tests, discarded environmental runs, reference reuse, and every remaining gate.
Independent evidence and code-quality reviews remain controller-owned. Do not
start Task 10 or launch Ymir.
