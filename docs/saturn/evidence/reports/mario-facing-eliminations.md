# Mario faces the wrong way: what it is not, and the one thing left

**Date:** 2026-09-04 - **Status:** cause not proven; search space reduced to one
mechanism with a stated test. **Not fixed.**

## Symptom, as reported

Mario faces the wrong direction moving forward or backward, but the correct
direction moving left or right. Separately, and at all times: "I can see the
front and back of Mario at the same time." The owner confirms the facing bug
**predates every change made this sprint**, including the sine table.

Screenshot evidence, headless route capture:

| frame | faceAngle | should show | actually shows |
|---|---|---|---|
| 3000 | 135 deg | back, angled right | **front**: eye, mustache, both buttons |
| 4200 | 90 deg, moving +X | screen-right | screen-right (correct) |

Rendered facing behaves as **phi = 180 - theta**: a mirror through the view
axis, not a rotation. Maximally wrong head-on and away, invisible in profile.
That signature is why no constant yaw offset can fix it, and it is exactly what
the +0x8000 experiment (tried 2026-09-03, reverted) failed to change.

## Ruled out, each by measurement rather than argument

| candidate | how it was eliminated |
|---|---|
| yaw rotation formula | term-for-term identical at all four sites: `demo_transform_mario_vertex`, `actor_depth_fast`, castleviewer `mario_world_point`, marioturntable |
| sine table | `gSaturnSineTableQ16` matches true sin/cos to 1e-5 across all 5120 entries |
| a half-turn convention | applied +0x8000 to the vertex rotation at both sites; owner confirmed still wrong |
| simulation state | faceAngle agrees with heading(vel) to 1 deg once negative `forwardVel` (backing up) is folded in |
| render vs sim angle | `gfx.angle[1]` equals `faceAngle[1]` exactly, 0.0 deg difference |
| mesh authoring | mesh measures +Z-facing in the bind pose and in all 30 idle and 77 walking frames |
| back-face cull sign | flipping `cross <= 0` changed nothing observable; a cull cannot rotate a model |
| camera basis | `forward = focus - position`, `right = (-forward.z, 0, forward.x)`; no negation |
| projection | `sm64_saturn_ir_transform_one` is a clean basis projection, no sign inversion |
| mesh winding | 459 outward / 185 inward, but the inward set is scattered 12-51% across **every** material and never dominates one - the signature of a centroid heuristic on a concave body, **not** a bake flip. Explicitly inconclusive; do not act on it |

## Correction to an earlier claim

An earlier session message stated that "Mario has no intra-model depth sort"
and that actors are "drawn in mesh order" because no sort exists. **The first
half is wrong.** `actor_meshlet_core()` in
`src/port/saturn/gfx/saturn_actor_meshlets.c:781-876` runs a full counting sort
over 64 depth bins for **both** the opaque and translucent passes, and its
prefix loop walks `bin = 63 -> 0` (line 793), so output really is **far-first**,
which is correct painter order. The structure is right.

## What is actually wrong with it: the bin is far too coarse to matter

- `SM64_SATURN_ACTOR_DEPTH_BIN_SHIFT` is 7 and the depth is taken in whole
  world units, so **one bin spans 128 world units** (`actor_depth_bin`,
  lines 116-123).
- The key is each **meshlet's furthest vertex** (line 853), so primitives
  inside a meshlet are never ordered against each other at all.
- `opaque_bins[]` / `translucent_bins[]` are **locals** of `actor_meshlet_core`
  (lines 638-639), which processes **one actor per call**. The 64-bin,
  8192-unit table is therefore spent on a single object.

Mario's model bounding box is x=111, y=150, z=72 units. Reproducing
`actor_depth_bin` over his real 31 meshlets across 24 yaws x 713 camera
distances (300..6000), 17,112 samples:

| result | samples | share |
|---|---:|---:|
| 1 distinct painter bin | 8,531 | 49.9% |
| 2 distinct painter bins | 8,581 | 50.1% |

