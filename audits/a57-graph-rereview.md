# A5.7 render-job graph re-review — GO

**Reviewed range:** `6628e2a2..04a44661`  
**Scope:** dependency graph foundation, queue transitions it relies on, host
fixture/static guard, and A5.7 plan/evidence claims.  
**Verdict:** **GO for the source-only A5.7 foundation.** It is not a live
CPU-DUAL or renderer-cutover approval.

## Verified contract

- **Independent-ready work remains eligible.** The fixture claims producer 0
  on the slave and independently claims/finishes job 2 on the master before
  producer 0 retires (`tools/saturn/render_job_graph_test.c:36-50`). A
  dependent is still rejected until `queue_job()` can observe its predecessor
  as `DONE` (`saturn_render_job_graph.c:36-46, 125-135`).
- **Self and cyclic masks fail closed before queue publication.** Publish
  rejects self bits (`saturn_render_job_graph.c:101-105`) and the bounded
  topological retirement check rejects every non-self cycle, including longer
  cycles, because no node in a closed cycle can retire
  (`saturn_render_job_graph.c:63-81,106`). The fixture exercises a two-node
  cycle (`render_job_graph_test.c:90-98`); the source check establishes the
  general bounded algorithm for the eight-node ABI.
- **Failure quarantine reaches a fixed point.** Repeated passes quarantine
  each still-READY reverse dependent until no transition is possible
  (`saturn_render_job_graph.c:155-171`). The fixture's 0 -> 1 -> 2 chain is
  deliberately in the direction that defeated the old single forward scan,
  and now reaches `all_terminal()` (`render_job_graph_test.c:100-119`).
  Claimed work is not rewritten: quarantine calls the queue transition that
  accepts only `READY` (`saturn_render_job_queue.c:249-255`).
- **P2/cache and exact identity boundaries are retained.** The graph itself
  is accessed through the SH-2 cache-through alias (`saturn_render_job_graph.c:9-23`),
  while all queue state/job inspection reaches the queue's equivalent P2
  accessor. Terrain merge accepts only DONE `WORLD_LOWER` descriptors,
  checks the local output-capacity bound, and rejects duplicate or
  non-monotonic `(job_index, output_index)` identities
  (`saturn_render_job_graph.c:174-197`). It does not infer a fixed worker
  range or bind physical payload storage.
- **No premature live cutover.** `saturn_demo_render.c` does not include the
  graph header, and this range adds no `cpu_dual_slave_set` or notify call.
  The intentionally-red live-cutover static contract still describes the
  unimplemented renderer migration. This matches the plan/evidence's
  source-only statements; neither claims target execution or an FPS result.

## Verification actually run

1. Direct MinGW C11 fixture:
   `gcc -std=c11 -Wall -Wextra -Werror -I src/port/saturn/gfx tools/saturn/render_job_graph_test.c src/port/saturn/gfx/saturn_render_job_graph.c src/port/saturn/gfx/saturn_render_job_queue.c -o .tmp-task5/render-job-graph-test.exe`, then executable — **PASS** (`render job graph fixture: PASS`).
2. `git diff --check 6628e2a2^..04a44661` — **PASS**.
3. Static source guard was inspected directly: its required graph tokens are
   present and `saturn_demo_render.c` has no graph include. The Python
   `unittest` invocation could not run in this agent environment because the
   only resolved `python.exe` was the inaccessible WindowsApps shim; this is
   an environment limitation, not recorded as a passed command.

## Findings

### Critical

None.

### Important

None.

### Minor

- The focused fixture proves a two-node cycle and the reverse chain but does
  not contain explicit self-edge or three-plus-node-cycle cases. The bounded
  implementation is correct for both; adding those two small cases later
  would make regression coverage mirror the complete public contract.

## Documentation truthfulness

`docs/saturn/evidence/reports/overlapped-render-pipeline-2026-08-03.md:927-955`
accurately distinguishes the repaired host foundation from a live cutover and
states that no target build, CUE, Ymir, counter, or FPS claim occurred. The
active architecture ledger makes the same source-only boundary explicit.

## Next action

Use this graph only as the prerequisite for the later atomic descriptor-owned
payload/callback migration. Preserve the existing fixed worker as the sole
CPU-DUAL owner until that migration is complete and separately reviewed.
