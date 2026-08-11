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
- Status: `complete` in behavior commits `db4c620d`
  (`feat(saturn): add release-bound native math audit v4`) and `9fcc9632`
  (`fix(saturn): harden audit v4 publication`). The first independent review
  returned `Needs fixes`; repairs `9fcc9632` and `2277c3e2` closed all three
  findings. Final scoped rereview marked the two residual races `ADDRESSED`,
  confirmed release-manifest mode legality remained addressed, and found no
  new Critical or Important breakage.
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
- Open gates: real target build, reproducibility, audit v4 measurement/contract,
  complete-package inventory, release evidence,
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

- Status: `complete` in behavior commit `2277c3e2`
  (`fix(saturn): publish exact audit objects`) and source-status commit
  `1ef1c99e`. Round-1 scoped rereview marked release-manifest mode legality
  `ADDRESSED`; final scoped rereview marked exact-object publication and late
  measurement-output aliasing `ADDRESSED` and found no new Critical or
  Important breakage. The first `Needs fixes` verdict is cleared.
- Round-1 sealer finding addressed in source: the checked private leaf could be replaced between its
  last identity check and the name-based no-clobber rename. A successful rename
  of that substituted leaf is not followed by a proof that the published object
  is the exact held/staged object.
- Round-1 measurement finding addressed in source: output alias rejection
  occurred only at preflight, so a late symlink or hardlink replacement before
  the eventual write could redirect publication into an input after release
  verification or tool activity.
- Design correction: shared `path_identity.publish_new_bytes` completely writes
  and fsyncs a privately owned same-directory object, then atomically publishes
  that exact held object without replacement. Windows uses handle-bound
  `FileRenameInfo` while Task 7's `DirectoryNamespaceGuard` pins the parent;
  POSIX uses an exclusive hardlink from the held descriptor through
  `/proc/self/fd` or `/dev/fd` and fails closed if neither facility works.
  Publication is followed by identity/size proof. Ambiguous private state is
  retained with a diagnostic; final or foreign paths are never unlinked.
- TDD RED/GREEN: deterministic last-check substitution initially published
  foreign bytes; after the correction it publishes only canonical bytes and
  retains the foreign private replacement. A preexisting measurement output
  initially reached Task 7 verification, and a hardlink introduced after all
  four tool calls was overwritten; both now fail without tool/preflight bypass
  or input mutation. The symlink variant runs where host privileges permit.
  The release-manifest mode matrix remains unchanged and green.
- Verification: sealer **12/12**; full verifier **241/242**, with only the
  documented pre-existing null-camera proof failure; Task 7 release-manifest
  plus staging adjacency **46/46** on the required unsandboxed Windows rerun;
  changed Python files compile and scoped diff/check passes. Audit v2 remains
  507 bytes / `87dabb51adc1c1cb6b646a826977658de305df086d1cfb21fc2c97a0bd6127e2`;
  audit v3 remains 416 bytes /
  `80f662863f6af8c8d905717cc06504677eedf144e2f00eff7b254ee7e099cba5`.
  `GOAL_AUDIT_CONTRACT_V4_SHA256` remains `None`.
- Independent rereview: Windows publication is bound to the held handle; POSIX
  publication is bound to the held descriptor and fails closed when exact-object
  facilities are unavailable; post-publication identity/size proof remains
  mandatory. Measurement rejects preexisting output before verification/tools
  and has no `write_if_changed` fallback, so late aliases lose the exclusive
  publication race without input mutation. Mode legality and v2/v3 immutability
  remain addressed.
- Open gates: real target build, reproducibility, real audit-v4
  measurement/contract/pin, complete-package inventory, release
  evidence, 20,100-frame smoke, visual, and manual play. No target or emulator
  evidence is claimed.

## Task 9 execution ledger

- Start base: `038391a4` (`docs(saturn): close audit v4 support review`).
- Status: `active`. Task 8 and its independent rereview are complete; Task 9
  owns the first real identity-v2 release build, two-build reproducibility
  comparison, audit-v4 measurement/seal/pin, capacity/package evidence, and
  deployment staging.
- Prerequisites observed before dispatch: `.venv-saturn-tools` Python,
  `baserom.us.z64`, `build/us_pc`, and the pinned Yaul SH-2 compiler are present.
  The one-shot v4 contract, measurement report, reproducibility report, and
  manual staging destination are absent.
- Fail-closed boundary: relevant source-closure dirt, a failed target gate,
  non-identical candidate artifacts, forbidden audit callers, insufficient
  capacity/package facts, or release-manifest mismatch stops sealing/staging.
  The documented unrelated null-camera host-test failure is not target evidence
  and is the only pre-approved baseline failure.
- Preserved unrelated state: the existing modified prior-campaign plans/ledgers
  and extensive untracked build, audit, evidence, and temporary artifacts remain
  outside Task 9 scope.
- Open gates: candidate A/B builds, reproducibility, measurement, v4 seal/pin
  and audit, package/capacity evidence, release staging, independent reviews,
  20,100-frame smoke, visual proof, desktop launch, and owner manual play.
- First candidate-A invocation was discarded before SH-2 compilation. The
  wrapper found inherited Qt GNU Make 4.2.1 for the prescribed
  `mingw32-make` spelling; sourceboot requires grouped targets from GNU Make
  4.3+, and Yaul dependency discovery failed while parsing. No release output
  or target evidence was produced.
- Systematic diagnosis confirmed `C:\msys64\usr\bin\make.exe` 4.4.1 is the
  installed MSYS Make and the repository's existing full-path precedent. TDD
  RED was the focused MSYS-wrapper suite at 2/3 with the observed 4.2 version;
  GREEN is 3/3 plus an explicit wrapper version invocation selecting 4.4.1.
  The narrow correction maps only the public `mingw32-make` spelling, fails
  closed below 4.3, and retains the exact Task 9 command and flags.
- Reference record: pattern-only reuse of the in-tree full-path MSYS Make
  invocations and wrapper contract at `8588d391`; no external code was copied
  and no new license/notice obligation was introduced. Candidate A must be
  restarted after the separately reviewable behavior commit; every target,
  reproducibility, audit, capacity/package, and staging gate remains open.
- The first restart after the Make correction was discarded during host asset
  generation, still before identity discovery or SH-2 compilation. POSIX
  `/bin/sh` consumed the backslashes in the plan's Windows-form
  `SOURCEBOOT_PYTHON`, yielding an unresolvable `D:Code...python.exe`. A direct
  wrapper probe launched the exact same repository interpreter successfully
  with a `D:/Code/.../python.exe` spelling (`Python 3.12.13`). The plan now
  normalizes only that assignment to forward slashes; no profile, flag,
  release-mode, toolchain, or serial-execution binding changed. Candidate A,
  source-closure cleanliness, and all downstream gates remain open.
