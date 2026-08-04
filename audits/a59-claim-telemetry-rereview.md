# A5.9 claim telemetry re-review — 2026-08-04

## Scope and verdict

**Verdict: GO for the serialized target-build gate.** Independently reviewed
the focused repair `b990b24e` atop the prior NO-GO audit `5a84f543` in the
exact `sh2/native-math-purge` worktree. The retirement telemetry now publishes
before the positive retirement release marker in both the SH-2 callback and
host fixture paths. No new source issue was found. This verdict does not itself
claim a target-safe cache result, visible dual-CPU overlap, or FPS improvement.

## Findings

### Critical

None.

### Important

None.

### Minor

None.

## Repaired contract

- `render_job_slave_entry()` completes its queue poll, publishes
  `telemetry.retired_generation` and `telemetry.retired_sequence`, crosses the
  existing compiler publication fence, and only then stores the runtime
  `retired_sequence` that releases the master's wait. A master that observes
  positive retirement can no longer interleave before the telemetry payload
  stores have executed.
- The non-SH host path uses the same payload-before-release ordering, keeping
  the executable fixture aligned with the target source contract.
- The new source mutation test checks both paths independently and fails if
  either retirement marker moves before `telemetry_retire()`.
- The focused change does not alter queue claims, graph dependencies, callback
  execution, payload ownership, profile layout, HUD assembly, or scheduling
  policy. The earlier review's positive findings for disjoint master/slave
  counters, uncached shared placement, exact phase mapping, bounded local wait
  counting, append-only profile ABI, and bounds-safe HUD output remain intact.

## Evidence run

- `git diff --check 5a84f543..b990b24e`: PASS.
- Direct MinGW C11 `-std=c11 -Wall -Wextra -Werror` render-job runtime fixture:
  PASS (`render job runtime fixture: PASS`).
- Focused Python runtime-source, live-cutover-source, and profile-decode suites:
  PASS, 20 tests run, 1 skipped.
- Direct MinGW C11 `-std=c11 -Wall -Wextra -Werror` VDP2 frame fixture: PASS.
- No target build, CUE, Ymir run, cache observation, or FPS claim was performed
  by this re-review.

## Remaining target gates

1. Run one serialized Saturn target build and confirm link/section budget plus
   sole CPU-DUAL callback ownership.
2. Boot the fresh CUE in desktop Ymir and read the visible
   `QM/QS/QN/QR/QW/QF/QQ` values.
3. Use that target observation—not the host delayed-slave schedule—to choose
   the next scheduler repair and evaluate any performance change.
