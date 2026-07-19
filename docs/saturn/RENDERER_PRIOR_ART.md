# Saturn renderer prior art

This note turns three source-level Saturn engine studies into concrete choices
for the SM64 port. Exact commits, licenses, and inspected paths are recorded in
[`PROVENANCE.md`](PROVENANCE.md).

## What each engine proves

| Reference | Strongest evidence | Use now | Use later |
|---|---|---|---|
| SlaveDriver | A commercial-era engine can combine sector visibility, fixed-point clipping, interpolated shade values, queued transfers, and master/slave render records. | Keep its bounded DMA behavior isolated; do not import more renderer code. | Use sector-local object lists and shade-preserving clip behavior as test oracles for the world renderer. |
| Jo Engine | A small C command pipeline can keep VDP1 setup commands explicit and flush bounded command blocks by DMA. | Retain libyaul's typed persistent command list; it already gives the useful lifecycle without another allocator. | Compare block/arena command allocation if variable scene command counts make a fixed list wasteful. |
| Sonic Z-Treme | Stable per-polygon Gouraud slots plus conditional VBlank table copies make realtime lighting optional and measurable; frustum/octree culling and contiguous model arenas are viable on Saturn. | Keep the intro face's cached vertex lighting, persistent Gouraud slots, A-button shine toggle, and update-only-on-change upload. | Benchmark contiguous level banks, coarse visibility, early slave-SH2 submission, and Gouraud quality tiers. |

## Decision for the intro face

The current shine path is already the Saturn-appropriate one:

1. Mesh topology and VDP1 command storage remain persistent.
2. Diffuse and shine terms are computed per source vertex and cached.
3. Each source face owns a stable four-color VDP1 Gouraud table.
4. Pressing A changes the lighting mode and rebuilds/uploads the tables once.
5. Camera motion only transforms, projects, and sorts geometry; it does not
   recompute the object-space highlight.
6. The HUD reports frame ticks and the isolated shade-rebuild cost so the
   quality/performance decision remains visible.

The first interactive proof reset 1,049 surface indices to source order every
frame and recomputed three transformed vertices inside each insertion-sort
comparison. The optimized path caches one depth per surface and retains the
previous frame's nearly sorted order. This is a local data-lifecycle fix rather
than copied engine code: the studied GPL engines are behavior references only,
and their SGL/sector sort structures do not match this libyaul face renderer.
The same pass caches each of the 440 transformed/projected face vertices once
per frame; triangle depth and command emission then index those results rather
than transforming shared vertices repeatedly.

This avoids texture upload, framebuffer blending, per-pixel simulation, and a
full Gouraud rebuild every frame. Sonic Z-Treme's source and README provide a
useful independent comparison point, but no GPL renderer code is used here.

## Quads versus triangles

None of these engines removes VDP1's quadrilateral primitive constraint.
SlaveDriver and Sonic Z-Treme both reinforce the practical answer: feed VDP1
well-bounded four-corner commands and make clipping, ordering, and Gouraud
ownership explicit. For SM64, the converter should therefore classify source
triangles and choose among:

- a repeated final vertex for a standalone triangle;
- a merged quad only when topology, material, winding, and UV constraints agree;
- clipped output with newly interpolated positions and shade values; or
- a deliberately slower fallback for exceptional arbitrary-UV geometry.

The intro face's first implementation intentionally used repeated-vertex
triangles as a topology and lighting proof. The first quad compiler pass now
preserves those 877 source triangles while emitting a separate Saturn render
IR. Pattern-only study of Rulesobeyer's Apache-2.0 converter at commit
`1e1cdb1aaf55bb3e222cd8ecf7233f9065af392c` supplied the candidate-graph and
one-pair-per-triangle model; Blender/PuLP integration was rejected as an
architecture mismatch because it does not enforce Saturn render constraints.

The deterministic host tool accepts only same-material, consistently wound
pairs whose triangle normals align by at least 0.80 and whose ordered boundary
remains strictly convex over 15 yaw/pitch camera samples. NetworkX 3.6.1's
BSD-licensed exact blossom matcher proves that 156 is the maximum-cardinality
selection from the 206 accepted candidates, then maximizes integer-weighted
normal alignment, shared-edge length, and stable source order. It selects 156 true
quads, leaves 565 face triangles as repeated-vertex fallbacks, and reduces the
face from 877 to 721 VDP1 primitives. The generated report records 114 material
rejections, 251 normal-divergence rejections, and 740 projected-convexity
rejections. Textured world geometry will additionally require the existing UV
rectangle/seam rules before it can enter the candidate graph.

## Measurements to add before world geometry

- command count and VDP1 draw-end time with shine on and off;
- CPU ticks for painter sort, transform/command build, Gouraud upload, and
  render wait as separate counters (present on the intro-face HUD; retain them
  when the renderer is generalized);
- fixed-list versus arena/block command storage at representative peaks;
- flat, cached Gouraud, and dynamic Gouraud quality tiers;
- near-plane clipping cases that verify shade interpolation;
- visibility rejection counts before and after coarse spatial grouping; and
- single-SH2 versus early slave-SH2 submission only after the single-CPU path
  is correct and profiled.

## Accepted M4 throughput pass

The Castle viewer now applies one narrow optimization from the same principles:
the source-derived tile is still transformed and projected directly for the
visibility decision and command emission, but its full-width maximum view-depth
key is retained for the subsequent opaque re-bucket. This removes the duplicate
depth transform without quantizing screen coordinates or changing source UVs.
The cache is 830 `int32_t` keys plus validity bytes (about 4 KiB in hot internal
RAM), while the source texture/UV banks remain cartridge-backed build assets and
are uploaded to VDP1 once. A larger four-corner point cache was tested and
rejected because its runtime image was blank; it is preserved as a failure, not
silently shipped.

This is the practical split suggested by the references: Z-Treme-style coarse
visibility and stable per-primitive keys stay in internal RAM, while
SlaveDriver-style queued/DMA staging remains reserved for cold 4 MiB RAM-Cart
asset transfers. No source SM64 geometry, material, or collision data is
replaced by hand-authored Saturn scene data.

## Visual-slice source decisions

The next runtime changes follow the concrete destinations in
[`UPSTREAM_CODE_LEDGER.md`](UPSTREAM_CODE_LEDGER.md), not a generic search for
an engine to import:

- R11's MIT VDP2/SMPC C layers provide a **pattern-only** reference for the
  title's separately measured NBG1 backdrop, NBG0 prompt, and input edge; Yaul
  remains the target API.
- SaitoTsutomu's Apache-2.0 triangle-pair optimizer provides a **pattern-only**
  independent matching objective for host regression. The existing NetworkX
  matcher and Saturn-specific safety filters remain authoritative.
- Pyrite64's MIT model/material/collision/animation boundaries provide a
  **pattern-only** shape for the M2/M3 Fast3D-to-Saturn IR, not a C++ renderer
  import.
- VGKintsugi's Apache-2.0 Ghidra Saturn loader is an optional external
  inspection aid for later ISO/save-state debugging, never target code.

## Explicit non-adoptions

- Jo Engine's command allocator is not copied because the port is already based
  on libyaul and has a persistent typed command list.
- Sonic Z-Treme's SGL data structures, octree/PVS code, and lighting functions
  are not copied because they are GPL, SGL-specific, and described upstream as
  experimental or inefficient in places.
- SlaveDriver's world renderer is not copied because its sector assumptions do
  not match SM64's scene graph and dynamic object model. Its clipping and work
  partitioning remain behavioral references.
