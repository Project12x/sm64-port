# W2 — finish fixed-point on the master: the census, then one cut

- Date: 2026-08-22. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `70703228`.
- Task: re-derive the soft-float **call-site** ranking with the exact master
  census, confirm or correct the plan's figures, and convert what the census
  actually says is worth converting.
- Baseline (owner-accepted): `id-57fb7312058b7f2e`, **7.0445 FPS mean /
  6.6667 median / 6.6667 1% low / 8.5172 VB per frame** at route tick 30,
  caches off, `closure_clean: true`.
- Candidate: `id-28b8cb0e531f8476`, **7.3109 mean / 7.5000 median / 6.0000 1%
  low / 8.2069 VB per frame** at route tick 30 — **+3.78% mean, +12.5%
  median, −10.0% on the 1% low**. At route tick 150, **7.2803 / 7.5000 /
  6.6667 / 8.2414** against a re-measured baseline of 6.9600 / 6.6667 /
  6.6667 / 8.6207 — **+4.60% mean, +12.5% median, 1% low unchanged**.
  `closure_clean: true`.
- **No emulator GUI.** Six headless Ymir boots (one contiguous census, three
  cadence captures, two route timelines), on-target identity **MATCH** on
  every one.

---

## 0. The one-paragraph version

**The plan's headline lead is not a soft-float call site at all, and one
function it does not mention is worth more than everything else combined.**
`_sm64_saturn_ztreme_frustum_aabb` contains **zero floating point** — checked
in the linked ELF, not in source — so "finish fixed-point" cannot be applied
to it; and its ~0.649 VB/frame figure **does not survive re-derivation**,
because the symbol is bursty in exactly the way W7 warned about and its
per-frame number moves by 1.8× with window length while its per-call cost
sits dead flat at 981.5 cycles. The re-derived census, one contiguous
**4.83-frame** window with new exact call-site attribution, ranks **`_sqrtf`
first by a factor of three**: 7.894% of master cycles in the soft-float it
causes plus 0.568% in its own body — **8.46% of the master, 326,395
cycles/frame, 0.729 VB/frame** — because each call ran `value / estimate`
four times, and on a CPU with no FPU that is four `__divsf3`, four `__addsf3`
and four `__mulsf3`. Replacing it with a 24-step integer digit-by-digit
square root measured **+3.78% at tick 30 and +4.60% at tick 150**. The result
is *the* correctly rounded IEEE-754 square root on all 2^32 bit patterns —
verified exhaustively, worst deviation **0 ulp** — and moves at most **1 ulp
(1.192e-07 relative)** against the four-iteration version it replaces, which
was itself never correctly rounded. **Downstream movement measured: none.**
Mario's action, position, velocity and facing agree between the two builds on
**every one of 196 complete route ticks from 0 to 199**, at the instrument's
1e-3 resolution — including every tick in both cadence windows, so the FPS
comparison is comparing the same scene. **Below `_sqrtf` the distribution is
a long flat tail and section 7 recommends stopping**: rank 2 is 2.707%, ranks
2–5 together are 7.8%, and 962 distinct functions in the linked image touch
soft-float.

---

## 1. Method, and the two corrections it applies to prior practice

Instrument: `tools/saturn/capture_idle_attribution.py`, the exact-residency
contiguous tracer. **The burst profiler is not used anywhere in this task**
(T2.20: ±2.6 pp per symbol, and it reported soft-float's trend with the wrong
sign).

```
capture_idle_attribution.py --warmup-ticks 150 --windows 1 \
  --window-steps 15000000 --slave-stride 16 --io-stride 20000 \
  --vblanks-per-frame 8.6207 --attribute-callers-of softfloat
```

**Correction 1 — window length.** 15,000,000 master instructions =
18,620,915 cycles = 41.6163 VBlanks = **4.8275 frames**, against W6a's 1.8152
and 2.0059. This is the length W7's protocol requires, and section 3 shows it
is the difference between a real number and a fabricated one.

**Correction 2 — the cadence normaliser is taken at the profile's own route
tick.** W6a normalised a **tick-150** census with **8.5172** VB/frame, which
is a **tick-30** cadence. This task measured the accepted baseline's cadence
at tick 150 itself — **8.6207 VB/frame, 6.9600 FPS mean**, 29 intervals,
identity MATCH — and used that. The correction is 1.2%; it is applied because
mismatching those is exactly how W6a's residual argument went wrong.

**New capability — exact call-site attribution.** Residency says how much
soft-float runs; it cannot say which call site to convert, and the only
instrument that had ever answered that was the discredited burst sampler's
caller edges. `--attribute-callers-of softfloat` reads PR at every entry to a
soft-float routine — where it still holds the caller's return address, the
same mechanism the existing wait-site attribution relies on — and rolls the
result up to the calling function. **It is complete, not an estimate**: the
tool's own output has attributed cycles equal to measured residency for every
callee, with nothing unaccounted. `regs.read` does not advance the machine,
so the cycle census cannot be perturbed by switching it on.

