# Saturn PS1-Parity Performance and Audio Research Design

**Date:** 2026-08-01  
**Status:** Approved design, pending implementation plan  
**Primary goal:** Recover enough frame time to move the BOB source-loop build
decisively beyond its current 2–3 FPS experience by adopting the architectural
lessons that make the PS1 port compact, while preserving Saturn-native VDP1,
VDP2, cartridge, and SH-2 constraints.

## Direction

The fixed-point camera remains part of the production direction, but it is not
treated as the sole performance solution. The sprint attacks the complete hot
frame path: immutable command construction, graph matrices, intermediate
records, culling, ordering, and remaining wrong-shaped SH-2 math.

Sound is a research-and-prototype lane. It receives a small bounded share of
capacity and cannot block, enlarge, or perturb the FPS-critical runtime until
its standalone transport is proven.

BOB is the deterministic proving ground, not the architecture boundary. All
production paths remain scene-neutral and suitable for the full game.

## Capacity allocation

- **70% renderer architecture:** compile-once VDP1 state, flat-color fast
  paths, compact visible records, fused passes, early rejection, and bounded
  ordering.
- **25% hot graph math:** Q16 graph matrices, removal of hot 64-bit division,
  and fixed held-object transforms.
- **5% audio research/prototype:** a standalone, source-built PCM-only 68K
  service and SH-2 command transport. No music integration in this sprint.

Agents work from bounded task briefs with separate implementation and review
ownership. Independent research and review may run concurrently. Target builds
and emulator captures remain serial because the owner machine is CPU-bound.

## Reference-code decisions

### PS1 renderer

`malucard/sm64-psx` is pinned at
`3073845688ea273da78d539b20c45110d8a868c3`. No repository-wide license is
established, so `src/port/gfx/gfx_rsp_jit.c`,
`src/port/psx/gfx_dl_exec_psx.c`, and related files are behavior/pattern study
only. No source is copied or closely ported.

The adopted lessons are:

- compile immutable display/material state once;
- fuse transform, rejection, lighting, packet construction, and depth
  classification where Saturn ownership permits;
- reject before expensive material work; and
- use bounded bucket insertion rather than comparison-sorting the whole frame.

The PS1 GTE, ordering-table packet format, GPU texture model, and exact code are
not adopted. Saturn retains its generated IR, VDP1 commands, explicit BSP
handling, near clipping, and cartridge-residency architecture.

### Target-native math

- In-tree SM64 source remains the behavioral oracle.
- Pinned libyaul `6012f79f237773378c8014e70d8998ad95a38d98`
  (MIT) remains the first dependency/API source for SH-2 fixed multiply, DIVU,
  trig, square root, vectors, and look-at.
- Existing attributed Jo Engine fixed multiply remains available under its
  recorded MIT/BSD-style notices.
- Sonic Z-Treme and SlaveDriver remain GPL-compatible pattern sources for
  fixed-point response, compact state, and SH-2 scheduling; no Sonic-specific
  camera or sector renderer is adopted.

### Audio

The recommended prototype reference is `ponut64/SCSP_poneSound` at
`31782e4c61337327f23eb9aa45ecd37fe0944ea0` (MIT). Permitted reuse mode is a
documented close port/direct adaptation of its 68K vector/linker layout,
SNDOFF/SNDON lifecycle, SCSP slot programming, pitch calculation, and PCM
metadata. MIT attribution, license text, source paths, and change notices must
be preserved. Its prebuilt `sdrv.bin`, ADX/CDDA machinery, broad control set,
and example wrapper are not adopted.

Jo Engine and Sonic Z-Treme audio are pattern-only because their paths depend
on proprietary Sega drivers/APIs. SlaveDriver is pattern-only because it
disables the 68K and drives SCSP from SH-2, which conflicts with the selected
CPU-ownership model.

## Optimization waves

### Wave 1 — Compile-once VDP1 state

Generate an immutable command template for every static BOB terrain primitive.
Templates contain stable VDP1 command mode, texture source, color/material,
linkage metadata, and other source-derived state. Per-frame work patches only
screen coordinates and fields proven dynamic.

The first narrow fast path detects equal four-corner colors and emits flat
RGB1555/REPLACE commands without allocating, filling, or uploading a Gouraud
table. True gradients retain the existing Gouraud path.

Wave 1 may not change visibility, clipping, BSP traversal, painter order,
texture selection, or primitive topology.

### Wave 2 — Native graph matrices

Add Saturn target branches that construct look-at, rotation, perspective, and
orthographic matrices directly in the existing Q16 matrix stack. Eliminate the
legacy `guLookAtReflectF`, `guRotateF`, `guPerspectiveF`, `guOrthoF`, and
`guNormalize` closures from the hot Saturn graph path.

Replace Q16 normalization's generic `__divdi3` with a bounded SH-2 DIVU or
reviewed reciprocal implementation. Preserve zero-vector behavior, signs,
range, saturation, perspective normalization, lighting compatibility, and PC
reference behavior.

