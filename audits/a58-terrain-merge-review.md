# A5.8 terrain merge assembly review — NO-GO

Reviewed commit: `faa949d5525fdadb4243c9eb0691e9fcd1bd2d4c`  
Scope: dormant descriptor-owned terrain merge assembly only. No files were
modified by this review other than this audit report.

## What is sound

- `demo_terrain_queue_assemble_merge_spans()` obtains accepted streams through
  `sm64_saturn_render_job_queue_done_job()` and then through
  `demo_terrain_queue_read_done()`. The latter revalidates terminal queue
  state, P2 result metadata, descriptor generation/index/count, output-bank
  lane, and payload-bank aliases.
- The assembler rejects invalid claimant-state metadata, mixed nonzero
  sequences, mixed generations, capacity overflow, invalid identities, and a
  non-master final reader. It does not reconstruct or read the legacy fixed
  `s_terrain_spans_shared` ranges.
- Stream order is descriptor-index order, and the existing master-only stable
  depth-bin builder remains the final ordering owner. The callback is still
  `unused`; no CPU-DUAL callback/activation is added.

## Blocking finding

`demo_terrain_queue_assemble_merge_spans()` silently skips any descriptor for
which `sm64_saturn_render_job_queue_done_job()` returns `NULL`:

```c
const sm64_saturn_render_job_t *const job =
    sm64_saturn_render_job_queue_done_job(&s_render_job_queue, job_index);
if (job == NULL) continue;
```

That accessor returns `NULL` for a queued/claimed/nonterminal `WORLD_LOWER`
descriptor. Consequently the assembler cannot distinguish an unrelated
nonterminal descriptor from a nonterminal terrain-lower descriptor; it can
successfully merge the already-DONE lower streams and omit the remaining
terrain. That is not a fail-closed terminal merge contract and would produce
an incomplete frame if called before the graph's final lower descriptor
retires.

Required repair: enumerate the generation-current graph/queue descriptors
without relying on `done_job()` for discovery, identify each `WORLD_LOWER`,
and return false unless every expected terrain-lower descriptor is terminal
`DONE` (with matching release/result/output metadata) before building any
stream. Add a runtime/host mutation fixture proving one claimed or READY
`WORLD_LOWER` makes assembly fail rather than omitting it.

## Verification run

Both direct host commands used Qt MinGW GCC with
`-std=c11 -Wall -Wextra -Werror` and passed:

1. `tools/saturn/render_job_terrain_route_source_test.c` — PASS.
2. `tools/saturn/render_job_graph_test.c` linked with
   `saturn_render_job_graph.c` and `saturn_render_job_queue.c` — PASS.

`git diff --check faa949d5^ faa949d5` also passed. These fixtures do not
exercise the missing incomplete-lower mutation, so they do not clear this
finding. No target build, CUE/Ymir run, or FPS evidence was performed.

## Verdict

**NO-GO** for live migration and for marking the A5.8 merge assembly
source-complete. The ownership and ordering direction is correct, but the
assembler must prove complete terminal coverage of the terrain-lower phase
before it may publish a merge.
