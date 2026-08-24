# S7.1 terrain decimation

> **RETRACTED 2026-08-24 -- the +4.07% below was measured on a corrupted mesh,
> and terrain decimation is not viable on BOB.** Owner testing found textures
> badly broken on the decimated build. `vertex_lock` was computed from vertices
> shared *by index* between materials, but the source duplicates its vertices at
> material boundaries, so **nothing was ever locked** -- the "0/1625 vertices
> locked" line, read at the time as evidence of disjoint material islands, was
> the bug reporting itself. meshoptimizer welds coincident positions to build
> its edge graph, so a collapse could move a triangle onto a neighbouring
> material's texture at zero geometric cost. Geometry and collision stayed
> correct, which is why it passed every check that was run.
>
> Fixed: seams are keyed by **position** (763/1625 lock on BOB) and a
> per-material retention guard fails the build on migration. With that fix the
> mesh will not decimate usefully -- **0.95 is the last clean ratio** (-5%
> triangles), 0.90 wipes material 16, 0.75 wipes 12 and 16. The knob stays off.
>
> What survives: the corrupted build genuinely drew 636 primitives against 867
> and genuinely ran 4.07% faster, so **that figure is an upper bound** on
> primitive elasticity (~0.15 at most), and the QuadriFlow / neural-remesh
> decline stands. The measurement methodology below is sound and reusable; the
> conclusion about decimation as a shipping option is not.


**Date:** 2026-08-24 · Measured end-to-end through the real bake and a sealed
capture, not a reconstruction.

## Result up front

Decimating BOB's terrain to 75% removes **26.6% of the baked primitives** and
buys **3.9% of frame time**.

| | baseline | decimated 0.75 |
|---|---:|---:|
| source triangles | 1,101 | 825 |
| baked primitives | 234 quads + 633 tris = **867** | 189 quads + 447 tris = **636** |
| frames observed | 29 | 29 |
| total vblanks | 281 | **270** |
| VB/frame | 9.6897 | **9.3103** |
| **guest FPS (mean)** | **6.1922** | **6.4444** (+4.07%) |
| guest FPS (1% low) | 5.4545 | **6.0000** (+10.0%) |
| guest FPS (median) | 6.0000 | 6.0000 (unchanged rung) |

Both arms: identical route span (simulation ticks 0–32), identical warmup tick
(0), 29 intervals each, and genuinely distinct builds — `scene_package_hash`
`0d6e2211…` vs `0ef7c92a…`, `source_hash` `00e79985…` vs `04b67747…`, sealed
identities `…cfg684490199c21` vs `…cfg2f304f88972a`.

`guest_fps_mean` here is a true aggregate — `intervals × 60 / total_vblanks`
(`capture_sourceboot_throughput.py:704`), summed over `vblank_delta` — not a
mean of per-interval reciprocals, so the +4.07% is wall-clock and not an
averaging artifact.

## What this says about the strategy

**Primitive count is not the dominant frame cost.** The elasticity is about
**0.15**: a 26.6% cut in primitives returns 3.9% of the frame. Quad-pairing
tolerance was measured first and returned 5.9% of primitives for real texture
warp (`quad-pairing-headroom.md`); decimation avoided that ceiling entirely and
still lands in the same low-single-digit FPS band.

That is the number needed to settle the open escalation question, and the answer
is **no**:

- **A QuadriFlow / neural remeshing tool is not justified.** It is a large
  engineering investment aimed squarely at the lever just measured to be weak.
  Even a perfect quad remesh that halved primitives would, on this elasticity,
  be worth roughly 7–8% — less than S1.12 returned from moving actor emission to
  the slave, for vastly more work and a new asset pipeline to maintain.
- **Pushing the ratio below 0.75 is not justified either.** The owner already
  rejected 0.5 visually ("75% looks nearly identical; 50% does not"), and the
  next increment of decimation buys single-digit percent while visibly degrading
  terrain.
- **The remaining levers are runtime-side, not bake-side.** Master-only work is
  ~70% of the frame (`post-S1.12 decomposition`), and terrain primitive emission
  is only a small slice of it. S1.10 (N64 geo-tree walk removal) is the
  outstanding candidate.

The phase split moved the way the mechanism predicts, which is the internal
consistency check: the pre-notification half of `construction` (where terrain
admission and clustering run on the master) gave up 8 vblank-crossings across
the window while `master_finalization` took 8 back, leaving `construction`
itself pinned at 197. Note that `attributed_vblank_crossings` is an overlapping
phase-accounting field, not wall time — it sums to 335 in *both* arms and
carries negative `unattributed` entries. Do not read throughput off it; read it
off `vblank_delta`.

## What it costs

- One material eliminated: **material 16**, 4 triangles, **0.0047%** of terrain
  area. Below the 0.1% guard, so the tool passed it and reported it.
