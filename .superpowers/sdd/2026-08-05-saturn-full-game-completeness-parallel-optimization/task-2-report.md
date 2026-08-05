# Task 2 execution report — authoritative feature and package identity

Status: source-complete at `f5a0248d` (`feat(saturn): bind feature tuple to target identity`). The controller-owned active plan and SDD progress ledger were not modified.

## Implementation result

- Added the 404-byte big-endian `SBI1` version-1 target ABI and a target-side structural validator. Integer layout is fixed by the published C struct and Python `>` packing; the C `_Static_assert` independently fixes the size at 404 bytes.
- Added a deterministic generator that hashes every supplied source/route/input/camera/cart/scene/dependency-set/actor/animation/audio artifact, rejects a missing or stale declared SHA-256, canonicalizes the feature/config/artifact tuple, and emits binary, C initializer, JSON identity, and identity-derived label outputs.
- Bound every Make wrapper scalar and feature to the spec through repeated `--expect NAME=INTEGER` checks. A spec/wrapper mismatch fails before C bytes or a label can be emitted. `SATURN_DEMO_FRAGMENT_MODE` and the established `SATURN_DEMO_BSP_FRAGMENTS` are one validated value, including compatibility for callers using either name.
- Added validated `0|1` feature variables and the versioned diagnostic modes `0`, `1` (animation sweep), and `2` (scene transition). Replay and live input remain separate serialized booleans and all four combinations are accepted by the identity ABI.
- Capture now resolves the exact `saturn_build_identity` symbol from the ELF, requires its 404 bytes to reside in a file-backed `PT_LOAD`, validates its intrinsic ABI, reads the loaded target tuple after the existing exact-code identity wait and before telemetry, requires byte-for-byte ELF/target equality, and derives the accepted label from those compiled bytes.
- Archived the accepted A9A ELF/ISO/CUE from the exact paths in the accepted throughput report. The helper validates all inputs before copying, checks the CUE's ISO reference, refuses different pre-existing archive bytes or manifest content, and records capture/profile/config/measurement/ancestry evidence.
- Updated `[Unreleased]` changelog reasoning. No external source was copied or closely ported; this is a repository-specific ABI and evidence-boundary implementation based on the approved local design/plan (reuse mode: original repository-local implementation).

## RED evidence

All prescribed RED commands were run serially before production implementation:

1. `.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_archive_a9a_baseline.py`
   - Exit `1`.
   - Expected failure: `ModuleNotFoundError: No module named 'archive_a9a_baseline'`, surfaced as `AssertionError: A9A baseline archive helper is missing`.
2. `.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_gen_build_identity.py`
   - Exit `1`.
   - Expected failure: `ModuleNotFoundError: No module named 'gen_build_identity'`, surfaced as `AssertionError: Saturn build identity generator is missing`.
3. `.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_sourceboot_feature_identity.py`
   - Exit `1`.
   - Expected failure: `ModuleNotFoundError: No module named 'gen_build_identity'`.

The failures were caused by the missing Task 2 behavior, not malformed fixtures or syntax.

## Immutable A9A archive evidence

Archive command (exit `0`; it performs no build):

`.\.venv-saturn-tools\Scripts\python.exe tools\saturn\archive_a9a_baseline.py --capture-report docs\saturn\evidence\reports\a9a-step11-overlap-throughput-repaired-2026-08-05.json --profile-report build\saturn\ymir-desktop-launches\a9a-repaired-manual-20260805.json --archive-dir build\saturn\baselines\a9a-2026-08-05 --manifest docs\saturn\evidence\reports\completeness-sprint-a9a-baseline-2026-08-05.json --repo-root .`

Fresh post-copy `Get-FileHash -Algorithm SHA256` results:

- ELF, 8,721,516 bytes: `1905ec8d42ea00ea2c000b5f53dd88f2079ffda8ceb67bcd5879e8e96acfc2e2`
- ISO, 4,653,056 bytes: `1ccaef4f2a2d379d82879d3e823d84db135fdee1045d69aa8e0a60d150cfaf96`
- CUE, 88 bytes: `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`

