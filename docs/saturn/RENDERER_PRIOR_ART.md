# Saturn renderer prior art

This note turns three source-level Saturn engine studies into concrete choices
for the SM64 port. Exact commits, licenses, and inspected paths are recorded in
[`PROVENANCE.md`](PROVENANCE.md).

## What each engine proves

| Reference | Strongest evidence | Use now | Use later |
|---|---|---|---|
| SlaveDriver | A commercial-era engine can combine sector visibility, fixed-point clipping, interpolated shade values, queued transfers, and master/slave render records. | Keep the attributed bounded-DMA close-port and adapt additional routines only where their data contracts fit SM64. | Directly adapt sector dependency records, local object lists, clipping, or master/slave work partitioning when profiling justifies them. |
| Jo Engine | A small C command pipeline can keep VDP1 setup commands explicit and flush bounded command blocks by DMA. | Retain libyaul's typed persistent command list; it already gives the useful lifecycle without another allocator. | Compare block/arena command allocation if variable scene command counts make a fixed list wasteful. |
| Sonic Z-Treme | Stable per-polygon Gouraud slots plus conditional VBlank table copies make realtime lighting optional and measurable; frustum/octree culling and contiguous model arenas are viable on Saturn. | Keep the intro face's cached vertex lighting, persistent Gouraud slots, A-button shine toggle, and update-only-on-change upload. | Benchmark contiguous level banks, coarse visibility, early slave-SH2 submission, and Gouraud quality tiers. |

## PS1 port: architecture lesson, not renderer source

The inspected PS1 port is not a Saturn code donor: it lacks one
repository-wide reuse license and its GTE, ordering-table, packet, and VRAM
contracts do not match Yaul or VDP1. It does establish the right *shape* for
the next Saturn work: preserve the original game/level loop, preprocess source
display work into a compact target IR, prepare texture and animation residency
by area, and measure the whole frame rather than only draw submission.

For Saturn, that means per-area VDP1 command templates and 4 MiB cartridge
manifests, with each frame patching only source-selected dynamic state. It does
not mean a generic PS1 ordering table, per-texture load during rendering, or
an 8 MiB benchmark assumption. The full decision and source-path list is in
[`PSX_PORT_ARCHITECTURE_LESSONS.md`](PSX_PORT_ARCHITECTURE_LESSONS.md).

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
than copied engine code: this specific pass predates direct GPL renderer reuse,
and the inspected SGL/sector sort structures do not match this libyaul face
renderer. The GPL-compatible project may nevertheless copy or close-port other
suitable routines with pinned provenance and full GPL compliance.
The same pass caches each of the 440 transformed/projected face vertices once
per frame; triangle depth and command emission then index those results rather
than transforming shared vertices repeatedly.

This avoids texture upload, framebuffer blending, per-pixel simulation, and a
full Gouraud rebuild every frame. Sonic Z-Treme's source and README provide a
useful independent comparison point; this particular intro-face optimization
does not yet contain code copied from its renderer.

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

The same constraint now governs the animated actor path. A second attempt to
hold all 424 Mario view-space and projected vertices in new static arrays also
produced a deterministic black frame and was removed. The accepted zero-growth
pass instead selects the active original SM64 animation bank once per frame and
reuses a single pointer through culling, ordering, Gouraud, and command build.
This retains the Z-Treme-style stable-data lifecycle without expanding the hot
internal-WRAM footprint; the accepted framebuffer hash is unchanged.

The Castle command list now follows the same persistent lifecycle. Its full
1,627-slot arena is zeroed only at startup; subsequent frames clear the old END
bit, rewrite the live commands, and ask Yaul to upload only the new prefix. A
linked-symbol Ymir probe measured 1,105 live slots in the accepted view, cutting
the per-frame command transfer from 52,064 to 35,360 bytes (32%) without a
framebuffer change. This is a direct use of Yaul's typed persistent list and a
Jo-style bounded command-block pattern; no Jo Engine allocator code is copied.

This is the practical split suggested by the references: Z-Treme-style coarse
visibility and stable per-primitive keys stay in internal RAM, while
SlaveDriver-style queued/DMA staging remains reserved for cold 4 MiB RAM-Cart
asset transfers. No source SM64 geometry, material, or collision data is
replaced by hand-authored Saturn scene data.

