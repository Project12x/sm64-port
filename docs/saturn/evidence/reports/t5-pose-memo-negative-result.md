# T5 constant-pose memo — implemented, measured, reverted

**Date:** 2026-08-23 · **Verdict: the memo works and buys nothing, because the
work it saves is no longer on the critical CPU.**

## What was built

17 of the 18 shipped banks are `joint_count == 1` with a single animation
(decoded from the bundle: only `MODEL_BLACK_BOBOMB` has 13 joints / 2
animations), so their pose is a function of (bank, animation frame, instance
scale) and nothing else. The actor phase walks instances back to back through
the same lane workspace, so when consecutive instances share that key the
previous evaluation's vertices are still sitting there.

A per-lane memo was added to `sm64_saturn_actor_pose_evaluate_staged`, keyed on
the workspace vertex pointer (which identifies the claimant lane without the
evaluator being told which one), with the bank identity, vertex count, version,
animation, frame and all three scale axes in the key. Cache-line aligned
entries, because lanes belong to different CPUs.

It came with a contract test asserting that a memoised pose is
indistinguishable from a recomputed one, that a scale change is never served
from the cache (S1.1 folds scale into the vertices, so forgetting it would draw
every actor at whichever scale ran first), and that an animation change misses.
The test is self-validating: it fails if the hit path is never taken, so the
assertions cannot pass vacuously.

## What it measured

**Hit rate: 33.44% — 311 hits against 619 misses over 930 evaluations**, read
off the target after a full boot handoff. The memo is genuinely working.

Cadence, against `id-8a01761db772d0d9`:

| | before | with memo |
|---|---:|---:|
| FPS mean | 6.1922 | 6.2143 (+0.36%) |
| FPS median | 6.0000 | 6.0000 |
| 1% low | 5.4545 | **5.0000** |

Mean moved by noise; the 1% low **regressed**.

## Why a working 33% cache is worthless here

**Pose evaluation no longer runs on the master.** S1.11 moved the whole
generic-actor phase to the slave, and the slave is measured **50.75% idle
(5.687 VB/frame)**. Saving cycles on a CPU with spare cycles cannot move a
frame time that the master determines.

T5's original 4.977% attribution came from S0.1, taken **before** the offload,
when the pose ran on the master. The figure was correct then and is
irrelevant now.

**The general lesson, which applies to every remaining item on the S0.1 ranked
list: an attribution measured before a phase moved to the slave says nothing
about what that phase is worth today.** Anything now executing inside the actor
phase is effectively free until the slave saturates. Re-measure before
optimising anything in that region; the ranked list is stale by construction for
exactly the work that was most worth moving.

The small 1%-low regression is consistent with the memo's own cost — a key
comparison on every evaluation, paid on all 930 including the 619 misses — being
just enough to tip a worst-case frame across a cadence rung.

## Reverted

Correct, tested, and worthless at present. Kept out of the tree rather than
carried as dead weight. **Do not re-attempt while the actor phase runs on the
slave with idle to spare.** It becomes interesting only if the slave becomes the
limiter, at which point this document says exactly what to rebuild: the key, the
33% hit rate to expect, and the contract test that must accompany it.