Conversion is per graph-node type. The source graph dispatcher is not replaced
wholesale.

### Wave 3 — Collapse intermediate frame work

Preserve transform-once vertex sharing, but replace large terrain-result
records with minimal visible descriptors or directly patched command records.
Remove repeated material lookup, texture binding, cross products, corner
copies, and large write/read cycles between classification, compaction, merge,
and emission.

Fusion may occur only where master/slave ownership, cache purge boundaries,
and command-arena lifetime remain explicit. No shared mutable buffer may be
introduced without a bounded producer/consumer contract.

### Wave 4 — Ordering and rejection

Bake source Fast3D cull policy and winding into generated metadata. Perform one
signed projected-area rejection during classification and do not recompute it
during emission.

Replace the full per-frame stable merge sort with either stable fixed depth
bins or a bounded fixed-pass radix scheme. The choice is made from host
ordering comparisons before target integration. BSP handling remains available
for crossing terrain; a PS1 ordering table is not copied mechanically.

## Hot-math scope

The current observation reports 599 statically reachable helper sites across
229 functions. Static reachability is not dynamic cost, so the sprint does not
blindly convert all 599.

Priority order:

1. the 166 legacy graph/view matrix helper sites;
2. repeated graph-node branches and their 64-bit divisions;
3. Q16 normalization's hidden `__divdi3` calls;
4. the held-object transform bridge;
5. shadow/floor geometry only when counters show it is hot; and
6. analog-stick math when manual camera-relative control returns.

Dialog, pause, HUD, transition, and cold gameplay helpers remain legal until a
route proves them hot. The production requirement is zero soft-float, libm,
and generic 64-bit division in every accepted hot closure—not zero float in the
entire source tree.

## Audio prototype

Audio begins as a separate `soundtest` target with a freestanding fixed-address
MC68000 image built by the configured `m68keb-elf` toolchain. The initial
driver supports four round-robin PCM voices and three generated or
public-domain mono 8-bit samples.

The SH-2 writes a 32-entry × 16-byte command ring in sound RAM through the
cache-through mapping. Commands and indices use aligned 16-bit big-endian
fields and sound-RAM offsets rather than C pointers. The writer publishes the
producer index last; the 68K publishes the consumer index last.

Initial commands are `PLAY`, `STOP_ALL`, and `SET_MASTER`. The prototype uses
SCSP playback hardware and performs no mixing or resampling on SH-2. It does
not claim music, positional sound, streaming, or broad SM64 sound-ID coverage.

Memory envelopes:

- driver/vector/stack/BSS: 16 KiB hard cap;
- mailbox/ring/status/metadata: 4 KiB;
- guard/future driver reserve: 12 KiB;
- PCM bank: remaining sound RAM, with the first three-effect proof capped at
  32 KiB.

After standalone proof, sourceboot may add one explicit initialization point
after cartridge loading and before the source game loop. The initial
`play_sound` adapter maps only three stable sound IDs. Unknown effects and all
sequence/music calls increment visible unsupported counters.

## Verification policy

This sprint batches evidence by wave. It does not launch an emulator after
every micro-change.

Each implementation task requires host contracts and linked-code inspection
appropriate to its seam. Each completed wave receives one serial BOB target
build and one meaningful comparison against the pinned predecessor:

- identical route input and final gameplay state;
- visible non-sky terrain and Mario;
- unchanged overflow/fault flags;
- command, Gouraud, transform, sort, build/upload, VDP wait, and total-frame
  counters as applicable; and
- fresh ELF/CUE/source identities.

Emulator timings are comparative evidence only. Manual feel is useful
acceptance evidence but not a substitute for counters. Retail Saturn remains
authoritative for final performance claims.

The audio prototype has independent gates: heartbeat, one audible generated
sample, zero command drops, bounded ring high-water, and transport below 1% of
source-tick time. It cannot be linked into the FPS comparison build until those
gates pass and the owner explicitly advances it.

## Failure and rollback boundaries

- A wave that changes pixels or gameplay state is retained as evidence but is
  not promoted into the next baseline until classified and approved.
- Command-template work must fall back per primitive to the existing emitter,
  never globally.
- Native graph nodes may fall back only in reference/source builds; accepted
  Saturn hot roles may not silently call the float constructor.
- Ordering changes retain the existing stable merge implementation as a
  comparison build until the replacement passes the named BOB views.
- Audio failure cannot stall or modify renderer scheduling.

## Completion criteria

The sprint is complete when all four optimization waves have either landed or
produced an explicit evidence-backed rejection, the accepted hot closure has
no unintended soft-float/libm/64-bit-division edges, and the resulting BOB
manual build is materially faster by obvious feel plus recorded stage
counters.

The audio lane completes when the standalone PCM/68K transport prototype and
its license/provenance record exist. Full SM64 sound effects and music remain a
later implementation sprint.
