# A5.7 render-job graph review — NO-GO

**Reviewed commit:** `6628e2a2 feat(saturn): add render job phase graph`  
**Scope:** `saturn_render_job_graph.{c,h}`, queue additions, fixture/static gate,
sourceboot inclusion, and A5.7 documentation.  
**Verdict:** **NO-GO**

## Blocking findings

1. **[P1] The claimed green host fixture fails, because its assertion contradicts
   the graph policy.** After job 0 is claimed by the slave, job 1 is correctly
   blocked by dependency 0, but independent job 2 remains ready. The fixture
   calls `claim_master()` and expects `false` (`"a consumer must not claim
   before its producer is DONE"`), although the graph correctly claims job 2.
   Direct Qt/MinGW C11 compilation succeeds, but executing the resulting
   fixture exits 1 with that assertion. This makes the evidence report's
   “GREEN introduces … independent work” statement untrue as written. Repair
   the fixture to distinguish the blocked dependent from a deliberately
   claimable independent job, then make its later ordering expectations match.

2. **[P1] Failure propagation is not terminal for a valid reverse-indexed
   dependency chain.** `sm64_saturn_render_job_graph_propagate_failures()`
   makes one increasing-index pass. With `0 -> 1 -> 2` dependencies encoded as
   `{2,4,0}`, failure of job 2 quarantines job 1 but has already passed job 0;
   job 0 remains `READY`, and `all_terminal()` is false. A caller is neither
   required nor documented to repeatedly invoke propagation. Iterate to a
   fixed point (or validate/enforce a topological ordering and test it) so a
   failed producer leaves every reachable still-ready dependent quarantined in
   the same graph operation. Add the reverse-order chain fixture.

3. **[P1] Cyclic dependency masks are accepted and permanently deadlock a
   generation.** `publish()` checks only out-of-range and self edges. A two-job
   cycle `{bit1, bit0, 0}` publishes successfully; neither job can become
   eligible, no failure exists to quarantine it, and the terminal join can
   never succeed. Reject cycles at publication (and add a fixture), or adopt a
   documented cycle-to-terminal-quarantine policy. The current public API has
   no way for the renderer to recover this state safely.

## Confirmed non-blocking points

- The graph claim uses the exact queue index and `claim_index()` retains the
  queue's lock/revalidate transition; it does not invent a claimant or permit
  a duplicate claim.
- `predecessors_done()` uses `queue_job()`, which returns a descriptor only for
  `DONE`, so a consumer cannot be claimed while a producer is merely READY or
  claimed. The cache-through access pattern is present for graph and queue
  observation, and the graph metadata stays outside the fixed descriptor ABI.
- Terrain merge validation rejects non-`DONE` jobs, non-world-lower jobs,
  out-of-capacity outputs, duplicate identities, and non-monotonic
  `(job_index, output_index)` order. It is an identity/order validator only;
  live payload-bank consumers remain intentionally unbound.
- The renderer does not include the graph header and the commit adds no
  `cpu_dual_slave_set`/notify call. The existing source guard passed, so this
  foundation has not prematurely installed a second CPU-DUAL owner or altered
  the accepted A3+A4 route.
- The plan/evidence correctly say no target build, CUE, Ymir, or FPS evidence
  occurred. They must be amended to retain the host fixture as **RED** until
  the two terminal-state repairs and their tests are green.

## Verification actually run

1. `python tools/saturn/test_render_job_graph_source.py` — **PASS** (2 tests).
2. Qt MinGW 13.1 direct C11 build (`-std=c11 -Wall -Wextra -Werror`) of
   `render_job_graph_test.c` plus graph/queue sources — **compiled**, but
   executable **FAILED** with `a consumer must not claim before its producer is
   DONE`.
3. Independent direct C11 edge fixture — **PASS as a reproducer**:
   reverse chain printed `states 1 6 5 terminal=0`, proving the one-pass
   propagation leaves job 0 ready; cycle `publish=1`, proving publication
   accepts a cycle.

No Saturn target build or runtime test was run; neither is authorized by this
unbound foundation.
