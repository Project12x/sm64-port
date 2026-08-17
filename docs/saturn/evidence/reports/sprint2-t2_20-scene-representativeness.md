# Sprint 2 Task T2.20 — what the capture windows are looking at

- Date: 2026-08-17. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `442ad4da` (T2.21 landed `c9adb159`..`930e37af`
  concurrently; nothing here touches its files and all captures are against
  the sealed `id-c0352f297034f653` artifact, which predates it).
- Task: establish empirically what scene each capture window observes, then
  re-measure the sprint's load-bearing ratios against a representative
  gameplay scene and audit which conclusions are scene-dependent.
- Target: the current baseline `id-c0352f297034f653`
  (`releases/2026-08-16_t2_17-product/id-c0352f297034f653/obj/...e2.elf`,
  SHA-256 `2933c5d5…fecd`). On-target identity **MATCH** on all seven captures.
- **Diagnosis only.** No product build, no product source change, no emulator
  GUI. Seven headless Ymir boots against the already-sealed ELF. Two capture
  tools converted from wall-clock to route-tick warm-up (T2.19d's task chip);
  one new read-only capture tool.
- New instrument: `tools/saturn/capture_route_timeline.py`.
- Evidence: `sprint2-t2_20-route-timeline-c0352f29.json`,
  `sprint2-t2_20-softfloat-profile-tick{30,180,180-gap7,300}-c0352f29.json`,
  `sprint2-t2_20-terrain-lod-census-tick{30,180}-c0352f29.json`,
  `sprint2-t2_20-idle-attribution-tick{30,180}-c0352f29.json`.

---

## 0. The one-paragraph version

**Mario is rendered from route tick 7 onward, so no capture in this sprint
was ever looking at a Mario-less scene - that worry is dead.** What every
narrow-window capture *was* looking at is a scene in which **Mario is standing
still**: the compiled route's first sample is `{ 120 ticks, stick 0, 0,
buttons 0 }`, so route ticks 1-120 supply neutral input and
`gMarioState->action` holds `ACT_IDLE` with the position bit-frozen at
`(-6558, -0, 6464)` from tick 4 - the first tick at which the simulation is
running - through tick 120, 117 consecutive ticks. Mario enters `ACT_WALKING`
at **tick 121**. Ticks 25, 30 and 31 - every warm-up point this sprint used -
sit inside that stationary block, and so does the 30-presentation cadence
window (ticks 0 to 31). **Three of the four things we then re-measured turned
out to be scene-independent, and the fourth moved less than feared.** Frame
cost: 8.9727 VB/frame stationary against 9.0429 running, **0.8% apart**,
independently corroborating the owner's report that the FPS is real. Master
idle: **0.000000 at both ticks** - T2.17's collapse holds in gameplay. The
cycle ranking, measured with a *contiguous every-instruction* trace rather
than a burst sampler, is **almost frozen**: identical top five in identical
order on the master, identical top four on the slave, no symbol moving more
than ~1 pp or 4 rank places. Only the VDP1 command mix really moves - terrain
40.0% to 44.8%, actors 60.6% to 55.9%. **The headline for the owner's "are we
leaving optimisations on the table" question is therefore not a different
ranking; it is that our profiling instrument cannot see one.**
`capture_softfloat_profile.py` at 50,000 samples mis-states symbol shares by
up to **+/-2.6 pp** against the contiguous ground truth on the same build at
the same tick - larger than nearly every effect the sprint has used it to
size, and large enough that it reported the soft-float trend with the wrong
sign. The one optimisation genuinely surfaced by this task is
**`_demo_render_prepare_publish`**, which no sprint task has examined and which
both instruments agree rises to rank 6 of master cycles in gameplay.
Recommended standard warm-up: **`--warmup-ticks 150`**, reasoning in section 8.

---

## 1. What the route is — the structural fact everything rests on

The shipped build is `camera_route: 0`, `route_replay_mode: 1`,
`live_input_mode: 1`, `bootstrap_ticks: 600`
(`releases/2026-08-16_t2_17-product/id-c0352f297034f653/saturn-release-manifest-v1.json`).
`main.c:2159-2166` therefore selects `sm64_saturn_sourceboot_bob_parity_v1`,
whose table is `src/port/saturn/sourceboot/source_demo_data.c:24-42`:

Tick ranges below are in `sState.input_replay_ticks`, which counts *applied*
ticks and so is 1-based (the first application leaves the counter reading 1).

| Route sample | Ticks | Applied at ticks | `stick_x` | `stick_y` | Buttons | Comment in source |
| ---: | ---: | :--- | ---: | ---: | :--- | :--- |
| 0 | 120 | **1–120** | 0 | 0 | — | *neutral startup* |
| 1 | 120 | **121–240** | 64 | 0 | — | *move* |
| 2 | 24 | 241–264 | 64 | 0 | `A` | *running jump* |
| 3 | 96 | 265–360 | 64 | 20 | — | *arc* |
| 4 | 72 | 361–432 | 0 | 0 | `L_C` | *normal camera input* |
| 5 | 72 | 433–504 | −48 | 48 | — | *return leg* |
| 6 | 48 | 505–552 | −48 | 48 | `A` | *second jump* |
| 7 | 48 | 553–600 | 0 | −64 | `R_C` | *final camera variation* |
| 8–17 | 1,400 | never | — | — | — | **cut off**: `bootstrap_ticks = 600` lands exactly on the end of sample 7 |

Two consequences are settled before any emulator runs:

1. **The route supplies no movement input at all for its first 120 applied
   ticks.**
   This is a compile-time property of the shipped image, not a measurement.
   The alternate route (`bob_default_camera_v1`,
   `source_camera_acceptance_route.c:7-11`) opens with the *same* 120 neutral
   ticks and then never moves Mario at all — so the finding is not sensitive
   to which route a build selects.
2. **The usable route is applied ticks 1–600.** Past `bootstrap_ticks` the live-input
   image stops applying samples (`saturn_source_runtime.c:117-127`) and the
   game free-runs on neutral input, which is a *third* stationary regime.

---

## 2. The route timeline, measured

`tools/saturn/capture_route_timeline.py`, one headless boot, 615 samples,
71.9 s wall. Coarse 60-VBlank stride through the boot stretch, 3-VBlank stride
afterwards — a frame is ~9 VBlanks, so every route tick is observed at least
twice and none can be skipped. Every column is a `mem.peek` of a global that
already exists in the shipped ELF.

`vb` below counts VBlanks **after the ELF identity match**. The capture
harness's BIOS handoff costs a further fixed 1,500 VBlanks
(`capture_sourceboot_boot_trace.py:429-444`: 120 + 30 + 1200 + 5×30), and the
identity match itself took **681** VBlanks (`startup_identity_attempts: 681`)
— so `--startup-vblanks 4096` is a *timeout bound on a load event*, never a
duration, and the real load costs 681.

### 2.1 The boundaries, with the evidence for each

| Boundary | Route tick | `vb` | Wall clock from power-on | Evidence |
| :--- | ---: | ---: | ---: | :--- |
| ELF identity match (CD load complete) | — | 0 | ~36.4 s | `startup_identity_attempts: 681`, after 1,500 fixed handoff VBlanks |
| Simulation starts ticking; first frame presented | **1** | ≈1,524 | ~61.7 s | `gGlobalTimer` 0 at `vb` 1,500 and 5 at `vb` 1,560; bracketed, then back-extrapolated at 9 VB/tick |
| **First frame containing Mario** | **7** | **1,587** | **~62.8 s** | `s_actor_command_count` 0 → **327**, `demo_actor_primitives_emitted` 0 → **315** |
| Level-load transient ends (terrain command count stops falling) | **~20** | 1,701 | ~64.7 s | terrain/frame 242 (t7) → 208 (t15) → 187 (t20), flat ±3 thereafter |
| Scene settled and **stationary** | **20–120** | 1,701–2,595 | 64.7–79.6 s | `ACT_IDLE` (`0x0C400201`), position bit-frozen at `(-6558.0, -0.0, 6464.0)`, `forwardVel` 0.00, terrain 187–190/frame |
| **First non-neutral route input applied** | **121** | 2,604 | ~79.7 s | `sState.last_applied_stick_x` 0 → **64** |
| **Mario first moves** | **121** | 2,610 | **~79.8 s** | action `0x0C400201` (`ACT_IDLE`) → `0x04000440` (`ACT_WALKING`), `forwardVel` 0.00 → **8.91**, `pos.x` −6558.00 → −6549.09, all within tick 121 |
| Mario reaches full walk speed | **150** | 2,868 | ~84.2 s | `forwardVel` saturates at **26.4833** and holds through tick 200 |
| **Representative sustained gameplay** | **≥150** | ≥2,868 | ≥84.2 s | steady locomotion, camera tracking, terrain 206→220 commands/frame |
| Running jump | 241–264 | — | — | route sample 2 (`A` held) |
| Route exhausted; neutral input forever | ≥600 | — | — | `bootstrap_ticks: 600` |

Both `ACT_IDLE` and `ACT_WALKING` are named engine constants
(`include/sm64.h:175,214`), so the movement boundary is read off a source
symbol rather than inferred from a position epsilon. Within tick 121 the input
is applied before `mario_update` runs, so the same tick shows neutral input at
its first sample and `ACT_WALKING` at its last.

Wall-clock column is `(1500 + 681 + vb) / 60` and includes the capture
harness's fixed 1,500-VBlank BIOS handoff. **62.8 s to Mario on screen and
79.8 s to Mario moving is a direct match for the owner's "almost a minute from
boot before mario is on screen and longer until he is moving."** The
observation was correct and this table is its mechanism. Measured from disc
start rather than from emulator power-on the figures are **37.8 s** and
**54.8 s**.

### 2.2 Where the sprint's warm-up points fall

| Warm-up used | Route tick reached | What is on screen | Verdict |
| :--- | ---: | :--- | :--- |
| `--warmup-vblanks 1800` on `id-a61d5203793986e7` | **25** | Mario rendered, `ACT_IDLE`, stationary | inside the neutral block |
| `--warmup-vblanks 1800` on `id-c0352f297034f653` | **31** | same | inside the neutral block |
| `--warmup-ticks 30` (T2.19d default) | **30** | same | inside the neutral block |
| `--phase-vblanks 1800 --phases 1` (T2.13, T2.14) | ~25–31 | same | inside the neutral block |
| `--startup-vblanks 4096` | **not a position** | — | timeout bound on the identity poll; the real match is at 681 VBlanks |
| cadence window, 30 presentation events | ticks **0 → 31** | boot + level-load transient + stationary block | inside the neutral block |

**Every attribution warm-up in this sprint lands between tick 25 and tick 31 —
5 to 11 ticks after the level-load transient ends, and 90 to 96 ticks before
Mario moves.** Nothing was ever looking at Mario in motion.

### 2.3 Sampling *span* matters as much as the warm-up point

A capture's warm-up is where it starts; its span is what it averages. The two
give very different answers here, and this is why some sprint numbers survive
better than others:

| Capture | Span mechanism | Ticks covered | Regime |
| :--- | :--- | :--- | :--- |
| `capture_idle_attribution` (1 window × 6M steps) | one contiguous ~16–20 VBlank trace | **~1.8 ticks** from the warm-up point (T2.16: 25 → 27; T2.17: 31 → 33) | wholly stationary |
| `capture_softfloat_profile` (200 bursts, gap 1) | 200 VBlanks | **30 → 52** (measured) | wholly stationary |
| `capture_route_counters` (40 × 11 VB) | 440 VBlanks | 25 → 74 | wholly stationary |
| `capture_route_counters` (120 × 11 VB, T2.14) | 1,320 VBlanks | 25 → 144 | ~79% stationary, ~21% moving |
| `capture_terrain_lod_census` (120 × 11 VB, T2.19c) | 1,320 VBlanks | **31 → 177** (30 → 176 measured here) | ~61% stationary, ~39% moving |
| `capture_sprint2_peaks` (24,000 VBlanks requested) | whole route **plus** a long post-route tail | 26,181 emulated frames, 89 samples | **62.2% of intervals stationary** — see below |

The peaks family deserves its own number because it has the opposite bias to
everything else. Re-reading `sprint2-t2_8-vdp1-fence-attribution.json`
directly: 75 valid Mario samples over 26,181 emulated frames; Mario's position
changes in **28** of 74 sample intervals and is unchanged in **46**, and his
position stops changing at all at emulated frame **19,281** — so the final
**26%** of the sweep (6,900 frames) is a completely static post-route tail,
with `action` back to `ACT_IDLE` at `(-1178, 256, 5491)`. Anything averaged
over that sweep is pulled toward a stationary scene from the other end.

So the **narrow** instruments (idle attribution, cycle profile) are the ones
whose windows are purely static, and they are exactly the instruments whose
output is a ranking. That is the shape of the problem.

---

## 3. The hot-spot ranking, at tick 30 and in real gameplay

This is the section the owner asked for: *are we optimising against the wrong
ranking?* The answer is **no - but only because the instrument we would have
used to find out cannot resolve the question.** Both halves matter.

### 3.1 Two instruments, and why the second one settles it

| Instrument | What it does | Bias |
| :--- | :--- | :--- |
| `capture_softfloat_profile.py` | 50,000 `exec.stepi` samples in 200 bursts of 250, each burst starting at a VBlank boundary | statistical, and burst starts are raster-phase-locked (T2.16 section 1.1) |
| `capture_idle_attribution.py` | **6,000,000 contiguous master instructions** - every instruction in a ~16.3-VBlank window, cycle-attributed | none for the master; it is a census, not a sample |

The idle-attribution tool has been used only for idle accounting. It also
emits `master_symbols` and `slave_symbols`, which on the master is a **complete
cycle profile of 1.8 consecutive frames with no sampling at all**. Nobody has
read it that way. It is the ground truth this section is built on.

### 3.2 The master ranking, measured contiguously

`share_of_master` is the share of all master cycles; master idle is 0.000000 on
this build (section 4.3), so it is also the working share.

| # @ t30 | Symbol | t30 % | t180 % | delta | # @ t180 |
| ---: | :--- | ---: | ---: | ---: | ---: |
| 1 | `_demo_render_finalize` | 10.834 | 10.934 | +0.100 | **1** |
| 2 | `___mulsf3` | 8.332 | 7.613 | -0.718 | **2** |
| 3 | `_sm64_saturn_ztreme_frustum_aabb` | 6.265 | 7.275 | **+1.010** | **3** |
| 4 | `___addsf3` | 6.085 | 5.531 | -0.554 | **4** |
| 5 | `_sm64_saturn_matrix_mul.isra.0` | 5.895 | 5.130 | -0.765 | **5** |
| 6 | `_saturn_geo_enter_object` | 3.666 | 3.658 | -0.008 | 7 |
| 7 | `___divsf3` | 3.633 | 3.132 | -0.501 | 9 |
| 8 | `___subsf3` | 3.403 | 3.158 | -0.244 | 8 |
| 9 | `_saturn_mtxq_refresh_float_mirror` | 3.361 | 2.722 | -0.639 | 11 |
| 10 | `_demo_render_prepare_publish` | 3.036 | 3.914 | **+0.878** | **6** |
| 11 | `_memcpy` | 2.839 | 3.102 | +0.263 | 10 |
| 13 | `_sm64_saturn_terrain_depth_bins_scatter` | 1.798 | 2.091 | +0.293 | 12 |
| 15 | `_sm64_saturn_terrain_depth_bins_digit` | 1.722 | 2.021 | +0.299 | 14 |
| 16 | `_support_radius_q16` | 1.718 | 1.982 | +0.264 | 15 |

**The top five are identical, in identical order, at both ticks.** The largest
share move in the whole table is **+1.01 pp**; the largest rank move is **four
places**.

The slave is the same story (`slave_render = 1`, so it carries terrain
classification):

| Symbol | t30 % of slave | t180 % of slave | rank |
| :--- | ---: | ---: | :--- |
| `_demo_terrain_queue_world_lower` | 7.486 | 8.305 | 1 -> 1 |
| `_sm64_saturn_ir_transform_one` | 3.679 | 3.348 | 2 -> 2 |
| `_demo_actor_lower_compat_wrapper` | 2.453 | 2.440 | 3 -> 3 |
| `_demo_actor_queue_vertex_lookup` | 2.408 | 2.408 | 4 -> 4 |
| `_sm64_saturn_ir_project_view` | 2.365 | 2.166 | 5 -> 7 |
| `_demo_prepare_position_owners` | 2.124 | 1.525 | 6 -> 9 |
| `_demo_terrain_queue_world_admit` | 1.626 | 0.950 | 9 -> 11 |

**Conclusion: the hot-spot ranking is scene-independent on this route.** The
work we have been optimising against is the work that runs in gameplay. Nothing
large is hiding at tick 180 that was absent at tick 30.

### 3.3 The two symbols that do move, and the one worth acting on

1. **`_demo_render_prepare_publish` - 3.04% to 3.91% of master cycles, rank 10
   to 6.** The burst profiler independently puts it at 1.48% to 3.21% (rank 22
   to 8) and all three gameplay windows agree on the direction. **No sprint
   task has examined this symbol.** It is the clearest answer to "an
   optimisation left on the table without knowing", and it is a rank-6 master
   cost in gameplay.
2. **`_sm64_saturn_ztreme_frustum_aabb` - 6.27% to 7.28%, already rank 3.** The
   burst profiler agrees (+1.03 pp minimum across three gameplay windows).
   Culling cost scales with camera motion, which a static window understates by
   about 16%.

Everything else in the master table is inside 1 pp.

### 3.4 The instrument finding - this is the important part

Comparing the burst profiler against the contiguous census, **on the same
build, at the same tick, over overlapping windows**:

| Symbol | t30 census* | t30 burst | error | t180 census* | t180 burst | error |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| `_demo_render_finalize` | 8.195 | 8.546 | +0.35 | 8.309 | 8.839 | +0.53 |
| `___mulsf3` | 6.302 | 3.827 | **-2.48** | 5.785 | 8.421 | **+2.64** |
| `_sm64_saturn_ztreme_frustum_aabb` | 4.739 | 3.537 | -1.20 | 5.529 | 5.160 | -0.37 |
| `___addsf3` | 4.603 | 3.303 | -1.30 | 4.203 | 4.016 | -0.19 |
| `_saturn_geo_enter_object` | 2.773 | 3.852 | +1.08 | 2.780 | 1.385 | **-1.40** |
| `___subsf3` | 2.574 | 3.166 | +0.59 | 2.400 | 0.969 | **-1.43** |

\* census master share rescaled to share of *combined* working cycles (the
master is 75.6% / 76.0% of combined working cycles at t30 / t180), so the two
columns are directly comparable.

**`capture_softfloat_profile.py` at 50,000 samples is wrong by up to +/-2.6 pp,
and `___mulsf3` is its worst case in both directions.** Its apparent "+4.6 pp,
rank 5 to 2" swing for `___mulsf3` is **entirely** the sum of its own two
errors (-2.48 at t30, +2.64 at t180); the census value is ~6.3% falling to
~5.8%, a small *decrease*. The same applies to `_saturn_geo_enter_object` and
`___subsf3`, which the burst profiler shows collapsing and the census shows
flat.

The aggregate is affected too. Soft-float share of combined working cycles:

| Instrument | tick 30 | tick 180 | trend |
| :--- | ---: | ---: | :--- |
| burst profiler | 17.47% | 19.53-20.15% | **rising** |
| **contiguous census** | **20.63%** | **18.91%** | **falling** |

**The burst profiler reported the soft-float trend with the wrong sign and
under-stated its level by ~3 pp.** All soft-float cycles run on the master and
none on the slave, so the census figure is a lower bound at 88% master-symbol
coverage - but coverage is identical at both ticks, so the direction stands.

**What this means for the sprint.** T2.13 and T2.14 sized the soft-float lever
with this instrument. Those sizings carry a +/-2.6 pp per-symbol error bar
nobody has been quoting, and it is larger than most of the differences they
were used to adjudicate. The repair is cheap and needs no new tool: **the
idle-attribution capture already produces an exact master profile and should be
the ranking instrument from now on.** The burst profiler's remaining unique
value is that it also covers the slave and attributes call counts.

### 3.5 The burst-profile runs, kept for the record

Four runs were taken before the census cross-check existed. Their aggregate
class shares are recorded here because section 3.4 needs them, not because
they are load-bearing.

| Run | Warm-up | Route span | idle share | soft-float share of working |
| :--- | ---: | :--- | ---: | ---: |
| `t30` | 30 | 30 -> 52 | 56.93% | 17.47% |
| `t180` | 180 | 180 -> 202 | 54.94% | 19.53% |
| `t180g7` | 180, `--gap-vblanks 7` | 180 -> 325 | 56.34% | 20.15% |
| `t300` | 300 | 300 -> 320 | 56.14% | 20.01% |

Their idle share (55-57%) also disagrees with the census (33.9-34.2%,
section 4.3), which is the phase-locking T2.16 diagnosed showing up exactly as
predicted: bursts start where the master spins, so idle is over-counted and
every working share is compressed by a common factor.

## 4. The three ratios, re-measured

### 4.1 Terrain versus actor VDP1 command share

`capture_terrain_lod_census.py`, now tick-warmed (section 7), 120 samples,
gap 11 VBlanks, on the same sealed ELF.

| Quantity | T2.19c (`--warmup-vblanks 1800`, ticks 31→177) | **T2.20 tick 30** (ticks 30→176) | **T2.20 tick 180** (ticks 180→317) | delta 30→180 |
| :--- | ---: | ---: | ---: | ---: |
| terrain commands/frame | 209.34 | **205.24** | **241.52** | **+17.7%** |
| actor commands/frame | 312.03 | **310.91** | **301.69** | −3.0% |
| VDP1 emissions/frame | 517.35 | **513.04** | **539.34** | +5.1% |
| **terrain share** | **40.46%** | **40.00%** | **44.78%** | **+4.78 pp** |
| **actor share** | **60.31%** | **60.60%** | **55.94%** | **−4.66 pp** |
| unattributed/frame | −4.01 | −3.11 | −3.87 | — |

**The tick-30 run reproduces T2.19c to within 2%** (205.24 against 209.34;
40.00% against 40.46%), which validates both the instrument and the
conversion. T2.19c's number was correct for the window it sampled.

**T2.19c's headline survives the move to a representative scene, with a
narrowed margin.** Actors remain the larger half of the frame's VDP1
commands — 55.94% against terrain's 44.78% — so the argument that Mario mesh
decimation targets the majority of commands **still holds**, and the work
decision it gates does not change. But the margin falls from 20 pp to 11 pp,
and the direction of travel is against it: terrain grows +17.7% with camera
motion while actors shrink 3%. Anyone quoting "60/40" should quote
**"56/45 in gameplay, 61/40 standing still"** instead.

Two further facts from the same captures, both scene-**in**dependent and both
confirming T2.19c:

- `demo_lod_tier_near/_mid/_far`, `demo_lod_primitives_suppressed` and
  `demo_bob_primitives_visible` read **exactly 0.00 at both ticks**. T2.19c
  section 3.3's "the LOD telemetry has no writer" is re-confirmed on a second
  window; it is a source fact, not a sampling accident.
- The variance widens sharply in gameplay: actor commands/frame range
  283–341 in the tick-30 window and **149.5–546** in the tick-180 window, as
  Mario's screen size changes through the jump. A single-window mean is a
  much weaker summary of gameplay than of the static block.

### 4.2 VDP1 occupancy

`capture_idle_attribution.py`, one contiguous 6,000,000-master-instruction
window, 300 strided VDP1/VDP2 register samples. `EDSR.CEF` (bit 1) is set once
VDP1 has finished plotting the current command list, so **1 - CEF share is the
fraction of the frame VDP1 spends plotting**.

| Capture | Build | Route tick | `EDSR.CEF` set | **VDP1 plotting** |
| :--- | :--- | ---: | ---: | ---: |
| T2.16 | `id-a61d5203793986e7` | 25 | 36 / 300 = 12.00% | 88.00% |
| T2.17 | `id-c0352f297034f653` | 31 | 19 / 300 = 6.33% | **93.67%** |
| **T2.20** | `id-c0352f297034f653` | **30** | 20 / 300 = 6.67% | **93.33%** |
| **T2.20** | `id-c0352f297034f653` | **180** | 36 / 300 = 12.00% | **88.00%** |

Two things fall out.

**T2.17's 93.67% reproduces cleanly at the same route position** - 6.33%
against 6.67% on the same build one tick apart. The instrument is sound.

**But the figure is scene-dependent, and it moves the other way from
intuition.** At a representative gameplay tick VDP1 plots **88%** of the frame,
not 93.67% - it has *more* slack in motion, despite carrying 5% more commands
(539.3 against 513.0 per frame, section 4.1). The mechanism is that plot time
is a function of *pixel coverage*, not command count: in gameplay more of the
scene is distant, so commands are smaller. T2.16's `id-a61d5203793986e7`
measurement at tick 25 also read 88.00%, so **the "VDP1 occupancy rose from 88%
to 93.67% between T2.16 and T2.17" comparison was conflating a build change
with a scene change** - and at the same scene position the two builds agree.

**This is the weakest number in this report and should be treated as
indicative.** 20 versus 36 hits out of 300 is a 5.33 pp difference against a
binomial sd of ~1.4-1.9 pp per capture, but the 300 samples are strided within
a single 1.8-frame trace and are therefore not independent; the effective
sample size is much smaller than 300. It is a real signal in the right
direction, not a precise share. **What it does license:** the "VDP1 becomes the
wall the moment the master stall is fixed" conclusion (T2.16 headline 4,
T2.17 section 6) should be re-derived against 88%, not 93.67%, which leaves
noticeably more headroom than the sprint has been assuming.

`COPR` sweeps 8 to 2,216 (t30) and 8 to 2,248 (t180) with 37 and 38 distinct
values - the full command list in both cases, so VDP1 is genuinely working
through the whole list at both ticks. `TVSTAT`'s VBlank bit reads 15.33% at
both ticks against ~14.8% expected from 39 non-active lines of 263, which is
T2.16's calibration reproduced: the strided sampler is essentially unbiased
in time.

### 4.3 Master and slave idle

Same captures.

| Quantity | T2.16 (`a61d`, tick 25) | T2.17 (`c0352`, tick 31) | **T2.20 tick 30** | **T2.20 tick 180** |
| :--- | ---: | ---: | ---: | ---: |
| master idle share | 19.5445% | **0.000000** | **0.000000** | **0.000000** |
| master idle VB/frame | 2.1903 | 0.0 | **0.0** | **0.0** |
| slave idle share | 79.1449% | 73.0397% | **67.8023%** | **68.4081%** |
| combined idle share | 49.3447% | 36.5198% | **33.9011%** | **34.2041%** |
| traced span | 19.64 VB / 1.75 frames | 16.36 VB / 1.46 frames | 16.27 VB / 1.82 frames | 16.32 VB / 1.83 frames |

**The master idle collapse is scene-independent and this is the cleanest
result in the report.** T2.17 removed the per-field epoch stall and measured
master idle at exactly 0.000000; that holds at a representative gameplay tick
too, and `master_wait_call_sites` is empty in both captures - the master never
enters `_sm64_saturn_source_runtime_wait_vblank` at all any more. T2.17's
headline survives without qualification.

**The slave idle share is flat within noise.** 67.80% at tick 30 against
68.41% at tick 180 is 0.6 pp. The noise floor is set by the T2.17-versus-T2.20
pair, which is the *same build one tick apart* and differs by **5.2 pp**
(73.04% against 67.80%) - so a 1.8-frame contiguous trace resolves the slave
idle share to no better than about +/-5 pp, and the tick-30-to-tick-180
difference is well inside it. **Treat the master/slave idle split as
scene-independent.**

The joint state confirms the shape: `work | idle:notification-wait` is 67.80%
(t30) and 68.41% (t180); `work | work` is 32.20% and 31.59%. There is no
`idle | *` row at all, because the master is never idle.

**One error in T2.17's evidence file, found here and worth recording.** Its
idle capture was run with the tool's default `--vblanks-per-frame 11.2069`,
which is the *predecessor* build's cadence, while the build it measured runs at
**8.9310**. Every `idle_vblanks_per_frame` in
`sprint2-t2_17-idle-attribution-fullframe.json` is therefore inflated by 25.5%:
its "slave idle 8.1855 VB/frame" should read **6.523**. Shares are unaffected,
and the T2.17 markdown quotes shares, so no published conclusion changes. This
task's captures pass `--vblanks-per-frame 8.9310` explicitly.

---

## 5. Is the FPS number representative?

**Yes, and it is not re-litigated here — the owner has confirmed it against
manual play of real scenes.** This report contributes one independent
corroboration and one mechanism note, both cheap:

**Corroboration.** The route timeline measures frame length directly, from
`frame_serial` deltas against VBlanks, without any cadence tool:

| Block | Route ticks | VBlanks | Frames | **VB/frame** | implied FPS |
| :--- | :--- | ---: | ---: | ---: | ---: |
| stationary | 10 → 120 | 987 | 110 | **8.9727** | 6.687 |
| running | 130 → 200 | 633 | 70 | **9.0429** | 6.635 |

**0.78% apart.** The frame *composition* changes with the scene; the frame
*cost* does not, on this route. `6.7181 FPS / 8.9310 VB` is a number about the
game.

**Mechanism note, reconciling `--startup-vblanks 4096` with "ticks 0→31".**
These never disagreed; they measure different things.
`--startup-vblanks` is the **bound on a polling loop** that runs one VBlank at
a time until the ELF's identity bytes appear in memory
(`wait_for_target_identity`), not a duration anyone waits. On this build the
match arrives at **681** VBlanks (`startup_identity_attempts: 681`, confirmed
independently by the timeline capture). `capture_sourceboot_throughput.py`'s
own default for that bound is in fact **600**, not 4096; the 4096 that appears
across the evidence tree is the *other* tools' default. After the match the
throughput tool free-runs to a count of **presentation events**, which is
simulation-anchored, and that window happens to cover replay ticks 0→31. So
"4096" describes a timeout, "0→31" describes the window, and both are correct.

---

## 6. The audit: which findings are scene-dependent

The rule used: a **within-build structural fact** (a source rule, a compiled
table, a symbol's existence, a mutation result, a hash) is scene-independent.
A **ratio, share, occupancy, per-frame count or ranking** is scene-dependent
and inherits whatever its window was looking at.

### 6.1 Scene-independent — unaffected, keep using them

| Finding | Source | Why it is safe |
| :--- | :--- | :--- |
| The epoch rule stamps three flags; G1/G2 are the only guarantees riding on them; the bank-ownership safety argument | T2.17 §1–2 | control-flow properties of the shipped source |
| The 867 baked primitives are 1 VDP1 command each; the only fan-out is the pentagon split | T2.19c §4.1 | generated header + `saturn_demo_render.c:2856` |
| Merge is impossible: 622 shared edges, **1** same-texture pair, **0** same-texture-and-coplanar | T2.19c §4.2 | static analysis of `bob_scene.h` |
| `lod_mid_mask` is inert; `lod_far_mask` drops 94 of 867 by `source0 % 8`; tier thresholds and hysteresis bands | T2.19c §5, §5.2 | generator + `ztreme_hot_promotion.c` |
| Six LOD counters have no writer | T2.19c §3.3 | tree-wide grep, **re-confirmed live at two ticks** (4.1) |
| The Mario double-emit is capped at 50 (50 of 644 primitives carry a texture tile) | T2.19a | `sm64_mario_texture_tile_start` |
| `sState.input_replay_ticks` indexes the route; 1,800 VBlanks lands on tick 25 / 31; the fail-loud rules; 10/10 mutations killed | T2.19d | reproduced exactly by this task |
| The static arithmetic census (1,827 hot-reachable soft-float sites) | `sprint2-arithmetic-census.md` | static reachability by construction |
| The master's *wait site* is `main.c:2013` and `display_and_vsync()` is unreachable (`display_suppressed` held true) | T2.16 §2 | `main.c:547`; only one reachable caller exists |
| Build identity, release manifests, host contract tests, gate results | all | not measurements of a scene |

### 6.2 Scene-dependent, now re-measured — the size of the error is stated

| Finding | As published | Re-measured in gameplay | Status |
| :--- | :--- | :--- | :--- |
| terrain 40.5% / actor 60.3% of VDP1 commands (T2.19c F1) | 209.34 / 312.03 | **241.52 / 301.69 → 44.8% / 55.9%** | **survives directionally**, margin 20 pp → 11 pp. The decision it gates is unchanged. |
| the cycle-profile hot-spot ranking (T2.13, T2.14) | tick ~25-31 | **section 3.2** | **SURVIVES.** Measured contiguously the master's top five are identical in order at both ticks; largest move 1.01 pp, largest rank move 4 places. The slave's top four are identical. |
| soft-float share of working cycles | 17.47% @ tick 30 (burst sampler) | **20.63% falling to 18.91% (contiguous census)** | **the published level and trend are both wrong** - for an *instrument* reason, not a scene reason (3.4) |
| VDP1 occupancy (`EDSR.CEF` share) | T2.16 88% plotting, T2.17 93.67% | **93.33% @ tick 30, 88.00% @ tick 180** | **scene-dependent.** T2.17 reproduces at its own tick; in gameplay VDP1 plots 88%, so "VDP1 is the next wall" should be sized against 88%. Weak statistics - see 4.2. |
| master idle | T2.17 0.000000 | **0.000000 at both ticks** | **SURVIVES unconditionally** |
| slave / combined idle split | T2.16 79.1% / 49.34%; T2.17 73.04% / 36.52% | **67.80% / 33.90% @ tick 30, 68.41% / 34.20% @ tick 180** | **scene-independent within a +/-5 pp noise floor** measured on the same build one tick apart (4.3) |
| `6.7181 FPS / 8.9310 VB` (T2.17) | ticks 0 to 31 | 8.9727 VB/frame static vs 9.0429 running | **representative** (0.78% apart) |

### 6.3 Scene-dependent and **not** re-measured here — read with the window stated

| Finding | Window it actually sampled | Caution |
| :--- | :--- | :--- |
| T2.14 route-counter funnel rates (`triangles_emitted` 502.8/frame, reject buckets, `find_floor`/`find_ceil`/`find_wall` rates) | ticks 25 → 144, so ~80% stationary | the collision-call rates in particular are for a Mario who is not moving; `find_floor` is a per-frame cost that scales with motion |
| T2.16 §3 master run structure, the 1:14 work-to-wait ratio, the ordered run log | ticks **25 → 27** — two ticks, wholly static | the *site* is structural (6.1); the *sizes* are from a 2-tick static window |
| T2.8 "552.2 commands per present, actor 97.5 = 17.7%" | `capture_sprint2_peaks`, 26,181 emulated frames — whole route **plus a 6,900-frame post-route neutral tail**; **62.2% of sample intervals stationary** | already contradicted by T2.19c on the actor figure; the tail is a third stationary regime and drags every mean toward it |
| T2.4 / T2.5 / T2.6 / T2.9 / T2.10 peak-capture per-frame counts | same sweep | same tail bias, same 62.2% |
| T2.13 §, T2.14 § soft-float profiles compared across builds | `--phases 1 --phase-vblanks 1800` on two builds | carries **both** defects: wall-clock warm-up (T2.19d §6.3) *and* a static window |

### 6.4 Already known void — no change

- T2.16's original 73.33% idle figure (five-window phase-locked sampler),
  retracted by T2.16 itself.
- The "terrain is ~82% of commands" figure, contradicted by T2.19c and again
  here.
- The "552 commands against 1,664 capacity ⇒ VDP1 is idle" inference, which
  T2.16 showed is a static array occupancy carrying no information about
  whether the chip is busy.

### 6.5 Findings currently driving decisions - the short list

1. **"Actors are 60% of commands, so Mario decimation targets the majority."**
   Survives at 56%. T2.21 landed the decimation concurrently with this task;
   **that decision is supported** - actors remain the larger half of the
   frame's VDP1 commands at a representative gameplay tick. Size the realised
   payoff off **55.9%**, not 60.3%, and re-measure the split on the
   post-decimation build, since decimation moves the numerator directly.
2. **"Terrain LOD is worth ~2-5% of frame commands" (T2.19c section 5.1).** The
   denominator moves: terrain is 241.5 commands/frame in gameplay, not 209.3,
   so the arithmetic bound rises with it. The conclusion (small, arbitrary
   holes) is unchanged; the number should be restated against 241.5.
3. **"VDP1 plots 93.67% of the frame and is the next wall" (T2.17 section 6).**
   That is the *stationary* figure. In gameplay VDP1 plots **88%**, so the
   headroom above the master's work is larger than the sprint has assumed.
   Re-derive the ceiling against 88% - and re-take the measurement first, since
   section 4.2 says the statistics are weak.
4. **The soft-float lever (T2.13, T2.14).** Not mis-sized by the *scene* - it is
   mis-sized by the *instrument*. The burst profiler's +/-2.6 pp per-symbol
   error is larger than most of the differences it was used to adjudicate, and
   it reported the soft-float trend with the wrong sign. **Re-derive from the
   idle-attribution census, which is exact on the master and needs no new
   tooling.**
5. **`_demo_render_prepare_publish` has never been examined** and is rank 6 of
   master cycles in gameplay (3.91%, up from 3.04% and rank 10). Both
   instruments agree on the direction. This is the one concrete "optimisation
   left on the table" this task surfaced.

## 7. Tooling changes

T2.19d left a task chip for two tools it could not convert because other
agents owned them. Both are now committed and their tasks (T2.19a, T2.19c)
are closed, **so they were free and are converted**:

| Tool | Before | After |
| :--- | :--- | :--- |
| `capture_terrain_lod_census.py` | `--warmup-vblanks` default 1800 | `--warmup-ticks` default 30; records `warmup` and `route_position_after_census`, so every census states the tick span it covered |
| `capture_actor_command_share.py` | `--warmup-vblanks` default 1800 | `--warmup-ticks` default 30; records `warmup` |

Both keep `--warmup-vblanks` working behind T2.19d's deprecation warning.
`capture_route_timeline.py` is new and read-only.

**One defect found and worked around, not fixed:** `YmirClient.__init__`
(`capture_route_views.py:105-107`) launches Ymir with `cwd=game.parent` while
passing the *original* `--game` string, so a **relative** CUE path is
unresolvable from the child's working directory and Ymir exits before
speaking protocol. Every capture in this tree must be given absolute paths.
`capture_route_timeline.py` resolves its own paths; the pre-existing tools
were not touched.

---

## 8. Recommended standard warm-up

**`--warmup-ticks 150`**, for every attribution, profile, idle, census and
route-counter capture. Reasoning:

1. **It is past tick 121**, so Mario is in `ACT_WALKING` and the route is
   actually driving the simulation. Tick 30 cannot be representative of
   anything the player sees, because at tick 30 the player is not doing
   anything.
2. **It is the tick at which `forwardVel` saturates** at 26.4833 — so the
   scene is in steady-state locomotion rather than mid-acceleration. Ticks
   121–149 are a transient with a changing collision and camera load.
3. **It is well clear of the level-load transient** (ends ~tick 20), which is
   the only part of the route where terrain command count is still falling.
4. **It leaves room for the span.** A 120-sample × 11-VBlank census covers
   ~146 ticks, so a tick-150 warm-up spans 150 → ~296: run, running jump and
   the start of the arc, all inside the 600-tick usable route with 300 ticks
   of headroom. A tick-180 warm-up (used here) spans to 317 and is equally
   safe; 150 is preferred only because it starts at the beginning of
   steady-state rather than 30 ticks into it.
5. **It stays a single number.** Every build reaches tick 150 by construction
   (`bootstrap_ticks: 600`), and T2.19d's fail-loud rules already reject an
   unreachable target before Ymir starts.

**Do not change `capture_sourceboot_throughput.py`'s default.** Its window is
event-counted and its historical cadence baselines (5.3538 / 6.7181) are quoted across
the sprint; section 5 shows the cadence is flat across the route anyway, so
moving it would cost comparability and buy nothing.

**And change the ranking instrument.** Section 3.4 is the stronger
recommendation of the two: `capture_softfloat_profile.py` at its default
50,000 samples cannot resolve a 2.6 pp effect, and the sprint has been using it
to adjudicate smaller ones. `capture_idle_attribution.py`'s `master_symbols`
block is an exact, unsampled cycle profile of the master over 1.8 consecutive
frames and costs one capture that is already being run. **Rank with the census;
use the burst profiler only for slave coverage and call counts.**

**Report the tick span, not just the warm-up.** Every converted tool now
records the route position after sampling. A capture that does not state the
tick range it covered cannot be audited, and this whole task exists because
several could not be.

---

## 9. Honesty

- **The worst case did not happen.** Mario is rendered from tick 7 and is on
  screen in every capture this sprint took. The finding is the weaker but still
  real one: he is standing still in all of them.
- **My own first answer was wrong, and the cross-check caught it.** The burst
  profiler said the hot-spot ranking reordered dramatically in gameplay -
  `___mulsf3` 3.83% to 8.42%, rank 5 to 2. The contiguous census, on the same
  build at the same two ticks, says `___mulsf3` goes 8.33% to 7.61% and stays
  rank 2. **Section 3's conclusion is the opposite of what a single-instrument
  reading of this task would have produced**, and it is only defensible because
  two instruments were compared at the same route position. An earlier draft of
  this report asserted the reordering.
- **The terrain/actor split moved less than the concern implied.** 40.5% to
  44.8% terrain. The decision it gates is unchanged. The tick-30 run reproduced
  T2.19c to within 2%, which is a clean result for T2.19c.
- **The VDP1 occupancy figure is the weakest number here.** 20 against 36 CEF
  hits from 300 non-independent samples inside one 1.8-frame trace. It is a
  direction, not a share. Re-take it with several windows at different route
  positions before deciding anything on the exact 88%.
- **The slave idle noise floor is ~5 pp** at this trace length, established by
  T2.17 and T2.20 disagreeing by 5.2 pp on the same build one tick apart. Any
  slave-idle comparison finer than that is unsupported, including this task's
  own 0.6 pp difference - which is why it is reported as flat rather than as a
  rise.
- **The census covers 88% of master cycles.** `master_symbols` lists 40 symbols
  summing to 6.44M of 7.28M master cycles, so the soft-float shares in 3.4 are
  lower bounds. Coverage is identical at both ticks, so the direction is safe;
  the absolute level is not.
- **One run per configuration.** The emulation and the tick warm-up are both
  deterministic, so a literal repeat would be bit-identical and would measure
  nothing. The variance that matters is sampler phase and scene, which is what
  the extra windows probe. Wall-clock run-to-run variance remains unmeasured -
  the same gap T2.16, T2.17 and T2.19c all recorded.
- **"Representative" is a judgement about this route.** Ticks 150-320 are Mario
  running forward and jumping across BOB with a tracking camera. That is
  representative of *this* 2,000-tick synthetic traversal, not of every scene in
  the game. A crowded actor scene or a different level could rank differently,
  and nothing here bounds that.
- **The wall-clock column in section 2.1 includes the harness's fixed
  1,500-VBlank BIOS handoff**, a capture-tool constant (`run_bios_handoff` waits
  1,200 frames for disc start), not a measurement of what a human waits at a
  real BIOS screen. The game-relevant figures are **37.8 s from disc start to
  Mario on screen** and **54.8 s to Mario moving**.
- **T2.8's peak-capture family was not re-measured.** Its sweep has the opposite
  bias to everything else here - a 6,900-frame post-route stationary tail, 62.2%
  of intervals stationary - and correcting it needs a different instrument, not
  a different warm-up.
- **Nothing in section 3 is a claim about the slave's soft-float**, because the
  slave runs none: every `___*sf3` cycle in both captures is on the master.

## 10. Reproduction

```bash
YMIR=<ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe>
IPL=<sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin>
NM=<work/yaul-install/bin/sh-elf-nm.exe>
B=<releases/2026-08-16_t2_17-product/id-c0352f297034f653>   # absolute path required

