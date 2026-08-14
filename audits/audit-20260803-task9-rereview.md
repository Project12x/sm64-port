# Peer Audit — 2026-08-03

## Context

- Prior agent: Codex
- Task claimed: Correct Task 9’s worker cancellation/retirement behavior and add the second, bounded Mario primitive-classification phase.
- Files examined: `src/port/saturn/gpl/slavedriver_dual_worker.c`, `src/port/saturn/gpl/slavedriver_dual_worker.h`, `src/port/saturn/gfx/saturn_demo_render.c`, `tools/saturn/dual_actor_worker_test.c`, `Makefile.saturn.mk`, `task-9-report.md`, and `docs/superpowers/plans/2026-08-02-saturn-dual-sh2-vdp-pipeline-sprint.md`.
- Commands run: `git -c safe.directory=… status --short`, history/diff/stat/name-status for `edc3e78..6e1a455`, `git diff --check`, and static source searches. Per the active safety restriction, no Make, MSYS/bash/sh-elf, target build, or Ymir command was run.

## Findings

### Critical

1. **Transform timeout fallback classifies master-owned vertices through the stale peer alias.**

   In `src/port/saturn/gfx/saturn_demo_render.c:983-990`, a failed split transform changes only `s_actor_slave_begin` to the full vertex count before serially regenerating all results. It does not also update `s_mario_transform_context.vertex_slave_begin`, which remains the original `212` from line 951. The immediately following classification callback uses the context field—not the global—at `:889-899` to select result ownership. Thus its master lane reads vertices `[212, 424)` via `sm64_saturn_dual_frame_read_range(0, 1, s_actor_results)` even though the serial fallback produced those results as master-owned cached writes. On SH-2 this selects the cache-through peer alias and can consume stale output. This violates the claimed safe serial fallback, determinism, and uncached sequence ownership contract. Set `s_mario_transform_context.vertex_slave_begin = s_actor_slave_begin` when entering this fallback (or derive the ownership boundary from one authoritative value), and add a target-contract-capable regression.

### Important

1. **The reported serial/split test does not exercise Task 9’s actual classification handoff or the fallback aliasing path.**

   `tools/saturn/dual_actor_worker_test.c:150-170` transforms and classifies local `actor_vertex_t` arrays with its own `transform_range()` and `classify()` helpers. It never calls `demo_classify_mario_range()`, reads `s_actor_ref_frame_bank`, or evaluates ownership through `sm64_saturn_dual_frame_read_range()`. The only renderer source check (`:93-122`) confirms identifier presence and context pointer hygiene. Consequently it cannot detect the critical stale-peer-alias bug or prove compact-ref sequence publication/output equivalence.

2. **The delayed host-thread timeout test is timing-dependent and can fail to enter its intended timeout path on a sufficiently slow host.**

   `tools/saturn/dual_actor_worker_test.c:78-89` sleeps the slave for 1000 ms. `slavedriver_dual_worker.c:130-133` times out after a fixed 10,000,000 busy-poll iterations rather than a clock deadline. If that polling loop takes at least the sleep duration, the worker completes normally and the assertion at test line 145 (`sm64_saturn_dual_worker_run(...) || ...`) fails. The test does positively check a post-return write when the timeout is induced, but it does not reliably induce one. Use an explicit test synchronization gate plus a bounded deadline/cancel trigger (or a host-only injection seam) so the regression is deterministic.

### Minor

1. **The target cancellation path intentionally has no final bounded escape.**

   After the initial timeout, `slavedriver_dual_worker.c:145-154` spins until `done`; a callback that does not poll cancellation or otherwise never returns stalls the caller indefinitely. This is safer than clearing shared spans early and is documented, and the current render callbacks poll, but it means the broader “no deadlock” claim relies on every future callback respecting the convention. Consider an assertion/contract annotation or a watchdog diagnostic so a violated callback contract is distinguishable from a normal long job.

### Discrepancies between summary and code

1. The report says a serial fallback may safely reuse the complete result range after return (`task-9-report.md:21-24`), but the transform-fallback classification path retains the obsolete split in `s_mario_transform_context.vertex_slave_begin`, as detailed above.

2. The report says the fixture “compares serial/split vertex coordinates and primitive order, colors, and corner coordinates byte-for-byte” (`task-9-report.md:58-59`). That is true for the fixture’s independently implemented local helpers, not for the renderer’s new compact-ref dispatch, frame-bank publication, cache-through reads, or recovery path.

## What was done well

- The corrected worker correctly postpones `active`/`cancel` clearing until after `done`, and Windows-host execution joins the actual thread before return (`slavedriver_dual_worker.c:139-168`). This prevents the old clear-before-retirement race when callbacks honor cancellation.
- The second actor phase is structurally after terrain merge and after transform-worker return (`saturn_demo_render.c:2473-2481`, `:983-1037`), uses disjoint primitive ranges, and leaves VDP1 allocation/emission on the master.
- Compact refs are bounded to one four-byte record per primitive and publish through their dedicated uncached frame bank (`saturn_demo_render.c:201-210`, `:912-914`).
- `git diff --check edc3e78..6e1a455` passed; no whitespace errors were found.

## Recommended next actions

1. Fix the transform-fallback boundary mismatch and add a regression that forces that fallback while validating all actual compact refs/results through the renderer’s ownership helpers.
2. Make the cancellation test deterministic; retain the real Windows thread but coordinate slave entry and cancellation without using CPU-speed-dependent polling duration.
3. Add host coverage of actual two-phase actor classification: normal split equivalence, transform/classification failure fallbacks, lane publication counts/sequences, and no post-fallback writes.
4. Re-run the scoped host test only after the DLL/MSYS environment is intentionally repaired; target build/emulator verification remains pending under the active restriction.
