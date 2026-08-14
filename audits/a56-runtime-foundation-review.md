# A5.6 runtime-foundation review — 2026-08-04

## Verdict

**NO-GO for the A5.6 runtime foundation.** It stays non-active in the current
renderer and the deliberately red live-cutover guard is truthful, but the
future SH-2 polling path reads the queue through a potentially cached alias.
That makes the advertised source-only runtime unsafe to promote without a
cache-alias repair and an SH-2-oriented contract test. The required SDD ledger
also has no record of this behavior commit or its remaining gates.

## Scope and evidence

- Reviewed commit: `0aa0e12244eb4df47eb4bf3d69068908d2f93b88`
  (`feat(saturn): add queue runtime cutover foundation`).
- Files examined: `saturn_render_job_runtime.{c,h}`, render-job queue and
  bridge sources, `slavedriver_dual_worker.c`, `saturn_demo_render.c`,
  `render_job_runtime_test.c`, `test_render_job_live_cutover_source.py`, the
  Saturn make targets, the active plan, evidence report, and SDD progress
  ledger.
- Commands run:
  - Direct Qt MinGW C11/Werror runtime fixture compile and execution: PASS
    (`render job runtime fixture: PASS`).
  - Direct Qt MinGW C11/Werror bridge fixture compile and execution: PASS
    (`render job bridge fixture: PASS`).
  - `test_render_job_bridge_source.py`: PASS.
  - `test_render_job_live_cutover_source.py`: expected RED (the renderer has
    neither the queue bridge include nor queue publish/drain/join, and still
    calls `sm64_saturn_terrain_worker_run` and
    `demo_dispatch_mario_transform`).
  - `git diff --check 0aa0e122^ 0aa0e122`: PASS.

No Saturn target build, CUE, desktop Ymir launch, cache-coherency observation,
or FPS measurement was run or is credited by this review.

## Findings

### Critical

1. **The SH-2 slave can observe a stale queue generation.**
   `sm64_saturn_render_job_runtime_activate()` stores the caller's raw
   `queue` pointer (`src/port/saturn/gfx/saturn_render_job_runtime.c:43`) and
   `sm64_saturn_render_job_runtime_poll_slave()` then reads
   `s_runtime.queue->generation` directly (`:68`). In contrast, every queue
   API first converts the queue to its `CPU_CACHE_THROUGH` alias in
   `saturn_render_job_queue.c` (for example `claim()` and `publish()`). A
   normal renderer-owned queue pointer is therefore allowed to be a P1 cached
   alias; P2 can retain a stale zero generation and return without draining
   newly published work. The host fixture cannot expose this because its
   non-SH alias is identity. Convert and retain the P2/cache-through queue
   address in the runtime (or add/export an appropriate queue accessor and use
   it), then add a structural/SH-oriented test that rejects the raw direct
   generation read.

### Important

1. **The mandatory execution ledger was not updated.** The plan and evidence
   report describe A5.6, but
   `.superpowers/sdd/2026-08-03-saturn-overlapped-render-pipeline/progress.md`
   has no `A5.6`, `0aa0e122`, `runtime foundation`, or `live-cutover` entry.
   This violates the repository operational documentation rule: the ledger
   must record the behavior commit, review state, tests actually run, and each
   remaining gate during the same task transition. Add a bounded A5.6 entry
   before starting integration; keep the static cutover gate explicitly RED
   and target evidence unchecked.

2. **The runtime fixture does not exercise the public notify contract.**
   `tools/saturn/render_job_runtime_test.c:32-37` activates, publishes, then
   calls `poll_slave()` directly. It never calls
   `sm64_saturn_render_job_runtime_notify()`, although the header says the
   master calls it after publication. On host notification is intentionally a
   no-op, so this need not require a worker thread, but the fixture should at
   least call it and retain an explicit source test that notification is not
   reachable from the legacy renderer before the atomic cutover.

### Minor

None.

## Claim verification

- **No live competition yet:** verified for the accepted renderer. Its default
  frame body still uses the legacy terrain and Mario dispatches, while it has
  no include or call to the runtime. The runtime is linked into sourceboot,
  but no renderer call can activate it in this commit.
- **One CPU-DUAL entry when activated:** the runtime itself installs exactly
  one `cpu_dual_slave_set(render_job_slave_entry)` under `__sh__`
  (`saturn_render_job_runtime.c:47-49`). The legacy generic worker still owns
  its separate registration (`slavedriver_dual_worker.c:89-90`), which is why
  the current static cutover guard must remain RED; it does.
- **Source-arm wording:** `source_arm` remains source-only: queue/bridge
  sources contain no CPU-DUAL set/notify token, and the bridge source guard
  passes. It is not an activation approval and the current docs say so.
- **Docs are otherwise materially honest:** the plan/evidence correctly call
  the renderer unbound, state that no target/Ymir/FPS evidence exists for A5.6,
  and preserve the earlier 3–4 FPS A3+A4 candidate only as rollback baseline.

## Recommended resolution

1. Repair the runtime queue access to use the queue's SH-2 cache-through alias
   before reading `generation` or passing it to a slave drain; add a focused
   mutation/source gate for this exact regression.
2. Extend the host fixture to invoke `runtime_notify()` after publication and
   document that host notification is a deliberate no-op while direct polling
   models the Yaul entry.
3. Update the SDD progress ledger with `0aa0e122`, both direct host commands,
   this NO-GO, the red live-cutover guard, and the still-open target/review
   gates.
4. Obtain a fresh independent review after the repair. Do not bind the
   renderer, replace the legacy callback, run a target build, or request a
   manual performance test from this foundation alone.
