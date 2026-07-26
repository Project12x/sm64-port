# VDP1 textures: design for the sourceboot (real-engine) path

**Status:** Design recommendation, pending owner acceptance.
**Date:** 2026-07-26
**Branch at time of writing:** `saturn/bootstrap` @ `312cc49`
**Scope:** How the sourceboot target, which runs the unmodified SM64 game loop
and walks real Fast3D display lists, gets textured geometry onto VDP1.
Depends on and does not relitigate
[`docs/saturn/SEGMENT_ADDRESSING_DECISION.md`](../../saturn/SEGMENT_ADDRESSING_DECISION.md)
(commit `44b4f6e`).

**Investigation and design only.** No source file was modified in producing
this note. No build was run and no capture was taken.

---

## 0. Summary

**Recommendation: adopt castleviewer's offline per-render-primitive tile bake,
re-keyed on `(display_list, list_ordinal)` — the quad map's key — and ship a
first milestone that textures Bob-omb Battlefield's terrain only.**

The single strongest reason: **the offline bake is the only approach that makes
VDP1's missing capability disappear at build time rather than at run time.**
VDP1 has no per-vertex UV, no wrap/mirror/clamp, no mask/shift, and no colour
combiner. The bake evaluates all of that on the host, in Python, against the
real `gsDPSetTile`/`gsDPSetTileSize`/`gsSPTexture` state, and emits a character
pattern whose *implicit* corner mapping is already correct. The runtime then
needs **zero** new GBI decoding — no `G_SETTIMG`, no `G_SETTILE`, no
`G_LOADBLOCK` — only a tile index looked up through machinery
(`saturn_fast3d_quad_map_bind`) that already exists, is already proven, and
already carries a generation stamp. That is the smallest possible change to the
hottest, most fragile part of the port.

The measurement that settles it: of BOB's 1,101 terrain triangles, **only 5.3%
could be drawn by binding a source texture to a VDP1 sprite**; 63.9% have
genuinely arbitrary affine UVs and 68.3% tile past one texture period (§2.1a).
The approach that two shipping Saturn engines used — bind a shared texture,
permute the corners — covers a twentieth of this level. It is not an option
here, because SM64's geometry was not authored for it.

Measured budget: **446,432 bytes of VDP1 VRAM are free today** (§5).

The honest cost is **tiling density** (§4.1), and it is the reason this design
does not simply copy castleviewer's uniform 16x16 tile. A triangle whose UVs
span `k` texture periods gets `k` periods squeezed into one `N x N` tile.
Measured over BOB's 1,101 terrain triangles: **median `k` = 1.97, p90 = 6.88,
max = 29.76**, and only **4.6% of the level's world surface area sits at
`k <= 1`**. A uniform 16x16 tile undersamples the *median* primitive 4:1.

So milestone 1 ships **two tile classes** — 16x16 for `k <= 2` (567 triangles)
and 32x32 for `2 < k <= 16` (510) — costing **333,696 bytes**, 74.7% of what is
free. The 24 triangles at `k > 16` are anisotropic path strips that no
affordable tile size helps; they stay on the existing Gouraud path and are
called out as a known, bounded hole rather than papered over.

---

## 1. The problem, stated precisely

### 1.1 What the N64 RDP does

Every SM64 `Vtx` row carries a texture coordinate pair `tc[2]` in S10.5 fixed
point (`include/PR/gbi.h:1112-1136`). The RDP:

- divides `s/w`, `t/w` per pixel — **perspective-correct** mapping;
- resolves each coordinate against a *tile descriptor* with **independent S and
  T** clamp / mirror / wrap, a power-of-two `mask`, a `shift`, and a
  `(uls, ult, lrs, lrt)` extent (`gsDPSetTile`, `gsDPSetTileSize`);
- scales by the per-primitive `gsSPTexture` S/T factors;
- bilinear-filters the fetched texels;
- and feeds the result into a **two-cycle colour combiner** that can multiply,
  interpolate and add texture, shade, primitive colour, environment colour and
  a second texture in one pass.

### 1.2 What VDP1 does

VDP1 has exactly three textured primitive types (`vdp1/cmdt.h:28-38`):
`VDP1_CMDT_NORMAL_SPRITE` (0), `VDP1_CMDT_SCALED_SPRITE` (1),
`VDP1_CMDT_DISTORTED_SPRITE` (2). Only the distorted sprite accepts four
arbitrary corners, so it is the only one usable for 3-D geometry. The command
table (`vdp1_cmdt_t`, 32 bytes) carries:

| Field | Meaning | Encoding constraint |
|---|---|---|
| `CMDSRCA` | character-pattern address | `base >> 3` (`cmdt.h:347`) — **8-byte aligned** |
| `CMDSIZE` | pattern size | `((width >> 3) << 8) \| height` (`cmdt.h:353`) — **width a multiple of 8, max 504; height 1-255** |
| `CMDCOLR` | colour bank index, or CLUT address `>> 3` (`cmdt.h:314`) | |
| `CMDPMOD` | colour mode (bits 5-3), colour calculation (bits 2-0), SPD, ECD, mesh, MSB (`cmdt.h:108-123`) | |
| `CMDXA..CMDYD` | four screen-space corners | signed 16-bit |
| `CMDGRDA` | Gouraud table address `>> 3` (`cmdt.h:415`) | 4 x RGB1555, 8 bytes |

**There is no texture-coordinate field anywhere in the command table.** The
mapping is fixed and implicit: texel `(0,0)` goes to corner A, `(W-1,0)` to B,
`(W-1,H-1)` to C, `(0,H-1)` to D. The whole pattern is always used. The only
mapping controls are the H/V flip bits (`cmdt.h:81-86`) and the choice of which
screen vertex you write into which of CMDXA..CMDYD.

Concretely, VDP1 therefore **cannot**:

1. **Map arbitrary UVs.** There is nowhere to put them. This is the primary
   mismatch and everything else follows from it.
2. **Correct for perspective.** VDP1 walks the A->D and B->C edges linearly in
   screen space and interpolates rows between them. The result is affine (or,
   for a general quad, bilinear), so a surface receding from the camera shows
   the classic PS1-era texture swim. There is no `w` anywhere in the pipeline.
3. **Wrap, mirror or clamp.** Those are RDP tile-descriptor attributes. VDP1's
   pattern is exactly `W x H` texels and is consumed exactly once.
4. **Combine.** VDP1's whole colour path is `CMDPMOD`'s three colour-calculation
   bits: replace, shadow, half-luminance, half-transparent, and Gouraud
   (`cmdt.h:64-70`). Gouraud is orthogonal (CMDPMOD bit 2) and *does* apply to
   textured sprites, but it is an **additive** signed correction around 16 in
   5-bit channel space, not a multiply — see `src/port/saturn/gfx/saturn_gouraud.h`.
   There is no `texture x shade` multiply, no primitive/environment colour, no
   second texture, no alpha compare.
5. **Filter or mip.** Nearest texel only; no LOD hardware.
6. **Do per-pixel alpha.** Texel code `0x0000` (RGB1555 with bit 15 clear) or
   palette index 0 is *transparent* when SPD is clear; the end code (`0xF` in
   4bpp, `0x7FFF` in RGB) terminates the row unless ECD is set. That is the
   whole of VDP1's alpha model — binary, and easy to trip over
   (`tools/saturn/vdp1_texture.py` and `castleviewer/main.c:997-1000,1056-1059`
   both carry scars from it).

