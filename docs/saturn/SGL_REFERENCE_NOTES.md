# SGL official-documentation study notes

Study record for Sega's SGL (Saturn Graphics Library) official SDK
documentation, read as prior art for the Fast3D-to-VDP1 lowering pipeline.
Companion to [`RENDERER_PRIOR_ART.md`](RENDERER_PRIOR_ART.md); provenance
entry in [`PROVENANCE.md`](PROVENANCE.md).

## Source and licensing posture

- Archive: `SGL302J.ZIP` (user-supplied, SGL 3.02j SDK, file dates 1994-1996),
  SHA-256 `429d729952b6837e2af221a5a6e0de4ca58d2ed1ce2471bd65411a62954ff811`.
- Contents relevant here: `DOC/210A_US/*.TXT` — official English plain-text
  manual chapters (2.10a-era docs shipped inside the 3.02j archive; hardware
  constraints described are version-independent). Also present: `LIB/LIBSGL.A`
  (compiled binary only — **no SGL library source exists in this archive**),
  `INC/` headers, official sample/demo programs, sound-driver assembly.
- Posture: **documentation study only.** SGL is proprietary Sega software.
  Nothing from the archive is copied, linked, or redistributed by this
  repository; the archive stays outside the repo. Techniques learned from the
  documentation are reimplemented independently against Yaul. Quotes below are
  limited to short attributed phrases. This is the same behavior-study rule
  the ledger already applies to non-licensable sources.
- Files read in full: `MATH.TXT`, `SPRITE.TXT`, `WORKAREA.TXT`, `MEMORY.TXT`,
  `SGLFAQ_F.TXT`, `SCROLL.TXT`, `PER.DOC`, `INT.TXT`, `EVENT.DOC`, `INIT.DOC`,
  `DMA.DOC`, `SGL020A.TXT`, `SGL0210.TXT`, `BITMAP.DOC`, `PACKS.TXT`,
  `MANGFS.TXT` (GFS/CD file-system manual).

## Critical code-facing findings

### 1. SCU DMA cannot touch LWRAM — the VDP1 upload path locked real hardware [FIXED]

**Status: fixed in `263c3f3` (CPU longword copy + `vdp1_sync_force_put()`,
compile-time-selected for the LWRAM-staged sourceboot target only; SCU path
retained for HWRAM-staged targets with a runtime assert) and hardened in
`bc351f7`. Confirmed by an independent verification review (rebuilds, symbol/
section audits, reconstructed baseline link) and an adversarial review that
traced all of `vdp_sync.c` and found no hole. Real-hardware confirmation of
the lockup premise itself remains outstanding — no emulator models it.**

Three independent sources agree:

- SGL release notes (`SGL020A.TXT`): PCM data placed in WORKRAM-L "could not
  be transferred" by SCU DMA — Sega moved those transfers to CPU DMA.
- GFS manual (`MANGFS.TXT` §1.7): transfers destined for WORKRAM-L
  (`0x00200000-0x002FFFFF`) force a CPU software transfer even when SCU DMA
  is requested.
- libyaul's own `scu/dma.h:32`: "Reading from or writing to LWRAM locks up
  the machine."

The merged sourceboot pipeline stages VDP1 commands in LWRAM
(`sourceboot_vdp1_cmdts`, section `.lwram_cmdts`) and uploads via
`vdp1_sync_cmdt_list_put()` — which (verified in
`third_party/libyaul/.../vdp_sync.c`, `vdp1_sync_cmdt_put`) programs an SCU
DMA read (`dnr`) from that LWRAM address with no address check or fallback.
**On real hardware this locks the machine on the first frame.**

**Ymir will not catch it** (verified in `ymir-agent`
`libs/ymir-core/src/ymir/hw/scu/scu.cpp:720,735`): its SCU DMA path is an
unrestricted `m_bus.Read<uint32>()`, and `scu.hpp`'s TODO comments show
hardware lockup regions are unmodeled. Emulator captures would show correct
triangles while retail hardware hangs — a concrete instance of the
"emulator evidence is not retail-hardware proof" rule.