Fresh ancestry commands and results:

- `git merge-base --is-ancestor 27cebc7e HEAD` — exit `0`
- `git merge-base --is-ancestor d5f70887 HEAD` — exit `0`
- `git merge-base --is-ancestor d7b04d61 HEAD` — exit `0`

The tracked manifest records measured mean presentation rate `5.294117647058823`, exact original/archive paths, the accepted capture identity, the existing `.ymir-profile`/profile-managed 32-Mbit cart launch evidence, the parsed reviewed build tuple, and all three ancestry results.

## Final GREEN evidence

The four required commands were rerun serially after the last implementation correction:

1. `.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_gen_build_identity.py`
   - Exit `0`; `Ran 8 tests`; `OK`.
2. `.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_sourceboot_feature_identity.py`
   - Exit `0`; `Ran 4 tests`; `OK`.
3. `.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_capture_sourceboot_throughput.py`
   - Exit `0`; `Ran 35 tests`; `OK`.
   - The intentionally failed CLI-report fixtures still record their expected argument/observation failures; the unittest suite is green.
4. `.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_archive_a9a_baseline.py`
   - Exit `0`; `Ran 8 tests`; `OK`.

Additional focused evidence:

- `C:\Qt\Tools\mingw1310_64\bin\mingw32-make.exe -f src\port\saturn\sourceboot\Makefile -n YAUL_INSTALL_ROOT=C:\missing SATURN_FEATURE_COMPLETE_MARIO_ANIMATION=2`
  - Exit `1` at Make parse time with the expected `SATURN_FEATURE_COMPLETE_MARIO_ANIMATION must be 0 or 1` rejection. The preceding Windows `find` diagnostic is unrelated and occurs because this was an intentionally toolchain-incomplete parse-only check.
- `git diff --cached --check` before the implementation commit — exit `0`.
- Global `git diff --check` remains nonzero only for the pre-existing unrelated dirty `.superpowers/sdd/2026-08-03-saturn-overlapped-render-pipeline/task-9-brief.md` blank line at EOF. A Task 2-scoped diff check is clean; the unrelated file was preserved.

## Self-review verdict and design decisions

Self-review verdict: PASS for the Task 2 source/test/archive scope.

- ABI order matches the approved interface exactly; magic/version/size and both reserved-zero fields are independently validated.
- Python serializes target integers big-endian, matching SH-2 target memory; byte-array hashes retain their literal SHA-256 order.
- Every mutation listed in the brief is rejected either intrinsically or by exact expected-ELF versus loaded-target tuple comparison. Each feature bit is also mutated separately.
- A label is produced only from validated identity bytes. When a hash-bound spec exists, the sourceboot staging label is the generator result from those same bytes; the older hand-authored directory tag remains only for pre-spec asset-generation invocations and cannot produce a linked identity-bearing target because the generated initializer is a required object prerequisite.
- The accepted historical A9A archive is never generated, rebuilt, or overwritten with different bytes. Later feature-off builds are descendants, not replacements.
- Material correction during self-review: spec values alone were insufficient because Make variables could compile different behavior. The final design makes every wrapper scalar/feature an explicit generator expectation before emission.
- Material correction during self-review: `SATURN_DEMO_FRAGMENT_MODE` now aliases the established BSP-fragment switch bidirectionally and rejects disagreement, preventing identity/renderer drift while retaining historical callers.

Independent review was not run in this worker task; controller-side independent review remains open.

## Open gates (not substituted by focused GREEN)

- Task 1's broad host/native-math-dependent gate remains outside this task's focused development evidence.
- No linked SH-2 target build or fresh ELF inspection was run, per the task instruction. Therefore target linkage, actual symbol address/size, and end-to-end generated-spec consumption remain open target evidence.
- No Ymir/headless/desktop capture or manual controls/visual check was run. The new loaded-identity capture path is host-tested but awaits a later exact target artifact.
- The broad native-math gate was not run, as explicitly prohibited.
- Controller-side independent review and active plan/SDD ledger reconciliation remain open and controller-owned.

## Independent-review fix round 1 of 5

