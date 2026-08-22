# W2 recheck — is the tick-30 1% low regression real, or capture noise?

- Date: 2026-08-22. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `68820e2d`.
- One question only. W2 reported the candidate `id-28b8cb0e531f8476` at
  **−10.0% on the 1% low at route tick 30** against the accepted baseline
  `id-57fb7312058b7f2e`, on the strength of **two 10-VBlank intervals in one
  29-interval window**, and said it should be re-measured before promotion.
  This is that re-measurement.
- **No product build. No emulator GUI.** Both artifacts were already sealed;
  neither was rebuilt. **60 headless Ymir cadence captures across 23 route
  positions**, `summarize_cadence` only, 30 presentation events / 29 intervals
  per run, caches off. On-target identity **MATCH** on every run,
  `presentation_generation_delta == 1` on every interval of every run, **0**
  cadence-unstable retries on every run, `status: complete` on every run.
  Plus **two `capture_idle_attribution` runs** (§6.1), one per arm, ≥4-frame
  contiguous windows at the same route position, for the causal question.
  The burst profiler is not used anywhere.
- Written as its own file rather than appended to `w2-softfloat-call-sites.md`
  because it is a different question with its own method and evidence; W2 §7
  and §12 now point here.

---

## 0. The answer, in one paragraph

**It is not capture noise, because this instrument has no noise: the capture
is bit-deterministic.** Five repetitions of each arm at each of ticks 30, 150
and 210 returned the **identical 29-interval VBlank sequence** — not similar,
identical — 30 runs, 6 sequences, zero variation; and 35 overlapping candidate
windows stitch into one per-route-tick profile with **zero conflicts**. So
"repeat it several times" cannot separate signal from noise here; repeating a
deterministic process only reproduces it. **The tick-30 regression is real and
reproduces every single time.** What repetition cannot tell you, and what this
task went on to measure, is whether one 29-frame window at tick 30 is a
property of the build or of the window — and there the answer is that **the
sign of the 1% low delta depends on where the window sits**: over 13 swept
start ticks the candidate is worse at 4, unchanged at 3, and **better at 6**,
including **+22.2% at tick 210** where the baseline itself throws an 11-VBlank
frame and the candidate's worst is 9. Swept exhaustively over the whole
covered route — 191 windows per arm — the candidate's 1% low is **6.6667 in
61.3% of windows against the baseline's 53.4%, and never falls to 5.4545,
which the baseline does in 7.3%**. Its worst frame anywhere on the route is
**shorter** than the baseline's (10 VB / 166.7 ms against 11 VB / 183.3 ms),
it has **5 frames of ≥10 VBlanks against the baseline's 14**, and it is
cumulatively behind the baseline at **exactly 1 of 219 route-tick boundaries,
by one VBlank**, finishing 83 VBlanks (1.38 s) ahead over 219 frames. **The
cause is not extra work, and that is measured rather than argued**: over
matched ≥4-frame contiguous windows at tick 30 the baseline's master idle is
**exactly `0.000000`** and the candidate's is **2.5687%, 0.2108 VBlanks per
frame** — and the candidate's cadence excess over a clean 8-VBlank pattern in
the same window is **0.2069 VBlanks per frame**, agreeing to 1.9%. The long
frames *are* the waiting; the master is now sometimes idle at tick 30, waiting
on a presentation loop whose simulation credit is granted in whole field
pairs. **Recommendation: promote, with the tick-30 figure documented as an
accepted, understood, window-position-dependent artifact.**

---

## 1. Method

Instrument: `tools/saturn/capture_sourceboot_throughput.py`,
`summarize_cadence` only. The burst profiler is not used anywhere. **The T2.11
concurrency-rail phase decomposition does not close on these captures and is
not quoted anywhere in this report**; `summarize_cadence` reads
presentation-edge deltas independently of it and stands on its own.

```
python tools/saturn/capture_sourceboot_throughput.py --ymir "$YMIR" --ipl "$IPL" \
  --game "$D/sm64-saturn-sourceboot-e2.cue" --elf "$D/obj/sm64-saturn-sourceboot-e2.elf" \
  --release-manifest "$D/saturn-release-manifest-v1.json" \
  --startup-vblanks 4096 --warmup-ticks <T> --presentation-events 30 \
  --timeout 3000 --output <out>.json
```