Sanity of the window itself: master idle **2.145%**, five entries forming one
block, all at `_main+0xe14` — **reproducing W7's 2.1450% exactly**, on a
different artifact (the rebuilt `id-57fb7312058b7f2e`, not W7's
`id-586944bb637a920f`).

---

## 2. Deliverable (1) — the re-derived census

### 2.1 Aggregate

Target `id-57fb7312058b7f2e`, identity probe MATCH
(`d88b17a9…23b5`), caches off, 4.8275 contiguous frames.

| | value |
| :--- | ---: |
| master cycles traced | 18,620,915 |
| master idle | 399,412 (2.145%) |
| frame | 3,857,273 cycles (8.6207 VB) |
| **soft-float total** | **4,949,390 cycles = 26.580% of master** |
| **soft-float per frame** | **1,025,249 cycles = 2.291 VB/frame** |

**Against `STATE.md`'s 2.0718 VB/frame** — the figure the plan flags as
descending from the burst profiler — the exact census says **2.291 VB/frame**.
The old number is **confirmed in magnitude and corrected upward by 10.6%**.

**Against W6a's 24.87% share:** the difference is mostly the denominator.
W6a's window carried 5.22% master idle and this one carries 2.145%; expressed
against non-idle master cycles the two are 26.24% and 27.16%, 0.9 pp apart,
which is window and phase composition. Both are reported here rather than
picking the flattering one.

Top routines (absolute, this window):

| routine | cycles | /frame | % master |
| :--- | ---: | ---: | ---: |
| `__addsf3` | 1,655,820 | 342,997 | 8.892% |
| `__mulsf3` | 1,091,780 | 226,158 | 5.863% |
| `__divsf3` | 701,991 | 145,415 | 3.770% |
| `__ashlsi3_r0` | 284,198 | 58,871 | 1.526% |
| `__gesf2` | 197,930 | 41,001 | 1.063% |
| `__lesf2` | 194,119 | 40,211 | 1.042% |
| `__lshrsi3_r0` | 183,225 | 37,954 | 0.984% |
| `__floatsisf` | 176,376 | 36,536 | 0.947% |
| `__clzsi2` | 131,236 | 27,185 | 0.705% |
| everything else | 333,715 | 69,131 | 1.792% |

`__subsf3` is 6.0 cycles per call over 4,942 calls: the `compiler-rt` thunk,
with its work billed to `__addsf3`, exactly as W6a described. Every
double-precision routine together is 0.75%.

### 2.2 The ranking that matters — by **call site**

Soft-float cycles caused, attributed to the calling function. Rows that are
themselves soft-float routines calling their own helpers (`__addsf3`,
`__floatsisf`, `__clzsi2`) are excluded; they are library-internal, not call
sites to convert.

| rank | caller | cycles in window | % master | cycles/frame | VB/frame | calls |
| ---: | :--- | ---: | ---: | ---: | ---: | ---: |
| **1** | **`_sqrtf`** | **1,469,928** | **7.894%** | **304,491** | **0.6805** | 12,421 |
| 2 | `_saturn_geo_enter_object` | 504,018 | 2.707% | 104,406 | 0.2333 | 5,713 |
| 3 | `_detect_object_hitbox_overlap` | 394,986 | 2.121% | 81,820 | 0.1829 | 4,255 |
| 4 | `_dist_between_objects` | 293,849 | 1.578% | 60,870 | 0.1360 | 3,797 |
| 5 | `_calculate_vertex_xyz` | 262,595 | 1.410% | 54,396 | 0.1216 | 3,610 |
| 6 | `_find_floor_from_list` | 178,534 | 0.959% | 36,983 | 0.0827 | 3,060 |
| 7 | `_round_float` | 159,314 | 0.856% | 33,001 | 0.0738 | 1,846 |
| 8 | `_find_wall_collisions_from_list` | 120,375 | 0.647% | 24,935 | 0.0557 | 1,835 |
| 9 | `_saturn_mtxq_refresh_float_mirror` | 102,156 | 0.549% | 21,161 | 0.0473 | 9,543 |
| 10 | `_make_shadow_vertex` | 94,109 | 0.505% | 19,494 | 0.0436 | 1,560 |
| 11 | `_is_point_within_radius_of_mario` | 94,071 | 0.505% | 19,486 | 0.0436 | 1,300 |
| 12 | `_calc_new_obj_vel_and_pos_y` | 62,662 | 0.337% | 12,980 | 0.0290 | 952 |
| 13 | `_guOrthoF` | 54,349 | 0.292% | 11,258 | 0.0252 | 741 |
| 14 | `_sm64_saturn_audio_spatial_quantize` | 49,872 | 0.268% | 10,331 | 0.0231 | 585 |

