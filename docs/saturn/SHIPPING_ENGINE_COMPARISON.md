# Shipping Saturn 3-D engines vs. this port — an architectural comparison

**Date:** 2026-07-26.
**Scope:** Investigation only. No source file was modified. No build was run and no
emulator capture was taken. Every figure below is labelled **MEASURED** (a number
present in source, in a committed capture, or read out of the committed ELF),
**DERIVED** (arithmetic over measured quantities), **DOCUMENTED** (a comment or
manual states it), or **ESTIMATED** (inference, stated as such).

**Question asked:** is the port's per-triangle cost explicable by soft-float alone,
or is there a structural gap beyond it?

**References studied** (already cloned; not re-cloned):

| Engine | Path | Pinned commit | Licence | Reuse posture |
|---|---|---|---|---|
| Lobotomy SlaveDriver | `work/upstream/slavedriver-engine` | `a8986591557b6e680550d3c23970284d3b38ff8f` | GPL-3.0-or-later | Copyable under the project's existing GPL policy, isolated per `THIRD_PARTY_LICENSES.md` |
| Sonic Z-Treme | `work/upstream/sonic-z-treme` | `cff75451c1616aac1236fc2b44223902b55c706b` | GPL-3.0 text with a contradictory no-sale clause | **Behaviour study only.** No code adoption |
| Jo Engine | `work/upstream/joengine` | `556d081146211b6a1cfa6591d70f9487d406758b` | Repo `LICENSE` is MIT; **76 of 77 per-file headers are BSD-3-Clause** | Copyable; honour the stricter per-file header |

A fourth reference is used throughout and is not a repository: Sega's own SGL
documentation, already studied in [`SGL_REFERENCE_NOTES.md`](SGL_REFERENCE_NOTES.md).
It supplies documented capacity and cost figures that both GPL engines silently
delegate to.

---

## 0. Verdict

**There is a structural gap, it is measurable, and it is one thing, not a fog.**

The port's per-triangle vertex transform — `sm64_saturn_fast3d_resolve_triangle`,
`src/port/saturn/gfx/saturn_fast3d_frontend.c:285-775` — is written in `float`, is
executed **per triangle corner** rather than per vertex, and **re-decodes the
Q16.16 model-view-projection matrix back to `float` on every corner of every
triangle**. Measured from the committed ELF, that function contains **66 static
soft-float call sites out of 70 total calls**, of which **37 sit inside a loop that
runs three times per triangle**. Against the measured per-frame counters that is
**≈325,000 libgcc soft-float calls per rendered frame from this one function**.
All three reference engines do the equivalent work in Q16.16 integer arithmetic
with `dmuls.l` / `mac.l` / the SH-2 DIVU, at a *whole-frame* budget of roughly
**500–660 cycles per emitted primitive**.

That finding changes the shape of the problem in two ways:

1. **The "82.4 % soft-float" and "renderer is only 1/34 of samples" results were
   never in tension.** The PC histogram resolves samples to the libgcc leaf they
   land in, not to the caller. The renderer's float arithmetic *is* a large part
   of the soft-float bucket. `PERFORMANCE_DIAGNOSIS_2026-07-26.md` §2 does not
   establish that the engine tree owns most of the float work, and its own
   `pr`-based caller attribution was subsequently retracted by
   `2026-07-26-soft-float-replacement-design.md` §1.2(b). **This is the single
   correction most worth making before the soft-float workstream commits.**
2. **The cheapest large win is inside one port-owned file, not 112 engine files.**
   The soft-float design's option (b2) — converting `camera.c`, `mario_*.c` and the
   behaviours — is correctly scoped as months of work. Converting
   `resolve_triangle` to the Q16.16 primitives the port *already has* is one file,
   port-owned, outside the engine tree, and the reference implementation of the
   exact pattern **already exists in this repository**, in `castleviewer/main.c`.

**And, stated as plainly:** this does not by itself explain the remaining ~7×
beyond the soft-float ceiling, because the renderer transform is *inside* the
82.4 %. The 13.4 M-cycle non-float residual is dominated by the SM64 simulation
tick's integer work, which is a **per-frame** cost, not a per-triangle one. §6
ranks what attacks it. The honest summary is:

- ~50–85 % of today's frame is the renderer's own float transform — **a
  structural gap against the references, and cheaply closable**.
- The residual after that is mostly SM64 game-tick cost — **the comparison against
  these engines does not transfer there**, because their simulations are an order
  of magnitude simpler and that is a content/design property we do not have.

---

## 1. A correction to the headline number, made before anything is built on it

The brief states "our frame costs ~14,700 SH-2 cycles per emitted triangle
(827 VDP1 commands from 913 resolved triangles, ~75.9 M cycles/frame)". Those
three numbers are not consistent with each other, and the study would be
misleading if it did not say so first.

| quantity | value | basis |
|---|---:|---|
| Rendered frame time | 2.65 s = **75.9 M SH-2 cycles** | MEASURED (differential `frame_serial`, diagnosis §1.2) → DERIVED at 28.6364 MHz |
| Triangles emitted | **913** | MEASURED (`e2-sourceboot-quadmerge-freeroam-2026-07-26.json`) |
| VDP1 commands emitted | **827** | MEASURED, ibid. |
| **Cycles per emitted triangle, today** | **83,132** | DERIVED, 75.9 M ÷ 913 |
| **Cycles per VDP1 command, today** | **91,778** | DERIVED, 75.9 M ÷ 827 |
| Cycles per emitted triangle **after perfect soft-float removal** | **14,677** | DERIVED, 13.4 M ÷ 913 |

**14,700 is the post-soft-float residual per triangle**, not today's cost. It comes
from `2026-07-26-soft-float-replacement-design.md:724`, where it is correctly
labelled as such. Today's figure is **~83,000**. Both matter; conflating them
understates the current gap by 5.7× and overstates how much is left after the
soft-float workstream.

(The brief's 0.3596 FPS at HEAD implies 79.6 M cycles/frame — 5 % worse than the
committed 0.378 FPS measurement. All arithmetic below uses the committed 75.9 M so
it can be checked against committed evidence. The conclusions are insensitive to
5 %.)

---

## 2. The cycles-per-primitive comparison

Common basis: SH-2 at 28.6364 MHz (352-dot NTSC). One 30 fps frame on one SH-2 is
954,547 cycles.

