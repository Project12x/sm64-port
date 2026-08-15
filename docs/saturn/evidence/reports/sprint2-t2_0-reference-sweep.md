# Sprint 2 Task 2.0 — SlaveDriver / Sonic Z-Treme reference sweep

- Date: 2026-08-15. Read-only sweep; no source changes, no builds.
- Tree: `.worktrees/saturn-recovery`, branch `saturn/recovery`.
- Owner directive (plan `2026-08-15-sprint2-cadence-recovery.md:31-33`): consult
  SlaveDriver and Z-Treme before designing T2.2-T2.4.
- Reference clones (GPL, referenced liberally per standing instruction):
  - `work/upstream/slavedriver-engine` @ `a898659`
  - `work/upstream/sonic-z-treme` @ `cff7545` (SGL 3.02j + Sega SGL docs)
- Every claim below cites a file and line I opened. Where an engine has no
  analogue for one of our mechanisms, that absence is recorded as the finding.

---

## 1. Memory-tier placement policy

### 1.1 SlaveDriver

SlaveDriver has no linker script in the tree. Tiering is expressed entirely by
a two-area LIFO allocator:

- `UTIL.C:344-346` — `mem1Start = 0x200000` (LWRAM), `mem2Start = (int)&amp;end`
  (HWRAM, immediately past `.bss`).
- `UTIL.C:348-353` — `mem_init` sets `areaEnd[0] = 0x0300000` (LWRAM top) and
  `areaEnd[1] = 0x6100000` (HWRAM top).
- `UTIL.C:366-386` — `mem_nocheck_malloc(area, size)`: bump allocator with an
  8-deep undo stack, and a *fallback* that spills to the other area when the
  requested one is full.

The policy is therefore in the call sites, and it is unambiguous:

| Tier | Contents | Citations |
| --- | --- | --- |
| HWRAM (area 1) | **All level geometry**: sectors, walls, vertices, faces, objects, push-blocks, wave verts, texture indices, vertex lighting, cut-plane matrix | `LEVEL.C:27-30` (`LOADPART` macro → `mem_malloc(1, size)`), applied at `LEVEL.C:51-64` |
| HWRAM (area 1) | Frame-sized scratch for effects/bitmaps | `BIGMAP.C:120`, `INITMAIN.C:404-405`, `PIC.C:511-513,545-547`, `SRUINS.C:1107` |
| LWRAM (area 0) | Sound sample buffers, compressed picture/sequence staging, localized text, backup-RAM work, AI frame tables | `SOUND.C:207`, `PIC.C:574,588,603`, `PICSET.C:27,37,51`, `SEQUENCE.C:31,100`, `LOCAL.C:25`, `BUP.C:60,62`, `AI2.C:332` |

Per-frame scratch is *statically* placed in HWRAM `.bss`, not malloc'd, and is
deliberately aliased to reuse one arena for two disjoint lifetimes:

- `WALLS.C:1234-1238` — `struct doorwayCache doorwayCache[MAXNMWALLS]` (per-wall
  screen bbox + distance cache).
- `WALLS.C:1248-1249` — `slaveResult` is *the same storage*:
  `static struct slaveDrawResult *slaveResult = (struct slaveDrawResult *)doorwayCache;`
- `WALLS.C:1253` — `slave_vCalc` is placed immediately after the slave result
  region inside the same buffer.
- `MENU.C:127-128,971-972` and `INTRO.C:573-574` re-alias the very same array as
  generic menu/intro temp storage.

