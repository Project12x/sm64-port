# SDD ledger — plan: docs/superpowers/plans/2026-08-10-hermetic-full-game-release-identity.md

# Hermetic Full-Game Release Identity — SDD Progress

## Reconciled starting point (2026-08-10)

- Design commit: `69c83a3f`.
- Implementation-plan commit: `15289cd6`.
- Task 1 dispatch base: `da15bd4857691fde83d0808400aca2c6ea715144`.
- Task 1 status: `complete`.
- Pre-existing Task 3 annotations remain blocked/pending: toolchain attestation
  and external dependency classification have not been implemented or reviewed.
- Unrelated dirty state was preserved: existing modifications under prior SDD
  campaigns and plan documents, plus the pre-existing untracked `.tmp-*`,
  `audits/`, evidence, review, and Saturn capture artifacts reported by
  `git status --short` at dispatch.
- Open gates: target build, reproducibility, audit, complete package inventory,
  20,100-frame smoke, visual, and manual-play evidence. Host tests do not close
  target gates.

## Task 1 execution ledger

- Status: `complete`; the first combined specification/code-quality review returned `Needs fixes`, and scoped repair rereview cleared every finding with no new Critical/Important breakage.
- TDD RED: `.\\.venv-saturn-tools\\Scripts\\python.exe tools\\saturn\\test_target_profile.py`
  exited 1 with the expected `ModuleNotFoundError: No module named
  'hermetic_manifest'` before production modules existed.
- TDD GREEN: the same command exited 0: `Ran 8 tests ... OK`.
- Scoped whitespace gate: `git diff --check -- tools/saturn/hermetic_manifest.py
  tools/saturn/target_profile.py tools/saturn/test_target_profile.py
  tools/saturn/profiles tools/saturn/manifests/sourceboot-bob-demo` exited 0.
- Additional profile check: a release-mode resolution of the checked-in BOB
  profile with its exact release config produced ten package rows and all ten
  class aggregates.
- Commit: `0f01c14d` (`feat(saturn): add deterministic target profiles`).
- First review: `Needs fixes`. Important findings are closure-wide exact
  duplicate/case-fold-collision rejection, measurement-to-publication TOCTOU
  protection, and relative output-name escape rejection. Minor finding:
  strengthen invalid-input assertions with failure-policy regexes.
- Repair round 1 status: `source-complete`; rereview remains open. Focused RED
  exposed the cross-descriptor duplicate, relative output-name escape, and
  mutation-to-publication failures. GREEN passed all 11 tests; fix commit:
  `b8f6f5e8` (`fix(saturn): harden target profile sealing`).
- Follow-up fix commit: `b345dd15` (`fix(saturn): preflight package payload
  paths`) makes the cross-descriptor case-fold rejection deterministic even
  when a case-variant spelling is absent on a case-sensitive filesystem.
- Plan-status commits: `447a2049` and `1f9bba59`; both preserve Task 1 as
  `source-complete` with independent rereview and all target gates open.
- Scoped rereview verdict: closure-wide duplicate/case-fold collision,
  measurement-to-publication revalidation, relative output-name escape, and
  failure-policy assertion findings all `ADDRESSED`; no new breakage and no
  out-of-scope observations. Task 1 specification and code-quality gates are
  approved.
- Review closeout commit: `e3bab96d` (`docs(saturn): close hermetic profile
  task review`).

## Task 2 execution ledger

- Dispatch base: `e3bab96dd0d8fae8d6a83ed34c8dcfbd1715c028`.
- Status: `complete`; the first independent combined specification/code-quality review returned `Needs fixes`, and scoped repair rereview cleared every finding with no new breakage.
- TDD RED: the focused closure command exited 1 with the expected absent-module
  ModuleNotFoundError before production implementation existed.
- TDD GREEN: .\\.venv-saturn-tools\\Scripts\\python.exe
  tools\\saturn\\test_gen_source_closure.py exited 0: Ran 11 tests ... OK.
- Regression: .\\.venv-saturn-tools\\Scripts\\python.exe
  tools\\saturn\\test_sourceboot_identity_spec_bootstrap.py exited 0:
  Ran 7 tests ... OK.
- Hygiene: scoped git diff check exited 0 before commit.
- Commit: 11cdef08 (feat(saturn): derive hermetic source closure).
- Reconciliation: the active plan and this ledger agree that Task 1 is complete. The pre-existing memory-residency campaign remains blocked only on its stale exact-target reproduction; Task 2 does not claim to close that target gate. The hermetic closure work addresses its diagnosed source-hash root cause without editing the older campaign's dirty annotations.
- Design correction: actual dependency paths are classified with sealed explicit
  classes only when rediscovered; sealed rows are never pre-added. This makes a
  missing compiler input fail exact-set equality rather than be masked by seal
  state. Compiler discovery has one stable compiler owner across C/C++ and
  assembly depfiles, while explicit classes retain their class owner.
- Open gates: Task 2 specification review, code-quality review, target build,
  reproducibility, audit, complete package inventory, 20,100-frame smoke,
  visual, and manual-play evidence.

## Task 2 repair round 1 ledger

- Review status: the first combined specification/code-quality review returned
  Needs fixes. Findings: release cleanliness omitted every generated input and
  porcelain status alone did not expose ignored/untracked closure paths.
- TDD RED: after adding the review-contract tests,
  .\\.venv-saturn-tools\\Scripts\\python.exe
  tools\\saturn\\test_gen_source_closure.py exited 1 with the expected three
  failures: case spelling was retained, an ignored generated input outside
  build passed, and an ignored untracked header passed.
- TDD GREEN: the same focused command exited 0: Ran 14 tests ... OK
  (skipped=1; the host does not permit separately case-colliding paths).
- Regression: .\\.venv-saturn-tools\\Scripts\\python.exe
  tools\\saturn\\test_sourceboot_identity_spec_bootstrap.py exited 0:
  Ran 7 tests ... OK.
- Repair behavior: canonical repository spelling now always comes from Task 1
  normalization. Every closure input except generated-input under build/ is
  proven tracked with git ls-files --error-unmatch before the required scoped
  porcelain status check. Missing/ignored/untracked inputs fail closed.
- Commit: b43169b4 (fix(saturn): harden source closure release checks).
  Rereview remains open; Task 2 stays
  source-complete and every target/reproducibility/audit/release-evidence gate
  stays open.
- First review findings: Git-check `generated-input` records outside `build/`;
  reject ignored/untracked checked-in closure inputs; normalize the single-path
  spelling consistently; add a genuinely distinct stale/removed depfile case.
  The review's ledger visibility warning is not a missing-ledger defect: this
  ignored plan-specific ledger contains the required execution record and is
  updated here for the repair transition.
- Repair commit: `b43169b4` (`fix(saturn): harden source closure release checks`).
- Final GREEN: focused closure suite has 14 passes plus one legitimate
  host-capability skip; identity-bootstrap regression has 7 passes; full Task 2
  range diff check passed.
