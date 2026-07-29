# Saturn Throughput Recovery Sprint

> **Status:** Corrective Pipe 5 image ready; awaiting owner inspection
> **Date:** 2026-07-28
> **Parent:** `2026-07-28-saturn-renderer-pipeline-sprint.md`
> **First deliverable:** a distinct replay-free CUE for owner manual testing

## Outcome

Recover useful Saturn-shaped throughput and terrain coverage before doing
another capture or telemetry pass:

1. transform each needed shared BOB vertex once per frame, not once per
   primitive corner;
2. stop incorrectly discarding near-plane-crossing terrain;
3. use the baked hierarchy for spatial admission, then sort lightweight
   compact-result descriptors without copying the full result stream;
4. build one manual image, obtain owner visual feedback, and only then run the
   serial/dual comparison.

Mario must remain textured, Gouraud-shaded, animated, and master-owned. VDP2
continues to own the sky and future HUD/fade planes. The 68000 remains an
audio/SCSP processor; it is not used as a geometry coprocessor.

## Reference implementation basis

The implementation follows the pinned local sources rather than a summary:

| Reference | Pin / licence | Files and relevant behavior | Reuse |
| --- | --- | --- | --- |
| `Maxime-XL2/SONIC-Z-TREME` | `cff75451c1616aac1236fc2b44223902b55c706b`; GPL-3.0 | `ZT_LOADING.c:299-355` shares one promoted `pntbl` between full/LOD `PDATA`; `ZT_RENDERING.c:204-252` retains per-polygon `SORT_MAX`/`SORT_MIN`/average depth policy; `ZT_RENDERING.c:406-505` performs inherited-frustum traversal and at `:494-503` deliberately visits near nodes first so fixed-buffer exhaustion retains nearby geometry; `ZT_RENDERING.c:719-786` updates NBG0/NBG1 separately from the VDP1 world; `workarea.c:14-20` assigns fixed non-overlapping buffers | Pattern-only architecture adaptation |
| `Lobotomy-Software/SlaveDriver-Engine` | `a8986591557b6e680550d3c23970284d3b38ff8f`; GPL-3.0-or-later | `WALLS.C:1240-1408` calls `normTransform` once for a wall's shared vertices, then indexes those results per face; `WALLS.C:1278-1279,1380-1381` checks a batch against capacity while retaining a 50-record guard margin; `WALLS.C:1803-1950` writes bounded cache-through compact results and sector-end markers; `WALLS.C:1986-2052` sorts visible leaves by distance and cut planes; `WALLS.C:2062-2285` performs one coarse master/slave split, one join, ordered result consumption, and bounded split correction | Pattern-only architecture adaptation |

The current SM64 Saturn port is deliberately GPL-compatible under the dated
owner decision already recorded in the upstream ledger. No source is copied
verbatim in this sprint.

## Hardware ownership

- **Master SH-2:** authoritative SM64 state, Mario, final VDP1 command/Gouraud
  lowering, VDP1 submission, VDP2 state.
- **Slave SH-2:** one coarse same-frame terrain job. It transforms its owned
  shared vertices, crosses one bounded phase fence, then produces compact
  results for its primitive range.
- **VDP1:** textured/Gouraud world and actors.
- **VDP2:** sky, menu/HUD/fades, and other flat or scrolling planes, following
  Z-Treme's NBG separation. No sky quad is sent through VDP1.
- **68000/SCSP:** audio service only, following the reference division of
  labor. Renderer work on the 68000 is explicitly out of scope.

## Tasks

### Task 1 — Shared transform-once terrain cache

- [x] Replace lane-local `[primitive][corner]` transform arrays with one
  shared position-indexed view/projected cache.
- [x] Before the single dispatch, derive which admitted primitives use each
  position and balance shared positions between the two SH-2s.
- [x] Each SH-2 transforms only its owned needed positions.
- [x] Cross one bounded LWRAM phase fence, then classify/clip/compact disjoint
  primitive ranges without further worker notifications.
- [x] Preserve cancellation and serial fallback.

**Gate:** every admitted primitive reads a completed position; every needed
position has exactly one owner; no worker writes VDP1/Gouraud state.

