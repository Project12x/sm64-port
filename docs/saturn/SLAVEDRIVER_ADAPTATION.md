# SlaveDriver adaptation boundary

SlaveDriver is a valid implementation reference for this port. The pinned
source revision is `a8986591557b6e680550d3c23970284d3b38ff8f`, licensed
GPL-3.0-or-later. The files reviewed were `DMA.C`, `DMA.H`, `SCL_FUNC.C`,
`INITMAIN.C`, `MEMCPY.S`, `LINK.S`, `README.md`, and `LICENSE.txt`.
Task 1 additionally inspected `WALLASM.S:253-353` for its projection DIVU
launch/independent-work/collect schedule.

## What we will reuse

The useful part is the small producer/consumer DMA queue:

- fixed-size, power-of-two request ring;
- explicit head/tail ownership;
- queue draining at a frame/interrupt boundary;
- CPU-copy fallback for transfers that cannot use SCU-DMA; and
- a wait-before-submit path for synchronous clients.

That maps well to SM64's display-list and texture upload staging, where work
can be accumulated while the game builds the next frame and drained after the
VDP1 submission boundary.

## What must be rewritten for Yaul

`DMA.C` is not copied verbatim. It uses an obsolete Saturn SDK, casts pointers
through 32-bit `int`, and writes implementation-specific SCU registers
directly. The Yaul close-port will instead:

1. use `uintptr_t` for address classification;
2. call the pinned libyaul SCU-DMA API (`scu_dma_transfer` and
   `scu_dma_transfer_wait`);
3. keep the source queue's bounded-ring behavior and CPU fallback; and
4. expose the adapter only from an explicitly GPL-3.0-or-later component.

The adapter must retain the SlaveDriver copyright notice, GPL-3.0-or-later
text, the pinned source commit, and a change notice identifying the Yaul API
rewrite. It must not be linked into a component whose licensing terms cannot
accept GPL code.

## Current status

The isolated adapter now lives in `src/port/saturn/gpl/` and is linked into the
hardware-test image. It replaces the cart-to-WRAM, WRAM-to-VDP1, texture, and
Gouraud upload calls with the bounded queue and Yaul-backed transfer/wait path.
The host tool tests pass and the image builds with the pinned SH-2 toolchain.
Boot and compatibility clients may still drain synchronously. A8's accepted
sourceboot frame path instead carries a TRANSFERRING bank across fields. It
adapts SlaveDriver's bounded serial-ring shape as a **close-port**, but departs
from upstream by atomically pairing two VDP1 destinations, tracking exact
per-ticket completion/failure, and delaying bank publication until both retire.
Upstream active DMA is serial; this project does not claim concurrent CPU-DMAC
and SCU-DMA lanes.

The E2 sourceboot target links that adapter for the A8 frame path. Both the
demo and normal/full-game emitters now only construct source banks. After the
old list is overwrite-safe, the shared manager queues LWRAM commands through
CPU-DMAC and HWRAM Gouraud tables through SCU-DMA, then returns. A later field
polls, arms, and publishes the resident list. Yaul commit
`6012f79f237773378c8014e70d8998ad95a38d98` (MIT), file
`libyaul/scu/bus/cpu/cpu_dmac.c`, was inspected and used **pattern-only**:
the queue does not call its wait-before-start convenience helper. After the
post-boot ownership handoff it uses public channel configure/start calls and a
queue-owned completion IHR. This is required because pinned
`cpu_dmac_status_get().channel_busy` can report false idle for DE=1/TE=0; only
the IHR retires the command ticket, while status still exposes address/NMI
failure. No Yaul implementation was copied. Z-Treme commit
`cff75451c1616aac1236fc2b44223902b55c706b` (GPL-3.0) remains
**pattern-only** for double-buffered build/present ownership; A8 deliberately
does not copy its renderer or claim destination-banked overlap.

Task 1 also adds the isolated `slavedriver_projection.sx/.h` close-port. It
owns only Q16 DIVU start/collect transport (not a SlaveDriver wall renderer),
retains the GPL declaration, upstream path, pin, and material-change note,
and is exercised once at sourceboot startup by a debugger-readable target
vector. Pinned Yaul public CPU-DIVU headers replace any raw SDK assumptions.

## Compact terrain results (renderer pipeline sprint)

The sprint's first renderer boundary is the close-port result discipline in
`src/port/saturn/gpl/slavedriver_terrain_result.h`. It adapts the fixed result
span and pre-write guard from `WALLS.C:1240-1408` at the pinned commit
`a8986591557b6e680550d3c23970284d3b38ff8f`. The adaptation is intentionally
smaller than the upstream wall record: it stores only projected corners,
shade inputs, stable primitive/leaf/material identities, painter key, and
clip/material flags. It contains no VDP1 command pointer or allocator state.