`--startup-vblanks 4096` is load-bearing and no prior report's reproduction
block carries it: the tool's own default for that bound is **600**, and the
identity match on these builds arrives at **681** VBlanks, so the default
fails closed at the `target-identity` stage. It is a reproduction detail, not
a measurement difference — `startup_identity_attempts` is 681 in every run
here and in W2's and W6a's originals alike.

Arms, both already sealed, neither rebuilt:

| arm | identity | ELF SHA-256 (head) | path |
| :--- | :--- | :--- | :--- |
| baseline | `id-57fb7312058b7f2e` | `4a287b76…` | `releases/2026-08-22_w6a-rebuild/id-57fb7312058b7f2e` |
| candidate | `id-28b8cb0e531f8476` | `5f60ba87…` | `releases/2026-08-22_w2-product/id-28b8cb0e531f8476` |

**The one methodological correction.** W2 asked for the tick-30 capture to be
repeated. Section 2 shows repetition has zero information content on this
instrument. The window **start tick** is the only thing that varies, so this
task swept it across 23 positions and reconstructed the whole per-route-tick
frame-length profile from the overlaps (section 4). That is the measurement
W2's question actually needed.

---

## 2. Deliverable (1) — every run, and what repetition shows

Run by run. Nothing averaged, nothing omitted.

| route tick | arm | run | FPS mean | median | **1% low** | VB/frame | field distribution |
| ---: | :--- | ---: | ---: | ---: | ---: | ---: | :--- |
| 30 | baseline `57fb7312` | 1 | 7.0445 | 6.6667 | **6.6667** | 8.5172 | `8:14 9:15` |
| 30 | baseline `57fb7312` | 2 | 7.0445 | 6.6667 | **6.6667** | 8.5172 | `8:14 9:15` |
| 30 | baseline `57fb7312` | 3 | 7.0445 | 6.6667 | **6.6667** | 8.5172 | `8:14 9:15` |
| 30 | baseline `57fb7312` | 4 | 7.0445 | 6.6667 | **6.6667** | 8.5172 | `8:14 9:15` |
| 30 | baseline `57fb7312` | 5 | 7.0445 | 6.6667 | **6.6667** | 8.5172 | `8:14 9:15` |
| 30 | candidate `28b8cb0e` | 1 | 7.3109 | 7.5000 | **6.0000** | 8.2069 | `8:25 9:2 10:2` |
| 30 | candidate `28b8cb0e` | 2 | 7.3109 | 7.5000 | **6.0000** | 8.2069 | `8:25 9:2 10:2` |
| 30 | candidate `28b8cb0e` | 3 | 7.3109 | 7.5000 | **6.0000** | 8.2069 | `8:25 9:2 10:2` |
| 30 | candidate `28b8cb0e` | 4 | 7.3109 | 7.5000 | **6.0000** | 8.2069 | `8:25 9:2 10:2` |
| 30 | candidate `28b8cb0e` | 5 | 7.3109 | 7.5000 | **6.0000** | 8.2069 | `8:25 9:2 10:2` |
| 150 | baseline `57fb7312` | 1 | 6.9600 | 6.6667 | **6.6667** | 8.6207 | `8:11 9:18` |
| 150 | baseline `57fb7312` | 2 | 6.9600 | 6.6667 | **6.6667** | 8.6207 | `8:11 9:18` |
| 150 | baseline `57fb7312` | 3 | 6.9600 | 6.6667 | **6.6667** | 8.6207 | `8:11 9:18` |
| 150 | baseline `57fb7312` | 4 | 6.9600 | 6.6667 | **6.6667** | 8.6207 | `8:11 9:18` |
| 150 | baseline `57fb7312` | 5 | 6.9600 | 6.6667 | **6.6667** | 8.6207 | `8:11 9:18` |
| 150 | candidate `28b8cb0e` | 1 | 7.2803 | 7.5000 | **6.6667** | 8.2414 | `8:22 9:7` |
| 150 | candidate `28b8cb0e` | 2 | 7.2803 | 7.5000 | **6.6667** | 8.2414 | `8:22 9:7` |
| 150 | candidate `28b8cb0e` | 3 | 7.2803 | 7.5000 | **6.6667** | 8.2414 | `8:22 9:7` |
| 150 | candidate `28b8cb0e` | 4 | 7.2803 | 7.5000 | **6.6667** | 8.2414 | `8:22 9:7` |
| 150 | candidate `28b8cb0e` | 5 | 7.2803 | 7.5000 | **6.6667** | 8.2414 | `8:22 9:7` |
| 210 | baseline `57fb7312` | 1 | 6.4444 | 6.6667 | **5.4545** | 9.3103 | `8:1 9:19 10:8 11:1` |
| 210 | baseline `57fb7312` | 2 | 6.4444 | 6.6667 | **5.4545** | 9.3103 | `8:1 9:19 10:8 11:1` |
| 210 | baseline `57fb7312` | 3 | 6.4444 | 6.6667 | **5.4545** | 9.3103 | `8:1 9:19 10:8 11:1` |
| 210 | baseline `57fb7312` | 4 | 6.4444 | 6.6667 | **5.4545** | 9.3103 | `8:1 9:19 10:8 11:1` |
| 210 | baseline `57fb7312` | 5 | 6.4444 | 6.6667 | **5.4545** | 9.3103 | `8:1 9:19 10:8 11:1` |
| 210 | candidate `28b8cb0e` | 1 | 6.7969 | 6.6667 | **6.6667** | 8.8276 | `8:5 9:24` |
| 210 | candidate `28b8cb0e` | 2 | 6.7969 | 6.6667 | **6.6667** | 8.8276 | `8:5 9:24` |
| 210 | candidate `28b8cb0e` | 3 | 6.7969 | 6.6667 | **6.6667** | 8.8276 | `8:5 9:24` |
| 210 | candidate `28b8cb0e` | 4 | 6.7969 | 6.6667 | **6.6667** | 8.8276 | `8:5 9:24` |
| 210 | candidate `28b8cb0e` | 5 | 6.7969 | 6.6667 | **6.6667** | 8.8276 | `8:5 9:24` |