# 1. The route timeline (~72 s, one boot).
python tools/saturn/capture_route_timeline.py --ymir "$YMIR" --ipl "$IPL" \
  --game "$B/sm64-saturn-sourceboot-e2.cue" --elf "$B/obj/sm64-saturn-sourceboot-e2.elf" \
  --state-address 0x060f0d68 --global-timer-address 0x060bcda4 \
  --mario-state-address 0x06091ff8 --profile-address 0x2cdac8 \
  --actor-address 0x060ea4ec --until-tick 200 \
  --output docs/saturn/evidence/reports/sprint2-t2_20-route-timeline-c0352f29.json

# 2. Cycle profiles, static versus gameplay (~2 min each).
for T in 30 180 300; do
  python tools/saturn/capture_softfloat_profile.py --ymir "$YMIR" --ipl "$IPL" \
    --game "$B/sm64-saturn-sourceboot-e2.cue" --elf "$B/obj/sm64-saturn-sourceboot-e2.elf" \
    --nm "$NM" --warmup-ticks $T --vblanks-per-frame 8.9310 --top 80 \
    --output docs/saturn/evidence/reports/sprint2-t2_20-softfloat-profile-tick$T-c0352f29.json
done
# phase control: add --gap-vblanks 7 at --warmup-ticks 180

