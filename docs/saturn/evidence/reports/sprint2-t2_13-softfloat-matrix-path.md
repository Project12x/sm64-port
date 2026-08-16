# Sprint 2 Task T2.13 - the soft-float purge, measured before it was cut

- Date: 2026-08-16. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `099ce72a`.
- Task: begin the soft-float purge, starting with the per-frame matrix path.
  Specification: `sprint2-arithmetic-census.md` (commit `b7e775fd`, ELF
  `1b4ff08d`) - remediation item 1, item 6, and the stand-alone "free win".
- Measurement basis: `summarize_cadence` only, per T2.11. Baseline to beat:
  `id-05046d9d5d8a5593` at **4.9432 FPS / 12.1379 VB per frame**.
- Oracle precedent: T2.12 (`sprint2-t2_12-hierarchical-admission.md` section 6).
  **This is the first oracle in the sprint that is not an identity oracle**, and
  section 5 says why, and what it bounds instead.
- Mid-task scope relaxation from the owner, applied and recorded here because it
  changed what was built: *"byte identical is not important for this case at
  all"* - take the faster path, bound blunders rather than small deltas, measure
  downstream movement but do not gate on it.

## Headline

**Measured: 5.3538 FPS mean, 11.2069 VBlanks per frame**, against T2.12's
4.9432 / 12.1379 on the same tool, same emulator, same route.
**+0.4106 FPS (+8.31%), -0.9310 VBlanks per frame.** Median **5.4545**, 1% low
**5.0**.

That is **past A9A on both numbers** - A9A archived reads 5.294 FPS mean and
11.333 VB/frame - for the first time in this sprint.

The T2.11 cadence-rail allowance the brief asked to watch went the *right* way:
needed on **0 of 29 intervals, from 14 of 29** at T2.12.

**Three findings outrank the plan, and all three come from measuring first:**

1. **The census pointed at the right instruction and the wrong function.** The
   double-precision `sinf`/`cosf` really are the whole of this build's
   soft-double cost - but they are not reached from `guRotateF`, which **never
   executes on this route at all**. Every sampled soft-double call comes from
   **`calculate_vertex_xyz` in `src/game/shadow.c`**.
2. **Static call-site counts misrank dynamic cost badly.** The census's number
   one float caller, `_find_wall_collisions_from_list` at 110 static sites, is
   near the bottom of the measured profile. Six of its top nine targets never
   executed once in 240,000 samples.
3. **70.21% of sampled SH-2 cycles are idle.** Deleting *all* float from the
   image would move at most 7.5% of sampled cycles. Float was worth cutting and
   the cut paid, but the largest lever visible in this profile is the
   master/slave handoff and the VBlank wait, not arithmetic.

**OWNER-VISIBLE CHANGE - see section 6.** Mario's shadow is now built from
slightly different vertex positions. Worst case **0.147 world units, under a
tenth of a screen pixel**, and it cannot accumulate across frames. It is still
a change to what is on screen, and the owner holds that gate.

---

## 1. Deliverable (1): the dynamic ranking

### 1.1 How it was measured - no rebuild, no instrumentation, no new probe

`ymir-headless` already returns everything a profiler needs. `exec.stepi`
reports `pc_before`, `pc_after` and `cycles_advanced` for one executed
instruction: the instruction at `pc_before` cost `cycles_advanced` cycles,
which is a complete cycle-attributed sample. Stepping advances the same
deterministic machine the free-running loop advances, so **the profile is of
the shipped build itself**, not of an instrumented variant, and no build was
needed to obtain it. That is cheaper and more faithful than the SMC1-style
in-target counter block this tree already uses for `atan2s`, which needs its
own build to read.

`tools/saturn/capture_softfloat_profile.py` advances at full speed for one
VBlank, single-steps a short burst, and repeats, alternating between the two
SH-2s because `slave_render=1` puts real render work on the slave. Symbols come
from `sh-elf-nm -S` on the exact ELF. A transfer whose `pc_after` equals a
symbol's first address is recorded as a **call into** that symbol, and the
symbol containing `pc_before` is recorded as its **caller** - which is where
the attribution in 1.4 comes from, and it is what turned the census's
conclusion around.

| Parameter | Value |
| --- | --- |
| Target | `releases/2026-08-16_t2_12-product/id-05046d9d5d8a5593/...e2.elf` |
| Target SHA-256 | `bda133fc5de43baa8b4b7b3556b38c4a1be26bde35518786447a21e76b171b4e` |
| Warm-up | 1,800 VBlanks after the ELF identity match |
| Sampling | 600 bursts x 400 single-steps, 1 VBlank between bursts, alternating `sh2.master` / `sh2.slave` |
| Total | **240,000 samples, 578,603 cycles**, 30.4 s wall |
| Evidence | `sprint2-t2_13-softfloat-profile-baseline.json` |

The warm-up is not a detail. **The ELF identity matches while the CD loader is
still running**, so a sweep taken immediately after the identity match profiles
`_cd_block_cmd_execute` and `_sha256_block`, not the route. The first two
attempts at this measurement did exactly that, and that is why the tool grew
`--warmup-vblanks` and a `--phases` sweep.

### 1.2 The ranking, by cycle share of the whole sample

| Cycle share | Symbol | Class |
| ---: | --- | --- |
| 41.443% | `___slave_polling_entry` | **idle** |
| 28.764% | `_sm64_saturn_source_runtime_wait_vblank` | **idle** |
| 3.662% | `_actor_meshlet_live_depth_bounds` | application |
| 2.211% | `_demo_actor_queue_vertex_lookup` | application |
| 2.176% | `_demo_actor_lower_compat_wrapper` | application |
| 2.047% | `_demo_terrain_queue_world_lower` | application |
| **1.889%** | **`___mulsf3`** | soft-float |
| 1.658% | `_find_floor_from_list` | application |
| **1.238%** | **`___muldf3`** | **soft-double** |
| **1.104%** | **`___addsf3`** | soft-float |
| 0.951% | `_demo_projected_read` | application |
| 0.794% | `_actor_position_ref` | application |
| 0.684% | `_sm64_saturn_hud_atlas_write_cell` | application |
| **0.671%** | **`___adddf3`** | **soft-double** |
| 0.525% | `___subsf3` | soft-float |
| 0.517% | `_saturn_mtxq_refresh_float_mirror` | application (no FP arithmetic) |
| **0.369%** | `___subdf3` | **soft-double** |
| 0.312% | `___divsf3` | soft-float |
| **0.171%** | `___truncdfsf2` | **soft-double convert** |
| **0.127%** | `_cosf` | libm |

