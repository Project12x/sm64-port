# Handoff — SH-2 Native Math Purge (2026-07-29)

## Controller rule — mandatory

This sprint is governed by `docs/superpowers/plans/2026-07-29-sh2-native-math-purge.md` and the ledger at `.superpowers/sdd/2026-07-29-sh2-native-math-purge/progress.md`.

The controller **must not send a final response** while any sprint task is pending, any review is outstanding, or a dispatched agent is active. Agent completion is not a user-facing stopping point. The mandatory loop is:

1. Receive agent completion.
2. Read its report and inspect its commit.
3. Dispatch the required scoped review.
4. Resolve review findings through the fix loop.
5. Update the ledger.
6. Dispatch the next task and wait.

Only the entire sprint completing, a genuine external decision, or an explicit user stop authorizes a final response.

## Current branch and checkpoint

- Worktree: `sm64-port/.worktrees/sh2-native-math-purge`
- Branch: `sh2/native-math-purge`
- Task 2 technical checkpoint: `3f249d8` (`fix: bind native math A/B evidence to build roles`)
- Scope decision: owner-approved Task 2 reslice on 2026-07-29; the completed
  seam is `atan2s`/`atan2_lookup`, not the broader original `math_util`/trig
  target.

## Completed, review-clean tasks

- **Task 0:** native-math census gate. Renderer route is immutable and graph-derived; spill/reload calls and the pinned worker callback edge are covered.
- **Task 1:** port-owned hot render math is Q16. Route visual/checkpoint evidence is byte-identical to Pipe 8. Differential and mutation checks are wired into `verify-all`.

## Task 2 — review-clean and closed at the measured atan2 seam

Implementation commits:

- `30b9baa` / `8f39fd8`: source-simulation route audit and enforceable replay-only math capture.
- `d1e9043`: deterministic 2,000-tick SMC1 corpus capture.
- `376b032`: `TARGET_SATURN` Q16 atan2 seam.
- `32eeb25` / `363bce2`: portable host-fixture temporary-directory and MSYS compiler selection.
- `f07fdd9`: fresh post-conversion capture evidence.
- `24cf416` / `3f249d8`: remediation gates and build-role-bound A/B evidence.

Fresh target result:

- Static audited simulation-helper total falls from 584 to 582; the converted
  callers have no remaining audited helper edges.
- Two independent, role-bound 2,000-tick target A/B comparisons pass. Final
  Mario XYZ bit patterns are exact, L-infinity positional divergence is
  **0.0 world units**, and timer/camera/output/reject gates are equal and
  valid.
- The route's `sim_frt_ticks_accum` falls from 73,447,067 to 71,594,372
  (-2.5225%). This is a simulation-kernel result, not an end-to-end or retail
  performance claim; the available emulator wall time is slower for Q16.
- Broader vec3, `approach_*`, and `sqrtf`/`sinf`/`cosf` conversions are
  explicitly deferred for follow-on planning after Task 3.

Primary evidence: `docs/saturn/evidence/reports/task2-remediation-sbr4-stage2-ab-2026-07-29.json`, `docs/saturn/evidence/reports/task2-remediation-sbr4-stage2-ab-repeat-2026-07-29.json`, and the role-bound capture artifacts beside them.

## Required next action

The owner approved
`docs/superpowers/specs/2026-07-29-saturn-camera-q-seam-design.md`, including
the separate non-vacuous default-camera route. The reviewed executable plan is
`docs/superpowers/plans/2026-07-29-saturn-camera-q-seam.md`.

Choose the execution workflow, then begin that plan at Task 1:

1. **Subagent-Driven Development (recommended):** one fresh implementation
   subagent per task, followed by spec-compliance and code-quality reviews.
2. **Inline Execution:** execute the same tasks serially in this task with
   the plan's mandatory review gates.

No Task 3 production code has started. Do not skip the additive raw transport,
HWRAM reclamation, writer-closure, or post-differential format-freeze gates.

## Environment facts

- Use the worktree-local `.msys-home` and `build/tmp` for MSYS home/temp.
- Initialize `third_party/libyaul` at pinned commit `6012f79f...` before top-level verification.
- Build target with Yaul `COMPILER_PATH`; run the host Q16 fixture with `COMPILER_PATH` unset so MinGW host GCC and assembler remain paired.
- Use fresh ELF/CUE symbols, Ymir build-agent2, and `--dram-cart` for capture.