- Scoped rereview verdict: all four findings `ADDRESSED`; no new breakage and
  no out-of-scope observations. Task 2 specification and code-quality gates
  are approved.
- Review closeout commit: `b6305500` (`docs(saturn): close source closure task
  review`).

## Task 3 execution ledger

- Dispatch base: `b6305500485a83389839c63028af6078ef344bdf`.
- Status: `source-complete`; first independent combined specification/code-quality review returned `Needs fixes`, and focused repair round 1 is active.
- TDD RED: `.\\.venv-saturn-tools\\Scripts\\python.exe
  tools\\saturn\\test_gen_toolchain_attestation.py` exited 1 with the expected
  `ModuleNotFoundError: No module named 'gen_toolchain_attestation'` before the
  production module existed. A publication-contract RED then exited 1 with the
  expected missing `write_toolchain_attestation` import before that behavior was
  added.
- TDD GREEN: the focused attestation command exited 0: `Ran 6 tests ... OK`.
  It proves root-independent canonical bytes; binary and header mutation
  resealing; unclassified/ambiguous external failures; duplicate, missing, and
  case-colliding input rejection; stale/unknown/invalid-hash seal rejection;
  and preservation of a prior output on validation failure.
- Regression: `.\\.venv-saturn-tools\\Scripts\\python.exe
  tools\\saturn\\test_gen_source_closure.py` exited 0: `Ran 14 tests ... OK
  (skipped=1)`; the skip is the existing host case-collision capability limit.
- Hygiene: `git diff --check -- tools/saturn/gen_toolchain_attestation.py
  tools/saturn/test_gen_toolchain_attestation.py` exited 0.
- Design correction: external ownership keys are `(component_id,
  component-relative path)`, and sourceboot's component version records the
  pinned Yaul version/commit plus exact GCC `--version` stdout. Component roots
  are never serialized; overlapping roots are rejected only when a discovered
  external dependency would be ambiguous.
- Commit: `1ab25c845b1a0c782db9e2aec0d27324763e6017`
  (`feat(saturn): attest release toolchain inputs`).
- Plan-status commit: `b04584d1` (`docs(saturn): record toolchain attestation
  source status`).
- Self-review: `git diff --check b6305500485a83389839c63028af6078ef344bdf..HEAD`
  returned clean after `b04584d1`; the reviewed range contains only
  `CHANGELOG.md`, the active hermetic plan, and the two Task 3 toolchain files.
  Verdict: no findings.
- Reconciliation: Task 1 and Task 2 are complete in the active plan and ledger. The older memory-residency campaign's stale-target reproduction remains blocked; Task 3 attests the external/toolchain bytes required by the new hermetic path and does not claim target evidence.
- Open gates: Task 3 specification review, code-quality review, target build,
  reproducibility, audit, complete package inventory, 20,100-frame smoke,
  visual, and manual-play evidence. Host tests do not close target gates.
- First review finding: component version/compiler stdout requires explicit
  rejection of embedded absolute POSIX/Windows host paths before canonical
  serialization. Add focused builder and CLI boundary coverage. The review's
  ledger visibility warning is non-defective: this ignored ledger exists and
  is updated for the repair transition without being staged.

## Task 3 review repair round 1 ledger

- Status: `source-complete`; focused repair is committed and independent
  rereview remains open. Both Task 3 review gates and all target evidence gates
  remain open.
- TDD RED: after adding the review-contract tests,
  `.\\.venv-saturn-tools\\Scripts\\python.exe
  tools\\saturn\\test_gen_toolchain_attestation.py` exited 1 with the expected
  two failures (programmatic POSIX/Windows path-bearing versions sealed) and
  four missing CLI reader-injection errors. The failing tests named the missing
  portable-content check and testable CLI boundary.
- TDD GREEN: the same focused command exited 0: `Ran 10 tests ... OK`. It now
  proves POSIX/Windows absolute-path rejection, pin mismatch failure, all eight
  explicit tool arguments reaching the component, and a path-bearing GCC banner
  preserving a prior output without shell invocation.
- Regression: `.\\.venv-saturn-tools\\Scripts\\python.exe
  tools\\saturn\\test_gen_source_closure.py` exited 0: `Ran 14 tests ... OK
  (skipped=1)`; the skip remains the existing host case-collision capability
  limit.
- Hygiene: scoped `git diff --check` passed before commit; final
  `git diff --check b6305500485a83389839c63028af6078ef344bdf..HEAD` and
  `git show --check --format=fuller HEAD` passed.
- Design correction: the single `_validate_component_version()` boundary rejects
  absolute POSIX, drive-qualified Windows, and UNC-like Windows paths in both
  fresh and sealed component metadata. `main()` accepts only an injected
  compiler-version reader for no-shell tests; command-line behavior still uses
  the real GCC invocation by default.
- Repair commit: `0b39b46d2daadd9681ff6b5101e612815e5d3b90`
  (`fix(saturn): reject host paths in toolchain metadata`).
- Self-review: the full `b6305500..0b39b46d` range contains only Task 3's
  changelog, active plan, and toolchain implementation/tests. Verdict: no
  findings; controller rereview remains required.
- Scoped rereview round 1: CLI pin/tool/banner coverage `ADDRESSED`; component
  version-path finding remains open because `file:///...` and `//host/path`
  bypass the validator. No new breakage. Repair round 2 is active; review and
  target gates remain open.

## Task 3 review repair round 2 ledger

- Status: `source-complete`; the round-2 repair is committed and independent
  rereview remains open. Specification/code-quality and all target evidence
  gates remain open.
- TDD: focused `file:///...` (including uppercase `FILE:`) and `//host/path`
  rejection tests were added with ordinary `source/tree` compiler metadata and
  no-overwrite coverage. The old broad slash matcher happened to reject the two
  root forms indirectly, so the ordinary relative-metadata assertion exposed
  the real RED: it exited 1 with an unexpected absolute-path rejection. This
  identified the maintainability defect—accidental slash matching rather than
  explicit path-root recognition.
- TDD GREEN: after replacing that matcher with explicit POSIX-root,
  drive/UNC, local `file:` URI (case-insensitive), and forward-network-root
  forms, `.\\.venv-saturn-tools\\Scripts\\python.exe
  tools\\saturn\\test_gen_toolchain_attestation.py` exited 0: `Ran 13 tests
  ... OK`.
- Regression: `.\\.venv-saturn-tools\\Scripts\\python.exe
  tools\\saturn\\test_gen_source_closure.py` exited 0: `Ran 14 tests ... OK
  (skipped=1)`; the skip remains the existing host case-collision capability
  limit. Scoped and full-range `git diff --check` commands passed.
- Design correction: path rejection is now explicit and narrow enough to allow
  ordinary relative compiler metadata; local file URIs and slash UNC/network
  roots cannot rely on incidental later-slash matches to be rejected.