- The next restart completed host assets and reached identity discovery, then
  was discarded before closure generation: the backslash-form
  `YAUL_INSTALL_ROOT` was likewise consumed by POSIX `/bin/sh` before the first
  dependency-only `sh-elf-gcc -MM` call. A direct wrapper probe launched pinned
  `sh-elf-gcc` 14.3.0 from the forward-slash spelling. The exact command now
  normalizes both explicit absolute Windows inputs passed into MSYS Make; no
  build flag or toolchain component changed.
- Reproducibility correction: candidate B will detach at the exact final source
  commit that produces successful candidate A, including the separately
  reviewable Task 9 execution fixes, rather than at historical Task 8 head.
  Otherwise the release Git/source identity could not be equal. Controller-owned
  independent review still gates Task 9 completion.
- Candidate A then completed all 228 dependency-only scans but was discarded
  at closure assembly before cleanliness evaluation: 52,931 characters of
  depfile paths plus the other repeated path families exceeded the Windows/MSYS
  Python command line (`Argument list too long`). No target object, link, seal,
  or release evidence was produced.
- TDD RED: `test_gen_source_closure.py` could not import the requested strict
  path-list loader; a follow-up RED proved `/d/...` rows were not native drive
  paths in Windows Python; `test_sourceboot_hermetic_build_make.py` was 7/8
  because repeated closure argv remained. GREEN: source closure 20/20 with one existing
  case-sensitive-filesystem skip, Make pipeline 8/8, and changed Python
  `py_compile` clean.
- Design correction: eight LF-only `sm64-saturn-path-list-v1` transports cover
  build compiled sources/depfiles/recipe/generator/generated/derived paths and
  post-link actual/assembly depfiles. Rows are nonblank, trimmed, NUL-free,
  unique, and byte-sorted. The transport bytes are not identity inputs; their
  semantic paths derive the same canonical closure, and the generator remains
  a hashed generator input. Canonical MSYS drive paths convert only at the
  Windows Python transport boundary. GNU Make writes the lists internally, avoiding a
  shell argv boundary; dry-run suppression examines only MAKEFLAGS' short-option
  word. Repeated CLI options remain compatible.
- Reference record: clean-room/pattern-only adaptation of the in-tree canonical
  handoff and closure interfaces at `bc6d9441`; no external source was copied
  and no license/notice obligation changed. Candidate A and every downstream
  gate remain open pending the exact release rerun from the fix commit.
- Candidate-A run from `21b79b1c` proved canonical list writing/reading and
  entered semantic closure classification, then failed closed because
  implementation `build/us_pc` is a junction to the parent checkout. A
  generated input resolved outside the guarded repository root. Cleanliness,
  target compile/link, seal, and all downstream gates remain open; the user
  junction is preserved untouched.
- Isolation correction: candidates A and B will be two fresh detached siblings
  at the same exact final source commit. Each independently receives only an
  owned `baserom.us.z64` and `build/us_pc` copy, with relative path, length, and
  SHA-256 inventory equality required before build. This replaces the original
  implementation-worktree A topology, removes the shared live namespace from
  both closures, preserves user state, and strengthens the reproducibility
  claim. Any later in-scope source fix requires both candidates to detach and
  rebuild at the new common commit.
- Owned prerequisite verification: candidate roots
  `hermetic-release-repro-a` and `hermetic-release-repro-b` each contain an
  ordinary, non-reparse `build/us_pc` directory. Source/A/B inventories are
  equal across 1,977 files by relative path, byte length, and per-file SHA-256;
  their canonical inventory digest is
  `43019209f5080af9a9d4a1c876176b5b9724c17913a4786a573f4ad2275fe7a1`.
  Source/A/B ROM SHA-256 is
  `17ce077343c6133f8c9f2d6d6d9a4ab62c8cd2aa57c40aea1f490b4c8bb21d91`.
  Both detached roots were tracked-clean before the ignored prerequisites were
  installed.
- Managed execution runs under a different identity from the worktree creator,
  so the prescribed build scopes Git `safe.directory` to the active candidate
  through inherited `GIT_CONFIG_COUNT`, `GIT_CONFIG_KEY_0`, and
  `GIT_CONFIG_VALUE_0`; global Git configuration remains untouched. Candidate
  A has not restarted, and both candidates must detach at the next common
  tracked commit before the exact build begins.
- The first owned-candidate-A invocation stopped immediately at
  `check-libyaul`, before discovery or SH-2 compilation, because a fresh Git
  worktree does not materialize submodule contents. No release output or target
  evidence was produced. An initial upstream submodule clone was rejected by
  restricted networking; no partial checkout was admitted.
- Both candidates now materialize the superproject's pinned, tracked
  `third_party/libyaul` gitlink at
  `6012f79f237773378c8014e70d8998ad95a38d98`; each checkout is tracked-clean.
  A one-shot local `file://` transport used the implementation worktree's clean
  pinned checkout and changed neither repository URL nor global Git config.
  This completes tracked source checkout and is not an added ignored
  prerequisite. Reference: `yaul-org/libyaul`, pinned commit above, MIT
  license, full submodule checkout, dependency/materialization reuse. Candidate
  A remains open pending the exact rerun.
- The next exact candidate-A restart entered `identity-assets` but stopped
  before identity discovery: `bake_bob_tiles.py` could not find
  `levels/bob/0.rgba16.png`. Fresh candidates correctly contain no copied
  ignored extraction outputs, exposing a missing derivation from the allowed
  baserom. No closure, target compile/link, seal, or release gate is claimed.
- Asset-boundary correction: `extract_assets.py` retains its legacy positional
  language/default-root behavior and adds an isolated output root plus strict
  canonical path-list emission. Sourceboot derives all 1,539 US assets under
  candidate-local `build/saturn/sourceboot/generated/extracted-assets`, orders
  extraction before BOB tile/fragment/sky consumers, and passes 1,540 semantic
  rows (assets plus extractor manifest) as a second generated-input list for
  closure sealing and post-link rediscovery. The recursive root Make uses
  `NOEXTRACT=1` because the independently verified `build/us_pc` tree already
  owns those includes.
- TDD RED: hermetic Make was 8/9 and isolated extractor output was 0/1. GREEN:
  extractor output 2/2, hermetic Make 9/9, source closure 20/20 with one
  existing case-filesystem skip, and changed Python compilation clean.
  Reference: in-tree `Makefile` extraction boundary at `fc56b14a`; this
  inherited SM64 fork has no root license file at that revision, so existing
  project terms remain unchanged; inspected `Makefile` lines 239-252 and
  `extract_assets.py`; same-repository close-port reuse introduced no external
  source or new notice obligation. Candidate A and every downstream gate remain
  open pending rerun from the behavior commit.
- The first run from the asset-boundary behavior commit derived all
  candidate-local assets, then stopped before identity discovery because
  `compile-bob-tiles` hard-coded `SATURN_REPO_ROOT` and discarded the
  caller-supplied `BOB_ASSET_ROOT`; the fragment sibling already honored that
  public override. No closure or target/release gate is claimed.