**Every repetition at every position returned a byte-identical interval
sequence.** 30 repeated runs, 6 distinct sequences, one per (arm, position).
The capture advances exactly one guest VBlank per observation over Ymir's
JSON-RPC boundary and warms up by simulation tick, so nothing in it is
wall-clock dependent; runs taken alone and runs taken with five other Ymir
processes competing for the same 12 cores returned the same numbers.

Three independent corroborations that this is the instrument and not luck:

- W2's own tick-30 candidate run and W6a's tick-30 baseline run, taken hours
  apart on different days, reproduce here **to the interval**.
- W6a's tick-30 capture of `id-586944bb637a920f` and this task's captures of
  its rebuild `id-57fb7312058b7f2e` — different artifacts, same content —
  produce the same 29 deltas.
- The tick-210 baseline row above reproduces **W7 §3.2's field distribution
  `8:1 9:19 10:8 11:1` and its 6.4444 mean exactly**, on the rebuilt artifact.

**So the honest restatement of W2's question is not "is it noise" — there is
none — but "is one 29-frame window at tick 30 a property of the build or of
the window".** Sections 3 and 4 answer that.

---

## 3. Deliverable (2) — the same comparison, swept across the route

One run per position; determinism is already established, so repetition here
would only reprint the same rows. "worst" is the longest interval in the
window, which is what the 1% low reads (section 5).

