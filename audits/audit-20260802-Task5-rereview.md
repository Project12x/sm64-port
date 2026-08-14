# Peer Audit — 2026-08-02

## Context

- Prior agent: Codex
- Task claimed: Harden the asynchronous SCU/CPU DMA queue with valid fences and pre-enqueue SCU safety validation.
- Files examined: `src/port/saturn/gpl/slavedriver_dma_queue.c`, `src/port/saturn/gpl/slavedriver_dma_queue.h`, `tools/saturn/dma_queue_test.c`, `tools/saturn/host_stubs/yaul.h`, `Makefile.saturn.mk`, Gouraud queue call sites and their HWRAM owner (`src/port/saturn/gfx/saturn_fast3d_vdp1_emit.c`, `src/port/saturn/gfx/saturn_demo_render.c`, `src/port/saturn/gfx/saturn_gouraud_bank.h`, `src/port/saturn/sourceboot/main.c`), and `src/port/saturn/hwtest/main.c`.
- Commands run: `git diff --check 54731bb..0d1a495`; commit/range and call-site inspection with `git show`/`git grep`; `mingw32-make -f Makefile.saturn.mk verify-dma-queue` in the existing `sh2/native-math-purge` worktree (passed). No MSYS, bash, `sh-elf-*`, target build, or Ymir command was run.

## Findings

### Critical

None.

### Important

None.

### Minor

- The host test demonstrates the cached, uncached, and purge LWRAM alias values, but it does not separately exercise invalid enum modes or a `len > UINT32_MAX` SCU request. The implementation has both guards before queue mutation (`slavedriver_dma_queue.c` `_request_valid`), so this is a coverage gap, not a demonstrated behavior defect. Add those two rejection cases to the fixture when the next test-only change is made.

### Discrepancies between summary and code

None. The follow-up claims match commit `0d1a495`.

## What was done well

- `saturn_dma_queue_wait()` now accepts a retired sequence through modular subtraction, locates only currently queued sequences before it can block, and rejects invalid/non-outstanding future requests. Its fixed 16-slot ring keeps every live sequence well inside the `UINT32_MAX / 2` comparison horizon.
- The sequence-wrap fixture is real: the Make target seeds at `UINT32_MAX - 1`, fills the 15 usable entries through the skipped-zero wrap, asserts the final sequence is 13, and then drains through that fence.
- SCU validation occurs before sequence allocation or FIFO writes. It rejects nulls, zero length, invalid mode, host-representable over-`uint32_t` lengths, and either endpoint whose physical 28-bit address range intersects LWRAM. The physical mask recognizes cached, uncached, and purge aliases.
- Existing frame callers queue the HWRAM double-buffered Gouraud staging prefix and still defer `kick` until after `vdp1_sync_wait()`. They retain the same VDP1 ordering and bank-ownership boundary. One-shot hwtest callers retain `transfer_wait` behavior for valid HWRAM/VRAM inputs.
- No whitespace errors were reported by `git diff --check`, and the isolated native host test passed with warnings treated as errors.

## Recommended next actions

1. Accept Task 5 source conditionally: retain the planned serial target build/section and VDP1-partition gate before promotion.
2. Extend `dma_queue_test.c` with invalid-mode and over-`UINT32_MAX` rejection assertions, preserving the existing FIFO-sequence non-mutation proof.
3. Do not treat this audit as validating the active `saturn/bootstrap` checkout: the reviewed commit is in its separate `sh2/native-math-purge` worktree.