- Focused TDD RED: hermetic Make was 8/9 because only one of two BOB raw-asset
  consumers used the override. GREEN: 9/9 after the tile recipe adopted the
  same override. The one-line correction changes no bound profile, inventory,
  output root, toolchain, or `-j1` requirement; candidate A remains open pending
  a clean exact rerun.
- The next exact restart passed the corrected initial tile bake, then stopped
  before discovery while generating `bob_scene.h`: the scene delegation
  re-invokes nested tile/BSP prerequisites without forwarding the sealed
  `BOB_ASSET_ROOT`. No closure or target/release gate is claimed.
- Focused TDD RED: hermetic Make was 8/9 with only two of three sourceboot BOB
  delegations bound to the extracted root. GREEN: 9/9 after scene propagation.
  The generated bytes and closure definition are unchanged; all nested BOB
  consumers now use the already-sealed candidate-local root. Candidate A
  remains open pending a clean exact rerun.
- The next exact restart derived all 1,539 candidate-local US assets plus the
  extractor manifest (1,540 sealed inventory rows) and completed every BOB
  consumer, then stopped before identity discovery in the recursive root Make.
  That parser unconditionally rebuilt all host tools, including obsolete
  `armips` sources that fail under the current host compiler, although every
  requested `build/us_pc` target was already present in the independently
  inventoried prerequisite tree. No closure or target/release gate is claimed.
- Boundary correction: root Make now exposes `NOTOOLS ?= 0`, preserving legacy
  all-tools behavior for every ordinary caller. Only sourceboot's verified
  asset-target submake passes `NOTOOLS=1`; a missing named target still invokes
  its normal recipe and fails closed. Focused TDD was RED at 8/9 before the
  opt-out existed and GREEN at 9/9 afterward. Reference: pattern-only reuse of
  the same-repository `NOEXTRACT`/recursive-Make boundary at `9949e655`; no
  external source, license, or notice change. Candidate A and all downstream
  gates remain open pending the exact clean rerun from the behavior commit.
- Candidate-A run from `fcfe8068` derived the 1,540-row extracted inventory and
  completed all BOB consumers, then stopped before discovery. Fresh-worktree
  source mtimes made the independently copied, byte-identical
  `build/us_pc/text/us/define_text.inc.c` look stale, so its normal recipe ran
  and failed on absent `tools/textconv`. This proves the narrow missing/stale
  path still failed closed, but also proves Make timestamps cannot define the
  identity of allowed copied prerequisites. No target/release gate is claimed.
- Superseding boundary correction: release mode uses the existing bounded
  target collector's new `--verify-existing` mode, includes explicit text and
  water-sky inputs, rejects missing or root-escaping files, and relies on
  closure build/post-link rediscovery for exact byte hashes. Development mode
  retains root Make materialization and its legacy all-tools behavior. TDD RED
  was 8/10 (release contract and CLI fixture); GREEN is 10/10, including an
  older generated file accepted despite a newer source and the same required
  file rejected after deletion. Reference: same-repository close-port/pattern
  reuse of the collector/closure boundary at `fcfe8068`; no external source or
  license/notice change. Candidate A remains open pending the exact clean rerun.
- Candidate A from `6781cb5f` passed the release prerequisite verifier and all
  228 dependency scans, then failed closed in closure assembly because
  sourceboot-local `main.c` was handed off relative and the closure consumer
  resolved it as repository-root `main.c`. No closure or downstream gate is
  claimed.
- Compiled-source handoff correction: normalize every `SH_SRCS_UNIQ` entry with
  Make `abspath` before excluding generated compiled inputs. This preserves the
  exact file Yaul compiles for local and already-absolute sources and removes
  candidate-root spelling from the semantic closure. TDD RED was 10/11; GREEN
  is 11/11. Reference: pattern-only reuse of Yaul absolute build-path handling,
  pinned MIT commit `6012f79f`, inspected `build.pre.mk` and
  `build.post.iso-cue.mk`; no copied upstream bytes or notice change. Candidate
  A and every target/release gate remain open pending the exact rerun.
- The next exact run passed the corrected compiled-source class, then stopped
  in discovery while parsing GCC depfile contents. Those rows carry MSYS
  `/d/...` paths; Windows Python converted only path-list rows and interpreted
  the dependency as `D:\d\...`, which correctly failed as an absent external
  path. No closure or downstream gate is claimed.
- Transport correction: dependency resolution and canonical classification now
  share the existing `_host_transport_path` conversion, while serialized
  closure paths remain repository-relative. Exact Windows depfile fixture was
  RED at 20/21 plus one existing case-fs skip and GREEN at 21/21 plus the same
  skip. Reference: same-file close-port reuse at `b7bdb347`; no external source
  or license/notice change. Candidate A remains open pending the exact rerun.
- Candidate A from `2bf05467` passed MSYS-path conversion, then stopped before
  closure publication on bare `saturn_build_identity_values.inc`. Inspection
  showed real depfiles also carry valid sourceboot-cwd rows (`main.c`, local
  headers, and `../runtime/...`), so repository-root relative resolution was
  not the compiler's semantics. No closure or downstream gate is claimed.
- Closure CLI correction: `--dependency-base` is bounded inside the repository
  and set to sourceboot `$(CURDIR)` in discovery and post-link verification.
  Missing single-component dependencies may alias only a unique explicit
  derived-output basename, modeling GCC `-MG` for the pre-seal identity header;
  no match stays missing and multiple matches fail ambiguous. TDD RED: closure
  21/22 plus existing skip and Make 10/11. GREEN: closure 22/22 plus skip and
  Make 11/11, including ambiguity rejection. Reference: clean-room extension
  of in-tree Task 2 derived classes/cwd semantics at `2bf05467`; no external
  source or license/notice change. Candidate A remains open pending exact rerun.
- Candidate A from `f9b1345e` built the complete closure `.tmp` and reached
  toolchain attestation, where atomic discovery publication stopped because
  Yaul uses the same `sh-elf-gcc.exe` for compiler and linker-driver roles. No
  closure seal was published and no downstream gate is claimed.
- Attestation correction: sourceboot still requires all eight explicit role
  arguments, then canonicalizes their exact path spellings to the schema's
  unique measured-binary set. Exact compiler/linker-driver repetition is
  measured once; direct component duplicates and case aliases retain existing
  rejection. TDD RED was 15/16; GREEN is 16/16. Reference: clean-room use of
  the in-tree Task 3 binary-set schema and pinned Yaul variables at `f9b1345e`;
  no external source or license/notice change. Candidate A remains open.
- Candidate A from `a35c2a93` built closure `.tmp` and passed shared-role
  deduplication, then attestation rejected extensionless Windows
  `sh-elf-readelf` because the concrete file is `sh-elf-readelf.exe`. MSYS can
  execute the former spelling, but byte attestation correctly requires a file;
  atomic discovery publication and downstream gates remain open.