By class:

| Class | Share of sampled cycles | Share of non-idle cycles |
| --- | ---: | ---: |
| idle | **70.21%** | - |
| application | 21.45% | 71.9% |
| soft-float | 4.27% | 14.3% |
| **soft-double** | **2.30%** | **7.7%** |
| shift (mostly *inside* the soft-fp helpers) | 0.73% | 2.4% |
| soft-float convert | 0.37% | 1.2% |
| soft-double convert | 0.23% | 0.8% |
| libm (`sinf`/`cosf`/`sqrtf` bodies) | 0.20% | 0.7% |
| soft-float support (`___clzsi2`, `___powisf2`) | 0.13% | 0.4% |
| **integer divide, all widths** | **0.12%** | **0.4%** |
| **all float, summed** | **7.50%** | **25.16%** |
| **soft-double + converts + the libm bodies** | **2.73%** | **9.16%** |

The entire `divide` class is 0.12%, and all of it is `___udiv_qrnnd_16` reached
from `___divsf3`. **`___sdivsi3` does not appear in the profile at all.**

### 1.3 Where the static census misranks

| Function | Census static sites | Measured helper entries | Verdict |
| --- | ---: | ---: | --- |
| `_find_wall_collisions_from_list` | **110 - the census's #1** | 7 `___addsf3` + 5 `___lesf2` | near the bottom |
| `_create_transformation_from_matrices` | 78 | **0** | never executed |
| `_sm64_saturn_audio_spatial_quantize` | 74-80 | **0** | never executed |
| `_evaluate_cubic_spline` | 70-71 | **0** | never executed |
| `_rotate_triangle_vertices` | 61 | **0** | never executed |
| `_envfx_update_snow_normal` | 41 | **0** | never executed |
| `_guRotateF` | 29 | **0** | **never executed on this route** |
| `_calculate_vertex_xyz` | 28 | **~75, including every soft-double** | **the top float consumer** |
| `_saturn_geo_enter_held_object` | 6 | **0** | never executed |

**`guRotateF` is not on the per-frame matrix path**, and the brief inherited
that error from the census. Its only callers are `src/game/ingame_menu.c:158`,
`src/game/paintings.c:922-923` and `src/goddard/renderer.c:2747,2773` -
HUD/dialog, paintings, and the file-select Mario head, none of which run on the
BOB demo route. The real per-frame matrix work is `mtxf_*` in
`src/engine/math_util.c`, driven by `sins`/`coss`, which are **already pure
table loads with no float arithmetic at all** and already Q16-mirrored in
`saturn_matrix_ctors.h`. The census's own section 10 item 1 warns that "HOT
means reachable per frame, not executed per frame"; this is that caveat cashing
out at full size.

### 1.4 Caller attribution: the soft-double is exactly one function

Entry samples, by the symbol that transferred into the helper:

| Helper | Callers observed |
| --- | --- |
| `___muldf3` | `_cosf` **26**, `_calculate_vertex_xyz` 1, `_sourceboot_vdp2_sky_scroll_set` 1 |
| `___adddf3` | `_cosf` **18**, `_sourceboot_vdp2_sky_scroll_set` 1 |
| `___subdf3` | `_cosf` **15** (all of them) |
| `___truncdfsf2` | `_cosf` **12**, `_calculate_vertex_xyz` 1 |
| `___fixdfsi` | `_cosf` 2 (all of them) |
| `___floatsidf` | `_cosf` 2, `_sourceboot_vdp2_sky_scroll_set` 1 |
| `_sinf` | `_calculate_vertex_xyz` **8** (all of them) |
| `_cosf` | `_calculate_vertex_xyz` (all of them) |
| `___mulsf3` | `_saturn_geo_enter_object` 26, `_calculate_vertex_xyz` 23, `_vec3f_normalize` 14, `_vec3f_cross` 12, `_find_floor_from_list` 7, `_sqrtf` 4 |

**Every sampled soft-double entry traces to `_cosf`/`_sinf`, and every sampled
`_sinf`/`_cosf` entry traces to `_calculate_vertex_xyz`.** One function, five
double-precision polynomial evaluations per shadow vertex.

### 1.5 The finding that outranks the plan

**70.21% of sampled SH-2 cycles are idle** - the slave in
`___slave_polling_entry`, the master in
`_sm64_saturn_source_runtime_wait_vblank`.

That is not by itself proof the frame is not CPU-bound: a spin-wait on a
handoff is still time the frame spends, and the two CPUs are sampled 50/50 by
burst count rather than in proportion to their real work, which inflates
whichever CPU is idler. But it does mean **the largest lever visible in this
profile is the master/slave handoff and the VBlank wait, not arithmetic**, and
no future task should treat "delete the float" as the frame's dominant cost
without re-reading this number. It is the most actionable thing the measurement
produced and it was not in the plan.

---

## 2. What landed

Three commits, deliberately split so the tolerance-bounded change is not
entangled with the identity-preserving ones.

### 2.1 `b4a2e3e6` perf(shadow) - the double-precision trig, deleted

`src/game/shadow.c`. `calculate_vertex_xyz` computed:

```c
f32 tiltedScale = cosf(s.floorTilt * M_PI / 180.0) * s.shadowScale;
f32 downwardAngle = s.floorDownwardAngle * M_PI / 180.0;
*xPosVtx = (halfTiltedScale * sinf(downwardAngle)) + (halfScale * cosf(downwardAngle)) + s.parentX;
*zPosVtx = (halfTiltedScale * cosf(downwardAngle)) - (halfScale * sinf(downwardAngle)) + s.parentZ;
```

**Root cause of the waste, which is what makes the fix nearly free.** Both
angle fields are produced by `atan2_deg` (`shadow.c:130`):

