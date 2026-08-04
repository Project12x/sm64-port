# Visual-slice upstream code ledger

Last updated 2026-08-04.

This is the implementation-facing companion to [PROVENANCE.md](PROVENANCE.md).
It answers a narrower question: which reviewed upstream code or design pattern
should inform the next Saturn visual-slice change, where it belongs, and what
is explicitly *not* being imported. Nintendo assets remain local-only inputs;
no extracted ROM asset is committed.

## Rules

- A source may be used only at the pinned revision recorded below.
- "Pattern-only" means the destination implementation is original and keeps
  the target's Yaul interfaces and data layout; it is not a translation.
- Direct-copy, fork, or close-port work needs a source-path note, notices,
  license text, and a change note in `PROVENANCE.md` before it is merged.
- The current visual milestones deliberately prefer small host tools and
  target-specific C over importing another engine runtime.

## A5 — shared opportunistic SH-2 queue (2026-08-04)

| Upstream | Pin / license | Inspected code | Reuse mode and concrete destination |
|---|---|---|---|
| `Lobotomy-Software/SlaveDriver-Engine` | `a8986591557b6e680550d3c23970284d3b38ff8f` / GPL-3.0-or-later | `WALLS.C:1803-1950` | **Pattern-only.** Its one persistent slave loop and disjoint compact results establish the ownership constraint. `src/port/saturn/gfx/saturn_render_job_queue.{h,c}` is original project-owned code with a different fixed descriptor ABI; no scheduler code was copied. |
| `Maxime-XL2/SONIC-Z-TREME` | `cff75451c1616aac1236fc2b44223902b55c706b` / GPL-3.0 | `Projects/SONIC Z-TREME/ZTE/ZT_RENDERING.c:718-786`, `workarea.c:12-25` | **Pattern-only.** Early slave dispatch and fixed, non-overlapping work areas support bounded producer/consumer work, but SGL command ownership and callback ABI are architecture-incompatible. No source is copied. |

## M1 — living title face

