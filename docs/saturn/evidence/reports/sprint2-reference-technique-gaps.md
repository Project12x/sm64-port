# Sprint 2 — SlaveDriver / Z-Treme dual-CPU and VDP technique gaps

- Date: 2026-08-15. Read-only comparative study; no source changes, no builds,
  no observations. Every number quoted here comes from an existing evidence file.
- Tree: `.worktrees/saturn-recovery`, branch `saturn/recovery`.
- Reference clones (GPL, read-only):
  - `work/upstream/slavedriver-engine` @ `a8986591557b6e680550d3c23970284d3b38ff8f`
  - `work/upstream/sonic-z-treme` @ `cff75451c1616aac1236fc2b44223902b55c706b`
    (Sonic Z-Treme sources + SGL 3.02j headers + Sega's shipped SGL documents)
- **This report deliberately does not re-derive T2.0's ground.** Memory tiering
  (L1/L2/L11), draw ordering (L7–L10), command-buffer strategy (L3–L6), frame
  pacing (L13), the ±1 load balancer (L12) and the FRT profiler (L14) are
  settled in `sprint2-t2_0-reference-sweep.md`. Everything below is new ground,
  driven by the owner's three questions.
- Every claim cites a file and line I opened. Where an engine has no analogue,
  that absence is recorded as the finding. Saturn hardware cost claims are
  either cited to a Sega document under
  `work/upstream/sonic-z-treme/Documentation/` or marked **inference**.
- Two Sega documents (`SGL020A.TXT`, `INIT.DOC`) are single unwrapped physical
  lines. They are cited as `:1` with the section named.

---

## 0. The measured situation this is compared against

| Fact | Value | Source |
| --- | --- | --- |
| Frame (diagnostic sustained run) | 41.00 VBlanks | `sprint2-t2_6-meshlet-arithmetic.md:595` |
| Construction | 9.450 (pre-notification 5.017) | `sprint2-t2_6-meshlet-arithmetic.md:596-597` |
| Simulation | 6.213 | `sprint2-t2_6-meshlet-arithmetic.md:599` |
| Slave work overlap | 2.950 | `sprint2-t2_6-meshlet-arithmetic.md:600` |
| Dropped VBlank credits | 6.917 | `sprint2-t2_6-meshlet-arithmetic.md:601` |
| Real cadence (matched basis) | 3.971 FPS vs A9A 5.294 | `sprint2-t2_7-a9a-regression-attribution.md:104-108` |
| VDP1 command peak | 653 of 1,664 | `sprint2-t2_1-peak-capture.md:18,183-185` |

Owner-supplied for this task: Mario is 638 of 882 visible items; ~50 source
triangles expand to ~200 VDP1 commands; ~16 VBlanks are unattributed. Two of
those figures resolve against the mesh header — `SM64_MARIO_PRIMITIVE_COUNT`
is **644** and `SM64_MARIO_TEXTURED_SOURCE_TRIANGLE_COUNT` is **50**
(`saturn_mario_actor_mesh.h:8,10`). Note that "882 visible items" and "653
published commands" are counts on **different bases**; §3.4D says how to
reconcile them, and nothing in this report depends on the relationship.

---

## 1. Question 1 — how the references divide CPU work

### 1.1 SlaveDriver: the slave's window is the entire simulation phase

This is the single most important structural difference, and it is not about
*how much* work the slave gets — it is about *when the master joins*.

Dispatch is one register write (`WALLS.C:2244-2246`):

```c
 slaveDrawStart=slaveSize;
 /* start slave */
 *(Uint16 volatile *)0x21000000=0xffff;
```

`0x21000000` raises the slave's FRT input capture. The slave spins on its own
FTCSR flag in `wallRenderSlaveMain` (`WALLS.C:1917-1934`):

```c
 *TIER=0x01;
 while (1)
    {/* wait for sync signal */
     while (!(*FTCSR & 0x80)) ;
     *FTCSR=0x0;
     slaveDraw();
     *(Uint16 volatile *)0x21800000=0xffff;
    }
```

The join is `drawWallsFinish` (`WALLS.C:2273-2285`). **The master's main loop
puts the whole game simulation between dispatch and join** (`SRUINS.C:2094-2160`):

| Master activity | Line | Slave |
| --- | --- | --- |
| `pushProfile("Walls"); drawWalls(...)` — portal flood, sort, **dispatch (`WALLS.C:2246`)**, then master draws the far half of `updateList` (`WALLS.C:2247-2255`) and all sector sprites (`WALLS.C:2259-2268`) | `SRUINS.C:2094-2096` | running near sectors |
| `pushProfile("Motion")`, `movePlayer`, the fixed-step `runObjects()` catch-up loop | `SRUINS.C:2102-2117` | **still running** |
| colour-offset stepping, weapon/invisibility timers, VDP2 register writes | `SRUINS.C:2118-2156` | **still running** |
| `drawWallsFinish()` — the spin-wait join | `SRUINS.C:2158-2160` | retiring |

The wait counter `i` in `drawWallsFinish` is the surplus, and the ±1 controller
(`WALLS.C:2280-2283`) drives it toward ~100 spins — i.e. toward *the slave
finishing just as simulation ends*.

**What we do.** Four independent short-lived fork-joins per frame, each blocking
the master immediately:

| Site | Job | Split |
| --- | --- | --- |
| `saturn_demo_render.c:1474` | Mario vertex transform | `s_actor_slave_begin` |
| `saturn_demo_render.c:1532` | Mario primitive classify | `s_actor_primitive_slave_begin` |
| `saturn_demo_render.c:3972` | **VDP1 command upload — a word copy** | `words / 2` |
| via `slavedriver_terrain_worker.c:14` | terrain range | `job->slave_begin` |

`sm64_saturn_dual_worker_run()` runs the master's half and then spins
(`slavedriver_dual_worker.c:138-148`):

```c
    fn(context, 0U, slave_begin);
    ...
    uint32_t spins = 0U;
    while (dual_control_read(&s_control.done) == 0U && spins++ < 10000000U
```

That is a barrier, four times a frame. Our header close-ported the *ownership*
rule — a disjoint half-open range (`slavedriver_dual_worker.h:1-10`) — but not
the enclosing-window property that makes SlaveDriver's version pay. Measured:
slave busy 3.15 VBlank-equivalents inside a ~3.09-VBlank window against ~38
VBlanks of master. The slave is saturated inside a window that is ~8% of the
frame.

### 1.2 What makes work slave-eligible in SlaveDriver

The property is **not** "it is geometry". It is a conjunction of four things,
all visible in source:

1. **The slave writes only to a private, disjoint, append-only region**, through
   the cache-through mirror (`WALLS.C:1272-1273`, and identically at `:1377-1378`):
   ```c
   struct slaveDrawResult *cacheThruResult=
      (struct slaveDrawResult *)(((int)slaveResult)+0x20000000);
   ```
   `slaveResult[]` is a flat array of 28-byte records (`WALLS.C:1243-1249`).
2. **The record stream is order-carrying, so the master replays it verbatim.**
   Boundaries are in-band sentinels — `tile == -1` end-of-sector, `-2` water
   surface (`WALLS.C:1255-1259`, `1815-1817`), consumed by the master's replay
   switch at `WALLS.C:1882-1904`. The slave never needs to know where in the
   command list its output will land.
3. **The slave never touches VDP1.** `slaveDraw()` (`WALLS.C:1806-1819`) calls
   `drawSector(..., slave=1)`, routing to `slave_drawRectWall` / `slave_drawWall`
   (`WALLS.C:1521-1529`), which compute screen-space quads, Gouraud tables and
   tile ids and stop. Every VDP1 command for the slave's sectors is built by the
   **master** in `drawSlaveWalls` (`WALLS.C:1822-1912`), emitting at
   `WALLS.C:1909`.
4. **The unit is coarse and self-limiting.** The unit is a *sector* (portal cell):
   `slaveSize` counts sectors (`WALLS.C:2242-2244`), capped at 50
   (`WALLS.C:2283`). Overflow is rejected *before* construction with a margin
   (`WALLS.C:1278-1279`):
   `if (height*width+nmSlavePolys+50>MAXNMSLAVEPOLYS) return;`

**The equivalent unit for us** is therefore not "half the vertices" and
certainly not "half the command words". It is the largest coarse spatial unit
whose output is a contiguous, order-carrying record run. In this tree that is
the **BSP leaf / scene-admission package** on the terrain side and the
**meshlet** on the actor side — both already exist (`saturn_scene_admission.c`,
`saturn_actor_meshlets.c`, `saturn_terrain_depth_bins.h`). Splitting a word copy
(`saturn_demo_render.c:3972`) satisfies none of the four properties except
disjointness, and is bus-bound rather than compute-bound, so it cannot approach
2× on a shared-bus machine (**inference** — both SH-2s and the SCU contend for
the same HWRAM bus; not measured here).

### 1.3 SGL: the opposite default, and duplicated per-CPU state

Sega's description (`SGLFAQ_F.TXT:1130-1143`):

> In the SGL, the SMPC reset is released by the InitSlaveSh function within the
> slInitSystem function, and the system enters the FRT input capture waiting
> state. … If the FRT input capture signal is input (by writing a "-1" to
> address 0x21000000), the slave CPU executes the SlaveControl function.
> … designed to pass requests from the master CPU through a shared buffer so
> that neither the master CPU nor the slave CPU enter the waiting state …
> Whether or not a request was output can be recognized when the master CPU's
> write pointer differs from the slave CPU's read pointer.

**SlaveDriver and SGL use the identical hardware doorbell** — a write to
`0x21000000` (`WALLS.C:2246` vs the FAQ text above). Ours uses Yaul's
`cpu_dual_slave_notify()` in `CPU_DUAL_ENTRY_POLLING` mode
(`slavedriver_dual_worker.c:88-89,126`), the same class of primitive. **The
difference is not the doorbell, it is the wait.**

**Slave-eligible unit in SGL: a whole polygon model.** From Sega's function
reference (`SPRITE.TXT:104-106`):

> slPutPolygon() above divides processing between the master and slave CPU
> depending on the slave CPU state to perform parallel processing. In
> comparison, this function [`slPutPolygonS`] executes all tasks with the slave CPU.

and the rationale for the slave-only variant (`SGL020A.TXT:1`, §1.1 item 10):

> Although slPutPolygon passes on a portion of display processing calculations
> to the master depending on the slave's processing state, slPutPolygonS forces
> all display processing on the slave. This function should be used to reduce
> the slave's idle time between the execution of slSynch and the first
> processing request made to the slave.

**The default is slave-first, master-assist.** Ours is master-first,
slave-assist. The failure mode Sega names — *slave idle between frame start and
the first request* — is exactly the shape of our 3.09-VBlank window.

**What makes a non-blocking ring safe is duplicated per-CPU state, not locking.**
SGL's system-variable page carries a master and a slave copy of every render
cursor (`MEMORY.TXT:28-35,52-63,90-96`):

```
028: (SlPbufPtr)      (Uint32 *) ; vertex coordinate calculation buffer pointer (Slave)
030: (MsSdataPtr)     (Uint16 *) ; sprite data set pointer (Master)
034: (SlSdataPtr)     (Uint16 *) ; sprite data set pointer (Slave)
044: (ComRdPtr)       (Uint32 *) ; command read pointer
048: (ComWrPtr)       (Uint32 *) ; command set pointer
04C: (MsLightVector)  (VECTOR) ; light source vector (Master)
058: (SlLightVector)  (VECTOR) ; light source vector (Slave)
068: (MsScreenDist)   (FIXED)  ; screen position (Master)
06C: (SlScreenDist)   (FIXED)  ; screen position (Slave)
090: (MsWinXAdder)    (Uint16) ; window check adder data (Master)
094: (SlWinXAdder)    (Uint16) ; window check adder data (Slave)
```

and two sprite record buffers (`SL_DEF.H:847-848`):

```c
    extern const SPRITE_T*   SpriteBuf ;	/* Main sprite control data buffer */
    extern const SPRITE_T*   SpriteBuf2 ;	/* Slave sprite control data buffer */
```

with `CommandBuf` (`SL_DEF.H:852`) as the request ring. SGL 2.0A additionally
moved the slave's poll off the master's pointer onto the FRT input-capture
signal *through a cache-through area* to cut bus traffic (`SGL020A.TXT:1`,
§1.1 item 2).

### 1.4 Does either engine let the slave touch VDP1? No — and we do

- SlaveDriver's slave stops at geometry records (§1.2 item 3); the master emits
  every command (`WALLS.C:1909`).
- SGL's slave stops at `SpriteBuf2` (`SL_DEF.H:848`); the VRAM write is an
  SCU-DMA indirect transfer driven from `SortList` (`SGLFAQ_F.TXT:1108-1112`).
- **Ours has both CPUs writing `VDP1_VRAM(0)` directly** —
  `demo_upload_vdp1_dual` (`saturn_demo_render.c:3961-3976`) splits a word copy
  into `VDP1_VRAM(0)` between master and slave.

That is outside both references' practice.

### 1.5 Z-Treme contributes nothing to Question 1 — an honest absence

**Z-Treme never uses the slave explicitly.** Both `slSlaveFunc` call sites in
the tree are commented out:

- `ZTE/ZT_LOADING.c:110` — `//slSlaveFunc(texturesToVRAM , (void*)currentAddress) ;`
  with the serial call on the next line.
- `SRC/game.c:760` — `update_physics(&PLAYER_2); //slSlaveFunc(update_physics, &PLAYER_2);`

All of Z-Treme's dual-CPU benefit is whatever `slPutPolygon` hands the slave
internally, and **SGL is a binary blob here** (`Compiler/SGL_302j/LIB_COFF/LIBSGL.A`).
Its author knew this was the weak point (`ZT_RENDERING.c:716-719`):

> MAIN DRAW FUNCTION / Could/should be cleaned up. It also should call draw
> functions ASAP to make the slave SH2 busy, so here it's not super efficient

Z-Treme is a **single-CPU application on top of a dual-CPU library**. It is a
usable source for VDP usage (§2) and for SGL's documented contract, not for
slave scheduling technique.

### 1.6 Fraction of frame work carried by the slave

| Engine | Derivable? | Value |
| --- | --- | --- |
| SlaveDriver | Partly | `slaveSize` sectors of `updateListSize`, adapted per frame toward ~100 master spin iterations (`WALLS.C:2276-2283`), cap 50. No absolute time share is recorded; the controller targets "master waits a little", and the window spans all of simulation. |
| SGL / Z-Treme | **No** | Internal to `LIBSGL.A`. `TotalPolygons` / `TotalVertices` exist (`MEMORY.TXT:74-76`) but are not split by CPU. |
| Ours | Yes | 3.15 VBlank-equivalents busy in a ~3.09-VBlank window against ~38 VBlanks of master — ~7-8% of frame work. |

---

## 2. Question 2 — VDP1 and VDP2 usage

### 2.1 Which command types each engine emits, and for what

**SlaveDriver uses all ten VDP1 command types**, named verbatim in its
abstraction layer (`SPR.H:48-57`):

```
#define  FUNC_NORMALSP       0x0000     /* draw normal sprite function      */
#define  FUNC_SCALESP        0x0001     /* draw scaled sprite function      */
#define  FUNC_DISTORSP       0x0002     /* draw distorted sprite function   */
#define  FUNC_POLYGON        0x0004     /* draw polygon function            */
#define  FUNC_POLYLINE       0x0005     /* draw polyline function           */
#define  FUNC_LINE           0x0006     /* draw line function               */
```

**SGL names them slightly differently, and has no normal-sprite API at all**
(`SL_DEF.H:167-175`): `FUNC_Sprite`=1 is a *scaled* sprite, `FUNC_Texture`=2 is
the distorted sprite. Z-Treme writes the literal `0` itself when it wants a
normal sprite (`ZT_SPRITES.H:85`).

| Content | SlaveDriver | Z-Treme / SGL | Ours |
| --- | --- | --- | --- |
| Terrain, textured | **distorted 0x0002** — `SPR.C:232-245`, emitted `WALLS.C:1082-1086,1145`, slave replay `WALLS.C:1909` | **distorted 0x0002** via `slPutPolygon` with `sprNoflip` ATTRs — `ZT_RENDERING.c:469,479`; `SPRITE.TXT:86-92` ("polygon **or distorted sprite**"); texture⇔distorted equivalence at `SPRITE.TXT:373-374` | **distorted 0x0002** — `saturn_ir_texture.c:113`, bound at `saturn_demo_render.c:3479-3484` |
| Terrain, untextured | polygon 0x0004 (water/force fields, + `DRAW_MESH`) — `WALLS.C:1227-1229`, `801-802` | polygon 0x0004 via `sprPolygon` ATTRs — `SL_DEF.H:228`, `ZT_LOAD_MODEL.c:113` | **polygon 0x0004 + Gouraud** — `saturn_demo_render.c:3475,3566-3579`; `saturn_fast3d_vdp1_emit.c:61` |
| **Player character** | **scaled sprite 0x0001** from a 2D chunk sheet, flat 4-corner Gouraud tint — `WALLS.C:2654-2656` (16bpp), `2663-2666` (8bpp) | **3D polygon mesh** — `display_model()` `ZT_RENDERING.c:557-569` → `slPutPolygonX` (real-time Gouraud, `SL_DEF.H:1923`) with `enableRTG=1` default (`ZT_RENDERING.c:17`); models `SONIC.ZTP` loaded `SRC/game.c:229-233` | **3D polygon mesh** — 644 primitives (`saturn_mario_actor_mesh.h:8`), one Gouraud polygon each (`saturn_demo_render.c:3871`, `4183`) |
| Enemies | scaled sprite 0x0001 (same path) | 3D mesh (`ZT_RENDERING.c:385,387`); explosion is untextured polygon + `MESHon` + dual-plane (`SRC/explosion.h:38-45`) | n/a in this slice |
| Small entities / pickups | scaled sprite 0x0001 | **scaled sprite 0x0001** via `slPutSprite` — rings `ZT_RENDERING.c:336,342,347`; pickups `:394`; particles `:299` | n/a |
| Character drop shadow | scaled 0x0001 + `COMPO_SHADOW` — `WALLS.C:2587-2588` | scaled 0x0001 + `CL_Shadow` — `ZT_RENDERING.c:622` | n/a |
| HUD | normal sprite 0x0000 (320×42 bar) — `SRUINS.C:1322`; polygons/lines for bars — `SRUINS.C:1235,1251,1258,1286,1314` | **normal sprite 0x0000** — `ZT_RENDERING.c:735-741` via `ztSetNormalSprite` (`ZT_SPRITES.H:82-94`); `setHUD()` VDP2 path is commented out (`SRC/game.c:134-167,245`) | **VDP2 NBG0 cell plane** — `saturn_hud_atlas.c:225-241` |
| Text | normal sprite 0x0000, `COLOR_1` 4bpp — `PRINT.C:128,133,148,163,230,235` | VDP2 NBG3 tilemap — `ZT_VDP2.c:10-26`, `slPrint` at `ZT_LOADING.c:392` | VDP2 NBG3 dbgio — `sourceboot/main.c:1841-1843` |
| Sky | **VDP2 RBG0 rotation bitmap** — `PLAX.C:94-111` | **VDP2 NBG1/NBG0 scrolling bitmap** — `ZT_VDP2.c:138-146`, scrolled `ZT_RENDERING.c:731,746` | VDP2 NBG1 scrolling bitmap — `sourceboot/main.c:1029-1035`, scroll `saturn_vdp2_frame.c:172-180` |
| Framebuffer erase | polygon 0x0004 in command slot 0 — `SPR.C:41-63` | SGL's own, one full-screen sprite in dynamic-frame mode (`SGL020A.TXT:1` §2.2.2) | Yaul's; not audited |

**The honest headline is narrower than "we use the wrong primitives".** Our
primitive *classes* match Z-Treme almost exactly: textured surfaces are
distorted sprites, untextured surfaces are Gouraud polygons, and the player
character is a 3D mesh of polygons. Z-Treme validates that choice.

What we do that **neither** reference does is:

1. **Emit two VDP1 commands for one textured actor primitive.** For a Mario
   primitive with a texture tile, `saturn_demo_render.c:3871-3900` writes a
   Gouraud **polygon** into `context->cmdts[s_actor_slots[ordinal]]`, and then
   `:3903-3931` writes a **second** command — a distorted sprite carrying the
   texture — into `context->cmdts[s_actor_texture_slots[ordinal]]`, with the
   *same* sort key (`detail->cmd_link = (uint16_t)(ref->sort_key >> 16)` at
   `:3930`). Both are drawn. SlaveDriver draws a wall tile as one distorted
   sprite; Z-Treme draws a model face as one command. Only ~10 Mario primitives
   carry a texture start (`saturn_mario_actor_mesh.h:49320-49327` — the
   non-`0xFFFF` entries), so the absolute cost is small, but the *pattern* —
   deliberate double-draw of the same surface — has no reference precedent.
2. **Reserve nothing for cheap primitives.** A whole-tree grep of the Yaul
   setters across `src/port/saturn/` finds only `vdp1_cmdt_polygon_set` and
   `vdp1_cmdt_distorted_sprite_set`. There is no normal sprite, no scaled
   sprite, no line, no polyline anywhere in the port. Both references reserve
   the two expensive quad primitives for surfaces that genuinely warp, and use
   normal/scaled sprites for everything with a fixed rectangular footprint.
   Today that costs us nothing (our HUD is on VDP2, which is *better*), but it
   means any future 2D element has only the expensive path available.
3. **Carry 644 polygons for one actor** (`saturn_mario_actor_mesh.h:8`) against
   Z-Treme's whole-scene budget of `MAX_POLYGONS 1900` (`Common.h:21-22`) shared
   by terrain, two players, enemies, rings and particles.

**On relative primitive cost:** on VDP1 hardware a polygon and a distorted
sprite are drawn by the same quad rasterizer; the polygon simply has no texture
fetch. **No document in either tree states a cycle cost for any primitive**
(§6 item 1). Treat "polygon is cheaper than distorted sprite, both are dearer
than a normal sprite" as **inference**, not a citable number.

### 2.2 Gouraud

**SlaveDriver:** on essentially every world primitive and not switchable. The
canonical terrain draw mode is hard-coded (`SPR.C:237`, identically at `:252`):

```c
 setDrawPara(cmd,UCLPIN_ENABLE|COLOR_5|HSS_ENABLE|ECD_DISABLE|DRAW_GOURAU,0);
```

Budget: the in-game level renderer initialises with **1,448 commands / 1,224
Gouraud tables** (`SRUINS.C:1879`); the 1,540/1,524 pair quoted in T2.0 is the
front-end/boot instance (`SRUINS.C:2392`, `INITMAIN.C:567`). Tables are 8 bytes,
double-buffered in VDP1 VRAM (`SPR.C:74-76`), staged through a 256-entry work-RAM
buffer (`SPR.C:21,35`), and **truncate silently** on overflow (`SPR.C:151-152`).
Four uses: terrain vertex lighting (`WALLS.C:1047-1068`), water (`WALLS.C:1176`),
**characters as a flat four-corner tint** (`WALLS.C:2638-2656` — all four entries
identical), and pulsing text (`PRINT.C:174-219`). The per-class disable hook
`setDrawModeBit` (`PIC.C:162-167`) has **zero callers**.

**Z-Treme:** likewise on everything, and the loader forces it. `USE_PALETTE` is
never defined, so this branch is live (`ZT_LOADING.c:461-475`):

```c
        for (j=0; j<MDATA.TOTAL_MESH; j++)
            for (p=0; p<LevelMesh[j]->nbPolygon; p++)
            {   LevelMesh[j]->attbl[p].atrb |= CL_Gouraud;
                LevelMesh[j]->attbl[p].gstb |= tex_def[totalTextures].CGadr+(cnt);
                cnt++; }
```

— every level polygon and every LOD polygon gets `CL_Gouraud` and a unique
`CMDGRDA`. Runtime table budget `GOUR_REAL_MAX 750` (`ZT_LOAD_MODEL.H:9`),
uploaded via `slGouraudTblCopy()` every V-blank (`ZT_RENDERING.c:59-60`).
**Sprites deliberately do not use Gouraud**: every entry in
`SRC/sprites_data.h:9-31` is `No_Gouraud`, `ztPutSprite` hardcodes it
(`ZT_SPRITES.H:99`), and `ztSetScaledSprite` forces `GRDA=0` (`ZT_SPRITES.H:54`).
Sega's rationale for Gouraud-on-textures is in `SGLFAQ_F.TXT:886-888`:
*"It is not possible to use the method above when using textures. Use VDP1's
Gouraud shading function instead to make color offsets to the entire texture."*

**Ours:** structurally the same and implemented well — neutral 0xC210 base so
table entries are the final RGB (`saturn_gouraud.h:6-15`), bounded bank with a
counted flat fallback (`saturn_fast3d_vdp1_emit.c:80-93`,
`saturn_demo_render.c:3510-3517`), capacity clamped to the VRAM partition
(`sourceboot/main.c:1995-2008`). **No gap.** One asymmetry: both references skip
the table entirely for flat-tinted content (Z-Treme's sprites) or use identical
four-corner entries (SlaveDriver's characters); our flat path already exists via
`VDP1_CMDT_CC_REPLACE` (`saturn_demo_render.c:3894-3898`) but is only reached on
bank exhaustion, not chosen for genuinely flat primitives.

### 2.3 Clipping — the clearest actionable gap

**Both references set the per-command user-clip *enable* bit on nearly every
command. We set it on none.**

- SlaveDriver: `UCLPIN_ENABLE 0x0400` (`SPR.H:88`) is in the terrain mode word
  (`SPR.C:237,252`), in all five pic-class draw words (`PIC.C:85,87,90,92,96`),
  and at `WALLS.C:1078,1142,1913,2500,2517,2655,2664`, `WEAPON.C:955`,
  `SEQUENCE.C:304`, `PRINT.C:148`, `MAP.C:204,207`.
- Z-Treme: `Window_In` (= `(2<<9)`, `SL_DEF.H:189-205`) is on effectively
  everything — `SRC/sprites_data.h:9-31`, `ZT_SPRITES.H:87,99`,
  `ZT_LOAD_MODEL.c:78,96,101,113`, `SRC/explosion.h:38-45`.
- Ours: a grep for `user_clipping_mode`, `hss_enable`, `pre_clipping_disable`,
  `mesh_enable`, `msb_enable` and `trans_pixel_disable` across `src/` returns
  **no matches at all**. Every `vdp1_cmdt_draw_mode_set` in the port sets only
  `color_mode`, `cc_mode` and sometimes `end_code_disable`. Bitfield at
  `third_party/libyaul/libyaul/scu/bus/b/vdp/vdp1/cmdt.h:108-123`.

**Where the two references then diverge:**

**SlaveDriver sets a per-sector user-clip rectangle** — the screen-space
bounding box of the portal chain into the sector about to be drawn, so VDP1
*output* is clipped to the portal rather than geometry being clipped to it.
Master path (`WALLS.C:2247-2253`):

```c
 for (i=updateListSize-1;i>slaveDrawStart;i--)
    {parms[0].x=updateList[i]->xmin+160;parms[0].y=updateList[i]->ymin+120;
     parms[1].x=updateList[i]->xmax+160;parms[1].y=updateList[i]->ymax+120;
#if RECTCLIP
     EZ_userClip(parms);
#endif
     drawSector(updateList[i]-sectorDraw,view,0);
```

Slave replay path re-issues it at every end-of-sector sentinel
(`WALLS.C:1894-1902`; initial clip `WALLS.C:1834-1838`). `RECTCLIP` is `1`
(`WALLS.C:27`). Boxes come from `doorwayCache[]` (`WALLS.C:1655-1673`),
intersected down the portal recursion (`WALLS.C:1750-1787`), against the
HUD-aware view rect `XMIN -160 / YMIN -110 / XMAX 160 / YMAX 90`
(`WALLS.C:115-118`). Two further uses a port would not think of: the sprite pass
resets to the view rect (`WALLS.C:2459-2463`), and **foot clipping** pulls the
clip bottom up to a sprite's feet so it cannot poke through a floor
(`WALLS.C:2592-2599`, restored `WALLS.C:2672-2680`). `EZ_userClip` takes
framebuffer coordinates (`SPR.C:322-331`), hence the `+160`/`+120`.

**Z-Treme never emits a user-clip or system-clip command at all.** Both
constructors exist (`ZT_SPRITES.H:9-17`, `:19-29`) with **zero call sites**. It
relies on two SGL *windows* — `winFar` / `winNear` (`ZT_RENDERING.c:95-101`,
`slCurWindow` switching at `SRC/game.c:757-768`) — plus the per-command
`Window_In` bit, and on `slNearClipFlag(1)` (`ZT_RENDERING.c:119,127`). Sega
added `slFrameClipSize` for changing the clip region in 2.0A
(`SGL020A.TXT:1` §1.1 item 3; `SPRITE.TXT:242-249`); Z-Treme never calls it.

**Ours:** exactly one system-clip command at list index 0
(`saturn_vdp1_backend.h:69-70`, and again in the caller-storage variant at
`:106-107`), never narrowed. Zero uses of `vdp1_cmdt_user_clip_coord_set`.
Consequence: even if we emitted a user-clip command it would be inert, because
no command sets `user_clipping_mode`.

**`hss_enable` (CMDPMOD bit 12, high-speed shrink) is off on every command we
emit, and on in both references.** SlaveDriver: `SPR.C:237,252`;
`PIC.C:85,87,90,92,96`; `WALLS.C:1078,1142,1913,2655,2664`; `WEAPON.C:955`;
`SEQUENCE.C:304`; `BIGMAP.C:253,270`. Z-Treme: all 22 entries of
`SRC/sprites_data.h:9-31`, `ZT_SPRITES.H:87,99`, `ZT_LOAD_MODEL.c:96,101`;
documented at `SPRITE.TXT:390-391` (`HSSon: High-speed shrink on`). Neither
engine sets pre-clipping-disable, i.e. both leave VDP1's hardware pre-clipping
**on** — which is also our state by default.

### 2.4 The one citable statement about distorted-sprite cost

There is no cycle table in either tree (§6 item 1). The closest Sega gets is the
SGL 2.0A release note (`SGL020A.TXT:1`, §1.1 item 4, "Distorted Sprite Draw
Optimization"):

> When distorted sprites (also includes textures) are displayed, a check is
> performed to see whether vertices exist within a window. The character is
> flipped and vertices are swapped as necessary to reorient vertex 0 within the
> window. The drawing efficiency of distorted sprites that go outside the window
> has been improved by the implementation of this software preclipping.

Exactly two things follow:

- Sega considered **distorted sprites crossing the clip window** a
  drawing-efficiency problem worth a library-wide fix, and the fix was *software*
  preclipping plus vertex-0 reorientation, done on the CPU before the command is
  built. That is evidence of **orientation- and window-dependent** cost, not a
  primitive-cost ranking.
- The fix lives in the library, so Z-Treme gets it for free and we do not.

Our near-plane clip (`slavedriver_terrain_clip.c`, invoked at
`saturn_demo_render.c:1955`) is a *view-space depth* clip, not a
screen-space window preclip, and there is no vertex-0 reorientation anywhere in
`src/port/saturn/`. Whether that matters here is a measurement, not a claim.

For completeness: `WORKAREA.TXT:68-76` does give a per-call cost table
(`slPutPolygon 72 bytes`, `slPutSprite 36 bytes`, …) but that is **host work-RAM
per API call**, not VDP1 drawing cost, and must not be quoted as one.

### 2.5 VDP2 offload

**SlaveDriver's in-game VDP2 configuration** (`SRUINS.C:1051-1077`, duplicated at
`INITMAIN.C:261-286`):

```c
 vcfg.vramA0=SCL_RBG0_K;
 vcfg.vramA1=SCL_RBG0_CHAR;
 SCL_SetColRamMode(SCL_CRM15_2048);
 SCL_SetPriority(SCL_SP0|...|SCL_SP7, 7);
 SCL_SetPriority(SCL_NBG0,6);
 SCL_SetPriority(SCL_RBG1,7);
 SCL_SetPriority(SCL_SP0,4);
 SCL_SetSpriteMode(SCL_TYPE1,SCL_MIX,SCL_SP_WINDOW);
```

| VDP2 use | SlaveDriver | Z-Treme | Ours |
| --- | --- | --- | --- |
| Sky plane | **RBG0 rotation bitmap**, 512×256 256-colour, loaded `PLAX.C:94`, configured `PLAX.C:96-111`, and *rotated + scrolled* from yaw/pitch every frame by poking the rotation register buffer (`PLAX.C:25-53`, called `SRUINS.C:2270`) | **NBG1 scrolling bitmap** — `slBitMapNbg1(...)` `ZT_VDP2.c:138-139`; scrolled from yaw and camera pitch at `ZT_RENDERING.c:731` (`slScrPosNbg1(...)`); NBG0 duplicates it for split-screen (`SRC/game.c:112-127`) | **NBG1 scrolling bitmap**, 512×256 RGB1555 — `sourceboot/main.c:1029-1035`; scrolled from yaw and pitch — `saturn_vdp2_frame.c:172-180`, `sourceboot/main.c:1090-1095` |
| Sky occlusion | **Windowed** to the union of sky-visible screen boxes accumulated during wall rendering (`SRUINS.C:2277-2279`; boxes `WALLS.C:1496-1514`, merged `WALLS.C:2291-2306`) | Windows used only to mask split-screen viewports (`ZT_RENDERING.c:102-104,116-130`), not for occlusion | None |
| **RBG0 rotation** | Used (above) | **Unused** — `ztSetRBG0()` is an empty stub (`ZT_VDP2.c:28-31`); no `slRparaMode`/`slMakeKtable`/`slScrScale*` anywhere | **Unused** |
| Overflow sprite layer | **NBG0 512×512 8bpp bitmap** — art too large for a VDP1 character is "drawn" by moving/windowing the plane (`SRUINS.C:1080-1096`, `PIC.C:455-498`, dispatch `SEQUENCE.C:267-294`) | The `USE_TRANSP_BUFFER` experiment maps NBG0 over the VDP1 framebuffer (`ZT_LOADING.c:528-543`, `ZT_GAME.c:17-46`) but is **disabled** (`ZTE_DEF.H:108`) | None |
| HUD | VDP1 normal sprite (`SRUINS.C:1322`); NBG0 carries VDP2 sprites instead | VDP1 normal sprites (`ZT_RENDERING.c:735-741`); `setHUD()` VDP2 path commented out (`SRC/game.c:134-167,245`) | **VDP2 NBG0 cell plane** (`saturn_hud_atlas.c:225-241`) — better than either reference |
| Colour offset | All global tinting in one register write: damage flash, fades, underwater — `SCL_SetColOffset(SCL_OFFSET_A,SCL_SP0\|SCL_NBG0\|SCL_RBG0,...)` `SRUINS.C:128-143`, called `SRUINS.C:2118`; offset B for power-up/invisibility `SRUINS.C:2127-2154` | Global fade only — `main.c:102-104`, ramps `ZT_TOOLS.c:85-140` | **None** — no VDP2 colour-offset call in `src/port/saturn/` |
| Colour calculation | `SCL_SetColMixRate(SCL_NBG0,...)` drives the invisibility dissolve (`SRUINS.C:2043-2044,2141,2154`) | `ztSetColorCalc()` builds an 8-step sprite-priority-register fade ladder (`ZT_VDP2.c:61-85`) — but the distance-fade consumer is `#ifdef USE_PALETTE` and **inert** (`ZT_RENDERING.c:462-468`) | None |
| Line scroll | Library supports it (`SCL_FUNC.C:36-39`) but no engine call installs a table; the per-column sky coefficient experiment is `#if 0` (`PLAX.C:117-132`) | **Unused** — `ztSetLineColor()` fully commented out (`ZT_VDP2.c:34-59`) | Unused |

Our priority stack is correct and matches SlaveDriver's shape (HUD above sprites
above sky): sprites capped at 6, NBG0 HUD 7, NBG3 dbgio 6, NBG1 sky 0
(`sourceboot/main.c:1119-1124`).

**So the VDP2 gap is narrower than it first looks.** Our sky already scrolls from
yaw and pitch, which is exactly Z-Treme's arrangement. What is missing, and only
one of the two references has each:

1. **Sky occlusion windowing** (SlaveDriver only) — stop compositing the sky
   where terrain covers it.
2. **Roll/rotation of the sky via RBG0** (SlaveDriver only) — Z-Treme does not
   do this either.
3. **VDP2 colour offset for full-screen effects** (both references) — we have
   none, and every such effect we add later would otherwise cost VDP1 commands.

### 2.6 Fill-rate discipline

| Technique | SlaveDriver | Z-Treme | Ours |
| --- | --- | --- | --- |
| **Distance LOD that reduces command count** | `MIPMAP 1` (`WALLS.C:38`): beyond `MIPDIST F(256)` (`WALLS.C:31`) a wall's tile grid halves in both axes — **4× fewer VDP1 commands for that wall** — with a pre-generated mip in the same 64×64 slot (`WALLS.C:1001-1013` master, `1281-1293` slave; mip build `PIC.C:359-388`, `createMippedPics` `PIC.C:422-434` called `SRUINS.C:2063`) | **Second polygon table** swapped in at 450 units (`ZT_RENDERING.c:457-460,469`), sharing the vertex table (`ZT_LOADING.c:154-170`); entities skipped past LOD band 4 (`ZT_RENDERING.c:461,470`); ring sprites swap to `L1…L8` LOD textures at half scale (`:339-347`) | LOD can **suppress** a primitive or **downgrade its material** (`saturn_demo_render.c:1925-1934`, `saturn_lod_lifetime.c`) but **no path reduces a surviving surface's command count** |
| Buffer-exhaustion ordering | Rejects before construction with a `+50` margin (`WALLS.C:1278-1279`) | **Near-to-far octree traversal specifically so exhaustion drops the farthest geometry** — `ZT_RENDERING.c:494-503`, with the rationale in the comment | Far-to-near painter (T2.3); see T2.0 L6 |
| Texture size ceiling | Fixed classes 64×64 and 32×32 only (`PIC.C:84-99`), pre-allocated once (`PIC.C:217-221`), runtime is pure LRU content swap; hard caps `MAXNMCHARS 512` (`SPR.C:19`) and `assert(((int)charStart)<1024*512)` (`SPR.C:113`) | No explicit cap; linear packing (`ZT_SPRITES.c:20-37`), loader assumes ≤512×512 (`ZT_SPRITES.c:47`) | `saturn_ir_texture.c:64-71,98-101` enforces width 8–504, multiple of 8, CMDSIZE ≤ 0x3FFF — hardware limits, no policy ceiling |
| Oversized art | Escalated to VDP2 (`TILEVDP` class `PIC.C:89`, asserted out of the VDP1 path at `PIC.C:259,445`) | No analogue | No analogue |
| High-speed shrink | On, everywhere (§2.3) | On, everywhere (§2.3) | **Off, everywhere** |
| Half-transparency | Deliberate and sparing — `WALLS.C:2500`, `MAP.C:204`, `BIGMAP.C:325,352-353`, `MENU.C:256,1248`, `MOV.C:390` | **Not used in normal play** — only under `WIREFRAME_TEST==4` (`ZT_LOADING.c:236`) | Available (`saturn_actor_material.c:86,93`) |
| Mesh | 6 sites, all water/UI (`WALLS.C:1227,801`, `SRUINS.C:1537,1542`) | 3 sites (`SRC/sprites_data.h:30-31`, `SRC/explosion.h:38-45`) | Unused |
| Backface / node rejection | Wall normal test, `doorwayCache[w].xmin=-32000` marks rejected walls (`WALLS.C:1573,1670-1673`) | Frustum test per octree node with parent-inside shortcut (`ZT_RENDERING.c:425-428,486-492`); whole-node backface rejection for single-plane nodes (`:437-451`) | Frustum port exists (`ztreme_frustum.c`); near-plane clip at `saturn_demo_render.c:1955` |
| Scene polygon budget | 1,448 commands in-game (`SRUINS.C:1879`) | `MAX_POLYGONS 1900`, `MAX_VERTICES 2800` (`Common.h:21-22`) | 1,664 commands; peak 653 (`sprint2-t2_1-peak-capture.md:18`) |

---

## 3. Question 3 — the unattributed ~16 VBlanks

### 3.1 What a Saturn frame's non-CPU time is made of, per the references

1. **The VDP1 plot itself.** SlaveDriver ends every frame with
   `EZ_closeCommand(); SPR_WaitDrawEnd();` (`SRUINS.C:2235-2236`) and *records
   that interval as a first-class number* — `lastDraw=htimer-lastCalc;`
   (`SRUINS.C:2237`), displayed next to calc time on its own HUD
   (`SRUINS.C:2202-2204`, `"time:%d %d:%d"`). **The engine's debug overlay
   separates calc from draw.**
2. **The wait for the display-frame boundary.** `while (vtimer<smoothVTime) ;`
   then `SCL_DisplayFrame();` (`SRUINS.C:2251-2255`).
3. **Framebuffer erase — and in draw-end-wait mode it is an extra VDP1 draw.**
   `SGL020A.TXT:1`, §2.2 item 2:
   > A dynamic frame change mode was added to advance the main program sequence
   > upon completion of VDP1 draw processing. … However, note that an extra draw
   > step takes place in this mode since a sprite the size of the entire screen
   > is drawn to clear the frame buffer.
   SlaveDriver does the same explicitly: command slot 0 becomes a screen-wide
   fill polygon when the hardware erase-write band does not cover the screen
   (`SPR.C:41-63`, `ERASEWRITESTARTLINE 110` at `SPR.C:39`).
4. **Framebuffer readback** — expensive, and Sega says so (`SPRITE.TXT:288-291`):
   *"the latency to read data from the frame buffer is long (it takes
   approximately 5 msec for 32 x 24 (=768) pixels)"*. We do no readback; recorded
   so it can be ruled out.
5. **DMA of the command list into VDP1 VRAM.** SGL uses SCU-DMA indirect mode
   driven from `SortList` (`SGLFAQ_F.TXT:1108-1112`).

### 3.2 How the references bound and observe it

- **SlaveDriver bounds it by pacing**, clamping the frame to 1–2 VBlanks with
  10-frame hysteresis (`SRUINS.C:2239-2268`) — T2.0 L13 — and *measures* the
  draw half separately (`SRUINS.C:2237`).
- **Z-Treme selects `slDynamicFrame(ON)`** — advance on VDP1 draw-end rather
  than a fixed period (`main.c:91-95`, `SynchConst = DEFAULT_FRAMERATE = 2`
  at `ZTE_DEF.H:62`), exactly the recipe at `SGLFAQ_F.TXT:87-94`, with runtime
  toggles at `ZT_GAME.c:90-92` and a menu setting mapping `SynchConst` 1/2/3/4
  to 60/30/20/15 fps (`ZT_MENU.c:153-156`). Sega's definition
  (`INIT.DOC:1`, `slInitSystem` note 2):
  > When a negative value is specified for the frame change count, VDP1 is
  > checked to see if it has completed its writes to the frame buffer after the
  > change count has elapsed (for −2, 2 V-blanks). The switch is performed after
  > the writes are completed.
  **But Z-Treme's own frame-time measurement is CPU-side, off the FRT**
  (`ZT_SYSTEM.c:53-71`), not from VDP1. It never reads a VDP1 status register.
- **SGL's overrun policy is discard, not carry-over** (`SGL020A.TXT:1`, §2.5
  item 4):
  > In version 1.0, unfinished sprite draws were carried over to the next frame
  > whenever sprite data calculations were incomplete when frame change
  > occurred. This timing has been changed from version 1.1 so that any
  > undisplayed sprites within a frame are discarded and not carried over to the
  > next frame.
