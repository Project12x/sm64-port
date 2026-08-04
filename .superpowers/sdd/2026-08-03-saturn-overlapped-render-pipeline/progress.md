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

Task 1D: **source-complete; target CUE/Ymir pending**. Runtime
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
non-promotable exception; it does not reopen A1. Both scoped rereviews are GO.
Focused source policy passed 4/4 and a fresh DLL-safe direct execution of
`build/saturn/host-tests/runtime-contract-test.exe` exited 0. The Make wrapper
path-translation failure remains an infrastructure gate; target build and Ymir
remain open. A1 remains **BLOCKED**. No target build or Ymir run occurred.
Task 1D: complete (commits 98f26f26..1e828879, spec and quality rereviews
clean). Serial `make -B -j1` through the audited wrapper built the tagged
diagnostic CUE in 258.5 seconds. The owner ran it in Ymir with the project
32-Mbit DRAM profile and observed approximately 2 FPS with no obvious
improvement. This is a negative upper-bound result: A1 stays BLOCKED for
correctness and is deferred behind Emergency A9.0. The runtime-contract Make
wrapper WinError 5 remains an infrastructure gate, not a D1 completion blocker.

2026-08-04 task transition: the valid desktop-Ymir run is launch-stable but
still visibly slow. The owner reports VDP1 ≈2 FPS and VDP2 ≈60 FPS. This
supersedes the earlier reversed counter interpretation and invalidates VDP2
as the active bottleneck; headless BIOS traces are not target evidence because
they did not load paired ELF main bytes at linked address. Emergency A9.0 is
source-complete/launch-stable with no claimed FPS gain. Task 2/A2 is ACTIVE at
Step 1: failing immutable snapshot-bank lifecycle tests. Its remaining gates
are the red test, minimal state machine, sourceboot capture integration,
pointer-free static proof, focused host gates, two reviews, and a later target
test. No build or emulator run occurred in this documentation transition.

Task 2/A2: **source-complete; verification/review pending**. The task adds a
Saturn-only fixed-width two-slot snapshot bank with fenced `FREE → WRITING →
READY → RENDERING → COMPLETE → FREE` lifecycle plus terminal quarantine. The
master alone captures live SM64 state after every source tick; the published
record retains scalar Mario/camera data and generated-bank identifiers only.
The watched direct-host red failed on the intentionally missing header/API;
the normal wrapper first hit the known `\\d\\Code...` Windows-Python path
translation failure. With an explicit forward-slash repository-root override,
`verify-render-snapshot-bank` and `verify-dual-frame-bank` passed. The former
also runs the source pointer-field guard. `verify-runtime-contracts` compiled
but failed at the preserved terrain command byte assertion
`runtime_contract_test.c:4018`; do not credit that gate. No target build/Ymir
was run. Task behavior/docs commit: `56866662`
(`feat(saturn): add immutable render snapshot banks`). Independent
specification and quality reviews, runtime-contract closure, and target
evidence remain required.

Task 2/A2 fix round 1: the first independent specification review was NO-GO
for terminal-quarantine reset escape, missing target cache-through accesses,
and a missing public generation validator. Red lifecycle/source/API tests
observed each gap. The repair preserves every non-FREE state through reset,
uses `CPU_CACHE_THROUGH` release and peer-payload accessors on SH-2 with host
identity aliases, and exports `sm64_saturn_render_snapshot_generation_valid`.
The focused snapshot-bank gate is green; runtime contracts still fail at the
preserved terrain-command comparison and no target/Ymir gate was run. Fresh
independent spec and quality review remain required.