- Repair commit: `f63e78d915e33898c89b20ada003cb234990e321`
  (`fix(saturn): close toolchain version path bypasses`).
- Self-review: `b6305500..f63e78d` contains only Task 3's changelog, active
  plan, and toolchain implementation/tests; `git show --check` and full-range
  diff check are clean. Verdict: no findings; controller rereview remains
  required.
- Scoped rereview round 2: the remaining version root-encoding finding is
  `ADDRESSED`; no new breakage and no out-of-scope observations. Task 3 status
  is `complete`; specification and code-quality gates are approved. Live
  attestation emission and all target evidence gates remain open for later
  integration tasks.
- Review closeout commit: `d09b358d` (`docs(saturn): close toolchain
  attestation review`).

## Task 4 execution ledger

- Dispatch base: `d09b358d8e9d8321ef30e77c551e670e28d8bd8e`.
- Status: `source-complete`; implementation is ready for controller-owned
  independent specification and code-quality reviews, which remain open.
- Reconciliation: Tasks 1–3 are complete in the active plan and ledger. Historical identity v1 and native-math audit v2/v3 remain immutable; Task 4 may extend only identity v2 and version-aware capture behavior.
- Pre-TDD baseline: generator passed 9 tests. The feature-identity suite then
  produced five setup errors because its pinned fixture predated the already
  required `object_pool_capacity`; the controller classified this as a Task
  4-owned fixture defect. Adding only `object_pool_capacity: 240` restored a
  clean four-suite v1 baseline: 9 + 5 + 35 + 1 = 50 tests.
- TDD RED: all four exact commands exited 1 for the intended absent v2
  contracts. Generator ran 14 tests with three missing-API errors and three
  missing-root failures; feature identity ran 6 with one v2-version failure
  and one 500-byte resolver error; throughput ran 37 with the expected
  500-byte probe resolver error; C/residency ran 2 with the expected compiled
  version/size/field static-assert failures. Follow-up RED cycles proved the
  generated initializer omitted appended roots under v1-only emission (15
  tests, one error) and legacy CLI JSON omitted `effective_config` (16 tests,
  one error).
- TDD GREEN: the four focused commands pass 16 + 6 + 37 + 2 = 61 tests.
  Adjacent identity bootstrap and object-pool capture suites pass 7 + 6 = 13
  additional tests. Final scoped/full-range diff checks and commit are pending
  below until the verification transition completes.
- ABI evidence: the exact historical v1 fixture remains 404 bytes with
  SHA-256 `faa7288b4c9fdf90ae14f01ab3af3752649b8e1ca78d77c47033425c9d68b23f`.
  V2 is 500 bytes; target-profile, package-set, and toolchain roots occupy
  offsets 404, 436, and 468. A host C compilation proves constants, size, and
  offsets. ELF/target capture tests read exactly 404 or 500 bytes and reject
  403, 499, and 501.
- Design correction: retain historical v1 `IDENTITY_STRUCT`/`HASH_FIELDS`
  aliases while using explicit v1/v2 layouts selected from the common `>IHH`
  prefix; reserve `IDENTITY_SIZE` as the requested new-build 500-byte alias.
  V2 JSON receives `effective_config` only after canonical bytes rehash to the
  embedded digest.
- Reuse record: in-tree pattern/extension only. Extended
  `tools/saturn/gen_build_identity.py`, the existing ELF symbol resolver and
  PT_LOAD extraction in `capture_sourceboot_throughput.py`, and the existing C
  ABI/static-assert pattern. No external source was inspected or copied.
- Commits: `09c30c23` (`feat(saturn): add build identity v2`) and
  `5867eb85` (`docs(saturn): record identity v2 source status`).
- Final verification: all four exact focused commands passed post-commit with
  61 total tests (16 + 6 + 37 + 2). `git diff --check d09b358d..HEAD`,
  `git show --check` for both commits, and the scoped staged diff check exited
  0. The full range contains exactly the ten Task 4 tracked paths.
- ABI self-review verdict: no findings. V1 raw bytes retain the exact fixture
  digest; v2 has the three roots at 404/436/468 and size 500; capture resolves,
  extracts, and reads the declared 404/500 bytes; all three root descriptors
  reseal `effective_config_hash` and all three embedded root mutations are
  rejected. The `d09b358d..HEAD` range has no diff in native-math audit v2/v3
  contracts or their comparison implementation/tests.
- Independent review verdict: open; controller owns both specification and
  code-quality reviews. No review is claimed from host tests or self-review.
- Open gates: v1 byte-compatibility review, v2 layout/offset review, dynamic
  capture-size review, all-root mutation review, code-quality review, target
  build, reproducibility, audit v4, complete package inventory, 20,100-frame
  smoke, visual, and manual-play evidence. Host tests do not close target gates.
- First independent combined review: `Needs fixes`. Critical finding: ELF
  symbol resolution and identity extraction must use one immutable snapshot.
  Important findings: strict duplicate/case-collision/unknown-key rejection for
  v2 descriptor JSON and canonical compact CLI manifest bytes. Minor finding:
  explicit `(1,500)` / `(2,404)` mismatch tests. Repair round 1 is active;
  review and target gates remain open.

## Task 4 review repair round 1

- Status: `source-complete`; independent rereview remains open. The first
  review verdict stays `Needs fixes` until the controller's rereviewer clears
  every finding.
- Snapshot TDD RED: throughput ran 38 tests with the new mutable-path regression
  failing because `build_elf_build_identity_probe()` read the source ELF twice.
  The first read supplied symbol metadata; after path replacement, the second
  supplied different PT_LOAD bytes.
- Snapshot GREEN: the same 38 tests pass after a single immutable byte buffer
  became the input to both symbol resolution and PT_LOAD extraction. Existing
  404/500 exact-size and target-read tests remain green.
- Strict-input TDD RED: generator ran 19 tests with nine expected failures:
  all three programmatic v2 root descriptors accepted unknown/case-colliding
  keys, and CLI JSON accepted duplicate top-level, duplicate nested, and
  case-colliding nested keys through last-key-wins parsing.
- Strict-input GREEN: all 19 passed after v2-only exact descriptor validation
  and recursive `object_pairs_hook` rejection. A characterization test proves
  historical v1 programmatic descriptors still ignore extra metadata.
- Canonical-output TDD RED: generator ran 21 tests with two exact-byte failures
  because v1 and v2 CLI manifests were pretty-printed. GREEN passes all 21
  with sorted compact ASCII JSON and exactly one trailing newline. The same
  suite explicitly rejects crossed common-prefix pairs `(2,404)` and `(1,500)`.
- Full GREEN: focused suites pass 21 + 6 + 38 + 2 = 67 tests; adjacent identity
  bootstrap and object-pool capture suites pass 7 + 6 = 13 tests.
- Design corrections: identity probing owns one immutable ELF byte generation;
  strict descriptor shape applies only to new v2 roots so v1 programmatic
  behavior is unchanged; CLI ambiguity rejection applies recursively to every
  JSON object; emitted identity JSON uses the portfolio canonical-byte rule.