- **And commands persist in VDP1 VRAM between frames** (`SGLFAQ_F.TXT:1200-1218`,
  §4-1 on afterimages): *"VDP1's display commands are only transferred during
  this time, so display commands normally remain in VDP1 VRAM."* Relevant to our
  bank-overwrite guard.

### 3.3 Why our 16 VBlanks are unattributed — the mechanical answer

**The VDP1 draw wait is structurally not measured in this build.** Three facts:

1. The phase counters are **VBlank crossings**, not FRT ticks.
   `sourceboot_phase_accumulate(start, end, ...)` differences two values of
   `sourceboot_vblank_out_count` (`sourceboot/main.c:345-352`). Anything shorter
   than a VBlank is invisible, and the phases overlap so they cannot be summed.
2. The fields that would carry the VDP1 wait exist and are **dead**.
   `sourceboot_vdp1_wait_ticks_accum` is declared (`:254`), zeroed (`:959`) and
   read (`:1171`) — and **never incremented anywhere in the file**. In
   `sourceboot_present_generation` the profile fields are hard-assigned zero
   immediately after the fence (`sourceboot/main.c:1165-1176`):
   ```c
       vdp1_sync();
       ...
       sourceboot_fast3d.profile.vdp1_wait_ticks_last = 0U;
       sourceboot_fast3d.profile.vdp1_wait_ticks_accum =
           sourceboot_vdp1_wait_ticks_accum;
       sourceboot_fast3d.profile.vdp1_terminal_fence_wait_ticks_last = 0U;
       sourceboot_fast3d.profile.vdp1_terminal_fence_wait_ticks_accum = 0U;
   ```
   They are printed on the VDP2 HUD as `"VDP1W"` (`saturn_vdp2_frame.c:63-65`),
   so a reader sees a zero and concludes there is no wait.
