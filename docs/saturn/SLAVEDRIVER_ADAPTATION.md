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
The queue is intentionally drained synchronously for this bring-up; the
renderer can later submit work across a frame boundary without changing the
source-level contract.

The E2 sourceboot target now links the same adapter too (Gouraud-shading
cycle, 2026-07-24): `saturn_dma_queue_transfer_wait()` uploads each frame's
used-prefix of Gouraud tables from a CPU-staged HWRAM array to VDP1 VRAM,
after the sourceboot Fast3D emit pass fills them
(`src/port/saturn/gfx/saturn_fast3d_vdp1_emit.c`) and before the VDP1 backend
uploads the command list that references them. Same synchronous-drain
bring-up posture as the hardware-test image above.

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