```c
f32 atan2_deg(f32 a, f32 b) { return ((f32) atan2s(a, b) / 65535.0 * 360.0); }
```

`atan2s` returns an s16 binary angle - the exact unit `sins`/`coss` index with.
`atan2_deg` scales it into degrees, `calculate_vertex_xyz` scales it back into
radians, and hands it to the libultra `sinf`, a five-term polynomial **in
`double`** (`lib/src/math/sinf.c:22-36`) on a CPU with no FPU. The port was
paying five soft-double polynomial evaluations per shadow vertex to recover an
angle it had thrown away three lines earlier.

The fix keeps the binary angle. `struct Shadow` gains `floorDownwardAngleBam`
and `floorTiltBam` (`s16`, Saturn only), set beside the existing f32 fields
from the same `atan2s` calls. 90 degrees is exactly `0x4000` BAM, so the tilt
subtraction stays an exact integer. `calculate_vertex_xyz`'s Saturn arm then
reads the engine's own table:

```c
const f32 sinDownward = sins(s.floorDownwardAngleBam);
const f32 cosDownward = coss(s.floorDownwardAngleBam);
f32 tiltedScale = coss(s.floorTiltBam) * s.shadowScale;
```

`sins`/`coss` are `gSineTable[(u16)(x) >> 4]` - **a single f32 load, no
arithmetic**. The non-Saturn arm is preserved verbatim under `#else`, so the
donor build is untouched.

**Why the table and not a single-precision `sinf`.** The brief allowed either.
Section 1.4 settles it: the entire soft-double cost is these five calls, and a
single-precision rewrite would still evaluate a ~12-operation polynomial per
call where the table costs one load. The owner's scope note ("take the faster
one") removes the remaining doubt.

The table is also the *consistent* choice, not merely the fast one. Every
`mtxf_*` constructor in this engine already quantises its angles to the same
4096 steps per revolution, and so does Sega's own SGL: `slSin`/`slCos` discard
the low 4 bits of the angle by documented design, and say so
(`Documentation/DOC/210A_US/MATH.TXT:326-336` in the Sonic Z-Treme tree,
section 9). The shadow was previously *more precise than the geometry it sits
on*. It is now consistent with it.

**No bigger table was baked**, although the brief invited one and the 4 MB cart
would have absorbed it for free. A finer table cannot help here: the angle
arrives as an s16 and `sins`/`coss` already resolve 4096 of its 65,536 steps,
so extra entries would only make the shadow disagree with every other consumer
of the same angle. Recorded because "we did not spend the cart" is a decision,
not an oversight.

### 2.2 and 2.3 `7b30011b` - the two identity-preserving items

`saturn_geo_enter_held_object` (`src/game/rendering_graph_node.c`) built its Q16
translation as `node->translation[i] / 4.0f` followed by
`sm64_saturn_float_to_q16` - nine soft-float helper calls to compute
`n * 16384`. It is now an integer multiply. (A multiply and not `<< 14`: the
input is signed, and left-shifting a negative value is undefined.)

`push_clamped_int` (`src/port/saturn/gfx/saturn_hud_layout.c`) divided by a
**runtime** divisor, which compiles to `___sdivsi3`, GCC's 32-step software
divide. Every call site passes `max_digits` of 2 or 3, so a three-step ladder
with constant divisors covers them and GCC reciprocal-multiplies each through
`dmuls.l`. The general loop is **retained** for `max_digits > 3` rather than
asserted away, so the function stays total over its declared domain and the
ladder is provable against it instead of merely believed equal.

**Both are bit-exact and both were proven so** (section 5.3) - but per the
owner's relaxation this is no longer a correctness boundary, and the honest
report on their value is that **the measurement says they are worth
approximately nothing**: `_saturn_geo_enter_held_object` never appears in the
sampled profile, `___sdivsi3` never appears in it either, and the entire divide
class is 0.12% of cycles. They are landed because they are free and correct,
not because they buy FPS. Essentially all of the +8.31% belongs to 2.1.

---

## 3. `verify-softfp-bitexact` - what it pins, established before editing

It compiles the same `third_party/gcc-soft-fp` sources the target links, with
the same `config/sh/sfp-machine.h`, for the host, and diffs **each soft-fp
routine** (`__addsf3` ... `__truncdfsf2`, 34 of them) against the host FPU bit
for bit - about 1.2e7 seeded pairs per binary operation and an exhaustive
stride-1 sweep of every single-argument routine.

**Its scope is the soft-float library itself. It says nothing about `sinf`,
`cosf`, `sins`, `coss`, or any application code**, and none of this task's
three changes is inside it. It neither protects this change nor blocks it.
Result: **PASS**, 43,333,704,485 bit-exact checks and 0 failures, including
97,742 NaN-result cases and 6,287,831,620 out-of-range integer casts. The
soft-fp substitution the port depends on is intact and this task did not
disturb it.

---

## 4. FPS

From `summarize_cadence` only, per T2.11 and T2.7 section 1.1. Nothing below is
hand-computed from failure diagnostics. ymir-headless `build-agent2`
(`fcc88d82b2ea7afd...3943`, the same byte-identical binary A9A, T2.7, T2.10,
T2.11 and T2.12 used), BIOS `Sega Saturn BIOS (USA).bin`, absolute `--cue`,
`--startup-vblanks 4096 --max-vblanks 3600 --presentation-events 30
--timeout 1800`, release-manifest bound. **`status: complete`, exit 0, one
attempt.** On-target identity **MATCH** (`observed_sha256 == expected_sha256`,
`d88b17a9...23b5`), `diagnostic_mode: 0` in the target build record.

Evidence: `sprint2-t2_13-throughput-30events.json`.

| Build | Events | Intervals | Window (VB) | **VB/frame** | **FPS mean** | Median | 1% low |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| A9A archived | 10 | 9 | 102 | 11.333 | 5.294 | 5.000 | 5.000 |
| `id-b46f60d0a6d129dd` (T2.10) | 30 | 29 | 403 | 13.8966 | 4.3176 | 4.2857 | 4.0 |
| `id-05046d9d5d8a5593` (T2.12) | 30 | 29 | 352 | 12.1379 | 4.9432 | 5.0 | 4.6154 |
| **`id-a61d5203793986e7` (T2.13)** | 30 | 29 | **325** | **11.2069** | **5.3538** | **5.4545** | **5.0** |

