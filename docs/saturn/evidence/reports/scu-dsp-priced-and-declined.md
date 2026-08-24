# The SCU DSP, priced — declined, and what it would actually be for

**Date:** 2026-08-24 · Priced from the existing cycle profile, our own vendored
SDK headers, and the two Saturn engines cloned in `work/upstream/`. No build, no
capture: the disqualifying facts are all static.

## Why it was asked

Burning Rangers and its peers ran real 3D at speed on this hardware, and Sega's
own 1995 guidance was **SCU DSP for the matrix work, SH-2 for polygon
processing, in parallel**. The DSP is 14.32 MHz fixed-point, ~86 MIPS, six
parallel instructions per cycle. We use it for nothing. The hypothesis was that
batch vertex transform — its textbook workload — is the lever that adds a
processor rather than shaving a percentage.

## Verdict: no. The work it would take is ~5.5% of ours, and the fit is hostile.

### 1. The transform kernel is not even in the profile

`sm64_saturn_ir_transform_one`, `sm64_saturn_ir_transform_batch` and
`sm64_saturn_ir_project_view` are **absent** from all 146 profiled symbols —
inlined, or below the sampling floor. The thing the DSP exists for is not a hot
spot here.

### 2. Everything DSP-shaped, totalled

| symbol | % of non-idle work | cyc/frame | calls/frame |
|---|---:|---:|---:|
| `sm64_saturn_matrix_mul` | 3.02% | 40,349 | 43.5 |
| `saturn_mtxq_refresh_float_mirror` | 2.19% | 29,355 | 43.5 |
| `sm64_saturn_float_to_q16` | 1.06% | 14,193 | 298.1 |
| `mtxq_rotate_zxy` + `scale_vec3f` + `billboard` | 1.40% | 18,721 | 86.9 |

The float mirror **dies with S1.10** and cannot be counted twice, leaving
**~5.5% of working cycles (73,263 cyc/frame)** as the genuine prize — an upper
bound of roughly **+0.2 FPS** assuming zero transfer overhead and perfect
overlap.

`demo_projected_read` (1,173.9 calls/frame) and `demo_view_read` (614.9) look
like transform work by name but are **reads of already-computed results** —
memory-bound lookups the DSP cannot help with.

### 3. Four structural disqualifiers

1. **Data RAM is 1 KB.** From our own vendored header
   (`third_party/libyaul/libyaul/scu/scu/dsp.h`): `DSP_PROGRAM_SIZE 0x0400`
   (256 instructions) and four RAM pages of `DSP_RAM_PAGE_SIZE 0x0100` = 64
   words each. At 3 words per vertex that is **~21 vertices per round trip**,
   i.e. **50–110 DMA round trips per frame** for our ~1,200–2,300 vertices.
2. **The DSP cannot divide, and our kernel is built around a divide.**
   `saturn_ir_transform.c:80-93` is a deliberate close-port of **libmic3d**'s
   scheduling pattern: transform Z first, launch the SH-2 hardware `DIVU`, do
   the independent X/Y camera dot products *while the divider runs*, consume the
   quotient once. Moving the 3x3 product to the DSP destroys that overlap and
   leaves the perspective divide on the SH-2 anyway — now with DMA orchestration
   added on top.
3. **SCU bus contention.** The SCU DMA already carries our VDP1 command
   transfers; `vdp1_bank_late_dma` runs ~1.02/frame today. DSP transfers compete
   for the same path.
4. **Prior art is zero.** Neither `slavedriver-engine` nor `sonic-z-treme`
   contains the string `dsp` **anywhere** in its tree. Neither does libmic3d,
   which our transform kernel is ported from. Three independent Saturn 3D
   codebases declined it.

## What the DSP *is* for

It is not useless — it is a fire-and-forget coprocessor with a specific shape.
Work suits it when **all** of these hold:

- fixed-point, multiply-accumulate heavy
- **no division**
- working set fits 64 words per bank
- data is **contiguous** — its DMA is a block mover, not a gatherer
- latency-tolerant: dispatch once, collect later, never stall on it
- items are independent — no sequential dependency chain

Judged against that, in this codebase:

| Candidate | Fit |
|---|---|
| Batch vertex transform | **No** — needs division, on the critical path, 50–110 round trips |
| Matrix stack composition | **No** — parent x child is a sequential dependency chain; nothing to batch |
| `actor_meshlet_live_depth_bounds` (7.28%) | **Partial** — pure MAC + min/max with no division, but it *gathers* through `actor_position_ref` (itself 1.57%), and gather fights linear DMA |
| Gouraud / vertex lighting | **Plausible** — dot products, no division, contiguous, latency-tolerant |
| Animation blending / keyframe interpolation | **Plausible** — MAC over contiguous arrays, no division, can run a phase ahead |
| Collision broadphase (AABB overlap sets) | **Plausible** — compares over contiguous bounds, latency-tolerant |

## The reason it is last in the queue, not first

**We have not used the second CPU yet.** `___slave_polling_entry` was **43.2% of
all cycles** in this profile, and post-S1.12 the slave window is still only
2.793 of 9.690 VB — **29% of the frame** — while the master works alone for 70%
of it.

The slave is a full 28.6 MHz SH-2 with cache, a real ISA and unrestricted memory
access. It is an order of magnitude more capable than a 256-instruction DSP with
1 KB of RAM and no divider, and vastly cheaper to program. Reaching for the
third processor while the second one idles is the wrong order.

**Revisit the DSP only after master/slave is balanced**, and only for a workload
matching the shape above — at which point the honest candidates are Gouraud
shading and animation blending, not vertex transform.

## Sources

Hardware characteristics cross-checked against
[Sega Saturn Architecture (Copetti)](https://www.copetti.org/writings/consoles/sega-saturn/),
[SCU DSP for matrix transformation (SegaXtreme)](https://segaxtreme.net/threads/scu-dsp-for-matrix-transformation.24242/)
and [Saturn SCU DSP Notes (SegaXtreme)](https://segaxtreme.net/threads/saturn-scu-dsp-notes.25289/).
All limits quoted above are from our own vendored `scu/dsp.h`, not from those
pages.