3. The only live VDP1 wait counter times the **wrong** fence.
   `vdp1_overwrite_wait_ticks_*` (`sourceboot/main.c:1499-1510`) measures the
   `vdp1_sync_wait()` overwrite guard at the *top* of the transfer path, not the
   plot.

Meanwhile the FRT tree profiler we already have
(`src/port/saturn/runtime/saturn_prenotify_profile.h`, 24 nodes, clock select at
`:232-238`, explicitly modelled on SlaveDriver's `PROFILE.C` per its own comment
at `:230-231`) is scoped to the **pre-notification window only** —
`SM64_SATURN_PRENOTIFY_PROFILE_END()` fires at the notification marker
(`sourceboot/main.c:1236`). The present path is entirely outside it.

**So the likely composition of the ~16 VBlanks, in descending order of my
confidence:**

| Component | Confidence | Why |
| --- | --- | --- |
| **VDP1 draw-end wait inside `vdp1_sync()`** | High | We run `vdp1_sync_interval_set(-1)` — uncapped variable interval, framebuffer change only after EDSR.CEF reports draw-end — and the code comment says so, citing this same Z-Treme/SGL `slSynch` precedent (`sourceboot/main.c:1753-1762`). This is the one blocking fence nobody measures. The 2026-08-04 note of *VDP1 ≈ 2 FPS vs VDP2 ≈ 60 FPS* in Ymir is consistent with it and remains the leading hypothesis. |
| **Framebuffer erase** | Medium | We selected the same mode class Sega documents as costing *an extra full-screen sprite draw per frame* (`SGL020A.TXT:1` §2.2.2). Yaul's erase configuration is not audited in this report, and at 320×224 a full-screen erase is not free. |
| Command-VRAM upload | Medium | 653 × 32 B ≈ 20.9 KB copied word-by-word through both CPUs into VDP1 VRAM (`saturn_demo_render.c:3961-3976`), plus the SCU path in `sm64_saturn_vdp1_frame_bank_submit_transfers`. `dma_wait_ticks_last` is live (`sourceboot/main.c:1654-1659`), so this is partly visible already. |
| VBlank alignment slack | Low | Whatever the last phase leaves before `vdp2_sync()` commits. |