- Reuse mode remains in-tree pattern/extension; no external source was
  inspected or copied.
- Repair commit: `7aef88c4` (`fix(saturn): harden identity v2 inputs`).
- Repair source-status commit: `1582467c` (`docs(saturn): record identity v2
  repair status`).
- Post-commit verification: all four exact focused suites pass 67 tests and the
  two adjacent suites pass 13 tests. `git diff --check d09b358d..HEAD` and
  `git show --check` for both repair commits exit 0. The full Task 4 range still
  contains exactly its original ten tracked paths.
- Repair self-review verdict: no findings. The identity probe has one source
  read and one byte generation; strict parsing is recursive; v2-only descriptor
  shape does not alter v1 compatibility; manifest bytes are compact/sorted
  ASCII plus one newline; crossed prefixes fail. Native-math audit v2/v3
  contracts and comparison code/tests have no `d09b358d..HEAD` diff.
- Independent rereview and target build, reproducibility, audit v4, complete
  package inventory, 20,100-frame smoke, visual, and manual-play gates remain
  open. Host tests do not close target gates.
- Scoped rereview: all four findings `ADDRESSED`; no new breakage and no
  out-of-scope observations. Task 4 status is `complete`; v1 byte-compatibility,
  v2 ABI/layout, dynamic capture, mutation, specification, and code-quality
  gates are approved. Target build and every release-evidence gate remain open.
- Review closeout commit: `b0c7fa03` (`docs(saturn): close identity v2
  review`).

## Task 5 execution ledger

- Dispatch base: `b0c7fa03437e54019803581f5652d099e85029b4`.
- Status: `source-complete`; behavior commit is recorded and controller-owned independent reviews remain pending.
- Reconciliation: Tasks 1–4 are complete and review-approved. Task 5 consumes only their validated profile/package/closure/toolchain/identity interfaces; it must remove the stale broad recursive source-root behavior without claiming a target build.
- TDD RED: `.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_sourceboot_identity_spec_bootstrap.py` exited 1 with six expected missing-v2-interface errors and the expected surviving `SOURCE_CLOSURE_ROOTS` failure; the CLI also reported the new inputs as unrecognized.
- TDD GREEN: the same focused command exited 0: `Ran 8 tests ... OK`. It proves unselected capture/test/evidence invariance, independent closure/profile/package/toolchain resealing, exact configuration equality, release-disabled full-profile rejection, nine legacy class mappings, aggregate-only texture coverage, required CLI inputs, and stale source/class manifest no-overwrite behavior.
- Regressions: `.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_gen_build_identity.py` exited 0 (`Ran 21 tests ... OK`), and `.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_target_profile.py` exited 0 (`Ran 11 tests ... OK`). Scoped `git diff --check` passed.
- Design correction: identity v2 consumes the canonical source-closure manifest directly as `source_hash`; every other legacy artifact field is the matching generated package-class aggregate. Texture has no invented v1 field and remains covered by the package-set root. The broad recursive bootstrap closure and all generated-source enumeration helpers are deleted with no fallback.
- Reuse record: in-tree close-port/pattern-only adaptation of `bootstrap_sourceboot_identity_spec.py`, `target_profile.py`, `gen_source_closure.py`, `gen_toolchain_attestation.py`, `hermetic_manifest.py`, and `gen_build_identity.py` at base `b0c7fa03`. No external source was inspected or copied.
- Commits: `992bfa7d2e4e33727a4809148a495ae924760163` (`feat(saturn): compose hermetic sourceboot identity`) and `2f220ae62d5d6bcde73675c77a52fb060071706c` (`docs(saturn): record hermetic bootstrap source status`). Post-commit `git show --check` and full-range `git diff --check b0c7fa03..HEAD` passed; the range contains exactly the changelog, active plan, bootstrap implementation, and focused bootstrap tests.
- Self-review: no findings. The old broad closure/constants/helpers are absent; all input/generated descriptors are validated and rehashed before final publication; exact config drift and release-disabled profiles fail before spec replacement; identity v1 implementation/parsing has no Task 5 diff. Independent specification and code-quality reviews remain controller-owned/open.
- Final post-commit verification repeated all three exact suites successfully: 8 + 21 + 11 = 40 tests. Both commits pass `git show --check`; `b0c7fa03..HEAD` passes full-range diff check and contains only the four Task 5 tracked paths. Searches confirm no broad-root symbol/helper/import remains and no identity-v1 or native-math audit file changed.
- Open gates: Task 5 independent reviews, target build, reproducibility, audit v4, complete package inventory, 20,100-frame smoke, visual, and manual-play evidence. Host tests do not close target gates.
- First review: `Needs fixes`. Critical findings are exact-snapshot
  validation/hashing and transactional preservation of every prior sibling on
  failure. Important finding: repository-relative forward-slash descriptor
  paths plus canonical compact spec bytes. Minor finding: reset root-mutation
  fixtures and assert only the intended root changes. The review initially
  requested live toolchain binary remeasurement, then adjudicated that Task 6's
  explicit post-link Task 3 verifier is the sufficient owner; Task 5 remains
  responsible for validating/rehashing the attestation document snapshot.
- Repair round 1 is active; Task 5 review and every target/release gate remain
  open.

## Task 5 review repair round 1

- Status: `source-complete`; focused repair is implemented and controller-owned rereview remains open. The first review verdict stays `Needs fixes` until every valid finding is independently cleared.
- Adjudication: live compiler/header remeasurement is withdrawn from Task 5. This compositor validates and hashes one exact toolchain-attestation document snapshot; Task 6 runs Task 3's live verifier against current binaries/external dependencies after link and before release sealing.
- TDD RED: after adding the four repair contracts, `.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_sourceboot_identity_spec_bootstrap.py` exited 1: `Ran 12 tests` with seven intended failures—three snapshot generations were silently rebound after validation, a staged-validation failure corrupted the prior actor sibling, prior/new output sets survived partial publication, and spec bytes were indented/absolute-path-dependent.
- TDD GREEN: the same focused command exited 0: `Ran 12 tests ... OK`. It proves immutable source/profile/toolchain snapshots and end-of-composition digest checks, complete-set preservation/removal on injected validation/publication failures, canonical repository-relative relocation-equivalent specs, and independently reset full-root mutation vectors.
- Regressions: identity generation passed 21 tests and target-profile resolution passed 11 tests. Current total is 44 host tests with no failures/errors/skips; scoped diff check passed.
- Design correction: composition occurs entirely from a captured target-profile snapshot and staged generated outputs. Identity validation reads staged copies with the final relative descriptor hashes; originals and staged bytes are rechecked immediately before the transaction. Publication snapshots every prior target, writes spec last, and restores prior bytes/removes new files on any exception.
- Repair commits: `ec546de2609708b2c39a343dfc57d4893a6fd87b` (`fix(saturn): publish identity inputs transactionally`) and `9d6a394a1a80cdfda68b1809acd1990349b23a25` (`docs(saturn): record identity publication repair`). Post-commit `git show --check` and full-range `git diff --check b0c7fa03..HEAD` passed; the range remains restricted to the changelog, active plan, bootstrap implementation, and focused bootstrap tests.
- Independent rereview plus target build, reproducibility, audit v4, complete package inventory, release manifest, 20,100-frame smoke, visual, and manual-play gates remain open.