- Tool-binding correction: sourceboot custom objdump/readelf/addr2line paths
  use `.exe` only under `OS=Windows_NT`; non-Windows and pinned bytes are
  unchanged, and native-math verification consumes the same paths. TDD RED was
  10/11; GREEN is 11/11. Reference: close-port reuse of in-tree
  `Makefile.saturn.mk` `HOST_EXEEXT` at `a35c2a93`; no external source or
  license/notice change. Candidate A remains open pending exact rerun.
- Candidate A from `1dea5fad` completed all 228 dependency scans and atomically
  published the source closure, external-dependency handoff, and toolchain
  attestation. Seal-stage bootstrap then rejected the selected checked-in
  profile before compilation: its canonical LF Git blob had been checked out
  with CRLF by Windows `core.autocrlf`. No compile, link, seal, or release gate
  is claimed.
- Canonical-checkout correction: `.gitattributes` now pins every tracked JSON
  file to `eol=lf`; the strict canonical parser remains unchanged and continues
  to reject noncanonical raw bytes. This applies uniformly to profiles,
  package descriptors, routes, and later JSON release inputs without changing
  their semantics. TDD RED was target-profile 11/12 with `eol: unspecified`;
  GREEN is 12/12 plus identity-bootstrap 15/15. Candidate worktrees must be
  refreshed at the behavior commit before the next exact run. Candidate A and
  all downstream target/release gates remain open.
- Candidate A from `e07d7ece` had 422/422 tracked JSON paths confirmed LF and
  blank tracked status. It completed dependency discovery and atomically
  republished closure, external handoff, and attestation, but bootstrap still
  rejected the target profile before compilation. The profile's raw LF digest
  was `860049d357faf7cf8f7bf8d04f0494824d8c135495a59ec5c902b1d4ef153ddd`;
  canonical serialization was
  `0787d2a97015d7f99383b8bf2446f05420abb0672493a8583b27f4b1e6d3b94b`.
- Remaining canonical-profile correction: Task 1's later `area_id` addition
  preserved semantic configuration but appended the key after `slave_render`.
  It now occupies its sorted canonical position. A real repository test binds
  the release profile plus all ten selected descriptors to exact canonical
  bytes. TDD RED was target-profile 12/13 with only that profile failing;
  GREEN is target-profile 13/13, identity-bootstrap 15/15, and hermetic Make
  11/11. Identity publication, compile, link, seal, and release gates remain
  open pending the exact candidate rerun from the behavior commit.
- Candidate A from `a18f612e` passed the canonical profile and identity
  bootstrap, then failed before identity label/publication or target compile:
  `gen_build_identity.py` resolved the spec's repository-relative
  `build/saturn/...` artifact paths from sourceboot's nested cwd. The spec and
  artifact hashes were valid; no downstream gate is claimed.
- Invocation-base correction: the sourceboot Makefile now uses one generator
  command rooted with `cd "$(ROOT)"` for label, directory tag, and output
  generation. The identity CLI's established caller-relative semantics remain
  unchanged. TDD RED was hermetic Make 11/12; GREEN is Make 12/12,
  build-identity 21/21, and bootstrap 15/15. Reference: same-repository
  close-port of Task 5's explicit-root/repository-relative spec contract at
  `a18f612e`; no external source or notice change. Candidate A and every
  target/release gate remain open pending exact rerun from the behavior commit.
- Candidate A from `7a8271b8` published identity v2 tag
  `id-d4082ee7fde66135`, passed geo depth at 172/192 across 518 inputs, and
  started target compilation. Its first `actors/common0.c` object failed when
  GCC launched `C:\msys64\mingw64\bin\as.exe`; that host assembler rejected
  the SH-2 `-big` option. Compile did not complete; link/seal/release remain
  open.
- Tool-helper correction: the Windows wrapper now orders the selected Yaul
  `bin` before MSYS host programs because this GCC finds unprefixed `as`
  through PATH. MSYS DLL directories remain before inherited PATH. Wrapper TDD
  was RED 2/3 and GREEN 3/3; a disposable minimal compile produced a 552-byte
  SH object and was removed before resealing. Reference: same-repository
  close-port of the Yaul helper-path setup in
  `test_camera_acceptance_route.py` at `7a8271b8`; no external source or notice
  change. Candidate A compile/link/seal/release gates remain open.
- The exact `7cee9a12` rerun retained identity tag
  `id-d4082ee7fde66135` but failed on the same first object. Root cause refined:
  pinned Yaul `share/build.pre.mk:66` prepends `/mingw64/bin` inside the nested
  Make parse, after the wrapper's environment order. Wrapper PATH alone cannot
  bind GCC's unprefixed helper lookup.
- Actual-invocation correction: `-B$(YAUL_INSTALL_ROOT)/bin/`
  `$(YAUL_PROG_SH_PREFIX)-` is present in C, C++, and link flags. Under a
  deliberately host-first PATH, verbose GCC output invoked exact
  `sh-elf-as.exe` and `-print-prog-name=ld` resolved exact `sh-elf-ld.exe`.
  Attestation now measures those two concrete invoked binaries, while GCC is
  independently measured as driver. TDD RED was Make 12/13; GREEN is Make
  13/13, attestation 16/16, and wrapper 3/3. Every disposable probe was removed
  before resealing. Reference: close-port of in-tree GCC `-B` usage and pinned
  Yaul helper naming at `7cee9a12`; no external source or notice change.
  Candidate A compile/link/seal/release gates remain open.
- Candidate A from `d1699ab7` published identity v2 tag
  `id-99f6dfa38dcc64f8`, passed canonical bootstrap and geo depth at 172/192
  across 518 inputs, then compiled all 75 main sourceboot objects with exact
  `sh-elf-as.exe`. The separate software-float recipe failed on
  `softfp/addsf3.o` because its independent `SOFTFP_CFLAGS` lacked the GCC
  helper prefix and therefore launched MSYS host `as.exe`, which rejected
  `-big`. No link, post-link seal, release, or reproducibility gate is claimed.
- Software-float boundary correction: `SOFTFP_CFLAGS` now begins with the same
  `SOURCEBOOT_GCC_TOOL_PREFIX` used by main C/C++/assembly/link invocations, so
  all GCC helper lookup remains bound to attested Yaul binaries. TDD RED was
  hermetic Make 12/13; GREEN is Make 13/13, attestation 16/16, and wrapper 3/3.
  Reference: same-repository close-port of the adjacent main sourceboot helper
  binding and inspection of vendored `third_party/gcc-soft-fp/README.md`; no
  external source, copied bytes, license, or notice change. Candidate A must
  restart from the resulting common source commit; compile/link/seal/release/
  reproducibility gates remain open.
