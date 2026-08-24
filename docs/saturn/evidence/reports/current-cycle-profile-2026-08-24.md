# The current cycle profile — and why every lever kept returning 4%

**Date:** 2026-08-24 · Captured on the shipped baseline
`e2-bob-identity-id-684490199c21c5f8` (6.1922 FPS, 9.690 VB/frame) at gameplay
tick 150. Long run: 6 phases x 900 bursts, **1,182,549 working cycles sampled
across 532 symbols**.

## Why this capture was needed

Every share quoted in sprint planning traced back to
`sprint2-t2_14-softfloat-profile-a61d5203.json`, whose own summary says
**11.2069 VB/frame — 5.354 FPS**. Since then the dynamic actor closure, S1.1,
T5, S1.11 and S1.12 all landed, including *both* slave offloads. **The build
being optimised had never been profiled.** Rankings such as "soft-float 14%",
"geo walk 15.8%", "actor pipeline 17%" described a machine that no longer
existed.

A first short re-capture (default sampling, ~81k cycles, ~2% of one frame)
produced a *third* different ranking — memory movement at 17.8%, soft-float at
2.49% — which the long run does not support. **That short sample was noise and
is retracted.** Sample size is quoted below for this reason.

## Measured, current

Idle is **51.25%** of pooled master+slave cycles: `___slave_polling_entry`
**49.28%**, `_sm64_saturn_source_runtime_wait_vblank` **1.97%**. The master is
~96% busy; the slave is essentially idle. This agrees with a separate
`capture_idle_attribution.py` run on the same build, which reported
**master idle 0.00%, slave idle 100.00%**, joint state
`work|idle:notification-wait` at 100% of traced cycles.

Non-idle work, 2,113,731 cyc/frame normalised to one CPU-frame budget:

| group | % of non-idle | cyc/frame | symbols |
|---|---:|---:|---:|
| everything else | **31.60%** | 667,880 | 360 |
| terrain / admission / render | 24.04% | 508,145 | 22 |
| actor pipeline | 17.00% | 359,232 | 68 |
| N64 geo walk + matrix stack | 10.06% | 212,578 | 53 |
| soft-float | 9.46% | 200,032 | 14 |
| memcpy / memset / movmem | 7.85% | 165,863 | 13 |

Top single symbols:

| symbol | % of non-idle | cyc/frame |
|---|---:|---:|
| `_demo_render_finalize` | 9.49% | 200,542 |
| `_sm64_saturn_ztreme_frustum_aabb` | 4.77% | 100,831 |
| `_demo_terrain_queue_world_lower` | 4.18% | 88,351 |
| `_sm64_saturn_actor_pose_evaluate_staged` | 4.00% | 84,501 |
| `___addsf3` | 3.65% | 77,049 |
| `_memcpy` | 3.42% | 72,264 |
| `_sm64_saturn_matrix_mul` | 3.39% | 71,690 |
| `_demo_render_prepare_publish` | 3.39% | 71,651 |
| `_memset` | 3.16% | 66,732 |
| `_sm64_saturn_ir_transform_one` | 3.02% | 63,910 |
| `_sm64_saturn_ir_project_view` | 2.09% | 44,169 |
| `_sqrtf` | 1.91% | 40,326 |

## The finding: the frame is diffuse, and half the machine is idle

**The largest single symbol is 9.49%, and a 360-symbol tail carries 31.60%.**
That is the structural explanation for why every investigation this sprint —
quad pairing 5.9%, terrain decimation <=4%, SCU DSP ~5.5%, admission hierarchy
already spent — returned a low single-digit number. There is no single hot spot
to remove. Micro-optimisation cannot compound out of a distribution this flat.

**The one non-diffuse fact is that `___slave_polling_entry` is 49.28% of the
machine's cycles.** Half the available compute is unused while the master runs
at ~96% occupancy. That is not a percentage lever; it is a factor-of-two lever,
and it is the only one of that size on the board.

## Corrections this capture forces

1. **`sm64_saturn_ir_transform_one` is NOT absent from the profile.** It is
   **3.02%**, and with `ir_project_view` (2.09%) the transform kernel is
   **5.11%**. The SCU DSP pricing
   (`scu-dsp-priced-and-declined.md`) argued in part from its absence — that
   argument came from the stale profile and is **withdrawn**. With
   `matrix_mul` (3.39%) and `refresh_float_mirror` (1.62%) the DSP-shaped
   arithmetic is roughly **10%**, not 5.5%.
   The other three objections stand unchanged and still carry the decision: the
   DSP cannot divide (and the kernel is built around overlapping the SH-2
   `DIVU`), 1 KB of data RAM forces 50-110 DMA round trips per frame, and no
   Saturn engine in `work/upstream/` uses it. **Verdict unchanged, reasoning
   corrected** — and it stays behind the slave, which is both larger and easier.
2. **The fixed-point purge is not done.** Soft-float is **9.46%**
   (`float_share_of_working` 11.65%), not the 2.49% the short sample suggested.
   `___addsf3` alone is 3.65%.
3. **Memory movement is ~7.85%**, not the 17.8% the short sample suggested.

## Reproducing

```bash
python tools/saturn/capture_softfloat_profile.py --ymir "<build-agent2 ymir-headless>" --ipl "<BIOS>" --game "$B/sm64-saturn-sourceboot-e2.cue" --elf "$B/obj/sm64-saturn-sourceboot-e2.elf" --nm /d/Code/RetroDev/sm64-saturn-port/work/yaul-install/bin/sh-elf-nm.exe --startup-vblanks 4096 --warmup-ticks 150 --vblanks-per-frame 9.690 --phases 6 --phase-vblanks 20 --bursts 900 --burst-steps 250 --output out.json
```

`--warmup-ticks 0` samples during CD streaming (`_cd_block_cmd_execute` at
49.6%) and is useless for gameplay profiling. Use tick 150. The default
sampling density is too low to rank groups; use the burst settings above.