### Task 2 — Correct conservative near handling

- [x] Clear primitive visibility before classification.
- [x] Replace stale `s_primitive_visible` control flow in clipped projection
  with a local success value.
- [x] Reject an entire primitive only when it is wholly behind or the bounded
  view-space clipper rejects it.
- [x] Enable the existing terrain-only view-space clip in the manual build;
  Mario remains on strict actor clipping.

**Gate:** the near-Mario terrain no longer disappears merely because a
crossing primitive was invisible in the prior frame.

### Task 3 — Correct compact-result ordering

- [x] Keep the baked hierarchy for spatial admission.
- [x] Cache one compact `(record pointer, depth, identity)` descriptor after
  the worker join.
- [x] Stable-sort those small descriptors far-to-near without copying compact
  result records or repeatedly reading uncached sort fields.
- [x] Allow two records with the same primitive identity when a five-vertex
  near-plane polygon is split into one VDP1 quad plus one triangle.

**Gate:** serial and dual produce the same stable result sequence; no source
primitive or clipped tail fragment is silently lost.

### Task 4 — Manual image first

- [x] Give the new lineage a distinct build-directory tag so the currently
  running manual image is not overwritten.
- [x] Build replay-free, dual-SH2, BSP-order, terrain-near-clip, textured Mario
  and textured BOB.
- [x] Run focused host/build checks only.
- [x] Hand the Pipe 2 CUE to the owner for manual visual and responsiveness
  testing.
- [x] Hand the corrective Pipe 3 CUE to the owner for immediate retest.
- [x] Hand the conservative-frustum Pipe 4 CUE to the owner for immediate
  retest.
- [x] Hand the capacity-priority Pipe 5 CUE to the owner for immediate retest.
- [ ] Use Ymir's internal screenshot function only if the owner requests a
  milestone capture.

No headless capture matrix, new counters, schema changes, gallery work, or
long route run occurs before owner feedback.

### Pipe 2 manual-gate result — failed

The owner reported that terrain disappearance became worse. The reference
patterns were sound, but two local adaptations were incomplete:

1. the BOB source BSP does not split polygons crossing its planes, so direct
   traversal was not a complete painter order; and
2. a one-plane clip of a quad can produce five vertices, while Pipe 2
   truncated the fifth to fit one four-corner VDP1 result.

Pipe 3 retains transform-once dual-SH2 work, restores a reference-consistent
depth/cut-order analogue through cached descriptor sorting, and splits a
five-vertex clip result into a quad plus triangle. Pipe 2 remains preserved as
the rejected comparison image.

### Pipe 3 manual-gate result — failed

The owner reported that Pipe 3 was just as broken. This rules out the missing
fifth clip vertex and compact-result order as sufficient explanations for the
large unstable terrain groups. Both corrections remain necessary, but neither
is the root cause of the shared disappearance silhouette.

Reviewing the implementation against pinned Z-Treme source exposed a concrete
error in our coarse frustum adaptation. Z-Treme's
`ZT_FRUSTUM.c:145-163` selects the AABB support point separately for each plane,
so opposite-signed axis components cannot cancel. Our center/extent equivalent
instead calculated:

`abs(dot(camera_axis, box_extents))`

That is not an AABB support radius. For a rotated camera, one positive and one
negative axis contribution can cancel to zero, underestimate a large BSP
node's bounds, and reject the entire spatial group as the camera moves.

Pipe 4 preserves the transform-once dual-SH2 path, conservative near clipping,
five-vertex split, and compact descriptor ordering. It replaces the radius
with:

`sum(abs(camera_axis[i]) * box_extent[i])`

and rounds outward. This is a close mathematical adaptation of Z-Treme's
support-point test, not a renderer rollback. The focused
`verify-ztreme-frustum` host gate reproduces the old 45-degree cancellation
case and requires the intersecting box to remain admitted.

### Pipe 4 manual-gate result — failed, downstream mask identified

The owner reported that Mario was absent and terrain disappearance was
slightly worse. The conservative AABB radius remains a correctness fix, but it
admitted enough additional terrain to expose a downstream shared-resource
error:

1. sourceboot has 2,045 draw slots after its two setup commands and required
   END command;