- Candidate A from `47ab8135` published identity v2 tag
  `id-8a95684a4575e95c`, passed closure/attestation/bootstrap and geo depth
  172/192, compiled all main and software-float objects, and linked the ELF.
  Packaging then failed before `IP.BIN` because pinned Yaul `wrap-error`
  defaulted `TMPDIR` to unwritable `/tmp`; its first `mktemp` returned
  permission denied. No IP.BIN, ISO/CUE, post-link seal, release manifest, or
  reproducibility gate is claimed.
- Packaging-temporary correction: sourceboot exports
  `$(SH_BUILD_DIR)/tmp`, creates it as an order-only prerequisite of `IP.BIN`,
  and lets Yaul hook sub-makes plus later ISO/CUE wrappers inherit it. Transient
  path/stderr bytes stay ignored and outside canonical identity; the tracked
  recipe remains in source closure and semantic package outputs remain
  manifest-bound. TDD RED was Make 13/14; GREEN is Make 14/14, attestation
  16/16, and wrapper 3/3 (33/33 combined). Reference: pinned MIT libyaul
  `6012f79f...`, inspected `libyaul/build/build.post.iso-cue.mk` and
  `libyaul/common/wrap-error`; dependency integration/pattern-only reuse of
  the repository's existing explicit temp-environment recipe, no copied bytes
  or notice change. Candidate A must restart at the resulting common commit;
  package/post-link/release/reproducibility gates remain open.
- Candidate A from `d48e069e` published identity v2 tag
  `id-fb999abbcc42c7fa`, completed compile/link, and produced verified-present
  package intermediates: 4,108-byte IP.BIN, 3,565,696-byte SOURCE.DAT,
  4,968,448-byte ISO, and 88-byte CUE. Post-link closure verification then
  stopped on a missing generated `bob_sky_bitmap.sx` depfile. No post-link
  closure pass or release manifest is claimed.
- Post-link scan correction: recipe-level `foreach` had space-joined all fresh
  assembly GCC invocations, so later compiler paths became operands and only
  the final depfile existed. Every assembly scan is now an explicit Make
  target depending on the linked ELF, its own source, and force-scan gate;
  `verify-sealed-inputs` requires the entire depfile set. This structurally
  preserves after-link freshness and independent commands. TDD RED was Make
  14/15; GREEN is Make 15/15, with source closure plus attestation yielding
  53/53 and one existing case-filesystem skip. Reference: clean-room reuse of
  the same sourceboot Makefile's existing per-source discovery rule; no
  external source, copied bytes, license, or notice change. Candidate A must
  restart at the resulting common commit; post-link closure, release, and
  reproducibility gates remain open.
- Candidate A from `0dc7ede0` published identity v2 tag
  `id-5125ad1b7f816a3b`, completed compile/link/package, and produced all four
  fresh assembly depfiles. Closure verification then rejected external-set
  drift: sealed discovery had 4 paths while actual compile/post-link depfiles
  had 103 (99 added, 0 missing). No post-link closure pass or release manifest
  is claimed.
- External-coverage correction: discovery used GCC `-MM`, excluding headers
  reached through `-isystem`, while pinned Yaul's real recipes use `-MD` and
  include them. C, C++, assembly discovery, and post-link assembly scans now
  use `-M -MG`, so the complete GCC/Yaul system-header set enters the external
  handoff and byte attestation before identity formation. TDD RED was Make
  15/16; GREEN is Make 16/16, with source closure plus attestation yielding
  54/54 and one existing case-filesystem skip. Reference: close-port of pinned
  MIT libyaul `6012f79f...` `build.post.bin.mk` `-MD` semantics; no copied
  bytes or notice change. Candidate A must restart at the resulting common
  commit; closure/release/reproducibility gates remain open.
- Candidate A from `361d27ad` published identity v2 tag
  `id-ed9d802a7e9609df`, passed external closure equality, compile, link, and
  packaging, then failed release cleanliness on untracked
  `build/us_pc/actors/amp/amp_body.rgba16.inc.c`. The verified copied
  prerequisite was compiler-owned `header`, not explicit `generated-input`,
  because its exact selected-target inventory was not handed to closure
  discovery. No release manifest or reproducibility gate is claimed.
- Generated-prerequisite classification correction: source asset preparation
  atomically publishes the verified selected `build/us_pc` targets through a
  canonical strict path list, and closure discovery consumes that bounded list
  as explicit generated inputs. The transport list is excluded from identity;
  each semantic target byte remains closure-hashed and post-link-rehashed.
  Cleanliness still Git-checks every class except `generated-input` under
  `build/`, so dirty/untracked checked-in inputs fail, and generated-input byte
  mutation fails rehash verification. Focused TDD was RED 0/2 and GREEN 2/2;
  combined Make/source-closure coverage is GREEN 38/38 with one existing case-
  filesystem skip. Reference: same-repository close-port of the strict list
  boundary in `extract_assets.py`/`gen_source_closure.py`; no external source,
  copied bytes, license, or notice change. Candidate A must restart from the
  resulting common commit; release/reproducibility gates remain open.
- Candidate A from `e943e2b0` emitted the strict 1,021-row PC asset inventory
  and then stopped before closure publication on duplicate explicit ownership.
  The exact normalized overlap was only `build/us_pc/bin/water_skybox.c` and
  `build/us_pc/include/text_strings.h`: both were in the new authoritative
  inventory and the legacy static generated list. No identity, compile,
  release manifest, or reproducibility gate is claimed.
- Duplicate-ownership correction: strict rejection remains unchanged; the PC
  inventory is the sole explicit owner of those two verified rows, which are
  filtered only from the static list. Compiled-source exclusion and semantic
  byte hashing remain unchanged. TDD RED was 0/1 and GREEN 1/1; combined Make
  and source-closure coverage remains GREEN 38/38 with one existing case-
  filesystem skip. Candidate A must restart at the resulting common commit;
  release/reproducibility gates remain open.
- Candidate A from `3df2ec4a` published identity v2 tag
  `id-c7c5e79df283e6a5`, completed compile/link/package and external equality,
  then failed release cleanliness only on
  `build/us_pc/include/text_menu_strings.h`. The direct 1,021-row inventory
  sealed its `text_strings.h` parent but not this quoted generated child. No
  release manifest or reproducibility gate is claimed.
- Transitive generated-header correction: verified inventory publication now
  follows quoted includes only within the bounded `build/us_pc` root and fails
  closed on missing children there or escaping direct targets, producing the
  exact observed 1,022-row
  compiler-relevant set. TDD RED was 0/1 and GREEN 1/1; combined Make/source-
  closure coverage is GREEN 38/38 with one existing case-filesystem skip.
  Reference: close-port of same-repository, independently reviewed commit
  `46162df5` identity-v1 generated-header traversal into source-closure v2; no
  external source, copied notice, or license change. Candidate A must restart
  from the resulting common commit; release/reproducibility gates remain open.
- The first `085cdbc9` candidate restart stopped in verified asset inventory,
  before discovery/identity. Generated `water_skybox.c` quotes repository
  `types.h`, but the initial transitive walk falsely treated that compiler-
  search include as missing `build/us_pc/bin/types.h`. No identity, compile,
  release manifest, or reproducibility gate is claimed.