Status: source-complete at `8c97fd4e` (`fix(saturn): close Task 2 identity review gaps`), pending controller-side independent rereview. The first independent review recorded `SPEC FAIL / QUALITY FAIL`; its three findings were accepted and repaired. The controller-owned active plan and progress ledger were again left untouched.

### Corrections

- Added every omitted compiler-affecting sourceboot control to the canonical effective-config digest and Make wrapper expectations: atan2 variant, demo-path enable and view radius, slave rendering, camera idle/discovery/range controls, flat BSP fragments, Fast3D Q16 trace, and experimental geo-walk suppression. Each legal mutation now changes the 404-byte identity and identity-derived output label, while wrapper/spec drift fails before emission.
- Replaced the archive helper's permissive FPS/target/profile checks with exact canonical hashes for the accepted full cadence record and launch report plus exact target identity content. The helper reads the actual profile's `Ymir.toml`, verifies SHA-256 `33a155e765dac9bd2871ca725ed7f444d1fbbb95876d43c955c0b688e6931566`, parses `DRAM` / `32Mbit`, requires the successful monitored command for the accepted CUE, and records both launch logs and their hashes.
- Moved conflicting-manifest rejection ahead of archive directory creation and all artifact copies. Source sizes are recorded before copying, and existing archive bytes remain fail-closed by exact SHA-256.
- Regenerated only the tracked manifest metadata from the already accepted evidence and archive bytes. No target build, Ymir run, capture, or ELF/ISO/CUE replacement occurred.

### Permanent RED evidence

1. `.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_gen_build_identity.py`
   - Exit `1`; ten mutation subtests failed because all ten omitted compiler controls retained the baseline effective-config hash. This was the missing identity behavior under review, not a fixture or syntax failure.
2. `.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_archive_a9a_baseline.py`
   - Exit `1`; the new exact cadence/target/profile/launch and pre-copy conflict contracts failed on the absent canonical-evidence API and permissive archive behavior. This was the missing archive behavior under review.

### Final GREEN evidence

The required focused gates were run serially after the final changes:

1. `.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_gen_build_identity.py`
   - Exit `0`; `Ran 9 tests`; `OK`.
2. `.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_sourceboot_feature_identity.py`
   - Exit `0`; `Ran 5 tests`; `OK`. This includes a permanent contract for all ten Make wrapper bindings.
3. `.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_capture_sourceboot_throughput.py`
   - Exit `0`; `Ran 35 tests`; `OK`. The emitted failed JSON documents are intentional negative CLI fixtures.
4. `.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_archive_a9a_baseline.py`
   - Exit `0`; `Ran 12 tests`; `OK`.

The no-build real archive revalidation command also exited `0` against the tracked manifest. Post-validation archived hashes and sizes remained exact:

- ELF, 8,721,516 bytes: `1905ec8d42ea00ea2c000b5f53dd88f2079ffda8ceb67bcd5879e8e96acfc2e2`
- ISO, 4,653,056 bytes: `1ccaef4f2a2d379d82879d3e823d84db135fdee1045d69aa8e0a60d150cfaf96`
- CUE, 88 bytes: `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`

The accepted full measurement canonical hash is `45685dfb2b013356e0c1e19a7b51fd621cd7a941f37b066d15b01a29bde2fb4b`; the exact target identity hash is `eee27f6d08b1fa473bbbf3c715972202020733511b1b012404f2cf7d70651727`; and the exact successful launch-report hash is `0c8e0d3617a8cec36b13818244980e6e3bdbf7c579851261f2622daea37b53a7`.

### Fix-round self-review and remaining gates

Self-review verdict: PASS for the three review findings and Task 2 source/test/manifest scope. The implementation commit included its `[Unreleased]` changelog reasoning, and `git diff --cached --check` passed before commit. No third-party source was copied or closely ported; reuse mode remains original repository-local implementation based on the approved local design.

Independent rereview remains open and controller-owned. No linked SH-2 target build, ELF linkage inspection, Ymir execution, manual visual/control gate, or broad native-math gate was run; those gates remain unchecked and must not be inferred from the focused host tests.
