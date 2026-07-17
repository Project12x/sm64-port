# Super Mario 64 on Sega Saturn + 4 MiB DRAM Cartridge

## Feasibility thesis and gated port plan

Prepared 2026-07-16. This document is the working technical plan for the
Saturn target. It should be updated when measurements replace assumptions.

## Executive conclusion

A Saturn port is technically plausible, and making the 4 MiB expansion DRAM
cartridge mandatory substantially improves the memory story. It is not a
simple replacement of N64 triangles with Saturn quadrilaterals.

The feasibility problems are:

1. VDP1's rectangular texture-source model versus SM64's arbitrary per-vertex
   UV coordinates.
2. CPU-side transformation, near clipping, lighting, and painter sorting on
   SH-2 without a PS1-style GTE or a Z-buffer.
3. Keeping the hot working set in 2 MiB of internal work RAM while using the
   cartridge as slower, mostly read-only bulk memory.
4. Holding a stable 30 Hz update and render cadence on retail hardware.

The project will therefore be asset-compiler-first and gate-driven. Before a
broad game port, it will measure the cartridge and VDP1, classify representative
SM64 assets offline, and prove one representative gameplay scene.

The Saturn directory in `malucard/sm64-psx` is evidence of an intended target,
not a partial Saturn port: its CRT file is empty, its graphics backend still
uses PS1 GPU/GTE/SPU interfaces, and its Makefile selects a missing
`Makefile.ss.mk`.

## Evidence baseline

The exact revisions, files inspected, permission status, and reuse modes are
recorded in [PROVENANCE.md](PROVENANCE.md). No third-party implementation code
is introduced by this planning baseline.

### Quads versus triangles

VDP1 can render a triangle by repeating two adjacent vertices of a polygon
command. Triangle geometry by itself is not the blocker.

A source scan at the pinned project revision found:

- 3,794 static `gsSP1Triangle` calls;
- 29,117 static `gsSP2Triangles` calls;
- an estimated 62,028 statically declared triangles across game data;
- no `gsSP1Quadrangle` calls;
- 13,993 of the 29,117 two-triangle calls (48.1%) with a reversed shared edge
  and four unique indices, making them topological quad candidates.

That candidate rate is only an upper bound. A safe merge also requires
compatible winding, convexity, planarity/twist, material state, and UV layout.

The harder problem is texture representation. An N64-style triangle can assign
arbitrary UV coordinates to its vertices. A VDP1 textured distorted sprite
selects a tightly packed rectangular character pattern and maps its four fixed
corners to four screen vertices. Texture width must also be a multiple of eight
pixels. Consequently:

- a triangle pair is a direct textured VDP1 quad only when its UV domain is a
  compatible rectangle;
- a geometric triangle is easy, but an arbitrary textured triangle generally
  needs cropping, rebasing, padding, baking, or splitting;
- general texture atlases are limited because VDP1 does not expose an arbitrary
  source row stride;
- UV wrap must be resolved offline into compatible pieces; and
- untextured triangles are substantially easier.

VDP1 line-based fill and double-write behavior also affect shared edges,
transparency, shadows, particles, water, and color calculation. These effects
must be tested explicitly rather than inferred from emulator screenshots.

### The 4 MiB cartridge

The cartridge is mandatory, but it is not equivalent to 4 MiB of fast internal
work RAM. Sega Technical Bulletin 47 documents cartridge ID `0x5C`, 4 MiB of
mapped DRAM, ordinary CPU accesses, DMA reads, and an important asymmetry:
SCU DMA may read from the cartridge but may not write it. SH-2 DMA can be used
for cartridge writes.

The design rule is:

> Keep cold or read-mostly converted assets in cartridge DRAM. Keep stacks,
> mutable state, hot collision data, transformed vertices, sort buckets,
> command staging, and frame-critical queues in internal work RAM. Stage
> sequential asset blocks into bounded internal rings before processing.

CD data bound for cartridge DRAM should pass through a CD/internal-WRAM staging
buffer and then use a measured SH-2 DMA or CPU copy. The implementation must not
assume that SCU DMA can write the cartridge.

## Initial product definition

- Retail Sega Saturn with a mandatory detected 4 MiB DRAM cartridge.
- A disc image generated from a user-supplied US SM64 baserom; Nintendo assets
  are neither committed nor distributed.
- 320x224 progressive output first; 320x240 and PAL modes come after budgets
  are proven.
- Original 30 Hz gameplay update, targeting one rendered frame per update.
- Saturn 3D Control Pad analog input preferred, with a digital-pad fallback.
- Internal backup RAM for saves.
- Original gameplay and level layout prioritized over exact N64 rendering.