Fix directions (decision pending): (a) replace the backend's upload with a
CPU-DMAC or CPU-copy path — SGL's own documented answer for WORKRAM-L
(`slDMACopy` is SH-2 on-chip DMA, no region restriction; SlaveDriver's real
command upload, `dmaMemCpy`, is likewise CPU DMA); (b) build commands
directly in VDP1 VRAM with CPU writes and a double-banked VRAM region
(SlaveDriver's `SPR.C` pattern); (c) move staging back to HWRAM — currently
infeasible (~380 bytes free vs. 16 KiB needed) without shrinking triangle
capacity. Whatever is chosen must integrate with Yaul's `vdp_sync` flag state
machine, which assumes its own SCU-DMA end handler sets `LIST_XFERRED`.

### 2. VDP1 coordinate validity is ±2047 x / ±1023 y — the int16 clamp was too wide [FIXED]

**Status: fixed in `bc351f7` — clamp bounds now the VDP1 window. Analysis
during the fix showed this was defense-in-depth rather than a live bug: the
span check (640) plus offscreen-visibility rejection already kept every
emitted coordinate within roughly ±960, so the old int16 clamp never bound
on surviving triangles. The new bounds make the hardware-validity guarantee
local instead of an emergent property of the span constant staying small.**

The FAQ (§3-4(c)) gives VDP1's valid drawing coordinate window as X in
[-2048, +2047], Y in [-1024, +1023], and separately warns that overflowing
position values can wrap sprites back onto the screen. The frontend's screen
coordinate clamp (Task 9) saturates to int16 range (±32767) — inside that
window values beyond ±2048 are UB-free C but **hardware-invalid**, risking
wraparound artifacts. The clamp bound should be the VDP1 validity window (or
the triangle rejected), not int16 limits.

## Decisions validated by the official docs

| Port decision | SGL confirmation |
|---|---|
| Q16.16 fixed point | SGL `FIXED` is the identical 16.16 format (`MATH.TXT`) |
| `max_z` depth keying | One of SGL's four official sort modes (`SortMax`; also `SortCen`/`SortMin`/`SortBfr`) (`SPRITE.TXT`) |
| Degenerate-quad triangles | "For triangular polygons, P2 and P3 are the same number" — the official convention (`SPRITE.TXT`) |
| Double-banked command staging (planned, from SlaveDriver study) | `Spritebuf`/`Spritebuf2`: two equal banks toggled per frame by `SprbufBias`; the VDP1 VRAM list itself stays single (`MEMORY.TXT`, `WORKAREA.TXT`) |
| Coordinate-overflow concern is real | `UseClip` exists specifically for "large polygons that cause the display position to overflow" (`SPRITE.TXT`) |
| Zero-triangle frames need explicit guarding | SGL itself shipped a master/slave deadlock with zero sprites (fixed v1.2) and a crash on zero-polygon models (fixed v2.0) (`SGL020A.TXT`) |
| LWRAM for bulk staging when HWRAM is tight | SGL budgeted 256 KiB of HWRAM (`0x40000` at `060C0000`) for its pipeline; shipped titles shrank it. Our HWRAM situation is far tighter — but see critical finding 1 for the DMA constraint that comes with LWRAM |

Deliberate difference, reaffirmed: SGL matrices are 3x4 affine (no
perspective column; projection is a separate divide stage, 20-level stack).
The port's full 4x4 homogeneous stack exists because it decodes real N64
Fast3D projection matrices — not a candidate for "simplification" to SGL's
shape.

## Technique references for future increments

### Depth sort (scaling past 16 buckets)

SGL's sort (FAQ §3-6, `MEMORY.TXT`): 128 primary list-head slots per VDP1
window (`Zbuffer`/`Zbuffer2`, pointer-based) plus a 256-entry secondary
"nest" buffer; a lazy two-level radix on a 15-bit Z where the secondary pass
runs only for occupied primary slots. Staged polygon records are 34 bytes:
the raw 30-byte VDP1 command plus Z-position and next-pointer metadata.
Compared to the port's 16 linear buckets: 8x finer primary resolution,
effectively 32K-level ordering, cost proportional to occupied slots only.

Sorted emission is **not** a CPU copy: SGL walks the Z structure back-to-front
writing 12-byte SCU indirect-DMA descriptors (`SortList`, sized
`(polygons+6)*12`, power-of-2 boundary), and the SCU gathers commands into
VDP1 VRAM during V-blank. Note: this works because SGL's staging lives in
HWRAM — the gather trick is unavailable to LWRAM staging (finding 1).

### Near-plane policy (third reference point)

SGL neither hard-rejects nor Sutherland-Hodgman-clips: it **clamps** — a
Z' closer than the `slZdspLevel` threshold (screen-distance/2, /4, or /8) "is
treated as" the threshold value, with per-polygon rejection only via the
`Zlimit` far bound (FAQ §3-4, §3-3; `INIT.DOC` defaults: Zlimit `0x7fff`,
level 1). So the three studied references answer near-plane crossing three
ways: this port hard-rejects (pops), SlaveDriver clips with interpolation
(correct, costlier), SGL clamps (cheap, mild distortion, no popping). Clamp
is a legitimate low-cost middle option for the port before full clipping.

### Frame pacing and sync

- SGL never blocks the game loop on VDP1 draw-end the way the port's
  back-to-back `vdp1_sync_render(); vdp1_sync();` does. `slSynch` hands the
  transfer to V-blank processing; commands persist in VDP1 VRAM (a missed
  sync leaves stale afterimages — draw-end command terminates the list).
- Official overload policies: fixed 1/N frame rate (`SynchConst`), or
  draw-end-wait dynamic frame change (`slDynamicFrame` /
  `slInitSystem` negative frame-change count = wait-minimum-N-vblanks then
  poll draw-end). From v1.1 on, sprites that miss the frame change are
  **discarded, not carried over** — official practice is drop-don't-stall.
- Official vblank edge assignment (`INT.TXT`): V-blank-IN = VDP2 shadow
  register commit + queued `slTransferEntry` SCU indirect DMA + user hooks;
  V-blank-OUT = VDP1 framebuffer erase, frame-change switch, sprite command
  DMA, SMPC peripheral reads. A direct template for placing the port's
  VDP1 upload vs. future VDP2 state commits.
- SGL's vblank-in user hook must not use the SH-2 divide unit (DIVU state is
  not saved across the ISR) — audit any future Yaul vblank callback for the
  same hazard.