- **-0.9310 VBlanks per frame, +8.31% FPS** against T2.12 on the same 30-event
  basis.
- **Both headline numbers are now past A9A** (11.2069 against 11.333 VB/frame;
  5.3538 against 5.294 FPS). The 0.805 VB/frame gap T2.12 reported is closed
  and slightly reversed.
- The **1% low is 5.0**, a full point above the 4 FPS retention floor, and the
  **median 5.4545** is above A9A's 5.000.

Per-frame phase profile, 29 intervals each:

| | T2.12 `05046d9d` | **T2.13 `a61d5203`** | Delta |
| --- | ---: | ---: | ---: |
| frame (`vblank_delta`) | 12.1379 | **11.2069** | **-0.9310** |
| construction | 6.6552 | **5.6897** | **-0.9655** |
| - master finalization | 4.6552 | 3.6897 | -0.9655 |
| - slave work overlap | 2.4828 | 2.4828 | 0.0000 |
| simulation / source tick | 5.7931 | 4.8276 | -0.9655 |
| transport + presentation | 0.0345 | 0.0000 | -0.0345 |
| attributed | 12.4828 | 10.5172 | -1.9655 |
| allowance available | 4.6552 | 3.6897 | -0.9655 |
| **allowance actually needed** | **1, in 14 of 29** | **needed in 0 of 29** | |

**Two honest caveats on the phase table.** First, `construction` and
`simulation` each fall by exactly -0.9655 = -28/29, which is one whole VBlank
crossing in 28 of 29 intervals. The shadow work lives in `construction` (the
geo walk reaches `saturn_geo_enter_shadow` -> `create_shadow_below_xyz` ->
`calculate_vertex_xyz`); **the identical drop in `simulation` is not
attributable to this change** and is more likely the same crossing being
counted against two overlapping spans at a coarser boundary. The whole-frame
figure of -0.9310 is the one to trust; it is the only one measured directly.
Second, `slave work overlap` is unchanged at 2.4828, which is what a
master-side change should do and is a small consistency check on the
attribution.

**The T2.11 cadence rail moved the right way.** T2.12 needed the concurrency
allowance on 14 of 29 intervals and flagged the trend as load-bearing near
8 VB/frame. At 11.2069 VB/frame it is needed on **0 of 29**. That is welcome
but should not be read as the problem going away: the mechanism T2.11
documented is unchanged, and a build materially faster than this one may put it
back.

---

## 5. The oracle - what it bounds, and what it refuses to claim

`tools/saturn/shadow_trig_test.c`, gates `verify-shadow-trig` and
`verify-shadow-trig-mutation` (both new, both in `verify-all`).

**This oracle does not prove byte-identity and must not be read as if it did.**
Replacing a double-precision polynomial with a 4096-step table changes the
answer by construction. What the fixture does instead:

### 5.1 The trig tolerance and its derivation

The reference is **not** host libm. It is the libultra polynomial copied
verbatim out of `lib/src/math/sinf.c` and `cosf.c`, fed through the exact f32
rounding chain the caller applied (`atan2s` -> degrees -> radians), so the
comparison is against what the target really computed.

The bound is derived, term by term, before it is measured:

| Term | Value |
| --- | ---: |
| index truncation: `(u16)angle >> 4` discards up to 15 of 65,536 steps; 15 * 2pi / 65536 | 1.4381e-3 |
| `atan2_deg` divides the BAM by 65535, the table by 65536; 2pi * 32768 / (65535 * 65536) | 4.7902e-5 |
| the table holds f32 roundings of the true sine, 2^-24 relative on a value <= 1 | 5.9605e-8 |
| the libultra minimax error, and the f32 rounding of the degrees intermediate | < 1.0e-6 |
| **derived bound** | **1.4871e-3** |

`SHADOW_TRIG_TOL` is that rounded up to **1.5e-3**.

**Measured, over all 65,536 s16 angles:**

```
shadow trig: 65536 angles, worst |d sin| 0.001486145 at bam 32767,
             worst |d cos| 0.001462176 at bam 16527, mean |d| 0.000458717
```

**1.486145e-3 measured against 1.4871e-3 derived** - the derivation is
confirmed to four significant figures, which is a stronger statement than
either number alone.

### 5.2 Blunders, named rather than left to the sweep

Per the owner's relaxation, catching blunders is the oracle's job and shaving
the tolerance is not. The sweep would catch all of these as "a number got big";
naming them means a failure says *which* blunder happened:

- the six cardinal angles (0, 45, 90, 180, 270, 315 degrees) checked against
  their exact values, which is where a **quadrant error**, a **sign flip** and
  an **off-by-one index** are each individually visible;
- both wrap points (`0`, `-1`, `32767`, `-32768`) and the two angles that
  bracket a single index step (`15`, `16`);
- **the degenerate floor**: `init_shadow` pins `floorTiltBam` to 0 on the
  divide-by-zero guard, so angle 0 must be exactly the identity or a flat floor
  gets a rotated shadow. Asserted exactly, not within tolerance.

### 5.3 The two bit-exact claims, proven not assumed

```
held object: 65536 s16 inputs, 0 divergences
push_clamped_int: 27942 cases over widths 0-5, 0 divergences
```

The held-object case is **exhaustive** over the entire input domain. The
`push_clamped_int` case compares the shipped function - reached by compiling
the real translation unit into the fixture, not by transcribing it - against
the pre-T2.13 loop copied verbatim, over widths 0 to 5, which includes the
widths that still take the retained general loop.

### 5.4 Stability: the property that actually matters here

A fixed small offset is acceptable to this project. Geometry that jitters, pops
or swims between frames is not. Two separate things were checked.

**Accumulation is impossible by construction.** `sins`/`coss` are a pure
function of the angle with no state, no accumulator and no feedback. The
substitution cannot drift across frames. There is nothing to measure.