`_sqrtf`'s own body is a further **105,779 cycles (0.568%)**, attributed to
whoever called `sqrtf`. **Total attributable to square roots: 1,575,707
cycles = 8.462% of master = 326,395 cycles/frame = 0.7294 VB/frame**, over
**1,011 calls in 4.83 frames = 209 calls/frame at 1,559 cycles each.**

Who calls it, from the same attribution: `_detect_object_hitbox_overlap`
(425 of the 1,011), `_cur_obj_update` (243), `_load_object_collision_model`
(102), `_vec3f_normalize` (60), `_calc_new_obj_vel_and_pos_y` (31),
`_init_shadow` (40), and a tail. **All simulation, none render.**

### 2.3 The shape of the distribution — this is the result, not a preamble

Rank 1 is **2.9× rank 2**. Ranks 2–5 together are 7.8% of master; ranks 6–14
together are 4.9%; each individual row below rank 5 is under 1%.

The static picture agrees and is worse. Scanning the **linked ELF**
(`--gc-sections` is live, so the symbol table is the authority) for
literal-pool references to soft-float entry points finds **3,482 references
across 962 distinct functions**, and the top 20 functions carry only 13.6% of
them. This is not a small set of hot call sites; it is the SM64 gameplay
codebase, which is float-native throughout.

**The Saturn render path is already essentially float-free.** Thirty
render-side functions hold **132 of the 3,482** static references. The
remaining ~96% are `mario_*`, `camera`, `bhv_*`, collision and audio — which
is **W6b's** territory ("Q16 the simulation"), not a render workstream. Two
of the plan's own leads confirm it: `saturn_geo_enter_object` is already Q16
for its matrix work, and `sm64_saturn_float_to_q16` / `q16_to_float` are
already pure bit manipulation with no soft-float in them at all, which is why
`_saturn_mtxq_refresh_float_mirror` — 16 conversions per call, 9,543 calls in
the window — costs only 0.549%.

---

## 3. Deliverable (2) — does the frustum-aabb figure survive? **No, twice
over.**

### 3.1 It is not a soft-float call site

`_sm64_saturn_ztreme_frustum_aabb` lives at `0x0607a80c`, 0x84c bytes, in the
sealed baseline ELF. Disassembled in full, it references **exactly two
symbols**: `_support_radius_q16` and `_scaled_limit`. **No soft-float routine
appears anywhere in its body.** The source
(`src/port/saturn/gpl/ztreme_frustum.c`) is `int32_t`/`int64_t` throughout.

So W2's stated headline lead **cannot be converted to fixed point, because it
already is fixed point.** Whatever it costs is a 64-bit-integer and
call-count question, not an arithmetic-representation one. It is a real
optimisation target; it is not this workstream's.

A free finding from the same window: **`_scaled_limit` records 0 cycles.**
T2.10 item 3's cross-multiplied fast path is therefore taken on **100%** of
calls on this route, and the hardware-divider fallback never executes at all.

### 3.2 The per-frame figure is an artifact of window length

Each `frustum_aabb` call produces exactly four contiguous residency runs
(entry plus three resumes after its three `_support_radius_q16` calls), so
runs÷4 counts calls — and `_support_radius_q16`'s own run count confirms it
at 3× that, independently.

| window | frames | runs | calls | cycles | **cycles/call** | cycles/frame | VB/frame |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| W6a baseline `e8720d58` | 1.8152 | 1,025 | 256 | 251,338 | **981.8** | 138,463 | 0.310 |
| W6a candidate `586944bb` | 2.0059 | 2,049 | 512 | 502,739 | **981.9** | 250,631 | 0.560 |
| **this task** `57fb7312` | **4.8275** | **4,100** | **1,025** | **1,006,004** | **981.5** | 208,390 | **0.466** |

**This is precisely the signature the task asked me to test for.** The
absolute per-call cost is flat to four significant figures across three
windows spanning 2.7× in length, while the per-frame number moves by **1.8×**.
The work is one cull burst of ~256 calls per frame; a 1.8-frame window catches
one burst, a 2.0-frame window catches two, and the per-frame normalisation is
meaningless at those lengths.

**Verdict on ~0.649 VB/frame: not reproducible as a single number.** The
honest statement is a range, and which end you land on depends on two choices
that were never stated:

| what is counted | by traced VBlanks | by pass count (1 burst/frame) |
| :--- | ---: | ---: |
| `frustum_aabb` body only | **0.466 VB/frame** | 0.562 VB/frame |
| body + `_support_radius_q16` | **0.590 VB/frame** | 0.714 VB/frame |

