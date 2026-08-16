# Sprint 2 Task T2.11 — the cadence summarizer, repaired, and T2.10's cadence

- Date: 2026-08-16. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `27bda107`.
- Task: repair `summarize_cadence` in
  `tools/saturn/capture_sourceboot_throughput.py`, which aborted on every build
  faster than ~15.5 VBlanks per frame and therefore blocked **every** FPS
  measurement in the project.
- Trigger: `sprint2-t2_10-spatial-admit-fixes.md` §7 (two aborts, no FPS).
  Constraint: `sprint2-t2_7-a9a-regression-attribution.md` §1.1 — nothing is
  hand-computed from failure diagnostics; the summarizer is the only source of
  cadence truth.
- **No target build was made and none was needed.** This is a host-side Python
  change plus three re-runs against artifacts that already existed.

## Headline

**T2.10's product build `id-b46f60d0a6d129dd` runs at 4.3176 FPS mean,
13.8966 VBlanks per frame** — against the previous product build's 3.8753 /
15.4828 on the same tool, same emulator, same route, in the same session. That
is **−1.586 VBlanks per frame, +11.4% frame rate**, and it is the first product
FPS figure the project has had since T2.7.

The control reproduces T2.7 to four significant figures, so nothing about the
measurement moved: **3.8753 mean / 3.75 median / 15.4828 VB/frame**, identical
to T2.7's archived run and to T2.10's own control.

The defect was **not** an approximation error. It was a phase boundary that the
master SH-2 does not stamp.

---

## 1. The invariant, and exactly what it asserted

`phase_delta()` in `tools/saturn/capture_sourceboot_throughput.py`, the guard as
it stood at `:535-540`:

```python
attributed = (
    simulation_crossings + construction_crossings +
    transport_presentation_crossings
)
if attributed > vblank_delta:
    raise ValueError("phase VBlank crossings exceed the observed interval")
```

Every quantity is an integer count of **whole VBlank crossings** — a difference
of the target's monotone `sourceboot_vblank_out_count` taken between two program
points. `vblank_delta` is the same difference taken between two adjacent
cadence-trace samples.

So the guard asserts: *the accounted phases are wall-disjoint sub-windows of the
interval.*

**That statement is exact, not approximate, for windows a single-threaded master
stamps itself.** If phase A occupies `[a0, a1]` and phase B occupies `[b0, b1]`
with `a1 <= b0` in wall time, then because the counter `C` is monotone,
`C(a1) <= C(b0)`, and therefore

```
(C(a1) − C(a0)) + (C(b1) − C(b0)) <= C(b1) − C(a0)
```

No rounding, no quantisation, no ±1. The sum of ordered disjoint windows can
**never** exceed the enclosing span, at any frame rate.

**So the brief's proposed diagnosis is refuted.** "Whole-VBlank resolution makes
equality-or-less brittle at short frames" is not what happens here; a
whole-VBlank counter differenced across ordered disjoint windows is already
exact, and it stays exact as the frame shortens. An overshoot can only come from
windows that genuinely **overlap in wall time**, or from windows charged to an
interval they do not lie inside. It is the former, and here is which one.

### 1.1 The boundary the master does not stamp

`construction` in the v2 schema is not one difference. It is reconstructed in
`src/port/saturn/runtime/saturn_render_overlap_phase.c:95-113` as

```
construction = (notification_vblank − construction_begin_vblank)
             + (terminal_vblank − retirement_vblank)
```

— the full construction span **minus** the slave-work window
`[notification, retirement]`, which is excluded precisely so that the source tick
the master runs during it is not counted twice. Three of those four clock reads
are taken by the master. The fourth is not:

```c
} else if (marker == SM64_SATURN_RENDER_JOB_RUNTIME_MARKER_RETIRED) {
    /* T2.5: T2.4 stamped the profiler here.  This observer runs on the
     * slave SH-2 (runtime_publish_retirement_marker is called from
     * render_job_slave_entry), and the FRT is a per-CPU on-chip block,
     * so the interval it produced differenced two unrelated counters.
     * The cadence rig's VBlank crossings measure this correctly. */
```
— `src/port/saturn/sourceboot/main.c:1471-1479`

