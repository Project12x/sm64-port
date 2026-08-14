# A5.8.1 graph-runtime re-review — GO (source scope)

Reviewed commits: `92ba442e646f8daa9b505dcbaf35c2ec03a062ba` and
`ec55157254c89f9eba3e7379d7969863e7f5d6ea`.

## Verdict

**GO for the A5.8.1 source-only prerequisite.** The prior P1 descriptor-read
defect is repaired. This is not a target, renderer-cutover, or performance
approval.

## Verified contract

- Both graph runtime drains claim only through `sm64_saturn_render_job_graph_claim_*`.
  No raw queue READY scan is used by the graph runtime.
- Both drains obtain a callback descriptor exclusively through
  `sm64_saturn_render_job_queue_claimed_job()`. That accessor first selects
  the queue's cache-through/P2 alias on SH-2, then checks generation, bounds,
  and the exact `CLAIMED_MASTER` or `CLAIMED_SLAVE` state before returning the
  immutable descriptor. The former `s_runtime.queue->jobs[index]` form is
  absent from all runtime dispatch paths; the source mutation guard forbids
  restoring it.
- Completion and failure use the queue terminal API, whose first operation
  also canonicalizes the queue through the cache-through/P2 helper and then
  revalidates generation plus the exact claimant state under the release
  claim. A null/revalidation-failed descriptor therefore fails the claim
  closed and triggers graph failure propagation instead of dispatching an
  untrusted descriptor.
- Graph admission preserves phase invariants: a job is claimable only when
  all named predecessors are `DONE`; failed or quarantined predecessors are
  propagated only to still-READY descendants; independent READY work remains
  admissible. The focused runtime test publishes `WORLD_LOWER` before its
  `WORLD_ADMIT` producer and passes only when the producer executes first.
- Raw `sm64_saturn_render_job_runtime_activate()` returns false and cannot
  install or wake a CPU-DUAL callback. The graph activation API contains the
  future single polling binding, but `rg` finds no production caller of it:
  only the host fixture calls it. `saturn_demo_render.c` is untouched by both
  reviewed commits, so there is no accepted live CPU-DUAL cutover here.
- The plan/evidence/CHANGELOG accurately label this as source-only and retain
  the existing A3+A4 desktop observation as rollback baseline; they make no
  CUE, Ymir, target, or FPS claim for A5.8.1.

## Evidence run

```text
C:\Qt\Tools\mingw1310_64\bin\gcc.exe -std=c11 -Wall -Wextra -Werror \
  -I src/port/saturn/gfx tools/saturn/render_job_runtime_test.c \
  src/port/saturn/gfx/saturn_render_job_runtime.c \
  src/port/saturn/gfx/saturn_render_job_graph.c \
  src/port/saturn/gfx/saturn_render_job_queue.c
render job runtime fixture: PASS

C:\Qt\Tools\mingw1310_64\bin\gcc.exe -std=c11 -Wall -Wextra -Werror \
  -I src/port/saturn/gfx tools/saturn/render_job_graph_test.c \
  src/port/saturn/gfx/saturn_render_job_graph.c \
  src/port/saturn/gfx/saturn_render_job_queue.c
render job graph fixture: PASS

git diff --check 92ba442e^ ec551572
clean
```

The Python static mutation fixture could not be launched in this session:
the available WindowsApps `python.exe` reports that the file cannot be
accessed by the system. Its assertions were manually inspected: the runtime
contains the cache-through generation accessor and `claimed_job()` calls and
does not contain either forbidden raw cached form. This does not replace a
future target P2/multicore gate.

## Remaining gates

- Bind the graph queue atomically into the live renderer only after replacing
  the existing CPU-DUAL worker ownership; maintain one polling callback owner.
- Route production terrain/actor payload banks through descriptor-owned lanes,
  then run target cache/ordering and desktop Ymir validation.