- Generated-header boundary correction: missing local children fail closed for
  generated headers; existing in-root children are followed; generated C/inc.c
  direct targets remain inventoried and hashed, while actual compiler-resolved
  headers remain depfile-sealed. The historical reviewed traversal did not put
  water-skybox C through its recursive set. A water-style TDD fixture was RED
  0/1 and GREEN 1/1; combined Make/source-closure coverage remains GREEN 38/38
  with one existing case-filesystem skip. Candidate A must restart from the
  resulting common commit; release/reproducibility gates remain open.
- Candidate A from `d816c3dc` published identity v2 tag
  `id-0fe6b6cd91ccfeb5`, completed compile/link/package and external equality,
  and classified all 1,022 PC generated inputs. Cleanliness then rejected
  tracked libyaul recipe `third_party/libyaul/libyaul/build/build.post.bin.mk`
  because the superproject indexes only its enclosing gitlink. No release
  manifest or reproducibility gate is claimed.
- Gitlink cleanliness correction: otherwise-untracked closure paths must map
  to an indexed mode-160000 prefix; the initialized nested checkout must be at
  that exact commit, and each relevant nested path must be tracked and clean
  under exact scoped Git trust. The superproject gitlink also remains clean,
  while unrelated nested dirt stays outside the closure. TDD was RED 0/1 and
  GREEN 1/1 for clean, dirty-file, and unpinned-HEAD cases; combined closure,
  Make, and attestation coverage is GREEN 55/55 with one existing case-
  filesystem skip. Reference: clean-room integration of Git index/gitlink
  semantics; no external source, copied bytes, license, or notice change.
  Candidate A must restart from the resulting common commit; release and
  reproducibility gates remain open.
- Candidate A from `8912faeb` published identity v2 tag
  `id-2644d689b9eb77c6`, completed compile/link/package and external equality,
  then nested cleanliness reported all three relevant libyaul recipes dirty.
  Host Windows Git showed exact pinned HEAD and clean status; target-PATH MSYS
  Git showed only CRLF/LF differences (`--ignore-space-at-eol` clean) because
  it lacks Windows Git's checkout `core.autocrlf=true`. No release manifest or
  reproducibility gate is claimed.
- Windows Git-normalization correction: cleanliness commands bind
  `core.autocrlf=true` on Windows rather than inheriting PATH-specific system
  config. Exact working bytes are still closure-hashed before status, so
  semantic dirt/unpinned commits remain rejected. A system-config-disabled
  CRLF fixture was RED 0/1 and GREEN 1/1; dirty-file and unpinned-HEAD cases
  remain GREEN. Combined closure/Make/attestation coverage remains 55/55 with
  one existing case-filesystem skip. Candidate A must restart from the common
  resulting commit; release/reproducibility gates remain open.
- Candidate A from `4dbf515f` published identity v2 tag
  `id-6d6ecbaa8345b28f`, completed compile/link/package and external equality,
  and passed nested libyaul cleanliness. Root cleanliness then raised
  `FileNotFoundError: [WinError 206]` before release-manifest sealing because
  the full checked-in closure was expanded into one `git status` argv. No
  release manifest or reproducibility gate is claimed.
- Windows command-boundary correction: root and nested cleanliness now batch
  the exact ordered status path set into deterministic commands whose rendered
  Windows command lines are at most 16,000 characters. Every path is checked
  exactly once; any dirty batch or individually overlong path fails closed.
  Focused TDD was RED 0/1 and GREEN 1/1; full source-closure coverage is GREEN
  24/24 with one existing case-filesystem skip, and combined closure, Make,
  and attestation coverage is GREEN 56/56. Reference: clean-room use of Git's
  existing path-limited status interface; no external source, copied bytes,
  license, or notice change. Candidate A must restart from the resulting
  common commit; release/reproducibility gates remain open.
- Candidate A from `535cba13` published identity v2 tag
  `id-a0b8a3f2d9a15efe`, completed compile/link/package and external equality,
  and passed root plus nested cleanliness. This proves bounded status checks
  on the real 4,234-record closure. Release-manifest construction then
  rejected each of the seven recipe rows because canonical schema owner
  `linker/build-recipe` contains a slash. No manifest or reproducibility gate
  is claimed.
- Source-owner schema correction: release validation accepts exact
  `CLASS_PRECEDENCE` values as schema-defined owners while retaining portable-
  identifier validation for arbitrary owner values. Malformed arbitrary
  owners and case collisions therefore remain fail-closed. Focused TDD was
  RED 0/1 and GREEN 1/1; full release-manifest coverage is GREEN 27/27 and
  staging coverage is GREEN 20/20. Reference: clean-room reconciliation of
  `release_manifest.py` with the same repository's canonical
  `gen_source_closure.py` schema; no external source, copied bytes, license,
  or notice change. Candidate A must restart from the resulting common commit;
  manifest/reproducibility gates remain open.
- Candidate A from `4dcc2235` published identity v2 tag
  `id-887f8a9ff04578ea`, completed compile/link/package, external equality, and
  root plus nested cleanliness, and cleared source-owner validation. Manifest
  construction then rejected the profile's unsuffixed ELF basename against
  exact sourceboot program output `sm64-saturn-sourceboot-e2.elf`; ISO and CUE
  carried the same latent suffix mismatch. No manifest or reproducibility gate
  is claimed.
- Profile/output correction: the release-enabled BOB profile now declares the
  real `-e2` ELF/ISO/CUE basenames. A checked-in integration test derives those
  names from sourceboot Make's `SH_PROGRAM`, while the deferred full-game
  profile retains its distinct deployment names. Focused TDD was RED 0/1 and
  GREEN 1/1; full target-profile coverage is GREEN 14/14. Reference: close-
  port of established sourceboot Make, Task 9 plan, and historical target-
  evidence paths; no external source, copied bytes, license, or notice change.
  Candidate A must restart from the resulting common source commit and new
  canonical profile digest; manifest/reproducibility gates remain open.
- Candidate A from `6188a5a4` published identity v2 tag
  `id-14026551f0ffa64e`, completed compile/link/package, external equality,
  root plus nested cleanliness, schema-owner validation, and profile/output
  binding. Manifest Git provenance then raised Windows `WinError 206` because
  it independently expanded the complete closure into one `git status` argv.
  No manifest or reproducibility gate is claimed.
- Final-provenance correction: release manifest construction reconstructs the
  exact sealed rows and reruns the shared bounded, tracked/clean, pinned-
  gitlink, and CRLF-normalized verifier immediately before publication; no
  cached boolean substitutes for that final check. Focused TDD was RED 0/1 and
  GREEN 1/1. Source-closure 24/24 with one existing case-filesystem skip,
  release-manifest 28/28, and staging 20/20 are GREEN (72/72 total); existing
  coverage places dirt in the last bounded batch and proves failure after all
  earlier batches. Reference: direct reuse of the same repository's post-link
  verifier; no external source, copied bytes, license, or notice change.
  Candidate A must restart from the resulting common source commit;
  manifest/reproducibility gates remain open.