Maximum meshlet depth spread over every sample: **84.9 units**, against a
128-unit bin.

So his 644 primitives collapse into **one** painter bucket half the time, and
two the rest of the time - and a 2-bin split only happens when the body
straddles an absolute 128-unit boundary, so *which* meshlets land in the near
bin is set by the camera's distance, not by which ones face the camera. It is
never a front/back separation. Inside a bucket the order is meshlet order then
mesh order, which is fixed at bake time and **independent of the camera**.

## The remaining hypothesis, and how to kill it

Two reported symptoms are probably one bug, not two:

1. Back faces are not being removed (owner sees front and back at once).
2. Surviving far faces are painted **over** near ones, because within Mario's
   single bin there is no depth order to stop them.

A body whose far side wins the paint reads as a body mirrored through the view
axis - **phi = 180 - theta** - maximal head-on and away, and invisible in
profile where the silhouette is near-symmetric. That is the measured signature,
and it needs no rotation error to produce it. It also explains why flipping the
cull sign "did nothing": with no effective depth order, trading which set
survives still leaves far geometry painting last.

This is a **hypothesis with a signature**, not a proven cause.

**The test**, one build, two independent looks:

- Make the actor bins fine enough to order a single body - bin on depth
  **relative to the actor's own nearest meshlet** rather than absolute view
  depth, which is safe precisely because the bin table is per-actor. Then look:
  is he still inside-out?
- Independently confirm whether any back-face rejection is happening at all, by
  counting rejects rather than by eye. Then look: are both sides still visible?

Answering those two separately is the point. Judging one flip on "does he face
the right way" is what produced the earlier wrong conclusion.

## Method note

The winding measurement is kept above precisely because it came out
**inconclusive by its own stated criterion**, and was nearly reported as a
cause anyway. The rule that saved it: decide the discriminating threshold
before looking at the number. The claim corrected in this document was made in
the opposite order - asserted from a reading of the emit path, without opening
`actor_meshlet_core`, which refutes it in one screen.
## RESOLVED 2026-09-10

Owner-confirmed live on candidate `f6a0836b34ddd407`: Mario faces the
direction he moves in all four directions.

**The cause was not in the elimination space this document explores.** Every
hypothesis here -- yaw convention, sine table, cull sign, painter order --
was argued against `demo_classify_mario_range`. That function is dead code.
Its only caller, `demo_dispatch_mario_transform`, is never invoked, and
`tools/saturn/dual_actor_worker_test.c:179` asserts as much. The live
classifier is `demo_actor_queue_classify`, reached through the render-job
callback table, and it is marked `__attribute__((unused))` because it is only
referenced via a function pointer. The dead function carries no such marker.

So this document's line "flipping the Mario back-face cull changed nothing"
was a true observation with a false implication: the flip was real, the
recompile was real, and the changed code never executed. The same trap caught
a 2026-09-09 retry.

**The cull sign was the answer**, at the live site: reject `cross >= 0`. The
derivation this document could not settle needed one fact it never stated --
`projected` is Y-DOWN (`saturn_ir_transform.c:53`). With that, the frontend's
Y-up `(v0-v1)x(v2-v1)` and this site's Y-down `a x b` are the same quantity
and take the same predicate.

**Method note, since this document is largely a record of method.** The two
recommendations in "The test" above were both eventually carried out. The
first -- finer intra-actor bins -- was implemented and correctly failed:
front and back faces of a body part share a meshlet, so no bin granularity
can separate them. The second -- "confirm whether any back-face rejection is
happening at all, by counting rejects rather than by eye" -- was the right
instinct and went undone for six weeks. When finally counted, the cull was
live and rejecting 60.8%.

What actually broke the deadlock was neither: it was asking whether the code
being edited runs at all. Reachability was assumed by everyone, including a
session that grepped for callers of the dead function, found three, and never
asked whether those were themselves reachable.