`retirement_vblank` is written **from the slave SH-2, at the instant the slave
finishes**. The master does not begin finalizing at that instant. It is still
inside whatever action the frame pipeline dispatched it to run — and the frame
pipeline explicitly admits the next source tick while a render is in flight:

```c
if (pipeline->available_sim_credit != 0U &&
    (!pipeline->render_active || pipeline->render_service_started ||
     (pipeline->render_completed_valid && ...
```
— `src/port/saturn/runtime/saturn_frame_pipeline.c:154-160`

So the span `[retirement_vblank, the master's first poll after it]` is charged
**twice**: once to `master_finalization`, hence to `construction`, and once to
`simulation`. That is a real double count in the accounting, not an artefact of
resolution.

### 1.2 Why it surfaced on T2.10's build and not before

The double charge is bounded by one crossing per affected interval on this build
family, and the previous product build had exactly enough slack per frame to
absorb it. T2.10 recorded `attributed == vblank_delta` in 25 of 29 control
intervals; this task's fresh control reproduces that distribution exactly — **25
intervals at margin 0, four at margin 1, none negative.**

T2.10 removed 1.586 VBlanks per frame of that slack. On the re-measured product
build the overshoot appears in **4 of 29 intervals, at exactly +1 crossing
each** (intervals 7-10: `attributed` 16 against `vblank_delta` 15). Four
intervals out of twenty-nine were enough to discard the entire summary.

### 1.3 The magnitude, in units, from the retained data

T2.10 retained both aborted runs. Both are deterministic replays of the same
build, so their retained cumulative cadence records — at presentation events 10
and 30 — are two samples of one trajectory, and their difference is a real
20-frame interval:

| Quantity, frames 11-30 | Crossings |
| --- | ---: |
| `observed_vblank_generation` delta (the interval) | 282 |
| `simulation` | 122 |
| `construction` | 162 |
| `transport_presentation` | 0 |
| **`attributed`** | **284** |
| **overshoot** | **+2** on 282, i.e. **+0.71%** |
| `slave_work` | 60 |
| `master_finalize` | 102 |

**Two whole VBlank crossings in 282.** That is the number that discarded a
complete, fully observed 30-event capture.

## 2. The fix, and why the bound is principled

```python
concurrent_allowance = (
    min(simulation_crossings, master_finalize_crossings)
    if has_overlap_phases else 0
)
if attributed - concurrent_allowance > vblank_delta:
    raise ValueError("phase VBlank crossings exceed the observed interval")
```

**Tolerance: `min(simulation, master_finalization)`, in whole VBlank
crossings.**

Its justification is a derivation, not a calibration:

1. The double-charged region is `[retirement_vblank, sim_end]`. By construction
   it is a sub-window of the simulation window **and** a sub-window of the
   finalization window.
2. A quantity that is a sub-window of both cannot exceed either, so
   `min(simulation, master_finalization)` is an **upper bound on a provable
   double count** — the tightest bound derivable from the integers the trace
   carries.
3. It is **read out of the same trace**, so it cannot be tuned. It is exactly
   **zero** where no concurrent window exists: a v1 trace with no overlap fields,
   or a phase aborted before notification (`..._phase_abort()` charges the whole
   span to `construction` and never touches `master_finalize`).
4. It cannot excuse an overshoot in `transport_presentation`, whose two
   boundaries are both master-stamped, nor a simulation overshoot larger than the
   finalization window, because it is capped by that window rather than by need.

### 2.1 A finer unit was preferred and is not available

The brief asks for a finer comparison unit in preference to a widened threshold.
**The v2 cadence trace does not carry one.** Its fifteen fields
(`CADENCE_RECORD_FIELDS`) are generations, counts and VBlank crossings. The only
sub-VBlank clock on the target is the FRT; it lives in the prenotify profile, it
is a **per-CPU on-chip block** — the T2.5 comment quoted in §1.1 records that
differencing a master FRT against a slave FRT produced nonsense — and this tool
does not read it at all.

