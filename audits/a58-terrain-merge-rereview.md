# A5.8 terrain merge repair re-review — 2026-08-04

## Context

- Prior agent: Codex subagent
- Task claimed: repair the dormant terrain merge so every current-generation
  `WORLD_LOWER` descriptor must be terminal and identity-matched before merge.
- Commits reviewed: `faa949d5525fdadb4243c9eb0691e9fcd1bd2d4c` and
  `c97f6343933aa82c9fe17f532e364162c9159ea7`.
- Files examined: `saturn_demo_render.c`, `saturn_render_job_queue.[ch]`,
  `saturn_render_job_graph.[ch]`, the graph fixture, the route-source fixture,
  active plan/evidence, and `CHANGELOG.md`.
- Scope: source-only dormant route. No target build, CUE, Ymir run, activation,
  or FPS claim was reviewed.

## Findings

### Critical

None.

### Important

1. **A valid all-culled terrain result cannot pass merge validation.**
   `demo_terrain_queue_assemble_merge_spans()` accepts a terminal lower whose
   published `record_count` is zero and still adds its stream, but it builds no
   result identities. It then calls
   `sm64_saturn_render_job_graph_validate_terrain_merge(..., count = 0)`, which
   unconditionally returns false. This conflicts with
   `sm64_saturn_terrain_depth_bins_build_streams()`, which explicitly supports
   nonzero stream count with zero records, and with normal culling behavior:
   every lower job can legitimately produce zero visible primitives. The
   dormant path therefore fails a valid empty frame. Repair either the graph
   validation contract to accept zero identities while separately proving the
   complete lower-descriptor set, or have the assembler special-case a fully
   validated empty merge. Add a focused empty-output fixture before activation.

2. **The requested nonterminal-assembler mutation remains source-inspected,
   not runtime-executed.** The previous review required a host mutation proving
   that one READY or CLAIMED `WORLD_LOWER` makes assembly return false instead
   of omitting it. The graph fixture only proves that `published_job()` can see
   a lower descriptor before DONE; it never invokes the static assembler. The
   route fixture checks source substrings (`published_job`, pointer equality),
   so it does not execute the failure path. The code inspection is convincing,
   but the explicitly required regression test is still absent.

### Minor

None.

### Discrepancies between summary and code

- The plan/evidence accurately says the accessor and source route are green,
  but the original review's requested runtime/host mutation has not been
  implemented as an executable behavioral test.
- The claimed incomplete-descriptor repair itself is present: this is not a
  repeat of the original omission defect.

## What was done well

- The assembler enumerates `s_render_job_graph.count` through the new
  P2/cache-through `published_job()` accessor. For every current
  `WORLD_LOWER`, it separately requires `done_job()` to return the identical
  descriptor pointer before any result metadata or payload is consumed.
  READY, CLAIMED, FAILED, quarantined, stale-generation, and descriptor
  mismatch cases therefore fail closed by inspection.
- `published_job()` grants descriptor inspection only; it does not grant
  claimed or terminal payload access. Its queue and release-generation reads
  use the SH-2 cache-through alias.
- Metadata is revalidated against generation, sequence, count, queue/output
  ownership, and payload-bank lane. Descriptor/local-output identities are
  assembled in deterministic descriptor/index order before the master-owned
  stable depth-bin pass.
- The merge remains dormant and marked `unused`; neither commit installs a
  CPU-DUAL callback or changes the legacy live renderer.

## Verification

- Direct Qt MinGW GCC C11/Werror route-source fixture: PASS.
- Direct Qt MinGW GCC C11/Werror graph fixture linked with queue and graph
  implementations: PASS.
- `git diff --check faa949d5^..c97f6343`: PASS.

## Recommended next actions

1. Add an executable merge-contract fixture covering READY, CLAIMED, FAILED,
   mismatched generation/identity, and all-lowers-DONE with zero total records.
2. Make the all-zero valid case succeed without weakening complete descriptor
   enumeration or terminal checks.
3. Re-run this scoped review; only then continue toward Mario parity and the
   atomic CPU-DUAL cutover.

## Verdict

**NO-GO for marking the dormant terrain merge source-complete or activating
it.** The original incomplete-lower defect is repaired, and the ownership/
ordering design is sound, but the valid all-culled case is broken and the
required behavioral regression fixture is still missing.