2. clipped BOB can produce up to 1,734 terrain results;
3. textured Mario can require up to 644 base primitive commands plus 50
   texture-detail commands;
4. terrain consumed the arena first, then Mario requested its entire batch as
   one reservation. Insufficient tail space therefore removed all of Mario;
5. the terrain stream is sorted far-to-near, so ordinary prefix exhaustion
   retained distant terrain and discarded the nearby tail; and
6. terrain also consumed Gouraud tables before Mario, explaining the prior
   flat-Mario failure mode on frames where his commands did fit.

Pipe 5 makes those finite-resource priorities explicit. It precomputes Mario's
exact visible command requirement, protects that all-or-nothing tail before
terrain emission, and allocates Mario's Gouraud tables before optional world
shading. If terrain exceeds the remaining command budget, it selects the
nearest tail and emits that retained subset far-to-near. This is a pattern-only
adaptation of Z-Treme's near-first buffer-pressure rule
(`ZT_RENDERING.c:494-503`) plus SlaveDriver's guarded batch-capacity checks
(`WALLS.C:1278-1279,1380-1381`).

The prior transform-once, dual-SH2, conservative-frustum, near-clip,
five-vertex split, and compact-order corrections remain intact. Pipe 5 changes
resource admission priority, not those renderer stages.

### Task 5 — Measure only after the image is useful

- [ ] If the owner accepts the manual direction, build the same-commit serial
  oracle.
- [ ] Run the existing deterministic route once per build.
- [ ] Report absolute serial/dual render FRT and guest cadence using existing
  counters.

The 4.57% prior result remains historical. It is not the acceptance result for
this sprint.

## Manual acceptance

The first handoff succeeds when the owner can load BOB and observe:

- textured/Gouraud Mario;
- materially more coherent terrain around the player;
- no autonomous replay input;
- no new every-other-frame flicker;
- responsiveness that is meaningfully better than the current manual image.

If the image fails one of those observations, fix that failure before any
capture or performance-report work.


### Pipe 5 manual-gate result — failed; real coherency defect fixed in Pipe 6

The owner reported Mario and terrain still disappearing under Pipe 5. Neither
frustum admission (Pipe 4) nor capacity priority (Pipe 5) was the mechanism.

**Defect found: cross-CPU shared state read through incoherent cached aliases.**
The SH7604 has no inter-CPU cache coherency, and the Pipe 5 image placed:

- the transform-phase fence flags in **cacheable LWRAM** (`0x0026291A`,
  verified by `sh-elf-nm` on the shipped Pipe 5 ELF) — the poller caches its
  own `0` reset and can spin its full timeout on a stale line, forcing the
  costly serial fallback intermittently; the code comment justifying this
  rested on the false premise that LWRAM is uncached — LWRAM at `0x00200000`
  is cacheable; only the `0x2xxxxxxx` mirror bypasses the cache;
- the terrain result-span headers in a **local on the master's cached stack**
  (`terrain_spans`) — the master's `slave.count = 0` init line stays hot in
  its cache, the slave's write-through increment lands only in memory, and
  the join reads the stale `0`, silently dropping the slave's entire record
  span. The adaptive split (`s_slave_begin`) moves each frame, so the missing
  terrain half wanders with camera/load. On frames where the stack line is
  evicted, the true (large) count merges instead — command/Gouraud demand
  spikes past the Pipe 5 budget and Mario's backend reservation fails
  (`demo_emit_mario` reserve path), removing Mario on exactly the frames
  terrain is most complete. **One incoherent header, two anti-correlated
  symptoms.**

**Pinned reference files inspected (reuse mode: pattern restored from the
already-close-ported upstream):** `slavedriver-engine@a898659`
`WALLS.C:1806-1810` (full cache flush at every slave pass:
`*CACHECNTRL=0x10; 0x01;`) and `WALLS.C:1272,1356-1406` (`cacheThruResult` —
all slave result writes through the cache-through alias). Our adaptation kept
the uncached-alias read for *records* (`slavedriver_terrain_result.h`) but
dropped the discipline for the fence, the span headers, and the shared
transform bank. `sonic-z-treme@cff7545` slave communication re-inspected;
consistent with the same requirement.

