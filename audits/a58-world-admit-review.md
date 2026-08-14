# A5.8 WORLD_ADMIT / merge metadata review — 2026-08-04

## Context

- Prior agent: Codex
- Commit reviewed: `94b8141e feat(saturn): publish terrain queue merge metadata`
- Scope: dormant WORLD_ADMIT/WORLD_LOWER terrain route only.  No files were
  edited and no branch/worktree was switched.
- Files examined:
  `src/port/saturn/gfx/saturn_demo_render.c`,
  `saturn_render_job_{queue,graph,runtime,bridge,payload_bank}.c`,
  `saturn_render_output_bank.c`,
  `tools/saturn/render_job_{terrain_route_source,bridge}_test.c`, and the
  active plan/evidence/STATE/ARCHITECTURE/CHANGELOG updates.
- Commands run: `git status --short`, `git show 94b8141e`, `git show --check`,
  targeted `rg`, direct Qt MinGW C11 `-std=c11 -Wall -Wextra -Werror` terrain
  route and bridge fixtures.

## Verdict: NO-GO

The result-count terminal record and the no-retransform lower callback are
sound as far as they go, and the dormant-path/no-activation claim is true.
However, WORLD_ADMIT's newly introduced descriptor-keyed completion record is
only written; no lower callback or other consumer ever reads or validates it.
Therefore it cannot actually be the documented release edge between an exact
admit descriptor's transformed data and WORLD_LOWER.  Do not advance this
increment to source-GO until the dependency identity is consumed and tested.

## Findings

### Critical

1. **WORLD_LOWER can run without establishing that its required WORLD_ADMIT
   record exists or matches.** `demo_terrain_queue_world_admit()` publishes
   `s_terrain_admit_metadata[job_index]` at
   `saturn_demo_render.c:1994-1997`, but the only references to that array are
   its declaration and that producer call (`rg` finds no reader).  In
   particular, `demo_terrain_queue_world_lower()` at :2004-2028 directly
   classifies `job->input_offset..+input_count` after binding its own output;
   it never reads a predecessor metadata record, checks a predecessor job
   index/generation/sequence/lane, or verifies that a predecessor was a
   terminal `WORLD_ADMIT`.

   The generic graph does prevent a descriptor from being claimed before the
   dependency mask's predecessor is DONE, but it does not encode which admit
   record supplies this lower descriptor's transform data.  That mapping is
   neither published nor validated by the renderer callback.  A future graph
   can therefore make a lower job eligible after an unrelated admit job and
   this callback will accept it.  The active documentation overstates this as
   a descriptor-keyed release between transform payload writes and the lower
   callback.

   Required repair: give WORLD_LOWER an immutable predecessor identity (or a
   checked graph API that returns it), then read/revalidate the corresponding
   P2 admit record before classification: generation, exact admit job index,
   terminal state/type/callback, writer/claim lane, nonzero sequence, and the
   transform count/bounds required by the lower span.  Add mutation coverage
   proving a missing, stale, wrong-index, wrong-lane, and non-DONE admit record
   fails closed.

### Important

None beyond the critical dependency-binding hole.

### Minor

The route source fixture only scans for producer and reader symbol names.  It
does not prove that `WORLD_LOWER` consumes admit metadata, so it passes even
with the missing edge above.  Extend it (or preferably add an executable
contract fixture) as part of the repair.

### Discrepancies between summary and code

- `ARCHITECTURE.md` and the evidence report say WORLD_ADMIT's record is the
  P2-visible release before graph runtime can expose DONE / lower can use the
  transform payload.  The code writes `s_terrain_admit_metadata`, but no
  consumer reads it; generic graph DONE alone is the only actual lower
  eligibility condition.
- The evidence says the transform completion is keyed by the exact claim.
  Its producer is keyed correctly by its own claim, but `WORLD_LOWER` has no
  checked relation to that key, so the end-to-end claim is not established.

## What was done well

- `demo_terrain_queue_world_lower()` now calls
  `demo_terrain_compact_transformed()` rather than the transform+compact
  wrapper, so it does not silently repeat transforms (:2019-2027).
- Result metadata is claim-derived, P2-visible, records the terminal count
  and sequence, and is checked against the output bank before the DONE reader
  exposes payloads (:1715-1789, :2034-2063).
- Queue-reachable terrain work does not infer ownership from `begin == 0` or
  `s_slave_begin`; the fixed split remains in the explicitly legacy adapter
  (:1964-1976).  The callbacks remain `__attribute__((unused))`, and no
  runtime activation appears in the renderer.
- Both focused host checks passed:
  `render job terrain route source fixture: PASS` and
  `render job bridge fixture: PASS`; `git show --check 94b8141e` was clean.

## Recommended next actions

1. Repair and test the exact WORLD_ADMIT-to-WORLD_LOWER dependency identity;
   rereview before calling A5.8 source-complete.
2. Keep CPU-DUAL/runtime activation and target builds blocked.  This is a
   dormant source contract defect, not a reason to regress to the legacy path.
3. Correct the active plan/evidence wording after the repair; preserve the
   existing honest claim that no CUE/Ymir/FPS result has been produced.
