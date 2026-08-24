# Baked visibility — scoped, and Stage A turned out to be already shipped

**Date:** 2026-08-24 · Corrected the same day it was written. The first version
of this document scoped a spatial hierarchy as the main prize on the strength of
a `nodes_tested = 1` counter. **That counter was stale**, and the work was
already done. Corrected below from the `verify-admission-hierarchy` gate run on
the current tree.

## Correction, and how the error happened

The T2.10 evidence (`sprint2-t2_10-spatial-admit-fixes.json`,
`/summary/spatial_admit/counts`) reports `nodes_tested: 1`, `nodes_admitted: 1`
against `clusters_tested: 867`, which reads as "the node hierarchy collapses to
a single root, so admission linearly scans every cluster". It was written on
**2026-08-16** — the same day as commit `0d3450f4`, *"feat(scene): bake a
median-split admission hierarchy **instead of one node**"*, and its companion
`031a575e`, *"feat(admission): descend a real hierarchy and inherit the frustum
result"*. The counter predates the fix it appears to indict.

The generated header states the true position plainly:
`SM64_SATURN_BOB_ADMISSION_NODE_COUNT` is **255**, and node 0 carries
`child_first = 1, child_count = 2` with real bounds. **The lesson is the same
one that produced the decimation seam bug earlier the same day: a number that
supports a convenient conclusion needs its provenance checked before it is built
on.** Both errors were "an old measurement, still true-looking, quietly wrong".

## Measured current state

From `mingw32-make -f Makefile.saturn.mk verify-admission-hierarchy` (the T2.12
equivalence oracle, which already implements the byte-identical admitted-set
check this document was about to propose building):

```
bob-generated   nodes 255 | flat 802.2 tests/pose -> tree 147.9 + 106.0 (31.6%)
clusters_tested reference 38,668,200 -> shipped 12,953,180   (-66.5%)
48,200 poses, 13,835,970 cluster admissions, 0 divergences
admitted-set digest a99b76b99477a4ff        FIXTURE: PASS
```

**Stage A is shipped, and it is already at its optimum.** The gate sweeps leaf
size, and the shipped configuration is the minimum of that sweep:

| leaf size | 1 | 2 | 4 | **8 (shipped)** | 16 | 32 | 64 | 128 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| tests/pose as % of flat | 67.8 | 48.9 | 36.0 | **31.6** | 33.2 | 39.7 | 48.4 | 63.0 |

So `sm64_saturn_scene_admit_with_scratch` (8.40% of working cycles) is the cost
that **remains after** a 66.5% reduction, at the best leaf size available. There
is no hierarchy headroom left to claim.

## Stage B (occlusion / portals) is also weak on BOB, and now quantified

The same gate reports **`admitted per pose min 125 max 867`**. From some
viewpoints the frustum admits *every cluster in the level*. That is the
open-mountain topology stated numerically, and it is the same property that
defeated the VDP2 RBG0 ground plane: a 16,383 x 16,383 world whose up-facing
faces spread across ~35 height levels has few large occluders.

Cell-and-portal visibility is fully implemented already
(`saturn_scene_admission.c`: portal windows with `node_a`/`node_b`/`open`, stats
down to `cycle_edges`), and BOB supplies none. That remains true — but it is a
deliberate configuration, not an oversight, and feeding it on BOB is not
obviously worth it.

## What is actually left here

- **Nothing on the hierarchy.** Shipped, optimal, oracle-gated.
- **Portals: measure occlusion per level before committing, and start indoors.**
  The castle interior and room-based areas are where a portal graph pays. BOB is
  the worst case and should not be the pilot.
- **The remaining admission-adjacent cost is downstream, not in admission.**
  `demo_terrain_queue_world_lower` (8.19%) and `actor_meshlet_live_depth_bounds`
  (7.28%) scale with the *admitted* set, and on BOB that set is large because
  the level genuinely is visible. Reducing it needs either real occluders
  (levels that have them) or a draw-distance/LOD policy decision — which is a
  product call about what the port draws, not an optimisation.

## Reproducing

```bash
pwsh -NoProfile -File ./tools/saturn/with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-admission-hierarchy
```