- Geometric error **0.0169** (meshoptimizer's own metric, UV-weighted).
- Collision is untouched. `levels/*/areas/*/collision.inc.c` is a separate
  source from `geo.inc.c` and never enters this pipeline, so decimation cannot
  move a floor out from under Mario.

## Upstream

meshoptimizer, **MIT**, github.com/zeux/meshoptimizer, vendored at
`work/upstream/meshoptimizer`, HEAD **97bbdce** (2026-08-08), verified live
2026-08-23 (8,258 stars, pushed 2026-08-21). Reuse mode: **dependency** —
`tools/saturn/decimate_terrain_mesh.cpp` calls the published API and copies no
upstream code. `meshopt_simplifyWithAttributes` is used rather than
`meshopt_simplify` so UVs participate in the error metric (VDP1 maps textures
affinely across a quad and is less forgiving of UV distortion than the
perspective-correct N64 source) and so `vertex_lock` can pin material seams.

**The lock count was the bug, and this is how it was rationalised.** The tool
reported **0 of 1,625 vertices locked**, and that was written up here as
correct: the source display lists duplicate vertices at material boundaries, so
the 18 materials are index-disjoint islands, so nothing *needs* locking. Every
step of that is true and the conclusion is still wrong. Index-disjointness does
not stop a collapse from crossing materials, because meshoptimizer welds
coincident positions to build its edge graph -- the duplicated seam vertices are
one vertex to the simplifier, and collapsing between them costs **zero**
geometric error precisely because they occupy the same point. A zero lock count
on a mesh with 18 textured materials should have read as an alarm, not a
property.

Seams are now keyed by position: **763 of 1,625 vertices** lock on BOB, and the
report emits `vertices_locked_material_seam` alongside `vertices_shared_by_index`
so the two are never conflated again. The lesson generalises past this tool: a
guard that reports "nothing to do" is making a claim about the input, and that
claim needs its own check.

## Two design defects found while landing this

Both were caught by the tool's own guards rather than by looking at the game
later, and both are fixed:

1. **The step decimated in place** over the extractor's own output, while
   `compile-bob-area` is phony and make invokes it several times concurrently.
   One invocation re-read an already-decimated mesh and decimated it a second
   time — 1,101 → 825 → **705**, eliminating materials 12 and 13 (0.64% of
   terrain area each). The lost-material guard refused it. Fixed: decimation now
   writes `bob_area1_mesh_ir_v2_decimated.json` and the input the extractor owns
   is never mutated; the in-place case is refused outright. This also un-breaks
   `test_mesh_ir_obj_shim.py`, which reads the extractor's path directly.
2. **Concurrent invocations shared intermediate paths**, so one read another's
   half-written file (a JSON decode error at 0.75). Fixed: intermediates are
   per-process and both the mesh IR and the report are published by rename.

The rule immediately below this one in the Makefile already documents the same
hazard class for `bob_area1_bsp_report.json` (`Makefile.saturn.mk:2414`).

## Reproducing

The knob is **off by default** (`SATURN_TERRAIN_DECIMATE_RATIO ?= 1.0`), and the
default path is verified inert: a default `compile-bob-area` bakes 867
primitives and never invokes the decimator. Enabling it changes
`scene_package_hash`, which requires a package-generation bump, and the bundle
publication is no-clobber by design, so bootstrap the new generation once first:

```bash
pwsh -NoProfile -File ./tools/saturn/with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 SCENE_PACKAGE_GENERATION=18 SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=1 SATURN_FEATURE_ACTOR_PHASE_SLAVE=1 SATURN_FEATURE_ACTOR_EMIT_SLAVE=1 SATURN_TERRAIN_DECIMATE_RATIO=0.75 compile-actor-family-bundle
```

Then build and capture. **The warmup tick must match between arms** — an arm at
`--warmup-ticks 150` against a baseline at `0` compares different stretches of
the route and is meaningless (it read +2.2% before the ticks were matched, versus
+4.07% once they were).

```bash
pwsh -NoProfile -File ./tools/saturn/build-sourceboot.ps1 -ProfilePath tools/saturn/profiles/sourceboot-bob-coins-v1.json SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=1 SATURN_FEATURE_ACTOR_PHASE_SLAVE=1 SATURN_FEATURE_ACTOR_EMIT_SLAVE=1 SATURN_TERRAIN_DECIMATE_RATIO=0.75 SCENE_PACKAGE_GENERATION=18
```

```bash
.venv-saturn-tools/Scripts/python.exe tools/saturn/capture_sourceboot_throughput.py --ymir "D:/Code/RetroDev/sm64-saturn-port/ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe" --ipl "D:/Code/RetroDev/sm64-saturn-port/sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin" --game "$D/sm64-saturn-sourceboot-e2.cue" --elf "$D/obj/sm64-saturn-sourceboot-e2.elf" --release-manifest "$D/saturn-release-manifest-v1.json" --startup-vblanks 4096 --warmup-ticks 0 --presentation-events 30 --max-vblanks 3600 --timeout 3000 --output out.json
```

`--ymir` must be the **build-agent2** binary; the older `build-agent`
ymir-headless silently ignores `--dram-cart`. `--game` must be an absolute path
to the `.cue`.