# 3. Terrain/actor census, static versus gameplay (~60 s each).
for T in 30 180; do
  python tools/saturn/capture_terrain_lod_census.py --ymir "$YMIR" --ipl "$IPL" \
    --game "$B/sm64-saturn-sourceboot-e2.cue" --elf "$B/obj/sm64-saturn-sourceboot-e2.elf" \
    --profile-address 0x2cdac8 --warmup-ticks $T --samples 120 --gap-vblanks 11 \
    --output docs/saturn/evidence/reports/sprint2-t2_20-terrain-lod-census-tick$T-c0352f29.json
done

# 4. Idle attribution and VDP1 occupancy (~10 min each).
for T in 30 180; do
  python tools/saturn/capture_idle_attribution.py --ymir "$YMIR" --ipl "$IPL" \
    --game "$B/sm64-saturn-sourceboot-e2.cue" --elf "$B/obj/sm64-saturn-sourceboot-e2.elf" \
    --nm "$NM" --warmup-ticks $T --windows 1 --window-steps 6000000 \
    --slave-stride 16 --io-stride 20000 --vblanks-per-frame 8.9310 \
    --output docs/saturn/evidence/reports/sprint2-t2_20-idle-attribution-tick$T-c0352f29.json
done
```

Symbol addresses for `id-c0352f297034f653` come from
`sh-elf-nm` on its ELF: `sState 0x060f0d68`, `gGlobalTimer 0x060bcda4`,
`gMarioState 0x06091ff8`, `sourceboot_fast3d 0x002cdac8`,
`s_actor_command_count 0x060ea4ec`. The `id-a61d5203793986e7` build's are
`0x060f0d28`, `0x060bcd64`, `0x06091fc8`, `0x002cdac4`, `0x060ea4ac`.

---

## 11. References

Per the reference-code-first rule and the standing owner instruction.

No third-party code was read, adapted or ported. This task is a set of
`mem.peek` reads of globals that already exist in the shipped ELF, plus two
three-line warm-up conversions using T2.19d's existing `route_warmup` module.
There is no upstream to reuse for "read this build's own counters".

| Source | Files inspected | Reuse mode |
| :--- | :--- | :--- |
| this repo, `tools/saturn/verify_route_warmup_parity.py` | `:60-110` (`gMarioState` window decode, `symbol_address`) | pattern-only, in-tree |
| this repo, `tools/saturn/capture_terrain_lod_census.py` | `:57-89` (profile offset table) | pattern-only, in-tree — offsets reused verbatim |
| this repo, `tools/saturn/capture_actor_command_share.py` | `:55-79` (`s_actor_command_count` adjacency) | pattern-only, in-tree |
| this repo, `tools/saturn/route_warmup.py` (T2.19d) | whole module | dependency |