**The real risk is that the table is a step function where the polynomial was
smooth**, so a sweeping floor angle now moves the shadow in discrete jumps
instead of continuously. Measured:

```
shadow stability: largest single-BAM-step discontinuity 0.001533980 at bam -32753
                  (= 0.230097 world units at shadowScale 100)
```

At Mario's shadow scale that is a worst-case **single-frame jump of 0.230 world
units, about 0.064 screen pixels** (section 6). In practice it is smaller
still, because `floorDownwardAngle` is the atan2 of a *floor triangle's* normal
and is therefore piecewise constant - it only changes when Mario crosses onto a
differently-oriented triangle, which is a place the shadow already jumped.

### 5.5 Mutations

Applied through `verify-shadow-trig-mutation`, which requires every one to make
the fixture **fail**.

| # | Mutation | Result |
| --- | --- | --- |
| M1 | `SHADOW_TRIG_TEST_MUTATE_TOLERANCE` - perturb the trig result by 2.0e-5 | **KILLED** |
| M2 | `SHADOW_TRIG_TEST_MUTATE_EXACT` - perturb the held-object Q16 result by 1 | **KILLED** |
| M3 | `SHADOW_TRIG_TEST_MUTATE_INDEX` - shift the table index by one entry | **KILLED** |
| M4 | `SHADOW_TRIG_TEST_MUTATE_QUADRANT` - swap sine for cosine | **KILLED** |

**M1 is deliberately tight.** 2.0e-5 on a measured worst case of 1.486145e-3
lands at 1.506145e-3 - just past the 1.5e-3 bound. A mutation that overshot by
an order of magnitude would prove only that a comparison exists, not that the
bound is where the fixture says it is.

M3 and M4 are the blunder mutations, and both are caught **twice**: by the
tolerance sweep, and by the named cardinal-angle cases, which report e.g.
`BLUNDER at 90 deg (bam 16384): sin 0.000000 want 1.000000` rather than a bare
threshold breach.

---

## 6. Downstream movement - what the owner could see

**Flagged prominently because this is a gate item, not a footnote: Mario's
shadow is drawn from different vertex positions than before.**

### 6.1 Vertex movement, measured

`calculate_vertex_xyz`'s real output expression evaluated both ways over a
swept corpus of tilt angle x downward angle x the nine (xCoordUnit, zCoordUnit)
pairs x five shadow scales:

| shadowScale | Worst vertex movement | Per unit of scale |
| ---: | ---: | ---: |
| 1 | 0.001474 | 0.001474 |
| 50 | 0.073700 | 0.001474 |
| **100 (Mario)** | **0.147401** | 0.001474 |
| 200 | 0.294801 | 0.001474 |
| 400 | 0.589603 | 0.001474 |

Mario's shadow scale is **100**, from
`actors/mario/geo.inc.c:1810`: `GEO_SHADOW(SHADOW_CIRCLE_PLAYER, 0xB4, 100)`.
So the worst case on the route is **0.147 world units**.

### 6.2 In screen pixels

SM64 renders with `fov = 45`, and `rendering_graph_node.c:1474-1477` documents
that the aspect ratio is not accounted for, making the *horizontal* effective
field of view 60 degrees. Over a 320-pixel-wide frame that is
`160 / (tan(30 deg) * d) = 277.1 / d` pixels per world unit at camera distance
`d`:

| Camera distance | px per world unit | Worst static offset | Worst single-frame step |
| ---: | ---: | ---: | ---: |
| 1000 (typical SM64 third-person) | 0.277 | **0.041 px** | 0.064 px |
| 600 | 0.462 | 0.068 px | 0.106 px |
| 300 (very close) | 0.924 | 0.136 px | 0.213 px |

**Sub-pixel at every realistic camera distance.** It is not zero: a shadow edge
sitting exactly on a rasteriser boundary can flip one pixel. It is not the kind
of movement that reads as the shadow being in a different place.

### 6.3 Admitted cluster set and VDP1 command count

**Admitted cluster set: unchanged, structurally.** `sm64_saturn_scene_admit()`
takes the camera pose and the 867-entry static cluster bank from
`bob_scene.h`. Shadow vertices are per-object display-list geometry and are not
an input to it. Corroborated rather than merely asserted: with these commits in
the tree, `verify-admission-hierarchy` still reports admitted-set digest
**`a99b76b99477a4ff`** and `verify-frustum-equivalence` still reports
**`0f70643abf026a13`** - both unchanged from T2.12.

**VDP1 command count: structurally unchanged, and not measured on target.** The
shadow node emits a fixed primitive count (4 or 9 vertices) regardless of where
those vertices are; only a clip or cull decision could change the count, and
`SATURN_DEMO_NEAR_CLIP=1` is active. A sub-pixel vertex move can flip such a
decision only when a vertex sits within ~0.06 px of the clip boundary.
**Confirming the on-target command count needs a `SATURN_DIAGNOSTIC_MODE=2`
build and a sub-stage capture, which was outside this task's budget. This is
stated as not measured rather than assumed equal.**

### 6.4 What to look at

If the owner wants to check this by eye: **Mario's shadow on sloped ground**,
particularly while he moves across a slope so the floor normal changes. The
claim being tested is that the shadow looks the same shape and stays put; the
predicted worst deviation is under a tenth of a pixel and should be invisible.
Nothing else in the frame is touched by 2.1, and 2.2/2.3 are bit-exact.

---

## 7. The native-math verifier - diagnosis, not repair

The brief asked for the cause of `verify_sh2_native_math.py` reporting
`helper_total = 1402` against a pinned `582`, and for it to be bounded to a
diagnosis. It is.

**Verdict: a stale contract over a real and intended closure expansion. Not an
arithmetic regression, and not a verifier artifact.**

The evidence chain:

1. **The classifier is byte-identical to the pin.** `LIBM_NAMES`, `DIV64_RE`,
   `SOFT_FLOAT_RE` and `SOFT_FLOAT_NAMES` in
   `tools/saturn/verify_sh2_native_math.py:3983-4005` are character-for-
   character what commit `24cf4169` (2026-07-29) pinned. Same root, same
   `analysis_mode = code-only`. Helper-set widening and mode drift are both out.