| start tick | baseline mean | baseline 1% low | baseline worst | candidate mean | candidate 1% low | candidate worst | Δ mean | Δ 1% low |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 30 | 7.0445 | 6.6667 | 9 VB | 7.3109 | **6.0000** | 10 VB | +3.78% | **−10.00%** |
| 40 | 7.0445 | 6.6667 | 9 VB | 7.3418 | **6.0000** | 10 VB | +4.22% | **−10.00%** |
| 60 | 7.0445 | 6.6667 | 9 VB | 7.3729 | **6.6667** | 9 VB | +4.66% | 0.00% |
| 80 | 7.0732 | 6.6667 | 9 VB | 7.3729 | **6.6667** | 9 VB | +4.24% | 0.00% |
| 100 | 7.0445 | 6.6667 | 9 VB | 7.3418 | **6.0000** | 10 VB | +4.22% | **−10.00%** |
| 120 | 6.9880 | 6.6667 | 9 VB | 7.2199 | **6.0000** | 10 VB | +3.32% | **−10.00%** |
| 140 | 6.9048 | 6.0000 | 10 VB | 7.2199 | **6.6667** | 9 VB | +4.56% | **+11.11%** |
| 150 | 6.9600 | 6.6667 | 9 VB | 7.2803 | **6.6667** | 9 VB | +4.60% | 0.00% |
| 160 | 6.9600 | 6.0000 | 10 VB | 7.2803 | **6.6667** | 9 VB | +4.60% | **+11.11%** |
| 180 | 6.9048 | 6.0000 | 10 VB | 7.2500 | **6.6667** | 9 VB | +5.00% | **+11.11%** |
| 200 | 6.6923 | 6.0000 | 10 VB | 7.0161 | **6.6667** | 9 VB | +4.84% | **+11.11%** |
| 210 | 6.4444 | 5.4545 | 11 VB | 6.7969 | **6.6667** | 9 VB | +5.47% | **+22.22%** |
| 220 | 6.3736 | 5.4545 | 11 VB | 6.7181 | **6.0000** | 10 VB | +5.41% | **+10.00%** |

**The mean improves at all 13 positions, by +3.32% to +5.47%. The 1% low is
worse at 4, unchanged at 3, and better at 6.** Tick 30 is one of the four, and
it is the one W2 measured.

**Answering W2's third question directly: at tick 150 the 1% low is unchanged,
as W2 found — and it is unchanged in all five repetitions. At tick 210 the
candidate is +22.2%**, because the baseline there produces eight 10-VBlank
frames and one 11-VBlank frame in 29 intervals while the candidate produces
none longer than 9. The heavier the scene, the more the candidate helps the
floor; the effect W2 saw is confined to the light stationary block.

---

## 4. Deliverable (3) — the window swept exhaustively

Because the capture is deterministic and warm-up is by route tick, windows
that overlap must agree — and they do. **60 windows (25 baseline, 35
candidate) stitch into one per-route-tick frame-length profile per arm,
covering route ticks 30–248 contiguously, with 0 overlap conflicts.** That
profile is the thing the 1% low is a 29-sample order statistic of.

VBlanks per presented frame, by route tick (`A` = 10, `B` = 11):

```
baseline id-57fb7312058b7f2e
 t30  989898989898989899898989898989898989898989898989898998989898
 t90  98989898898989898989989898898989989989898989898998989989989A
 t150 8998999899899989898989989898989989989A8998999899898989899898
 t210 99899999A999A99AA9A999A9AB99AA9A9A99999

candidate id-28b8cb0e531f8476
 t30  8A88888898888889888888A8888889888888988888889888888988888988
 t90  8888898888888889888889888888889888A88889888888A8888988898898
 t150 898898898898898888988888988888988988898898898898888889888988
 t210 89898989998999999999999999999A999999898
```

| over route ticks 30–248 (219 frames) | baseline | candidate |
| :--- | ---: | ---: |
| total VBlanks | 1906 | **1823** |
| VBlanks per frame | 8.7032 | **8.3242** |
| FPS | 6.8940 | **7.2079** |
| **longest frame anywhere** | **11 VB (183.3 ms)** | **10 VB (166.7 ms)** |
| frames ≥ 10 VB | 14 | **5** |
| frames ≥ 9 VB | 139 (63.5%) | **66 (30.1%)** |
| route ticks with a ≥10 VB frame | 149, 187, 218, 222, 225, 226, 228, 232, 234, 235, 238, 239, 241, 243 | **31, 52, 124, 136, 239** |

Per route tick, the candidate's frame is **shorter at 100, equal at 103, and
longer at 16** of the 219. Accumulating both arms tick by tick, **the
candidate is behind the baseline at exactly 1 of 219 boundaries — by one
VBlank, at route tick 31, the first anomaly — and is ahead at every boundary
after it**, finishing **83 VBlanks (1.38 s) ahead over 219 frames**.

