# E2 sourceboot: corrupted G_MTX pointer traced into gGfxPools — 2026-07-22

Continues the investigation recorded in `HANDOFF_2026-07-22.md`'s "Free-roam
gameplay reached" section, using the `dbg_bad_mtx_w1`/`dbg_bad_mtx_ordinal`/
`dbg_bad_mtx_params` latch fields added in commit `d244950`.

## Capture

Deterministic boot (240 BIOS frames + 25,000 post-poke frames, the confirmed
free-roam depth), `sm64-saturn-sourceboot-e2` cart, fresh `sh-elf-nm` resolve
of `_sourceboot_fast3d` after a forced clean rebuild (`060b987c`).

**Struct-layout correction made before trusting this capture**: the profile
struct's field offsets were previously hand-derived from reading the header
and got a field (`reject_w_nonpositive_overflow_suspect`, offset 168) dropped
entirely, shifting every field after it by 4 bytes and producing plausible-
looking-but-wrong "garbage" values (a power-of-two pointer, an implausible
9-figure ordinal) on the first decode attempt. Caught by noticing those
values didn't fit any sane pattern, then re-derived every offset via a tiny
host program using `offsetof()` against the real header (compiled with the
same flags `verify-runtime-contracts` uses), rather than continuing to
hand-count struct fields. **Lesson for future diagnostic work on this
struct: always ground-truth offsets via `offsetof()`, never hand-derive.**

## Findings, with the corrected offsets

| Field | Value |
| --- | --- |
| `frame_serial` (sanity gate) | 387 |
| `tri_transformed` / `tri_emitted` / `tri_vdp1_emitted` | 1227 / 18 / 0 |
| `rej_w_nonpos` / `rej_backface` / `rej_z_far` | 552 / 261 / 395 |
| `reject_w_nonpositive_overflow_suspect` | 392 (of 552 — still the dominant cause) |
| `dbg_mp_compose_overflowed_ever` | 1 (an MP compose has overflowed) |
| `dbg_bad_mtx_w1` (the corrupted G_MTX command's matrix-data pointer) | `0x0608f970` |
| `dbg_bad_mtx_ordinal` (which matrix command this frame, of `matrix_cmds=20`) | 9 |
| `dbg_bad_mtx_params` | `0x02` (`G_MTX_MODELVIEW\|G_MTX_LOAD\|G_MTX_NOPUSH`) |

**The pointer is not garbage — it falls inside `gGfxPools`.** `nm` resolves
`_gGfxPools` (the actual backing array, plural) at `0x06083980`; `0x0608f970`
is `0xB9F0` (47,600) bytes into that array — comfortably inside a single
51,200-byte bank (`GFX_POOL_SIZE=6400` Gfx entries × 8 bytes). This is a much
more precise finding than the earlier one-time "do the two fronts collide"
snapshot (`d244950`'s commit message, 39.8 KiB gap measured) — that measured
aggregate head/tail positions at one instant; this shows a *specific*
corrupted command's pointer genuinely lands inside pool memory, whatever the
aggregate gap looked like when sampled.

Reading 96 bytes at `0x0608f970` directly (same deterministic boot) shows a
mix, not a clean 64-byte matrix:

- Words interleaved throughout that look like plausible small rotation/
  position floats (e.g. `0.115`, `0.732`, `-0.723`, `-830.96`).
- Word offset 40 (`0xe200001c`) — **the exact same `G_SETOTHERMODE_L`-opcode
  signature already found at the *decoded* corruption site in `eb5bbbf`**,
  now also present in the *raw pool bytes themselves* at this address, not
  just in what a G_MTX command read out of them.
- Four consecutive HWRAM-address-shaped words (`0x0608f9b0`, `0x0608f970` —
  the very address being read — `0x060b9000`, `0x060b9800`), consistent with
  this region holding pointer-bearing data rather than a plain float array.
- A clean tail (`1.0, 0,0,0, -0.0,1.0,0,0`) that does look like the last two
  rows of a legitimate identity-ish matrix.

This pattern (legitimate-looking float data interleaved with opcode bytes and
real pointers) is consistent with: an `Mtx` was legitimately allocated from
`gGfxPools` at some point, but by the time this session's Fast3D frontend
decoded the G_MTX command referencing it, *some* of that memory had already
been overwritten by different content — not the whole 64 bytes, which is why
some words still look plausible.

## Two plausible mechanisms ruled out with direct evidence

1. **Missing per-frame pool reset — ruled out.** Retail's `select_gfx_pool()`
   (`src/game/game_init.c:390-393`, alternates between `gGfxPools[gGlobalTimer
   % ARRAY_COUNT(gGfxPools)]` banks and resets `gDisplayListHead`/
   `gGfxPoolEnd` to that bank's bounds) **is already called every frame** via
   the exact same unmodified call site this port already runs through
   (`game_init.c:789`, in the same block as `level_script_execute()` at
   `:791` and `read_controller_inputs()` at `:790` — all inside the loop
   `sourceboot`'s boot path reaches via `thread5_game_loop`). This is not
   another missing-init-call bug like the five fixed earlier today.
2. **Cross-frame decode lag — ruled out.** This port's `exec_display_list()`
   (`src/port/saturn/runtime/saturn_source_runtime.c:100-111`) calls the
   registered task consumer (the Fast3D frontend) synchronously, immediately,
   with no queueing or deferral. Since `exec_display_list()` itself is called
   from `display_and_vsync()` within the same frame iteration that just built
   the list via `level_script_execute()`, the frontend decodes each frame's
   display list before `select_gfx_pool()` ever re-selects for the *next*
   frame. A stale pointer surviving into a later frame (when the same bank
   is reselected two frames later) is not the mechanism.
3. **`alloc_display_list()` itself checked, not an obvious bug.**
   `src/game/memory.c:750-759` (the non-`USE_SYSTEM_MALLOC` variant, the one
   this build compiles) is a standard bump-allocator: `size = ALIGN8(size);
   if (gGfxPoolEnd - size >= gDisplayListHead) { gGfxPoolEnd -= size; ptr =
   gGfxPoolEnd; }`. Nothing platform-specific here, and no obvious defect
   read from the source alone.

## What remains open

The exact mechanism by which *this frame's own* memory ends up partially
overwritten before the frontend reads it is not yet found. With (1) and (2)
ruled out, the leading remaining hypotheses are: a same-frame allocation-size
miscalculation somewhere in the many `alloc_display_list(sizeof(Mtx))` /
`alloc_display_list(N * sizeof(Gfx))` call sites (a `GBI_FLOATS`-related
`sizeof(Mtx)` mismatch between what code assumes it allocated and what the
Fast3D frontend assumes it's reading is a concrete, checkable next lead,
not yet checked), or a genuine same-frame forward/backward overlap that a
single head/tail position snapshot doesn't catch (would need either a
live single-instruction-step trace bracketing an actual allocation and its
later overwrite, mirroring the technique that found the `animList` NULL
crash, or byte-level pool diffing across several points within one frame).

Not attempted in this pass: no code changes were made. This is a pure
evidence-gathering capture, deliberately not guessing at a fix given how
deep and still-ambiguous the exact mechanism is.