0.649 sits inside the range only if `_support_radius_q16` is counted *and*
one burst per frame is assumed. The 4.83-frame window observed **4 bursts in
4.83 frames** (0.83/frame), consistent with one per frame truncated at both
window edges but not proving it. **Do not quote a three-decimal per-frame
figure for this symbol; quote cycles per call (981.5) and calls per frame.**

### 3.3 And T2.10's history stands

Item 3 cross-multiplied to avoid four hardware 64/32 divisions per call and
**measured +0.075 VB worse**, because the SH-2 divider overlaps with pipeline
work while the multiplies serialise. Nothing here contradicts that; the
present measurement adds that the divided path it kept as a fallback is now
dead code on this route. Any future work on this function must not price SH-2
costs serially.

---

## 4. Deliverable (3) — what landed

**One change: `src/port/saturn/compat/sqrtf.c`.** The rest of the census is a
long tail and section 7 recommends against chasing it.

The pre-W2 body was the port's own compat shim, not libultra:

```c
estimate.u = (estimate.u >> 1) + 0x1FC00000U;
for (uint8_t iteration = 0; iteration < 4; iteration++)
    estimate.f = 0.5f * (estimate.f + value / estimate.f);
```

**Root cause of the waste:** nothing about a square root needs floating-point
division. The four divides existed only because the seed and the iteration
were expressed in float; on a CPU with no FPU that is four `__divsf3`, four
`__addsf3` and four `__mulsf3` per call, measured at 1,559 cycles.

The replacement halves the exponent by arithmetic on the exponent field and
takes the significand from a **24-step restoring (digit-by-digit) integer
square root** of `significand << 23`, a 48-bit radicand split at its 32-bit
boundary — pairs are aligned on even bit positions and 32 is even, so no pair
straddles it, and the whole loop is 32-bit with no 64-bit helper linked.
**Every shift count is a compile-time constant**, deliberately: the SH-2 has
no barrel shifter and a variable shift is a libgcc call, which is what the
census shows `__lshrsi3_r0` and `__ashlsi3_r0` costing 2.5% between them.

Symbol sizes in the linked ELFs: `_sqrtf` 0x80 → **0x114** (128 → 276 bytes).
`___addsf3`, `___mulsf3` and `___divsf3` are byte-for-byte unchanged in size,
as expected — nothing else was touched.

Per plan section 2a this is a **port**: the algorithm — Newton's method on a
seed, then a rounded significand — is not preserved, but the *function* is,
and acceptance is by bounded divergence against the implementation replaced.

---

## 5. Tolerance, worst case, and mutations

New gates `verify-sqrtf-fixed` and `verify-sqrtf-fixed-mutation`, both in
`verify-all`. Fixture `tools/saturn/sqrtf_fixed_test.c` compiles **the
shipped translation unit itself**, renamed at the include so GCC's `sqrtf`
builtin cannot fold the call to the host FPU and test nothing — which it did
on the first attempt, and is recorded here because it is the standard way a
fixture ends up vacuous.

**The tolerance is stated as exact equality, not as a bound.** The shipped
`sqrtf` must equal the correctly rounded IEEE-754 single-precision square
root — the reference being double `sqrt` narrowed to float, which is
correctly rounded for float radicands because 2·24+2 ≤ 53.

```
swept 4294967296 bit patterns at stride 1
correctly rounded: 2139095040 exact, worst deviation 0 ulp
divergence from the pre-W2 float body: 535863898 results moved
  normal inputs: 532785701 moved, worst 1 ulp, worst relative 1.192093e-07 (0x00800003)
  including subnormals: worst relative 9.926344e-01 (0x00000001)
RESULT: shipped sqrtf is the correctly rounded IEEE-754 square root on every
        input, and every named blunder case holds.
```

**Exhaustive over the entire 2^32 domain. Worst case 0 ulp, not "mean small".**

**Worst-case divergence from what shipped: 1 ulp, 1.192093e-07 relative**, on
normal inputs — 12.4% of the whole domain moves, which is 25.0% of normal
inputs. It cannot be smaller: the four-iteration version was itself never
correctly rounded, differing from the true result by 1 ulp on a quarter of
normal inputs, so *no* exact implementation reproduces it. The 9.93e-01
figure is subnormal inputs only and is the **old** code being wrong there —
its seed does not converge in four iterations below the normal range, and it
returned 2.03e-21 for the smallest subnormal against a true 1.05e-22.

**Frame-to-frame stability** needs no measurement and is asserted by
construction: `sqrtf` is a pure function of its argument with no state, no
accumulator and no feedback, so its error cannot drift or accumulate between
frames. The risk that remains is a fixed per-call offset, which is what the
1-ulp bound above covers, and section 6 measures its consequence directly.