2. **The 582 was measured against an oracle that declared zero indirect
   edges.** `sh2_native_math_sim_route_oracle_v1.txt` at that commit was six
   lines with no `INDIRECT_EDGE` at all, which made `_level_script_execute` a
   leaf and left essentially the whole simulation outside the audit. The oracle
   now declares **115**. Its SHA pin in the verifier was updated in all nine
   commits that grew it; `EXPECTED_TOTAL` was updated in **none**.
3. **A saved observation proves the scope effect directly.**
   `docs/saturn/evidence/reports/sh2-native-math-hot1-clip1-indirect-inventory-2026-08-01.json`
   is a real dump against the **same zero-edge oracle** as the pin:
   `helper_total = 599`, `closure_functions = 229`. Three days after the pin,
   on the same oracle, the number was 599 - a +17 build drift, not +820. The
   census's run has a **701-function** closure. The closure tripled.
   `_get_pos_from_transform_mtx` reads **33 then and 33 now** - an unchanged
   shared function, which is the fixed point that rules out a per-function
   arithmetic regression.
4. **The project already diagnosed and adjudicated this.**
   `sh2_native_math_goal_audit_contract_v3.txt` and `..._v4.txt` both pin
   `EXPECTED_TOTAL 700` for the corrected census, and
   `docs/superpowers/specs/2026-08-09-goal-target-native-math-audit-v3-design.md:9-13`
   states verbatim that "The remaining problem is contract scope, not target
   behavior." Commit `081c8575` (2026-08-10) deliberately excluded the v2 gate
   from release builds. **The v2/582 gate has been knowingly red since
   2026-08-10**; the census section 2a is re-discovering an adjudicated issue.
5. **Six of the seven undeclared indirect transfers are pre-existing call
   sites** that became *visible* when the closure widened, not new code. Only
   `_emit_words` (the semantic-audio policy callback, added `9a904766`) is new.
6. **The residual 700 -> 1402 is configuration, not counting.** The v4 goal
   build's label carries `feature_bits = 3` (complete_mario_animation=1,
   dynamic_actor_closure=1, semantic_audio=0); the current product is
   `feature_bits = 4` - **all three bits differ**. Neither v2 nor v4 is a valid
   comparator for this ELF.

**It does not gate this task, and it could not have rewarded it either.** The
v2 audit compares with exact equality (`verify_sh2_native_math.py:6460`,
`:4775`), so it fails at 1402 and would fail at any other number - *including a
smaller one*. It cannot signal improvement. The gate this work did have to
respect is `sh2_native_math_baseline_v1.txt` (renderer route, `HOT_CEILING 19`,
enforced unconditionally), whose rule is that a non-allowlisted (caller, helper)
pair or a higher count fails while **disappearing calls are fine**. Every change
in this task only removes helper calls on that route.

**The minimal correct fix is not small and was not attempted**, per the brief's
instruction to bound this to a diagnosis: declare the six legitimate dispatchers
in the sim oracle (a derivation judgement, not a mechanical edit, and it will
raise the total again), then re-measure and pin a v5 contract for the current
tuple using the existing v4 machinery - which requires the verifier's ~95-minute
run - then repoint `SOURCEBOOT_NATIVE_MATH_SIM_AUDIT_CONTRACT` at
`src/port/saturn/sourceboot/Makefile:106`. `EXPECTED_TOTAL 582` must **not** be
edited in place: the file is SHA-pinned at `verify_sh2_native_math.py:3975` and
the owner decision recorded in
`docs/superpowers/plans/2026-08-09-memory-residency-campaign.md:258-266` is "V2
stays frozen."

**Also worth closing out:** the baseline ELF `54d36006...` **is not on disk**
anywhere under the project root, so the direct nm/objdump diff the census
section 10 item 4 asks for is impossible without rebuilding it. The 2026-08-01
observation JSON is the best available proxy and is a good one.

---

## 8. Gates

| Suite | Result |
| --- | --- |
| `verify-shadow-trig` (new) | **PASS** |
| `verify-shadow-trig-mutation` (new) | **PASS** - 4 of 4 mutations caught |
| `verify-render-native-math` | **PASS** |
| `verify-render-native-math-mutation` | **PASS** |
| `verify-memory-map` | **RESULT = OK** |
| `verify-admission-hierarchy` | **PASS** - digest `a99b76b99477a4ff` unchanged |
| `verify-frustum-equivalence` | **PASS** - digest `0f70643abf026a13` unchanged |
| `verify-scene-admission` | **PASS** |
| `verify-saturn-hud-layout` | **PASS** |
| `verify-saturn-hud-layout-mutation` | **PASS** |
| `verify-actor-meshlets` | **PASS** |
| `verify-ztreme-frustum` | **PASS** |
| `verify-terrain-depth-bins` | **PASS** |
| `verify-vdp1-painter-chain` | **PASS** |
| `verify-vdp1-frame-bank` | **PASS** |
| `verify-render-job-runtime` | **PASS** |
| `verify-demo-render-overlap` | **PASS** |
| `verify-render-overlap-integration` | **PASS** |
| `verify-portal-windows` | **PASS** |
| `verify-visible-position-set` | **PASS** |
| `verify-audio-loop-contracts` | **PASS** |
| `verify-pcm68k-model` | **PASS** |
| `verify-softfp-bitexact` | **PASS** - 43,333,704,485 bit-exact checks, 0 failures (see the note below on how it had to be made runnable) |
| `verify-render-clusters` | **RED - pre-existing, not caused by T2.13** |

`verify-render-clusters` still fails, now at a different step from the one
commit `099ce72a` repaired: its Python helper raises
`FileNotFoundError: [WinError 2]` launching a subprocess. Neither the recipe nor
its inputs are touched by any T2.13 commit. **Filed, not fixed here** - it is
the same class of latent recipe defect T2.10 section 3 counted 22 more of.