Acceptable first-release substitutions include VDP2 backgrounds, baked or
cropped per-surface textures, mesh/dither or opaque effect fallbacks,
conservative culling/LOD, and a deliberately limited early audio path.

The first playable build does not target 60 fps, stock 2 MiB operation,
bit-exact N64 output, or speculative use of the SCU DSP.

## Architecture

### Toolchain and source layout

Use libyaul 0.3.1 at the pinned commit as an external dependency. Preserve its
MIT license and attribution. Add a real `Makefile.saturn.mk`; do not extend the
non-building PSX placeholder. Retain the PC target as a fast reference and
diagnostic build.

Planned additions:

```text
src/port/saturn/          Runtime integration
src/port/saturn/gfx/      VDP1/VDP2 renderer, clipping, sorting, texture cache
src/port/saturn/audio/    SCSP/68K transport and game-audio adapter
src/port/saturn/io/       Controller, CD, save, and DRAM-cartridge support
tools/saturn/             Asset, level-bank, and disc builders
include/saturn/           Target-neutral Saturn IR and generated metadata
docs/saturn/              Budgets, provenance, and hardware measurements
```

Do not apply a blanket license to inherited code the project does not own.

### Asset compiler

The asset compiler is a renderer component, not a late optimization. It should
convert extracted display lists into a compact Saturn IR while retaining the
geo-layout system's ability to select and animate display lists. The runtime
should not interpret a full N64 RSP command stream every frame.

Every primitive is assigned one of six representations:

1. **Direct textured quad**: two compatible triangles with a rectangular UV
   domain and a safe four-vertex shape.
2. **Direct untextured quad/triangle**: a VDP1 polygon command, repeating a
   vertex where needed.
3. **Textured degenerate triangle**: a standardized rectangular texture block
   with a transparent or cropped unused region.
4. **Split/cropped surface**: wrapping or atlas regions resolved into tightly
   packed blocks.
5. **Baked surface**: arbitrary/rotated UV mapping converted to a face-specific
   block.
6. **Effect fallback**: an explicit VDP2 plane, Gouraud polygon, mesh effect,
   sprite, or specialized implementation.

The converter must report, by level and actor:

- source triangles and generated VDP1 commands;
- direct-quad rate and every rejected-merge reason;
- texture bytes before and after crop, padding, baking, quantization, and
  deduplication;
- peak resident VDP1 texture set;
- cartridge-bank size; and
- representative transform and command counts.

Required early audit subjects are Mario, castle interior, Bob-omb Battlefield,
a Bowser stage, water, shadows, particles, and a transparency-heavy scene.

### Renderer

Build a custom SM64-aware renderer using libyaul hardware APIs. Do not adopt
libmic3d as the whole renderer: its polygon representation is quad-oriented and
the inspected pipeline culls near-plane intersections instead of clipping them.
Its fixed-point and hardware-integration patterns remain useful MIT-licensed
prior art.

Pipeline:

1. Traverse the geo graph on the master SH-2 and emit coarse mesh jobs.
2. Stage sequential cartridge blocks into an internal-WRAM ring.
3. Transform, light, backface-cull, and near-clip in fixed point.
4. Place opaque primitives into coarse depth buckets with stable
   object/material ordering.
5. Submit decals and translucent effects in explicit later passes.
6. Build VDP1 command tables in internal RAM and transfer them to the selected
   VDP1 VRAM command area.
7. Maintain a scene-aware texture-block cache with bounded uploads.
8. Use VDP2 for the back screen, selected backgrounds, fades, and possibly HUD
   or text layers.

Near-plane clipping is mandatory. A third-person camera regularly intersects
walls, Mario, and foreground objects. Start with fixed-point
Sutherland-Hodgman clipping against the near plane, then normalize its output
to VDP1-compatible triangles or quads.

### CPU split

Start single-SH2 for correctness. Add the slave SH-2 only after profiling
identifies a stable coarse-grained job.

- **Master SH-2:** gameplay, object behavior, collision, geo traversal,
  render-job construction, final bucket linkage, and I/O.
- **Slave SH-2 candidate:** batches of transform, lighting, backface tests, and
  clip preparation using private buffers.
- **DMA/SCU:** measured bulk transfers permitted by each bus destination.
- **VDP1:** rasterization while CPUs prepare later work where synchronization
  permits.

Both SH-2s contend for important buses. Treat the slave as a measured worker,
not an assumed 2x multiplier.

### Math and behavioral equivalence

Use fixed point for renderer matrices/vertices, lighting, projection, and later
for measured hot gameplay helpers. Software float may remain temporarily in
cold gameplay paths. Instrument soft-float helper calls and remove expensive
ones iteratively.

Keep a PC reference mode able to compare float and fixed-point state over
recorded input traces. The PSX fork demonstrates useful categories of work, but
its implementation must not be copied without explicit permission.

