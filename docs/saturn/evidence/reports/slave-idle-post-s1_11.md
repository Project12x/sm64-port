# Slave idle after S1.11, and why the next chunk is not the obvious one

**Date:** 2026-08-23 · **Build:** shipped `id-2ff8718cc26fed8d` ·
`capture_idle_attribution.py`, warm-up tick 30.

## The measurement

| | pre-S1.11 (S0.1) | post-S1.11 |
|---|---:|---:|
| master idle | 0.00% | **0.00%** |
| slave idle | 74.04% | **50.75%** (5.687 VB/frame) |

Joint contingency: `work | idle:notification-wait` 50.75%, `work | work`
49.25%. Zero master VBlank-spin excursions traced.

**The master is still the limiter and the slave still has 5.687 VBlanks per
frame to sell.** S1.11 consumed roughly 23 points of slave idle, which is
consistent with it having taken the actor pipeline; it did not exhaust the
peer.

A first attempt at this measurement used `--windows 1 --window-steps 20000`
and reported *both* CPUs at 0.00% idle, i.e. "the slave is now saturated, stop
offloading". That was a sample-size artifact — 0.1148 frames traced at the
default settings already contradicts it. Re-running before acting is the only
reason the wrong conclusion was avoided.

## Where the master's time actually sits

The pre-notification window is only ~2.90 VB/frame of the master's 7.345 VB
`construction`. `spatial_admit` is 91% of that window, so ~2.6 VB/frame — the
largest single *pre-notify* item, but **not** the largest master block.

The master is not stalled while the slave runs —
`sm64_saturn_render_lifecycle_poll` returns `PENDING` and its caller advances
simulation, which is why master idle reads 0.00%.

**Unverified inference, flagged as such:** the remaining ~4.4 VB of
`construction` outside the pre-notification window is *assumed* here to be
post-notify `drain_master` graph work. That has **not** been measured — it is
arithmetic on two phase counters whose exact boundaries are not documented, and
`construction` may also cover finalize-adjacent or simulation-adjacent time.
Anyone acting on the concurrent-drain plan below should measure the master's
post-notify composition first; a `SATURN_DIAGNOSTIC_MODE=2` node around
`drain_master` would settle it directly. Do not treat the 4.4 VB as an
established quantity.

## The structural finding

`saturn_render_lifecycle.c:65` makes the master wait for `slave_retired()`
**before** `drain_master`. The graph pass is therefore *sequential*: the slave
claims whatever is ready in one pass and retires, then the master drains the
rest alone. The two CPUs never work-steal from the graph concurrently, even
though `graph_claim` supports exactly that with atomic per-index claims and a
cache-through graph.

Two compounding effects:

1. `graph_claim` returns false when nothing is *momentarily* claimable —
   including when a job's predecessors are still executing. `poll_slave`'s
   `while (claim_slave(...))` exits on that first failure and the slave
   retires. Work that becomes ready afterwards all falls to the master.
2. Because the master only drains after retirement, that later work is done by
   one CPU while the other is idle.

## Why the obvious fix is not a one-liner

Letting the master drain concurrently with the slave looks like a small
reordering in `sm64_saturn_render_lifecycle_poll`. It is not safe as-is.

`demo_prepare_position_owners()` (`saturn_demo_render.c:1112`) is called from
*inside* the terrain lower callback (`:3046`, `:3113`) — a callback both CPUs
can claim — and it begins by `memset`-ing two **shared file-scope arrays**,
`s_position_use_mask` and `s_position_owner`, then rebuilds them from
`s_render_work_order`. Two CPUs running lower jobs concurrently would race on
both, and on cached HWRAM with no coherency the corruption would be silent.

This is very likely the real reason the drain is sequential — a stronger reason
than the payload-recycling note attached to `slave_retired()`. Making the drain
concurrent therefore requires making that owner state per-lane first, exactly
as S1.11 had to do for the gouraud accumulator and the meshlet counters.

## Recommended order

1. **Per-lane the terrain owner state** (`s_position_use_mask`,
   `s_position_owner`), mirroring the S1.11 accumulator pattern. Correctness
   work with no expected FPS change — verify with frame identity.
2. **Measure the master's post-notify composition** before building anything —
   see the flagged inference above. If graph draining really is the bulk of it,
   letting the master drain concurrently with the slave is where the payoff is,
   against a peer with 5.687 VB/frame spare. Note the queue imposes **no
   claimant restriction** (`sm64_saturn_render_job_queue_claim_index` accepts
   either CPU for any READY job), so nothing in the queue design blocks this —
   only the lifecycle ordering and the shared owner state do.
3. `spatial_admit` offload stays available as a fallback, but it is the smaller
   prize (~2.6 VB) *and* it is the terrain admission a VDP2 ground plane would
   shrink — so it should not be built before the VDP2 decision is made.