Now scan every fully covered 29-interval window — that is, ask what
`summarize_cadence` would have reported from every possible warm-up point:

| worst interval in the window | 1% low it reports | baseline (191 windows) | candidate (191 windows) |
| ---: | ---: | ---: | ---: |
| 9 VB | 6.6667 | 102 (53.4%) | **117 (61.3%)** |
| 10 VB | 6.0000 | 75 (39.3%) | 74 (38.7%) |
| 11 VB | 5.4545 | 14 (7.3%) | **0** |

**Taken over the route rather than at one point, the candidate's 1% low
distribution is better than the baseline's**: it sits at the 6.6667 floor more
often, it never reaches 5.4545, and its VB/frame is lower in **191 of 191**
windows. Comparing the two arms window by window, the candidate's worst
interval is worse in 48 (25.1%), equal in 70 (36.6%) and **better in 73
(38.2%)**.

Tick 30 is not an unrepresentative window in the sense of being wrong — it is
one of the 25.1%.

---

## 5. What the "1% low" is at n = 29

`summarize_cadence` computes

```python
"guest_fps_1pct_low": rates[max(0, math.ceil(len(rates) * 0.01) - 1)]
```

At `len(rates) == 29`, `ceil(0.29) - 1 == 0`. **The reported 1% low is the
single slowest interval in the window** — the minimum, with no averaging in
it. Two consequences the owner should have in hand before reading the −10%:

1. **It is maximally sensitive.** One 10-VBlank frame anywhere in 29 moves it
   the full 10%; a second one moves it not at all. W2's "two 10-VBlank
   intervals" and "one" are the same reported number, which is why the
   tick-40 window — containing only one of them — reports the same −10.00%.
2. **It is quantised to `60/k`.** The only values available near here are
   7.5000, 6.6667, 6.0000 and 5.4545. There is nothing between 6.6667 and
   6.0000, so the smallest expressible movement of this statistic **is**
   −10%. The figure is one field of one frame, reported as a percentage.

Neither point makes the regression unreal — sections 2 and 4 show it is very
real and perfectly repeatable. Both mean the **−10.0%** headline overstates
its own precision, and that the metric cannot express what actually happened,
which is "the worst frame in this window got 16.7 ms longer while 84% of
frames got 16.7 ms shorter".

---

## 6. Deliverable (4) — the cause

**The candidate does not do more work at the long frames.** Four measurements
say so, and none of them depends on the phase rail.

**(a) The long frames are route-locked, and there are five of them in 219
ticks.** They sit at route ticks **31, 52, 124, 136 and 239**, and they appear
at those ticks in every window that covers them, across 35 stitched candidate
windows with zero overlap conflicts. They do not move when the window moves,
so they are a property of the route, not of the capture.

**(b) They are not the heavy frames — one of them is nearly the lightest.**
Reading `w2-route-new.json`'s per-tick `vdp1_commands_last` on the candidate
itself: ticks 31, 52, 124 and 136 carry **543, 550, 493 and 529** commands
against a range of **485 to 576** over ticks 30–140. The span's *heaviest*
frame, 576 at tick 80, is an 8-VBlank frame; tick 124, at 493, is near the
span's minimum and is a 10-VBlank frame. Scene weight does not select them.

**(c) Where the candidate's frame is longer, the baseline was on the short
half of its own alternation.** Of the 16 ticks where the candidate's frame
exceeds the baseline's, **15 have the baseline at 8 VBlanks** — the fast half
of the 8/9 pattern it runs at ~8.70 VB/frame. The sixteenth is tick 124,
baseline 9 against candidate 10. There is no tick where a long candidate frame
lines up with a long baseline frame, which is what "the same work got slower"
would look like.

**(d) The candidate never falls behind.** One VBlank, at one of 219 boundaries,
at the first anomaly, and never again (section 4). A build that did more work
somewhere would fall behind there and stay behind. This one does not.

