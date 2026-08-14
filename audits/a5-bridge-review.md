# A5.5 descriptor-to-result bridge review

**Commit reviewed:** `962dfde304f9d87c54edf9bf09c08430986747b1`  
**Verdict:** **NO-GO**

## Scope and method

Reviewed the A5.5 diff without editing or switching the worktree, then traced
the bridge through the existing queue/output-bank contracts and the active
fixed-split dual-worker path.  The review specifically checked descriptor and
generation binding, early reads, P2/cache selection, callback ownership, and
whether the commit honestly remains source-only.

## Blocking finding

### P1 — A5.5 introduces a second compiled `cpu_dual_slave_set` owner while the fixed worker remains live

`sm64_saturn_render_job_queue_slave_attach()` in
`src/port/saturn/gfx/saturn_render_job_queue.c` calls
`cpu_dual_slave_set(render_job_queue_slave_entry)`.  The current renderer
still links and actively initializes `sm64_saturn_dual_worker_init()`, which
calls `cpu_dual_slave_set(dual_slave_entry)` in
`src/port/saturn/gpl/slavedriver_dual_worker.c`.  The new API therefore adds a
second, compiled callback registration path before the old owner is removed.

This violates the Task 5 design correction: no dormant or compile-disabled
second `cpu_dual_slave_set` path is permitted, and the queue may become the
accepted callback only when the old fixed-split callback is replaced.  Calling
the new attach in the current build would overwrite the legacy entry; the next
legacy `sm64_saturn_dual_worker_run()` notification would run the queue entry
instead of `dual_slave_entry`, leaving its `done` control word unset and
potentially stalling its unbounded positive-retirement wait.

**Required correction:** keep callback installation out of the source-only
bridge, or land the registration switch atomically with the live renderer
conversion that removes every legacy `sm64_saturn_dual_worker_run()` use.  The
bridge may expose/test an attachment *state contract* in host code, but the
target `cpu_dual_slave_set` call must have exactly one accepted owner in the
linked renderer.

## What is sound in the source-only bridge

- `begin_output()` selects the output bank from immutable descriptor kind and
  delegates claimant validation to `sm64_saturn_render_output_bank_publish()`;
  the caller cannot forge a master/slave lane.
- `read_output()` obtains the descriptor through `done_job()` before consulting
  its owner metadata, so reads of `READY` or `CLAIMED_*` jobs fail closed.
- The output-bank contract revalidates queue generation, descriptor generation,
  kind, actual claimed state, and exact job index through uncached/P2 access.
  A peer reader therefore selects cache-through only when the descriptor-owned
  writer lane differs from its own lane.
- The commit does not modify `saturn_demo_render.c`; it does not publish jobs,
  invoke the bridge, or claim a target/visual/FPS result.  Apart from the
  callback-registration hazard above, its stated source-only status is honest.

## Tests actually run

Direct host compile and execution (GCC):

```text
gcc -std=c11 -Wall -Wextra -Werror -I <worktree>/src/port/saturn/gfx \
  tools/saturn/render_job_bridge_test.c \
  src/port/saturn/gfx/saturn_render_job_bridge.c \
  src/port/saturn/gfx/saturn_render_output_bank.c \
  src/port/saturn/gfx/saturn_render_job_queue.c \
  -o %TEMP%/a5-bridge-review.exe
render job bridge fixture: PASS
```

The fixture covers master steal, terminal-read rejection, peer cache routing,
slave actor ownership, wrong descriptor rejection, and a second logical queue
attach.  It cannot exercise Yaul's single global slave callback or SH-2 cache
coherency.

## Open target gates

- Compile/link the Saturn sourceboot target after a single-callback ownership
  design is in place.
- Prove on target that only the chosen polling entry receives notifications and
  legacy worker retirement cannot be stranded.
- Complete the live renderer producer/consumer conversion, then exercise
  cross-SH-2 cache visibility, exact-once completion, terminal fence, CUE/Ymir
  visual behavior, and performance counters.  None of these are satisfied by
  the host fixture.