### SH-2 micro-patterns

- The 39-clock DIVU divide is issued early and hidden behind the remaining
  X'/Y' MAC row products (FAQ §3-4) — a concrete scheduling pattern to audit
  the port's vertex transform against when it moves to fixed-point.
- All hot SGL state is GBR-anchored (`0x060FFC00` system area, "GBR register
  always points to here") for short-encoding access — a cheap codegen trick
  for the port's hottest globals.
- Copy-free stack push (`slIncMatrixPtr`) and register/no-copy variants exist
  "to minimize the overhead" of full `slPushMatrix`/`slPopMatrix` copies —
  same optimization applies to the port's 11-deep 4x4 stack if profiling
  shows push/pop cost.
- SGL runs the CPU in 4-way cache mode and keeps DIVU/MAC use out of
  interrupts so applications may use them freely.

### Master/slave SH-2 (future dual-CPU work)

FAQ §2-10, `SGL020A.TXT`, `MEMORY.TXT`: `slPutPolygon` is adaptive
work-stealing with three load states (slave idle → slave does everything;
one request pending → master transforms, slave finishes; two+ pending →
master does all but Z-registration). Shared buffer filled slave-from-top /
master-from-bottom; master-to-slave work goes through a 32-byte-entry ring
(`CommandBuf`, `ComRdPtr`/`ComWrPtr`); slave woken by FRT input-capture write
(`0x21000000`), not cache-through mailbox polling (v2.0 change, reduces bus
contention); slave never touches SMPC (semaphore-guarded, master-only, else
"SMPC deadlock"); manual cache purge discipline for shared data; interrupts
stay on the master. Per-CPU duplicated pipeline state (light vector, screen
distance, buffer cursors) throughout the system area.

### SMPC / input timing

`PER.DOC`: IntBack is issued from vblank-in service ~300 µs after V-blank-IN;
results are harvested at V-blank-OUT; pad data is double-buffered with the
switch at vblank (stable snapshot, 1-frame latency floor; 2-frame settling
after reconfiguration); at most 31 cached SMPC commands. SMPC I/O is a
vblank-domain activity, never a mid-frame poll.

### VDP2 conventions (for future sky/letterbox/fade work)

`SCROLL.TXT`, `INIT.DOC`:

- Default is shadow-register-commit-at-vblank; only functions explicitly
  marked set VDP2 registers immediately. Adopt the same convention.
- VDP1 output is composited under VDP2 priority as up to 8 sprite groups
  (`scnSPR0-7`); priority 0 = invisible (footgun). Default layering: NBG0=7,
  polygon sprites=6, other sprites=5, RBG0=4, NBG1=3.
- Sprite type (0-15) decides how VDP2 decodes priority/color-calc bits from
  each VDP1 framebuffer pixel — a day-one decision once backgrounds exist,
  as is color-RAM mode (mode 0 required for gradation effects).
- Free effects available over the VDP1 layer with zero VDP1 cost: full-screen
  fades via VDP2 color offset (`SPRON`), letterboxing via VDP2 rectangular
  windows clipping `scnSPR`, hardware shadow via MSB shadow sprites,
  selective half-transparency gated by sprite priority group.
- VDP2 VRAM cycle patterns are a schedulability constraint (auto-assignment
  can fail; rotation and vertical-cell-scroll data cannot share a bank);
  bitmap modes need 0x20000-boundary VRAM alignment.
- RBG0 rotation parameters are fed from the same matrix pipeline
  (`slScrMatConv`) with the 0xE0-byte parameter table in VDP2 VRAM — the
  intended path for a matrix-driven skybox at no HWRAM cost.
- VDP2 registers are write-only; SGL keeps a readable shadow in the system
  area — useful debugging convention.

### Miscellaneous hardware facts worth keeping

- VDP1 scale-1.0 draws one pixel larger than the source art; official
  workaround is scale 0.99999 (FAQ §2-3).
- Texture lighting is possible only in RGB color mode via the Gouraud table;
  palette-mode textures cannot be light-modulated (FAQ §3-4(d)).
- Software preclipping of distorted sprites (reorienting vertex 0 on-screen
  by vertex rotation/flip) measurably improves VDP1 draw efficiency for
  offscreen-extending quads (`SGL020A.TXT` v2.0).
- Official DMA channel etiquette: SCU ch0 free for the app, ch1
  sprite/polygon, ch2 PCM; CPU-DMAC ch0 app/`slDMACopy`, ch1 reserved
  (vblank scroll transfer). Cache must be explicitly purged around DMA into
  cached address space (`slCashPurge`).
- SGL default capacity: 2,500 vertices / 1,786 polygons per frame
  (`WORKAREA.TXT`) — calibration context for scaling the port's current
  64-vertex/192-triangle first increment.
- Per-entry costs: 36-byte staging records per bank, 16-byte transformed
  vertex records (`Pbuffer`), 72 bytes of master/slave command traffic per
  `slPutPolygon` call. Official docs endorse buffer aliasing (`Pbuffer` may
  be overlapped when unused).
- `MEMORY.TXT` has internal inconsistencies between its prose map, ASCII
  diagram, and sample code (different SGL revisions); treat all default
  addresses as illustrative, not ABI.

## Cross-reference: three-way prior-art picture

With this study, the reference landscape for the VDP1 pipeline is:

| Question | SlaveDriver (GPL, source) | Sonic Z-Treme (GPL, source) | SGL (proprietary, docs only) |
|---|---|---|---|
| Depth sort | Offline portal/BSP topological sort — not portable to live decode | Delegated to SGL binary — nothing visible | 128-slot + nest lazy radix, DMA-gather emission — documented, reimplementable |
| Near plane | Sutherland-Hodgman clip w/ interpolation (portable source) | Delegated to SGL — invisible | Clamp-at-threshold policy (documented) |
| CPU/VDP1 overlap | Double-banked VRAM command regions + deferred wait (portable source) | Single `slSynch` passthrough | Double-banked *staging* + vblank-domain transfer + drop-don't-stall (documented) |
| Matrix stack | Proprietary SGL `MTH_*` — invisible | Proprietary SGL `sl*` — invisible | 3x4 affine, 20 levels, copy-free push variants (documented; port's 4x4 is deliberate) |
| Slave SH-2 | Split of already-resolved draw list, VDP1 jump-chaining (portable source) | None visible | Adaptive 3-state work stealing, FRT wake, ring mailbox (documented) |

SGL documentation turns out to be the missing implementation-level reference
that both GPL engines silently delegated to.