**Named blunder cases, asserted exactly** rather than left to the sweep:
61 powers of four across the exponent range (exponent-halving sign and
off-by-one, and parity, since consecutive powers of two alternate parity);
perfect squares; both parities at 2.0 and 8.0; the min/max subnormal and
min/max normal range boundaries; the significand-renormalisation carry;
`+inf`; sNaN quieting; monotonicity across the whole ordered domain; and the
non-IEEE contract callers depend on — a negative input returns `+0.0f`.

**Mutations — five, all caught:**

| # | Mutation | Result |
| --- | --- | --- |
| M1 | `SQRTF_TEST_MUTATE_TOLERANCE` — perturb the result by **1 ulp** | **KILLED** |
| M2 | `…MUTATE_SQRTF_TRIAL` — trial subtrahend `2R+1` instead of `4R+1` | **KILLED** |
| M3 | `…MUTATE_SQRTF_RADICAND` — radicand scale off by one (`<<22`) | **KILLED** |
| M4 | `…MUTATE_SQRTF_PARITY` — fold the exponent on the wrong parity | **KILLED** |
| M5 | `…MUTATE_SQRTF_ROUND` — truncate instead of rounding to nearest | **KILLED** |

**M1 is the tightest mutation that can exist against an exact-equality
bound.** One ulp is the smallest representable perturbation; if that survived,
the tolerance would be decorative. **M2 is not hypothetical** — it is the bug
this implementation actually had on its first draft, caught by the same sweep
before anything was built.

---

## 6. Downstream movement — measured, and it is zero

**Flagged prominently because it is the thing that could have moved what the
owner sees, and because the FPS comparison depends on it.** `sqrtf` feeds
gameplay physics — hitbox overlap, object updates, collision-model loading,
`vec3f_normalize` — so a 1-ulp change can in principle flip a comparison and
diverge the replay, which would also mean the two builds' cadence windows are
not observing the same scene.

`tools/saturn/capture_route_timeline.py` on both sealed builds, walking route
ticks 0–200, sampling `gMarioState` every 3 VBlanks:

| | result |
| :--- | :--- |
| complete route ticks compared (0–199) | **196 of 196** |
| ticks where Mario's **action** differs | **0** |
| ticks where Mario's **face angle** (exact `s16`) differs | **0** |
| ticks where Mario's **position** differs | **0** |
| ticks where Mario's **velocity** differs | **0** |

Sampled positions at ticks 30, 60, 90, 120, 150, 179, 180 and 196–199 are
identical value for value, `-6558.0 → -4796.266`. **Both cadence windows
(ticks 30–59 and 150–179) are inside the identical span**, so the FPS numbers
in section 7 compare the same scene on both builds.

**Stated honestly:** the instrument rounds position to 1e-3 world units and
velocity to 1e-4, so this is "identical at 1e-3 resolution", not
bit-identical. Action and face angle are integers and are exactly equal. The
one apparent difference, at tick 200, is the walk's terminating sample: only
one sample exists at that tick on each build and the candidate's row still
carries tick 199's state, so it is a sampling-boundary artifact, not a
divergence — ticks 196–199 agree exactly.

---

## 7. FPS, plainly, including the regression

`summarize_cadence` only. 30 presentation events / 29 intervals per run,
caches off, on-target identity **MATCH**, `presentation_generation_delta == 1`
on every interval, `status: complete`, **0** cadence-unstable retries on every
run. Both arms warmed to the same route tick and observing the same route span.

| Route span | | baseline `id-57fb7312058b7f2e` | candidate `id-28b8cb0e531f8476` | Δ |
| :--- | :--- | ---: | ---: | ---: |
| **30 → 59** | FPS mean | **7.0445** | **7.3109** | **+3.78%** |
| | FPS median | 6.6667 | 7.5000 | +12.50% |
| | FPS 1% low | 6.6667 | **6.0000** | **−10.00%** |
| | VB/frame | 8.5172 | 8.2069 | −0.3103 |
| | field distribution | 8:14 9:15 | 8:25 9:2 **10:2** | |
| **150 → 179** | FPS mean | 6.9600 | **7.2803** | **+4.60%** |
| | FPS median | 6.6667 | 7.5000 | +12.50% |
| | FPS 1% low | 6.6667 | 6.6667 | unchanged |
| | VB/frame | 8.6207 | 8.2414 | −0.3793 |
| | field distribution | 8:11 9:18 | 8:22 9:7 | |

The tick-30 baseline row is the accepted baseline's own recorded figure; the
tick-150 baseline row was measured in this task and reproduces W7's tick-150
number for the same-content build to four decimal places (6.9600, 8.6207,
distribution 8:11 9:18).

**The 1% low regression at tick 30 is real and is stated as such.** The
candidate produces two 10-VBlank intervals there where the baseline produced
none; 60/10 = 6.0 FPS. The mean and median both improve substantially at the
same position, and at tick 150 the 1% low does not move at all, so this is two
slow frames in one 29-interval window rather than a systematic loss of floor —
but it is a measured regression on the metric the owner feels as smoothness,
and it should be re-measured before this build is promoted.

