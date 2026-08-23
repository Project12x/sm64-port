# T3 (bank depth carry) — implemented, measured, reverted

**Date:** 2026-08-23 · **Verdict: real redundancy, immaterial saving. Do not
re-attempt without new evidence.**

## What T3 proposed

`actor_meshlet_core` walks every meshlet twice — once to admit and bin, once to
emit. Before T2.6 the emission walk recomputed each meshlet's depth bounds from
scratch; T2.6 added a *carry* so the emission walk reuses what the admission
walk already computed. Mario's entry point passes a carry
(`s_mario_depth_carry`). The **bank** entry point passed `NULL`, with this
justification at `saturn_actor_meshlets.c:88`:

> a shared static would be a cross-CPU race, and the bank path is not on the
> measured hot path

The second clause had gone stale: S0.1 measured
`actor_meshlet_live_depth_bounds` at **3.359% of a fully saturated master**, and
the bank path *is* the actor closure's hot path. The first clause was a real
constraint, answered by giving each claimant lane its own cache-line-aligned
carry (lanes are disjoint per CPU by construction).

## What was measured

Built as `id-a87ada8d9e5a4093` against `id-2ff8718cc26fed8d`:

| | before | after |
|---|---:|---:|
| FPS mean | 5.7237 | 5.7237 |
| FPS median | 5.4545 | 5.4545 |
| 1% low | 5.0000 | 5.0000 |

Not merely equal in summary: **the two runs' 29 interval records are
byte-identical**, down to every per-phase sub-counter (`construction`,
`master_finalization`, `slave_work_overlap_window`, `dropped_vblank_credit_delta`)
on every single frame. That rules out the usual explanation for a flat cadence
result — that a real saving fell below the 60/k rung quantisation — because the
phase counters would still have drifted.

## Why it was immaterial, and where the 3.359% actually lives

Decoded from the shipped bundle (`bob-area1-actors-v3.s64f`, meshlet_count is a
big-endian `u16` at bank byte 14, per `saturn_actor_bank.c:566`):

| model_id | 219 | 143 | 205 | 188 | 150 | 165 | 149 | 166 | … | 132 |
|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|
| meshlets | 10 | 9 | 7 | 7 | 7 | 6 | 6 | 6 | … | 1 |

**Maximum meshlet count across all 18 banks is 10.** So:

- The carry was genuinely *taken* — the 64-entry capacity never fell back, and
  `workspace->lane` is populated at bind (`saturn_actor_meshlets.c:290`), so the
  `lane < LANE_COUNT` guard passed. This was verified rather than assumed.
- The carry removes recomputation only for **admitted** meshlets in the
  **emission** walk. The **admission** walk still computes bounds for every
  meshlet of every instance, and no carry can remove it.
- With ≤10 meshlets per instance, the removable half is a small fraction of a
  small number. The 3.359% is dominated by the admission walk plus Mario's
  path — and Mario already had a carry.

A caution worth carrying forward: `draw_records_per_instance` is **not**
meshlet count. `MODEL_BLACK_BOBOMB` has 68 draw records, which briefly looked
like it would overflow the 64-entry carry; its actual meshlet count is 7.

## Why it was reverted rather than kept

The change is correct (meshlet golden hashes unchanged, so it is an exact
equivalence) and it does remove real redundant work. It was still reverted:

- It costs **1,056 bytes of HWRAM** (2 lanes × 528). HWRAM is the binding
  constraint for the full-port goal and is watched like a build error; current
  slack is 13,940 bytes above the 0x1F00 floor.
- It buys nothing observable in any measurement available today.

Spending a scarce, actively-tracked resource on an unmeasurable gain is the
pattern this project's evidence discipline exists to prevent. **T3 should drop
off the ranked list.** If a later content increase (S7) multiplies instance
counts substantially, the arithmetic changes linearly with instance count and
it can be reconsidered — at which point this document says exactly what to
rebuild and what to compare.