## Task 7 execution ledger

- Initial dispatch base: `48f5a61a` (`docs(saturn): start exact release
  sealing`), following Task 6 closeout `cad2e90d`.
- Status: `complete`; behavior and all three scoped repair rounds are committed,
  and independent specification/code-quality review is approved.
- Reconciliation: Tasks 1–6 are complete and independently approved. Task 7
  consumes the reviewed identity/profile/closure/toolchain outputs and must use
  one profile-agnostic path for the BOB demo and eventual full game.
- Reference-code-first record: in-tree close-port/pattern-only reuse from
  `hermetic_manifest.py`, `gen_build_identity.py`,
  `bootstrap_sourceboot_identity_spec.py`, existing capture preflights, both
  sourceboot Makefiles, and focused tests at base `48f5a61a`. Pinned Yaul
  `6012f79f237773378c8014e70d8998ad95a38d98` (MIT), files
  `libyaul/build/build.post.iso-cue.mk` and `build.post.bin.mk`, informed the
  post-build ordering. No external source was copied.
- TDD RED: release/staging started with two absent-module errors (12 cases with
  ten expected skips). Capture/Make RED then produced 2 throughput, 1
  occupancy, 1 HUD, 1 desktop, and 2 Make failures. Self-review RED added one
  malformed-closure-schema failure and one mismatching-v1-spec failure.
- GREEN: canonical root-neutral sealing verifies exact output bytes, ELF
  identity, effective config and profile/closure/package/toolchain roots plus
  exact CUE/ISO binding. Staging verifies before mutation, creates only new
  files, copies the manifest last, and re-verifies the staged tree. Throughput,
  occupancy, HUD, and desktop launch verify the manifest before emulator/SH
  tool use and bind their reports; v2 occupancy uses manifest-owned capacity,
  while explicit v1 compatibility also proves the spec's identity bytes.
  Make seals after `verify-sealed-inputs` and includes `verify-release` in the
  outer verification path.
- Design correction: `build_release_manifest(root=...)` treats `root` as the
  Git repository root; output-relative paths are derived from the CUE/manifest
  directory because pinned Yaul emits the ELF and `SOURCE.DAT` in its `obj/`
  child. This preserves canonical host-root neutrality without escaping paths.
- Behavior commit: `7195fc48` (`feat(saturn): seal and stage exact releases`).
- Source-status commit: `ec83cc4e` (`docs(saturn): record exact release source
  status`).
- Fresh post-commit verification: all seven exact suites pass 9 + 4 + 40 + 10
  + 3 + 7 + 7 = 80 host tests; adjacent identity generation, identity-spec
  bootstrap, boot-trace capture, and route-view suites pass 21 + 15 + 16 + 1 =
  53 host tests. There are zero failures, errors, or skips. Six changed
  production scripts pass `py_compile`. One earlier adjacent command used two
  wrong filenames; its result was discarded and replaced by the fail-fast
  53-test rerun.
- Self-review: no remaining finding after the strict closure-row and exact v1
  identity-spec repairs. Scoped staged diff check passed before commit. No real
  SH-2 build, emulator capture, or target evidence was run or claimed.
- Final scoped checks: `git show --check` passes for both commits;
  `git diff --check 48f5a61a..ec83cc4e` passes; the range contains exactly the
  18 Task 7 tracked paths; and the index is empty. Unrelated dirty/untracked
  paths remain preserved.
- Open gates: Task 7 independent reviews, real target build, reproducibility
  comparison, audit v4, complete-package inventory,
  20,100-frame smoke, visual, and manual-play evidence. Host results do not
  close target gates.
- First independent review: `Needs fixes`, with five Important findings.
  Preserve immutable verified manifest/output snapshots through capture and
  staging, and roll back every partial staging write; semantically bind profile
  effective config/output names; reject duplicate/casefold/alias collisions in
  closure/package/toolchain/output schemas; make the supported writer produce
  a real v1-compatible manifest; and compare only canonical identity inputs and
  output bytes while ignoring host layout/non-identity metadata. Repair round 1
  and rereview are open; no target gate is affected.

## Task 6 execution ledger

- Initial dispatch base: `eded1a505790432cbbdca29414b11dfe8bac390f`.
- Reconciled implementation base: `57da18b7` (`docs(saturn): reconcile
  hermetic build cli contracts`).
- Status: `complete`; Make-stage integration and scoped independent reviews are
  approved. Real target/release-evidence gates remain pending in later tasks.
- Reconciliation: Tasks 1–5 are complete and review-approved. Task 6 integrates their CLIs into assets/discovery/seal/build/post-link verification without claiming a real SH-2 build; Task 9 owns reproducible target construction.
- Open gates: Task 6 source completion and reviews, real target build, reproducibility, audit v4, complete package inventory, release manifest, 20,100-frame smoke, visual, and manual-play evidence.
- Pre-implementation interface reconciliation: Task 2 had no CLI, while Task
  6 assumed one; Task 3's reviewed CLI is flag-style rather than the plan's
  example subcommands. Task 6 now owns a Task 2 `build`/`verify` CLI and
  external-dependency handoff plus a backward-compatible Task 3 handoff-file
  reader. Make will invoke Task 3 through its existing `--output` / `--verify`
  interface. The plan correction is committed; implementation may proceed.
- Status transition: `source-complete`; behavior commit and controller-owned
  independent specification/code-quality reviews remain open.
- TDD RED: the new Make suite ran 5 tests with four failures and one error for
  absent discovery-stage/order/prefix-map contracts. The closure suite failed
  importing absent `main`; the attestation suite produced two intended
  unrecognized-argument errors for `--external-dependencies`.
- TDD GREEN: exact suites pass 5 Make + 15 composition + 16 closure + 15
  attestation = 51 host tests. Closure has one legitimate case-spelling skip
  on this host filesystem. The dry run expands 227 unique dependency scans:
  223 C and 4 `.sx`, with exact flags minus only `-save-temps=obj`, identical
  specs, and all three repository prefix maps.
- Design correction: generated `.sx` inputs moved from identity-tagged object
  directories to stable `build/saturn/sourceboot/generated` paths. This breaks
  the identity/discovery cycle while keeping generated-input ownership and the
  same source paths in discovery and build.
- Integration: Task 2 now has additive `build`/`verify` CLI subcommands and a
  strict absolute-path diagnostic handoff. Task 3 reads it through additive
  `--external-dependencies` while retaining `--output`, `--verify`, and
  repeated `--external-dependency`. Make orders assets, discovery, seal/tag,
  build, and Task 2/Task 3 post-link verification.