**Re-measured — see `w2-onepercent-low-recheck.md`.** It is reproducible (the
capture is bit-deterministic; 5 repetitions per arm returned identical
sequences), the long frames are route-locked, and the sign of the delta
depends on where the 29-frame window sits: worse at 4 of 10 swept start ticks,
unchanged at 3, and **better at 3** — including **+22.2% at tick 210**, where
the baseline itself produces an 11-VBlank frame and the candidate's worst is 9.
Over route ticks 30–248 the candidate's worst frame is *shorter* than the
baseline's. The two 10-VBlank intervals quoted above sit at route ticks 31 and
52, and are the same two frames every time. Matched ≥4-frame idle censuses at
tick 30 show they are the master **waiting**, not working: baseline master
idle `0.000000`, candidate 0.2108 VB/frame, against a cadence excess of
0.2069 VB/frame.

**Conversion rate.** Square roots cost 326,395 cycles/frame (0.729 VB) before.
The measured frame shortened by 0.3793 VB at tick 150, so the integer square
root costs about 0.35 VB/frame — roughly **750 cycles per call against 1,559**,
a 2.1× reduction, converting at close to full face value. That is consistent
with W7's 80–100% planning figure and with the master being essentially
saturated. **No absorption discount was applied and none was needed.**

Caches-on was not measured. One basis only; the two are never mixed.

---

## 8. Gates

| Gate | Result |
| :--- | :--- |
| **`verify-sqrtf-fixed`** (new, exhaustive, 4,294,967,296 inputs at stride 1) | **PASS — worst deviation 0 ulp** |
| **`verify-sqrtf-fixed-mutation`** (new, 5 mutations) | **PASS — all 5 caught** |
| `verify-render-native-math`, `-mutation` | **PASS** (mutation caught) |
| `verify-mtxq-ctors`, `-mutation` | **PASS** (mutation caught) |
| `verify-memory-map` | **RESULT OK** — `___end = 0x060FAAE8`, `hwram_remaining = 0x5518` (required ≥ `0x1F00`), `lwram_remaining = 0x16A20` (floor ≥ `0x4000`) |
| `verify-shadow-trig`, `-mutation` | PASS |
| `verify-runtime-contracts` | PASS |
| `verify-terrain-clip` | PASS |
| `verify-ztreme-frustum` | PASS |
| `verify-frame-pipeline` | PASS |
| `verify-vdp1-frame-bank` | PASS |
| `verify-vdp1-user-clip`, `-mutation` | PASS |
| `verify-gouraud-transfer` | PASS |
| `verify-ir-transform` | PASS |
| `verify-hot-promotion` | PASS |
| `verify-saturn-object-pool-probe-contract` | PASS |
| `verify-softfp-bitexact` (full, 43.3 G checks) | **PASS, 0 failures** — see §8.1 |

`___end` moved by 0x80 and `hwram_remaining` fell from `0x5598` to `0x5518`
— 128 bytes, the growth of `_sqrtf` — against a required floor of `0x1F00`.

Not run, known pre-existing failures per the standing list:
`verify-render-clusters`, `verify-sourceboot-presentation-boundary`,
`verify-vdp1-transfer-pipeline`, `verify-render-snapshot-bank`,
`verify-graph-q16-contract`, `test_gen_actor_identity_registry.py`,
`verify-tools`, `verify-bob-bsp-header`.

### 8.1 `verify-softfp-bitexact`

This task changes no soft-float source: `third_party/gcc-soft-fp`,
`third_party/llvm-compiler-rt` and both `SOFTFP_FAST_FUNCS` lists are
untouched, and the linked `___addsf3`/`___mulsf3`/`___divsf3` are identical in
size to the baseline's. The gate was nonetheless run to completion at shipped
defaults (`SOFTFP_TEST_PAIRS=12000000`, `SOFTFP_TEST_STRIDE=1`) rather than
argued away. Full log: `w2-softfp-bitexact.txt`.

```
total bit-exact checks:            43333704485
of which NaN-result cases:         97742
of which out-of-range int casts:   6287831620
failures:                          0

RESULT: soft-fp matches the host IEEE-754 reference bit for bit across every
        input class.
```

**43,333,704,485 checks, 0 failures** — the same volume W6a obtained, unchanged
by this task.

---

## 9. Identity and hashes

