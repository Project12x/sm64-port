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

## Superseding camera direction (2026-07-31)

The owner has superseded the function-by-function camera-Q seam direction with
`docs/superpowers/specs/2026-07-31-saturn-fixed-point-camera-core-design.md`.
The old plan remains historical evidence and is not the executable next plan.

The new direction is:

1. retain the original float camera only in a separate baseline/replay build;
2. measure source, held-camera bypass, and minimal fixed follow builds on the
   same 2,000-tick BOB route to attribute camera cost quickly;
3. proceed to a scene-neutral production fixed camera only when the recorded
   attribution thresholds pass;
4. target hybrid semantic parity and feel equivalence rather than private-state
   or per-tick vector identity; and
5. expand capability-by-capability toward the full game with no runtime float
   camera fallback.

BOB is the current proving ground, not the production boundary or end goal.

### Work from the camera-Q sprint that remains authoritative

- immutable BOB baseline/default-camera routes and role-bound replay builds;
- SCC1 raw camera transport, schema/version discipline, and capture decoder;
- camera FRT timing plus whole-simulation and render-workload counters;
- SH linked-code/helper audits and object disassembly;
- range capture and fixed-format evidence;
- HWRAM staging, memory-map verification, and transport budgeting;
- writer/call-graph closure, reinterpreted as the inventory of authoritative
  camera inputs, events, transitions, and compatibility consumers;
- host differential tools, retargeted to semantic windows and feel metrics;
  and
- the MSYS-safe SH tool launcher and correctly provided runtime DLLs.

Existing additive Task 3 transport/probe changes in the working tree are
preserved. Do not delete or rewrite them merely because the camera architecture
changed.

Retired assumptions are the complete private-global Q shadow, exact write-order
mirroring, function-by-function translation, per-tick float import/publish, and
runtime fallback into the source camera.

### Reference and math strategy

- The in-tree source camera remains the semantic oracle.
- `malucard/sm64-psx` at the recorded unlicensed pin is behavior/architecture
  study only. Its fixed Q20.12 camera provides the strongest capability map for
  normal modes, obstruction, transitions, splines, shakes, and cutscenes, but
  no code may be copied or closely adapted.
- Pinned MIT libyaul is the first target-native math candidate: fixed multiply,
  DIVU, trig/atan2, square root, vectors, normalization, and look-at are adopted
  selectively only after camera-closure timing and disassembly.
- Sonic Z-Treme, SlaveDriver, and Jo Engine remain pattern sources for compact
  follow state, SH-2 scheduling/approximations, and the already attributed
  multiply primitive respectively.
- Do not vendor `libfixmath` or `libfixmatrix` for this sprint. A small,
  auditable camera-math facade over existing target primitives is preferred.

## Required next action

Finish written review of the 2026-07-31 fixed-camera design, then write a new
implementation plan from that spec. The new plan begins with the Phase A
attribution triplet and incorporates the useful additive transport/probe work
already present; it must not resume the superseded camera-Q plan as written.

## Environment facts

- Use the worktree-local `.msys-home` and `build/tmp` for MSYS home/temp.
- Initialize `third_party/libyaul` at pinned commit `6012f79f...` before top-level verification.
- Build target with Yaul `COMPILER_PATH`; run the host Q16 fixture with `COMPILER_PATH` unset so MinGW host GCC and assembler remain paired.
- Use fresh ELF/CUE symbols, Ymir build-agent2, and `--dram-cart` for capture.
