# Post-S1.11 frame budget — where the frame actually goes now

**Date:** 2026-08-23 · **Build:** `id-a87ada8d9e5a4093` (== shipped
`id-2ff8718cc26fed8d` behaviourally; T3 was a measured no-op) · **29 intervals,
caches-off basis.**

Derived from the cadence capture's own per-interval phase records, which carry
VBlank crossings per phase. **This supersedes the S0.1 symbol attribution as the
steering instrument for what to do next**, because S0.1 was taken before the
actor phase moved to the slave and its top entry (`pose_evaluate_staged`) is no
longer master work at all.

Mean frame: **10.483 VBlanks = 5.7237 FPS.**

| phase | mean VB | % of frame | min | max |
|---|---:|---:|---:|---:|
| `construction` | 7.345 | 70.1% | 5 | 9 |
| `master_finalization` | 5.000 | 47.7% | 2 | 6 |
| `simulation` (== `source_tick`) | 4.759 | 45.4% | 4 | 5 |
| `concurrent_phase_allowance` | 4.552 | 43.4% | 2 | 5 |
| `dropped_vblank_credit_delta` | 4.241 | 40.5% | 3 | 5 |
| **`slave_work_overlap_window`** | **2.897** | **27.6%** | 2 | 4 |
| `transport_presentation` | 0.241 | 2.3% | 0 | 1 |

Percentages exceed 100% because phases overlap; `concurrent_phase_allowance`
is the credit for that overlap.

## The two findings that should drive the next task

**1. `master_finalization` is 5.000 VB — nearly half the frame — and the slave
is completely idle for all of it.** Finalization runs *after* the lifecycle's
`slave_retired()` barrier, so by construction no slave work overlaps it. This
is now the single largest block of un-overlapped master-serial time in the
frame. Its contents, in order (`saturn_demo_render.c:5118-5147`): the terrain
emit loop, Mario emit, generic actor emit, then `vdp1_backend_finish` and
`link_depth_bins`. Prior measurement put actors at ~60% of VDP1 commands and
terrain at ~40%, so the actor emit is likely the majority of it.

**2. The slave still idles inside construction.** S1.11 bought overlap, but only
2.897 VB of it against 7.345 VB of construction — the slave finishes the actor
phase and then waits ~4.4 VB for the graph notify. The auxiliary-dispatch
mechanism added in S1.11 can carry a second task; what it cannot do yet is
give the slave something whose dependencies are satisfied that early.

## The fork, and its risk asymmetry

- **Offload actor emit into the finalization window.** Biggest prize (a slice
  of 5.000 VB with a guaranteed-idle peer). **Risky**: terrain, Mario and actor
  emit all append into one VDP1 backend and one Gouraud bank, and correctness
  depends on depth-bin linking. Concurrent append needs per-lane reservations
  and an argument about bin ordering before any code is written. It also touches
  command memory the hardware reads.
- **Give the slave a second construction chunk.** Smaller prize (≤4.4 VB, and
  only the part that is genuinely independent). **Safer**, and the dispatch
  plumbing already exists. Needs a dependency audit of spatial admit / work
  order / visible position set against graph publication.

Recommendation: take the second first. It reuses machinery already validated
this sprint and cannot corrupt VDP1 command memory, so a wrong answer costs a
measurement rather than a corrupted frame.

## Also visible here

`dropped_vblank_credit_delta` is **4.241 per frame** — the simulation is still
being starved of roughly four VBlanks of credit every frame. That remains a
consequence of render cost rather than an independent defect, so every VBlank
returned to the master pays twice: once in cadence, once in simulation credit.