Colour modes are colour-bank (`CM_CB_16/64/128/256`, where CMDCOLR supplies the
high bits of an index into VDP2 colour RAM), lookup-table (`CM_CLUT_16`, where
CMDCOLR points at a 16-entry x 16-bit table in VDP1 VRAM), or direct
(`CM_RGB_32768`, RGB1555 with **B in bits 14-10, G in 9-5, R in 4-0** — the
reverse of the N64's RGBA16 lane order, confirmed independently by
`tools/saturn/vdp1_texture.py` and SlaveDriver's `UTIL/MAKETEX.C:36-40`).

### 1.3 The exact incompatibility `SATURN_MESH_IR.md` refers to

`docs/saturn/SATURN_MESH_IR.md:92-95` says textured triangles are forced to
fallbacks "until VDP1 rectangular texture compatibility is implemented". The
incompatibility is item 1 above, sharpened by two further facts:

- **A triangle is drawn as a degenerate quad** (corner D repeated onto C — see
  `saturn_fast3d_vdp1_emit.c:53-61`). A degenerate quad still maps the *entire*
  rectangular character pattern; it folds the D corner onto C. So a source
  triangle whose UVs cover half of a 32x32 texture cannot be drawn by binding
  that texture — the pattern is rectangular and the primitive is not.
- **The pattern's texel extent is its UV extent.** The UV rectangle is not a
  parameter; it *is* `CMDSIZE`.

So "rectangular texture compatibility" means: *a VDP1 textured primitive can
only show a rectangular region of a texture, mapped corner-to-corner, at
whatever rotation/flip the four vertex slots and the two flip bits allow.*
Everything SM64 does that is not that shape has to be resolved somewhere else.

### 1.4 Where the two sets do intersect

They intersect in exactly one place, and it is the whole opportunity: **the
rectangle VDP1 insists on is one we choose.** If the character pattern is
manufactured offline by resampling the source texture *through* the primitive's
own UVs and tile state, then VDP1's fixed corner mapping reproduces the
intended appearance by construction. That inverts the problem from "map
arbitrary UVs onto a fixed texture" — impossible — to "manufacture a texture
that a fixed mapping renders correctly" — a build-time job.

That is what `tools/saturn/bake_castle_uv.py` already does, and it is why
castleviewer has real textures on screen today.

---

## 2. Measured facts

Every number in this section was measured for this note. Commands are cited.

### 2.1 What BOB actually demands

Measured by a balanced-paren GBI tokenizer over `levels/bob/leveldata.c`'s nine
`model.inc.c` files, `levels/bob/texture.inc.c`, and `bin/generic.c`.

| Quantity | Value |
|---|---|
| Distinct textures referenced | **18** |
| Total texture bytes | **38,912** (38.0 KiB) |
| Formats | 17 x RGBA16, 1 x IA16. **Zero CI4, zero CI8, zero TLUT** |
| Sizes | 17 x 32x32, 1 x 32x64. Nothing larger, nothing non-power-of-two |
| Texture bindings | 26 (23 terrain, 3 level objects) |
| Terrain triangles | **1,101** across 23 bindings |
| Triangles per binding | min 2, median 28, max 226 |
| `gsDPSetTile` render tiles | 15; **mirror count: 0** |
| Wrap modes | S/T = WRAP/WRAP x22, CLAMP/WRAP x3, CLAMP/CLAMP x1 |
| Combiners | `G_CC_MODULATERGB` x21, `G_CC_DECALRGBA` x4, `G_CC_MODULATEIA` x1. Zero `gsDPSetCombineLERP` |
| `gsSPTexture` scale | always `0xFFFF, 0xFFFF` (unity) |

### 2.1a What BOB's UVs actually look like — the decisive measurement

Measured by walking `bob_geo_000488` (`levels/bob/areas/1/geo.inc.c`) through
`dl_rigid_groups.parse_geo_layouts` and a Fast3D 32-slot vertex-cache walk that
carries `Vtx.tc[2]`, over all six `levels/bob/areas/1/{1..6}/model.inc.c`.
**1,101 triangles, 3,303 UV references, 0 vertex-cache misses** — the triangle
count is confirmed independently of the Makefile comment.

| Quantity | Value |
|---|---|
| `s` range | **-735.5 .. +872.5 texels** (raw -23,536 .. +27,920) |
| `t` range | **-842.0 .. +446.1 texels** (raw -26,944 .. +14,274) |
| Distinct `(s,t)` pairs | 955 of 3,303 references |
| `s` on a whole-texel boundary | 680 / 3,303 (20.6%) |
| Triangles whose 3 UVs sit on 3 corners of *some* axis-aligned rectangle | 397 (36.1%) |
| ...where that rectangle is exactly one texture period | **61 (5.5%)** |
| **Triangles with genuinely arbitrary UVs (3 distinct `s` and/or `t`)** | **704 (63.9%)** |
| **Triangles whose own UV extent exceeds one texture period** | **752 (68.3%)** |
| Triangles fitting inside a single period | 349 (31.7%) |

And for adjacent (shared-edge) triangle pairs — the case that would let a merged
quad bind a source texture directly:

| Criterion | Pairs | Triangles | % of 1,101 |
|---|---:|---:|---:|
| Shared-edge adjacent, disjoint matching | 433 | 866 | 78.7% |
| 4 combined UVs on the corners of *some* axis-aligned rectangle | 160 | 320 | 29.1% |
| **...rectangle = the whole bound pattern, period-aligned (direct-bindable)** | **29** | **58** | **5.3%** |

The direct-bind figure is stable under tolerance: 0 at exact equality, 29 at a
2-raw-unit tolerance, 31 at 8-64 raw units. The 2-unit tolerance is needed
because **SM64's exporter writes the full-pattern span as 990, not
`(32-1) << 5 = 992`** — a one-off worth knowing before anyone writes an exact
comparison and concludes the answer is zero.

**This is the number that decides the design.** Only 5.3% of BOB's terrain can
be drawn by binding a source texture to a VDP1 sprite. 63.9% has arbitrary
affine UV mapping that VDP1 structurally cannot express, and 68.3% tiles past
one texture period — classic wrapped ground, one 32x32 grass pattern smeared
across a hill. Any approach that keeps VDP1's corner-mapping model *and* the
source textures unmodified tops out at 29.1% coverage even in the most generous
reading.

**Structural fact that matters more than any of the above:** BOB's display
lists are strictly two-level. A *wrapper* list holds all render state and tile
setup and calls *leaf* lists; each leaf begins with exactly one
`gsDPSetTextureImage` / `gsDPLoadSync` / `gsDPLoadBlock` triple and then emits
nothing but `gsSPVertex` and triangles until `gsSPEndDisplayList`. **There is
never a second texture bind inside a leaf.** So *texture state is constant over
a whole leaf display list* — which means the bake's key can be the display list
itself, and two triangles in the same leaf always share texture state.

### 2.2 What Mario demands (for scoping only — Mario is out of milestone 1)

| Quantity | Value |
|---|---|
| Bound textures | 17 (of 19 declared; two eye variants are dead) |
| Bytes | 45,056 (44.0 KiB); all RGBA16 |
| Sizes | 12 x 32x32, 4 x 32x64, 1 x 64x32 |
| Render tiles | 43, **all `G_TX_CLAMP` on both axes**; the 8 metal bindings are WRAP/WRAP |
| Dominant textured combiner | `G_CC_BLENDRGBFADEA` x36 — a LERP between texture RGB and primitive RGB keyed on texture alpha. **VDP1 cannot express this.** |
| Metal path | `G_TEXTURE_GEN` spherical env-mapping with non-unity `gsSPTexture` scale `0x0F80, 0x07C0` |
| Textured triangle keys in the quad map | **99 of 1,265** (`build/saturn/sourceboot/generated/saturn_quad_map.json`) |

Mario is **mostly untextured**: 1,166 of his 1,265 triangle keys carry no
texture binding. Terrain is where texture matters.

### 2.3 The whole BOB+Mario texture working set

35 distinct textures, **83,968 bytes**. Uploaded as RGB1555 direct-colour
patterns that is 83,968 bytes of VDP1 VRAM — 18.8% of what is free. **The
distinct textures are not the budget problem.** Anything that instances
textures per primitive is.

### 2.4 The quad map today

`build/saturn/sourceboot/generated/saturn_quad_map.json`:

| Actor | Triangle sites | Keys | Ineligible | Reason | Quads | Commands saved |
|---|---:|---:|---:|---|---:|---:|
| `mario_geo_body` | 4,242 | 1,265 | 99 | `textured` (100%) | 260 | 260 |
| `cannon_barrel_geo` | 62 | 62 | 16 | `textured` (100%) | 16 | 16 |
| `cannon_base_geo` | 38 | 38 | 8 | `textured` (100%) | 8 | 8 |

**Every single ineligible key in the entire map is ineligible for one reason:
`textured`.** `tools/saturn/dl_rigid_groups.py:250-292` marks any triangle with
an active texture binding `unsafe`, citing `SATURN_MESH_IR.md`'s "current
limits". `Makefile.saturn.mk:38-41` records why BOB's terrain is not in the map
at all: *"every one of its 1101 triangles is textured, and textured triangles
are never paired, so its map is empty."*

### 2.5 What the frontend decodes today

`src/port/saturn/gfx/saturn_fast3d_frontend.c` dispatches semantics for exactly
five opcodes: `G_MTX`, `G_MOVEMEM`, `G_MOVEWORD`, `G_VTX`, `G_GEOMETRYMODE`,
plus `G_TRI1`/`G_TRI2`/`G_DL`/`G_ENDDL` control flow.

Every texture command — `G_SETTIMG`, `G_SETTILE`, `G_SETTILESIZE`,
`G_LOADBLOCK`, `G_LOADTILE`, `G_LOADTLUT`, `G_TEXTURE`, `G_SETCOMBINE`,
`G_RDPSETOTHERMODE`, `G_RDPLOADSYNC`, `G_RDPTILESYNC`, `G_RDPPIPESYNC` — falls
through to `default:` and is **counted only**
(`saturn_fast3d_count_command`, `:81-144`, buckets them into
`profile->texture_commands` and `profile->rdp_commands`). Nothing is decoded.

The vertex decode at `:1127-1216` reads `ob[3]` and `cn[4]` and **never touches
`tc[2]`**. The resolved-triangle record
(`saturn_fast3d_frontend.h`, `sm64_saturn_resolved_triangle_t`) carries
`x[4]`, `y[4]`, `corner_rgb1555[4]`, `depth_bucket` — 20 bytes, no texture
field. The emit path (`saturn_fast3d_vdp1_emit.c:67`) issues
`vdp1_cmdt_polygon_set` unconditionally.

### 2.6 HWRAM

Measured now, against the existing artifact:

```
$ sh-elf-nm build/saturn/sourceboot/e2-bob/obj/sm64-saturn-sourceboot-e2.elf
060dece4 B ___end
```

HWRAM top is `0x06100000` (`sourceboot-cart.x:17,134`), so the margin is
`0x2131C` = **135,964 bytes (132.8 KiB)**. This ELF was relinked 2026-07-26
12:15, so it supersedes the 137,660 B figure in `SEGMENT_ADDRESSING_DECISION.md`
§4.3 and the stale "~380 bytes" comment in `saturn_fast3d_frontend.h`.

**A 333,696-byte tile bank does not fit in HWRAM.** It belongs in
`.cart_rodata` (2,140,352 B free per `SEGMENT_ADDRESSING_DECISION.md` §4.1) and is staged
to VDP1 through the internal-WRAM ring, exactly as
`castleviewer/main.c:1136-1183` already does.

---

## 3. What castleviewer already solved

`src/port/saturn/castleviewer/` is the reference implementation and it has
already crossed most of this ground. Measured against its generated header
`build/saturn/castlearea/generated/castle_uv_tiles.h`:

| Quantity | Value |
|---|---|
| Tile size / format | 16x16, **CLUT16** (`SM64_CASTLE_UV_TILE_BYTES` = 128) |
| Tiles | **832** |
| CLUTs | **9** (one per source texture) |
| Textured render primitives | 567 |
| **Four-vertex textured primitives** | **195** (`paired_textured_quads`) |
| Source triangles / post-BSP triangles | 619 / 888 |
| Post-BSP polygons before adaptive split | 713 |
| Texture partition it sets | `832*128 + 50*512` = **132,096 bytes** (`main.c:1209-1211`) |

### 3.1 What it solved

1. **Arbitrary UV.** `bake_castle_uv.py:246-257` (`sample_quad`) resamples the
   source texture through `distorted_sprite_weights(x, y, W, H)` — the
   project's own BIOS-probed model of VDP1's distorted-sprite corner
   interpolation (`tools/saturn/vdp1_texture.py:66-84`). Every destination
   texel's source UV is the weighted blend of the primitive's four source UVs.
2. **Triangles.** `sample_triangle` (`:227-243`) uses
   `repeated_vertex_weights`, which folds the D weight onto C to match the
   `(A,B,C,C)` degenerate quad the runtime emits. The comment at `:232-236` is
   the load-bearing lesson: *do not* mask a diagonal in texture space; that
   discards valid coverage and creates view-dependent seams.
3. **The whole Fast3D tile state.** `sample_raw` (`:202-224`) resolves
   `sp_scale_s/t`, `uls/ult`, tile `width/height`, `mask_s/t`, `shift_s/t`,
   `clamp_s/t` and `mirror_s/t` — independently per axis — *before* any
   downscale. Its comment records the bug that motivated it: inferring both
   axes' clamp state from the whole `gsDPSetTile` macro turned S-wrap/T-clamp
   walls into horizontal streaks.
4. **CLUT quantization.** `quantize_clut16` (`:87-139`) is a deterministic
   median-cut producing transparent + 15 colours per source texture.
5. **RGB1555 conversion and downsampling.** `vdp1_texture.py:7-63` box-filters
   in 5-bit channel space with majority-coverage alpha and canonicalises every
   transparent result to exactly `0x0000` — VDP1's transparent code is `0x0000`
   and nothing else.
6. **Residency and staging.** `sm64_saturn_texture_residency_upload`
   (`saturn_texture_residency.h:32-51`) is a bounds-checked SCU transfer into
   the VDP1 texture partition; `upload_texture_bank` (`main.c:1136-1183`) reads
   the bank out of cartridge DRAM through an internal-WRAM ring in
   `CART_STAGE_CHUNK` pieces, with a fall-back to direct upload when no cart is
   present.
7. **Affine-error mitigation.** `adaptive_subdivide_triangle` (`:274-324`)
   bisects a primitive's longest world-space edge until it is under a threshold,
   bounding how much affine warp any single sprite can show. That is why 567
   primitives became 832 tiles.
8. **Textured four-corner primitives.** 195 of its 567 textured render
   primitives are genuine quads, each baked as a **single tile** through
   `sample_quad` (`bake_castle_uv.py:638-644`). Provenance matters here and is
   easy to overstate: these are *BSP-output convex quads*, not pairs chosen by
   the `quad_pairing` matcher — 888 post-BSP triangles became 713 polygons, a
   19.7% reduction. So castleviewer proves the **bake mechanism for a textured
   quad**, not the matcher's ability to find one. That distinction is what §6
   has to close.

### 3.2 What it deliberately did not solve

- **Perspective correction.** Not attempted. Subdivision bounds the error; it
  does not remove it.
- **Runtime texture state.** Nothing in castleviewer decodes a GBI texture
  command. All of it is resolved offline.
- **Streaming or eviction.** The whole bank is uploaded once at startup and
  never changes.
- **Camera-independent coverage.** `DEFAULT_SELECTED` (`bake_castle_uv.py:34-40`)
  names **nine** hand-picked source textures visible to its fixed M3 camera.
  It is a bounded slice, not the level.
- **The combiner.** Castle tiles draw `CC_REPLACE` (or `CC_HALF_TRANSPARENT` for
  decals). Mario's textured tier draws a Gouraud *polygon* and then a
  `CC_REPLACE` textured sprite on top (`main.c:1036-1066`) — two commands, not
  a modulate.
- **Anything animated.** Its Mario tiles are baked from one pre-selected pose's
  UVs; UVs do not deform, so this works, but nothing validates it against a
  pose envelope.

### 3.3 What transfers to the sourceboot path, and what changes

sourceboot is a fundamentally different consumer: it runs the **real engine**
and walks the **real display lists** through `saturn_fast3d_frontend.c`.
castleviewer walks a pre-baked mesh IR with its own BSP, its own sort, and a
scene-global primitive id.

| castleviewer | sourceboot | Transfers? |
|---|---|---|
| `vdp1_texture.py` RGB1555 conversion, downsample, transparent canonicalisation | same | **Yes, verbatim** |
| `distorted_sprite_weights` / `repeated_vertex_weights` | same | **Yes, verbatim** |
| `sample_raw` Fast3D tile-state resolution | same | **Yes** — but must be driven from parsed `gsDPSetTile`/`gsDPSetTileSize`/`gsSPTexture` in the real `.inc.c`, not from a hand-built intake JSON |
| `quantize_clut16` | same | **Yes** |
| `saturn_texture_residency.h` + cart->WRAM ring staging | same | **Yes, verbatim** |
| Tile index = scene-global primitive id | **No.** Must be `(display_list, list_ordinal)` | **Replaced** — but by the quad map's existing, proven key |
| Offline BSP + camera-selected material slice | **No.** Free roam sees everything | **Replaced** — bake all 1,101 |
| Its own painter sort / cull / projection | **No.** The frontend owns all of it | **Dropped** |
| Emitting the tile bind at draw time from a static table | Becomes a lookup in the frontend's per-list bind | **Adapted** |
| Two-command Gouraud-polygon-then-textured-sprite for actors | Should become **one** command using `CC_GOURAUD` on the sprite | **Improved** |

The three things that genuinely change:

1. **Keying.** The tile index must survive a *runtime* display-list walk in
   which the same list can be entered several times per frame, nested, and
   branched into. That is exactly the problem `saturn_fast3d_quad_map_bind`
   (`saturn_fast3d_frontend.c:201-250`) already solves, and its
   `quad_slot_generation` design earned its keep by catching a real
   cross-display-list corruption bug. **The tile map must use the same key and
   the same generation discipline.** Do not invent a second scheme.
2. **Coverage.** A free-roam camera can see all of BOB. There is no
   "selected materials" escape hatch.
3. **The bake input.** castleviewer's baker consumes
   `docs/saturn/evidence/reports/castle-area1-source-root-ir-*.json`, an
   intake produced by a separate extractor. The sourceboot baker must instead
   parse `levels/bob/**/model.inc.c` directly, with the same Fast3D 32-slot
   vertex-cache semantics `tools/saturn/quad_map.py:resolve_display_list_vertices`
   already implements — reuse that, do not rewrite it.

---

## 4. The options

### Option A — Shared texture bank, direct sprite binding (the SlaveDriver model)

Upload each of BOB's 18 distinct textures once as an RGB1555 character pattern
(38,912 bytes). For each primitive, emit a distorted sprite bound to the shared
pattern, choosing which screen vertex goes into CMDXA..CMDYD (a 4-way corner
permutation) plus the H/V flip bits to match its UVs.

**This is what a shipping Saturn title did.** SlaveDriver's `WALLS.C:1042-1092`
walks a grid of sub-quads, pulls a tile index per cell, and uses a `pattern[]`
array to permute which screen corner maps to A/B/C/D — the only "texture
orientation" control it has. Sonic Z-Treme does the same with flip bits plus
CMDSRCA row-scrolling for animation.

**Cost / constraint**

- Correct **only** when the primitive's four UVs are exactly the four corners
  of one texture period, in some rotation/flip. Anything else is wrong, not
  approximate.
- A source *triangle* covering half a texture square cannot be drawn: the
  degenerate quad maps the whole rectangle. So Option A is only ever available
  to **merged quads**, which makes it depend on unblocking textured quad
  merging first.
- 22 of BOB's 26 bindings are `G_TX_WRAP` on both axes with `mask` 5 (32) or 6
  (64). A primitive whose UVs span *k* periods needs either a pre-tiled
  `k x k` pattern (VRAM x k^2) or a geometry split.

**Coverage, measured (§2.1a): 29 quads = 58 of 1,101 triangles = 5.3%.**
Widened to "any axis-aligned rectangle, allowing one offline resample per quad
but no per-vertex UV interpolation" it reaches 160 quads = 320 triangles =
29.1%, and those rectangles are often hundreds of texels wide.

**Unlocks:** full source texture resolution; trivial VRAM (8.7% of free);
zero resampling; the natural home for any future *dynamic* geometry, since a
runtime `G_SETTIMG` decode can pick the pattern.

**Verdict: rejected as a primary strategy, on measurement.** At 5.3% coverage
it leaves 94.7% of BOB's terrain untextured — not a patchy result, a
predominantly untextured one. It survives only as a **milestone-2
micro-optimisation**: for those 29 quads it saves 29 tiles of VRAM and gives
full source resolution, which is worth having but is not a design. The
measurement that killed it is the single most valuable thing this
investigation produced, and it was not obvious in advance — SlaveDriver and
Sonic Z-Treme both shipped on exactly this model, but both authored their
level geometry *for* it.

### Option B — Per-render-primitive baked tiles (the castleviewer model) — recommended

Bake one square character pattern per render primitive, resampling the source
texture through that primitive's own UVs and its full Fast3D tile state. Key the
bank on `(display_list, list_ordinal)`.

**Cost / constraint**

- VRAM scales with **primitive count**, not texture count. For BOB's 1,101
  terrain triangles a uniform 16x16 CLUT16 tile would be 140,928 bytes — 31.6%
  of what is free (§5) — but §4.1 shows a uniform 16x16 is too coarse; the
  recommended two-class layout costs **333,696 bytes**, 74.7%. At 16x16 RGB1555
  a uniform bank would be 563,712 bytes and **does not fit** at all.
- Resolution loss. A primitive whose UVs span a full 32x32 source texture is
  resampled to 16x16 — half resolution. A primitive spanning a quarter of the
  texture is upsampled and loses nothing.
- CLUT16 costs a 15-colour quantization per source texture.
- Only valid for geometry whose UVs are link-time constants. They are: UVs live
  in `Vtx` rows in `.cart_rodata` and are never written. Skeletal animation
  moves positions, not UVs.
- Affine/bilinear warp is unchanged — the bake fixes *which texel*, not *how
  VDP1 interpolates between corners*.

**Unlocks:** every wrap/mirror/clamp/mask/shift/scale case, exactly, at zero
runtime cost. Arbitrary UVs, exactly. **Zero new GBI decoding in the frontend.**
Full coverage of BOB terrain in one step. A directly reusable in-tree tool
(`bake_castle_uv.py`) and a directly reusable in-tree runtime
(`saturn_texture_residency.h`).

**Forecloses:** nothing. Option A can be layered on later as a per-primitive
classification (§4, Option C) without discarding any of Option B's machinery.

### Option C — Hybrid classifier: direct-bind where the UVs allow, bake otherwise

An offline pass classifies each render primitive:

- **DIRECT** — UVs are the corners of exactly one texture period (up to
  rotation/flip). Bind the shared texture. Full resolution, no extra VRAM.
- **BAKE** — anything else. One resampled tile.
- **FLAT** — over budget, or too small on screen to be worth a sprite. Keep the
  existing Gouraud polygon.

**Cost:** the union of A and B's tooling, plus a classifier and a two-mode emit
path. Strictly more machinery than either alone.

**Unlocks:** the best fidelity/VRAM point available, and a budget knob
(demote BAKE->FLAT, or shrink tile size, until the bank fits) that neither pure
option has.

**Verdict:** the right destination, the wrong starting point — and, given
§2.1a, a much smaller prize than it looked before the measurement. DIRECT
covers 5.3% of BOB. The genuinely valuable half of the classifier is not DIRECT
at all but the **tile-size** decision (§4.1): choosing N per primitive from its
UV span and screen importance, which is Option B with a budget, not Option A
with a fallback. Build B, then add tile-size classification, and treat DIRECT as
a 29-quad footnote.

### Rejected without further work

- **Perspective-correct texturing by subdivision to near-pixel size.** The
  command budget cannot pay for it. BOB already runs 1,365-1,431 triangles per
  frame (`sourceboot/main.c:198-201`) against a 2,048-command arena.
- **VDP2 for the ground plane.** A rotation-scroll background could carry a
  flat floor cheaply, but BOB's terrain is not a plane, and this would fork the
  renderer for one level.
- **Runtime texture conversion.** RGBA16->RGB1555 lane swapping 1,101 tiles per
  level load on an SH-2 that currently runs the game at ~1 fps. Do it offline.

### 4.1 The residual hard problem: tiling density

Option B is recommended, but it is not free of consequences and this is the one
that matters.

**Addressing is exact; sampling rate is not.** `texture_coordinate`
(`bake_castle_uv.py:182-199`) resolves wrap by modulo, clamp by min/max, and
mirror by fold, so a UV of 872.5 texels into a 32-texel pattern lands on
*exactly* the right source texel. What the baker cannot fix is that
`sample_quad`/`sample_triangle` take **one point sample per destination texel**.
A primitive whose UVs span `k` texture periods therefore squeezes `k` periods
into an `N x N` tile: `N / k` destination texels per period. With `N = 16` and
`k = 4` that is 4 texels per 32-texel period — an 8:1 undersample, which is
aliasing, not a texture.

#### The measured distribution of `k`

Same walk as §2.1a, `k = max(span_s / tile_width, span_t / tile_height)` per
triangle, over all 1,101. The `k <= 1` count reproduces §2.1a's 349 exactly,
which cross-validates both walks.

| bucket | triangles | % | cumulative | share of **world surface area** |
|---|---:|---:|---:|---:|
| `k <= 0.5` | 91 | 8.3% | 8.3% | 1.3% |
| `0.5 - 1` | 258 | 23.4% | 31.7% | 3.3% |
| `1 - 2` | 218 | 19.8% | 51.5% | 11.3% |
| `2 - 4` | 250 | 22.7% | 74.2% | 21.4% |
| `4 - 8` | 203 | 18.4% | 92.6% | **39.6%** |
| `8 - 16` | 57 | 5.2% | 97.8% | 19.2% |
| `16 - 32` | 24 | 2.2% | 100.0% | 3.9% |
| `> 32` | 0 | — | — | — |

Percentiles: **p50 = 1.97, p75 = 4.16, p90 = 6.88, p95 = 11.28, p99 = 21.96,
max = 29.76.** Mean 3.32. The axis split is even (S drives `k` on 529
triangles, T on 497), so no anisotropic tile shape fixes it.

Three things in that table are load-bearing:

1. **The median triangle spans two texture periods.** A uniform 16x16 tile gives
   the median primitive 8 destination texels per 32-texel period — a 4:1
   undersample at the *median*, before considering the tail.
2. **The area weighting is much worse than the count weighting.** High-`k`
   triangles are the physically large ones — the tiled ground and hill planes.
   Only **4.6% of BOB's surface area sits at `k <= 1`** versus 31.7% of triangle
   count, and 62.7% of surface area is at `k > 4`. (Caveat: this is *world*
   area, computed from the `Vtx` positions; it is not a screen projection, and a
   distant hillside weighs the same as near ground. It is a proxy, and it is
   labelled as one.)
3. **The `k <= 1` group is not free.** Its median `k` is 0.92 and 52% of it sits
   in the top eighth-bin (0.875-1.0), i.e. those triangles use nearly the whole
   texture period. Only **91 triangles (8.3%)** have `k <= 0.5`, the population
   for which a 16x16 tile genuinely loses nothing. The intuition "they fit in one
   period, so baking is lossless" is wrong.

Destination texels per source period, by uniform tile size:

| N | `>= 8` texels/period (`k <= N/8`) | `>= 4` texels/period (`k <= N/4`) |
|---:|---:|---:|
| 8 | 349 (31.7%) | 567 (51.5%) |
| 16 | 567 (51.5%) | 817 (74.2%) |
| 24 | 724 (65.8%) | 956 (86.8%) |
| 32 | **817 (74.2%)** | **1,020 (92.6%)** |

**No single uniform `N` is right**, and going 16 -> 32 costs 4x the VRAM for
+22.7 points at the 8-texel bar.

The tail (`k > 4`, 284 triangles) is unusually tractable: **154 of the 284 sit
in just two display lists** (`bob_seg7_dl_07003CA8` on `generic_09005800`, 88;
`bob_seg7_dl_0700D910` on `generic_09009800`, 66), and the 24 extreme cases
(`k > 16`) are strongly anisotropic strips — the worst is
`ps = 29.76, pt = 0.97`, a 952-texel S span against a single-period T span.
Those are path/ribbon geometry and would subdivide cleanly along one axis.

Four responses exist and they are not equally good:

1. **Subdivide until `k <= 1`.** SlaveDriver's `WALLS.C:1042-1092` does this
   structurally — a `height x width` grid of sub-quads, one tile per cell.
   Castleviewer's `adaptive_subdivide_triangle` (`:274-324`) does a weaker
   version keyed on world-space edge length. **Cost is quadratic in `k`**: a
   `k = 10` triangle becomes ~100 primitives and ~100 tiles. Viable for small
   `k`, catastrophic for the tail.
2. **Larger `N` for large `k`.** Buys a linear factor at quadratic VRAM cost,
   and `N` is capped at 504 by CMDSIZE and long before that by the budget in
   §5. Useful for `k` in the low single digits, useless beyond.
3. **Supersample in the baker** (average several source samples per destination
   texel) instead of point-sampling. This converts aliasing into blur, which is
   the right trade — but note the limit: as `k` grows, a supersampled tile
   converges on the **mean colour of the texture**, which is very close to what
   the existing untextured Gouraud path already draws. Beyond some `k`,
   texturing a primitive buys nothing.
4. **Leave high-`k` primitives flat.** Falls out of (3): if the honest output
   for large `k` is the texture's mean colour, emit a Gouraud polygon tinted by
   that mean and save both the tile and the sprite cost. This is the FLAT class
   of Option C, and §2.1a is the argument that it is a principled choice rather
   than a cop-out.

**Recommendation, revised by the measurement above:**

- **Supersampling in the baker is mandatory, not optional.** At a median `k` of
  1.97 and p90 of 6.88, point sampling produces noise. Average
  `ceil(k * N_source / N)` source samples per destination texel.
- **Milestone 1 ships two tile classes, not one:** `16x16` for `k <= 2`
  (567 triangles) and `32x32` for `k > 2` (534 triangles), both CLUT16. Budget:
  `567 x 128 + 534 x 512 = 345,984 bytes` — 77.5% of the 446,432 free (§5), and
  it fits with 99,424 bytes to spare after commands, Gouraud and CLUTs. That
  gives `>= 8` destination texels per period to everything up to `k = 4` (74.2%
  of triangles) and `>= 4` up to `k = 8` (92.6%).
  Two classes is deliberately the *smallest* variable-size scheme that answers
  the measurement, and it is the same shape as SlaveDriver's class pool, which
  shipped with exactly 32x32 and 64x64 classes (`PIC.C:84-99`).
- **The 24 `k > 16` strips get nothing that works.** Even a 64x64 tile gives
  them 2-4 texels per period. Leave them on the existing Gouraud path in
  milestone 1 (response 4) and revisit with single-axis subdivision in
  milestone 2. 24 of 1,101 is a visible but bounded hole, and pretending
  otherwise would be dishonest about what milestone 1 delivers.
- **Do not add general subdivision in milestone 1.** The point is to make the
  affine and sampling artefacts observable before choosing a mitigation.

---

## 5. VDP1 VRAM budget, measured

**Method.** `VDP1_VRAM_SIZE` is `0x00080000` = **524,288 bytes**
(`vdp1/vram.h:26`). The frame buffers are *separate* memory (`VDP1_FB`,
`VDP1_FB_SIZE` `0x40000` x `VDP1_FB_COUNT` 2, `vram.h:28-30`) and are **not** carved from it —
so the whole 512 KiB is available for command tables, character patterns,
Gouraud tables and CLUTs. `vdp1_vram_partitions_set`
(`third_party/libyaul/libyaul/scu/bus/b/vdp/vdp1_vram.c:21-77`) reserves one
command table at the head and then bump-allocates in the fixed order
**cmdt -> texture -> gouraud -> clut -> remaining**. `sizeof(vdp1_cmdt_t)` is 32
(static_assert, `cmdt.h:217`); `sizeof(vdp1_gouraud_table_t)` is 8
(`vram.h:32-34`); `sizeof(vdp1_clut_t)` is 32 (`vram.h:55-58`).

**Current sourceboot partition** (`sourceboot/main.c:240-242`:
`vdp1_vram_partitions_set(2048, 0, 1536, 0)`):

| Region | Bytes | Note |
|---|---:|---|
| Reserved head command table | 32 | libyaul |
| Command tables (2,048) | 65,536 | matches `SOURCEBOOT_VDP1_COMMAND_CAPACITY` |
| **Texture** | **0** | the gap this design closes |
| Gouraud (1,536) | 12,288 | `SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES` |
| CLUT (0) | 0 | |
| **Free (`remaining_size`)** | **446,432** | **436.0 KiB, 85.2% of allocatable** |

`524,288 - 32 - 65,536 - 12,288 = 446,432.`

**What 446,432 bytes admits.** Character-pattern width must be a multiple of 8
and each pattern must start on an 8-byte boundary, so every row below is legal.

Uniform-size reference (the recommended layout is the two-class table below):

| Layout | Bytes/tile | 1,101 tiles | % of free |
|---|---:|---:|---:|
| 8x8 CLUT16 (4bpp) | 32 | 35,232 | 7.9% |
| 8x8 RGB1555 | 128 | 140,928 | 31.6% |
| **16x16 CLUT16** | **128** | **140,928** | **31.6%** |
| 16x16 colour-bank 8bpp | 256 | 281,856 | 63.1% |
| 24x24 CLUT16 | 288 | 317,088 | 71.0% |
| 16x16 RGB1555 | 512 | 563,712 | **126.3% — does not fit** |
| 32x32 CLUT16 | 512 | 563,712 | **126.3% — does not fit** |

For comparison, the **shared-texture** bank (Option A) is 38,912 bytes for BOB
and 83,968 bytes for BOB+Mario — 8.7% and 18.8% of free.

**Proposed milestone-1 partition** — two tile classes, sized by the measured
`k` distribution in §4.1 rather than by a uniform guess:

| Class | Criterion | Triangles | Bytes/tile | Total |
|---|---|---:|---:|---:|
| `16x16` CLUT16 | `k <= 2` | 567 | 128 | 72,576 |
| `32x32` CLUT16 | `2 < k <= 16` | 510 | 512 | 261,120 |
| (none — Gouraud) | `k > 16` | 24 | 0 | 0 |
| **Texture partition** | | **1,077** | | **333,696** |

```
vdp1_vram_partitions_set(2048,            /* 65,536 B commands, unchanged    */
                         333696,          /* 567 x 128 + 510 x 512 tiles     */
                         1536,            /* 12,288 B gouraud, unchanged     */
                         32);             /* 1,024 B = 32 CLUTs (18 needed)  */
/* used 412,544 of 524,256 -> 111,712 B (109.1 KiB) still free */
```

That is tight but real, and it leaves the headroom milestone 2 needs to promote
some of the `4 < k <= 8` population to 64x64 in exchange for demoting elsewhere.
If it proves too tight in practice the first knob is `cmdt_count`: real frames
run 1,365-1,431 triangles against a 2,048-command arena, so dropping to 1,600
returns 14,336 bytes without touching fidelity.

`333,696 mod 8 == 0`, so `gouraud_base` stays 8-byte aligned and the guard at
`sourceboot/main.c:264-276` — which exists precisely to catch this — passes.
That guard's own comment (`:218-238`) is the instruction this design follows.

**Sanity check against shipped Saturn software.** SlaveDriver's in-game
partition (`SPR.C:65-94`, `SRUINS.C:1879`
`EZ_initSprSystem(1448, 4, 1224, 240, 0x8000)`) leaves **411,840 bytes** for
character patterns after two 46,336-byte command banks, 128 bytes of CLUT and
two 9,792-byte Gouraud banks. Sonic Z-Treme's
(`ZTE_DEF.H:63,88`) leaves ~425,920 bytes with CLUTs at `0x7A960`. Both are the
same order as the 446,432 measured here. The proposed 333,696-byte texture
partition is 81% of what SlaveDriver spent, and SlaveDriver was streaming its
textures through an LRU (`PIC.C:250-401`) rather than holding a whole level
resident. That is the honest read: this design is spending a shipping engine's
worth of texture VRAM on one level's worth of instanced tiles, and it works only
because BOB fits. A level with materially more geometry will need either
milestone 2's budget allocator or SlaveDriver's eviction model.

---

## 6. Interaction with quad merging

### 6.1 Why textured triangles are refused today

`tools/saturn/dl_rigid_groups.py:250-292` tracks the live texture binding
(`gsDPSetTextureImage`, `gsDPLoadTextureBlock`, cleared by
`gsSPTexture ... G_OFF`) and marks any triangle emitted while a binding is
active `unsafe`, reason `textured`. The comment says so plainly: it is not an
unmodelled construct, it is a deliberate deferral to
`SATURN_MESH_IR.md`'s current limits.

Consequence, measured: **100% of every ineligible key in the entire generated
map is ineligible for this one reason**, and BOB's terrain is excluded from the
map entirely.

### 6.2 Would textured quads become mergeable? Yes — under four conditions

A merged quad is **one** VDP1 command with **one** character pattern. Under
Option B that pattern is baked from the quad's four combined UVs by
`sample_quad`, which is the same function castleviewer already uses to produce
its **195 merged textured quads**. So the mechanism is proven in-tree. The
conditions are:

1. **Identical texture state.** Both triangles must share the same
   `gsDPSetTextureImage`, `gsDPSetTile`, `gsDPSetTileSize`, `gsSPTexture` scale
   and combiner. For BOB this is nearly free: §2.1 measured that a leaf display
   list binds exactly once and then drains, so *any* two triangles in the same
   leaf share it by construction. The change is to make texture state part of
   the pairing **bucket key** in `dl_rigid_groups.py` instead of a veto.
2. **The existing geometric gates.** Projected convexity and normal alignment
   are already enforced by `quad_pairing.py` and are unchanged by texturing.
3. **A consistent corner cycle in UV space.** The quad map's weld is
   *attribute-exact* — two `Vtx` rows are one vertex only when the whole row
   matches, UV included (`quad_map.py:135-144`). So the shared edge's UVs agree
   in both triangles by construction and the boundary cycle derived from
   positions is automatically a valid cycle in UV space. No new check needed.
4. **Bounded bilinear-vs-piecewise-affine error.** This is the one genuinely
   new correctness question. The RDP maps each triangle by its own affine map;
   VDP1 maps the merged quad by a single bilinear map. The two agree exactly
   iff the quad's UVs are an affine image of its positions — i.e. for
   parallelogram-like quads. For strongly non-parallelogram quads there is a
   mismatch that peaks near the former diagonal. Castleviewer ships 195 baked
   textured quads and the visuals were accepted, so the error is evidently
   tolerable in practice — but those quads came out of a BSP that produces
   convex, mostly-regular fragments, which is a friendlier population than the
   matcher's. The matcher should be given a weight term preferring
   near-parallelogram pairs, and the failure mode must be looked for in the
   visual gate.

### 6.3 The size of the prize

The texture veto is the *only* reason BOB contributes nothing today. Two
independent in-tree rates calibrate what unblocking it would buy:

| Source | Rate | How derived |
|---|---:|---|
| `mario_geo_body` (quad map, untextured keys) | **20.6%** | 260 quads / 1,265 keys |
| `cannon_barrel_geo` | 25.8% | 16 / 62 |
| `cannon_base_geo` | 21.1% | 8 / 38 |
| castleviewer BSP-output quads (textured) | 19.7% | (888 - 713) / 888 |

Applying ~20-26% to BOB's 1,101 terrain triangles is a **calibrated estimate,
not a measurement**: roughly **220-285 commands saved**, leaving ~815-880
primitives. Terrain is flatter and more regular than Mario, so the true rate may
well be higher; it may also be lower, because terrain triangles are large and
the projected-convexity gate is stricter on large primitives. Measure it by
running `quad_map.py` against BOB's geo *before* committing to the number. Two
compounding wins:

- Terrain would go from **zero** command savings to roughly a fifth.
- The tile bank would shrink by the same fraction — ~250 fewer tiles at 128
  bytes is ~32,000 bytes of VDP1 VRAM returned.

**The 9.42% whole-frame figure was independently re-derived for this note.**
Decoding `probe_window.data` (248 bytes at `0x060BE918`) from the committed
capture `docs/saturn/evidence/reports/e2-sourceboot-quadmerge-freeroam-2026-07-26.json`
(commit `312cc49`) as `sm64_saturn_fast3d_profile_t`, big-endian:

| Counter | Value |
|---|---:|
| `triangles_transformed` | 2,311 |
| `reject_backface` / `reject_near_far` / `reject_degenerate` | 1,181 / 214 / 3 |
| `triangles_emitted` (resolved) | **913** |
| `quads_merged` | **86** |
| `triangles_vdp1_emitted` | **827** |
| `quad_map_mismatch` | **0** |
| `quad_pairs_declined` | 97 |
| `texture_commands` (decoded: none) | **507** |
| `rdp_commands` (decoded: none) | **430** |

`2311 - (1181 + 214 + 3) = 913` and `913 - 86 = 827` both close, which
validates the field mapping. `86 / 913 = 9.42%` exactly. Every one of those 86
merges is Mario or a cannon piece; terrain contributes **zero**. Adding terrain
at ~20-26% of its 1,101 triangles would be several times the current whole-frame
win.

The same decode gives the size of what the frontend currently discards:
**507 texture commands and 430 other RDP commands are walked and counted every
frame** and none is decoded (§2.5).

### 6.4 Ordering

**Bake against the render-primitive set the quad map produces, from day one**,
even though at milestone 1 that set is all triangles. Then unblocking textured
merging (milestone 3) changes only which primitives exist, not the key space or
the runtime. Getting this ordering wrong means rebuilding the bank's index
scheme later.

---

## 7. Recommended plan and scope

### Milestone 1 — BOB terrain textured (recommended first step)

**In scope**

1. Widen the VDP1 texture partition at `sourceboot/main.c:240` from `0` to the
   baked bank size, with `clut_count` for the CLUTs. Keep it a multiple of 8.
2. A new host tool (`tools/saturn/bake_source_uv.py`, name provisional) that:
   - parses `levels/bob/**/model.inc.c` with the existing Fast3D vertex-cache
     semantics from `quad_map.py:resolve_display_list_vertices`, **extended to
     carry `Vtx.tc[2]`** (it currently keeps position only) and with a
     **balanced-paren tokenizer** — `dl_rigid_groups.parse_display_lists`'
     `r"(gs\w+)\(([^;]*?)\)"` stops at the first `)` and silently truncates
     `gsDPSetTileSize(0,0,0,(32-1)<<F,(64-1)<<F)` to `0, 0, 0, (32-1`, losing
     the tile dimensions. Both were found the hard way while measuring §2.1a;
     see also that `gsDPSetTile`'s argument order is `(..., cmt, maskt, shiftt,
     cms, masks, shifts)` — **T before S** — which is the exact axis-swap bug
     `bake_castle_uv.py:205-208` already records having shipped once;
   - resolves the full tile state per binding by reusing
     `bake_castle_uv.py:sample_raw`'s logic verbatim;
   - computes `k` per primitive and assigns it a tile class (§4.1);
   - resamples one tile per render primitive with
     `sample_quad`/`sample_triangle` from the same module, **supersampled** at
     `ceil(k * source_dim / N)` source samples per destination texel;
   - quantizes one 16-colour CLUT per source texture with
     `bake_castle_uv.py:quantize_clut16`;
   - emits a generated `.c`/`.h` pair keyed `(display_list, list_ordinal)` in
     the same shape as `saturn_quad_map.c` — carrying **byte offset and tile
     class**, not just an index, since tiles are no longer uniform — plus a JSON
     report that records the per-primitive `k` and the class histogram.
3. Frontend: extend the per-list bind to also bind a tile row, and carry a tile
   index (plus a "no tile" sentinel) into `sm64_saturn_resolved_triangle_t`.
   Reuse `quad_slot_generation`'s discipline; do not add a second one.
4. Emit path: when a resolved primitive has a tile, issue
   `vdp1_cmdt_distorted_sprite_set` with `color_mode = VDP1_CMDT_CM_CLUT_16`,
   `end_code_disable = true`, `char_base` = partition base + the tile's byte
   offset, `char_size` = the tile's class dimensions, `color_mode1_set` = the
   tile's CLUT; otherwise the existing polygon path, unchanged. **Keep `cc_mode = VDP1_CMDT_CC_GOURAUD` and
   the existing per-primitive Gouraud table on the textured path too**: Gouraud
   is CMDPMOD bit 2 and applies to sprites as well as polygons, so one command
   carries texture *and* per-corner shade. It is an additive correction, not the
   multiply `G_CC_MODULATERGB` asks for (21 of BOB's 26 bindings), so it is an
   approximation — but it is a single-command approximation, which is strictly
   better than castleviewer's two-command polygon-then-sprite pattern
   (`castleviewer/main.c:1036-1066`). Verify against a flat-textured variant in
   the visual gate; if additive shade reads badly, the fallback is
   `CC_REPLACE` plus baking the shade into the tile, which costs the dynamic
   lighting.
5. Bank residency: bank lives in `.cart_rodata`, staged cart -> internal-WRAM
   ring -> `sm64_saturn_texture_residency_upload`, copying
   `castleviewer/main.c:1136-1183` including its no-cart fallback. Carry a
   generation stamp on the bank binding from day one, per
   `SEGMENT_ADDRESSING_DECISION.md` §8.2.
6. New profile counters (§8).

**Explicitly out of scope**

- Perspective correction of any kind.
- Mario, and every actor. Mario's dominant textured combiner
  (`G_CC_BLENDRGBFADEA`, 36 bindings) has no VDP1 expression, his metal path
  needs `G_TEXTURE_GEN`, and his eyes are switch-case selected. None of that is
  needed to see textured ground.
- Unblocking textured quad merging (milestone 3).
- Runtime `G_SETTIMG` / `G_SETTILE` / `G_LOADBLOCK` decoding. Nothing in
  milestone 1 needs it, and the addressing decision (§8.1) says not to build
  wrappers speculatively.
- Fog. BOB terrain runs `G_RM_FOG_SHADE_A` with `gsSPFogPosition(980, 1000)`;
  the frontend ignores it today and should keep doing so.
- Texture animation, mip/LOD, streaming, eviction, and multi-level residency.
- The IA16 texture's intensity/alpha semantics beyond binary transparency.
- `adaptive_subdivide_triangle` and any geometry subdivision. Milestone 1 ships
  *without* it so the affine and sampling artefacts are observable and can be
  measured before a mitigation is chosen. Keep the parameter plumbed, default
  it off.
- **The 24 triangles at `k > 16`** (§4.1). They stay on the Gouraud path. This
  is a deliberate hole, not an oversight, and the capture should be read knowing
  where it is.

**Why this is the right first milestone.** It textures 1,077 of BOB's 1,101
terrain triangles in a free-roam capture — a result the human eye can judge in
one screenshot. It touches the frontend in exactly two places (bind and emit)
and adds no GBI decoding. Every hard sub-problem it solves (arbitrary UV,
wrap/clamp/mask/shift, transparent-code canonicalisation, CLUT quantization,
cart staging, `(display_list, ordinal)` keying) already has working in-tree
code. And it is independent of both the streaming sub-project and the
quad-merge work, so it cannot be blocked by either.

**Why not narrower.** Texturing only the `k <= 1` population (349 triangles,
4.6% of world surface area) would teach nothing about the problem that actually
governs SM64 terrain on VDP1 — tiled wrap. **Why not wider.** Adding Mario means
adding a combiner VDP1 cannot express and a switch-case-selected actor; adding
subdivision means choosing a mitigation before the artefact has been seen.

### Milestone 2 — Fidelity and budget

In decreasing order of measured value:

1. **Single-axis subdivision for the `k > 8` tail** (81 triangles, of which 24
   are the `k > 16` hole milestone 1 leaves open). §4.1 measured these as
   strongly anisotropic — the worst is `ps = 29.76, pt = 0.97` — so splitting
   along the long axis only is cheap: an 8-way split of a `k = 30` strip costs
   +7 commands and brings each fragment to `k < 4`. Applied to all 81 that is
   roughly +300 commands, against ~250 saved by milestone 3's merging.
2. **A real budget allocator**, generalising milestone 1's two classes to a
   full class pool sized by `k` *and* by a screen-importance proxy better than
   world area. SlaveDriver's `initPicSystem` (`PIC.C:201-231`, five geometries,
   `{28, 31, 1, 10, 12}` slots at `SRUINS.C:1903`) is the model for the
   allocator; `bake_castle_uv.py`'s `--max-tiles` is the model for the policy.
3. **8bpp colour-bank versus CLUT16** (O6). At 2x the bytes per tile it is only
   affordable once (1) has reduced the tail, but it removes the 15-colour
   quantization from the noisiest textures.
4. **`adaptive_subdivide_triangle`** against whatever affine warp the
   milestone-1 capture actually shows.
5. **The DIRECT class** for the measured 29 quads (§4, Option A). Last, and
   only because it is nearly free once the classifier exists. 5.3% coverage is
   not a strategy.

### Milestone 3 — Textured quad merging

Change `dl_rigid_groups.py`'s texture veto into a bucket key (§6.2), add BOB's
terrain to `QUAD_MAP_ACTOR_ARGS`, and have the baker consume the resulting
render-primitive set. Add a near-parallelogram preference to the matcher weight.

---

## 8. Success criteria and verification

Following this project's evidence discipline. The order matters: nothing
downstream is trusted until the step above it passes.

1. **Host tests** (`tools/saturn/test_tools.py`). Byte-exact fixtures for the
   baker covering, at minimum: a WRAP/WRAP 32x32 binding, each of the three
   CLAMP variants BOB actually uses, the 32x64 binding, the IA16 binding, a
   triangle (repeated-C) and a quad, and a UV span exceeding one period. Assert
   the transparent-code canonicalisation (`0x0000`, never a bit-15-clear
   nonzero word) and the RGB1555 lane order (B:14-10, G:9-5, R:4-0).
2. **Mutation testing** of those tests. Flip the S/T axis order in
   `sample_raw` (the exact bug `bake_castle_uv.py:205-208` records), swap
   clamp for wrap, drop the majority-alpha rule, transpose two corner weights
   in `distorted_sprite_weights`. Each mutation must fail at least one test.
   \>20% survival means the fixtures are not discriminating.
3. **Build-time assertions.** A `_Static_assert` that the generated bank's
   total byte size (summed over classes, not `count x uniform_size`) is `<=` the
   texture partition size, that every tile's byte offset is 8-byte aligned
   (CMDSRCA granularity), that every class width is a multiple of 8 (CMDSIZE),
   and that the partition size is a multiple of
   `sizeof(vdp1_gouraud_table_t)`. `verify-sourceboot`
   must still pass its entry-point and `.cart_rodata` VMA checks.
4. **Runtime counters**, added to `sm64_saturn_fast3d_profile_t` and read from
   a live capture:
   - `texture_tiles_bound` — expected to equal the textured primitive count;
   - `texture_tile_missing` — a resolved primitive whose ordinal has no tile;
     expected nonzero only for untextured geometry, and its value must match
     the offline report's untextured count;
   - `texture_tiles_by_class[2]` — the 16x16 and 32x32 counts, which must match
     the bake report's class histogram exactly; a mismatch means the runtime and
     the generated table disagree about tile geometry, which shows as garbled
     texture rather than as a crash;
   - `texture_bank_overflow` — must read **0**;
   - `texture_map_mismatch` — a corrupt or out-of-range tile index; must read
     **0**, and like `quad_map_mismatch` it is a correctness fault, not a knob;
   - existing `gouraud_bank_overflow` and `quad_map_mismatch` must not regress.
5. **Live capture** (`tools/saturn/capture_hwtest.py`) on BOB free roam:
   framebuffer PNG plus a **`probe_window` read of
   `sm64_saturn_fast3d_profile_t`** — not the `telemetry` block, which does not
   decode for this target (§11) — committed under
   `docs/saturn/evidence/reports/`. Report command count, VDP1 draw-end time
   and the per-phase timings before and after, so the fill-rate question in
   §9-O2 gets a number rather than an argument. Two tooling caveats: `exec.run_for`
   silently no-ops above Ymir's 3,600-frame cap (fixed in `c28980a`, but
   re-check the frame count in the report), and the probe window must be sized
   to the *new* `sizeof(sm64_saturn_fast3d_profile_t)` after the counters in
   item 4 are added, or the tail counters read as truncation.
6. **Human visual gate.** The user's eyes are the final arbiter. Terrain must
   read as Bob-omb Battlefield — grass, dirt and the mountain path recognisable,
   no folded or mirrored tiles, no black seams, no scanline truncation from a
   missed end code. **No TIMELINE entry is written before the user confirms.**
7. **Regression gate.** Keep the accepted BOB capture as the visual regression
   baseline for every subsequent texture change, mirroring
   `CARTRIDGE_ASSET_POLICY.md` gate 5.

---

## 9. Open questions

**O1 — RESOLVED, and it changed milestone 1.** The per-triangle `k`
distribution was measured (§4.1): p50 1.97, p90 6.88, max 29.76, with only 4.6%
of world surface area at `k <= 1`. The uniform 16x16 tile this note originally
proposed undersamples the median primitive 4:1, so milestone 1 now ships two
tile classes and leaves 24 triangles untextured. What remains open is the
*screen*-space version of the same question: world surface area is a proxy, and
a proper projected-area weighting (from a real free-roam capture rather than
from geometry alone) could shift the class thresholds either way. Take it from
milestone 1's capture rather than guessing.

**O2 — VDP1 fill-rate cost is unmeasured on this target.** A textured distorted
sprite's cost is driven by `W x H` texels, not by projected area, so the
recommended two-class layout is `567 x 256 + 510 x 1,024` = **~667,000 texel
operations per frame** if every terrain primitive is visible — roughly 9x a
320x224 screen. The 32x32 class alone is 78% of that, which is the specific
number to watch. Castleviewer's phase probe
(`RENDERER_PRIOR_ART.md:155-163`) found the master SH-2, not VDP1 draw
completion, to be the bottleneck — but that was ~1,032 mostly-flat commands.
Nobody has measured VDP1 draw-end time with a majority-textured list, and the
painter-algorithm depth bucketing in `saturn_fast3d_vdp1_emit.c:34-41` means
there is no early rejection — every command drawn is every command paid for.
Measure it in the milestone-1 capture before treating texturing as free. If VDP1
turns out to be the bound, the first lever is demoting the 32x32 class, which
costs fidelity on exactly the primitives §4.1 says need it most.

**O3 — Magnification behaviour.** Whether VDP1 replicates texels or leaves gaps
when a small character pattern is stretched over a large screen quad is *not*
established by this investigation. This matters much more for BOB than for
castleviewer: BOB's terrain triangles are large on screen. Castleviewer's
accepted captures at 16x16 are weak positive evidence at castle scale only.
`adaptive_subdivide_triangle` is the existing mitigation if the artefact is
real — which is why milestone 1 deliberately ships without subdivision, so the
artefact is observable rather than pre-emptively hidden.

**O4 — Bilinear-vs-piecewise-affine error on merged quads (§6.2 condition 4)
has no bound.** Castleviewer ships 195 such quads and they were accepted, but no
one has quantified the worst case. Before milestone 3, compute the maximum UV
deviation over the candidate set offline and use it as a matcher rejection
threshold, rather than discovering it visually.

**O5 — CLUT16 versus 8x8 RGB1555 is genuinely undecided.** They cost the same
128 bytes per tile. CLUT16 gives 16x16 spatial resolution with 15 colours;
8x8 RGB1555 gives full colour at a quarter of the spatial resolution.
Castleviewer chose CLUT16 for nine castle textures; BOB's grass and mountain
textures are noisier and may quantize worse. This is a build parameter and
should be decided by the visual gate, not by argument. If median-cut proves
inadequate, `tools/n64graphics_ci_dir/exoquant/` (MIT, Dennis Ranke) is already
in-tree.

**O6 — Mixing colour-bank and RGB1555 sprites in one frame.** Milestone 2's
8bpp colour-bank option puts palette-code pixels (bit 15 clear) and direct-RGB
pixels (bit 15 set) in the same 16-bit frame buffer, which is what VDP2 sprite
types 0-7 exist for — but this target currently sets all eight sprite priorities
to 7 (`sourceboot/main.c:121-123,150-152`) without configuring a sprite type,
and the interaction with priority/colour-calculation bits taken from the palette
code is unexamined. Prove it on a small target before adopting it.

**O7 — RESOLVED, and it changed the design.** The UV corner-alignment fraction
was measured (§2.1a): 5.3% direct-bindable, 29.1% under the most generous
reading. Option A is dead as a strategy. Recorded here rather than deleted
because the *method* — walking the real geo layout with a UV-carrying vertex
cache — is the same walk the milestone-1 baker needs, and because the negative
result is the load-bearing evidence for the recommendation.

**O8 — Whether the 1,101-tile bank should be per-area rather than per-level.**
BOB has six area sub-directories under `levels/bob/areas/1/`. If a future level
needs more tiles than fit, per-area banks with the addressing decision's
fixed-VMA slot mechanism are the natural refinement. Not needed for BOB.

**O9 — Fog.** 8 of BOB's terrain bindings run `G_RM_FOG_SHADE_A` with
`gsSPFogPosition(980, 1000)`. The frontend ignores fog entirely today, so
distant terrain will read as too saturated. VDP1's `CC_HALF_LUMINANCE` or a
per-depth Gouraud bias could approximate it. Out of scope, but it will be
visible in the milestone-1 capture and should not be mistaken for a texture bug.

---

## 10. Prior-art findings

Licences and pins verified against the working copies under `work/upstream/` and
`tools/`. Reuse modes are proposals; none has been enacted. Any adoption must
update `THIRD_PARTY_LICENSES.md` and `docs/saturn/PROVENANCE.md` per the
maintenance rule at `THIRD_PARTY_LICENSES.md:165-167`, and GPL-derived code is
confined to `src/port/saturn/gpl/` per `PROVENANCE.md:419-423`.

| Upstream | Pinned SHA | Licence | Files inspected | Relevance | Proposed reuse mode |
|---|---|---|---|---|---|
| `Lobotomy-Software/SlaveDriver-Engine` | `a8986591557b6e680550d3c23970284d3b38ff8f` | GPL-3.0-or-later (`LICENSE.txt`; `README.md:1-20` SPDX). Already authorized, `THIRD_PARTY_LICENSES.md:119-123` | `SPR.C:65-119,165-175,202-260`, `SPR.H:21-23,50,79-93`, `PIC.C:84-99,201-231,250-401,359-388,611-668`, `WALLS.C:1042-1092,2663-2666`, `SRUINS.C:1879,1890-1903`, `UTIL/MAKETEX.C:36-40` | **The strongest single reference.** A shipped Saturn engine that (a) leaves 411,840 B for character patterns after a 1,448-command double-banked table, (b) uses `FUNC_DISTORSP` for *every* 3-D quad, (c) has **no UVs at all** and solves it by geometry subdivision + a 4-way corner permutation array + flip bits — i.e. Option A, proven, (d) stages commands and Gouraud tables in WRAM and block-DMAs them, (e) uses VDP2 CRAM colour banks as a free per-object lighting term (`CMDCOLR = light << 8`), (f) confirms the RGB1555 lane order | **Pattern-only** for the VRAM budget model, the corner-permutation strategy and the class-based slot pool. If the WRAM-staged block-DMA command flush is ever adapted, it goes in `src/port/saturn/gpl/` with GPL notices intact — precedent `slavedriver_dma_queue.c` |
| `Maxime-XL2/SONIC-Z-TREME` | `cff75451c1616aac1236fc2b44223902b55c706b` | GPL-3.0 text, **contradicted by a no-sale clause in `README.md:6-12`**; vendors proprietary Sega SGL/SBL. Recorded contested at `PROVENANCE.md:382-385` | `ZTE/ZTE_DEF.H:61-94`, `ZT_SPRITES.c:15-54`, `ZT_LOADING.c:47-74,92,103-115,208-261`, `ZT_RENDERING.c:158,283-285,672` | Second worked VDP1 partition (~425,920 B texture area, CLUTs at `0x7A960`, 693-CLUT ceiling). The `((width & 0x1f8) << 5) \| height` CMDSIZE pack and the `+7)/8` CMDSRCA rounding are the two things easiest to get wrong. One CLUT per texture, no dedup. CMDSRCA row-scrolling as a substitute for a V-offset UV | **Behaviour study only** — the licence contradiction makes copying unsafe regardless of this project's GPL authorization. Matches the existing record |
| `queueRAM/sm64tools` `n64graphics` (vendored) | in-tree at `tools/n64graphics.c` | **MIT**, `tools/sm64tools.LICENSE`, (c) 2015-2018 queueRAM | `n64graphics.c:17-22,630-641,673-679`, `n64graphics.h:34-87` | RGBA16 / IA / I decode and the `SCALE_5_8`/`SCALE_8_5` bit-depth macros — the exact conversions a Saturn RGB1555 converter mirrors. Already the tool that produces every `.inc.c` texture array in this build (`Makefile:678-681`) | **Direct-copy available** (MIT, attribution preserved). Not needed for milestone 1 — the Python baker reads PNGs, not raw N64 data |
| `n64graphics_ci` + `exoquant` (vendored) | in-tree at `tools/n64graphics_ci_dir/` | **MIT** — (c) 2019 David Benepe; exoquant (c) Dennis Ranke | `tools/n64graphics_ci_dir/LICENSE`, `n64graphics_ci.c`, `exoquant/exoquant.c` | Colour quantization for RGBA -> 16-entry palettes. The designated fallback if `bake_castle_uv.py:quantize_clut16`'s median-cut proves inadequate on BOB's noisier textures (O5) | **Direct-copy available**, **not recommended now** — the in-tree median-cut ships today and is deterministic |
| `tools/saturn/vdp1_texture.py`, `bake_castle_uv.py` | in-tree, this repo | this project's own | as cited throughout §3 | The BIOS-probed distorted-sprite corner weights, the RGB1555 box filter, the transparent canonicalisation, the full Fast3D tile-state resolver, and the CLUT16 quantizer | **Direct reuse (in-repo).** Start here |
| `Fast-64/fast64` (GPL-3.0) | — | — | — | Named in the task brief as usable F3D texture/CLUT source. **Not cloned anywhere in this tree** (searched `work/upstream/` and `.gitmodules`); not fetched, per the no-network posture of this investigation | **Not evaluated.** Would only be worth fetching if the in-tree Fast3D tile-state resolver in `bake_castle_uv.py:202-224` turns out to be incomplete — it is not, for BOB's measured feature set (no mirror, no LOD shift, unity SP scale) |
| `glankk/libgfxd` (MIT), `DavidSM64/Quad64` (MIT) | — | — | — | Not present in this tree; not fetched | **Not evaluated.** The in-tree GBI parsers (`quad_map.py`, `dl_rigid_groups.py`) already cover BOB's entire measured opcode vocabulary |

**Explicit negative finding.** No external code is recommended for adoption in
milestone 1. Every mechanism it needs — corner weights, resampling, tile-state
resolution, quantization, residency, cart staging, `(display_list, ordinal)`
keying — already exists inside this repository, written for this target, and
carries its own regression evidence. Bringing in `fast64` or `libgfxd` would
add a GPL/licence surface and a second parser for a job the in-tree parser
already does. The right prior-art use here is **SlaveDriver as a design
reference** for the VRAM budget and the Option A corner-permutation strategy,
consumed as behaviour, not as source.

---

## 11. Observations made in passing (not fixed, not in scope)

- `src/port/saturn/gfx/saturn_fast3d_frontend.h`'s HWRAM budget comment still
  cites "~380 bytes" of margin. The real measured figure against the current
  ELF is **135,964 bytes** (§2.6). The comment's own advice — re-measure with
  `sh-elf-nm`, do not re-quote — is being violated by the comment itself.
- `sourceboot/main.c:80-86` cites 149,084 B measured 2026-07-24;
  `SEGMENT_ADDRESSING_DECISION.md` §4.3 cites 137,660 B; the current link is
  135,964 B. Three different numbers in three places for the same quantity. A
  captured, regression-tracked pool/HWRAM figure (as O1 of the addressing
  decision already recommends) would end this.
- `bin/generic.c` declares 22 texture arrays totalling 47,104 bytes; BOB
  references 13 of them. The other 9 (18,432 bytes) are permanently resident in
  `.cart_rodata` and never read — a small instance of the same unused-actor-bank
  waste the addressing decision measured at 285,250 bytes (§4.2).
- **The committed quad-merge capture's `telemetry` block does not decode.**
  `e2-sourceboot-quadmerge-freeroam-2026-07-26.json` (commit `312cc49`) records
  `telemetry.decode_error: "unexpected telemetry magic 0x2118DE5A"` against
  `telemetry_decode.py:23`'s expected `MAGIC 0x53415430` ("SAT0"), with
  `raw_telemetry.bytes: 120`. The evidence is **not** lost — the real profile
  counters are in `probe_window.data` and decode cleanly (§6.3) — but the
  capture ships a broken block alongside a working one, and a reader who trusts
  the named `telemetry` field will conclude the run produced nothing. Either
  wire the sourceboot target's telemetry emitter or stop requesting the block
  for this target. Reported, not fixed.
- `tools/saturn/bake_castle_uv.py` writes ROM-derived pixels and is explicitly
  marked "must never be committed". The sourceboot baker reads the already-
  extracted PNGs under `levels/` and `textures/` instead, which are themselves
  user-ROM-derived and gitignored. The same non-commit discipline applies to its
  generated header, and it belongs under `build/`, alongside
  `saturn_quad_map.c`.