**A second one of those was found and is worth writing down**, because it made a
gate the brief explicitly required un-runnable:
`verify-softfp-bitexact`'s first step creates its build directory with
`Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests/softfp').mkdir(...)`. Under
MSYS, `SATURN_REPO_ROOT` is the `/d/Code/...` spelling, so native Windows Python
creates **`D:\d\Code\...`** instead - a stray directory tree at `D:/d` - and
the recipe's next line `cd`s into a directory that does not exist. The gate has
therefore been failing for an environmental reason unrelated to soft-float, and
leaving litter on the disk each time. Creating the real directory by hand makes
it run; the recipe still needs the `pwd -W` treatment its own sibling lines
already carry.

---

## 9. Reference use

Standing owner instruction to reference SlaveDriver and Sonic Z-Treme was
followed before designing anything. **Reuse mode: behavior-only / doc-study.
No code was copied from either.**

| Repo | Path | SHA | License | Verified |
| --- | --- | --- | --- | --- |
| SlaveDriver Engine | `work/upstream/slavedriver-engine` | `a8986591557b6e680550d3c23970284d3b38ff8f` | GPL-3.0-or-later (`LICENSE.txt`) | 2026-08-16 |
| Sonic Z-Treme | `work/upstream/sonic-z-treme` | `cff75451c1616aac1236fc2b44223902b55c706b` | GPLv3 text, README adds contradictory non-GPL terms - treat as unreliable | 2026-08-16 |
| Jo Engine | `work/upstream/joengine` | `556d081146211b6a1cfa6591d70f9487d406758b` | BSD-3-Clause | 2026-08-16 |

**The decisive finding is a negative one: neither reference contains a trig
implementation to copy.** Both delegate to Sega's proprietary binaries -
SlaveDriver to SBL `MTH_Sin`/`MTH_Cos` (`UTIL.H:4`, and it overrides only
`MTH_Mul`, at `UTIL.H:85-96`), Z-Treme to SGL `slSin`/`slCos`
(`Compiler/SGL_302j/INC/SL_DEF.H:865-867`). Neither table is in either tree.

What they did supply, and it is the load-bearing item:

- **SGL's documented contract** (`Documentation/DOC/210A_US/MATH.TXT:326-336`):
  `slSin` takes a 16-bit BAM, **discards the low 4 bits**, returns Q16.16, and
  the doc names the resulting accuracy loss as a known limitation. That is
  bit-for-bit the same index convention as SM64's `sins()`, and it is the
  precedent that Sega shipped exactly this quantisation for commercial Saturn
  3D. Section 2.1's argument rests on it.
- **SGL also exposes `slRotYSC(FIXED sn, FIXED cs)`** (`SL_DEF.H:655`) - build a
  rotation from a *pre-fetched* sin/cos pair. Noted for future work that wants
  to hoist lookups out of a matrix constructor.
- **Jo Engine's fallback was examined and rejected**: `jo_fixed_sin`
  (`jo_engine/math.c:146-164`) computes `sqrt(1 - cos^2)` from a Taylor cosine
  and carries a standing `@bug` about some angles (`jo/math.h:32`). Strictly
  worse than what this port already had.

Files inspected: `slavedriver-engine/UTIL.H`, `UTIL.C`, `SQRTTAB.H`,
`SRCUINS.C`; `sonic-z-treme/.../SL_DEF.H`, `ZTE/ZT_RENDERING.c`,
`Documentation/DOC/210A_US/MATH.TXT`; `joengine/jo_engine/math.c`,
`jo_engine/jo/math.h`.

---

## 10. Identity and hashes

Same 27-variable invocation as `sprint1-stage1-link-smoke.md` and T2.2-T2.12
(pool 208), `SATURN_DIAGNOSTIC_MODE=0`, via
`tools/saturn/with-msys-toolchain.ps1` to MSYS `sh --noprofile --norc -l`,
sourcing `.yaul.env`, then `unset COMPILER_PATH`. **All tracked source was
committed before the build ran** and the tree stayed frozen for its duration.
One build attempt, **exit 0**. The g15 staleness cascade did not fire and no
repair was needed.