- Reuse record: in-tree close-port/pattern extension at `57da18b7`, inspecting
  Task 2/3/5 tools, sourceboot Make/spec/linker rules, the outer Makefile, and
  pinned Yaul `build.pre.mk`, `build.post.bin.mk`, and
  `build.post.iso-cue.mk` at commit
  `6012f79f237773378c8014e70d8998ad95a38d98`. No external source was copied.
- Commits: plan/interface correction `57da18b7`; Task 6 behavior commit
  `b1d75772` (`feat(saturn): integrate hermetic sourceboot sealing`); Task 6
  source-status commit `adfd68dd` (`docs(saturn): record hermetic build source
  status`).
- Final post-commit verification: all four exact suites pass 5 + 15 + 16 + 15 =
  51 host tests, with one legitimate case-spelling skip on Windows. Both
  `git show --check` and `git diff --check 57da18b7..adfd68dd` pass; the range
  contains only the 11 Task 6 tracked paths and the index is empty.
- Self-review verdict: no findings across exact flag/spec parity, stage
  isolation, 223 C + 4 `.sx` coverage, derived-output cycle breaking,
  post-link dependency/external equality, release cleanliness, Task 3 live
  verification, and stale-spec failure.
- First independent review verdict: `Needs fixes`; repair round 1 is active.
  Critical: `.sx` discovery/post-link scans add `SH_SPECS` while Yaul's real
  `.sx` compile does not, so the sealed dependency closure can differ from the
  assembled source. Important: force/key fresh discovery when preprocessing
  configuration changes; add exact C++ discovery and all three C++ prefix maps;
  reject closure/handoff output aliasing before I/O; replace token-level dry-run
  checks with exact expanded inventory and normalized argv-parity coverage.
  No target build, reproducibility, audit v4, complete-package inventory,
  release manifest, 20,100-frame smoke, visual, or manual-play evidence is
  claimed; every real gate remains open.
- Repair round 1 status: `complete` in `b2ea338c` (`fix(saturn): match
  hermetic scans to Yaul`) after scoped rereview approved all findings.
- Repair TDD RED: four focused cases failed for the reviewed reasons—resolved
  closure/handoff aliases were accepted and overwrote one path; `.sx` discovery
  contained the extra C-only `sourceboot.specs`; a materialized cached `main.c`
  depfile suppressed discovery after `SATURN_DIAGNOSTIC_MODE` drift; and an
  injected existing repository `.cpp` was absent from the scan inventory.
- Repair GREEN: checked-in pinned Yaul recipes are expanded and normalized
  against discovery after removing only dependency/compile output switches and
  `-save-temps=obj`. Default equality is exactly 223 C + 4 `.sx`, including 4
  post-link `.sx` scans; the injected model is 228 sources with exactly one C++
  command using SH G++, `SH_CXXFLAGS`, both spec lists, and all three maps.
  Exact suites pass 7 + 15 + 17 + 15 = 54 host tests, with one legitimate
  Windows case-spelling skip.
- Repair design: C, C++, and `.sx` scanners are distinct; a phony discovery
  prerequisite prevents depfile reuse across preprocessing changes; and Task 2
  rejects resolved output aliasing before closure construction or publication.
  No SH-2 build was run; rereview and every target/release gate remain open.
- Repair commits: `b2ea338c` (`fix(saturn): match hermetic scans to Yaul`) and
  `8e657d50` (`docs(saturn): record hermetic scan repair`). Fresh post-commit
  execution repeats all four exact suites at 54 tests with one skip. Both
  commits pass `git show --check`; `git diff --check 81fecf32..8e657d50`
  passes, the range contains only the seven Task 6 tracked paths, and the index
  is empty.
- Repair self-review: no new findings across language-specific flag/spec parity,
  forced freshness, exact inventories, C++ maps/specs, post-link `.sx`, alias
  rejection before writes, stage/profile/mode/cycle-breaking preservation, and
  release verification ordering.
- Scoped rereview verdict: `.sx` parity, forced-fresh discovery, exact C++
  support, pre-I/O output-alias rejection, and strengthened Make proof are all
  `ADDRESSED`; no new Critical or Important breakage was found. Task 6 is
  `complete`. Real SH-2 build, reproducibility, audit v4, complete-package
  inventory, release manifest, 20,100-frame smoke, visual, and manual-play
  gates remain open.

## Task 7 review repair round 1

- Status: `source-complete` in behavior commit `bb5a840d`
  (`fix(saturn): harden exact release snapshots`); controller-owned scoped
  rereview remains open and the first `Needs fixes` verdict remains effective.
  Source status is commit `eccb815b`
  (`docs(saturn): record exact release snapshot repair`).
- TDD RED: the initial review suite produced 10 intended release failures or
  errors, 3 staging failures/errors, and one each in throughput, occupancy,
  HUD, and desktop (17 total). Focused self-review subsequently observed one
  cross-component toolchain collision failure and one post-preflight hardlink
  alias race failure before their fixes.
- GREEN: all seven exact suites pass 24 + 9 + 41 + 12 + 4 + 9 + 7 = 106 host
  tests; adjacent identity/bootstrap/boot-trace/route-view suites pass
  21 + 15 + 16 + 1 = 53. There are zero failures, errors, or skips, and all six
  changed production scripts pass `py_compile`. The same exact, adjacent, and
  compile checks passed again after the behavior commit.
- Design corrections: verification owns private manifest/output snapshots
  through consumers; desktop cleanup follows a still-live Ymir PID; staging
  rollback removes only identity-proven owned paths; schema paths are
  forward-slash/case-fold host neutral; profile config is mapped semantically
  to the canonical identity schema; real v1 writing derives config from the
  profile; and comparison excludes layout/provenance metadata from equality.
- Scoped precommit checks: the behavior index contained exactly 15 Task 7
  paths and passed `git diff --cached --check`. All unrelated dirty and
  untracked paths remain preserved.
- Open gates: controller rereview, real target build, reproducibility, audit
  v4, complete-package inventory, release evidence, 20,100-frame smoke,
  visual, and manual-play. No real SH-2 or emulator evidence was run or
  claimed.

## Task 7 review repair round 2

- Status: `source-complete` in behavior commit `00736856`
  (`fix(saturn): close release namespace races`); controller-owned rereview
  remains open. Round-1 rereview findings 2–5 stay `ADDRESSED`; this repair is
  the proposed closure for the namespace-race portion of Finding 1.
- TDD RED: 39 focused release/stage cases produced seven intended failures or
  errors covering a coherent manifest replacement in the check/open gap,
  exact-inventory extras, deep ancestor replacement, a real Windows junction,
  concurrent contamination after initial emptiness, a post-publication extra,
  and an owned-file foreign replacement retained during failure handling.