| Upstream | Pin / license | Inspected code | Reuse mode and concrete destination |
|---|---|---|---|
| [R11/saturn-libs](https://github.com/R11/saturn-libs) | `cecf21a68dfca4388887b28e906bb37b95f0849c` / MIT | `saturn-vdp2/core/saturn_vdp2_bg_core.c`, `saturn-vdp2/saturn/saturn_vdp2_bg_saturn.c`, `saturn-smpc/core/saturn_smpc_core.c`, and their host tests | **Pattern-only.** Use its separable background-state and button-edge-test shape to implement a Yaul NBG1 title backdrop below NBG0 text and a Start edge in `src/port/saturn/introface/main.c`. SGL calls and physical-address conventions are not copied. |
| `yaul-org/libyaul` | `6012f79f237773378c8014e70d8998ad95a38d98` / MIT | VDP1, VDP2, peripheral, DMA, and fixed-point public APIs recorded in `PROVENANCE.md` | **Dependency.** It remains the only target hardware API layer. New title work uses its APIs directly rather than wrapping or importing R11's SGL layer. |
| [yaul-org/libyaul-examples](https://github.com/yaul-org/libyaul-examples) | `66b648eb059bb8bb7392eac70821605a68205b85` / MIT | `vdp2-normal-bitmap/vdp2-normal-bitmap.c` | **Close-port (small configuration pattern).** Its A0/A1 VDP2 VRAM cycle allocation for RGB555 bitmap fetches is adapted in `src/port/saturn/vdp2probe/main.c` and `introface/main.c`; no assets, decoder, or runtime are copied. The probe documents the isolated before/after evidence. |
| `johannes-fetz/joengine` | `556d081146211b6a1cfa6591d70f9487d406758b` / MIT plus BSD-3-Clause-style file headers | `jo_engine/vdp1_command_pipeline.c`, `jo_engine/3d.c` | **Pattern-only.** Retain the existing persistent command lifetime and fixed setup-command prefix; do not add Jo Engine's allocator to the libyaul renderer. |

Implementation commitments:

1. Add an original local-only title-asset converter under `tools/saturn/` when
   the source image/layout is selected. It must emit RGB555/NBG-ready data and
   a manifest with input SHA-256, dimensions, palette/format, and output size.
2. Keep the title face's deformation, eye ordering, and shine in the existing
   VDP1 path. The VDP2 background and `PRESS START` are separate layers so
   their cost is measurable independently.
3. Capture neutral, animated, shine-off, and Start-handoff frames in the
   gallery; record background bytes, VDP1 command count, and frame timing.

## M2–M3 — actor and Castle-lobby asset path

| Upstream | Pin / license | Inspected code | Reuse mode and concrete destination |
|---|---|---|---|
| yaul-org/libyaul libmic3d | `6012f79f237773378c8014e70d8998ad95a38d98` / MIT | `libmic3d/render.c`: `__render_init`, `render_sort_depth_set`, `render_points_xform`, `render_mesh_xform`, `render_end`, `_perspective_transform`, and the depth/clip helpers; `libmic3d/sort.c` and `sort.h`: `__sort_init`, `__sort_insert`, `__sort_iterate`; `libmic3d/light.c`: `__light_transform`, `_polygon_process`, `__light_gst_put`, `light_gst_set` | **Dependency / planned close-port.** Reuse its transform-once screen/depth pools, caller-sized power-of-two depth domain, linked bucket lists, bounded command pool, and linear Gouraud-table staging in the scene-neutral Saturn backend. The port keeps its source-identified render jobs and explicit VDP1 command ownership rather than adopting libmic3d as an opaque scene runtime. Preserve libyaul's MIT notice for any closely ported implementation. |
| [SaitoTsutomu/Tris-Quads-Ex](https://github.com/SaitoTsutomu/Tris-Quads-Ex) | `f5acd93873728c45d48c3398382aec380a280182` / Apache-2.0 | `__init__.py`, `README.md` | **Pattern-only test oracle.** Its one-selected-edge-per-triangle objective informs an independent regression case for `tools/saturn/quad_pairing.py` and `tools/saturn/test_tools.py`. Keep NetworkX exact matching; do not add Blender or PuLP to the project. Saturn filters for material, winding, convexity, UVs, and deformation stay mandatory. |
| [HailToDodongo/pyrite64](https://github.com/HailToDodongo/pyrite64) | `297a10e606af6149327364d8b694f136c62b506e` / MIT | `src/project/assets/model3d.h`, `collision.h`, `src/renderer/n64Mesh.h`, `animation.h` | **Pattern-only.** Create an original limited Fast3D-to-Saturn IR exporter under `tools/saturn/` with explicit model/material partitions, source primitive IDs, collision data, and animation streams. The C++/desktop/libdragon runtime is not a Saturn runtime candidate and no code is copied. |
| [zeux/meshoptimizer](https://github.com/zeux/meshoptimizer) | `dc9d09ed83e1004aef47a1c3c597e0ec64848a37` / MIT | `src/meshoptimizer.h`, `src/indexgenerator.cpp`, `src/vfetchoptimizer.cpp` | **Pattern-only.** Preserve independent position/attribute/index streams in the new IR so seams and per-material splits survive conversion. Do not introduce a native dependency until host-tool profiling proves it worthwhile. |
| [malucard/sm64-psx](https://github.com/malucard/sm64-psx) | `3073845688ea273da78d539b20c45110d8a868c3` / no repository-wide license found; bundled components vary | `README.md`, `src/game/game_init.c`, `src/port/gfx/gfx_rsp_jit.c`, `src/port/psx/gfx_dl_exec_psx.c`, `gfx_tessellation_psx.c`, `gfx_texture_psx.c`, `cd_psx.c`, `controller_psx.c`, `src/game/hud.c`, `tools/preprocess_graphics.py`, `convert_image_psx.py`, `pack_textures.py`, `compress_mario_anims.c` | **Behavior study only.** The port proves a source-loop/compact-IR/asset-preparation architecture: `OSContPad` input, source frame/level execution, target command lowering, whole-loop profiling, prepared texture/animation residency, and target-specific large-polygon policy. Saturn must implement those boundaries through Yaul, VDP1/VDP2, and the 4 MiB cartridge—not copy PS1 source, GTE math, ordering tables, VRAM layout, or packet format. The concrete Saturn decision and limits are in `PSX_PORT_ARCHITECTURE_LESSONS.md`. |
| [yaul-org/libyaul-examples](https://github.com/yaul-org/libyaul-examples) | `66b648eb059bb8bb7392eac70821605a68205b85` / MIT | `vdp1-mesh/vdp1-mesh.c`, `cd-block/cd-block.c` | **Close-port (small peripheral cadence).** `marioturntable/main.c` uses the same public Yaul pattern: initialize SMPC, issue INTBACK from VBlank-out, process the completed collection in the frame loop, and read port 1. Button mapping and the `OSContPad` backend remain original Saturn-port code. |
| `yaul-org/libyaul` | `6012f79f237773378c8014e70d8998ad95a38d98` / MIT | `sh-elf/lib/ldscripts/yaul.x`; public `dram-cart.h`, CD block, and CDFS interfaces | **Configuration adaptation + dependency/API use.** `sourceboot-cart.x` is a small attributed adaptation of Yaul's linker layout: source-tree read-only data is linked at `0x22400000`, while Yaul/runtime code stays in HWRAM. `source_cart.c` calls the public cart/CD/CDFS APIs to load `SOURCE.DAT` through a bounded 16 KiB staging block before the original game loop. No Yaul loader implementation is copied. |
| `yaul-org/libyaul` | `6012f79f237773378c8014e70d8998ad95a38d98` / MIT | `libmic3d/render.c`, `libmic3d/sort.h`, `libyaul/scu/bus/b/vdp/vdp1/cmdt.h`, `vdp1_vram.c` | **Dependency / API use.** Its sort modes establish that the permissive Saturn references provide depth keys, not a reusable static BSP. The original host BSP therefore retains exact SM64 attributes and emits only Yaul command data. The next indexed-bank pass will call its public `VDP1_CMDT_CM_CLUT_16`, `vdp1_cmdt_color_mode1_set`, and CLUT partition APIs directly. |
| `johannes-fetz/joengine` | `556d081146211b6a1cfa6591d70f9487d406758b` / MIT plus file-level BSD-style terms | `jo_engine/3d.c`, `jo_engine/jo/sega_saturn.h` | **Pattern-only.** Its `SORT_CEN` path confirms a center-depth renderer but contains no static-world BSP to port. No Jo Engine code enters `tools/saturn/static_bsp.py`. |
| [Maxime-XL2/SONIC-Z-TREME](https://github.com/Maxime-XL2/SONIC-Z-TREME) | `cff75451c1616aac1236fc2b44223902b55c706b` / GPL-3.0 | `ZT_FRUSTUM.c:126-161`; `ZT_RENDERING.c:406-505,718-786`; `ZT_LOADING.c:118-176,299-355`; `workarea.c:14-20`; `ZTE_DEF.H` | **Pinned pattern study plus authorized close-port.** The inspected implementation supplies bounded fixed work areas, hierarchy AABB/frustum rejection, inherited inside/intersects state, camera-relative near-to-far child traversal, per-leaf full/LOD selection, distant-entity suppression, contiguous level-package loading, and early render-job dispatch. `src/port/saturn/gpl/ztreme_frustum.{c,h}` now close-ports the tri-state AABB decision against generated tight BOB bounds. Applicable helpers may be close-ported into `src/port/saturn/gpl/`; experimental PVS and loose map bounds are explicitly rejected. Any reuse retains pinned provenance, copyright/GPL notices, change records, and corresponding-source/copyleft compliance. |
| [Lobotomy-Software/SlaveDriver-Engine](https://github.com/Lobotomy-Software/SlaveDriver-Engine) | `a8986591557b6e680550d3c23970284d3b38ff8f` / GPL-3.0-or-later | `WALLS.C:288-500,1240-1408,1803-1950,2062-2285`; `DMA.C`; `DMA.H`; `V_BLANK.C:94-145` | **Pinned pattern study plus authorized close-port.** Applicable pieces are view-space shade-preserving clipping, fixed-capacity compact results, cache-through worker writes, coarse master/slave partitioning, one join, spin-count balancing, command linking, queued DMA, and VBlank ownership. Portal/sector data structures are rejected for SM64. Reuse is isolated under `src/port/saturn/gpl/` with pinned provenance, copyright/GPL notices, change records, and corresponding-source/copyleft compliance. |
| [Lobotomy-Software/SlaveDriver-Engine](https://github.com/Lobotomy-Software/SlaveDriver-Engine) | `a8986591557b6e680550d3c23970284d3b38ff8f` / GPL-3.0-or-later | `WALLS.C:1803-1950` | **Close-port:** `src/port/saturn/gpl/slavedriver_terrain_worker.{c,h}` provides one same-frame coarse range notification over the existing Yaul polling worker. It carries no backend or game-state ownership; the callback writes only disjoint compact result spans. |
| [Lobotomy-Software/SlaveDriver-Engine](https://github.com/Lobotomy-Software/SlaveDriver-Engine) | `a8986591557b6e680550d3c23970284d3b38ff8f` / GPL-3.0 | `WALLS.C:1806-1950` (bounded master/slave hand-off and spin-count balancing) | **Close-port:** `src/port/saturn/gpl/slavedriver_dual_worker.{c,h}`. Yaul polling-mode notification replaces the upstream runtime; callbacks and disjoint output ranges are new adaptation code. No world-renderer source copied. |
| [musl libc](https://git.musl-libc.org/cgit/musl/tree/src/math/sqrtf.c) | file blob `740d81cbab421707a090d2475523807fd27b1337` / MIT-compatible project copyright terms | `src/math/sqrtf.c`, `COPYRIGHT` | **Pattern study only.** The SH freestanding build needs positive-domain `sqrtf` for SM64 vector magnitudes. Musl's implementation depends on its reciprocal-square-root table, floating-point environment helpers, and internal libm ABI, so `src/port/saturn/compat/sqrtf.c` uses an independent exponent seed plus four Newton steps instead of importing that architecture-mismatched dependency. |

Implementation commitments:

1. Define a generated, versioned Saturn IR record before adding a general
   display-list interpreter. Minimum fields: source display-list/primitive ID,
   material and texture key, vertex streams, representation decision and
   rejection reason, plus optional collision and animation references.
2. Keep the current compiler's conservative matching. Add the Apache-pattern
   objective only as a deterministic host regression, never as a reason to
   merge unsafe animated or arbitrary-UV geometry.
3. M2 proves the IR with Mario; M3 uses the same records for Castle Area 1.
   A PC reference renderer and Ymir must capture matching named camera views.
4. Texture reduction remains an original offline RGB1555 box filter in
   `tools/saturn/vdp1_texture.py`. A general-purpose image dependency is an
   architecture mismatch for the five-bit/binary-alpha output contract. Both
   Mario and Castle bakers expose source-scale and emitted-tile controls and
   must report the resulting residency.
5. Follow the PSX port's milestone ordering without inheriting its backend:
   get the original game loop, controller state, collision, camera, behaviors,
   and graph traversal running before texture perfection. Translate emitted
   Fast3D state at the renderer boundary; do not keep adding per-room placement
   or camera logic to the Saturn viewer.
6. Avoid the PSX port's documented texture-loading stall: package an area
   manifest and batch/prefetch its texture bank into the 4 MB RAM cart, then
   promote only the visible working set to VDP1 VRAM. The texture tools must
   support per-class 1x/2x/4x source scales and 8/16/32-pixel output profiles.
   Prepared Mario animation banks belong in the same area/actor residency
   manifest, with source action selection remaining in the original game loop.
7. The behavior study of `src/port/psx/gfx_dl_exec_psx.c` uses the farthest
   transformed vertex for its opaque ordering-table key. The Castle diagnostic
   now exercises that conservative key while retaining source order inside a
   bucket. Its related large-polygon subdivision idea is exposed as an offline
   source-diagonal control, but the 512-unit blanket experiment is rejected:
   it increased VDP1 triangle seams. No PSX source is copied; the repository's
   absent root license keeps both changes behavior-only at pin `3073845688`.
8. The all-layer Castle path uses the pinned Yaul
   `vdp1_cmdt_draw_mode_t` contract from
   `libyaul/scu/bus/b/vdp/vdp1/cmdt.h` and the repository's existing
   `src/port/saturn/hwtest/main.c` half-transparency probe. This is a
   **dependency/API adaptation**: N64 RGBA16 alpha remains RGB1555 bit 15 for
   VDP1 transparent-pixel rejection, while only `LAYER_TRANSPARENT_DECAL`
   selects `VDP1_CMDT_CC_HALF_TRANSPARENT`. No upstream renderer
   implementation is copied.
9. The textured-quad Castle checkpoint reuses the existing original
   `tools/saturn/quad_pairing.py` exact NetworkX matching path. Re-reading the
   BIOS-backed probe with Saturn RGB1555 lane order corrected shows ordinary
   A/B/C/D character corners; repeated C=D triangles merge source D into
   destination C in `tools/saturn/vdp1_texture.py`. This agrees with pinned
   MIT Yaul example
   `vdp1-uv-coords/vdp1-uv-coords.c` at
   `66b648eb059bb8bb7392eac70821605a68205b85` (**validation/pattern-only**);
   no upstream mesh code is copied. The
   target directly adapts pinned Yaul's MIT-licensed
   `libyaul/scu/bus/b/vdp/vdp1_vram.c` partition API so generated command,
   texture, and Gouraud counts—not the default fixed partition—own VDP1 VRAM.
    The preserved 144-quad failure establishes a local policy constraint:
    planar-convex validity does not imply painter-safe granularity.
10. The static-world ordering pass is original because the pinned permissive
    Saturn candidates expose depth sorting but no reusable BSP implementation.
    `tools/saturn/static_bsp.py` uses exact rational planes, splits, and
    attribute interpolation; `bake_castle_uv.py` serializes its 352 nodes and
    inserts dynamic Mario at traversal time. A 144-split/884-tile capture
    preserves the unresolved texture fans, proving that painter order is not
    their primary cause.
11. The PS1 longest-edge lesson was tested **pattern-only** after BSP lowering,
    where each child remains in the same node. Those stress profiles were
    useful diagnosis but became unnecessary after the VDP1 corner correction:
    the accepted exact BSP bank has 884 tiles, zero adaptive splits, and 28,288
    CLUT bytes. No PS1 code is copied.
12. The four-bit Castle path directly uses pinned Yaul's MIT public
    `VDP1_CMDT_CM_CLUT_16`, `vdp1_cmdt_color_mode1_set()`, CLUT partition, and
    `end_code_disable` field. Quantization and nibble packing are original host
    code: each source material receives transparent index zero plus fifteen
    deterministic RGB555 colors. The first end-code failure and corrected
    frames are both retained. A valid build-flagged A-B-C-D probe, interpreted
    with the correct RGB1555 lane labels, resolves native character corners as
    A/B/C/D; repeated C=D fallbacks merge D into C. The old 5,253-tile stress
    frame remains failure evidence, while the unsplit 884-tile bank is now the
    default.
13. Source-camera extraction is a **close port** of the values and branching in
    `src/game/camera.c`: the Area 1 spawn lies outside the local
    `cam_castle_lobby_entrance` trigger, so the generated initial base comes
    from `set_fixed_cam_axis_sa_lobby`. The target adds original viewport
    rejection and coordinate saturation as Saturn-specific lowering; it does
    not change source room vertices or author camera coordinates.
14. The PS1 study adds an E2/E3 architecture constraint: source loop and level
    execution must select render work; generated Saturn command templates only
    patch dynamic matrices, visibility, ordering, and residency. A benchmark
    cannot rely on the PS1 port's 8 MiB non-retail configuration. The Saturn
    4 MiB cartridge policy stays authoritative.

## M3 onward — inspection and debugging evidence

## Native parity Task 1 — Q16 kernel reuse (2026-07-27)

| Upstream | Pin / license | Inspected code | Reuse mode and concrete destination |
|---|---|---|---|
| `johannes-fetz/joengine` | `556d081146211b6a1cfa6591d70f9487d406758b`; MIT root plus BSD-3-Clause-style source notice | `jo_engine/math.c:57-70` (`jo_fixed_mult`) | **Direct adaptation.** The compact `dmuls.l`/`sts`/`xtrct` primitive is adapted in `src/port/saturn/gfx/saturn_q16_sh2.h`; that destination retains the source notice, upstream pin, and material-change note. |
| `Lobotomy-Software/SlaveDriver-Engine` | `a8986591557b6e680550d3c23970284d3b38ff8f`; GPL-3.0-or-later | `WALLASM.S:253-353` | **Close-port.** `src/port/saturn/gpl/slavedriver_projection.sx/.h` retains the GPL declaration, source path, pin, and change note. It preserves launch → independent work → collect scheduling, not renderer code. |
| `yaul-org/libyaul` | `6012f79f237773378c8014e70d8998ad95a38d98`; MIT | `libyaul/scu/bus/cpu/cpu/map.h`, `cpu/divu.h` | **Dependency/API contract.** Confirms DIVU register addresses and Q16 split semantics for the close-port; no Yaul source is copied. |

| Upstream | Pin / license | Inspected code | Reuse mode and concrete destination |
|---|---|---|---|
| [VGKintsugi/Ghidra-SegaSaturn-Loader](https://github.com/VGKintsugi/Ghidra-SegaSaturn-Loader) | `c489a190a79d2634b9ecf82e2c0dcec8fd999cf5` / Apache-2.0 | `README.md` and loader layout | **External tool / behavior study.** Use it, if needed, to inspect a generated ISO or emulator save state during M8 debugging. It is not linked to the build and no loader code belongs in the target. |
| `Project12x/Ymir` | `6efc5324943c27f4db7a1b7c8bcf90f51459b12e` / GPL-3.0 | headless JSON-RPC, frame capture, debug-break areas recorded in `PROVENANCE.md` | **External process.** Continue deterministic controller pulses, hashes, and PNG captures. No emulator source or headers enter `sm64-port`. |

## Explicit non-adoptions

- R11's SGL-specific target layer is not a second hardware abstraction; the
  project is Yaul-based.
- Pyrite64's C++/desktop renderer is not an import path for the SH-2 target.
- Tris-Quads-Ex is neither a Blender build dependency nor sufficient geometry
  validation for VDP1.
- The Ghidra loader is a developer-side inspection tool, not a runtime asset
  pipeline.
- GPL engine code remains isolated and attributed under the existing
  `src/port/saturn/gpl/` and `SLAVEDRIVER_ADAPTATION.md` rules; this ledger does
  not expand that adaptation boundary.

## Per-change checklist

Before implementing a row, add the exact source paths used to the commit note
and update `PROVENANCE.md` if the reuse mode changes. Before accepting it,
record a reproducible host test or Ymir capture in `evidence/` and link the
result from the visual timeline.

## PCM68K protocol foundation — 2026-08-04

| Upstream | Pin / license | Inspected code | Reuse mode and concrete destination |
| --- | --- | --- | --- |
| `ponut64/SCSP_poneSound` | `31782e4c61337327f23eb9aa45ecd37fe0944ea0` / MIT | `LICENSE`, `README.md`, `documentation.md`, `PROJ/main.c`, `PROJ/linker`, `PROJ/makefile`, `jo_demo/pcmsys.c`, `jo_demo/pcmsys.h` | **Pattern-only.** `src/port/saturn/audio/saturn_pcm_protocol.h` is original byte-addressed protocol code, informed by the reference's fixed sound-RAM boundary. It copies no code, C struct, driver, wrapper, ADX/CDDA path, or binary. The retained MIT notice and exact change boundary are in `docs/saturn/audio/PCM68K_PROVENANCE.md`. |

## M4 — input/cache probe additions (2026-07-19)

| Upstream | Pin / license | Inspected code | Reuse mode and concrete destination |
|---|---|---|---|
| `yaul-org/libyaul` | `6012f79f237773378c8014e70d8998ad95a38d98` / MIT | `libyaul/scu/bus/cpu/smpc/smpc_peripherals.c` (`pressed.raw` XOR normalization) and pinned public input examples | **Dependency / API use.** The controller boundary treats Yaul's normalized `pressed.raw` and digital direction bits as active-high, matching the upstream decoder and examples. The existing `OSContPad` mapping remains original port code; this source is cited because polarity at this boundary directly affects idle drift and movement determinism. |

The stable frame probe in `src/port/saturn/platform/saturn_frame_sample.h` is
new project code. It is deliberately not attributed to an upstream engine: its
sequence-guarded, all-16-bit layout exists to make the Yaul/Ymir observation
boundary deterministic.

## M5 — sourceboot gGfxPool collision investigation (2026-07-22)

| Upstream | Pin / license | Inspected code | Reuse mode and concrete destination |
|---|---|---|---|
| [Maxime-XL2/SONIC-Z-TREME](https://github.com/Maxime-XL2/SONIC-Z-TREME) | `cff75451c1616aac1236fc2b44223902b55c706b` / GPL-3.0 | `Projects/SONIC Z-TREME/ZTE/workarea.c` (the full file — `enum workarea`'s `AdjWork` chain, and the const pointer/size exports below it) | **Pattern study, informing diagnosis, not yet a code destination.** Every work-area region (`sort_list`, `zbuffer`, `spritebuf`, `pbuffer`, `clofstbuf`, `commandbuf`) is a fixed, compile-time, non-overlapping slot sized from a `MAX_POLYGONS`/`MAX_VERTICES`-class constant via a cumulative-offset macro — never two pointers growing toward each other at runtime the way SM64's `alloc_display_list`/`gGfxPool` bump allocator does. Concrete evidence that a real, shipped SGL engine architects this class of per-frame buffer with a structurally different, collision-incapable-by-construction discipline. Directly informs `docs/saturn/evidence/e2-sourceboot-bad-mtx-pointer-2026-07-22.md`'s characterization of the bug class; no code copied yet. |
| [Lobotomy-Software/SlaveDriver-Engine](https://github.com/Lobotomy-Software/SlaveDriver-Engine) | `a8986591557b6e680550d3c23970284d3b38ff8f` / GPL-3.0 | `WALLS.C` lines ~1240–1300 (`MAXNMSLAVEPOLYS`, `slaveResult`/`nmSlavePolys`, `slave_drawRectWall`'s `if (height*width+nmSlavePolys+50>MAXNMSLAVEPOLYS) return;` guard) | **Pattern study, informing diagnosis, not yet a code destination.** A real, shipped retail Saturn title's own slave-side polygon buffer is a single fixed-capacity array (`MAXNMSLAVEPOLYS=1300`) indexed by a monotonic counter, with an explicit bounds check *before* every write that drops the data (not out-of-bounds writes, not a converging second pointer) when the buffer would overflow. Same conclusion as the Z-Treme row: real Saturn engines avoid the two-ends-bump-allocator pattern entirely rather than relying on head/tail never crossing. Already-cited `DMA.C`/`DMA.H`/`SCL_FUNC.C` close-port (`SLAVEDRIVER_ADAPTATION.md`) is unchanged by this row. |

Both rows are consulted for *characterization* of the bug class (is this an
inherent fragility of the ported N64 allocator pattern, or a bug this port
introduced) per `docs/saturn/ENGINE_PORT_ARCHITECTURE.md`'s engine-ownership
boundary — `gGfxPool`/`alloc_display_list`/`select_gfx_pool` are unmodified
SM64 engine code and are not candidates for adopting either engine's opposite
allocation discipline. If a fix ships from this investigation, it will be
scoped to this port's own platform layer, and this row will be updated with
the concrete destination file at that point.