**So the long frames are a discretisation effect, not a cost.** The baseline's
frame sits at ~8.70 VBlanks, well clear of an integer, so its sub-field
residue is discharged every other frame as the 9 in an 8/9 alternation and
never accumulates. The candidate's frame sits at ~8.32, so the same
frame-to-frame variation mostly rounds down to 8 and the residue accumulates
until it must be discharged in one step. **The step is two fields wide, not
one**, and that is a structural property of the presentation loop rather than
anything about square roots:
`src/port/saturn/runtime/saturn_frame_pipeline.c` grants simulation credit as
`elapsed / SM64_SATURN_FRAME_SIM_VBLANK_DIVISOR` with the divisor **2** —
"presentation observes every field, while authoritative SM64 simulation
remains 30 Hz" — and `pipeline_finish_presentation()` discards whatever credit
is unspent at each presentation. The loop's unit for "wait for the next thing
to simulate" is therefore a **field pair**. A frame arriving just after a
credit boundary waits two fields for the next one. That is why the residue
surfaces as one 8→10 rather than as two 8→9s, and why a statistic that reads
the single worst frame moves a full 10% on a change that shortened the average
frame by 0.38 VBlanks.

### 6.1 And it is measured directly: the long frames are waiting, not working

`capture_idle_attribution`, one contiguous window per arm, **both warmed to
route tick 30 and both ending at route tick 34** — so both cover the
candidate's 10-VBlank frame at tick 31 — caches off, identity **MATCH** on
both, W7's ≥4-frame window requirement met on both. The burst profiler is not
involved.

| | baseline `id-57fb7312058b7f2e` | candidate `id-28b8cb0e531f8476` |
| :--- | ---: | ---: |
| window steps | 13,000,000 | 12,500,000 |
| master cycles traced | 15,838,709 | 15,629,704 |
| contiguous frames | 4.1561 | 4.2563 |
| **master idle** | **0 cycles — `0.000000`** | **401,477 cycles — `2.5687%`** |
| **master idle per frame** | **0.0000 VB** | **0.2108 VB** |
| VBlank spin runs | **0** | 4, all at `_main+0xe14`, longest 337,132 cycles |

**The baseline's master does not wait at tick 30 at all** — reproducing W7
§3.3's `0.000000` for the same content at the same route position, and
confirming that there the frame *is* master work, with no residue.
**The candidate's master waits 0.21 VBlanks per frame at the same place**, at
the single wait site W7 attributed.

And the amount closes. The candidate's tick-30 cadence window is 238 VBlanks
over 29 frames; a clean 8-VBlank cadence would be 232, so the **excess is 6
VBlanks = 0.2069 VB/frame**. The independently measured master idle is
**0.2108 VB/frame**. Those agree to **1.9%**. The excess that produces the
10-VBlank frames *is* the waiting, measured two different ways on two
different instruments.

Reading the work through: the candidate's master work per frame is
8.2069 − 0.2108 = **7.996 VB**, against the baseline's 8.5172 VB of work with
no idle at all. **The change removed ~0.52 VB/frame of work; ~0.31 VB/frame
reached the frame and ~0.21 VB/frame did not.** That is the first
route-position-matched, ≥4-frame measurement in this project showing the
master idle at tick 30 above zero, and it is stated narrowly: **one route
position, one window, one pair of builds.** W7 withdrew a "the master is no
longer saturated" claim built on weaker evidence than this, and the right
response is to treat it as a lead for the next task rather than as a planning
input — it does not change anything in this report's recommendation, which
rests on the cadence measurements.

**What is measured and what is modelled.** (a)–(d) and every figure above are
measurements. The divisor and the credit-drop are source facts, quoted from
the file. The join between them — "the accumulated residue is discharged
against the 30 Hz credit grid" — is a model consistent with all of them and
not separately measured. **Nothing in the recommendation depends on the model
being the whole story; it depends on (a)–(d).**

---

## 7. Honesty — what is wrong with these numbers

1. **One route, one level, one camera path**, exactly as W2 §11.1. Everything
   here is `sourceboot-bob-demo`, route ticks 30–248. The window scan
   generalises across *window position on this route*, which is strictly
   weaker than generalising across content. A different level could put the
   candidate's frame at a different fraction and move the effect.
2. **The window scan weights every start tick equally.** A player does not
   sample 29-frame windows uniformly; they play the route. The percentages
   say how often a *measurement* would see the effect, not how often a
   *player* would. The route-total rows in section 4 are the player-facing
   ones.
