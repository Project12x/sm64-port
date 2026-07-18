# SlaveDriver adaptation boundary

SlaveDriver is a valid implementation reference for this port. The pinned
source revision is `a8986591557b6e680550d3c23970284d3b38ff8f`, licensed
GPL-3.0-or-later. The files reviewed were `DMA.C`, `DMA.H`, `SCL_FUNC.C`,
`INITMAIN.C`, `MEMCPY.S`, `LINK.S`, `README.md`, and `LICENSE.txt`.

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

The hardware-test image still uses direct Yaul calls so its DMA measurements
remain a small, auditable baseline. The next renderer/DMA phase should add the
isolated adapter and replace the two display-upload call sites with it. This
keeps the current telemetry comparable while making the reuse decision
explicit and reversible.