**I measured none of this.** §3.4 is how to settle it.

### 3.4 Concrete recommendation for the next measurement task

Use the profiler that already exists, and read the registers libyaul already
exposes.

**A. Bracket the present path with FRT ticks.**
`SM64_SATURN_PRENOTIFY_PROFILE_NODE_SPARE` is unused
(`saturn_prenotify_profile.h:128`). Either open a second profiler window around
`sourceboot_present_generation` (`sourceboot/main.c:1152-1200`), or — simpler and
lower risk — populate the already-declared dead fields with plain
`cpu_frt_count_get()` deltas:

| Field | Bracket |
| --- | --- |
| `vdp1_terminal_fence_wait_ticks_last` | around `vdp1_sync_render()` (`sourceboot/main.c:1159`) |
| `vdp1_wait_ticks_last` | around `vdp1_sync()` (`sourceboot/main.c:1165`) |
| a new `present_total_ticks_last` | the whole of `sourceboot_present_generation` |

All three are already surfaced on the VDP2 HUD (`saturn_vdp2_frame.c:63-65`) and
in the cadence trace, so no new plumbing is needed downstream.

**Clock arithmetic, and the one trap.** The profiler selects internal clock /128
(`saturn_prenotify_profile.h:232-238`). NTSC 320-mode SH-2 clock is 26.84 MHz
(`INIT.DOC:1`, `slSetTVMode`: *"320, 640 pixels … (26.84 MHz) … <NTSC>"*), so a
tick is ~4.77 µs and the 16-bit counter wraps at **~312 ms ≈ 18.8 VBlanks**
(arithmetic, mine). A single start/end delta around a wait longer than that
aliases silently. **Accumulate inside the spin loop** — the discipline the
existing profiler already uses (`sm64_saturn_prenotify_profile_charge`,
`:245-256`, which charges each probe gap rather than one span) — or bound the
wait and count iterations the way SlaveDriver does (`WALLS.C:2275-2277`).
Also note the FRT is a **per-CPU** block; the tree already learned this the hard
way (`sourceboot/main.c:1240-1244`), so only master-side brackets are valid here.

