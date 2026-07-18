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

The intro face intentionally uses repeated-vertex triangles. It is a topology
and lighting proof, not evidence that all SM64 geometry should remain one VDP1
command per source triangle.

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

## Explicit non-adoptions

- Jo Engine's command allocator is not copied because the port is already based
  on libyaul and has a persistent typed command list.
- Sonic Z-Treme's SGL data structures, octree/PVS code, and lighting functions
  are not copied because they are GPL, SGL-specific, and described upstream as
  experimental or inefficient in places.
- SlaveDriver's world renderer is not copied because its sector assumptions do
  not match SM64's scene graph and dynamic object model. Its clipping and work
  partitioning remain behavioral references.