So the alternative was taken instead: rather than widen a threshold, the
**model** was corrected to account for concurrency the runtime demonstrably has,
using quantities the trace already publishes.

### 2.2 What this costs, stated plainly

The interval-level margin on this build family widens from **0** to about **5
crossings of 15** (control mean allowance 4.517; T2.10 build 4.897). That is a
real loss of sensitivity and it is the price of not having a finer clock. Two
things limit the damage: the allowance is capped by a phase that is itself small,
and the check still runs per interval rather than on aggregates, so a runaway
phase in a single frame still trips it.

**Owner follow-up, not done here:** stamping `retirement_vblank` from the
master's *observation* of retirement, or adding an FRT field to a cadence trace
v3, would remove the ambiguity rather than bound it, and would restore a tight
margin. Both are target-side changes; T2.11 is explicitly host-side.

## 3. Schema

`attributed_vblank_crossings` and `unattributed_vblank_crossings` keep their
existing definitions and names. **`unattributed` may now be reported negative**
where it previously forced an abort — that is the honest raw margin, and a reader
must not read it as idle time. v2 intervals gain one new field,
`concurrent_phase_allowance_vblank_crossings`. **v1 intervals are byte-identical
to before**, so every archived evidence JSON, A9A's included, keeps both its
exact shape and its exact strictness.

## 4. Regression test and mutation result

Added to `tools/saturn/test_capture_sourceboot_throughput.py`. Host only; no
emulator, no build. The fixtures **load the two retained abort JSONs from
`docs/saturn/evidence/reports/`** rather than restating their numbers, and assert
those files are still `status: failed`.

| Test | What it pins |
| --- | --- |
| `test_t2_10_retained_abort_data_summarises_and_reports_negative_margin` | the retained 20-frame interval summarises; `attributed` is still 284 against 282 and `unattributed` still reads **−2**, so the overshoot is reported, not hidden |
| `test_transport_overshoot_has_no_concurrent_window_and_still_raises` | inflating `transport_presentation` on otherwise real data still aborts — no concurrency can excuse a master-stamped phase |
| `test_overshoot_larger_than_the_finalization_window_still_raises` | +103 construction against a 102-crossing window still aborts, and a +1000 simulation overshoot still aborts, so the allowance does not grow to meet need |
| `test_v1_trace_without_overlap_fields_gets_no_allowance` | a v1 record where `min(simulation, construction)` *would* have excused the overshoot still aborts |

**Mutation proofs.** Applied to the working tree, run, reverted. None is
committed. **Four applied, four killed:**

| # | Mutation | Result | Killed by |
| --- | --- | --- | --- |
| M1 | `concurrent_allowance = attributed` — loosened to absurdity | **KILLED** | both must-raise tests, plus the retained-abort value assertion (3 failures) |
| M2 | allowance applied with no recorded overlap window (`if True`) | **KILLED** | both v1 tests (2 failures) |
| M3 | allowance removed — pre-T2.11 behaviour | **KILLED** | the retained-abort test |
| M4 | `min()` dropped, allowance `= simulation` | **KILLED** | the capped-allowance test and the value assertion (2 failures) |

M1 is the one the brief asks for: widening the tolerance until nothing trips
**stops the genuinely inconsistent cases failing**, and the suite says so.

Suite: **45 tests, OK** — 41 before, 4 added.

## 5. Validation on target — no rebuild, existing artifacts

ymir-headless `build-agent2`, SHA-256 `fcc88d82b2ea7afd…3943` — the same
byte-identical binary A9A and T2.7 used. BIOS `Sega Saturn BIOS (USA).bin`.
Absolute `--cue`, `--startup-vblanks 4096 --max-vblanks 3600 --timeout 1800`,
release-manifest bound. All three runs `status: complete`, exit 0, one attempt
each.

### 5.1 Control — `id-6eca5970628d581d`, must not drift