**B. Read VDP1's own registers to separate "waiting" from "drawing".** libyaul
maps them (`third_party/libyaul/libyaul/scu/bus/b/vdp/vdp1/map.h:110-116`:
`edsr`, `lopr`, `copr`, `modr`) and exposes `vdp1_transfer_status_get()`
returning EDSR (`vdp1.h:73-85`).

| Register | Read where | Tells you |
| --- | --- | --- |
| `EDSR` (CEF/BEF) | immediately before `vdp1_sync()` at `sourceboot/main.c:1165` | whether VDP1 had *already* finished when we arrived. CEF set on entry ⇒ the wait is zero and the 16 VBlanks are elsewhere |
| **`COPR`** (current command address) | inside the wait loop, or once per VBlank from `sourceboot_vblank_out_handler` (registered `sourceboot/main.c:1769`) | **the highest-value probe.** COPR/32 is the command index VDP1 is plotting. Logged once per VBlank across a frame it gives a *plot progress curve* — commands retired per VBlank — converting "VDP1 is slow" into "VDP1 retires N commands per VBlank on this scene", and showing whether a few large primitives dominate |
| `LOPR` | after draw-end | where the previous plot ended; cross-checks COPR sampling |
| `MODR` | once at init | confirms the VDP1 mode actually in force |

