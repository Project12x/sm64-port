# Baked visibility — scoped against the admission API we already have

**Date:** 2026-08-24 · Scoped from the shipped admission implementation, BOB's
live scene binding, and the T2.10 admission counters. No new capture.

## The finding that sets the scope

`sm64_saturn_scene_admission.c` already implements **cell-and-portal
visibility** in full: nodes carrying bounds, cluster refs, portal refs and a
child hierarchy; portal windows carrying bounds, `node_a`/`node_b` and an `open`
flag; and statistics for `portals_tested`, `portals_open`,
`portals_rejected_closed`, `portals_rejected_frustum` and `cycle_edges` (so
cyclic portal graphs are handled).

BOB feeds it (`saturn_demo_render.c:931-947`) clusters, nodes, cluster refs — and
then:

```c
scene.portal_ref_count = 0U;
scene.root_node        = 0U;          /* hardcoded, not the camera's cell */
.portal_capacity       = 0U           /* output */
```

`scene.portals` is never assigned at all. But the decisive number is from the
T2.10 counters:

| counter | value |
|---|---:|
| `clusters_tested` | 867 |
| `clusters_admitted` | 307 |
| `clusters_rejected_frustum` | 560 |
| `admitted_share_of_tested` | **35.4%** |
| **`nodes_tested`** | **1** |
| **`nodes_admitted`** | **1** |
| `portals_tested` | 0 |

**The node hierarchy is a single root node.** Despite `scene.nodes` being
supplied and the traversal being implemented, there is no spatial subdivision:
admission performs a **linear frustum test over all 867 clusters, every frame**,
and throws away 560 of them one at a time.

That is what `sm64_saturn_scene_admit_with_scratch` (8.40% of working cycles,
112,369 cyc/frame) is spending its time on.

## Two stages, and they buy different things

This distinction matters and is easy to get wrong:

**Stage A — a real node hierarchy. Makes the test cheaper; admits the same
clusters.** Rejecting a subtree by its bounds removes hundreds of individual
cluster tests, turning an O(n) scan into O(log n) plus the admitted set. It
attacks the **8.40%** admission cost. It does **not** reduce the 307 admitted
clusters, so it does not touch the downstream per-cluster work.

**Stage B — portals / occlusion. Reduces the admitted set.** Only this attacks
the downstream work that scales with admitted clusters:
`demo_terrain_queue_world_lower` (8.19%, 109,574 cyc) and
`actor_meshlet_live_depth_bounds` (7.28%, 97,412 cyc). Together with admission
itself that is the **~24%** headline — but only Stage B reaches the other 15.5%.

## The honest risk on BOB

**BOB is an open mountain, which is the worst case for portal visibility.** The
VDP2 RBG0 ground scoping already established the shape of this terrain: a
16,383 x 16,383 world with a 4,677-unit height span and up-facing faces spread
over ~35 distinct height levels. Cell-and-portal culling pays where sightlines
are bounded by geometry — corridors, rooms, the castle interior. On an outdoor
mountain you can see much of the level from much of it, so the open-portal set
stays large and Stage B's prize shrinks.

Stage A is **level-agnostic** and pays regardless: a linear scan over every
cluster is wasteful whatever the topology.

Therefore: **Stage A first, unconditionally. Stage B measured per level, castle
and interior areas first, and never assumed from BOB's numbers.**

## Prior art already in the tree

- **A BSP compiler exists**: `tools/saturn/compile_bob_bsp.py` produces
  `bob_bsp.h` and `bob_area1_bsp_report.json`. It is not currently feeding
  `scene.nodes` — that binding is the first thing to check, because the
  hierarchy may be half-built already.
- `sonic-z-treme` (GPL, `work/upstream/`) — its frustum type is already
  referenced by our admission header (`sm64_saturn_ztreme_frustum_t`), so its
  traversal is the natural comparison for Stage A.

## Scope

**Stage 0 — confirm the binding (hours).** Establish why `node_count` collapses
to one usable node: is `sm64_saturn_bob_scene_admission_nodes` genuinely a
single entry, or is a populated hierarchy being rejected by validation? The
answer changes Stage A from "build a hierarchy" to "fix a binding". Re-read the
counters on the current build — T2.10's numbers predate S1.11/S1.12.

**Stage A — spatial hierarchy (the main prize).** Bake a bounded-depth node tree
over BOB's clusters, emit it into the scene package beside the existing cluster
arrays, and let admission reject subtrees. Acceptance: `nodes_tested` >> 1,
`clusters_tested` well below 867, identical admitted set (the admitted cluster
list must be **byte-identical** to today's — this is a pure speed change and any
difference is a bug).

**Stage B — portals, per level.** Bake portal windows between adjacent cells and
set `root_node` from a per-frame camera point-locate. Gate on a measured
occlusion ratio per level before committing; do not start on BOB.

**Risk:** low for Stage A — it is a pure acceleration structure with an exact
oracle (the admitted set must not change). Medium for Stage B, which changes
what is drawn and needs visual verification.