This is exactly the pattern our port already uses for
`sourceboot_vdp1_cmdts[0]` as the upload stage (T1 surprise #3), independently
arrived at by the reference engine.

**The cross-CPU discipline is explicit.** The slave writes its results through
the cache-through mirror:

- `WALLS.C:1272-1273` and `WALLS.C:1377-1378` —
  `cacheThruResult = (struct slaveDrawResult *)(((int)slaveResult) + 0x20000000);`
- `WALLS.C:1808-1810` and `WALLS.C:1826-1828` — both CPUs explicitly purge/enable
  the cache (`*CACHECNTRL = 0x10; *CACHECNTRL = 0x01;`) at the top of their
  respective halves.

Our `.uncached` / `DEMO_CROSS_CPU_SHARED` arrangement (`_source_scene_owner`,
2,224 B, T1 table) is the same construct.

**Cart-mapped space: no analogue.** SlaveDriver is a CD title; there is no
A-bus cart tenancy in the tree. Cold data is streamed from CD into LWRAM
(`FILE.C:363-366`) and decompressed forward. Recorded as a genuine absence.

### 1.2 The DMA guard — direct corroboration of the linker ASSERT

`DMA.C` refuses SCU-DMA and silently degrades to `memcpy` in exactly two cases:

```c
/* DMA.C:88-91 */
 if (
     from&lt;(void *)0x06000000 || from&gt;(void *)0x06100000 ||
     (to&gt;=(void *)0x00200000 &amp;&amp; to&lt;=(void *)0x00300000))
    {qmemcpy((void *)to,from,size);
```

and, on the queued path:

```c
/* DMA.C:60-68 */
     dmaQ[qTail].from&lt;0x06000000 ||
     dmaQ[qTail].from&gt;0x06100000)
    {qmemcpy(...);
```

That is: **SCU-DMA source must be HWRAM, and SCU-DMA destination must not be
LWRAM.** This is an independent 1996-era confirmation of the rule our
`sourceboot-cart.x` encodes as a hard ASSERT and which T1 identified as the
config-attributable regression class (`sprint2-t1-hwram-attribution.md:241-247`).
No design in T2.2 may weaken it.

### 1.3 Sonic Z-Treme

Z-Treme's linker script has **exactly one output region, in HWRAM**:

```
/* Compiler/COMMON/nosgl.linker:1-43 */
SECTIONS {
	SLSTART 0x06004000 : { ___Start = .; *(SLSTART) }
	.text ... .rodata ... SLPROG ... .tors ... .data ...
	.bss ALIGN(0x10) (NOLOAD): { ... _end = .; }
}
```

There is no LWRAM output section at all. LWRAM is reached only through a raw
address constant (`ZTE/ZTE_DEF.H:43` — `#define LWRAM 0x00200000`; the sibling
`HWRAM` macro at `:44` is defined but never used anywhere in the tree), and its
role is exclusively **CD load / decompress staging plus the sound driver**:

| LWRAM use | Citation |
| --- | --- |
| Sound driver program + sound work area | `ZT_AUDIO.c:9-21,30` |
| Map header, CRAM palette, animation table land at `LWRAM`, then `memcpy_l` out | `ZT_LOADING.c:377-380` |
| Whole binary map body loaded to `LWRAM` before unpacking | `ZT_LOADING.c:400`, `:504` |
| Model header/textures staged at `LWRAM` before VRAM upload | `ZT_LOAD_MODEL.c:227-248` |
| VDP2 bitmap staged at `LWRAM` before `Cel2VRAM` | `ZT_VDP2.c:89-131`, `SRC/game.c:141-164` |
| Menu scratch, wiped with `memset_l((void*)LWRAM, 0, 0x0FFFFF)` | `ZT_MENU.c:24,38` |

Everything the frame touches is moved *out* of LWRAM into HWRAM before play:

- `Common.h:43` — `HWRAM_DYNAMIC_MEM_SIZE (300*1024)`; `ZTE/workarea.c:49-50`
  reserves `DYNAMIC_RAM[128*1024]` and `HWRAM_DYNAMIC_MEM[300*1024]` as HWRAM
  `.bss`.
- `ZT_LOADING.c:299-318` — `loadNodes` bump-allocates every octree node and its
  entity table into `HWRAM_DYNAMIC_MEM_PTR`.
- `ZT_LOADING.c:320-353` — a function literally named
  `/**Temporary function to move the vertices to high work ram***/`
  (`mallocVertices`) DMAs every mesh's `pntbl` into the HWRAM arena and
  repoints `LevelMesh[i]-&gt;pntbl` / `LevelMeshLOD[i]-&gt;pntbl` at the copy.

The commented-out lines at `ZT_LOADING.c:330` and `:364`
(`//HWRAM_DYNAMIC_MEM_PTR=(void*)(LWRAM+(400*1024));`) show the author tried
leaving the hot arena in LWRAM under the SatLink debug path and backed it out.

**The SGL frame work area is pinned in HWRAM.** `Common.h:26` sets
`WORK_AREA 0x060C0000`, and `ZTE/workarea.c:12-21` lays out, in order:
`sort_list`, `zbuffer`, `spritebuf`, `pbuffer`, `clofstbuf`, `commandbuf`.
The Z sort buckets, the per-command sprite records, the projected-vertex buffer
and the master→slave command ring are all HWRAM residents. `Common.h:28,17,32-33`
place `trans_list` at `0x060FB800`, the system-variable page at `0x060FFC00`,
the master stack at `0x060FFC00` and the slave stack at `0x06001E00` — all HWRAM.

**Cart-mapped space: no analogue.** Z-Treme is a CD title with no A-bus cart
tenancy. Recorded as a genuine absence.

### 1.4 Tier-policy synthesis

Both engines converge on the same three-line policy, and neither one violates it:

1. Everything the frame reads or writes more than once lives in HWRAM.
2. LWRAM is a **staging tier** — CD landing zone, decompression scratch, audio
   sample store — never a working-set tier.
3. When HWRAM runs out, the answer is **aliasing two disjoint lifetimes onto one
   arena** (SlaveDriver `doorwayCache`/`slaveResult`), not demotion of a hot
   tenant.

Our current arrangement — `_s_bob_hot_workarea` (43,776 B), actor scratch
(10,304 B) and `_sourceboot_fast3d` (44,616 B) in `.lwram_bss`
(`sprint2-t1-hwram-attribution.md:104-114,248-255`) — is the one configuration
neither reference engine ever adopts.

---

## 2. Draw-order machinery

### 2.1 SlaveDriver — no per-command depth sort exists

This is a finding by absence, and it is the important one for T2.3's scope.
SlaveDriver never sorts VDP1 commands. It sorts **sectors**, then emits their
commands in that order, and uses VDP1 link patching only at sector granularity.

The pipeline, in `drawWalls` (`WALLS.C:2061-2270`):

1. **Portal flood** builds the visible sector set with screen-space bounding
   boxes (`WALLS.C:2100-2113`, `findDoorways`), producing `updateList[]`.
2. **Dependency DAG** (`WALLS.C:2131-2135`, `buildTree` at `WALLS.C:1958-1980`):
   each sector records `ancestor[]` / `nmAncestors` / `nmChildren`
   (`WALLS.C:74-84`, `MAXFANIN 20`, struct size 60 B).
3. **Leaf list + sort** — `sortLeafList` (`WALLS.C:1986-2058`) is two passes:
   - `WALLS.C:1991-2001`: a plain **insertion sort by `distance`**, descending.
     Not a bucket sort, not a radix — insertion, on a list that is only as long
     as the current leaf frontier (typically single digits).
   - `WALLS.C:2003-2052`: a **cut-plane correction pass** — for sector pairs
     sharing a `cutChannel`, a precomputed plane index
     `(*level_cutPlane)[s1-&gt;cutIndex][s2-&gt;cutIndex]` selects a wall whose normal
     is dotted against the camera offset (`MTH_Product`, `WALLS.C:2039-2041`) to
     decide which sector must be painted later; the loser is rotated backwards
     in the list.
4. **Topological drain** (`WALLS.C:2183-2233`): repeatedly take the sorted-front
   leaf, append to `drawList`, decrement its ancestors' `nmChildren`, promote any
   new leaves, **re-sort the (short) leaf list**, repeat. A cycle-breaker exists
   for the degenerate case (`WALLS.C:2192-2214`).
5. **Reverse for painting** (`WALLS.C:2236-2238`):
   `updateList[i] = drawList[updateListSize-i-1]` — far-to-near.

Per-frame cost: `O(V)` for the flood + DAG, then `O(V)` insertion sorts of a
short frontier, i.e. small-constant `O(V^2)` worst case where `V` is the number
of *visible sectors* (tens), never the number of commands (~1,500).

**Link patching is coarse and rare.** Only three `EZ_linkCommand` sites exist in
the whole engine:

- `WALLS.C:2263` — terminate a per-sector sprite block with `JUMP_RETURN`.
- `WALLS.C:2269` — `EZ_linkCommand(lastWallCmd, JUMP_ASSIGN, EZ_getNextCmdNm());`
  one jump that skips the master's sprite blocks so the slave's wall commands
  can be spliced in.
- `WALLS.C:1888-1889` — `JUMP_CALL` into a sector's pre-built sprite block as the
  slave's per-sector end marker is consumed.

So the entire frame costs **three classes of link write**, on the order of one
per visible sector, against our current 64 × ~1,800 rescan. `EZ_linkCommand`
itself (`SPR.C:443-458`) is careful to patch the *staging buffer* if the target
command has not been flushed to VRAM yet, and VRAM otherwise.

### 2.2 Z-Treme — SGL's two-level bucket sort, verbatim

Z-Treme does not implement its own sort. Every draw call ends in
`slSetSprite(&amp;user_sprite, drawPrty)` with an explicit depth key
(`ZTE/ZT_SPRITES.H:59-80` for polygons, `:41-56` for scaled sprites, `:82-94`
for normal sprites) or in `slPutPolygon` / `slPutSprite`
(`ZT_RENDERING.c:469,479,299`). Ordering is SGL's.

SGL's algorithm is documented in
`Documentation/DOC/210A_US/SGLFAQ_F.TXT`, section 3-6 "SGL Z-sort processing and
the Z-data buffer", lines 1000-1120. The load-bearing paragraph
(`SGLFAQ_F.TXT:1057-1071`):

&gt; The sorting that determines priority uses a method that registers the address
&gt; of display data in the buffer corresponding to the Z position and then selects
&gt; the data in sequence, starting from that which is farthest away along the Z
&gt; axis. While this method does promise to be faster since there is less
&gt; comparison processing, it does consume memory and there is some wasted
&gt; processing due to reading empty buffers where no data is registered. In order
&gt; to reduce these inefficiencies, the Z position (15 bits) is divided into an
&gt; upper byte and a lower byte. Sort processing is then performed first on the
&gt; upper byte, followed by a secondary sort of those Z positions where entries
&gt; were made.

Structure (`SGLFAQ_F.TXT:1073-1104`, and the ASCII diagram there):

- A **bucket array indexed by Z**, entries are pointers to polygon records;
  `NULL` means empty.
- Each polygon record is **36 bytes**: 30 bytes of literal VDP1 command payload
  (`CMDCTRL`..`CMDGRDA`), then `CMDZPOS` (the sort key) and `CMDNEXT` (the
  intrusive bucket chain pointer). Confirmed independently by the C declaration
  at `Compiler/SGL_302j/INC/SL_DEF.H:425-442`
  (`SPRITE_T` = 15 × `Uint16`, then `Sint16 Z`, then `Uint32 *NEXT`).
- Bucket sizes are fixed and small: `Zbuffer` = 128 entries (window 0),
  `Zbuffer2` = 128 (window 1), `Zbuf_nest` = 256 (the secondary/refine pass).
  Documented at `Documentation/DOC/210A_US/MEMORY.TXT:33-43` and asserted in the
  header comment `SL_DEF.H:846` — `/* (128 + 128 + 256) * 4 Bytes fix */`.
  Verified against the shipped binary work area: `Documentation/libsgl/sglarea.S`
  gives `_Zbuffer = 0x060C5ED4` and `_SpriteBuf = 0x060C66D4`, a gap of exactly
  `0x800` = 2,048 B = `(128+128+256) * 4`.
- Drain (`SGLFAQ_F.TXT:1108-1112`): *"Transfers to the hardware (VDP1) use
  SCU-DMA indirect mode. The Z sort buffer described above is read in sequence
  starting from the back, and the appropriate data address is set in the transfer
  table."* The transfer table is `SortList`, sized `(MAX_POLYGONS+6) * 3 * 4`
  bytes — three longwords (size / destination / source) per command
  (`workarea.c:14-15`, `MEMORY.TXT:25-27`).

**The decisive structural point for us:** SGL never encodes draw order in
`CMDLINK`. Order is carried by the *bucket chain* in out-of-band metadata
(`Z`, `NEXT` — the 6 bytes past the 30-byte payload), and is realised by the
*order of the DMA transfer descriptors*. Commands therefore land contiguously in
VDP1 VRAM already in painter order and VDP1 walks them with plain
sequential link. Z-Treme's own command builders bear this out — they write a
constant `LINK` and never revisit it (`ZT_SPRITES.H:65`, `:20`, `:34`, `:45`).

Per-frame cost: one pass over the emitted commands to bucket them
(`O(N)`), plus a scan of 128 coarse buckets and 256 refine slots
(`O(bins)` — 384 total, *not* `O(bins × N)`), plus one pass to write the DMA
table (`O(N)`).

### 2.3 Both engines: order is decided upstream, not on the command array

- SlaveDriver: sector-level topological order + insertion sort of a short leaf
  frontier; commands inherit it by emission order.
- Z-Treme/SGL: per-command key, but bucketed once into chains and drained once —
  the command array is written *in* final order, never re-walked.

Neither engine performs a full rescan of the command array once per bin. That is
the specific inefficiency our current `link_depth_bins` has
(`src/port/saturn/gfx/saturn_vdp1_backend.h:198-211`: outer loop over
`bin_count`, inner loop over all live commands, 64 × ~1,800 ≈ 115k iterations
per frame, each dereferencing a 32-byte record).

---

## 3. VDP1 command-buffer strategy

### 3.1 SlaveDriver — banks in VRAM, a 10 KB staging window in HWRAM

`SPR.C:65-94` (`EZ_initSprSystem(nmCommands, nmCluts, nmGour, ...)`) lays out
VDP1 VRAM as byte offsets from `VRAM_ADDR`:

```
commandStart[0] = 64
commandStart[1] = commandStart[0] + (nmCommands&lt;&lt;5)
clutStart       = commandStart[1] + (nmCommands&lt;&lt;5)
gourStart[0]    = clutStart      + (nmCluts&lt;&lt;5)
gourStart[1]    = gourStart[0]   + (nmGour&lt;&lt;3)
charStart       = gourStart[1]   + (nmGour&lt;&lt;3)
```

The **two command banks live in VDP1 VRAM**, not in work RAM. `EZ_openCommand`
(`SPR.C:128-138`) just toggles `bank` and resets the write cursors.

The only work-RAM cost is a write-combining staging window:

- `SPR.C:20-21` — `#define CMDBUFFERSIZE 256`, `#define GOURBUFFERSIZE 256`
- `SPR.C:35-36` — `static struct gourTable gourBuffer[GOURBUFFERSIZE];`
  `static struct cmdTable cmdBuffer[CMDBUFFERSIZE];`
- Sizes: 256 × 32 B = **8,192 B** commands + 256 × 8 B = **2,048 B** Gouraud =
  **10,240 B of HWRAM total.**
- `SPR.C:159-163` — `getCmdTable()` returns the next staging slot and flushes
  when full; `SPR.C:141-148` — `flushCmdBuffer()` DMAs the accumulated block into
  the VRAM bank and advances the VRAM cursor.

Live capacities actually shipped:

| Scene | `nmCommands` | `nmCluts` | `nmGour` | Citation |
| --- | ---: | ---: | ---: | --- |
| **In-game (main)** | **1,540** | 8 | 1,524 | `INITMAIN.C:567`, `SRUINS.C:2392` |
| In-game (alt path) | 1,448 | 4 | 1,224 | `SRUINS.C:1879` |
| Automap | 1,248 | 4 | 1,024 | `BIGMAP.C:173` |
| Menus / intro | 500-1,000 | 4-8 | 500-1,000 | `INITMAIN.C:190,356`, `INTRO.C:401,580`, `UTIL.C:190` |
| FMV playback | 600 | 8 | 600 | `MOV.C:260` |

At the in-game figure the VRAM budget is
`64 + 2×49,280 + 256 + 2×12,192 = 123,264 B`, leaving ~400 KB for characters —
guarded by `assert(((int)charStart) &lt; 1024*512)` at `SPR.C:113`.

**Overflow is clamped, never fatal.** `SPR.C:142-143`:

```c
 if (cmdBufferUsed+totCommand&gt;commandAreaSize)
    cmdBufferUsed=commandAreaSize-totCommand;
```

and `EZ_getNextCmdNm` (`SPR.C:430-441`) saturates its returned index at
`maxc-1`. Upstream of that, whole primitives are rejected before they are built:
`WALLS.C:1278-1279` (`if (height*width+nmSlavePolys+50&gt;MAXNMSLAVEPOLYS) return;`)
and `WALLS.C:1380`. The `+50` is a deliberate margin.

The slave's intermediate buffer is capped at `MAXNMSLAVEPOLYS 1300`
(`WALLS.C:1240`) records of 28 B (`WALLS.C:1243-1247`), aliased onto
`doorwayCache` (`WALLS.C:1248-1249`).

### 3.2 Z-Treme / SGL — records in HWRAM, commands DMA'd to VRAM in sorted order

Z-Treme keeps its command *records* in HWRAM and lets SGL DMA the 30-byte
payloads into VDP1 VRAM at draw time. Capacities from `Common.h:21-22`:
`MAX_VERTICES 2800`, `MAX_POLYGONS 1900`.

Derived from `ZTE/workarea.c:14-21`, at `WORK_AREA = 0x060C0000`:

| Arena | Formula | Bytes | HWRAM address |
| --- | --- | ---: | --- |
| `sort_list` (DMA descriptor table) | `(1900+6) × 3 × 4` | 22,872 | `0x060C0000` |
| `zbuffer` (Z buckets) | `512 × 4` | 2,048 | `0x060C5958` |
| `spritebuf` (**command records, both banks**) | `36 × (1900+6) × 2` | **137,232** | `0x060C6158` |
| `pbuffer` (projected vertices) | `16 × 2800` | 44,800 | `0x060E7968` |
| `clofstbuf` (light colour table) | `32 × 3 × 32` | 3,072 | `0x060F2868` |
| `commandbuf` (master→slave ring, nominal) | `32 × 1900` | 60,800 | `0x060F3468` |

`spritebuf` at **137,232 B** is the direct analogue of our 131,072 B cmdt
double-buffer: two banks of 1,906 records, in HWRAM, per frame. Ours is
2 × 2,048 × 32 = 131,072 B. The reference engine spends *more* HWRAM on this
than we do, and does so deliberately.

**Capacity constants are nominal ceilings, not reserved footprints.** Verified
against SGL's own shipped default work area, disassembled at
`Documentation/libsgl/sglarea.S:8-45`:

| Symbol | Value | Note |
| --- | --- | --- |
| `_MaxVertices` | `0x09C4` = 2500 | |
| `_MaxPolygons` | `0x06E1` = 1761 | |
| `_SortList` / `_SortListSize` | `0x060C0000` / `0x52D4` = 21,204 | = `(1761+6) × 12` ✓ |
| `_CLOfstBuf` | `0x060C52D4` | |
| `_Zbuffer` | `0x060C5ED4` | gap to `_SpriteBuf` = `0x800` ✓ |
| `_SpriteBuf` / `_SpriteBufSize` | `0x060C66D4` / `0x1F0B0` = 127,152 | = `36 × (1761+5) × 2` ✓ |
| `_Pbuffer` | `0x060E5784` | gap `0x9C40` = `16 × 2500` ✓ |
| `_CommandBuf` | `0x060EF3C4` | |
| `_TransList` | `0x060FB800` | |

`_TransList − _CommandBuf = 0xC43C = 50,236 B = 1,569 command slots`, against a
declared `MaxPolygons × 32 = 56,352 B`. **SGL ships with 191 fewer slots
physically reserved than declared.** Z-Treme, having raised `MAX_POLYGONS` to
1,900, is tighter still: `0x060FB800 − 0x060F3468 = 0x8398 = 33,688 B =
1,052 slots` against a declared 60,800 B.

The engine works because the master→slave command ring never approaches its
declared depth — the constant is a ceiling on *calculable* polygons, and the
frame never gets there. This is the strongest available evidence that a declared
VDP1 command capacity and the observed per-frame peak are different numbers.

**VDP1 VRAM budget** is derived from the same constant:
`ZTE/ZT_DEF.H:63` — `TEXTURE_BASE_ADDRESS ((MAX_POLYGONS+6)*32 + MAX_POLYGONS*8)`
= `60,992 + 15,200 = 76,192 B` for command tables plus Gouraud tables, textures
after.

**Overflow behaviour, documented and designed for.**
`Documentation/DOC/210A_US/MEMORY.TXT:16-21`:

&gt; slPutPolygon() does not process a model if the number of polygons and vertices
&gt; used in that specified model is exceeds the maximum for each parameter. ...
&gt; When the maximum number is exceeded, the processing of data is halted.

and Z-Treme arranges emission order so that "halted" costs the least:

```c
/* ZT_RENDERING.c:494-503 */
/**Near to far traversal of the octree. If the sprite or vertex buffer gets
   full, at least you will still see the sprites close to camera and skip
   those further away**/
int i = ((camPos[X] &lt; curNode-&gt;bv.x) ? 0:1) | ... ;
if (curNode-&gt;child[i] &gt;= 0)   traverse_octree(nodes[curNode-&gt;child[i]], ...);
if (curNode-&gt;child[i^1] &gt;= 0) traverse_octree(...);
...
```

Depth keys are chosen per polygon by sort mode
(`ZT_RENDERING.c:242-252` implements `SORT_MAX` / `SORT_MIN` / 4-point average;
modes enumerated at `SL_DEF.H:394-397`, attribute field at `SL_DEF.H:248`).

---

## 4. Bearing on the 53-VBlank scene-construction bottleneck

### 4.1 Frame pacing — both engines measure, clamp, and adapt

**SlaveDriver** runs a hysteretic pacing controller in the main loop
(`SRUINS.C:2239-2268`):

```c
     if (vtimer&lt;smoothVTime)
	{vspeedSwitchCount++;
	 if (vspeedSwitchCount&gt;10)
	    {smoothVTime=vtimer; if (smoothVTime&lt;1) smoothVTime=1; ...}
	}
     ...
     while (vtimer&lt;smoothVTime) ;
     SCL_DisplayFrame();
     if (vtimer-1&gt;smoothVTime)
	{smoothVTime=vtimer-1; if (smoothVTime&gt;2) smoothVTime=2; ...}
     monsterMoveCounter+=vtimer;
     framesElapsed=vtimer;
```

The frame period is locked to a stable 1-2 VBlanks and only drops after 10
consecutive fast frames, so cadence never oscillates. Simulation is decoupled:
`framesElapsed` is clamped to 8 (`SRUINS.C:2068-2069`) and object logic runs a
fixed-step catch-up loop (`SRUINS.C:2105-2110`).

**Z-Treme** measures the real elapsed time off the FRT and clamps
(`ZT_SYSTEM.c:53-72`):

```c
    unsigned int frm = TIM_FRT_CNT_TO_MCR( TIM_FRT_GET_FRC());
    TIM_FRT_SET_FRC(0);
    ZT_FRAMERATE = (frm/TIM_DIV);
    if (ZT_FRAMERATE &lt; 1) ZT_FRAMERATE=1;
    else if (ZT_FRAMERATE &gt; 10) ZT_FRAMERATE=10;
```

`ZT_FRAMERATE` then scales every per-frame delta (`ZT_RENDERING.c:653,664-690`,
`ZT_GAME.c:103`). `main.c:94-97` selects `slDynamicFrame(ON)` — advance on VDP1
draw-end rather than a fixed period — with `SynchConst = DEFAULT_FRAMERATE = 2`
(`ZTE_DEF.H:62`), i.e. a 30 FPS target with dynamic slip, toggleable at runtime
(`ZT_GAME.c:90-92`).

Note the clamp: **Z-Treme's worst tolerated frame is 10 VBlanks.** We are at 53.

### 4.2 Slave-SH2 work division

**SlaveDriver runs a closed-loop load balancer.** `slaveSize` is the number of
sectors handed to the slave; it is adjusted ±1 every frame by measuring how long
the master spun waiting on the slave's FRT flag (`WALLS.C:2273-2285`):

```c
void drawWallsFinish(void)
{int i;
 /* wait for slave to finish */
 i=0;
 while (!(*FTCSR &amp; 0x80))
    i++;
 *FTCSR=0x0;
 if (i&gt;100 &amp;&amp; slaveSize&gt;0)
    slaveSize--;
 if (i&lt;100 &amp;&amp; slaveSize&lt;50)
    slaveSize++;
 drawSlaveWalls();
```

Dispatch is one register write: `*(Uint16 volatile *)0x21000000 = 0xffff;`
(`WALLS.C:2246`), after which the master draws the far half of `updateList`
(`WALLS.C:2247-2255`) while the slave draws the near half
(`slaveDraw`, `WALLS.C:1806-1819`). The slave writes only geometry results, never
VDP1 commands; the master converts them afterwards
(`drawSlaveWalls`, `WALLS.C:1822-1912`, ending in `EZ_specialDistSpr`). Sector
boundaries are marked in-band with `tile = -1` sentinels
(`WALLS.C:1815-1817`, consumed at `WALLS.C:1882-1904`).

**SGL's split is a non-blocking command ring.**
`SGLFAQ_F.TXT:1136-1143`:

&gt; ... to pass requests from the master CPU through a shared buffer so that
&gt; neither the master CPU nor the slave CPU enter the waiting state ... Whether
&gt; or not a request was output can be recognized when the master CPU's write
&gt; pointer differs from the slave CPU's read pointer. Because this method allows
&gt; the master CPU to proceed to the next process as soon as it adds its request
&gt; to the buffer, there is no need for the master CPU to wait for the slave CPU
&gt; to complete a task.

The pointer pair is `ComRdPtr` / `ComWrPtr` in the system-variable page
(`MEMORY.TXT:104-105`), and SGL 2.0A explicitly moved the slave's poll off the
master's pointer onto the FRT input-capture signal *"through a cache-through
memory area"* to cut cross-CPU traffic (`SGL020A.TXT`, §1.1 item 2).

Z-Treme's own author notes the cost of getting this wrong
(`ZT_RENDERING.c:716-719`):

&gt; MAIN DRAW FUNCTION / Could/should be cleaned up. It also should call draw
&gt; functions ASAP to make the slave SH2 busy, so here it's not super efficient

### 4.3 Culling granularity

| Engine | Unit | Mechanism |
| --- | --- | --- |
| SlaveDriver | **Sector** (portal cell) | Screen-space bbox flood through doorways, `WALLS.C:2100-2113`; per-sector `EZ_userClip` rectangle before drawing, `WALLS.C:2248-2252` and `WALLS.C:1894-1902` (`RECTCLIP 1`, `WALLS.C:27`) |
| SlaveDriver | Wall | `doorwayCache[w].xmin = -32000` marks totally-rejected walls, reused next frame (`WALLS.C:1573`, `1670-1673`) |
| Z-Treme | **Octree node** | `ztCheckBoxInFrustum(boxMin, boxMax)`, `ZT_RENDERING.c:425-428` |
| Z-Treme | Node, hierarchical short-circuit | If the parent tested `INSIDE_FRUSTUM`, children skip the test entirely; only `INTERSECTS_FRUSTUM` re-tests (`ZT_RENDERING.c:425`, `486-492`) |
| Z-Treme | Leaf | Single-plane nodes back-face-rejected against the camera before any projection (`ZT_RENDERING.c:437-451`) |
| Z-Treme | Leaf | Distance LOD swap at 450 units and far cutoff at −250 (`ZT_RENDERING.c:456-481`); entity sprites dropped past LOD level 4 (`ZT_RENDERING.c:470-473`) |

Both engines cull at a **coarse spatial unit and never per-triangle**, and both
carry a rejection counter for tuning (`tot_rejected`, `ZT_RENDERING.c:5,427,
189-190`; SlaveDriver's FRT profiler tree, `PROFILE.C:11-30,66-80`, driven by
`pushProfile("Find Visible")` / `pushProfile("Walls")` at `WALLS.C:2087,2101`
and `SRUINS.C:2095-2098`).

### 4.4 Profiling rig worth copying

SlaveDriver's `PROFILE.C` is a nestable, zero-allocation tree profiler on the
SH-2 free-running timer (cycles/32, `PROFILE.C:12-23`), with 60 nodes and 8
children each (`PROFILE.C:41-45`), keyed by the literal string pointer so
`pushProfile("Walls")` costs a pointer compare (`PROFILE.C:66-70`). This is the
shape T2.1's diagnostic counters should take: fixed tables, no allocation, and
an FRT read rather than a VBlank count for sub-frame resolution.

---

## 5. Lessons applied to T2.2-T2.4

| # | Finding (with citation) | Applies to | Decision it supports |
| --- | --- | --- | --- |
| L1 | SlaveDriver's DMA layer refuses SCU-DMA whose source is outside `0x06000000-0x06100000` or whose destination is inside `0x00200000-0x00300000` (`DMA.C:60-68`, `88-91`) | T2.2 | **Do not weaken the `sourceboot-cart.x` ASSERT.** The `.lwram_cmdts` prohibition is independently corroborated; the cmdt buffer stays in HWRAM regardless of how the reclamation package resolves. |
| L2 | Neither engine places any per-frame working set in LWRAM. LWRAM is CD-landing + decompression + audio only (`UTIL.C:344-353` call sites; `ZT_LOADING.c:377-504`, `ZT_AUDIO.c:9-21`). Z-Treme has a function whose name is literally "move the vertices to high work ram" (`ZT_LOADING.c:320-353`) | T2.2 | **The workarea return is the right shape of fix**, not a workaround. Prefer package (b) (full 54,080 B) over (a) if the gates clear; `_sourceboot_fast3d` (44,616 B) — the interpreter's per-frame matrix/vertex/resolved arrays — is the same class of tenant and should be scheduled for a later rung, not accepted as permanent. |
| L3 | SlaveDriver's whole VDP1 work-RAM cost is a **10,240 B** staging window (`SPR.C:20-21,35-36`); both command banks live in VDP1 VRAM (`SPR.C:71-76`) and are filled by block DMA (`SPR.C:141-157`) | T2.2 (stretch), T2.4 | A **build-in-VRAM-through-a-small-staging-window** architecture would return ~120,832 B of the 131,072 B outright — an order of magnitude more than the whole reclamation package. Out of scope for T2.2 (it changes the transport contract and would need its own CUE), but this is the single highest-value structural lever the sweep surfaced. Record it as a T2.4+ candidate with a concrete precedent. |
| L4 | SlaveDriver ships **1,540** in-game commands (`INITMAIN.C:567`, `SRUINS.C:2392`); Z-Treme declares 1,900 but physically reserves **1,052** (`workarea.c:12-21` vs `Common.h:28`); stock SGL declares 1,761 and reserves **1,569** (`sglarea.S:8-45`) | T2.2 capacity shrink | **2048→1664 is well within reference practice** — above SlaveDriver's shipped figure and above SGL's real reserved figure. The 1536 hard floor plus 128 control/HUD commands is consistent with how the references budget. Still gated on T2.1's measured `command_count` peak; nothing here substitutes for measurement, but nothing here counter-indicates the cut either. The conservative 2048→1792 variant is not required by precedent. |
| L5 | Every reference capacity limit **degrades gracefully**: SlaveDriver clamps the flush (`SPR.C:142-143`) and saturates the index (`SPR.C:430-441`); primitives are rejected before construction with a `+50` margin (`WALLS.C:1278-1279,1380`); SGL "halts processing" on overflow (`MEMORY.TXT:16-21`) | T2.2 capacity shrink safety | Before shrinking, **confirm our overflow path is clamp-and-drop, not assert-or-corrupt.** If the fast3d emitter currently faults or overruns at capacity, add the clamp *first* — that converts a capacity cut from a correctness risk into a visual-quality risk, which the owner look-and-listen gate can actually adjudicate. |
| L6 | Z-Treme orders octree emission **near-to-far specifically so buffer exhaustion drops the farthest geometry** (`ZT_RENDERING.c:494-503`, with the rationale in the comment) | T2.2 capacity shrink safety | If our emission order is far-to-near (which the painter chain implies), a capacity cut drops the *nearest* geometry — the worst possible failure mode. **Check emission order against clamp order.** If they conflict, either clamp by admitting near-first at the scene-construction stage or ensure the drop is applied to the far tail. This makes the 2048→1664 cut safe under the peaks T2.1 measures *and* under an unmeasured outlier frame. |
| L7 | SGL keeps sort metadata **out of band** — `Z` and `NEXT` in the 6 bytes past the 30-byte payload (`SL_DEF.H:425-442`, `SGLFAQ_F.TXT:1073-1104`) — and never uses `CMDLINK` for ordering; order is realised by the **DMA descriptor sequence** (`SGLFAQ_F.TXT:1108-1112`) | **T2.3 counting-sort design** | **Primary design lesson.** Our `link_depth_bins` overloads `cmd_link` as both sort key and output link, which forces the 64 × ~1,800 rescan (`saturn_vdp1_backend.h:198-211`). Separate them: keep an 8-byte side reference (`{command_index, bin}`) — the tree *already has* this exact structure and a working stable radix scatter for the terrain path (`saturn_terrain_depth_bins.h:22-27,74-95`) — and let the link write be a single ordered pass over the sorted references. |
| L8 | SGL's bucket sort is `O(N)` bucket + `O(bins)` drain over 128 coarse + 256 refine slots, with intrusive chaining; its own FAQ names *"wasted processing due to reading empty buffers"* as the cost the two-level split exists to avoid (`SGLFAQ_F.TXT:1057-1071`, bucket sizes at `MEMORY.TXT:33-43` and verified at `sglarea.S:24-28`) | **T2.3** | Concrete target algorithm: **one counting pass** producing 64 head/tail indices, then one linked drain. Cost `O(N + bins)` ≈ 1,864 steps vs today's ≈ 115,200 — a ~60× reduction on this stage. Intrusive chaining (SGL's `NEXT`) is preferable to a prefix-sum scatter here because it needs no second command-sized buffer; but the prefix-sum variant is already proven in-tree at `saturn_terrain_depth_bins.h:74-95` and is stable by construction, which is what the host equivalence test needs. Prefer the in-tree pattern for consistency. |
| L9 | 64 bins is coarser than SGL's 128+256 two-level arrangement | T2.3 | With `O(N + bins)` the empty-bin scan is 64 steps and irrelevant; **do not add a second level.** Keep `SM64_SATURN_TERRAIN_DEPTH_BIN_COUNT = 64` and `_SHIFT = 7` (`saturn_terrain_depth_bins.h:19-20`) so the chain-equivalence test compares like with like — the sort must produce *the same* far-to-near order, stable within bins, as today. Bin-count changes are a separate experiment with their own visual gate. |
| L10 | SlaveDriver's per-frame link writes number in the **tens** (one `JUMP_ASSIGN` at `WALLS.C:2269`, one `JUMP_RETURN` per sector at `WALLS.C:2263`, one `JUMP_CALL` per slave sector at `WALLS.C:1888-1889`); order comes from a sector-level topological sort (`WALLS.C:1986-2058`, `2180-2238`), never a per-command sort | T2.3, T2.4 | Confirms the counting sort is the *right* fix for the current architecture, and also that a **coarser ordering unit** (per-BSP-leaf rather than per-command) is the architectural fix beyond it. If T2.3's counting sort still shows up hot, batching links at leaf granularity is the precedented next step. |
| L11 | SlaveDriver aliases `slaveResult` onto `doorwayCache` and `slave_vCalc` onto its tail (`WALLS.C:1248-1253`), and re-aliases the same array for menus and intro (`MENU.C:127-128,971-972`, `INTRO.C:573-574`) | T2.2 | The lifetime-aliasing trick our port already applies to `sourceboot_vdp1_cmdts[0]` (T1 surprise #3) is standard reference practice, not a hack. If the reclamation package comes up short, **aliasing another cold staging buffer onto an idle hot arena is a precedented member of the ranked table** — subject, as T1 says, to a lifetime proof per alias. |
| L12 | SlaveDriver adapts the master/slave split ±1 per frame from measured master spin (`WALLS.C:2273-2285`); SGL uses a non-blocking write/read pointer ring so neither CPU waits (`SGLFAQ_F.TXT:1136-1143`), and moved the slave's poll to FRT input capture through cache-through memory to cut bus traffic (`SGL020A.TXT` §1.1.2) | T2.4+ | With scene construction at ~24.5 VBlanks, **measure the master's spin-wait on the slave before touching anything else in that path.** A static split that leaves either CPU idle is the classic failure; SlaveDriver's ±1 controller is ~10 lines and directly transplantable. Also verify our cross-CPU handshake polls through the uncached mirror, not a cached location. |
| L13 | Both engines clamp their worst tolerated frame: SlaveDriver to 2 VBlanks with 10-frame hysteresis (`SRUINS.C:2240-2268`), Z-Treme to 10 VBlanks (`ZT_SYSTEM.c:66-71`) | T2.4+ | At 53 VBlanks we are 5× outside the worst case either reference engine was ever designed to survive. Any per-frame delta scaling we carry is running far outside its tested range; when cadence improves, **re-check simulation-step clamps** rather than assuming they still hold. |
| L14 | SlaveDriver's FRT tree profiler: 60 fixed nodes, 8 children each, string-pointer keyed, cycles/32 resolution, zero allocation (`PROFILE.C:11-30,41-45,66-80`) | T2.1 | Shape for the `SATURN_DIAGNOSTIC_MODE=1` counters — fixed tables, no allocation, **FRT reads rather than VBlank counts** so the 24.5-VBlank scene-construction block can be decomposed rather than merely totalled. |

### Net effect on the T2.2 decision

Nothing in either reference engine argues against the (a) or (b) reclamation
packages, and L1/L2 argue strongly *for* them: the current LWRAM residency of
the hot set is the one arrangement neither engine ever adopts. The 2048→1664
capacity cut sits comfortably inside reference practice (L4) provided the
overflow path clamps (L5) and the drop order is far-first (L6) — two checks that
cost nothing and convert the shrink from a correctness gamble into a
quality-gated change. T2.1's measured peaks remain the binding gate; this sweep
narrows the *design space*, it does not substitute for measurement.

### Net effect on the T2.3 design

Adopt SGL's separation of concerns (L7): sort 8-byte references, not 32-byte
commands; carry the bin out of band; write links once in sorted order. Reuse the
stable radix scatter already in
`src/port/saturn/gfx/saturn_terrain_depth_bins.h:74-95` rather than inventing a
second one (L8). Keep 64 bins and the existing key derivation so the host
equivalence test can pin identical far-to-near, bin-stable output (L9).
Expected stage cost: `O(N + 64)` ≈ 1,864 steps against ≈ 115,200 today.

---

## 6. Honest gaps

- **SGL is a binary blob here** (`Compiler/SGL_302j/lib_coff/LIBSGL.A`). The
  Z-sort description is from Sega's own FAQ document and the `SPRITE_T` layout
  in `SL_DEF.H`, cross-checked against the disassembled default work area in
  `Documentation/libsgl/sglarea.S`. I did not read the sort routine's
  instructions, so the *implementation* is documented rather than verified.
- **The nominal-vs-reserved command-buffer arithmetic (§3.2)** is my own
  computation from the shipped address constants. It shows the declared
  `MaxPolygons × 32` extent overlaps `TransList` in both SGL's default and
  Z-Treme's config. I did not observe runtime behaviour to confirm the ring
  simply never reaches that depth; the arithmetic is solid, the inference about
  why it is harmless is an inference.
- **Neither engine has a cart-mapped tier.** Both are CD titles. There is no
  reference precedent in these two trees for the owner's 419 KB A-bus cart
  directive; that decision stands on our own measurements alone.
- **SlaveDriver's `sortLeafList` is an insertion sort, not a bucket/radix sort.**
  I looked for a counting or radix sort throughout `WALLS.C`, `SPRITE.C`,
  `OBJECT.C` and `SPR.C` and there is none — the engine simply never sorts a
  list long enough to need one. That absence is the finding, and it is why L10
  points at ordering granularity as the deeper lever.
- I read the primary (non-`FLASH/`, non-`OLDJAP/`, non-`SAVE/`) copies of
  SlaveDriver's sources. The duplicate trees were excluded from every grep;
  spot-checks showed `SAVE/WALLS.C` differs only by line offsets.
