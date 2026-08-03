# SDD ledger — plan: docs/superpowers/plans/2026-08-03-saturn-overlapped-render-pipeline.md

Workspace: D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge
Branch: sh2/native-math-purge
Plan commit: 1c262766586388937f1993d7accd7a6fda05b49f
Execution base: 1c262766586388937f1993d7accd7a6fda05b49f

Preflight: existing linked worktree verified; plan conflict scan clean.
Preserved dirty scope: native-math verifier edits, prior audits/evidence,
temporary observation/build directories, and route files remain unrelated and
must not be staged by this plan.

Task 1: **BLOCKED**. Full-range quality review found that implementation
`4a8fe1ce` suppresses the whole `geo_process_root()` walk even though it owns
animation progression and stateful painting/DDD-warp, water, camera, moving-
texture, flying-carpet, and matrix-derived object updates. The Critical
optimization finding remains unresolved; Task 1 is not source-complete.

Runtime safety is **GO / fail-closed** at `77ee306c`, documented by
`19963fd3`: `sourceboot_run_source_tick()` cannot call the reserved
scene-graph suppression setter, so every accepted Saturn build retains the
full geo walk. Focused source policy PASS (2), runtime-contract executable
PASS, and profile decoder PASS (13, 1 skip). The policy/counter ABI remains
dormant; normal walks increment and suppressed walks remain zero.

No Task 1 optimization CUE build or Ymir launch is authorized while the
Critical finding is open. No target build, Ymir launch, or strict native-math
census was run by this task.
Baseline: `python tools/saturn/test_tools.py` timed out after 120 seconds while
still running; every emitted test before timeout passed. This is incomplete
baseline evidence, not a green aggregate gate and not a reported test failure.
Current aggregate: `python tools/saturn/test_tools.py` ran 193 tests in 204.286
seconds and failed with 17 pre-existing Bob parity route-schema errors from
preserved dirty route state. Its two Task 1 Fast3D-profile fixture failures
were corrected and their focused class now passes. The Make aggregate compiles
the runtime contract under the required wrapper but then exits nonzero from
the MSYS/Windows executable-path handoff; the exact compiler invocation and
resulting runtime-contract executable both pass directly.

Owner scope correction: this is a Saturn-exclusive repository. `a00cdd17`
removes the temporary PC/N64 `area.c` syntax gate; PC/N64 compilation is not a
Task 1 gate. “Ordinary interpreted” now explicitly means the Saturn
interpreted renderer. Focused Saturn source-policy, runtime-contract, and
profile tests pass; no target/Ymir work was run.

Quality safety fix round 2/5 is documentation-only. The tracked plan now leaves
the unsafe suppression Step 5 unchecked/BLOCKED and marks the CUE/Ymir step
NOT AUTHORIZED. No code or tests were run in this documentation transition;
only `git diff --check` is required before the tracked documentation commit.

Task 1D: **ACTIVE in quality-fix round 2/5**, not source-complete. Runtime
containment is implemented at `98f26f26`; review-fix round 1 is `fe1074b8` and
quality-fix round 2 is `98670c48`. The scoped spec rereview marks both prior
Important findings **ADDRESSED** with no new Critical or Important findings.
Independent quality review is **NO-GO**:
trailing whitespace bypassed the demo/replay prerequisite check while still
activating the compile definition/tag, the tests admitted demo-keyed tagging
and setters outside the direct branch, and the plan contradicted the sealed D1
exception. Round 2's TDD red was 4 tests with 3 failures; after one intermediate
leading-space correction, green was 4 tests in 11.766s. The Makefile now rejects
observable whitespace and malformed values before activation. Tests compare
otherwise identical demo+replay configurations, scan the complete normal
compile-time path, and discover tool paths without clone-specific constants.
The plan/spec/evidence consistently define D1 as the sole owner-authorized,
non-promotable exception; it does not reopen A1. Quality rereview, the known
host-wrapper path gate, target build, and Ymir remain open. A1 remains
**BLOCKED**. No target build or Ymir run occurred.