ELF `1b4ff08d763519e7…0ad4`, ISO `69ca844f5635dac9…fff1`, manifest
`8622d87a5124e61f…dff4`, config `6eca5970628d581d…7d3c`.
Evidence: `sprint2-t2_11-control-30events.json`.

| | T2.7 archived | T2.10's control | **T2.11, post-fix** |
| --- | ---: | ---: | ---: |
| FPS mean | 3.875 | 3.8753 | **3.8753** |
| FPS median | 3.750 | 3.75 | **3.75** |
| FPS 1% low | 3.750 | 3.75 | **3.75** |
| intervals | 29 | 29 | **29** |
| VBlanks | 449 | 449 | **449** |
| **VBlanks/frame** | 15.483 | 15.4828 | **15.4828** |

**Zero drift, to every digit.** The fix changed when the tool aborts. It changed
nothing the tool computes.

The per-interval margins are unchanged too: 25 intervals at `attributed ==
vblank_delta`, four at −1, none positive. The allowance was available on every
interval (mean 4.517 crossings) and **needed on none**.

### 5.2 T2.10's product build — `id-b46f60d0a6d129dd`, at last

ELF `68fbdeb05f5da3ae…88a6`, ISO `147c1306cc63fec5…5c3c`, manifest
`6907a5e4d864e267…6d6b`, config `b46f60d0a6d129dd…6c86`, label
`feat001-pipe4-l9-a1-route0-replay1-live1-boot600-cam0v3-diag0-cart32-stage8-hot1-clip1-bsp1-poly2-frag0-cfgb46f60d0a6d1`.
Evidence: `sprint2-t2_11-t2_10-build-30events.json`,
`sprint2-t2_11-t2_10-build-10events.json`.

| Run | Events | Intervals | Window (VB) | **VB/frame** | **FPS mean** | Median | 1% low |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| A9A archived | 10 | 9 | 102 | 11.333 | 5.294 | 5.000 | 5.000 |
| `id-6eca5970628d581d`, 30 events | 30 | 29 | 449 | 15.4828 | 3.8753 | 3.75 | 3.75 |
| **`id-b46f60d0a6d129dd`, 30 events** | 30 | 29 | **403** | **13.8966** | **4.3176** | **4.2857** | **4.0** |
| **`id-b46f60d0a6d129dd`, 10 events** | 10 | 9 | **121** | **13.4444** | **4.4628** | **4.2857** | **4.0** |

- **−1.586 VBlanks per frame, +11.4% FPS** on the 30-event basis. On the
  10-event matched basis, against T2.7's 3.971 / 15.111, it is −1.667 VB and
  +12.4%. The two bases agree on direction and roughly on size.
- **Above the 4 FPS floor**, including the 1% low, which sits exactly on 4.0.
- **The gap to A9A is now 1.23x** (13.8966 against 11.333 VB/frame), from 1.37x.
- T2.10 measured its three fixes at **−1.914 VBlank-equivalent** on the FRT
  instrument in a *diagnostic* build. The frame-level result is **−1.586 VB**.
  T2.10 §8.1 warned that a stage saving is not a promise about cadence and that
  the diagnostic rig itself costs +0.276 VB; the two figures are consistent with
  that warning, and the frame-level one is what counts.

Per-frame phase profile, 29 intervals each:

| | control `6eca5970` | **T2.10 `b46f60d0`** |
| --- | ---: | ---: |
| frame (`vblank_delta`) | 15.4828 | **13.8966** |
| construction | 9.5172 | **7.8966** |
| — master finalization | 4.5172 | 4.8966 |
| — slave work overlap | 2.9310 | 3.0000 |
| simulation / source tick | 5.8276 | **6.0345** |
| transport + presentation | 0.0000 | 0.0000 |
| attributed | 15.3448 | 13.9310 |
| allowance available | 4.5172 | 4.8966 |
| **allowance actually needed** | **0** | **1, in 4 of 29 intervals** |

**The saving is entirely in `construction`** — −1.621 VB/frame, where
`demo_spatial_admit()` lives. `simulation` is flat to +0.2 VB. That is
independent corroboration, at the frame level, of what T2.10 measured at the
stage level.

