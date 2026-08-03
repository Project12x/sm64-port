# SDD ledger — plan: docs/superpowers/plans/2026-08-03-saturn-overlapped-render-pipeline.md

Workspace: D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge
Branch: sh2/native-math-purge
Plan commit: 1c262766586388937f1993d7accd7a6fda05b49f
Execution base: 1c262766586388937f1993d7accd7a6fda05b49f

Preflight: existing linked worktree verified; plan conflict scan clean.
Preserved dirty scope: native-math verifier edits, prior audits/evidence,
temporary observation/build directories, and route files remain unrelated and
must not be staged by this plan.

Task 1: implementation commit `4a8fe1ce` created; source-policy and runtime
contract constituent tests pass. Independent spec/quality reviews are pending,
so Task 1 remains active rather than source-complete. Controller retains the
serial experimental CUE/Ymir gate; no target build, Ymir launch, or strict
native-math census was run by this task.
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

Spec-fix round 1: `658d5ad9` adds a focused host syntax gate for
`src/game/area.c` without `TARGET_SATURN` and includes `<stdbool.h>` outside
the Saturn-only header guard. Red compiler evidence showed unknown `bool` and
`false`; the direct wrapper compiler command is green after the fix. The Make
target prints the same invocation but still exits nonzero in the inherited
MSYS/native executable handoff environment, so its direct wrapper constituent
is recorded as the executed green evidence. No target/Ymir work was run.

Spec-fix round 2: `9904097e` replaces the standard-header dependency with
project-native `s32`/`FALSE` in the shared guard. The focused syntax gate now
uses `-DTARGET_N64 -nostdinc` and the actual repository/build include paths.
Its red command failed on missing `stdbool.h`; the amended gate and the
source-policy test pass. No target/Ymir work was run.