The first non-wrapping Castle phase probe changes the priority. Yaul's 16-bit
FRT at `/8` wrapped every ~19.5 ms, so it could not measure the observed slow
frame. The viewer now uses `/128` (about 312 ms of range). A deterministic
Ymir sample reports roughly 2.3 ms update, 101 ms visibility/painter sorting,
63 ms command construction/upload, 10 ms VDP wait, and 17 ms VBlank. The
master-SH2 software path—not VDP1 draw completion—is therefore the first-order
bottleneck. Of 882 visible items, 638 are Mario; his 50 textured source
triangles expand to 200 commands. The next accepted work is consequently:

1. transform Mario vertices once into bounded frame records and reuse them;
2. lower textured actor triangles without the current four-command expansion;
3. compile source-derived full/medium/far actor LOD banks for the RAM Cart; and
4. only then prototype Z-Treme-style early slave-SH2 transform jobs.

The first transform-once actor pass is now accepted. Moving the 256 KiB source
collision allocator from high WRAM to the upper quarter of low WRAM freed the
hot-memory headroom that the earlier cache lacked. A 424-entry view/projection
cache then reduced the measured sort phase from roughly 101 to 73 ms and
command construction from 63 to 25 ms. The sampled full loop improved from
about 4.6 to 6.6 FPS while the original animation vertices, collision stream,
materials, and VDP1 commands remained active. This is the new optimization
baseline; it is progress toward, not satisfaction of, the 15 FPS gate.

Actor texture lowering is now an explicit quality tier. The default maps each
of the 50 original textured Mario triangles to one complete 16x16 repeated-C
VDP1 sprite; the previous four-way affine split remains selectable offline.
In the captured view, 43 visible textured triangles remove 129 live commands
(1,032 to 903), texture residency falls from 100 KiB to 25 KiB, command build
drops from about 25 to 20 ms, and the sampled loop rises from 6.6 to 7.5 FPS.
No source triangle, UV, animation, or material is replaced.

Source `G_CULL_BACK`/`G_CULL_FRONT` state is now preserved by the Castle IR.
The accepted per-fragment Fast3D winding test removes 148 tiles and lowers the
captured live VDP1 prefix from 1,105 to 1,032 commands without changing the
recognized scene. Attempts to cache culling at unsplit-source or whole-
primitive granularity produced black frames because exact BSP fragments do
not share a reliable winding key; those failures remain in the gallery.

The concrete placement and staging rules for that split live in
[`CARTRIDGE_ASSET_POLICY.md`](CARTRIDGE_ASSET_POLICY.md). Any future move of
Castle BSP, texture banks, animation data, or LOD blocks into the 4 MiB
expansion must preserve its internal-WRAM hot-cache and measured-DMA gates.

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
  are not copied wholesale because their SGL contracts do not fit Yaul and
  portions are described upstream as experimental or inefficient. Stable,
  relevant routines may be directly copied or close-ported under GPL terms.
- SlaveDriver's world renderer is not copied because its sector assumptions do
  not match SM64's scene graph and dynamic object model wholesale. Its clipping,
  dependency ordering, and work partitioning are eligible for attributed direct
  adaptation when their contracts are integrated into the shared renderer.

## Stable M4 measurement boundary

The Castle viewer now publishes a separate sequence-guarded frame record after
the VBlank wait in `src/port/saturn/platform/saturn_frame_sample.h`. This keeps
the phase budget from being read while the legacy rolling HUD counters are
still being updated. A 900-frame USA-BIOS Ymir run decodes as 2,296 update,
5,041 sort, 569 command, 2,634 wait, and 3,495 VBlank FRT ticks: 10,540 render
ticks, 14,035 loop ticks, or 14.93 loop FPS using the existing `/128` clock.
The result is emulator software-VDP evidence, not a retail timing claim.

The same capture path proves the persistent command-lowering cache is
pixel-identical at the neutral boundary: the pre-cache and post-cache PNG
SHA-256 is `3d2d56d9ff48fa2a955d40e5474b3b01bed7210639e117e08be022bf534f71d6`.
The cache skips CPU command reconstruction while still re-arming and
uploading the VDP1 list each frame, which preserves Saturn submission
semantics. Yaul's normalized SMPC polarity is documented separately in the
upstream ledger; correcting it removes the all-buttons/idle-drift failure
before further movement profiling.