### Audio

Audio is a separate high-risk stream. Initial milestones are silence, then one
PCM sound, then basic PCM effects with pre-rendered or streamed music for the
vertical slice. By the end of that slice the project must choose either a
properly licensed SCSP/68K sequence driver or an explicit rendered-audio design.

## Provisional memory envelopes

These are design envelopes, not measured allocations.

### Internal work RAM: 2 MiB

| Use | Initial envelope |
|---|---:|
| Hot code/rodata, stacks, SDK, interrupts | 512-640 KiB |
| Mutable gameplay, objects, collision, level heap | 576-704 KiB |
| Transform/clip buffers, buckets, command staging | 320-448 KiB |
| CD/cartridge staging rings | 128-256 KiB |
| Audio command and sequence state | 64-128 KiB |
| Safety margin | at least 128 KiB |

These ranges deliberately expose overcommit. Link maps and peak telemetry must
produce a real allocation. Prefer overlays over moving arbitrary mutable state
into slow cartridge memory.

### Cartridge DRAM: 4 MiB

Use for current-level converted geometry, collision and metadata; global and
current actors/animations; converted texture blocks awaiting upload; compact
display-list IR; and a bounded explicit asset cache. Avoid per-frame pointer
chasing through the cartridge.

### VDP1 VRAM: 512 KiB

Begin characterization around 96-128 KiB for 3,000-4,000 32-byte command
entries, 16-32 KiB for Gouraud tables/CLUTs/alignment, and roughly 352-400 KiB
for texture blocks. Generate the final partition from scene peaks: command
growth directly reduces texture residency.

## Gated roadmap

### Phase 0: provenance and reproducible bring-up

Status as of 2026-07-17: the provenance baseline, libyaul 0.3.1 submodule pin,
isolated hello-disc source, and Saturn Makefile are present. A source-built
GCC 14.3.0/binutils 2.44 SH-2 toolchain and the pinned libyaul produced a CUE
and ISO from a clean target build; the executable header and artifact hashes
are recorded in `evidence/hello-disc-2026-07-16.md`. Yabause 0.9.15 also
reached the expected hello screen for 600 frames using its explicitly
lower-confidence HLE BIOS. A GPL-contained Project12x Ymir fork now provides
JSON-RPC stepping, memory/register inspection, continuous execution, pause,
bounded frame runs, deterministic stopped events, canonical frame hashes, and
base64 PNG capture; it remains an external tool and has not yet supplied the
required BIOS-backed hello-disc evidence. A portable one-command toolchain
bootstrap wrapper is now present but has not yet been executed in this
environment; a second BIOS-backed emulator run and retail-hardware execution
also remain open.

Deliver:

- explicit license or written permission for any PSX-specific code reuse;
- pinned dependencies and a maintained provenance/reuse ledger;
- preserved libyaul MIT terms;
- a reproducible build producing a bootable `.cue`/`.bin`; and
- a hello-screen disc verified in two emulators and retail hardware.

Gate: a clean checkout builds with one documented command, and no unlicensed
PSX or examples code has been copied.

### Phase 1: hardware-characterization disc

Deliver:

- detect cartridge ID `0x5C` and reject unsupported configurations visibly;
- destructively test all 4 MiB before allocator use;
- measure cached/uncached CPU access, SH-2 DMA, legal SCU DMA reads,
  CD-to-WRAM-to-cartridge copies, and WRAM-to-VDP1 uploads;
- draw solid/textured quads, repeated-vertex triangles, Gouraud shading,
  transparency modes, and concave/twisted problem cases; and
- record draw-end time, command count, and an approximate pixel count.

Gate: retail-hardware results replace the provisional transfer and frame
assumptions.

### Phase 2: asset audit and PC reference renderer

Deliver the six-way classifier, texture conversion and deduplication, reports
for representative assets, and a PC renderer for the Saturn IR.

Gate:

- representative texture residency fits the measured VDP1 partition;
- worst representative views fit measured command and draw-time budgets;
- current-level banks fit 4 MiB with useful cache/loading headroom; and
- arbitrary-UV fallbacks do not cause uncontrolled per-face texture growth.

If this fails, reduce texture fidelity/LOD or revise the asset strategy before
broad gameplay work.

### Phase 3: single-SH2 Bob-omb Battlefield slice

Deliver Mario, terrain, representative actors, camera, collision, shadows,
HUD, input, minimal audio, correct near clipping, stable ordering, and visible
timing/memory telemetry. Compare recorded PC and Saturn state traces.

Retail-hardware gate: 30 Hz gameplay update, one render per update or an
explicit approved fallback, no memory/cache/command overflow, and no systematic
UV corruption, camera-wall holes, or ordering failures. This is the main
go/no-go point for the full port.