- GREEN: focused release/stage passes 26 + 14 = **40 host tests**. All seven
  exact suites pass 26 + 14 + 41 + 12 + 4 + 9 + 7 = **113**, adjacent
  identity/bootstrap/boot-trace/route-view suites pass 21 + 15 + 16 + 1 =
  **53**, and six production scripts pass `py_compile`, with zero failures,
  errors, or skips. One module-form adjacent invocation could not resolve the
  route-view script-local import and was discarded; the required direct-script
  rerun produced the totals above.
- Design correction: canonical JSON is parsed and hashed only from a no-follow
  opened descriptor after before/open/after object checks and full-ancestor
  validation. Staging assembles and exactly verifies a manifest-last private
  sibling, publishes with atomic no-replace namespace operations, pins Windows
  directories against rename/delete sharing, and re-verifies exact inventory.
  A proven empty backup is removed by Windows handle; ambiguous content is
  quarantined whole, named in diagnostics, and never path-unlinked. The
  requested missing or empty destination state is restored and retry succeeds.
- Reference reuse remains the Task 7 in-tree pattern-only record at base
  `48f5a61a` plus pinned Yaul `6012f79f237773378c8014e70d8998ad95a38d98`
  (MIT) post-build ordering. No external code was copied; the namespace guard
  is a clean-room platform adaptation because no named release helper provides
  the required Windows object-identity boundary.
- Open gates: controller rereview, real target build, reproducibility, audit
  v4, complete-package inventory, release evidence, 20,100-frame smoke,
  visual, and manual-play. No real SH-2 build or emulator evidence was run or
  claimed.

## Task 7 review repair round 3

- Status: `complete` in behavior commit `740a08bc`
  (`fix(saturn): dispatch atomic release publication`) after scoped rereview.
  The original namespace-race finding and the POSIX portability finding are
  both `ADDRESSED`.
- TDD RED: the 20-case staging suite produced nine intended failures/errors:
  Linux/Darwin/FreeBSD adapter selection and exclusive flags (three),
  Linux/Darwin/OpenBSD `EEXIST` mapping (three), unsupported POSIX preflight,
  platform help, and the stable retained-empty-backup warning.
- GREEN: focused release/stage passes 26 + 20 = **46 host tests**. All seven
  exact suites pass 26 + 20 + 41 + 12 + 4 + 9 + 7 = **119**; adjacent
  identity/bootstrap/boot-trace/route-view suites pass 21 + 15 + 16 + 1 =
  **53**; all six production scripts pass `py_compile`, with zero failures,
  errors, or skips.
- Design correction: an immutable atomic-rename adapter is resolved before any
  staging namespace mutation and reused through publish/quarantine/restore.
  Windows uses exclusive rename; Linux requires directory-relative
  `renameat2` flag `RENAME_NOREPLACE=1`; Darwin/BSD-family hosts require libc
  `renameatx_np` flag `RENAME_EXCL=4`. Missing symbols and other platforms fail
  closed. Path-only `renamex_np` is deliberately rejected because it abandons
  the guarded parent descriptor. `EEXIST` is normalized to `FileExistsError`.
- Backup ownership: Windows removes the proven preexisting-empty backup only by
  its identity-checked retained handle. Other platforms retain the empty sibling
  at `.sm64-saturn-quarantine-<destination>-<unique-id>` with a stable warning;
  missing-destination success creates no backup. Exact destination inventory
  and repeat staging after restoration to empty are tested.
- Platform reference inspection: Apple Developer's APFS Tools and APIs page
  documents the directory-relative `renameatx_np` signature, while Apple's
  exclusive-renaming resource documentation binds `RENAME_EXCL`; official
  OpenBSD and NetBSD `rename(2)` manuals show only replacing `renameat` on those
  documented versions. Reuse mode is API-contract/pattern-only; no external
  source was copied, and no Linux/macOS/BSD execution is claimed.
- The behavior index contained exactly four Task 7 paths (implementation, test,
  BUILDING, and CHANGELOG) and passed `git diff --cached --check`.
- Open gates: controller rereview, real target build, reproducibility, audit
  v4, complete-package inventory, release evidence, 20,100-frame smoke,
  visual, and manual-play. No target or emulator work was run or claimed.
- Scoped rereview verdict: portability finding `ADDRESSED`; no new Critical or
  Important breakage. Task 7 is `complete`. Real target build, non-Windows
  platform execution, reproducibility, audit v4, complete-package inventory,
  release evidence, 20,100-frame smoke, visual, and manual-play gates remain
  open.

## Task 8 execution ledger

- Initial implementation base: `15265084` (`docs(saturn): start release-bound
  audit v4`), following Task 7 review closeout `a07ffbe1`.
- Status: `source-complete` in behavior commits `db4c620d`
  (`feat(saturn): add release-bound native math audit v4`) and `9fcc9632`
  (`fix(saturn): harden audit v4 publication`). The first independent review
  returned `Needs fixes`; repair round 1 is source-complete and controller-owned
  rereview remains open, so Task 8 is not `complete`.
- Scope boundary: no real measurement, real v4 contract, or pinned digest in
  Task 8. Historical v2/v3 contract bytes and invocation behavior must remain
  unchanged. Task 9 owns target measurement and pinning.
- Reference record: pattern-only/close-port reuse from the in-tree audit
  parser/integrity/preflight/main and tests at `15265084`, plus Task 7's
  context-managed `release_manifest.py` snapshots and release fixture at
  reviewed closeout `a07ffbe1`. Source and destination are the same deliberately
  GPL-compatible project, with no root-wide license declaration; no external
  source was copied and no new notice obligation was introduced.
- TDD RED/GREEN: the first parser case exited 1 on an invalid new directive;
  the sealer suite exited 1 on the absent module. Measurement then produced two
  tests/four expected errors because both flags were unknown. Focused parser,
  preflight, integrity, immutability, and snapshot cases passed after the
  minimal implementation; measurement passes two CLI/report/lifetime cases;
  and the sealer passes four cases. Self-review added a snapshot-consumption
  mutation: RED showed public v4 preflight hashing the mutable original, GREEN
  showed it hashing only the private snapshot and releasing it afterward.
- Verification: the fresh full verifier ran 238 tests with 237 passes and only
  the documented unrelated null-camera proof failure. The fresh one-shot
  sealer passes 4/4. Task 7 release-manifest adjacency passes 26/26 with
  worktree-local `TEMP`/`TMP`; the first adjacent attempt was discarded after
  the sandbox denied traversal of global `C:\Users` temp paths. All four
  changed scripts pass `py_compile`. `git diff --cached --check` and
  `git show --check db4c620d` pass, and the behavior commit contains exactly
  the five Task 8 behavior paths.
- Historical immutability: audit v2 remains 507 bytes with SHA-256
  `87dabb51adc1c1cb6b646a826977658de305df086d1cfb21fc2c97a0bd6127e2`;
  audit v3 remains 416 bytes with SHA-256
  `80f662863f6af8c8d905717cc06504677eedf144e2f00eff7b254ee7e099cba5`.
