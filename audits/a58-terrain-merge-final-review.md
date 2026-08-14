# A5.8 terrain merge final re-review — GO (source scope)

Reviewed commits: `faa949d5525fdadb4243c9eb0691e9fcd1bd2d4c`,
`c97f6343933aa82c9fe17f532e364162c9159ea7`, and
`d2011823`.

## Verdict

**GO for the dormant terrain merge source increment.** The two blocking
findings from the prior reviews are repaired. This is not approval for the
live CPU-DUAL cutover, a target build, a new CUE, or an FPS claim.

## Verified contract

- `collect_done_world_lower()` enumerates every immutable descriptor in the
  graph's current generation through the queue's cache-through/P2 accessor.
  It identifies each exact `WORLD_LOWER` independently of its state, then
  requires the same descriptor pointer from `done_job()`. A READY, CLAIMED,
  FAILED, stale-generation, missing, or mismatched lower therefore rejects the
  whole merge rather than being skipped. The executable graph fixture covers
  READY and CLAIMED rejection and exact DONE enumeration.
- Empty output is now distinct from incomplete work. Once at least one exact
  lower exists and every expected lower is DONE, zero result identities are
  accepted as a valid all-culled frame. Before that terminal proof, the same
  empty merge is rejected.
- The dormant renderer assembler repeats the exact published/DONE descriptor
  identity check before reading metadata. Result metadata is uncached/P2,
  generation- and job-index keyed, nonzero-sequence checked, bounded by the
  descriptor, and cross-checked against the output bank's claimant-derived
  owner lane. Payload reads pass through the terminal queue/output bridge and
  select the actual writer lane.
- Streams and result identities are assembled in ascending descriptor index
  and local output index. The final stable depth-bin/order pass remains
  master-owned; no legacy fixed master/slave span or `begin == 0U` lane
  inference is used in the new assembler.
- `demo_terrain_queue_assemble_merge_spans()` remains `unused`. The reviewed
  diff installs no runtime/graph activation and no `cpu_dual_slave_set` call;
  the accepted legacy render path is unchanged pending Mario parity and the
  later atomic cutover.

## Verification

```text
C:\Qt\Tools\mingw1310_64\bin\gcc.exe -std=c11 -Wall -Wextra -Werror \
  -I src/port/saturn/gfx tools/saturn/render_job_graph_test.c \
  src/port/saturn/gfx/saturn_render_job_graph.c \
  src/port/saturn/gfx/saturn_render_job_queue.c
render job graph fixture: PASS

C:\Qt\Tools\mingw1310_64\bin\gcc.exe -std=c11 -Wall -Wextra -Werror \
  tools/saturn/render_job_terrain_route_source_test.c
render job terrain route source fixture: PASS

git diff --check faa949d5^..d2011823
clean
```

The graph fixture is the executable contract proof for nonterminal rejection
and valid empty completion. The renderer route fixture is source-structural;
the static assembler itself remains dormant and is not directly executed by a
host fixture. Target cache behavior and the live integrated path remain open
gates.

## Remaining gates

1. Give Mario's transform/classify/output route the same descriptor-owned
   DONE and merge contract.
2. Perform the one atomic CPU-DUAL ownership cutover without introducing a
   second slave callback owner.
3. Run the serialized Saturn target build, target cache/ordering evidence, and
   desktop Ymir manual test before making runtime or performance claims.