### Phase 4: measured dual-SH2 optimization and castle loop

Move one proven transform/lighting stream to the slave SH-2. Add castle entry,
level transitions, star collection, save/load, controller selection, and asset
bank transitions.

Gate: repeatable castle-to-level-to-castle operation without leaks, stale
pointers, active-play CD stalls, or dual-CPU races.

### Phase 5: content breadth

Port by stress class: simple outdoors; dense interiors; water and translucent
effects; moving/deforming geometry; Bowser/particle-heavy scenes; then menus,
ending, and credits. Every level must pass automated budgets and a retail
hardware route capture.

### Phase 6: compatibility and release engineering

Test official and representative third-party 4 MiB carts, available regional
consoles, and multiple emulators while keeping retail hardware authoritative.
Ship no baserom, extracted assets, Nintendo audio, or prebuilt game image.

## Principal risks

| Risk | Severity | Early proof or mitigation |
|---|---|---|
| Arbitrary UV conversion causes texture explosion | Critical | Phase 2 classifier and byte audit |
| Transform/clip/sort cost misses 30 Hz | Critical | Phase 1 kernels and single-SH2 slice |
| PSX changes cannot legally be reused | Critical schedule | Permission or clean-room behavior spec |
| Painter ordering fails on intersections | High | Buckets plus explicit scene/effect tests |
| Near clipping produces excessive commands | High | Fixed-point clip metrics and camera stress cases |
| VDP1 texture/command partition is too small | High | Joint scene report and measured cache design |
| Cartridge is treated like internal RAM | High | Sequential staging and real-hardware profiling |
| Internal 2 MiB hot set does not fit | High | Map budgets, pools, overlays, compact structures |
| Audio becomes a late rewrite | High | Choose a licensed direction by Phase 3 |
| Dual-SH2 bus contention erases gains | High | Coarse jobs and before/after measurements |
| Transparency produces double-write artifacts | Medium-high | Effect-specific VDP1/VDP2 fallbacks |
| Emulator success hides hardware faults | High | Retail-hardware evidence at every gate |

## Initial issue backlog

1. Obtain explicit reuse terms for original `malucard/sm64-psx` changes.
2. Add a libyaul-pinned hello-disc target. **Done; execution gates remain.**
3. Add mandatory 4 MiB cartridge detection and full-memory test. **Kronos BIOS-backed 0x5C/full-test pass recorded; retail execution gate remains.**
4. Add cartridge, CD, DMA, and VDP1 benchmark telemetry. **Kronos CPU/VDP1 timings and SCU-DMA result recorded; retail timing characterization remains.**
5. Add a VDP1 primitive and transparency torture disc. **Per-probe solid, repeated-vertex, concave, transparency, textured, and Gouraud submissions now run in Kronos; retail torture results remain.**
6. Add a static display-list statistics tool. **Initial geometry/UV classifier now reports triangle/quad counts plus vertex, display-list, texture, tile, combine, render-mode, and geometry-mode macro inventory.**
7. Define the primitive/material/texture Saturn IR.
8. Implement the quad/triangle/UV classifier with rejection reasons. **Shared-vertex UV consistency, eight-pixel extent checks, and conservative six-way representation counts are now enforced; full decoded-asset coverage remains.**
9. Implement the PC Saturn-IR reference renderer.
10. Implement CI4/CI8/RGB1555 conversion, padding, and deduplication.
11. Generate a Bob-omb Battlefield budget report.
12. Implement a single-SH2 opaque renderer with near clipping and buckets.
13. Add 3D Control Pad and digital-pad input.
14. Boot the Bob-omb Battlefield slice with frame/memory telemetry.
15. Choose and document the licensed SCSP audio architecture.
16. Profile and, only if justified, add a slave-SH2 transform worker.

## Effort estimate

This is a new console engine below an existing game, not another desktop
graphics backend. Rough order of magnitude:

- hardware lab and reproducible bring-up: 0.5-1 person-month;
- asset audit and PC reference pipeline: 1-3 person-months;
- real-hardware Bob-omb Battlefield slice: 3-6 person-months total elapsed;
- feature-complete game: 12-24 person-months if PSX work is reusable, more for
  a clean-room rewrite; and
- polished compatibility release: 18-30 person-months total.

Re-estimate after the Phase 3 gate. A solo part-time effort should be expected
to take multiple years.

## Immediate implementation order

After this documentation baseline, produce two evidence streams:

1. a libyaul hardware-characterization disc for the cartridge and VDP1; and
2. a host-side SM64 asset classifier measuring geometry and texture viability.

The hardware bring-up starts first because it creates the reproducible Saturn
build path required by every later target. The asset classifier should begin as
soon as that build skeleton is stable. If either stream fails its gate, revise
the architecture before committing to a broad port.