**C. The decisive cheap experiment.** Re-measure cadence on the same route with
Mario's commands suppressed — the LOD suppression path already exists
(`s_primitive_lod_suppressed`, `saturn_demo_render.c:1925-1930`). If the frame
collapses toward A9A's floor, the unattributed time is VDP1 fill and §4 rows 1–3
are the sprint. If it does not move, VDP1 is not the bottleneck and the 16
VBlanks are DMA/erase/alignment, and §4 re-ranks around row 6. One build, one
capture, and it discriminates between the two hypotheses better than any further
reading.

**D. Surface the per-source command breakdown that already exists.** The profile
carries `texture_commands` (`saturn_fast3d_frontend.h:60`),
`triangles_vdp1_emitted` (`:92`), `demo_actor_primitives_emitted` (`:346`) and
`vdp1_commands_last` (`:358`) — **none of which reaches the VDP2 HUD or the
cadence trace**. Adding actor-vs-terrain command counts to the HUD line costs
two `append_u32` calls (`saturn_vdp2_frame.c:56-65`) and immediately reconciles
"882 visible items" with "653 published commands", which no current instrument
does.

---

## 4. Gap table

Ordered by my estimate of value.

| # | Technique | SlaveDriver | Z-Treme / SGL | Us | Gap | Est. value | Risk / effort |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | **Slave overlap window** | Dispatch `WALLS.C:2246`, join `WALLS.C:2273`; master runs far-half geometry **and all of simulation** in between (`SRUINS.C:2094-2160`) | Non-blocking pointer ring, "no need for the master CPU to wait" (`SGLFAQ_F.TXT:1136-1143`); default is **slave-first** (`SPRITE.TXT:104-106`, `SGL020A.TXT:1` §1.1.10). Z-Treme itself: unused (`ZT_LOADING.c:110`, `SRC/game.c:760`) | Four blocking fork-joins per frame (`saturn_demo_render.c:1474,1532,3972`; `slavedriver_terrain_worker.c:14`), each spinning at `slavedriver_dual_worker.c:141-148` | Slave window ~3.09 VB of a ~41 VB frame; saturated but tiny. No work scheduled across simulation | **High** — slave is ~92% idle by frame time | Medium. Needs one job consumed *after* simulation. Ownership machinery (`sm64_saturn_dual_frame_*`, cache-through mirrors) already exists |
| 2 | **VDP1 draw-fence instrumentation** | `lastDraw=htimer-lastCalc;` (`SRUINS.C:2237`), on the debug HUD next to calc time (`SRUINS.C:2202-2204`) | `slDynamicFrame(ON)` draw-end advance (`main.c:94`, `INIT.DOC:1`); overrun discards (`SGL020A.TXT:1` §2.5.4). Z-Treme measures frames off the FRT, not VDP1 (`ZT_SYSTEM.c:53-71`) | `vdp1_wait_ticks_*` declared, zeroed, **never incremented** (`sourceboot/main.c:254,959,1165-1176`); phases counted in whole VBlanks (`:345-352`) | ~16 VBlanks/frame cannot be attributed | **High (diagnostic)** — unblocks every other row | **Very low.** Three FRT deltas into fields that already exist and already print |
| 3 | **Per-command user-clip enable + per-object clip rect** | `UCLPIN_ENABLE` on every terrain/character command (`SPR.C:237,252`; `PIC.C:85-96`) **plus** per-sector portal-bbox `EZ_userClip` on master and slave replay paths (`WALLS.C:2247-2253`, `1894-1902`) | `Window_In` on effectively every command (`SRC/sprites_data.h:9-31`, `ZT_LOAD_MODEL.c:78-113`); clipping via two SGL windows (`ZT_RENDERING.c:95-101`), **no per-object rect**; `slFrameClipSize` never called | **Zero** uses of `vdp1_cmdt_user_clip_coord_set`; `user_clipping_mode` never set anywhere in `src/` — so a clip command would be inert. One system clip at `saturn_vdp1_backend.h:69-70` | VDP1 rasterizes every command against the full frame | Medium–High if fill-bound | **Low.** One PMOD bit plus two commands per BSP package. The screen boxes already exist in the admission path |
| 4 | **Distance LOD that reduces command count** | `MIPMAP`: beyond `MIPDIST F(256)` a wall's tile grid halves in both axes → **4× fewer commands** (`WALLS.C:31,38,1001-1013,1281-1293`) | Second polygon table at 450 units (`ZT_RENDERING.c:457-460,469`); entities skipped past band 4 (`:461,470`); LOD ring textures (`:339-347`) | LOD suppresses whole primitives or downgrades material (`saturn_demo_render.c:1925-1934`); never reduces a surviving surface's command count | Nothing converts distance into fewer commands for visible geometry | Medium | Medium. Needs a coarse-tier bake; LOD tier plumbing exists |
| 5 | **High-speed shrink (CMDPMOD bit 12)** | Set on every terrain and character sprite (`SPR.C:237,252`; `PIC.C:85-96`; `WALLS.C:1078,1142,1913,2655,2664`) | Set on every sprite and every textured model polygon (`SRC/sprites_data.h:9-31`, `ZT_SPRITES.H:87,99`, `ZT_LOAD_MODEL.c:96,101`); documented `SPRITE.TXT:390-391` | `hss_enable` never set anywhere in `src/` (`cmdt.h:112`) | A draw mode **both** references use universally is unused | Unknown — do **not** adopt without measurement; HSS trades sampling accuracy for speed and only applies when shrinking | Very low effort; needs a visual gate |
| 6 | **Double-draw of textured actor primitives** | One command per wall tile (`WALLS.C:1909`) | One command per model face (`ZT_RENDERING.c:566-567`) | Textured Mario primitives emit **two** commands with the same sort key — Gouraud polygon (`saturn_demo_render.c:3871-3900`) then a distorted sprite over it (`:3903-3931`) | Deliberate overdraw with no reference precedent | Low absolute (only ~10 primitives carry a texture start, `saturn_mario_actor_mesh.h:49320-49327`) but it is a pattern to stop | Low. Bind the texture into the existing polygon slot instead of a second slot |
| 7 | **Sky occlusion windowing** | Sky plane windowed to the union of sky-visible boxes accumulated during wall rendering (`SRUINS.C:2277-2279`; `WALLS.C:1496-1514`, `2291-2306`) | Windows used only for split-screen viewports (`ZT_RENDERING.c:102-130`) | None — sky composites everywhere including under terrain | VDP2 bandwidth spent on covered pixels | Low–Medium (VDP2 bandwidth, not VDP1 time) | Medium. Needs a screen-box accumulator in the admission pass |
| 8 | **VDP2 colour offset for full-screen effects** | Damage flash, fades, underwater tint, invisibility dissolve all in `SCL_SetColOffset` / `SCL_SetColMixRate` (`SRUINS.C:128-143,2127-2154`) | Global fade only (`main.c:102-104`, `ZT_TOOLS.c:85-140`) | No VDP2 colour-offset or colour-calc call in `src/port/saturn/` | Any future full-screen effect would cost VDP1 commands | Low today, high later | Low |
| 9 | **Sky on a VDP2 rotation plane (roll)** | RBG0 512×256 bitmap rotated *and* scrolled per frame from yaw/pitch (`PLAX.C:94-111,25-53`) | **RBG0 unused** — `ztSetRBG0()` is an empty stub (`ZT_VDP2.c:28-31`) | NBG1 bitmap scrolled from yaw+pitch (`saturn_vdp2_frame.c:172-180`) — i.e. we already match Z-Treme | Only roll/rotation is missing, and only one reference does it | Low (fidelity) | Medium–High. RBG0 needs a coefficient table and a VDP2 VRAM bank rework; NBG1 currently owns banks A0+A1 in full (`sourceboot/main.c:1036-1066`) |
| 10 | **Slave writes VDP1 VRAM directly** | Never — slave stops at geometry records (`WALLS.C:1806-1819`), master emits (`WALLS.C:1909`) | Never — slave stops at `SpriteBuf2` (`SL_DEF.H:848`); SCU-DMA writes VRAM (`SGLFAQ_F.TXT:1108-1112`) | **Both CPUs write `VDP1_VRAM(0)`** (`saturn_demo_render.c:3961-3976`) | Outside both references' practice | Neutral-to-negative: a bus-bound split cannot approach 2× (**inference**) | Low effort to replace with SCU-DMA plus a real compute job |
| 11 | **Oversized art escalated to VDP2** | `TILEVDP` class: art too big for a VDP1 character is drawn by moving/windowing an NBG0 bitmap (`PIC.C:89,455-498`, `SEQUENCE.C:267-294`) | The equivalent experiment exists but is disabled (`ZT_LOADING.c:528-543`, `ZTE_DEF.H:108`) | No analogue; our HUD is already on VDP2, which is the same instinct | Only matters for future large 2D elements | Low today | Low |

