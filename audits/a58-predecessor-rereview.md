# A5.8 terrain predecessor re-review — 2026-08-04

## Scope and verdict

**Verdict: GO (dormant source scope only).** Reviewed `94b8141e` and its
predecessor-proof repair `fdc38fa8` in the exact
`sh2/native-math-purge` worktree. This review does not authorize CPU-DUAL
activation, a Saturn target build, CUE/Ymir testing, or an FPS claim.

## Findings

### Critical

None.

### Important

None.

### Minor

None.

## Verified contract

- `sm64_saturn_render_job_graph_world_lower_admit_done()` derives the lower
  descriptor through an exact current `CLAIMED_MASTER`/`CLAIMED_SLAVE` lookup,
  rejects any non-`WORLD_LOWER` callback, and accepts only a one-bit graph
  dependency. It resolves that bit only through `render_job_queue_job()`,
  whose contract requires the predecessor to be terminal `DONE`, then requires
  `WORLD_ADMIT` plus its matching callback ID.
- `demo_terrain_queue_world_lower()` independently obtains its exact queue
  index, calls that graph proof before any compact/classify read, resolves the
  completed admit descriptor, and rejects absent/mismatched P2 admit metadata.
  The metadata reader checks generation, descriptor index, sequence, claimant
  state shape, writer lane, and output-bank owner lane.
- The graph fixture deliberately forces both malformed states: a lower claim
  before admit completion and a lower dependency on a completed actor job.
  Both fail closed. The normal producer/lower chain still passes.
- The queue route remains dormant: both terrain callbacks are marked unused and
  no new CPU-DUAL registration or queue activation was introduced. The only
  remaining `begin == 0U` lane selections are outside the queue callbacks;
  the queue lower route uses descriptor-derived output ownership and does not
  reference `s_slave_begin`.

## Evidence run

- `git diff --check 94b8141e^..fdc38fa8` and `git show --check fdc38fa8`: PASS.
- Direct host MinGW C11 `-std=c11 -Wall -Wextra -Werror` graph fixture: PASS
  (`render job graph fixture: PASS`).
- Direct host C11/Werror terrain route source fixture: PASS
  (`render job terrain route source fixture: PASS`).
- Direct host C11/Werror bridge fixture: PASS (`render job bridge fixture: PASS`).

## Documentation check

`ARCHITECTURE.md`, `STATE.md`, the active overlapped-render-pipeline plan, and
the evidence report accurately retain the dormant status and open gates:
terrain merge-span assembly, Mario parity, one atomic CPU-DUAL cutover, then
target/cache/visual evidence.

## Remaining gates

1. Assemble terminal terrain merge spans from descriptor metadata.
2. Give Mario the equivalent descriptor-owned producer/reader contract.
3. Conduct fresh integration review, then make the sole CPU-DUAL callback
   transition atomically and validate on target/Ymir.