- Candidate A from `acf67eef` published identity v2 tag
  `id-80d7ae1aa3bb0200`, completed compile/link/package, passed external
  closure equality and root plus pinned-libyaul cleanliness, atomically
  published its 3,266-byte release manifest, and passed direct manifest
  verification. Manifest SHA-256 is
  `c26a73be5006ac62fca03e226821c5c4676d789c17686250a8aeadff27bcc3f0`.
  The enclosing exact `verify-sourceboot` command then failed the historical
  v2 exact-total overlay: expected 582, found 700. Because v4 is not selected
  until reproducibility and measurement, neither the candidate-A nor audit
  gate is claimed and 700 is not copied into v4.
- Audit-sequencing correction: release-mode ordinary `verify` continues to
  require the native-math baseline and route oracle but does not inject the
  immutable historical v2 audit overlay before reproducibility. Development
  `verify` and explicit `verify-sim-math-route` retain v2. Task 9 still must
  measure candidate B unsealed, reject forbidden callers, exclusively seal and
  pin exact v4, and rerun v4 before staging. TDD was RED 0/1 and GREEN 1/1;
  full hermetic Make coverage is GREEN 17/17, and the v2/v3 byte/digest
  immutability test is GREEN 1/1 (`87dabb51...6127e2`,
  `80f66286...9cba5`). Reference: close-port of the active plan's explicit
  historical-audit and release-candidate phase split; no external source,
  copied bytes, license, or notice change. Candidate A must restart from the
  resulting common commit; all later Task 9 gates remain open.
- Candidate A succeeded from common detached source commit
  `081c8575280a07b80e9e7164155aacfa6cc40ab1`. The exact release tuple and
  `-j1` command exited 0 after 968.2 seconds; direct manifest verification also
  exited 0. Identity is `id-03b0d87ea1993a5a`; the 3,266-byte manifest
  SHA-256 is
  `9e57fbf376151938f59519c3d5b0865fb818dcec8687960fc083e08a4fd18759`.
  Manifest-bound artifact hashes are ELF `c1567a35...2f3655`, `SOURCE.DAT`
  `f0d3781c...0fde6c`, ISO `2f1e5fd0...2c69c80`, and CUE
  `cdbf0bfa...f46dba7`; closure cleanliness is true and provenance names the
  exact common commit. Task 9 Steps 1–3 are complete.
- Performance inspection before B: the general code-only native-math analysis
  ran approximately 238 seconds (20:55:04–20:59:02). The 3,397 function
  symbols are grouped/sorted; alias overlap scans only active nesting, while
  the only owner-wide local-island collision scan has two candidate symbols.
  No whole-symbol quadratic alias/collision hot path was found. The existing
  dataflow traversal is the scale-sensitive phase and remains unchanged so A/B
  execute identical gates. Candidate B and every later Task 9 gate remain
  open.
- Fresh candidate B at the same `081c8575` source and matching inventoried
  prerequisites failed closed at identity publication: B emitted
  `id-228b9dd66476aad9`, not A's `id-03b0d87ea1993a5a`. Only source closure
  differed (`d1e6cb87...b06a4` versus `840b13b0...ae81a`): exactly
  `bob_sky_bitmap.sx` and `bob_texture_bank.sx` among 4,234 rows embedded
  candidate-specific absolute `.incbin` paths. Toolchain, profile, package,
  binary prerequisites, and all other closure rows matched. The B compile was
  stopped; reproducibility and measurement remain failed closed.
- Generated-assembly relocation correction: all five texture, optional-
  fragment, and sky `.incbin` operands are repository-relative to sourceboot's
  real Make working directory. The generating Makefile is a normal prerequisite
  so retained outputs cannot hide a recipe change. TDD was RED 0/1 then GREEN
  1/1, with the full hermetic Make suite GREEN 18/18. Exact pinned
  `sh-elf-as` resolved the canonical relative sky path and emitted a proof
  object's `0x40000`-byte `.rodata`. Reference: same-repository close-port of
  the generated assembly rules at `44b73975f`, `2ae3fcce`, and `51744d4e`; no
  external source, copied notice, or license change. Both candidates must
  rebuild from empty owned `build/saturn` trees at the resulting common commit;
  the earlier A manifest is diagnostic, not reproducibility evidence. Task 9
  Steps 2–3 are reopened until corrected candidate A succeeds and verifies.
- Corrected candidate A rebuilt from an empty owned `build/saturn` at common
  source `342c173d910b86d166c5cd2191746cf1b1c9fde3`. The exact `-j1` command
  exited 0 in 956.8 seconds and direct manifest verification exited 0.
  Identity is `id-419ec1cdf746b8a1`; source closure is
  `e0dba9745e18666dbc1682f0c9be72e28a883491aee8388608f632057c027951`;
  the 3,266-byte manifest SHA-256 is
  `e40bc000196c49287bd2b78fedb0e8467180a53db4bf287716fa7d7e5d43dd29`.
  Generated sky/texture assembly now contains only canonical
  `../../../../build/...` operands. Task 9 Steps 2–3 are complete again;
  candidate B remains an empty independent build at the same source commit,
  and reproducibility plus all later gates remain open.
- Corrected candidate B also completed its exact `-j1` command (956.2 seconds)
  and passed direct manifest verification, but canonical comparison failed
  closed solely on `outputs.elf.sha256`. A/B manifest hashes are
  `e40bc000...43dd29` / `4389146d...e1048`; failed comparison evidence was
  `identical:false`, SHA-256 `bb5efebd...9adce`. All canonical roots and
  `SOURCE.DAT`/ISO/CUE outputs matched. Measurement did not start.
- Root cause: stripping debug yielded identical 4,497,924-byte A/B ELFs at
  SHA-256 `5b33c0a4...619a37`; unstripped strings contained 50 candidate-specific
  paths from the separately compiled `third_party/gcc-soft-fp` sources.
  `SOFTFP_CFLAGS` intentionally did not inherit main flags and omitted their
  repository prefix maps. It now retains independent `-O2`/math/tool flags but
  adds the same file/debug/macro mappings. TDD was RED 0/1 then GREEN 1/1;
  full hermetic Make coverage remains GREEN 18/18. Reference: direct reuse of
  adjacent sourceboot prefix normalization; no external source, copied notice,
  or license change. The failed comparison report is diagnostic and must be
  recreated exclusively after both empty owned Saturn trees rebuild at the
  resulting common commit. Steps 2–4 are open; measurement and later gates
  remain closed.