- Design correction: both CLI measurement and the public v4 preflight require
  the requested ELF path to equal Task 7's verified original, then keep and
  consume only the immutable verified snapshot through all digest and SH-tool
  activity. Identity v1 is rejected only for v4/measurement; historical v2/v3
  invocation behavior remains manifest-free and unchanged.
- Open gates: Task 8 independent reviews, real target build, reproducibility, audit
  v4 measurement/contract, complete-package inventory, release evidence,
  20,100-frame smoke, visual, and manual play.
- First independent review: one Critical and two Important findings. The v4
  sealer exposes partial final bytes and raceably unlinks by path on failure;
  measurement output lacks fail-before-tool alias rejection against its inputs;
  and `--release-manifest` is accepted in historical modes where it silently
  changes the target. Repair round 1 is implemented and rereview remains open.

## Task 8 review repair round 1

- Status: `source-complete` in behavior commit `9fcc9632`; this source-status
  update is recorded in the following docs commit. The first `Needs fixes`
  verdict remains effective until controller-owned scoped rereview clears all
  three findings.
- Reference/reuse: directly reused Task 7's in-tree, reviewed
  `DirectoryNamespaceGuard` and platform-dispatched `_rename_noreplace`
  primitive from `release_manifest.py` / `stage_saturn_release.py` at closeout
  `a07ffbe1`. Reuse mode is same-project close-port/dependency; no external
  source, license, attribution, or notice obligation was added.
- TDD RED: the expanded sealer suite had six review-specific failures/errors:
  final-path visibility during partial/crash writes, absent publish-race and
  rename-replacement retention, and two input-hardlink alias cases. The CLI
  matrix reached Task 7 verification in ordinary/v2/v3 modes and returned the
  wrong object-reference-only diagnostic. Alias cases were then unmasked from
  unrelated integrity pins and proved premature release verification.
- TDD GREEN: sealer passes 11/11, including partial/slow/crash/concurrent-reader,
  file/directory fsync, preexisting output, publish race, rename-replacement
  retention, and input alias contracts. Measurement rejects exact aliases for
  ELF, baseline, route oracle, audit route oracle, audit contract, and release
  manifest plus `..`, Windows casefold spelling, hardlink, and symlink where
  supported; input bytes remain unchanged and Task 7 verification/SH tools are
  not called. Ordinary, object-reference-only, v2, and v3 release-manifest
  combinations fail before tool activity; v4 and measured-unsealed behavior
  remains release-bound to identity v2 snapshots.
- Verification: full verifier is 240 tests, 239 pass, with only the preserved
  `test_pinned_bob_null_camera_trigger_proof_removes_only_exact_two_sites`
  failure. Task 7 release-manifest and staging adjacency pass 46/46; the first
  sandboxed attempts errored before assertions because Windows ancestor handle
  access was denied, and the approved unsandboxed rerun passed. Changed
  production scripts pass `py_compile`; scoped cached diff/check was clean.
- Historical immutability: audit v2 remains 507 bytes / SHA-256
  `87dabb51adc1c1cb6b646a826977658de305df086d1cfb21fc2c97a0bd6127e2`;
  audit v3 remains 416 bytes / SHA-256
  `80f662863f6af8c8d905717cc06504677eedf144e2f00eff7b254ee7e099cba5`.
  `GOAL_AUDIT_CONTRACT_V4_SHA256` remains `None`; no measurement, real v4
  contract, target build, emulator run, or target evidence was created.
- Repair self-review: no remaining direct final-path write/unlink, ambiguous
  cleanup, output/input alias, release-manifest mode, mutable-original ELF, or
  snapshot-lifetime finding. Controller rereview plus real target build,
  reproducibility, audit-v4 measurement/contract, complete-package inventory,
  release evidence, 20,100-frame smoke, visual, and manual-play gates remain
  open.

## Task 8 review repair round 2

- Status: `active`. Round-1 scoped rereview marked release-manifest mode
  legality `ADDRESSED`; the first `Needs fixes` verdict remains effective
  because exact-object publication and late measurement-output aliasing remain
  open.
- Sealer residual race: the checked private leaf can be replaced between its
  last identity check and the name-based no-clobber rename. A successful rename
  of that substituted leaf is not followed by a proof that the published object
  is the exact held/staged object.
- Measurement residual race: output alias rejection occurs only at preflight.
  A late symlink or hardlink replacement before the eventual write can redirect
  publication into an input after release verification or tool activity.
- Required correction: publish the exact opened or namespace-immutable private
  object with an atomic exclusive primitive, and publish measurement output
  exclusively so a late alias can only fail. Deterministic race tests must prove
  inputs and foreign state remain unchanged. Reuse the reviewed Task 7/shared
  path-identity primitives and preserve cross-platform fail-closed behavior.
- Open gates: scoped rereview after repair, real target build, reproducibility,
  real audit-v4 measurement/contract/pin, complete-package inventory, release
  evidence, 20,100-frame smoke, visual, and manual play. No target or emulator
  evidence is claimed.

## Task 5 review repair round 2

- Status: `source-complete`; repair round 2 is implemented and controller-owned rereview remains open. The first `Needs fixes` verdict remains effective until both remaining findings clear.
- TDD RED: after adding staged-profile generation and exhaustive rollback contracts, `.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_sourceboot_identity_spec_bootstrap.py` exited 1: `Ran 14 tests` with exactly two failures. A post-resolver staged-profile mutation published, and an injected restore failure replaced the original publication exception with `RuntimeError`.
- TDD GREEN: the same command exited 0: `Ran 14 tests ... OK`. The staged profile is digest-checked immediately before and after resolver consumption and at final prepublication revalidation. Rollback performs real writes before the injected publication failure, attempts every target despite one restore failure, preserves the original exception type/message with diagnostic notes, and removes owned class/profile/package/spec `.tmp` files on success and both failure paths.
- Regressions: identity generation passed 21 tests and target-profile resolution passed 11 tests. Current total is 46 host tests with no failures/errors/skips; scoped diff check passed.
- Design correction: a dedicated staged-profile snapshot owns the resolver input generation. Publication cleanup runs before and after writes and after rollback; restore/remove and cleanup failures are accumulated as notes on the original exception without changing its type or stopping later attempts.
- Repair commits: `c1612fd8c9e8ae595595255cbd6dd25b30eb375e` (`fix(saturn): preserve identity publication failures`) and `727c4075e0c0c5bca1a191a77e91c09d8f9344d3` (`docs(saturn): record identity rollback repair`). Post-commit `git show --check` and full-range `git diff --check b0c7fa03..HEAD` passed; the range remains restricted to the four Task 5 tracked paths.
- Independent rereview plus target build, reproducibility, audit v4, complete package inventory, release manifest, 20,100-frame smoke, visual, and manual-play gates remain open.
