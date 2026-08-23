# Pre-notification breakdown after S1.11 — terrain admission is 91% of it

**Date:** 2026-08-23 · **Build:** `id-32587bb992b1d320`
(`sourceboot-bob-coins-diag2-v1.json`, `SATURN_DIAGNOSTIC_MODE=2`, offload ON)
· 1,934 windows · all 12 acceptance checks pass, 0 profiler faults.

## Read this caveat before using any number here

`capture_prenotification_profile.py` cross-checks itself against the cadence
rig and **that cross-check failed**: `ticks_per_vblank_agreement = 7.47`
(measured 26,227 vs nominal 3,509). The tool's own documentation says a large
disagreement means one instrument is lying and the numbers must not be used.

So **the absolute VBlank-equivalents in this capture are not trustworthy** and
are not used below. What *is* usable is the **relative share between nodes**:
every node is timed by the same FRT with the same divider, and the profiler's
own accounting is essentially complete — `unattributed_share = 0.00044`. A
systematic error in the tick→VBlank conversion cancels in a ratio.

Supporting the ratios being sound: the diagnostic build's own cadence-rig
figures are close to the product build's (construction 8.21 vs 7.345 VB/frame,
finalization 5.31 vs 5.000, simulation 5.03 vs 4.759), so diagnostic mode is
not grossly distorting the workload — only the conversion constant is suspect.

## The breakdown

| node | share of window | calls/window |
|---|---:|---:|
| **`spatial_admit`** | **91.03%** | 1 |
| `work_order` | 2.84% | 1 |
| `meshlet_depth_admit` | 2.01% | 57 |
| `meshlet_admit` | 0.90% | 6 |
| `actor_closure` | 0.80% | 2 |
| `position_set` | 0.67% | 1 |
| `meshlet_emit` | 0.59% | 6 |
| `meshlet_depth_emit` | 0.54% | 26 |
| `mario_ctx` | 0.17% | 1 |
| `mario_draw_order` | 0.14% | 1 |
| `meshlet_prefix` | 0.13% | 6 |

**`demo_spatial_admit` — scene/BSP admission for terrain — is 91% of the
pre-notification window. Everything else is rounding error.**

Two reasons to believe the 91% rather than dismiss it as instrumentation cost:

1. **`spatial_admit` is a single push/pop per window.** Profiler overhead is
   charged per PUSH/POP, so a one-call node cannot be inflated by it. By
   contrast `meshlet_depth_admit` (57 calls) and `meshlet_depth_emit` (26) are
   the nodes whose shares *are* plausibly inflated — which, usefully, is
   independent corroboration of the T3 negative result: the depth-bounds work
   is small even when the instrument is biased in its favour.
2. **The `SM64_SATURN_ADMIT_DIAG` blocks around it are FRT reads and a publish,
   not extra geometry work** (`saturn_demo_render.c:882,935,958`). Diagnostic
   mode does not make admission itself do more.

Also captured: actors are **53.98%** of emitted VDP1 commands
(`present.commands.actor_share_of_total`), consistent with the earlier ~60%
figure, and `prepare_mario_stage` is only **4.34%** of the window — so Mario
preparation is not a worthwhile offload target.

## What this means for the plan

The only construction item large enough to be worth moving to the slave is
terrain admission. That is precisely the work a VDP2 RBG0 ground plane would
shrink, since clusters that stop being VDP1 geometry stop being admitted at
all.

The two candidate readings, which is why the idle re-measurement matters:

- **If the master is still the limiter**, offloading `spatial_admit` to the
  slave is the largest available win, and the machinery is not wasted by a
  later VDP2 migration — whatever terrain remains still needs admitting, and
  it would still be admitted on the slave. Only the magnitude shrinks.
- **If the slave has become the limiter** (S1.11 moved ~10.5% of master work
  onto it), adding terrain admission on top would make the frame *worse*, and
  the correct next move is VDP2 ground, which removes the work rather than
  relocating it.

`capture_idle_attribution.py` was re-run on the shipped build to settle this;
S0.1's master-0.00%/slave-74.04% split predates the offload and cannot be
reused.

## Hazards found while auditing the offload (for whoever implements it)

- `demo_spatial_admit` uses `&s_terrain_master_commands[0][0]` as its scratch
  (`saturn_demo_render.c:934`). That bank is **cached `.lwram_bss` and
  master-owned**, and is later filled with the master's terrain VDP1 commands.
  A slave writing it would leave stale cache lines that could write back over
  those commands. The slave must use `s_terrain_slave_commands` instead — same
  dimensions, and the slave's own sequential reuse of it for graph-pass
  commands is safe because it is one CPU.
- The output handoff is `s_render_work_order[]` (written directly via
  `admission_output.cluster_indices`) plus `s_render_work_count`, plus the
  `profile->demo_bob_*` counters. The counters are the same shared-accumulator
  problem S1.11 solved; the array wants an uncached staging buffer that the
  master copies into the cached array at join time, so the master's later hot
  loops still read cached memory.