Sealed identity **`id-a61d5203793986e7`** (`effective_config_sha256`
`a61d5203793986e719cd15beaa57faab6c33431172f47be23432a1f9a663f70d`), label
`feat001-pipe4-l9-a1-route0-replay1-live1-boot600-cam0v3-diag0-cart32-stage8-hot1-clip1-bsp1-poly2-frag0-cfga61d52037939`,
producing revision `7b30011b`.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `obj/sm64-saturn-sourceboot-e2.elf` | 9,991,816 | `bc49eafa8bc9695ec708a7ffa24884be7084be10b0ad7a1bd1ca87bdbe6ccbad` |
| `sm64-saturn-sourceboot-e2.iso` | 5,179,392 | `df3424b422c838fe131408c14b818b1d6bbbd17744021cecb9b3aa155926e5a5` |
| `sm64-saturn-sourceboot-e2.cue` (88 B, not identity-bearing) | 88 | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` |
| `saturn-release-manifest-v1.json` | 3,270 | `b5e4f36572f38ef276713a6310cd57af9bc80159cea75520ef592e799b7aa566` |
| `identity_sha256` (target build record) | 500 | `ac6b9d88de3a14a5c20c95c6ade410321ef0bc3cb11306e4de4ebcf0c6697197` |

The ELF grew by **484 bytes** against T2.12's 9,991,332 - the two s16 fields
and the table indexing, against the deleted double-precision call sites.
`verify-memory-map` **RESULT = OK**.

Preservation (mandatory rule): the accepted candidates
(`id-05046d9d5d8a5593` and the T2.10 product set) copied to
`releases/2026-08-16_t2_13-pre-build/` **before** the build ran; this build's
four artifacts to `releases/2026-08-16_t2_13-product/id-a61d5203793986e7/`.
No diagnostic build was made, so no profile-JSON `diagnostic_mode` flip was
needed and none was made: `git diff` on
`tools/saturn/profiles/sourceboot-bob-demo-v1.json` is empty and it still reads
`"diagnostic_mode":0`.

---

## 11. Honesty - what is wrong with these numbers

1. **The profile is burst-sampled, not uniformly sampled.** Samples within one
   burst of 400 instructions are serially correlated; samples across the 600
   bursts are not. A helper that runs in a tight cluster is over- or
   under-represented depending on where the bursts land. The class-level shares
   are robust; a single symbol's share at the 0.1% level is not.
2. **The two CPUs are sampled 50/50 by burst count**, not in proportion to
   their real work. That inflates the idle share of whichever CPU idles more,
   so the 70.21% figure is an upper bound on true idleness, not a point
   estimate. The relative ranking *within* a CPU is unaffected.
3. **"Never executed" in section 1.3 means "not observed in 240,000 samples"**,
   not "provably dead". A function that runs rarely can hide in the gaps
   between bursts. The claim is strong for `_guRotateF` (structurally
   unreachable from this route's callers) and weaker for the rest.
4. **The -0.9310 VB/frame is the whole-frame figure and is trustworthy; the
   phase attribution is not fully.** Section 4 says why: `construction` and
   `simulation` fall by the identical -0.9655, and only the `construction` half
   has a mechanism.
5. **The FPS gain is larger than the profile's 2.73% of sampled cycles would
   naively predict** (-0.93 of 12.14 VB is 7.7% of the frame). The two are
   reconcilable - 70% of sampled cycles are idle, so removing work from the
   master's critical path shortens the frame by much more than its share of
   *sampled* cycles - but this task did not prove that mechanism, and the gap
   should be treated as unexplained rather than as confirmation.
6. **VDP1 command count was not measured on target** (section 6.3). Structural
   argument only.
7. **The pixel figures in section 6.2 assume a camera distance.** The
   world-unit figures are measured; the pixel conversion is arithmetic on a
   stated projection and a chosen `d`. The route's actual camera distance was
   not extracted.
8. **`verify-softfp-bitexact` had to be made runnable by hand** (section 8).
   Its recipe defect is environmental and pre-existing, but the result was
   obtained after a manual `mkdir`, not from a clean invocation.
9. **This is not an owner observation.** It is a headless capture. The product
   gate is still a CUE the owner sees and hears - and this build changes
   something the owner can, in principle, see.

---

## 12. What this leaves

### For the owner

1. **`id-a61d5203793986e7` is a new candidate for the owner gate**, at 5.3538
   FPS / 11.2069 VB per frame - the first build in this sprint past A9A on both
   numbers, with a 1% low of 5.0.
2. **It is the first candidate that is not byte-identical to its predecessor in
   emitted geometry.** Mario's shadow moves by up to 0.147 world units (under
   a tenth of a pixel). Section 6.4 says what to look at.
3. The T2.11 cadence-rail margin, which T2.12 flagged as trending badly, is
   **not needed on any interval** in this build.

### For the next task

1. **Re-read section 1.5 before planning more arithmetic work.** 70% of sampled
   cycles are idle and the largest remaining lever looks like the master/slave
   handoff, not float.
2. **Of the census's 1,827 hot-reachable soft-float sites, this task removed
   the double-precision ones on the live path and nothing else.** What remains
   is roughly 4.6% of sampled cycles (soft-float plus its converts and
   support), and the measured leaders inside it are, in order:
   `_saturn_geo_enter_object` (26 sampled `___mulsf3` entries plus 26
   `___addsf3`), `_calculate_vertex_xyz`'s *remaining* float arithmetic (23
   `___mulsf3` - the surrounding expression was left alone, only the trig was
   cut), `_vec3f_normalize` and `_vec3f_cross` (14 and 12), and
   `_find_floor_from_list` (17 `___floatsisf`).
3. **`_saturn_geo_enter_object` is now the top float caller and has a direct
   template**: `_saturn_geo_enter_camera` and
   `_saturn_geo_enter_ortho_projection` are already fully Q16 with zero float.
4. **The census's ranked items 2 and 5 should be deprioritised on the evidence
   of section 1.3** - `_sm64_saturn_audio_spatial_quantize`,
   `_find_wall_collisions_from_list`, `_evaluate_cubic_spline`,
   `_rotate_triangle_vertices` and `_envfx_update_snow_normal` are static
   phantoms on this route.
5. **The native-math v5 re-pin (section 7)** is a real, bounded, ~95-minute job
   that nobody has done, and until it is done that gate cannot report anything.
6. **Two recipe defects filed, neither fixed**: `verify-render-clusters`'s
   subprocess failure and `verify-softfp-bitexact`'s MSYS path bug, the latter
   also creating a stray `D:/d/Code/...` tree.
7. **`_actor_meshlet_live_depth_bounds` at 3.662% is the single largest
   non-idle symbol in the whole profile** and is not float at all. It is the
   obvious next target on measured evidence, and no census item names it.

---

## 13. Reproduction

```
# Dynamic profile (no build, ~90 s including boot):
python tools/saturn/capture_softfloat_profile.py \
  --ymir <ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe> \
  --ipl <sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin> \
  --game <absolute .cue> --elf <absolute .elf> \
  --nm <work/yaul-install/bin/sh-elf-nm.exe> \
  --output <report.json> \
  --phases 1 --phase-vblanks 1800 --bursts 600 --burst-steps 400 --gap-vblanks 1

# Host gates (no build, no emulator). NOTE: run these through
# tools/saturn/with-msys-toolchain.ps1 -- from a plain shell the host compiler
# lands on C:\WINDOWS for its temporaries and dies.
#   make -f Makefile.saturn.mk verify-shadow-trig verify-shadow-trig-mutation
# verify-softfp-bitexact additionally needs
#   mkdir -p build/saturn/host-tests/softfp
# first, because its own mkdir step writes to D:/d/Code/... under MSYS.

# Product build: the 27-variable invocation from sprint1-stage1-link-smoke.md
# with SATURN_OBJECT_POOL_CAPACITY=208 and SATURN_DIAGNOSTIC_MODE=0.
# FREEZE ALL TRACKED SOURCE FIRST.

# Cadence: summarize_cadence only (T2.11). Never hand-computed.
python tools/saturn/capture_sourceboot_throughput.py \
  --ymir <...> --ipl <...> --game <absolute .cue> --elf <absolute .elf> \
  --release-manifest <saturn-release-manifest-v1.json> \
  --output docs/saturn/evidence/reports/sprint2-t2_13-throughput-30events.json \
  --startup-vblanks 4096 --max-vblanks 3600 --presentation-events 30 --timeout 1800
```