| | baseline | candidate |
| :--- | :--- | :--- |
| identity | `id-57fb7312058b7f2e` | **`id-28b8cb0e531f8476`** |
| `identity_sha256` | `d45cbc253c7a9dbf…b7b3c5` | `2c7d54085dd94b57…d405ea` |
| `effective_config_sha256` | `57fb7312058b7f2e…35bb95` | `28b8cb0e531f8476…a022c` |
| ELF SHA-256 | `4a287b769d94978007840ca08d2a1ae594aa1e3a287e5cad2c3574891779b66b` | `5f60ba879918893bfec7eea8fbd10572f42330a8f90b73859e7e403b61c91c63` |
| ELF size | 9,983,384 | 9,984,436 (+1,052) |
| ISO SHA-256 | `fc007092b2429ff9844dd34fb1b4874c4ddac97b6bccdb1250a62a38b1d68f6e` | `4c14dd0efbda96a18c60c0dc2c5e915e98f8345a759bb7cdfb6174fc438ca3d1` |
| ISO size | 5,177,344 | 5,177,344 |
| CUE SHA-256 (88 B, never identity-bearing) | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` | **identical** |
| `SOURCE.DAT` SHA-256 | `531dbda73dd5e81b95808eff175e37c1cca5c1825e9008517f2d965fc1870daf` | `e947d7f687a46c999046fe922df8f8a4e4509bd19f7f7e96446ee62b706dd172` |
| manifest SHA-256 | `2d66f95fd871abc3b2a8e74e2d39559cca50ff39cec9aa85fbb52d4ce51b2c57` | `8c31fefa47afa63678ea90e32dcbee796617b602587f597e0806e63f34cfb4fe` |
| `source_closure_sha256` | `d150814458dc1274…f00097` | `fda4dd8490af5b19…c23bf` |
| `package_set` / `target_profile` / `toolchain_attestation` | `94014e46…` / `08356912…` / `df570174…` | **all three identical** |
| executable-window probe | `d88b17a9fd1c42e57e3a0c76cd790a506b6a7e778f0a37dfc63f1850850d23b5` | **identical**, MATCH on all five captures |
| **`provenance.closure_clean`** | **`true`** | **`true`** |
| `provenance.git_revision` | `996487b9` | `da071fb7` |

**`closure_clean: true`, and the recorded revision is this task's own commit.**
All tracked source was committed before `sourceboot` was invoked; `git diff`
was empty at build time and no profile JSON was flipped (the tracked
`sourceboot-bob-demo-v1.json` already carries `diagnostic_mode: 0` and
`object_pool_capacity: 208`).

Build: 27-variable product tuple, `SATURN_DIAGNOSTIC_MODE=0`,
`SATURN_OBJECT_POOL_CAPACITY=208`, `-j12` through
`tools/saturn/with-msys-toolchain.ps1` → MSYS `sh --noprofile --norc`,
sourcing `../../.yaul.env` then `unset COMPILER_PATH`, invoked from
PowerShell. One attempt after the first was corrected — the wrapper does
**not** source `.yaul.env` itself, and without it the build dies at
`build.pre.mk:10: Undefined YAUL_ARCH_SH_PREFIX`. Artifacts preserved to
`releases/2026-08-22_0835_w2-pre-build/` before the build and staged to
`releases/2026-08-22_w2-product/id-28b8cb0e531f8476/`.

---

## 10. Evidence

| File | What |
| :--- | :--- |
| `w2-census-tick150-57fb7312.json` | the re-derived census: 4.83-frame contiguous window with call-site attribution |
| `w2-cadence-tick150-57fb7312.json` | baseline cadence at tick 150 (the normaliser the census uses) |
| `w2-cadence-tick30-28b8cb0e.json` | candidate cadence at tick 30 — the headline comparison |
| `w2-cadence-tick150-28b8cb0e.json` | candidate cadence at tick 150 |
| `w2-route-base.json`, `w2-route-new.json` | route timelines, ticks 0–200, both builds — the downstream measurement |
| `w2-sqrtf-fixed-exhaustive.txt` | the 2^32-input gate run |
| `w2-softfp-bitexact.txt` | the 43.3 G-check soft-float gate run |

Reproduce (PowerShell for Make; `$env:YAUL_INSTALL_ROOT` must be the Windows
spelling; the make itself must run under MSYS `sh` **after sourcing
`../../.yaul.env`**):

```sh
python tools/saturn/capture_idle_attribution.py --ymir "$YMIR" --ipl "$IPL" \
  --game "$G/sm64-saturn-sourceboot-e2.cue" --elf "$G/obj/sm64-saturn-sourceboot-e2.elf" \
  --nm "$NM" --warmup-ticks 150 --windows 1 --window-steps 15000000 \
  --slave-stride 16 --io-stride 20000 --vblanks-per-frame 8.6207 \
  --attribute-callers-of softfloat --timeout 9000 --output <out>.json