---

## 5. Ranked "adopt next" — top 5, each with its confirming measurement

**Rows 1 and 2 are measurement, not change. Do not commit to rows 3–5 before
they land.**

### 1. Instrument the VDP1 draw fence (gap row 2)

- **Change**: populate `vdp1_wait_ticks_last`,
  `vdp1_terminal_fence_wait_ticks_last` and a new present-total with FRT deltas
  around `sourceboot/main.c:1159` and `:1165`, accumulating inside the wait to
  avoid the ~312 ms / 18.8-VBlank 16-bit wrap (§3.4A).
- **Confirming measurement**: one capture on `camroute0`. A large
  `vdp1_wait_ticks_last` means the 16 VBlanks are VDP1 and rows 3–5 are the
  sprint; near zero means re-rank around DMA and erase.
- **Why first**: cheapest change in the table, and every other ranking depends
  on its answer.

### 2. Sample `COPR` once per VBlank, plus the per-source command split (gap row 2)

- **Change**: in `sourceboot_vblank_out_handler` (registered
  `sourceboot/main.c:1770`) record `vdp1_ioregs->copr` (`vdp1/map.h:114`) into a
  small ring, and sample `EDSR` on entry to `vdp1_sync()`. Separately, add
  `demo_actor_primitives_emitted` and `texture_commands` to the HUD line
  (`saturn_vdp2_frame.c:56-65`) — both already exist in the profile (§3.4D).
