# Task 5 report — asynchronous SlaveDriver-derived DMA queue

## Result

The GPL-isolated `slavedriver_dma_queue` is now a bounded asynchronous FIFO.
`submit` copies only a descriptor and returns its completion sequence; `kick`
starts no more than the FIFO head; `poll` retires the active entry only after
Yaul reports SCU level 0 idle; `wait(sequence)` advances the FIFO through the
named source-lifetime boundary; and `idle` performs a non-blocking poll.

Follow-up review hardening makes `wait(sequence)` return a status: a completed
sequence is accepted immediately with wrap-safe retired-through arithmetic,
while an invalid or non-outstanding future sequence is rejected instead of
spinning. The fixed FIFO is smaller than the sequence half range, so the
modular comparison remains unambiguous across `UINT32_MAX -> 1`.

Submission now rejects null pointers, zero lengths, invalid modes, overlarge
SCU lengths, and any SCU source or destination range that intersects LWRAM
through cached, uncached, or purge aliases. This enforces Yaul's documented
LWRAM-SCU-DMA hardware lockup condition before a descriptor can mutate the
queue. CPU requests and the existing HWRAM-Gouraud-to-VDP1 caller remain valid.

The frame emitters enqueue the used Gouraud prefix after construction, defer
the start until the one real VDP1 VRAM dependency boundary, and wait only
before the final command list can be made drawable. This keeps final command
order, VDP1 presentation, VDP1/Gouraud VRAM allocation, and all VDP access
master-owned. A bounded full ring retires existing descriptors then retries;
it never overwrites a source bank or writes beyond the fixed 16-entry ring.

`sourceboot/main.c` now has two command staging banks and two HWRAM Gouraud
staging banks. CPU construction may therefore use bank N+1 while VDP1 consumes
the prior frame. VDP1 command/Gouraud VRAM remains single and is touched only
after draw-end, because the current partition architecture has no second VRAM
range to make concurrent overwrites safe.

## TDD and host verification

- RED: after adding `tools/saturn/dma_queue_test.c` and the Qt-MinGW-only
  `verify-dma-queue` target, the test compile failed for the absent sequence,
  `kick`, `poll`, `wait`, `idle`, and capacity API. This was the expected
  missing-feature failure.
- GREEN: `C:\Qt\Tools\mingw1310_64\bin\mingw32-make.exe -f
  Makefile.saturn.mk verify-dma-queue` passed after the implementation. The
  mock SCU verifies submit does not copy payload data, kick starts exactly one
  transfer, poll retires FIFO entries in order, full-ring wrap is bounded and
  reusable, and wait drains all queued work through the requested final
  sequence. The follow-up fixture seeds a near-wrap sequence, verifies repeat
  waits on retired work return immediately, rejects a reordered/non-outstanding
  sequence, and proves null/zero/LWRAM-alias SCU submissions leave FIFO order
  unchanged.
- `git diff --check` passed.

The host target deliberately uses a tiny test-only Yaul declaration stub and
the native Qt MinGW compiler. No MSYS tool, `sh-elf-*` executable, target
build, target disassembly/readelf, or Ymir process was invoked.

## Required target-wave gate (pending)

Before promotion, perform the plan's serial target build and compare
`sh-elf-readelf -S` plus sourceboot VDP1 partition counters against the
predecessor. The candidate must show zero queue overflow and unchanged
command, texture, and Gouraud VRAM ranges. This is a hard pending gate under
the active DLL-popup restriction; host mocks do not prove SH-2 section layout
or live VDP1 ownership.

## Reference/provenance

- **SlaveDriver Engine** — pinned `a8986591557b6e680550d3c23970284d3b38ff8f`,
  GPL-3.0-or-later; inspected `DMA.C`, `DMA.H`, and `LICENSE.txt` from the
  in-tree pinned reference. Reuse mode: **close-port**, retained in
  `src/port/saturn/gpl/` with its GPL-3.0-or-later notice. The adapted pattern
  is its fixed 16-entry ring and explicit start-next/active separation; raw
  SDK register writes and unsafe assert-only overflow handling were not copied.
- **Yaul/libyaul** — pinned `6012f79f237773378c8014e70d8998ad95a38d98`, MIT;
  inspected `libyaul/scu/scu/dma.h`, `libyaul/scu/scu_dma.c`, and `LICENSE`.
  Reuse mode: attributed API integration. `scu_dma_transfer()` is documented
  asynchronous and `scu_dma_level_busy(0)` supplies completion state; no Yaul
  source was copied. `scu/map.h`'s LWRAM address aliases and size were also
  inspected for the submission validation contract.
- **In-tree prior art** — `docs/saturn/SGL_REFERENCE_NOTES.md` and
  `docs/saturn/PROVENANCE.md` were reviewed. Their stated single VDP1 VRAM and
  LWRAM-SCU-DMA prohibition explain the deferred kick boundary and the use of
  HWRAM-only Gouraud staging.