- Final corrected candidate A rebuilt from an empty, owned `build/saturn` at
  common detached source commit
  `44b786276f73c3dbd7dc91d9f39c332b2b51bf65`. The exact release tuple and
  `-j1` command exited 0 in 968.4 seconds, and an independent
  `release_manifest.py verify` invocation exited 0. Identity is
  `id-9a051d30880c78f0`; source closure is `8bc5261a...853d`; manifest SHA-256
  is `b75ba5f0...a2ddf`; and the manifest binds ELF `f3e01ff2...81c1b`,
  `SOURCE.DAT` `f0d3781c...fde6c`, ISO `b0589b78...b989`, and CUE
  `cdbf0bfa...dba7`. Profile/package-set/toolchain roots are
  `fe090885...1dd2`, `85a5a190...c266`, and `e1360ab5...fa76`.
  Task 9 Steps 2–3 are complete; candidate B is still an empty independent
  output tree at the same source commit, so reproducibility, measurement,
  sealing, package, capacity, and staging gates remain open.
- Final candidate B completed the same exact release tuple and `-j1` command
  from empty owned output in 963.1 seconds at source `44b78627`. Independent
  verification of A and B each printed manifest SHA-256 `b75ba5f0...a2ddf`.
  Canonical comparison returned `identical:true` and `differing_fields:[]`;
  report SHA-256 is `9c3b179f...9b231`. Both candidates bind identical
  identity, closure, profile, package set, toolchain, ELF, `SOURCE.DAT`, ISO,
  and CUE. Task 9 Step 4 is complete. Measurement, sealing, exact-v4,
  package/capacity, staging, and Task 10 target gates remain open.
- One-shot candidate-B measurement exited 0 in 273 seconds and published
  schema `sm64-saturn-native-math-measurement-v1`, status
  `measured-unsealed`, root `_game_loop_one_iteration`, and total `700`.
  Neither `_atan2_lookup` nor `_atan2s` appears among its 75 callers. Report
  SHA-256 is `ad79a992...fa8075`, bound to release manifest
  `b75ba5f0...a2ddf` and ELF `f3e01ff2...81c1b`. This closes Step 5 only as
  measurement; it is not acceptance. No v4 contract or pin was created before
  validation, and sealing/exact-v4 plus all later gates remain open.
- The sealer generated an exact 606-byte v4 contract at SHA-256
  `2c23ce448c6495e552461bf2e5b75e596d57a5a292a9c8bef8f71d1edf7f7265`.
  Focused integrity TDD was RED 0/1 with the v4 pin still `None` and GREEN 1/1
  after pinning exactly that digest. Combined v2/v3/v4 integrity tests are
  GREEN 3/3; historical v2/v3 digests remain `87dabb51...6127e2` and
  `80f66286...9cba5`. Step 6 is source-complete in the scoped contract/pin/test
  behavior commit with CHANGELOG, plan, and ledger. Exact candidate-B v4 is
  still open; measurement is not substituted for acceptance.
- Exact-v4 command preflight exposed a Task 8/Task 9 interface defect before
  any target tool ran: Task 9 requires a durable `--json-output`, while the
  verifier reserved it for non-accepting observation mode. Normal release-
  bound v4 now publishes canonical result schema
  `sm64-saturn-native-math-audit-v4-result-v1` exclusively after all failures
  are empty. V2/v3, measurement, and producer-commit observation modes remain
  unchanged; preexisting and input-aliasing outputs fail before target tools.
  Focused RED was 0/1 on the exact rejection, GREEN 1/1, and the five-test
  v4/measurement/mode/integrity slice is GREEN 5/5. Reference: same-repository
  close-port of the reviewed measurement document and exact-object publisher;
  no external source or license/notice change. Exact candidate-B v4 remains
  open pending the scoped behavior commit and real command.
- Exact candidate-B v4 from acceptance-report behavior commit `43c82145`
  exited 0 in 274.0 seconds. Canonical result status is `passed`, root is
  `_game_loop_one_iteration`, and total is `700`; report SHA-256 is
  `1fe9d585...f4ddbf`. It binds contract `2c23ce44...f7265`, manifest
  `b75ba5f0...a2ddf`, ELF `f3e01ff2...81c1b`, identity
  `f5fd613b...8da9`, effective config `9a051d30...71c5b`, and profile
  `fe090885...1dd2`, with both forbidden callers explicitly absent. Task 9
  Step 7 is complete. Capacity/package, staging, docs closeout, and both
  independent reviews remain open; no Task 10 gate was run.
- Candidate-B facts pass: `___end=0x060fca38`; HWRAM physical/usable margins
  are 13,768/6,856 bytes. Cart span `0x22400000..0x22766880` is 3,565,696
  bytes with 628,608 bytes headroom. Object, staged-CD, and xorriso-extracted
  ISO `/SOURCE.DAT` are equal at SHA `f0d3781c...fde6c`; ISO listing is LBA
  534/1,742 blocks. The package set contains exactly ten required records and
  ten classes at root `85a5a190...c266`. Identity-v2 seals capacity 208; the
  prior artifact-bound idle-boot peak 138/failures 0 leaves 70 slots. The
  coverage gap remains explicit: idle 138 lacks pickup/hold/action particles,
  and Task 9 did not run a new route or idle occupancy capture. Step 8 is
  complete; staging, closeout, reviews, and Task 10 remain open.

## Task 5 review repair round 2

- Status: `source-complete`; repair round 2 is implemented and controller-owned rereview remains open. The first `Needs fixes` verdict remains effective until both remaining findings clear.
- TDD RED: after adding staged-profile generation and exhaustive rollback contracts, `.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_sourceboot_identity_spec_bootstrap.py` exited 1: `Ran 14 tests` with exactly two failures. A post-resolver staged-profile mutation published, and an injected restore failure replaced the original publication exception with `RuntimeError`.
- TDD GREEN: the same command exited 0: `Ran 14 tests ... OK`. The staged profile is digest-checked immediately before and after resolver consumption and at final prepublication revalidation. Rollback performs real writes before the injected publication failure, attempts every target despite one restore failure, preserves the original exception type/message with diagnostic notes, and removes owned class/profile/package/spec `.tmp` files on success and both failure paths.
- Regressions: identity generation passed 21 tests and target-profile resolution passed 11 tests. Current total is 46 host tests with no failures/errors/skips; scoped diff check passed.
- Design correction: a dedicated staged-profile snapshot owns the resolver input generation. Publication cleanup runs before and after writes and after rollback; restore/remove and cleanup failures are accumulated as notes on the original exception without changing its type or stopping later attempts.
- Repair commits: `c1612fd8c9e8ae595595255cbd6dd25b30eb375e` (`fix(saturn): preserve identity publication failures`) and `727c4075e0c0c5bca1a191a77e91c09d8f9344d3` (`docs(saturn): record identity rollback repair`). Post-commit `git show --check` and full-range `git diff --check b0c7fa03..HEAD` passed; the range remains restricted to the four Task 5 tracked paths.
- Independent rereview plus target build, reproducibility, audit v4, complete package inventory, release manifest, 20,100-frame smoke, visual, and manual-play gates remain open.