```

`$G` must be **absolute** (W6a §10). Window wall clock: 1,686 s.

---

## 11. Honesty — what is wrong with these numbers

1. **One route, one level, one camera path.** Everything here is
   `sourceboot-bob-demo` at ticks 30–59 and 150–179. The *structural* claims
   — frustum-aabb contains no float, `sqrtf` did four soft divides, the new
   one is correctly rounded — do not depend on the route. The 8.46% and the
   +3.78% do.
2. **The 1% low fell 10% at tick 30** and that is a measured regression, not
   noise to be explained away. It did not appear at tick 150. Two windows is
   not enough to say which is representative.
3. **The frustum per-frame range in §3.2 is a range because it has to be**,
   and the burst count per frame (0.83 observed over 4.83 frames) is not
   pinned. A window of an integer number of frames, or normalisation by pass
   count, would settle it; neither was done here because the symbol turned
   out not to be a W2 target.
4. **Downstream identity is at the instrument's 1e-3 resolution**, not bit
   level, and covers 200 route ticks. A longer route could still diverge; the
   1-ulp bound makes that possible in principle and no argument here rules it
   out.
5. **`verify-softfp-bitexact` passing is expected, not informative here.**
   Nothing in this change is inside its scope; it is reported because it is
   mandated, and because "the change is out of scope" is an argument rather
   than a result.
6. **The caller attribution reaches the immediate caller only.** A routine
   that tail-calls — `__subsf3` onto `__addsf3` — is billed to whichever site
   PR named, which for a tail call is the original application caller. That
   is the behaviour wanted, but it means "cycles caused by X" is a one-level
   claim, and callers *of* `_sqrtf` (`_detect_object_hitbox_overlap` and
   friends) carry only `sqrtf`'s body in their own rows, not its soft-float.
7. **No caches-on figure**, deliberately. Reporting one basis avoids the
   mixing error; a caches-on comparison would need both arms re-measured.
8. **The conversion-rate arithmetic in §7 assumes the census's 326,395
   cycles/frame is the whole cost removed.** It is a difference of a measured
   before against a measured after with one variable changed, but the
   ~750-cycles-per-call figure for the replacement is *inferred* from the
   frame delta, not measured directly. A post-change census would measure it.

---

## 12. What remains of the purge

**The recommendation is to stop converting call sites here, and the census is
the reason.**

- **Soft-float after this change is still roughly a fifth of the master.**
  This task removed ~0.38 VB/frame of the 2.291 VB/frame the census measured.
  What is left is a **long flat tail**: rank 2 is 2.707%, rank 5 is 1.410%,
  everything below rank 5 is under 1%, and **962 distinct functions** in the
  linked image reference soft-float across 3,482 static call sites. Converting
  that tail means converting SM64's gameplay code function by function, each
  with its own oracle and its own downstream risk, for sub-1% returns each.
  That is **W6b's** design problem — "which parts of the simulation to make
  Q16, and which to recreate" — and the plan is right to sequence it after
  W5b's actor-scaling curve says which parts are hot.
- **If one more call site is taken, it should be
  `_saturn_geo_enter_object` (2.707%, 0.233 VB/frame)** — the only
  render-path entry in the top five, the plan's own second lead, with
  `_saturn_geo_enter_camera` available as a fully-Q16 sibling template. Its
  remaining float is the inlined `obj_is_in_view`, which reads `gMatStack`
  (the derived float mirror) when `gMatStackQ` is the authoritative Q16 copy
  sitting beside it, and recomputes a per-frame-constant
  `sins(halfFov)/coss(halfFov)` once per object.
- **`_sm64_saturn_ztreme_frustum_aabb` is a live target for a different
  workstream**: 981.5 cycles per call, ~256 calls per frame, all 64-bit
  integer. Reducing the *call count* is the lever, not the arithmetic; and
  T2.10 item 3 is the standing warning against pricing its divides serially.
- **`_saturn_mtxq_refresh_float_mirror`** is only 0.549% of master in
  soft-float, but 3.014% including its own body, and it exists purely to keep
  a derived float copy of a Q16 matrix in sync for consumers that could read
  the Q16 directly. That is **W1**'s "discarded transform bookkeeping", and
  the census sizes it here at **0.260 VB/frame** for the mirror refresh alone.
- **Double precision remains `soft-fp` and should stay there.** 0.75% of the
  master; replacing it would be measurement-free work, exactly as W6a said.
- **`id-28b8cb0e531f8476` is an evidence artifact, not an accepted
  baseline.** It needs an owner-observed CUE before it replaces
  `id-57fb7312058b7f2e`, per AGENTS.md's product gate — and the tick-30 1%
  low should be re-measured as part of that. **That re-measurement is done:
  `w2-onepercent-low-recheck.md`, 50 cadence runs across 20 route positions.
  Its recommendation is to promote with the tick-30 figure documented as an
  accepted, understood, window-position-dependent artifact.** The owner-observed
  CUE is still the gate.
