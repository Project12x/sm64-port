# VDP2 ground (RBG0) — scoping. Verdict: not worth it for BOB.

**Date:** 2026-08-23 · Scoped against measured frame data and BOB's actual
compiled geometry (`build/saturn/sourceboot/generated/bob_area1_compiled.json`).

## Why it looked promising

The frame decomposes into three spans, and the two where the master works
**alone** are pre-notification (2.345 VB, 91% of it `spatial_admit`) and
finalize (5.000 VB, VDP1 command emission). Terrain that becomes a VDP2
rotation plane leaves both: it is neither admitted nor emitted. On paper that
is the best-shaped lever in the frame.

## Why it does not work for BOB

BOB is a mountain, and RBG0 is **one surface**.

BOB's world is 16,383 × 16,383 with a **4,677-unit height span**
(Y from −383 to 4,294). Classifying all 867 terrain primitives by face normal:

| orientation | primitives | share | share of surface area |
|---|---:|---:|---:|
| floor/ceiling (≤15° from horizontal) | 278 | 32.1% | 44.1% |
| slope 15–45° | 193 | 22.3% | 21.7% |
| steep 45–75° | 83 | 9.6% | 14.4% |
| wall >75° | 313 | 36.1% | 19.7% |

The 263 up-facing flat faces are **spread across ~35 distinct height levels**.
Area by level:

| level | area | share of ALL terrain | primitives |
|---|---:|---:|---:|
| Y≈768 | 63.6M | **15.6%** | 42 |
| Y≈0 | 30.8M | 7.6% | 46 |
| Y≈1024 | 21.7M | 5.3% | 29 |
| Y≈896 | 12.7M | 3.1% | 16 |
| remaining ~31 levels | — | 11.9% | 130 |

**A single RBG0 plane can take at most the Y≈768 level: 42 of 867 primitives.**
A coefficient table lets the plane tilt or curve per scanline, but it is still
one continuous surface — it cannot represent 35 terraces.

## What that is worth, in the currency that matters

VDP1 **fill** is not the frame limiter (measured earlier:
`previous_frame_reuse_count` delta 0). **Command emission** is — 5.000 VB/frame.
Command cost is roughly per-primitive, so the relevant metric is primitive
count, not area. That is exactly where the Y≈768 level is weakest: it is 15.6%
of *area* but only 4.8% of *primitives*, i.e. a few large quads.

Measured mix: **574.4 VDP1 commands per present**, actors 310.1 (53.98%), the
balance terrain/textured. Removing 42 primitives is an upper bound of ~42
commands/frame ≈ **7% of commands**, and only when all 42 are admitted — the
realistic figure is lower. Against a 5.000 VB emission span that is roughly
**0.2–0.4 VB/frame**, or about 2–4% FPS.

## Cost side

Not small: RBG0 plane setup and VRAM/CRAM budgeting alongside the existing NBG1
sky, a rotation-parameter table and per-frame update from the camera, a ground
texture bake (BOB's ground is currently many small textured/flat quads, not one
tileable image), and priority/colour-calculation interaction so VDP1 sprites
sort correctly against the plane. Plus the terraced-level mismatch above means
the plane covers one level and every other flat level still ships as VDP1
geometry, so nothing is deleted from the pipeline — only reduced.

## Verdict and what to do instead

**Do not build RBG0 ground for BOB.** ~2–4% FPS for a large change, against a
level whose geometry is the worst case for a rotation plane.

Worth revisiting **per level, not globally** — the calculus flips wherever
ground is genuinely one plane. Whomp's Fortress platforms and the castle
grounds/lobby are plausible candidates and should be re-measured with the same
script before any commitment.

The underused-VDP2 observation still stands and has better targets than ground:
today VDP2 carries only an NBG1 sky bitmap and HUD text, leaving NBG0/2/3 and
RBG0/RBG1 idle. The better use is **distant terrain as a VDP2 backdrop layer**:
far-LOD geometry is where primitive counts are high and per-primitive screen
area is small, which is precisely the opposite of the Y≈768 ground quads and
therefore the right shape for removing *commands*. That deserves its own
scoping pass.

## Reference material checked

- `sonic-z-treme` (GPL, in `work/upstream/`): **`ztSetRBG0()` is empty.**
  Z-Treme sets an RBG0 priority and colour-calculation flags but never
  configures a rotation plane — its terrain is VDP1 polygons. Not prior art for
  this.
- `slavedriver-engine` (in `work/upstream/`): RBG0 referenced in
  `FLASH/INITMAIN.C`, `MENU.C`, `MOV.C`, `PLAX.C` — worth reading if RBG0 is
  ever pursued for another level.
- `libyaul-examples/vdp2-rbg0` and `vdp2-rbg0-bitmap`: the working API
  reference — `vdp2_scrn_rotation_params_t`, `rp_table_base`, coefficient table
  (`kast`, per-line and per-dot increments), `VDP2_SCRN_RBG0_PA`. This is what
  an implementation would be built on.