3. **The route ticks 30–248 do not include the boot or level-load transient**,
   and the tail beyond ~200 is the post-route static block that T2.20 §2.3
   warns about. The tick-210 and tick-220 rows are inside that regime, which
   is why they are reported as their own rows rather than folded into an
   average.
4. **The idle census in §6.1 is two windows of ~4.2 frames each, at one route
   position.** It is enough to establish that the candidate's master waits
   where the baseline's does not, and the 1.9% agreement with the cadence
   excess is a strong check, but it is not a rate and must not be quoted as
   one — that is precisely the error W6a made and W7 corrected. The
   ~0.52 VB/frame of work removed is likewise a tick-30 figure, not a
   route-wide one, and it disagrees with W2 §7's inferred ~0.35 VB/frame,
   which was derived at tick 150 from the frame delta alone with no idle term
   available. Both can be right; they are different route positions and
   different derivations.
5. **Caches-on was not measured**, deliberately, on the same one-basis rule
   W2 applied. These numbers must not be mixed with caches-on figures.
6. **This report does not re-establish downstream identity.** W2 measured
   Mario's action, position, velocity and facing as equal across 196 of 200
   route ticks at 1e-3 resolution, and nothing here revisits that. The FPS
   comparison assumes it.
7. **`guest_fps_median` is also quantised** and moves 6.6667 → 7.5000 in one
   step; the +12.5% W2 reports for it has the same precision problem as the
   −10%, in the favourable direction. It is not quoted as a headline here for
   that reason.

---

## 8. Recommendation

**Promote — with the tick-30 1% low documented as an accepted, understood
artifact.** Not "hold", and not "promote silently".

The reasons, in order of weight:

1. **The candidate's frame is faster everywhere.** Lower VB/frame in **191 of
   191** window positions, higher mean FPS at **all 13** swept start ticks
   (+3.32% to +5.47%), and 83 VBlanks ahead over 219 route frames. It is
   cumulatively behind at one boundary out of 219, by one VBlank.
2. **On the 1% low itself the sign is window-dependent, not negative.** Worse
   at 4 of 13 swept positions, unchanged at 3, better at 6 — and swept
   exhaustively, the candidate reports the better 1% low value more often than
   the baseline (61.3% against 53.4% at 6.6667) and never reports the
   baseline's worst value at all.
3. **Route-wide, the candidate is smoother by every measure except a
   min-of-29 at particular positions**: longest frame 166.7 ms against
   183.3 ms; frames ≥ 10 VB, 5 against 14; frames ≥ 9 VB, 30.1% against 63.5%.
4. **The cause is understood, measured, and is not a defect in the change.**
   The long frames are the master *waiting* — `0.000000` master idle on the
   baseline against 0.2108 VB/frame on the candidate at the same route
   position, matching the cadence excess to 1.9%. The thing being waited on is
   the presentation loop's 30 Hz credit granularity, which was there before
   this change and will be reached by any future change that moves the frame
   below ~8.5 VBlanks. The next optimisation will meet the same grid.
5. **The change itself is verified beyond the FPS argument**: exhaustive
   `verify-sqrtf-fixed` over all 2^32 inputs at 0 ulp, its mutation companion
   catching all five, and no downstream movement across 196 route ticks.

**What "documented as accepted" means concretely.** The accepted-baseline
record for `id-28b8cb0e531f8476` should carry the tick-30 1% low of **6.0000**
together with three things: that the statistic is a min-of-29 on a `60/k`
grid, so −10% is its minimum expressible step; that it is caused by five
route-locked field-pair discharges in 219 frames; and that the same statistic
is **+22.2% at tick 210**. **Do not quote −10.0% without the other columns** —
it is true and it is not the whole measurement.

**AGENTS.md's product gate is unchanged and still applies.** This report does
not promote anything: an owner-observed CUE does. What it does is remove the
reason W2 gave for withholding the build from that observation.