### 5.3 Determinism check — the retained record is reproduced exactly

The 10-event re-run's final cadence record is **byte-identical** to the record
T2.10 retained in `sprint2-t2_10-throughput-10events.json`:
`observed_vblank_generation` 1678, `simulation_vblank_crossings` 83,
`construction_vblank_crossings` 74, `master_finalize_vblank_crossings` 43,
`slave_work_vblank_crossings` 30, `transport_presentation_vblank_crossings` 0.

That is what licenses the regression test to treat the two retained aborts as two
samples of one trajectory, and it independently confirms the replay route is
deterministic across sessions.

## 6. Honesty — what is wrong with these numbers

1. **The check is genuinely looser than it was.** The margin on this build family
   goes from 0 to ~5 crossings of ~15. A phase error smaller than the
   finalization window will now pass. That is bounded, derived and documented,
   but it is a loss.
2. **The tolerance bounds a double count; it does not measure it.** The actual
   double charge on the re-measured build is **1 crossing** where the allowance
   is **6**. Without a sub-VBlank clock in the trace, the gap between the bound
   and the truth cannot be closed from the host.
3. **The brief's proposed diagnosis was refuted, not confirmed.** §1 shows
   whole-VBlank quantisation over ordered disjoint windows is exact at any frame
   rate. Had the fix been the suggested ±1-per-phase slack, it would have been a
   coincidence that happened to fit, and it would have kept drifting as frames
   shortened.
4. **The retained aborts contain aggregates, not per-interval data.** The +2/282
   figure in §1.3 is a 20-frame difference of two cumulative records, so it
   cannot show *which* intervals overshot. The re-run supplies that (4 of 29, +1
   each) and the two are consistent, but they are different measurements.
5. **`unattributed_vblank_crossings` can now go negative** in emitted evidence.
   Any reader or downstream tool that assumed it non-negative is now wrong.
   Nothing in-tree does: `attributed_`/`unattributed_` have no consumers outside
   this tool and its test.
6. **One route, one level, one camera path.** 4.3176 FPS describes level 9 area 1
   route 0 under Ymir. The structural claim in §1 does not depend on the route;
   the number does.
7. **The 10-event and 30-event figures differ by 0.45 VB/frame** (13.4444 against
   13.8966). The build is slower later in the route, on both builds, so basis
   matters when comparing. The 30-event basis is quoted as headline because
   T2.7's 3.8753 is on that basis.
8. **This is not an owner observation.** It is a headless capture. The product
   gate is still a CUE the owner sees and hears.

## 7. What this leaves for the owner

1. **T2.10 is confirmed and should be kept.** 4.3176 FPS / 13.8966 VB/frame
   against 3.8753 / 15.4828, with the saving landing in `construction` exactly
   where the three fixes were made, and visuals proven bit-identical by T2.10's
   516,090-case oracle.
2. **`id-6eca5970628d581d` remains the un-adjudicated open owner gate**, and
   `id-b46f60d0a6d129dd` now supersedes it on cadence. A single look-and-listen
   on the newer build would close both.
3. **The cadence rail has a target-side defect worth fixing** — §2.2. It is not
   blocking now, but the margin it consumes will matter again at ~8 VB/frame.
4. **Still 1.23x off A9A.** The remaining gap is 2.56 VBlanks per frame.

---

## Evidence index

| File | What |
| --- | --- |
| `sprint2-t2_11-control-30events.json` | control `id-6eca5970628d581d`, complete, 3.8753 / 15.4828 |
| `sprint2-t2_11-t2_10-build-30events.json` | **T2.10 product build, complete, 4.3176 / 13.8966** |
| `sprint2-t2_11-t2_10-build-10events.json` | T2.10 product build, matched 10-event basis, 4.4628 / 13.4444 |
| `sprint2-t2_10-throughput.json` | retained abort, 30 events — now a test fixture |
| `sprint2-t2_10-throughput-10events.json` | retained abort, 10 events — now a test fixture |