| Engine / target | Primitives per frame | Frame rate | **Cycles per primitive** | Confidence |
|---|---:|---|---:|---|
| **SlaveDriver** — command-table ceiling | 1,448 cap | 30 fps, 1 CPU | **659** | DERIVED from a MEASURED cap (`SRUINS.C:1879`) and a MEASURED frame-pacing clamp (`SRUINS.C:2240-2263`) |
| **SlaveDriver** — bottom-up from the assembly | — | — | **~250–400 / quad** | **ESTIMATED** (instruction counts over `WALLASM.S` + `WALLS.C:1125-1147` + `SPR.C:246-260`) |
| **Sonic Z-Treme** — quad ceiling | 1,900 cap (`Common.h:22`) | 30 fps default (`ZTE_DEF.H:62`, DOCUMENTED in the menu at `ZT_MENU.c:152-156`) | **502** | DERIVED from MEASURED caps |
| **SGL** (Sega's own library) — documented work-area capacity | 1,786 polygons / 2,500 vertices | 30 fps | **534** | DERIVED from a DOCUMENTED capacity (`WORKAREA.TXT`, via `SGL_REFERENCE_NOTES.md:233`) |
| **Jo Engine** | — | — | **no figure exists** | Its 3-D path is a thin wrapper over the same SGL binary; it documents no budget and no frame rate anywhere |
| **This port — castleviewer / m4** (pre-baked Q16.16 mesh IR in work RAM, no SM64 tick) | ~1,032 VDP1 commands | 14.93 FPS | **~1,741** | DERIVED from MEASURED FRT ticks (`RENDERER_PRIOR_ART.md:220-222`); see the caveat below |
| **This port — sourceboot, today** | 827 VDP1 commands / 913 triangles | 0.378 FPS | **91,778 / cmd** — **83,132 / triangle** | DERIVED from a MEASURED frame time and MEASURED counters |
| **This port — sourceboot, at the perfect-soft-float bound** | 913 triangles | 2.14 FPS | **14,677** | DERIVED bound |

### How to read this table

**The reference figures are ceilings, not measurements.** Every one of them is
"whole CPU time ÷ the maximum primitives the engine was dimensioned for". Real
frames never fill the cap, and the CPU is not 100 % renderer — AI, physics, sound,
CD and input all run in the same budget. So the true renderer cost per primitive in
those engines is **lower** than the number shown. This makes them conservative for
our purpose: the gap is at least as large as stated.

**The castleviewer row carries a specific caveat.** The 14.93 FPS figure and the
~1,032-command figure come from adjacent but not identical captures in
`RENDERER_PRIOR_ART.md` (§"Accepted M4 throughput pass" and §"Stable M4
measurement boundary"). The tick arithmetic is self-consistent — 14,035 loop ticks
at FRT `/128` is 1,796,480 cycles, which at the 320-dot NTSC clock of 26.8741 MHz
is 66.8 ms = 14.96 FPS, reproducing the doc's 14.93 — but the pairing of that
frame time with that command count is DERIVED across two captures, not measured in
one. Treat it as **±20 %**.

### The three ratios that matter

- **Sourceboot vs. the shipping engines: ~140–180× per primitive** today
  (91,778 ÷ 502…659), and **~22–29×** even at the perfect-soft-float bound.
- **Sourceboot vs. our own castleviewer: 52×** (91,778 ÷ 1,741) — same project,
  same emulator, same VDP1 backend family, same SM64 source geometry.
- **Castleviewer vs. the shipping engines: 3.5×** (1,741 ÷ 502).

That middle ratio is the study's most useful single result. **Our renderer
architecture is not 30× off the shipping engines. It is within ~3.5× of their
design budget when it is given pre-staged Q16.16 geometry in work RAM and runs no
float.** The 52× separating sourceboot from castleviewer is not an architectural
mystery; §5 identifies it.

### On "their content is fundamentally lighter" — partly true, and not where it matters

At the **emitted-primitive** level it is false: we emit 827 VDP1 commands against
design caps of 1,448 (SlaveDriver), 1,786 (SGL) and 1,900 (Z-Treme). Our per-frame
primitive volume sits at 43–58 % of what these engines were dimensioned for. We are
not asking VDP1 to do anything unusual.

At the **transformed-primitive** level it is true and it is a real gap: we transform
2,311 triangles to emit 913 — and we transform **6,933 corners** to load 3,833
vertices (§5.2). SlaveDriver and Z-Treme transform only what survives a hierarchical
visibility test they baked offline. That advantage is genuinely unavailable to us in
its original form (§7).

At the **topology** level it is true and structural: VDP1 rasterises quads. Z-Treme's
JADE1 map is 4,996 vertices to 2,502 quads — **2.00 vertices per quad**, a
grid-regular authored topology. SM64 triangles each cost a full degenerate quad.
The port's quad merger recovers some of this (86 merges of 913 triangles in the
measured frame, 9.4 %) but the ~2× structural disadvantage is permanent.

---

## 3. Per-engine architectural summaries

### 3.1 SlaveDriver (Powerslave / Exhumed, Duke Nukem 3D, Quake)

**Pipeline.** One camera matrix per frame (`SRUINS.C:2071-2084`). Visibility is a
**sector-portal fixpoint with screen-space bounding-box propagation**
(`findDoorways`, `WALLS.C:1546-1794`): each portal's screen AABB is intersected with
its parent sector's box, and an empty intersection means the sector behind is never
touched at all. Portals are sorted first within each sector by the offline tool
(`UTIL/CONVERT.C:2098-2125`) so the inner loop `return`s on the first non-portal
(`WALLS.C:1567-1569`). Sector projections are cached *within* the frame so a sector
reached by three portals projects once (`WALLS.C:1562-1572`).

**Sorting.** No per-primitive depth sort and no Z-buffer. A portal-adjacency DAG is
built and topologically peeled (`buildTree`, `WALLS.C:1953-1983`; peel at
`:2187-2233`), with ties broken by an insertion sort on an **integer octagonal
distance norm** (`approxDist`, `UTIL.C:165-177` — no sqrt) and, where sectors
overlap, by a **baked N×N cut-plane matrix** computed offline
(`CONVERT.C:2393-2423`, consumed `WALLS.C:2016`). Within a sector nothing is sorted
at all, because the tool guarantees convexity (`CONVERT.C:2231`).

**Command lists.** Rebuilt every frame, but **double-banked** (`SPR.C:71-72,129`),
staged into a 256-entry HWRAM buffer and **bulk-DMA'd** to VDP1 VRAM
(`SPR.C:141-157`). No dirty flags, no inter-frame reuse. It does use VDP1's own
`JUMP_CALL`/`JUMP_RETURN` to splice a pre-built sprite command run into the correct
depth position **without moving data** (`WALLS.C:1887-1889`, `2259-2268`) — a
genuinely reusable trick.

**Precision.** Zero runtime float; every `float`/`double` in the tree is in an
offline PC tool or a dead `#if 0`. Deliberately **mixed**: Q16.16 for camera and
matrices, **plain `short` for stored level vertices** (`SLEVEL.H:115-118`, 8 bytes
per vertex), 16-bit integer screen coordinates, one byte per vertex for baked light.
Per-vertex normalisation, sqrt and trig are **absent from the wall renderer
entirely** — normals are per-wall and baked unit-length, distances use `approxDist`,
and `WALLS.C` contains zero trig calls.

**SH-2 hardware.** This is the strongest part. `normTransform`
(`WALLASM.S:231-372`) does three `mac.l` per dot-product row and extracts Q16.16
from the Q32.32 accumulator with a **single `xtrct`** (`:266-273`). Integer→Q16.16
conversion is **free**: big-endian `mov.w` stores into pre-zeroed longwords *are* the
`<<16` (`:247-264`). The DIVU base is held in **GBR** so register access is one
instruction (`:14-15`, `:254-255`). And the divide latency is fully hidden — the
divide starts at `:284` and the result is collected at `:350`, with **~40
instructions of real work in between** (the X and Y `mac.l` rows plus the entire
depth-cue lighting computation). The engine documents the 37-clock latency at
`UTIL.H:171-176` and states the design rule outright. There is also a C-level
three-way interleave (`WALLS.C:1017-1027`) that turns six divisions into three DIVU
operations each hidden behind one software divide. The counterexample is honest and
instructive: the *general* `project_point` cannot fill the shadow and says so —
`WALLASM.S:31`, `; can't do much during the divide wait, too bad.`

**Dual SH-2.** Yes, and it is the best-documented example available. The slave runs
**the same `drawSector` code** with `slave=1` (`WALLS.C:1806-1819`) over sectors
taken from the far end of the sorted draw list, writing `{poly[4], gtable, tile}`
records through a **cache-through alias** (`WALLS.C:1272-1273`) so the master sees
them; both sides purge cache at the handoff (`WALLS.C:1808-1810`, `1826-1828`).
Signalling is the SH-2 **FRT input-capture flag**, not interrupts
(`WALLS.C:1932`, `2246`). There are **no locks** — shared state is read-only during
the parallel phase, and the slave gets a complete duplicate set of mutable scratch
globals (`WALLS.C:593-625`, `679-680`, `1253`). The split is **auto-tuned every
frame by the master's spin-wait iteration count** (`WALLS.C:2277-2284`): five lines,
no telemetry. Crucially, the slave's rendering overlaps **the entire game-logic
tick**, not just the master's rendering (`SRUINS.C:2096` … `:2159`).

**Frame pacing.** Adaptive, clamped to 1–2 vblanks (`SRUINS.C:2240-2263`) — 60 fps
degrading to 30 and no further; game logic is delta-timed off `vtimer`.

### 3.2 Sonic Z-Treme

**Framing fact:** this is *not* a from-scratch rasteriser. All transform, clip, sort
and VDP1 command generation happen inside **Sega's SGL 3.02j binary**
(`LIBSGL.A`, 349,738 bytes). XL2's engine is a visibility-and-data-layout layer over
`slPutPolygon()`. Its lessons are about data placement and culling, not about
transform kernels.

**Pipeline.** `ztRender()` (`ZT_RENDERING.c:721-795`) submits everything inside a
single matrix push — level meshes are pre-transformed into **world space** by the
offline map compiler, so there is no per-object matrix stack for level geometry.
Render is issued **before** physics (`SRC/game.c:772-775`) specifically to keep the
SGL slave-SH-2 pipeline busy while the master simulates; the author notes at
`ZT_RENDERING.c:718` that even this is "not super efficient".

**Visibility.** A **baked octree** with tri-state hierarchical frustum culling
(`ZT_RENDERING.c:411-507`, `ZT_FRUSTUM.c:145-165`). A node fully inside the frustum
makes all descendants skip plane tests. Leaves carry a single-plane flag enabling a
whole-mesh backface reject with **zero vertices touched** (`:437-451`), and a
distance-banded LOD switch (`:452-481`). Children are visited **near-to-far** so
that when SGL's 1,900-primitive cap overflows, the primitives dropped are the far
ones (`:494-503`). Measured leaf granularity: 8 quads / 16 vertices (JADE1),
18 / 30 (RED).

**PVS is baked into the shipped maps and is DISABLED at runtime** —
`ZTE_DEF.H:121`, `//#define USE_PVS ... But it's glitchy...`. Do not credit this
engine with working PVS.

**Memory layout — the most transferable idea in the codebase.** The entire map
streams from CD into **LWRAM**, pointers are fixed up in place, and then
`mallocVertices()` (`ZT_LOADING.c:321-353`, candidly labelled *"Temporary function
to move the vertices to high work ram"*) DMA-promotes exactly two things into a
300 KB HWRAM arena: **the octree** (walked every frame) and **every mesh's vertex
position table** (touched per-vertex per-frame). Polygon topology, attributes,
collision and PVS stay in LWRAM. That is a precisely targeted hot/cold split, and it
depends on nothing about the content.

**Sorting.** SGL's 512-bucket depth sort, O(N) insert per primitive per frame
(`workarea.c:15-16`, `slZdspLevel(6)`). Rebuilt from scratch every frame into a
double-buffered 137 KB command staging area. **No caching, no dirty flags, no
frame-to-frame coherence anywhere** — `mapTick` exists only for the disabled PVS.

**Precision.** Zero runtime float (one grep hit, in a NetBeans indexer stub not in
`SRCS`). Q16.16 world coordinates, 16-bit binary angles, Q8.8 compressed animation
keyframes, `Uint8` normal indices into a 162-entry table. Sqrt/divide/trig appear
**only in frustum setup** — roughly 10 sqrts, 30 divides and 6 trig calls **per
frame, total**. Zero divisions per vertex and zero per primitive in engine code; the
perspective divide lives inside SGL.

**Lighting.** Level geometry is **fully baked, per-quad-corner** — one 8-byte VDP1
Gouraud table per quad, uploaded once at map load (`ZT_LOADING.c:96`), referenced by
pointer forever. Verified: declared `TOTAL_GOURAUD` equals full-res quads + LOD
quads exactly in all three shipped maps. Characters use real-time Gouraud via
`slPutPolygonX`.

**Character animation — the Mario analogue.** Sonic is **173 vertices, 185 quads, 30
keyframes**, animated by **pure vertex morph** (no skeleton, no skinning): two
keyframes lerped with a shift, not a divide (`ZT_ANIMATION.c:36-39`). Total
per-frame animation cost ESTIMATED at ~5,000 cycles; the 185 quads are the expensive
part, ~10 % of the whole frame budget for one character.

**Dual SH-2.** The game never dispatches to the slave explicitly — both
`slSlaveFunc` call sites are commented out. The slave is used *implicitly* by SGL's
polygon pipeline via `CommandBuf` (`workarea.c:20,39`). No measurement of what it
buys exists.

**Honesty note the engine makes about itself:** `README.md` states this post-SAGE
build is **slower** than the demo people played, for two named reasons — the map
compiler was not updated after the frustum-culling rewrite so bounding boxes are not
tight, and Gouraud replaced flat lighting. This is a good architecture captured at a
regressed moment.

### 3.3 Jo Engine

**Its 3-D pipeline is not a candidate.** `jo/3d.h:125-986` is one
`#if JO_COMPILE_USING_SGL` block with no `#else`. `jo_3d_mesh` is literally
`struct { PDATA data; }` — SGL's own polygon struct — and `jo_3d_draw` is
`slPutPolygon(&mesh->data)`. Transform performance is SGL's, inside a 423 KB
proprietary binary that the MIT/BSD licence does not and cannot cover. Its own
`vdp1_command_pipeline.c` is a 176-line skeleton with the sort function commented
out, off by default. No polygon budget, sample frame rate or performance note exists
anywhere in the repo.

**Its fixed-point primitives are the real asset, and there are three of them.**

- `jo_fixed_mult` (`math.c:57-70`) — `dmuls.l` / `sts MACH` / `sts MACL` / `xtrct`.
  Four instructions, the canonical SH-2 Q16.16 multiply. ESTIMATED ~7–9 cycles
  inlined. Credited upstream to **Ponut64**.
- `jo_fixed_div` (`math.c:109-144`) — correct SH7604 DIVU register sequence
  (DVSR `0xFFFFFF00`, DVDNTH `0xFFFFFF10`, DVDNTL `0xFFFFFF14`). The **39-cycle**
  latency is DOCUMENTED at `math.c:123-124`, with the author's own hedge
  *"But check with real hardware first, you know?"*
- `jo_fixed_rsqrt` (`math.c:477-508`) — 8-entry table seed plus three
  Newton-Raphson iterations, adapted from trenki2. The one numerically competent
  routine in the file.

**Where it leaves performance on the table**, and this matters because we would be
adopting it: **`jo_fixed_div` has zero latency hiding** — the write to DVDNTL at
`math.c:136` is immediately followed by the read at `:137`, stalling the full 39
cycles with nothing overlapped. That is precisely the optimisation SlaveDriver
(`WALLASM.S:284→350`) and SGL (`SGL_REFERENCE_NOTES.md:160-163`) both build their
transform kernels around. It also never checks DVCR, is not re-entrant, and is not
`__jo_force_inline`.

**Do not adopt beyond those three.** `jo_fixed_dot` (`math.c:72-88`) is the only
`mac.l` in the tree and its inline-asm constraints are wrong — `mac.l @Rm+,@Rn+`
post-increments both pointers but they are declared input-only `"r"`, a genuine
miscompile hazard; it is never called. `jo_matrix_mul` (`math.h:1514-1531`)
**accumulates fixed-point into a `float`** — 64 int→float conversions, 64 soft-float
adds and 16 float→int per 4×4 concatenation on an FPU-less CPU; also never called.
`jo_fixed_pow` returns x^(n+1). The trig path routes through soft-**double**
(`RADtoANG` in `SL_DEF.H:146` uses `65536.0` and `M_PI`). The pattern is clear: the
fixed-point layer has essentially no test coverage. Anything copied must be
differential-tested before it is trusted.

**Licence correction worth recording:** the repo `LICENSE` is MIT, but **76 of 77
source files carry a BSD-3-Clause header**. BSD-3 adds two obligations MIT does not
— binary-form attribution in accompanying documentation, and a no-endorsement
clause. Honour the per-file header, not the root file.

### 3.4 SGL (documentation only) — the missing implementation reference

Both GPL engines delegate their transform and sort to SGL, so its documented
behaviour is the only implementation-level account of what the fast path looked
like. From `SGL_REFERENCE_NOTES.md`:

- **Capacity: 2,500 vertices / 1,786 polygons per frame** (`WORKAREA.TXT`) — the
  calibration point used in §2.
- **The 39-clock DIVU divide is issued early and hidden behind the remaining X'/Y'
  MAC row products** (FAQ §3-4) — the same schedule as SlaveDriver's, independently
  documented by Sega.
- **Sort:** 128 primary list-head slots plus a 256-entry nest buffer, a lazy
  two-level radix over a 15-bit Z, cost proportional to occupied slots only.
  Sorted emission is **not a CPU copy** — SGL writes 12-byte SCU indirect-DMA
  descriptors and the SCU gathers commands into VDP1 VRAM during V-blank.
- **All hot state is GBR-anchored** for short-encoding access.
- **Master/slave is adaptive three-state work-stealing**, slave woken by an FRT
  input-capture write, ring mailbox, no SMPC access from the slave.
- **Drop-don't-stall:** from v1.1 on, sprites that miss the frame change are
  discarded, not carried over.

---

## 4. What this port actually does — measured

All figures from `docs/saturn/evidence/reports/e2-sourceboot-quadmerge-freeroam-2026-07-26.json`
and from `build/saturn/sourceboot/e2-bob/obj/sm64-saturn-sourceboot-e2.elf`
(md5 `8305522447a477d1717772e9fdedf57c` — the same ELF the diagnosis and the
soft-float design analysed).

| counter | value |
|---|---:|
| display-list commands walked | 3,453 |
| display-list calls / branches | 232 / 6 |
| `G_MTX` commands | 84 |
| `G_VTX` commands | 314 |
| **vertices loaded** (`lit 3,506` + `unlit 327`) | **3,833** |
| triangles transformed | 2,311 |
| — rejected, `w ≤ 0` | 10 |
| — rejected, backface | 1,181 |
| — rejected, near/far (of which offscreen 157) | 214 |
| triangles emitted | 913 |
| quads merged | 86 |
| **VDP1 commands emitted** | **827** |

**Pipeline shape.** One pass, no caching. `sm64_saturn_fast3d_frontend_submit`
(`saturn_fast3d_frontend.c:1277`) walks the display list; `resolve_triangle`
transforms, divides, culls, maps and writes into `resolved[]`; `vdp1_emit`
(`saturn_fast3d_vdp1_emit.c:20`) sweeps and uploads. There is **no frame-to-frame
coherence of any kind**. The named caching experiments in
`docs/saturn/evidence/reports/` (`persistent-command-lowering-cache`,
`projection-cache`, `global-leaf-marker-cache`, `transform-once`) are all recorded
against `build/saturn/castlearea/...cue` — **the castleviewer target. None of that
code exists in the sourceboot path.** The only laziness in sourceboot is
within-frame: `mp_dirty` on the matrix stack (`saturn_matrix.h:268-279`) and
`lights_changed` (`saturn_light_q16.h:100`).

**Geometry source.** Display lists, `Vtx` arrays, the quad map and the trig LUT all
live in `.cart_rodata` at `0x22400000` — the SH-2 cache-through partition, uncached
**by construction** (libyaul defines no cached CS0 alias). There is **no staging or
promotion to HWRAM**, which `SEGMENT_ADDRESSING_DECISION.md:708-718` already flags
as a live violation of `CARTRIDGE_ASSET_POLICY.md`. Both GPL engines promote
per-frame-hot geometry into HWRAM before traversal and neither contains any A-bus
address at all. Per-frame uncached read traffic, DERIVED from the measured counters:
vertices 3,833 × 4 longwords = 15,332, display-list words ~10,400, quad-map
~6,000 → **~31,700 longwords ≈ 127 KB** (about 60 % above the diagnosis's earlier
derived 19,500; the `lit_vertices` + `unlit_vertices` counters settle the
diagnosis's open question §7.5 — it is 3,833 vertices, not "~10 per load, bounded at
32").

**Sort.** Not a sort — a **16-pass linear sweep** (`saturn_fast3d_vdp1_emit.c:34-41`)
over `resolved[]`, 16 × 827 = 13,232 iterations, streaming a 21.5 KB array through a
4 KiB 4-way data cache sixteen times. Bucket assignment
(`saturn_fast3d_frontend.c:271-276`) is an `int64_t` division that GCC lowers to a
**`__divdi3` software 64-bit divide per primitive** (call sites confirmed in the
disassembly at `0x60712e6` and `0x607136e`). Sixteen depth levels; within a bucket,
arbitrary order. Compare SGL's 128+256 lazy radix at 32K effective levels.

**Upload.** Gouraud tables go HWRAM→VDP1 by SCU DMA. The command table goes
LWRAM→VDP1 by a **hand-written non-unrolled CPU longword loop**
(`saturn_vdp1_backend.h:264-272`) because SCU DMA cannot read LWRAM — 830 commands ×
8 longwords = **6,640 uncached B-bus stores, 26,560 bytes, every frame, whether or
not anything changed**. Single-buffered. Both GPL engines double-bank their command
region and DMA it.

**Precision.** Q16.16 covers the matrix stack, all `mtxf_*` mirrors, lighting and
trig (a pure generated LUT). **The per-triangle transform, perspective divide,
backface cull and viewport map are all `float`** — `saturn_fast3d_frontend.c:450-563`.
The source comment admits it: *"mp's Q16.16 entries are divided back to float for
this scratch computation. This is fine for a first, correctness-focused pass."*

**SH-2 hardware use: none, at source level.** `grep -rn asm` across
`src/port/saturn/` returns **zero hits**; there are no `.S` files. No `mac.l`
anywhere. No DIVU use in the sourceboot path. `dmuls.l` appears only as a compiler
consequence of `(int64_t)a * (int64_t)b >> 16` in `saturn_matrix_kernels.h:45`,
with no source-level control and no `xtrct`.

**Dual SH-2: powered on and spinning on nothing.** libyaul's `cpu_init.c:65` calls
`cpu_dual_comm_mode_set(CPU_DUAL_ENTRY_POLLING)` at boot, parking the slave in
`__slave_polling_entry` running a no-op `_default_entry`. `src/port/saturn/` never
touches it.

---

## 5. Where the 52× between sourceboot and castleviewer goes

### 5.1 The per-triangle float transform — MEASURED from the ELF

`_sm64_saturn_fast3d_resolve_triangle` is at `0x06070990`, size `0xa50` = 1,320
SH-2 instructions. Resolving every `jsr` target by tracking the PC-relative literal
loads that feed it:

| call target | static sites in the function |
|---|---:|
| `___mulsf3` | 18 |
| `___floatsisf` | 16 |
| `___addsf3` | 13 |
| `___subsf3` | 6 |
| `___lesf2` | 5 |
| `___gesf2` | 3 |
| `___fixsfsi` | 3 |
| `___divsf3` | 2 |
| `___divdi3` | 2 |
| `_sm64_saturn_matrix_mul` | 1 |
| `___lshrsi3_r0` | 1 |
| **total `jsr`** | **70** |
| **of which soft-float** | **66 (94 %)** |

The `for (int c = 0; c < 3; c++)` transform loop is **not unrolled** — back-edge
`0x6070db8 → 0x6070b86`, guarded by `cmp/eq #3,r0` at `0x6070db0`. Its body
contains, per corner:

| call | per corner |
|---|---:|
| `___floatsisf` — one per MVP matrix entry read | **12** |
| `___mulsf3` — 9 dot-product terms + 3 × `2^-16` scale | 12 |
| `___addsf3` | 9 |
| `___lesf2` (`w <= 0.0f`) | 2 |
| `___divsf3` (`x/w`, `y/w`) | 2 |
| **total** | **37** |

GCC did fold `/ 65536.0f` into `× 0x1p-16f` and hoisted the scale to one multiply
per output component. It did **not** hoist the 12 `__floatsisf` conversions of the
Q16.16 matrix out of the corner loop. **The MVP matrix — which the port went to the
trouble of making Q16.16 end-to-end, wire format included (`-DSATURN_MTX_IS_Q16=1`)
— is decoded back to `float` three times per triangle, 2,311 times per frame, for a
matrix that changes 84 times per frame.** That is **83,196 redundant `__floatsisf`
calls per frame** before any of the arithmetic they feed.

Adding the rest of the function (backface cross ≈ 8, viewport map + clamp loop
14 × 3 = 42, three `__fixsfsi` on `cw`):

- per surviving triangle: **~166** soft-float calls
- per backface-rejected triangle: **~116**

Against the measured counters (2,311 transformed, 1,181 backface-rejected,
913 emitted, only 10 early-exiting on `w ≤ 0`):

> **≈325,000 libgcc soft-float calls per rendered frame, from `resolve_triangle`
> alone.** DERIVED from MEASURED static counts × MEASURED dynamic counters.

**What that costs, bounded honestly.** The per-call cycle cost is unmeasured (it is
open question §7.1 of the diagnosis and §11.1 of the soft-float design). The
soft-float design derives ~200–260 *executed* instructions for one `float + float`.
Bounding:

| assumed average cycles per soft-float call | cycles/frame from `resolve_triangle` | share of the 75.9 M frame |
|---:|---:|---:|
| 100 (an optimistic floor) | 32.5 M | **43 %** |
| 150 | 48.8 M | **64 %** |
| 200 | 65.0 M | **86 %** |

The measured soft-float budget is 62.5 M cycles (82.4 % of 75.9 M). At 192
cycles/call `resolve_triangle` would account for *all* of it — which cannot be true,
since the SM64 tick certainly executes float. So the true average is below 192, and
**`resolve_triangle` is somewhere between roughly 40 % and 85 % of the entire
frame.** Even the floor of that range makes it the largest single identified cost in
the port.

By comparison, an equivalent Q16.16 transform — nine `dmuls.l` products, nine adds,
one DIVU reciprocal reused for both screen coordinates — is ESTIMATED at
**~150–250 cycles per corner** against the float path's ~4,000–8,000. That is a
**25–50× reduction on the port's single hottest path**, and it needs no engine-tree
change.

**This is not hypothetical, and the reference implementation is already in this
repository.** `src/port/saturn/castleviewer/main.c:434-491` contains
`project_vertex` and `world_to_view_project`, described in their own comments as a
*"close-port of libmic3d's MIT-licensed transform-once projection schedule: start
one SH-2 DIVU reciprocal, prepare X/Y while it runs, then reuse that quotient for
both screen coordinates. No software `___sdivsi3` is required."* That is exactly
SlaveDriver's `WALLASM.S:284→350` schedule and exactly SGL's documented one. It
works, it is in-tree, and **sourceboot does not use it.**

### 5.2 Transform per corner, not per vertex — a second, independent redundancy

The upstream reference this frontend is ported from transforms each vertex **once**,
at `G_VTX` time, into `rsp.loaded_vertices[]` — `src/pc/gfx/gfx_pc.c:610-619`,
`gfx_sp_vertex`. Our frontend instead stores untransformed model-space floats in
`frontend->vertices[]` (`saturn_fast3d_frontend.c:1185-1187`) and transforms **three
corners per triangle** inside `resolve_triangle`.

- Reference behaviour: **3,833** vertex transforms per frame (MEASURED count).
- Actual behaviour: **2,311 × 3 = 6,933** corner transforms per frame (DERIVED).
- **Redundancy factor: 1.81×.**

Every studied Saturn engine transforms per vertex, once, into an indexed buffer
(`vCalc[]` in SlaveDriver, `pbuffer` in SGL/Z-Treme). Castleviewer already fixed
this for actors — `RENDERER_PRIOR_ART.md:155-162` records a 424-entry
view/projection cache dropping the sort phase 101 → 73 ms and command build
63 → 25 ms, and the loop 4.6 → 6.6 FPS. Sourceboot never received it.

### 5.3 Transform before cull

51 % of all transform work is discarded. The order in `resolve_triangle` is: full
three-corner MP transform **and perspective divide** (108 soft-float calls) →
*then* backface cull. 1,181 triangles pay that in full to be thrown away.

The cull is a screen-space cross product on `cx/cy` (`:509-513`), which is why it
needs the divide. The standard alternative — the sign of the unnormalised
`(x_i·w_j − x_j·w_i)` cross, which needs no division at all — would remove
~1,181 × 6 `___divsf3` plus the entire viewport-map stage for those triangles.
SlaveDriver culls on a **precomputed per-wall plane normal** before touching any
vertex (`WALLS.C:1578-1585`); Z-Treme rejects whole single-plane leaf meshes before
touching any vertex (`ZT_RENDERING.c:437-451`). Both cull earlier and cheaper than
we do.

### 5.4 The float vertex wire format is a build-flag decision we own

`src/port/saturn/sourceboot/Makefile:95` sets `-DF3DEX_GBI_2E=1`, which
`include/PR/gbi.h:90-94` turns into `GBI_FLOATS`, which makes `Vtx_t.ob` a
`float[3]` instead of the N64's `short[3]` (`gbi.h:1113-1118`). Consequences:

- Vertex positions in the cart are **12 bytes instead of 6** — doubling the largest
  component of per-frame uncached A-bus traffic.
- The transform's inputs are float **by construction**, which is a large part of why
  `resolve_triangle` is float at all.
- SlaveDriver's free `int16 → Q16.16` conversion trick (`WALLASM.S:247-264`, a
  big-endian half-word store into a pre-zeroed longword — **zero shift
  instructions**) is unavailable while the source data is float.

The vertex initialisers in `actors/*.c` and `levels/*/leveldata.c` are integer
literals; under `GBI_FLOATS` they are converted at compile time. Switching the
sourceboot target off `F3DEX_GBI_2E` is therefore a **rebuild plus the port code
that assumes floats**, not a data-format migration. The port already did exactly
this for matrices (`SATURN_MTX_IS_Q16`).

### 5.5 The non-float residual, and why "cycles per triangle" stops being the right metric

The 13.4 M-cycle residual after perfect soft-float removal is **not** dominated by
per-triangle work. Attribution, all ESTIMATED except where noted:

| item | cycles/frame | basis |
|---|---:|---|
| SM64 game-tick integer work (object list, `find_floor` surface traversal, geo graph, level script — all chasing pointers through the LWRAM main pool) | **~7–10 M** | ESTIMATED residual |
| Uncached cart traversal (~31,700 longwords, MEASURED) | 0.95–3.2 M | DERIVED at 30–100 cycles/longword; **A-bus latency remains unmeasured in this tree** |
| 16-pass bucket sweep + cache line fills | 0.5–0.7 M | DERIVED from 13,232 iterations (MEASURED) |
| `__divdi3` per primitive in the depth bucket | ~0.3 M | DERIVED |
| MP recomposition (dirty-gated, ~100–170 full 4×4 multiplies/frame) | 0.2–0.3 M | ESTIMATED |
| Lazy light recompute (`isqrt64` — a 32-iteration bit-by-bit `int64` loop — plus three `int64` divides, re-armed by every `G_MTX` *and* `G_POPMTX`) | 0.2–0.3 M | ESTIMATED |
| VDP1 command upload (6,640 uncached B-bus stores, MEASURED) | 0.15–0.3 M | DERIVED |
| Per-primitive VDP1 command build | ~0.1 M | ESTIMATED |

**Roughly half to three quarters of the residual is SM64 simulation, which does not
scale with triangle count.** Dividing it by 913 to get "14,700 cycles per triangle"
attributes simulation cost to geometry. The reference engines' cycles-per-primitive
figures are *whole-frame* budgets for engines whose simulation is an order of
magnitude simpler — SlaveDriver's `runObjects`, Z-Treme's `update_physics` on one
morph-animated character. **That part of the comparison does not transfer, and the
study should not pretend it does.**

---

## 6. Verdict, with ranked candidate explanations

**Is 83,000 (today) / 14,700 (post-soft-float) cycles per triangle explicable by
soft-float alone?**

**Today's 83,000: yes, and more precisely than the existing diagnosis states — but
the soft-float in question is the port's own renderer, not the engine tree.** The
gap against shipping engines at the *renderer* level is real, structural, ~50×, and
concentrated in one function.

**The residual 14,700: no, and also not architecturally, in the sense the question
implies.** It is mostly SM64 simulation cost, which these engines cannot calibrate
because their simulations are far cheaper by design.

### Ranked candidates for closing the gap

Ranked by ESTIMATED magnitude against today's 75.9 M-cycle frame. Confidence is
stated for each.

| # | Change | Estimated saving | Availability | Confidence |
|---:|---|---|---|---|
| **1** | **Convert `resolve_triangle`'s transform, perspective divide, cull and viewport map to Q16.16** using the port's existing primitives and castleviewer's DIVU schedule | **30–60 M cycles/frame (40–80 % of the frame)** | **Fully available.** One port-owned file, outside the engine tree | **High** on direction (325,000 soft-float calls MEASURED); **low** on the exact figure (per-call cost unmeasured) |
| **2** | **Transform once per vertex at `G_VTX`, not per corner** — restore the upstream `gfx_sp_vertex` shape | **1.81× on whatever stage 1 leaves** | Fully available; castleviewer already did it for actors | **High** — 3,833 vs 6,933 both MEASURED/DERIVED |
| **3** | **Cull before the perspective divide** (unnormalised `x_i·w_j − x_j·w_i` sign test, or an object-level test) | Removes the divide + viewport stage for **51 %** of transformed triangles | Fully available | **High** on the count, **medium** on the saving |
| **4** | **Stage per-frame-hot geometry into HWRAM** (the `CARTRIDGE_ASSET_POLICY.md` fix) | 0.95–3.2 M today; **60–180 % of a 1.8 M target frame** | Fully available; both GPL engines do exactly this. See `SEGMENT_ADDRESSING_DECISION.md` for the pointer-translation decision that must be made first | **Medium** — A-bus latency is unmeasured; the harness at `hwtest/main.c:163-190` has never fired |
| **5** | **Drop `F3DEX_GBI_2E`** so vertices are `short[3]` again | Halves the largest cart-traffic component; unblocks the free `int16→Q16.16` store trick; removes float from the vertex source | Available — a build-flag change plus port code that assumes floats | **Medium** |
| **6** | **Put the slave SH-2 to work** | Up to ~2× on the geometry stage; strictly less overall (command emission is serial on the master) | Available. SlaveDriver's `WALLS.C:1806-1950` is a complete, GPL-copyable worked example including the 5-line auto-tuning load balancer | **Medium** — the diagnosis ranked this 6th of 6 at 80× over budget; **at 5–10× over budget that dismissal no longer holds** |
| **7** | Remove the bring-up `vdp2_sync_wait(); vdp1_sync_wait();` block (`sourceboot/main.c:370-371`), which costs a **second** full VBLANK-IN+OUT pair per iteration on top of the raw poll `display_and_vsync` already does | ~0.95 M cycles/frame (2 vblanks) — **noise today, ~50 % of a 15 FPS frame** | Fully available; the code comment at `:311-357` already contains the whole analysis and flags itself as a bring-up override | **High** — the mechanism is documented in the source |
| **8** | Replace the 16-pass sweep with a proper bucket/radix emit and kill the per-primitive `__divdi3` | 0.5–1.0 M | Fully available. SGL's documented 128+256 lazy radix is the pattern | **Medium** |
| **9** | `-O2` on the float/geometry translation units instead of `-Os` | Unmeasured | Available; contends with Rank 4 for the 134 KiB HWRAM margin | **Low** |
| **10** | Adopt `xtrct`-based Q16 multiply and DIVU-with-latency-hiding as explicit primitives rather than relying on GCC | Small per-op, large in aggregate once the pipeline is integer | Available (Jo Engine, BSD-3; SlaveDriver, GPL) | **Medium** |
| **11** | Reduce transformed-primitive volume by hierarchical visibility | Potentially large — but see §7 | **Partly unavailable**; SM64 has per-node culling radii but no spatial partition | **Low** |

**Ranks 1–3 are one workstream, they are all inside one file, and together they are
the study's principal recommendation.** They should be scheduled *ahead of* the
soft-float replacement's option (a1), not after it: they attack the same 82.4 % at
25–50× on the hottest path rather than the ~3× a soft-fp library swap delivers, and
they are strictly less invasive than option (b1)'s engine-tree edits.

---

## 7. What is not available to us, and why

These are content-pipeline privileges, not code techniques. A port of fixed N64
content cannot have them without changing the game's data — which is the thing this
port exists not to do.

1. **Sector/portal or octree visibility.** SlaveDriver's entire `findDoorways`
   fixpoint needs a convex-sector world with explicit portal adjacency; Z-Treme's
   traversal needs a baked octree whose leaves each own exactly one small mesh with a
   bounding volume and a single-plane flag. SM64 arrives as arbitrary display lists
   with no spatial partition. **This is the single largest reference advantage and it
   is not portable in its original form.** (A coarse load-time partition derived from
   SM64's own area/room structure and `GEO_CULLING_RADIUS` nodes is a different,
   smaller, genuinely available thing — do not confuse the two.)
2. **Baked per-vertex or per-quad-corner lighting.** Z-Treme's world costs *zero*
   runtime lighting; SlaveDriver's is one byte per vertex plus two shifts of fog.
   SM64's lighting comes from F3D light structs applied per-vertex at runtime, with
   dynamic geometry. Baking it changes behaviour.
3. **Quad-native meshes.** 2.00 vertices per quad in Z-Treme's shipped maps. Every
   SM64 triangle costs a full VDP1 command for half the coverage. The quad merger
   recovers 9.4 % in the measured frame; the residual ~2× is permanent.
4. **Offline LOD meshes** at 23–63 % of full-res quad count, switched at one
   distance threshold.
5. **World-space pre-transformed static geometry** (Z-Treme draws the entire level
   under one matrix push). SM64's scene graph is hierarchical with per-object
   transforms.
6. **Grid-regular walls.** SlaveDriver's `WALLFLAG_PARALLELOGRAM` unlocks
   `rectTransform`'s incremental additive traversal — a W×H wall transforms with
   **zero matrix multiplies**. SM64 meshes are not axis-regular.
7. **Texture orientation as a baked vertex permutation** (`WALLS.C:969-979`) — zero
   runtime UV math. SM64 has arbitrary per-vertex UVs.
8. **Sprite actors.** All of SlaveDriver's enemies and most of Z-Treme's entities are
   billboards — one VDP1 command, no geometry. Mario, Bob-ombs and Goombas are
   meshes.
9. **Vertex-morph animation instead of skinning.** 173 vertices × 30 keyframes,
   lerped with a shift. SM64 uses a bone hierarchy with per-limb display lists and a
   matrix stack.
10. **Vertex-count discipline as a design constraint.** These engines chose caps that
    fit their content. Ours are fixed by the original game.
11. **A simulation an order of magnitude cheaper.** This is the one that most
    undermines the cycles-per-primitive comparison, and it is why §5.5 exists.

**Portable, and independent of content** — these are the transferable half:
DIVU latency hiding; `mac.l` + `xtrct` fixed-point dot products; the free
`int16 → Q16.16` big-endian store; hot/cold LWRAM→HWRAM promotion of exactly the
per-frame-touched structures; tri-state hierarchical frustum culling with
INSIDE-propagation over *any* hierarchy; near-to-far submission as an overflow
policy; double-banked command regions with bulk DMA; VDP1 `JUMP_CALL`/`JUMP_RETURN`
command reordering; integer octagonal distance instead of `sqrt`; insertion sort for
small N; adaptive frame pacing clamped to a small vblank range; dual-SH-2 with
FRT-flag signalling, duplicated scratch globals and a cache-through result buffer;
and spin-count-driven adaptive load balancing.

---

## 8. Concrete adoption candidates

Recorded in the vocabulary of [`PROVENANCE.md`](PROVENANCE.md). Nothing below has
been adopted; this section is a menu, not a decision.

### A. DIVU-scheduled Q16.16 vertex projection — **already in this repository**

| Field | Record |
|---|---|
| Upstream | In-tree: `src/port/saturn/castleviewer/main.c:434-491` (`project_vertex`, `world_to_view_project`), itself a recorded close-port of libmic3d's MIT projection schedule |
| Licence | Project-internal; the upstream lineage is MIT (see `UPSTREAM_CODE_LEDGER.md`) |
| Reuse mode | **Internal reuse / promote to shared** — lift into `src/port/saturn/gfx/` and call from `resolve_triangle` |
| Buys | Ranks 1 and 3 of §6 combined. Removes ~325,000 soft-float calls/frame and the two `__divsf3` per corner |
| Risk | Medium — precision. The existing `mtxq_ctor_diff_test.c` / `mtxf_lookat_host_diff_test.c` pattern is the acceptance gate |

**This should be evaluated before any external adoption.** It is the same technique
as SlaveDriver's and SGL's, it is already written for this project's types, and it
is already proven at ~15 FPS in the castleviewer target.

### B. Jo Engine `jo_fixed_mult` — `dmuls.l` + `xtrct`

| Field | Record |
|---|---|
| Upstream | `https://github.com/johannes-fetz/joengine`, pinned `556d081146211b6a1cfa6591d70f9487d406758b` |
| Licence | Root `LICENSE` MIT; **per-file header BSD-3-Clause** (`math.c:1-27`). Honour the per-file header. Onward attribution to **Ponut64** (PRs #15/#17/#19) is required by `math.c:47-55` |
| Files | `jo_engine/math.c:57-70` (14 lines), plus `typedef int jo_fixed;` from `jo/types.h:49`. **No other dependency** — `math.h` has zero `#include` directives and the asm needs no `jo_core`, allocator or SGL |
| Reuse mode | **Direct copy**, isolated, with a differential test against the existing `sm64_saturn_q16_mul` |
| Buys | Replaces `(int64_t)a * b >> 16` with a 4-instruction sequence. GCC's lowering already uses `dmuls.l` but then shifts a 64-bit pair; `xtrct` extracts the Q16.16 middle in **one** instruction. ESTIMATED a few cycles per multiply — small individually, meaningful across ~60,000 multiplies/frame once the pipeline is integer |
| Risk | Low, but **must be tested** — the adjacent `jo_fixed_dot` in the same file has miscompiling constraints and `jo_matrix_mul` accumulates into a `float`. Copy the one function, not the file |

### C. Jo Engine DIVU register sequence — **as a starting point, to be improved**

| Field | Record |
|---|---|
| Upstream | Same repo/commit, `jo_engine/math.c:109-144` |
| Licence | BSD-3-Clause per-file, attribution to Ponut64 |
| Reuse mode | **Pattern-only or close-port with a required modification** |
| Buys | Correct SH7604 DVSR/DVDNTH/DVDNTL addresses and the 64/32 Q16.16 sequence, with the 39-cycle latency documented |
| **Required change** | **Split into `start` / `collect`.** Jo's version writes DVDNTL then reads it on the next instruction, stalling the full 39 cycles. SlaveDriver (`WALLASM.S:284→350`) and SGL (FAQ §3-4) both fill that shadow with the remaining matrix rows. Adopting Jo's version unmodified inherits the stall it was chosen to avoid. Also add a DVCR check |
| Note | libyaul already exposes `cpu_divu_fix16_set` / `cpu_divu_quotient_get`, which castleviewer uses. Candidate A may make this redundant |

### D. SlaveDriver DIVU latency-hiding schedule

| Field | Record |
|---|---|
| Upstream | `Lobotomy-Software/SlaveDriver-Engine`, pinned `a8986591557b6e680550d3c23970284d3b38ff8f` |
| Licence | GPL-3.0-or-later. Copyable under the project's existing policy; must live in an isolated GPL component (`src/port/saturn/gpl/`), retain the copyright notice, licence text, pinned commit and a change notice — exactly as `SLAVEDRIVER_ADAPTATION.md` already specifies for the DMA queue |
| Files | `WALLASM.S:231-372` (`normTransform`, the transform+divide+light kernel), `WALLASM.S:106-146` (`rectTransform`), `WALLS.C:1017-1027` (the C-level three-way interleave), `UTIL.H:171-213` (the DIVU macros and the 37-clock documentation) |
| Reuse mode | **Pattern-only for the schedule; direct-copy eligible for `UTIL.H:178-213`** (which `UTIL.H:168` notes came from the GCC FAQ) |
| Buys | The scheduling discipline behind candidate A, plus the `mac.l`+`xtrct` dot product and the free `int16→Q16.16` store. Also documents the honest limit: `WALLASM.S:31`, *"can't do much during the divide wait, too bad"* — the fast paths exist precisely because they have work to fill the shadow |
| Risk | Medium — SH-2 assembly, and the data contract must be adapted to our vertex layout |

### E. SlaveDriver dual-SH-2 pattern

| Field | Record |
|---|---|
| Upstream | Same repo/commit |
| Licence | GPL-3.0-or-later, same isolation requirement |
| Files | `WALLS.C:1806-1819` (`slaveDraw`), `:1822-1919` (`drawSlaveWalls`), `:1921-1950` (slave main + `startSlave`), `:1272-1273` (cache-through result alias), `:1808-1810` (purge discipline), `:2273-2285` (**the spin-count auto-balancer**), `:593-625` / `:679-680` / `:1253` (duplicated slave scratch globals) |
| Reuse mode | **Close-port** — libyaul's `cpu_dual_*` API replaces the raw SMPC/FTCSR pokes; the ownership model, cache discipline and load-balancer transfer directly |
| Buys | Up to ~2× on the geometry stage. Strictly less overall — command emission stays serial on the master |
| Risk | High (dual-CPU coherence), and it should follow ranks 1–4, not lead them. But **the diagnosis's Rank-6 dismissal was written against an 80×-over-budget frame**; at 5–10× over budget a clean 1.6–2× is no longer irrelevant |

### F. Sonic Z-Treme — **lessons only, no code**

The licence contradiction stands; nothing is copyable. The transferable lessons,
already restated in §3.2 and §7: hot/cold LWRAM→HWRAM promotion of exactly the
per-frame-touched structures (`ZT_LOADING.c:321-353`); tri-state hierarchical
frustum culling with INSIDE-propagation (`ZT_FRUSTUM.c:145-165`); near-to-far
submission as an overflow policy (`ZT_RENDERING.c:494-503`); delta-timed logic
driven by a measured FRT frame delta (`ZT_SYSTEM.c:53-71`); render-before-simulate
ordering to keep the slave busy (`SRC/game.c:772-775`).

### G. Explicit non-adoptions

- **Jo Engine's 3-D pipeline** — it is an SGL wrapper; the transform is inside a
  proprietary Sega binary that no permissive licence covers.
- **Jo Engine's `jo_fixed_dot`, `jo_matrix_mul`, `jo_fixed_pow`, trig and sqrt** —
  respectively: miscompiling asm constraints; a `float` accumulator; returns
  x^(n+1); routes through soft-double; bit-by-bit with no table.
- **SlaveDriver's world renderer** — its sector assumptions do not match SM64's
  scene graph, as `RENDERER_PRIOR_ART.md:211-213` already records.
- **Any Z-Treme source.**

---

## 9. Incidental findings (reported, not fixed)

1. **A live wire-format leak.** `lib/src/guMtxF2L.c:101-108`, `guMtxIdent()`, under
   `GBI_FLOATS` calls `guMtxIdentF(m->m)` — writing raw IEEE floats directly into the
   wire `Mtx`, bypassing the float→Q16 conversion that the same file performs
   correctly at `:71-80` for translate/scale/rotate/ortho/perspective. Its only
   caller is `src/game/ingame_menu.c:128`, `create_dl_identity_matrix()`. The
   measured capture confirms it: `dbg_root_mtx_decoded_m22 = 1065353216` = `0x3F800000`
   = the bit pattern for `1.0f`, decoded on a Q16.16 wire where `1.0` is `65536`;
   `dbg_root_mtx_params = 2` (LOAD, no push) and `stack_depth = 1` match that call
   site exactly. Every HUD/menu identity matrix loads as `16,256.0` on the diagonal.
   `dbg_mp_compose_overflowed_ever = 1` in the same capture — this is the likely
   remaining source of MP-compose overflow.
2. **`sm64_saturn_fast3d_quad_map_bind`** (`saturn_fast3d_frontend.c:202-243`, scan at `:231-240`) does a
   linear scan of the whole quad-map table on every display list entered — 239 binds
   per frame — **and the table is in uncached cart memory**. It serves 86 merges.
3. **`fog_dropped_triangles` = 349** — 38 % of emitted geometry silently loses fog.
   Documented degradation, not a bug; noted for the fidelity ladder.
4. **The `lit_vertices` + `unlit_vertices` counters already answer the diagnosis's
   open question §7.5.** Vertices loaded per frame is **3,833**, MEASURED — not
   "~10 per `G_VTX`, bounded at 32". The per-frame cart-traffic figure in
   `PERFORMANCE_DIAGNOSIS_2026-07-26.md` §4.4 can be upgraded from derived to
   measured without a new capture.

---

## 10. Open questions

1. **What does one fp-bit soft-float call actually cost, in cycles, on this
   target?** Still unmeasured, and it is the width of §5.1's 43 %–86 % band. One
   FRT-timed microbenchmark settles it and converts this study's central claim from
   a bound into a number. **Highest value per hour of anything in this document.**
2. **What does `resolve_triangle` cost as a phase?** There are no phase timers
   anywhere in sourceboot (`saturn_fast3d_frontend.h` has 60+ counters and zero
   timing fields). An FRT delta around traverse / resolve / emit / upload / wait
   would replace §5's entire estimate with a reading.
3. **What is the A-bus latency?** The harness at `hwtest/main.c:163-190` exists,
   measures exactly the right four things, and **has never produced a non-zero
   reading**. Rank 4 cannot be sized honestly without it.
4. **Is the castleviewer 14.93 FPS / 1,032-command pairing real?** They come from
   adjacent captures. A single capture recording both would turn the study's most
   important internal control from ±20 % into a measurement.
5. **How much does converting `resolve_triangle` to Q16.16 actually buy?** §5.1
   predicts 40–80 % of the frame. That is a falsifiable prediction and it should be
   treated as one: build it behind a flag, capture both, compare.
6. **What is the right fixed-point format for the transform?** The render matrix is
   committed to Q16.16; the PS1 port chose Q20.12 for range. `dbg_first_w_reject_mx`
   = −8191 and `dbg_mp_compose_overflowed_ever` = 1 in the measured capture suggest
   range is already marginal. Histogram real operand magnitudes before choosing.
7. **Does SM64's existing `GEO_CULLING_RADIUS` machinery give a usable coarse
   visibility pass?** It is the only spatial information the content actually
   carries. Whether it can cut the 2,311 transformed count meaningfully is unknown.
8. **Is one SM64 tick really one rendered frame?** `sourceboot/main.c:295` runs
   exactly one `game_loop_one_iteration()` per iteration, so it should be 1:1, but
   `gGlobalTimer` has never been captured. If it is not 1:1, §5.5's attribution is
   proportionally wrong. Also worth stating: at 15 FPS with a 30 Hz simulation, the
   reference engines' "decouple the tick" lesson **inverts** — running two ticks per
   render would make things worse, and running one tick per render means the game
   runs at half speed. That is a design decision, not a performance one, and it
   should be made deliberately.
9. **How faithfully does Ymir model SH-2 memory stalls and A-bus latency?**
   Unchanged from the diagnosis's open question §7.4. If it under-models them, real
   hardware is slower than everything here assumes.

---

## Appendix: method and reproduction

No build was run and no capture was taken. Everything is static analysis of the
committed ELF, committed capture JSON, and the pinned upstream trees.

```sh
export PATH="/d/tmp/gcc15sh/sh-gcc-toolchain/work/bin:$PATH"
ELF=build/saturn/sourceboot/e2-bob/obj/sm64-saturn-sourceboot-e2.elf
md5sum "$ELF"                                    # 8305522447a477d1717772e9fdedf57c

# §5.1 — resolve_triangle extent, then every jsr target resolved
sh-elf-nm -n --print-size "$ELF" | grep resolve_triangle
#   06070990 00000a50 t _sm64_saturn_fast3d_resolve_triangle
sh-elf-objdump -d --start-address=0x06070990 --stop-address=0x060713e0 "$ELF"
# For each `jsr @rN`, take the most recent `mov.l <pool>,rN  ! <addr> <symbol>`
# annotation objdump emits for that register. Loop bodies are delimited by
# backward branch targets; the corner loop is 0x6070b86..0x6070db8, guarded by
# `cmp/eq #3,r0` at 0x6070db0.

# §4 — per-frame counters
python tools/saturn/fast3d_profile_decode.py \
  docs/saturn/evidence/reports/e2-sourceboot-quadmerge-freeroam-2026-07-26.json
```

Reference trees were read at the pinned commits listed at the head of this
document. No file in any upstream tree was modified.

Cross-references: [`PERFORMANCE_DIAGNOSIS_2026-07-26.md`](PERFORMANCE_DIAGNOSIS_2026-07-26.md),
[`../superpowers/specs/2026-07-26-soft-float-replacement-design.md`](../superpowers/specs/2026-07-26-soft-float-replacement-design.md),
[`SEGMENT_ADDRESSING_DECISION.md`](SEGMENT_ADDRESSING_DECISION.md),
[`RENDERER_PRIOR_ART.md`](RENDERER_PRIOR_ART.md),
[`SGL_REFERENCE_NOTES.md`](SGL_REFERENCE_NOTES.md),
[`SLAVEDRIVER_ADAPTATION.md`](SLAVEDRIVER_ADAPTATION.md),
[`UPSTREAM_CODE_LEDGER.md`](UPSTREAM_CODE_LEDGER.md),
[`../../THIRD_PARTY_LICENSES.md`](../../THIRD_PARTY_LICENSES.md).
