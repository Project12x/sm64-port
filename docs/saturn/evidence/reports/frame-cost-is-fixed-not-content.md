# The frame cost is fixed, not content-driven — three measurements agree

**Date:** 2026-08-24 · Measured on the shipped baseline
(`e2-bob-identity-id-684490199c21c5f8`, 6.1922 FPS, 9.690 VB/frame), matched
route span and warmup tick 0 throughout.

## The experiment

`DEMO_FAR_DEPTH` (`saturn_demo_render.c:179`) sets the frustum far plane. BOB's
world is 16,383 units across, so the shipped 8,192 draws roughly half the level
from any viewpoint — consistent with the admission gate's
`admitted per pose min 125 max 867`. If frame cost tracked what is drawn,
cutting the draw distance should be a large win.

| draw distance | VB/frame | guest FPS | vs shipped | 1% low |
|---|---:|---:|---:|---:|
| **8192 (shipped)** | 9.6897 | 6.1922 | — | 5.4545 |
| 4096 (half) | 8.9310 | 6.7181 | **+8.49%** | 6.0000 |
| 2048 (quarter) | 8.7931 | 6.8235 | **+10.20%** | 6.0000 |

**Halving the visible world buys 8.5%. Quartering it buys only 1.7% more.** The
curve is flat almost immediately. Drawing a quarter of the level — which would
look unacceptable — leaves the frame within 10% of where it started.

## Three independent measurements now say the same thing

| measurement | result |
|---|---|
| Primitive elasticity (S7.1 decimation) | -26.6% primitives -> **+4%** at most (elasticity <=0.15) |
| Draw distance (this report) | quarter the distance -> **+10.2%**, saturating |
| Current cycle profile | top symbol **9.49%**, 444-symbol tail at **18.96%** |

These are not three separate disappointments. They are one fact seen three ways:
**roughly 90% of the frame is fixed per-frame cost that does not scale with what
is visible.** Reducing content — fewer primitives, shorter draw distance, more
aggressive culling — cannot reach it, because the work is not proportional to
content in the first place.

## What the fixed cost is

From the master-only profile (`profile-master-only-2026-08-24.json`, 584
symbols, 96.4% busy), the work that runs per-frame or per-actor regardless of
visibility:

| | share of master work |
|---|---:|
| soft-float + libm | 16.33% |
| N64 geo walk + matrix stack | 13.54% |
| actor pipeline | 12.04% |
| memory ops (`memcpy`/`memset`/`memcmp`) | 7.37% |
| long tail (444 symbols) | 18.96% |

`_demo_render_prepare_publish` (6.03%), `_sm64_saturn_actor_pose_evaluate_staged`
(4.20%), `_sm64_saturn_matrix_mul` (4.36%), `_saturn_geo_enter_object` (3.70%)
and `_saturn_mtxq_refresh_float_mirror` (2.02%) all run per actor per frame
whether or not that actor is drawn.

## Consequences for the plan

- **Content reduction is closed as a strategy.** Terrain decimation (not
  viable), quad pairing (5.9% for texture warp), occlusion/portals (BOB has no
  occluders), and now draw distance (+10% saturating) all attack the same ~10%.
  Do not re-open them.
- **The target is the per-frame fixed machinery.** That is S1.10 (replace the
  N64 geo walk and delete `gGfxPools`), the actor pipeline, and the
  publication/validation/copy overhead — not the renderer's content path.
- **This also re-prices the master/slave rebalance honestly.** Its ceiling is
  +60% (`profile-master-only`/`-slave-only`: master 96.4% busy, slave 23.8%),
  and it moves fixed work rather than removing it. Worth doing, but it is a
  scheduling win on top of a cost that should not exist.
- **20 FPS requires removing the fixed cost, not degrading the picture.** The
  "Saturn-shaped degradation" instinct was sound in principle and is simply not
  where this port's time goes: the port is not slow because it draws too much,
  it is slow because every frame it re-does N64-shaped bookkeeping for the whole
  actor and scene graph.

## Reproducing

Edit `DEMO_FAR_DEPTH` in `src/port/saturn/gfx/saturn_demo_render.c:179`, rebuild
the sourceboot profile, and capture cadence at `--warmup-ticks 0`. Editing the
constant changes `source_hash` and therefore the sealed identity on its own — no
new build flag, and no package-generation bump, because the far plane is a
runtime frustum parameter and does not touch the baked scene package. The three
identities measured here were `684490199c21c5f8` (8192), `65741bf2db8a27d9`
(4096) and `bb299b25f799d26c` (2048). The constant is restored to 8192.