- **Confirming measurement**: the COPR curve is *commands retired per VBlank*.
  With the published `command_count` (653) it yields per-command plot cost
  directly, and the actor/terrain split says whose commands they are.
- **Why second**: it turns rows 3, 4 and 6 from opinions into arithmetic.

### 3. Widen the slave's overlap window (gap row 1)

- **Change**: move one existing job so its result is consumed *after*
  simulation rather than at the next statement — SlaveDriver's exact topology
  (`WALLS.C:2246` → `SRUINS.C:2102-2156` → `WALLS.C:2273`). The terrain range job
  is the natural candidate; the disjoint-range ownership rule
  (`slavedriver_dual_worker.h:1-10`) is unchanged, only the join point moves.
  Retire the word-copy split (`saturn_demo_render.c:3972`) at the same time — it
  satisfies none of §1.2's four eligibility properties and puts the slave inside
  VDP1 VRAM, which neither reference does (gap row 10).
- **Confirming measurement**: `slave_busy_ticks` and `master_wait_ticks` are
  already collected (`slavedriver_dual_worker.c:174-178`). Success is
  `slave_busy_ticks` rising while frame VBlanks fall. `master_wait_ticks` rising
  with no cadence change means the window moved but the work did not.
- **Risk**: highest in the list. Every cross-generation ownership invariant in
  `saturn_render_overlap_phase` and `saturn_vdp1_frame_bank` assumes the current
  join order.

### 4. Enable per-command user clipping, then per-package clip rectangles (gap row 3)

- **Change**: two steps, in order. First set `user_clipping_mode` on world
  commands — without it any clip command is inert, and both references set the
  equivalent bit on everything (`SPR.C:237,252`; `SRC/sprites_data.h:9-31`).
  Then emit `vdp1_cmdt_user_clip_coord_set` with each BSP package's screen-space
  box before its command run, SlaveDriver's exact arrangement
  (`WALLS.C:2247-2253`). Two commands per package; T2.1 measured 1,011 commands
  of headroom.
- **Confirming measurement**: gated on adopt-next #1/#2. Adopt only if the VDP1
  wait is material; then re-run the same route and compare
  `vdp1_wait_ticks_last`. Our HUD is on VDP2, so the "HUD must escape the clip"
  problem SlaveDriver solves by omitting `UCLPIN_ENABLE` (`SRUINS.C:1235,1251,1286`)
  does not arise.
- **Risk**: low. A wrong box clips visible geometry, which the owner gate sees
  immediately.

### 5. Distance LOD that reduces command count (gap row 4)

- **Change**: SlaveDriver's `MIPMAP` in our terms — beyond a distance threshold
  a surface emits its coarse meshlet tier instead of its fine one, with a
  pre-baked half-resolution texture in the same VRAM slot (`WALLS.C:1001-1013`,
  `PIC.C:359-388`). Z-Treme's variant is a second polygon table sharing the
  vertex table (`ZT_LOADING.c:154-170`), which may suit our meshlet bake better.
- **Confirming measurement**: `command_count` peak on the same route (T2.1's
  `vdp1_commands_highwater` probe exists) plus the VDP1 wait from #1. Success is
  a lower peak *and* a lower wait; a lower peak with an unchanged wait means we
  were never command-count bound.
- **Risk**: medium; visible LOD popping is an owner-gate question.

**Deliberately not in the top 5.** HSS (gap row 5) is a one-bit change that both
references use universally, but neither states when it is safe and it trades
sampling accuracy for speed — it needs a visual gate, not a ranking. The
double-draw fix (row 6) is correct but affects ~10 primitives. RBG0 sky (row 9)
is fidelity-led, only one reference does it, and it touches the VDP2 VRAM bank
plan, which is currently load-bearing and well-commented
(`sourceboot/main.c:1036-1066`); it should follow a cadence win, not precede one.

---

## 6. What I could not determine

1. **No VDP1 primitive cost table exists in either tree.** All 25 files under
   `Documentation/DOC/210A_US/` and the Japanese counterpart set were searched;
   there is no `SPR_*.TXT`, no `SL_*.TXT`, no `VDP1*.TXT`, and no
   cycles/drawing-speed/fill-rate statement about any primitive. The nearest
   adjacent statements are the distorted-sprite preclipping note
   (`SGL020A.TXT:1` §1.1.4, §2.4 above) and the framebuffer-readback latency
   figure (`SPRITE.TXT:288-291`). `WORKAREA.TXT:68-76` is a **host work-RAM**
   cost table, not a VDP1 one. Every claim in this report that a distorted
   sprite or polygon is dearer than a normal sprite is **inference** and is
   marked as such.
2. **Four Sega PDFs under `Documentation/` could not be read** —
   `ST-135-R4-092295.pdf`, `ST-237-R1-051795.pdf`, `ST-238-R1-051795.pdf`,
   `TUTORIAL.pdf`. All carry `/Filter /Standard` (encrypted) and are largely
   scanned raster; no PDF rasteriser was available. **These are the only
   unexamined candidates in the tree for a VDP1 timing table.** If a citable
   hardware cost figure ever becomes load-bearing, that is where to look.
3. **SGL's slave scheduling is documented, not readable.**
   `Compiler/SGL_302j/LIB_COFF/LIBSGL.A` is a binary COFF archive. The
   master/slave split inside `slPutPolygon`, the `SlaveControl` dispatcher, the
   Z-sort drain and the CMDCTRL byte assembly are known only from
   `SGLFAQ_F.TXT`, `SPRITE.TXT`, `SGL020A.TXT`, `INIT.DOC` and the `SL_DEF.H` /
   `MEMORY.TXT` layouts. I read no instruction of it.
4. **Z-Treme contributes nothing to Question 1** — both `slSlaveFunc` sites are
   commented out (`ZT_LOADING.c:110`, `SRC/game.c:760`).
5. **Neither engine's slave-carried fraction is a number.** SlaveDriver's
   controller targets a spin count, not a ratio (`WALLS.C:2276-2283`); SGL's
   counters are not split by CPU (`MEMORY.TXT:74-76`).
6. **Z-Treme's character polygon counts are not readable** — models are binary
   `.ZTP` files. The comparison "644 Mario polygons vs a 1,900-polygon whole-scene
   budget" (`Common.h:21-22`) is a budget comparison, not a like-for-like model
   comparison.
7. **No "VDP1 is the bottleneck" comment exists in either engine**, and no
   Gouraud-cost comment in either. Z-Treme's `debug.txt` lists emulator VDP1
   profiling toggles (`PG_VDP1_NORMAL`, `PG_VFP1_GOURAUDSAHDING`, …) showing the
   author profiled VDP1 command classes, but it records no measurements.
8. **SlaveDriver's `RBG1` is given priority 7 (`SRUINS.C:1072`) but I found no
   configuration or content for it** in the primary sources. Either vestigial or
   set up in a path I did not open.
9. **I did not audit our framebuffer-erase configuration.** Per `SGL020A.TXT:1`
   §2.2.2 a full-screen erase is itself a VDP1 draw in draw-end-wait mode, and we
   run the Yaul analogue of that mode (`sourceboot/main.c:1753-1762`). This
   report does not establish what ours costs; it is listed in §3.3 as a
   medium-confidence component of the 16 VBlanks.
10. **The 2026-08-04 "VDP1 ≈ 2 FPS vs VDP2 ≈ 60 FPS" Ymir note was not
    re-observed.** It remains a hypothesis, and §3.4 is written so counters
    confirm or kill it rather than another emulator reading.
11. **No measurement was taken for this report.** Every cadence and phase figure
    is quoted from an existing evidence file; nothing was built or run.
12. I read the primary (non-`FLASH/`, non-`OLDJAP/`, non-`SAVE/`, non-`JEFF/`)
    copies of SlaveDriver's sources, as T2.0 did.