Master and slave receive separate fixed-capacity arenas. A producer reserves
the complete output count before writing a clipped fan, with explicit
headroom; rejected reservations increment a reasoned counter and do not write
past the arena. The master merges the spans after the one-dispatch join and is
the only owner allowed to lower results into VDP1 commands, texture slots, or
Gouraud tables. This is a close-port of the ownership pattern, not a copy of
SlaveDriver's sector/portal world model.

The same sprint adds `slavedriver_terrain_worker.{c,h}` as the narrow hand-off
wrapper. It permits exactly one notification per rendered frame and carries a
single immutable range job into the existing Yaul polling adapter. The current
callback performs terrain classify/compact production; master-only VDP1
lowering and Mario remain outside the worker boundary. Transform-once and
view-space clip stages are explicit inputs to that callback, so a timeout can
discard both spans and fall back to the serial oracle without consuming
partial results.

## The slave dispatch schedule (Sprint 2 T2.25)

Pinned revision `a8986591557b6e680550d3c23970284d3b38ff8f`, GPL-3.0-or-later.
Files read for this section, all at that commit: `WALLS.C:1255-1273`,
`:1376-1379`, `:1795-1817`, `:1822-1835`, `:1918-1934`, `:1936-1950`,
`:2061-2062`, `:2240-2271`, `:2273-2299`; `SRUINS.C:2094-2160`;
`V_BLANK.C:36,130`. Reuse mode for this task: **behaviour and structure read
in full, nothing copied.** Section "Why nothing was ported" says why, and the
answer is a measurement rather than a licence question -- direct GPL reuse is
authorised for this component.

### What upstream actually does

One dispatch and one join per frame, with the game simulation between them:

| Step | Site | Mechanism |
| :--- | :--- | :--- |
| size the slave's share | `WALLS.C:2242-2244` | `slaveSize` clamped to `updateListSize-1`, then `slaveDrawStart = slaveSize` |
| dispatch | `WALLS.C:2246` | `*(Uint16 volatile *)0x21000000 = 0xffff` -- the master-to-slave FRT input-capture trigger |
| master's own share | `WALLS.C:2247-2255` | draws `updateList[updateListSize-1 .. slaveDrawStart+1]` |
| **all simulation** | `SRUINS.C:2102-2156` | `movePlayer`, `runObjects`, item/hurt/invisibility counters -- between the two calls, by construction |
| join | `WALLS.C:2277-2280` | `i = 0; while (!(*FTCSR & 0x80)) i++;` then `*FTCSR = 0x0` |
| **adapt** | `WALLS.C:2281-2284` | `if (i > 100 && slaveSize > 0) slaveSize--; if (i < 100 && slaveSize < 50) slaveSize++;` |
| merge | `WALLS.C:2285` | `drawSlaveWalls()` -- master-only conversion of `slaveResult[]` into EZ_ commands |

The slave side is a permanent loop: `wallRenderSlaveMain()` (`WALLS.C:1918-1934`)
does `while (1) { while (!(*FTCSR & 0x80)); *FTCSR = 0; slaveDraw();
*(Uint16 volatile *)0x21800000 = 0xffff; }`.

Two properties are worth stating explicitly because they bound what the split
can ever be worth:

1. **Upstream's visibility determination is master-only and serial.**
   `drawWalls()` builds `updateList` by leaf plucking and sorting
   (`WALLS.C:2180-2239`) *before* the dispatch. The slave never participates.
2. **Upstream's command emission is master-only.** `slaveDraw()` writes
   projected/lit polygon records into `slaveResult[]`; only the master turns
   them into EZ_ commands, and it stitches its own run to the slave's with
   `EZ_linkCommand(lastWallCmd, JUMP_ASSIGN, EZ_getNextCmdNm())`
   (`WALLS.C:2270`) and per-sector `JUMP_CALL` fixups (`WALLS.C:1885-1888`).

So upstream splits exactly one stage -- transform/light of an
already-determined draw list -- and keeps the stage before it and the stage
after it serial on the master.

### How upstream handles cache coherency

The SH7604 pair has no cache coherency. Upstream's discipline is two
mechanisms, and it is worth being precise about which one is for correctness:

- **Whole-cache purge at each boundary.** `slaveDraw()` opens with
  `*CACHECNTRL = 0x10; *CACHECNTRL = 0x01;` (`WALLS.C:1806-1807`) and
  `drawSlaveWalls()` opens with the same pair (`WALLS.C:1824-1825`).
  `CACHECNTRL` is CCR at `0xfffffe92`; `0x10` is CP (purge every entry),
  `0x01` re-enables. So the slave drops its cache before reading state the
  master just wrote, and the master drops its cache before reading the
  slave's results. This is the correctness mechanism, and it is blunt: it
  discards the reader's entire working set once per frame per CPU.
- **Cache-through aliasing for the streaming result array.**
  `slave_drawRectWall()` and `slave_drawWall()` each derive
  `cacheThruResult = (struct slaveDrawResult *)(((int)slaveResult) + 0x20000000)`
  (`WALLS.C:1272-1273`, `:1377-1378`) and write records through it. The SH-2
  cache is write-through, so this is not needed for the write to reach RAM;
  it is there so that a large write-only stream does not evict the slave's
  hot read set.

### What this port adopts, and where it diverges deliberately

The port already implements the same two ideas, in a finer-grained form, and
this task's audit confirmed the placement rather than changing it:

| Concern | SlaveDriver | This port |
| :--- | :--- | :--- |
| release record | implicit; the FRT ICF flag plus a whole-cache purge | explicit `.uncached` section (`DEMO_CROSS_CPU_SHARED`, `saturn_demo_render.c:204-214`) for fence records and result-span headers |
| peer payload read | whole-cache purge, then ordinary reads | per-object cache-through alias, selected by lane: `sm64_saturn_render_job_queue_cache_through()`, `graph_cache_through()`, `sm64_saturn_dual_frame_cache_through()`, and `access->cache_through = reader_lane != release->producer_lane` (`saturn_render_callback_context.c:78`) |
| streaming result writes | `+0x20000000` alias | lane-owned arenas with a reserve-before-write guard (`slavedriver_terrain_result.h`, itself a close-port of `WALLS.C:1240-1408`) |
| enforcement | none -- discipline by convention | build-time gates: `tools/saturn/verify_dual_cpu_coherency.py`, now including `--graph-source` |

**The divergence is deliberate and is a strict improvement:** a whole-cache
purge costs the reader its entire working set, twice per frame, on a machine
whose frame is memory-bound enough that evicting the 54,080 B hot set to
16-bit LWRAM once took the accepted 5.29 FPS to roughly 1
(`saturn_demo_render.c:216-234`). Per-object
cache-through aliasing pays only for the words that actually cross. The
port therefore does **not** adopt `CACHECNTRL` purging, and
`verify_dual_cpu_coherency.py`'s `--source` mode explicitly rejects a
whole-cache purge appearing in the accepted transform frame path.

**The audit found one place where the port had lapsed from its own rule.**
`sm64_saturn_render_job_graph_propagate_failures()` dereferenced
`graph->count`, `graph->queue` and `graph->dependency_mask[]` through the
cached alias, on a path both SH-2s execute. Fixed, and pinned by the new
`--graph-source` gate, which fails on the pre-fix source. No emulator capture
could have found it: Ymir sets `m_emulateSH2Caches = false`.

### Why nothing was ported

The dispatch schedule itself was measured before it was copied, and the
measurement says a port would convert nothing. In one sentence: **this port's
master never waits, so widening the slave's window has nothing to widen.**

- Master idle is **0.000000** at both route positions measured
  (`sprint2-t2_20-idle-attribution-tick{30,180}`), so frame time equals master
  work exactly. Overlap cannot shorten a critical path that contains no stall;
  only moving work off it can.
- Upstream's adaptive controller drives `slaveSize` from the master's join
  spin count `i`. Here `i` is always 0, so the controller's rule
  `if (i < 100) slaveSize++` saturates at "give the slave everything" -- which
  is where this port already is. **Its fixed point is our current
  configuration**, so porting it is provably a no-op on this route. It is
  still worth having as a heavy-scene safety valve, and that is recorded as
  future work rather than smuggled in as a cadence change.
- The two master blocks large enough to matter are the two upstream also
  keeps master-only: visibility determination before the split (ours is a
  BFS over a node hierarchy with shared visited/queued/seen state,
  `saturn_scene_admission.c:487-745`) and VDP1 command emission after the join
  (ours is a sequential bump-allocating arena plus a shared Gouraud bank,
  `saturn_demo_render.c:4729-4775`).

Full numbers, the offload-eligibility census and the ranked alternatives are in
`docs/saturn/evidence/reports/sprint2-t2_25-slave-dispatch.md`.