**Fix (smallest, Pipe 6):**
1. Fence flags and the span-header struct moved to the `.uncached` section
   (`saturn_demo_render.c`, `DEMO_CROSS_CPU_SHARED`) — every access from
   either CPU bypasses cache by address; the spans struct came off the
   master's stack (`s_terrain_spans_shared`).
2. One `cpu_cache_purge()` per CPU after crossing the fence, before consuming
   the peer's half of the shared transform bank — SlaveDriver's own per-pass
   discipline; SH7604 write-through means purge is invalidate-only.
3. The false LWRAM comment corrected in place.

**Regression gate (failed first, passes now):**
`tools/saturn/verify_dual_cpu_coherency.py`, wired into the sourceboot
`make verify` for demo-path images — asserts the three shared symbols resolve
at or above `0x20000000`. Against the shipped Pipe 5 ELF it fails (cached
addresses); against Pipe 6 all three resolve at `0x260FD1xx` (uncached
mirror). Pipe 6 full `make verify` exit 0, including this gate and the HWRAM
floor.

- [x] Hand the coherency-fixed Pipe 6 CUE to the owner for manual retest
  (replay-free, Pipe 5 configuration, `SATURN_RENDERER_PIPELINE=6`):
  `build/saturn/sourceboot/e2-bob-demo-r6000-slave1-poly0-hot1-clip1-bsp1-frag0-pipe6/sm64-saturn-sourceboot-e2.cue`

**Pipe 6 manual result — failed (2026-07-29):** the owner still observed
unstable Mario and terrain. A same-source `SATURN_SLAVE_RENDER=0` r6000
serial oracle then showed the same instability. Cross-CPU coherency therefore
remains a required correctness fix, but it is not the primary mechanism behind
the surviving disappearance.

The proposed r2048 discriminator was also invalid: under
`SATURN_DEMO_BSP_ORDER=1` and `SATURN_DEMO_BSP_FRAGMENTS=0`,
`SATURN_DEMO_VIEW_RADIUS` does not participate in the selected visibility
path. Pipe 6 r2048 and r6000 were byte-identical:

- ELF SHA-256:
  `BF688648C1004DC686B42FF4327885B2EA730268BAFA4770E64679387C3456D8`;
- ISO SHA-256:
  `BD0CD5A79FF0B84240BCE1D736D1DADF4BDFE0C13C8F41B82C8C331295517828`.

The r2048 crash therefore cannot be attributed to a radius change.

### Pipe 7 — draw-completion-gated VDP1 presentation

The serial failure moved the discriminator below terrain production. Both
serial and dual images share one command-VRAM upload and framebuffer lifecycle.
Source inspection found that sourceboot used Yaul's default auto interval and
treated one VBLANK-IN/OUT pair as proof that plotting had retired. At pinned
Yaul commit `6012f79f237773378c8014e70d8998ad95a38d98`,
`libyaul/scu/bus/b/vdp/vdp_sync.c:816-854` auto mode calls the sprite-end
transition at VBLANK-IN without reading `EDSR.CEF`, then clears the sync state
at VBLANK-OUT. A multi-field VDP1 list can consequently still read command
VRAM while the next frame overwrites it. Because the painter stream is
far-to-near and Mario is emitted last, the corrupted/unplotted tail is exactly
near terrain plus Mario.

**Pinned references re-inspected:**

- `Lobotomy-Software/SlaveDriver-Engine`
  `a8986591557b6e680550d3c23970284d3b38ff8f`,
  GPL-3.0-or-later, `INITMAIN.C:529-531` and `SPR.C:465-477`:
  `EZ_closeCommand(); SPR_WaitDrawEnd(); SCL_DisplayFrame();` — draw-end
  precedes display-frame advancement.
- `Maxime-XL2/SONIC-Z-TREME`
  `cff75451c1616aac1236fc2b44223902b55c706b`, GPL-3.0,
  `ZTE/ZT_GAME.c:52,107`: every gameplay iteration closes through SGL
  `slSynch()`.