**If the owner wants the artifact gone entirely**, the lever is not the square
root — it is `SM64_SATURN_FRAME_SIM_VBLANK_DIVISOR` and the credit drop in
`pipeline_finish_presentation()`. That is a scheduler change with its own
risk, it is out of W2's scope, and it should not be attempted as part of this
promotion.

---

## 9. Evidence

| File | What |
| :--- | :--- |
| `w2-onepercent-low-recheck-runs.json` | all 60 runs: identity, artifact hashes, route span, the full 29-interval list, the three statistics, retry count and gate booleans — plus the stitched per-route-tick profile and the complete 191-window scan for both arms |
| `w2-onepercent-low-recheck-cadence-tick210-57fb7312.json` | raw baseline cadence at tick 210, unabridged (a route position not previously captured for this artifact) |
| `w2-onepercent-low-recheck-cadence-tick210-28b8cb0e.json` | raw candidate cadence at tick 210, unabridged |
| `w2-onepercent-low-recheck-idle-tick30-57fb7312.json` | baseline idle census at tick 30, §6.1 — trimmed to identity, tracing configuration and the summary block (the per-symbol residency tables are dropped; this run was taken to read master idle, not as a census) |
| `w2-onepercent-low-recheck-idle-tick30-28b8cb0e.json` | candidate idle census at tick 30, §6.1, same trim |
| `w2-cadence-tick30-28b8cb0e.json`, `w6a-rebuild-throughput-30events-57fb7312.json` | W2's and W6a's original tick-30 captures, reproduced here to the interval |

Reproduce (`$D` must be absolute; PowerShell for anything invoking Make, which
this does not):

```sh
YMIR=<ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe>
IPL=<sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin>
BASE=<releases/2026-08-22_w6a-rebuild/id-57fb7312058b7f2e>
CAND=<releases/2026-08-22_w2-product/id-28b8cb0e531f8476>

cap() {  # cap <artifact-dir> <warmup-tick> <out>
  python tools/saturn/capture_sourceboot_throughput.py --ymir "$YMIR" --ipl "$IPL" \
    --game "$1/sm64-saturn-sourceboot-e2.cue" --elf "$1/obj/sm64-saturn-sourceboot-e2.elf" \
    --release-manifest "$1/saturn-release-manifest-v1.json" --startup-vblanks 4096 \
    --warmup-ticks "$2" --presentation-events 30 --timeout 3000 --output "$3"
}

# section 2: repetitions
for T in 30 150 210; do for R in 1 2 3 4 5; do
  cap "$BASE" $T base-t$T-r$R.json; cap "$CAND" $T cand-t$T-r$R.json
done; done

# sections 3 and 4: the window sweep
for T in 40 60 80 100 120 140 160 180 200 220; do
  cap "$BASE" $T base-t$T-r1.json; cap "$CAND" $T cand-t$T-r1.json
done
# plus candidate-only fine steps 31..38, 45, 52 to pin the route-locked frames

# section 6.1: the idle census, ~25-45 min per arm
NM=<work/yaul-install/bin/sh-elf-nm.exe>
python tools/saturn/capture_idle_attribution.py --ymir "$YMIR" --ipl "$IPL" \
  --game "$BASE/sm64-saturn-sourceboot-e2.cue" --elf "$BASE/obj/sm64-saturn-sourceboot-e2.elf" \
  --nm "$NM" --warmup-ticks 30 --windows 1 --window-steps 13000000 \
  --slave-stride 16 --io-stride 20000 --vblanks-per-frame 8.5172 \
  --timeout 9000 --output idle-base-t30.json
python tools/saturn/capture_idle_attribution.py --ymir "$YMIR" --ipl "$IPL" \
  --game "$CAND/sm64-saturn-sourceboot-e2.cue" --elf "$CAND/obj/sm64-saturn-sourceboot-e2.elf" \
  --nm "$NM" --warmup-ticks 30 --windows 1 --window-steps 12500000 \
  --slave-stride 16 --io-stride 20000 --vblanks-per-frame 8.2069 \
  --timeout 9000 --output idle-cand-t30.json
```

The two step counts differ because the two builds' frames differ: they are
chosen so both windows are ≥4 frames and both end at route tick 34.

Wall clock: ~20 s per capture unloaded, ~2 min under six concurrent Ymir
processes; identical results either way.