- `yaul-org/libyaul`
  `6012f79f237773378c8014e70d8998ad95a38d98`, MIT,
  `vdp_sync.c:330-374,980-1068`: variable interval mode starts plotting
  explicitly and requests framebuffer change only after `EDSR.CEF`.

Reuse mode is API use plus pattern restoration; no upstream engine source was
copied. Pipe 7 selects uncapped variable sync once with
`vdp1_sync_interval_set(-1)`. The existing source-game VBlank normally retires
the state with no extra wait; an overlong plot delays only the next
presentation/upload rather than corrupting command VRAM.

The new dual-SH2 image passed full sourceboot verification and is distinct from
Pipe 6:

- CUE:
  `build/saturn/sourceboot/e2-bob-demo-r6000-slave1-poly0-hot1-clip1-bsp1-frag0-pipe7/sm64-saturn-sourceboot-e2.cue`;
- ELF SHA-256:
  `1CF2188227B3DC5025269950851464A7B9C38366608B22A64EA4BD45A5B1D4CD`;
- ISO SHA-256:
  `CFE1539713C8B47985CDA41FCBAD2C2F3C919C5208EB6CC091B4FD1FBC0B094B`.

- [x] Owner manual gate (2026-07-29): “Terrain and mario are finally much
  more stable.” Pipe 7 fixes the shared presentation instability. The owner
  isolated one residual defect: terrain geometry remains present, but its
  texture flickers out.

### Pipe 8 — preserve terrain material across near clipping

The residual texture-only flicker has a direct frame-dependent switch in the
terrain classifier. Every BOB primitive crossing `SATURN_DEMO_NEAR_DEPTH`
successfully produced clipped geometry, but `s_primitive_recovery` was also
set unconditionally. Both terrain emitters interpret that bit as “do not bind
the source texture” and lower the same surface as a flat/Gouraud polygon.
Camera motion changes the crossing set every frame, so the texture—not the
geometry—blinks at the near plane.

This is separate from Pipe 7's presentation fix and does not change command
ownership, painter order, dual-SH2 dispatch, clipping coordinates, command
budget, or VDP1 synchronization. Pipe 8 keeps the source texture identity on
the clipped distorted sprite. The clipped edge can stretch because VDP1 maps
the complete source tile over the new quadrilateral; that localized mapping
error is preferable to changing the material class from frame to frame and
can later be improved with source-domain-aware subdivision.

**Pinned references re-inspected:**

- `Maxime-XL2/SONIC-Z-TREME`
  `cff75451c1616aac1236fc2b44223902b55c706b`, GPL-3.0,
  `ZTE/ZT_LOAD_MODEL.c:88-102` constructs textured polygon attributes with
  `UseNearClip`, and `ZTE/ZT_RENDERING.c:119,127` enables near clipping. The
  texture attribute remains the polygon's material while clipping is active.
- `Lobotomy-Software/SlaveDriver-Engine`
  `a8986591557b6e680550d3c23970284d3b38ff8f`,
  GPL-3.0-or-later, `WALLS.C:723-803` uses a flat `drawClippedFace` recovery,
  but confines it to an exceptional subdivided-face path; ordinary tiled wall
  results retain their tile identity through `WALLS.C:1261-1408`. Applying
  that fallback to every large crossing BOB primitive made the exceptional
  path visually dominant here.

Reuse mode: close behavioral adaptation of the pinned clipping/material
policy; no upstream source copied.

- [x] Build and verify the dual-SH2 Pipe 8 image. Full `make verify` passed,
  including the uncached dual-CPU coherency gate.
  - CUE:
    `build/saturn/sourceboot/e2-bob-demo-r6000-slave1-poly0-hot1-clip1-bsp1-frag0-pipe8/sm64-saturn-sourceboot-e2.cue`
  - ELF SHA-256:
    `A1F406775D3DEA71EA25026BCA3C1BCBAF96BF63D64F3829ECAC2D94E7F07814`
  - ISO SHA-256:
    `6F04F6F6D5267D23FCF1E0B466D884AAA58DCC642C09FC6CEF3D0B516373D3D0`
- [ ] Owner manual gate: terrain textures no longer switch to flat material
  as nearby polygons cross the camera plane; record any remaining clipped-edge
  stretching separately from disappearance/flicker.
