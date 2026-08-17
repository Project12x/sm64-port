# Sprint 2 Task T2.19d — capture warm-up moved from VBlanks to route ticks

**Status:** tooling complete, live-verified on two sealed builds. No product
source changed, nothing rebuilt, no emulator GUI opened. This is a measurement
instrument change, not a product change, so it carries no product gate.

---

## 0. The one-paragraph version

Every capture in this tree warmed up by free-running a fixed number of VBlanks
after the ELF identity match — 1,800 in the common case. A VBlank is wall
clock. **Measured: 1,800 VBlanks puts `id-a61d5203793986e7` (5.3538 FPS) on
replay tick 25 and `id-c0352f297034f653` (6.7181 FPS) on replay tick 31**, and
at those two points the faster build has already plotted **28.2% more VDP1
triangles** (13,553 against 10,570 cumulative). The two builds were never
looking at the same scene. Warm-up now advances until the input replay's own
`ticks_consumed` reaches a target, so both builds stop at replay tick 30 — at
1,852 and 1,788 VBlanks respectively — where their render funnels are
**bit-identical**. `--warmup-vblanks` still works, still does exactly what it
did, and now prints a deprecation warning and records the route tick it landed
on.

---

## 1. Which counter indexes the route, and the proof

### 1.1 The answer

`sState.input_replay_ticks` — offset 24 of
`sm64_saturn_source_runtime_state_t` — which is a mirror of
`sm64_saturn_input_replay_t.ticks_consumed`.

### 1.2 Why it is the index and not a correlate of the index

`src/port/saturn/runtime/saturn_input_replay.h:52-77` is the only writer, and
`ticks_consumed` is advanced by the same statement block that advances the
route sample cursor:

```c
    replay->ticks_in_sample++;
    replay->ticks_consumed++;

    if (replay->ticks_in_sample == sample->ticks) {
        replay->sample_index++;
        replay->ticks_in_sample = 0U;
        if (replay->sample_index == replay->sample_count)
            replay->complete = true;
    }
```

`ticks_consumed` and `(sample_index, ticks_in_sample)` are the same clock.
Knowing `ticks_consumed` *is* knowing which route sample is being applied and
how far into it the replay is. Nothing else in the image has that property.

`src/port/saturn/runtime/saturn_source_runtime.c:86-127` is the only caller,
once per `game_loop_one_iteration()`, and it publishes the value to
`sState.input_replay_ticks` at lines 108 and 123. The struct field order is
pinned by a host test (`test_route_warmup.StructLayoutTests`) rather than by a
comment, so a future field insertion fails the test instead of silently moving
the peek.

### 1.3 Why the simulation tick counters are *not* the answer

The same function says so, in a comment that predates this task
(`saturn_source_runtime.c:92-100`):

> Do not spend deterministic route samples while the source boot is still
> constructing its authoritative Mario state. Renderer profiles can make this
> bootstrap interval longer; consuming input there makes the same route begin
> at different gameplay ticks in the interpreted and demo builds.

So `sourceboot_sim_tick_count` (`sourceboot/main.c:244,578`) and `gGlobalTimer`
(`src/game/game_init.c:89,474`) carry a **build-dependent offset** by
construction. They are simulation clocks, not route indices. They agree with
the replay tick on the shipped configuration only because live-input builds
drop the `gMarioState` guard (`saturn_source_runtime.c:117-127`) — which is a
property of one build flag, not a property to rely on.

### 1.4 Candidates considered and rejected

| Candidate | Where | Rejected because |
| --- | --- | --- |
| `sourceboot_vblank_out_count` | `main.c:246,990` | It **is** the VBlank count. This is the defect. |
| `sourceboot_sim_tick_count` | `main.c:244,578` | Build-dependent offset during bootstrap (1.3). |
| `gGlobalTimer` | `game_init.c:89,474` | Same. Retained, but only as a liveness witness (2.2). |
| `sourceboot_fast3d.profile.frame_serial` | `main.c:2132` | A *render* serial. It is what we are trying to measure, and it is exactly what differs between builds. |
| `sourceboot_route_checkpoint.replay_ticks` | `main.c:628` | The right quantity, but the publisher is inside `#if SATURN_SOURCEBOOT_ROUTE_REPLAY && !SATURN_SOURCEBOOT_LIVE_INPUT` (`main.c:599`) and every shipped build is `live_input_mode=1`. Compiled out. (T2.14 section 2.1 diagnosed this already.) |
| `sState.input_polls` | `saturn_source_runtime.h:30` | Counts poll calls, not consumed route samples. Coincides with the replay tick today; would diverge the moment polling and replay application stop being 1:1. |

### 1.5 The counter's ceiling, which is real and had to be handled

`sm64_saturn_input_replay_apply` returns early once `complete` is set, and
live-input images stop applying route samples entirely at
`SATURN_SOURCEBOOT_LIVE_INPUT_BOOTSTRAP_TICKS` (`saturn_source_runtime.c:119`;
both sealed builds are `bootstrap_ticks=600`). **Past tick 600 the counter is
frozen forever and route position is undefined** — the game free-runs on
neutral live input. Every capture in this tree today lives between tick 0 and
tick ~170, so this is headroom rather than a constraint, but it is the exact
shape of a silent no-op and is handled twice (section 2.2, section 5).

---

## 2. What changed

New module `tools/saturn/route_warmup.py`, plus `tools/saturn/test_route_warmup.py`
(28 host contracts) and `tools/saturn/verify_route_warmup_parity.py` (the
two-build proof harness of section 4).

### 2.1 Per tool

| Tool | Before | After |
| --- | --- | --- |
| `capture_route_counters.py` | `--warmup-vblanks` default **1800** | `--warmup-ticks` default **30**; also prints the sampled route span (`replay ticks 30 -> 69`) |
| `capture_idle_attribution.py` | `--warmup-vblanks` default **1800** | `--warmup-ticks` default **30**; also records `route_position_after_trace`, so the traced span is stated rather than inferred from a VBlank count |
| `capture_softfloat_profile.py` | `--warmup-vblanks` default **0** | `--warmup-ticks` default **30**; also records `route_position_after_profile`, and now **warns** when `--phases 1 --phase-vblanks N` is used as a warm-up in disguise (section 6.3) |
| `capture_sourceboot_throughput.py` | no warm-up at all | `--warmup-ticks` default **0** — behaviour unchanged on purpose (2.3) — plus a new `route_span` field recording the replay ticks the cadence window actually covered |

`--startup-vblanks` was deliberately **not** converted in any tool. It is a
timeout bound on a load event (poll one VBlank at a time until the ELF's bytes
appear), not a position on the route. Counting it in VBlanks is correct.

### 2.2 The fail-loud rules

A warm-up that does not reach its target **raises**. There is no third outcome.

1. **Pre-flight rejection.** A target above the build's own `bootstrap_ticks`
   (read from `saturn_build_identity`, when `live_input_mode=1`) is rejected
   before Ymir is started. Measured cost of a rejected run: **0.44 s**, no
   emulator process.
2. **Runtime plateau detection.** If the replay tick stops advancing while
   `gGlobalTimer` keeps advancing — 8 simulation ticks with no route sample
   consumed — the warm-up raises. Armed only *after* the first replay tick, so
   the `gMarioState == NULL` bootstrap of replay-only builds (where the
   simulation ticks but samples are deliberately withheld) is not mistaken for
   exhaustion.
3. **Bounded budget.** `--max-warmup-vblanks` (default 20,000) is a hard
   failure, not a return.
4. **Overshoot is an error.** Landing past the target raises rather than
   reporting the wrong tick.
5. **No silent no-op.** Every advance is chunked below Ymir's 3,600-frame
   `exec.run_for` cap and issued through `client.call`, which raises on a
   JSON-RPC error — the failure mode commit `c28980a` paid for once already.

### 2.3 Interface decision, and why

**Chosen:** add `--warmup-ticks`, make it the default, keep `--warmup-vblanks`
working exactly as before behind a deprecation warning. The two are mutually
exclusive; passing both is an error.

Rationale:

* Roughly two dozen JSON reports and five markdown reports under
  `docs/saturn/evidence/reports/` carry `--warmup-vblanks` in their
  reproduction instructions. A hard replacement would make every one of those
  commands fail, and reproduction instructions that fail are indistinguishable
  from reproduction instructions that lie.
* The legacy path is not merely tolerated, it is **improved**: it now records
  the replay tick it landed on, so a legacy-warmed report documents its own
  route position instead of leaving it unknowable. That is how sections 4 and
  6 could be written at all.
* **Default tick target 30** was chosen because it is inside the band the old
  default actually produced (25 on the slower sealed build, 31 on the faster),
  so tick-warmed numbers stay roughly comparable with the VBlank-warmed
  evidence they replace, while being exact going forward. It is reachable on
  every shipped build (cap 600).
* **`capture_sourceboot_throughput.py` keeps warm-up off by default.** Its
  observation window counts *presentation events*, not VBlanks, so it is
  already simulation-anchored (section 6.2 measures this). Turning a warm-up on
  by default would move the accepted 5.3538 / 6.7181 FPS baselines mid-sprint,
  with three agents currently measuring against them. It gained `route_span`
  recording instead, which is the actual repair for that tool.

---

## 3. Measured target behaviour that shaped the design

Sampled on both sealed builds, six counters every 60 VBlanks for 3,000 VBlanks
after the identity match:

| Observation | Value |
| --- | --- |
| VBlanks from identity match to the **first** gameplay tick | **~1,530**, and the same on both builds |
| `input_replay_ticks` vs `input_polls` vs `gGlobalTimer` | identical at every sample on both builds (live-input configuration) |
| VBlanks per tick, `id-a61d5203793986e7` | 1,200 VB / 109 ticks = **11.01** |
| VBlanks per tick, `id-c0352f297034f653` | 1,200 VB / 133 ticks = **9.02** |

Two consequences worth stating plainly:

* **The old 1,800-VBlank warm-up bought about 25–31 game ticks of gameplay.**
  ~1,530 of those 1,800 VBlanks are the CD/boot stage, in which no simulation
  runs at all. The warm-up was ~85% dead time, and the remaining ~15% is where
  the entire cross-build discrepancy lives.
* Because the boot stretch is wall-clock-identical on both builds, the *whole*
  route-position difference comes from the frame-rate difference — which is the
  thing every optimisation task deliberately changes.

---

## 4. The two-build demonstration

`tools/saturn/verify_route_warmup_parity.py`, four headless Ymir boots, two
sealed ELFs, no rebuild. Evidence:
`sprint2-t2_19d-warmup-parity.json`.

| Warm-up | Build | VBlanks | replay tick | `frame_serial` | cumulative `triangles_vdp1_emitted` |
| --- | --- | ---: | ---: | ---: | ---: |
| `--warmup-vblanks 1800` | `id-a61d5203793986e7` | 1,800 | **25** | 23 | **10,570** |
| `--warmup-vblanks 1800` | `id-c0352f297034f653` | 1,800 | **31** | 29 | **13,553** |
| `--warmup-ticks 30` | `id-a61d5203793986e7` | **1,852** | **30** | 28 | **13,054** |
| `--warmup-ticks 30` | `id-c0352f297034f653` | **1,788** | **30** | 28 | **13,054** |

**Read the last two rows.** Two different builds, two different renderers, two
different frame rates, stopped at different wall-clock depths — and the render
funnel agrees to the last triangle. Mario's action, position bits and facing
agree bit-for-bit as well. That is the claim, and it is measured, not argued.

**Read the first two rows.** Same wall-clock warm-up, **six route ticks apart**,
and **+28.2% cumulative VDP1 triangles** already plotted in the faster build.
That is the confound, and it is measured too.

### 4.1 What this demonstration does *not* show

Mario's coordinates are **identical in all four rows**, including at ticks 25
and 31. He is still stationary this early in the route, so his position cannot
discriminate tick 25 from tick 31 and is only a necessary condition, not
evidence. The render funnel is the discriminator that matters here — and it is
also the right one, because VDP1 plot time is a function of what is on screen.

---

## 5. Mutation results

`route_warmup.py`, ten mutations, host contracts re-run from a cleared bytecode
cache each time.

| # | Mutation | Result |
| --- | --- | --- |
| M1 | loop exits one tick early (sample early and return) | KILLED (11 errors) |
| M2 | budget exhaustion `break`s instead of raising | KILLED |
| M3 | saturation detector disabled | KILLED |
| M4 | overshoot check removed | KILLED |
| M5 | unreachable-target pre-flight weakened | KILLED |
| M6 | coarse step ignores the remaining-tick bound | KILLED (5 errors) |
| M7 | legacy warm-up drops the `exec.run_for` chunk cap | KILLED |
| M8 | replay tick offset moved one field (24 → 28) | KILLED |
| M9 | deprecated flag silently ignored | KILLED |
| M10 | mutual-exclusion check removed | KILLED |

**10/10 killed, 0% survival.**

M4 and M8 survived the first sweep and the tests were strengthened until they
did not: an overshoot case needs a target whose counter can step by more than
one (`ticks_per_step=3`), and the offset needed pinning to the struct rather
than to itself.

### 5.1 Live fail-loud verification, not just fakes

| Check | Result |
| --- | --- |
| Pre-flight: `--warmup-ticks 900` on a `bootstrap_ticks=600` build | `ValueError: warm-up tick target 900 is unreachable: this build applies route samples only for its first 600 ticks (live_input_mode=1, bootstrap_ticks=600)` in **0.44 s**, no emulator started |
| Runtime plateau: target **605**, pre-flight deliberately bypassed, on `id-c0352f297034f653` | raised after **7,501 VBlanks / 184 steps**: `replay tick is frozen at 600 while the simulation kept ticking (8 ticks with no route sample consumed)`. Evidence: `sprint2-t2_19d-saturation-live.json` |
| Mutual exclusion: `--warmup-ticks 30 --warmup-vblanks 1800` | rejected before Ymir starts |

The plateau case is the one that matters. It is the exact shape of the
`c28980a` bug — a loop that could have kept running, observed nothing new, and
eventually sampled at tick 600 while reporting 605. It raises instead.

### 5.2 Deprecated flag still behaves as before

`capture_route_counters.py --warmup-vblanks 1800` on `id-a61d5203793986e7`
lands on **replay tick 25** and advances exactly 1,800 VBlanks — reproducing
T2.14's starting position (`sprint2-t2_14-route-counters.json`, whose first row
is also replay tick 25) and its per-frame rates
(`triangles_emitted` 502.4 against T2.14's 502.8; the residual is 40 samples
against 120). Evidence:
`sprint2-t2_19d-route-counters-legacy1800-a61d5203.json`.

### 5.3 The primary FPS tool is unchanged

`capture_sourceboot_throughput.py` on `id-c0352f297034f653`, 30 presentation
events: `guest_fps_mean = 6.718146718146718` — the accepted baseline, to every
digit — with the new field `route_span: {start: 0, end: 31}`. Evidence:
`sprint2-t2_19d-throughput-routespan-c0352f29.json`.

---

## 6. How much prior evidence this affects

### 6.1 Confounded — re-read these with caution

| Report | Build | Route position of the sample | Why it is confounded |
| --- | --- | --- | --- |
| `sprint2-t2_16-idle-attribution.md` (+ `.json`, `-fullframe.json`) | `id-a61d5203793986e7` | starts at replay tick **25** | compared directly against T2.17 below |
| `sprint2-t2_17-epoch-stall.md` (`-idle-attribution-fullframe.json`) | `id-c0352f297034f653` | starts at replay tick **31** | **This is the pair T2.17 section 6.2 flagged.** Its "VDP1 plot time fell from ~9.86 VB to ~8.37 VB" compares tick-25-onward against tick-31-onward, at which point the faster build had already plotted 28.2% more triangles. The report was right to refuse the comparison; this task supplies the number that justifies the refusal. |
| `sprint2-t2_14-shadow-visibility-and-cull-funnel.md` (`-route-counters.json`) | `id-a61d5203793986e7` | ticks **25 → 144** | fine as a within-build funnel; not comparable tick-for-tick against any capture on another build |
| `sprint2-t2_19c-terrain-lod-census.json` | `id-c0352f297034f653` | starts at replay tick **31** | any comparison of it against an `a61d` census is 6 route ticks offset |
| `sprint2-t2_13-softfloat-matrix-path.md` (baseline `id-05046d9d5d8a5593`) vs `sprint2-t2_14-softfloat-profile-a61d5203.json` | two builds | both `--phases 1 --phase-vblanks 1800` | same defect under a different flag name (6.3) |

**The general rule.** Any comparison between two *different builds* that were
each warmed up by a fixed VBlank count is offset by
`1800 * (1/vb_per_frame_A - 1/vb_per_frame_B)` route ticks — about 6 ticks for
a 25% speed difference. Within-build comparisons and within-report trends are
unaffected. Everything measured before the sprint's builds started differing
in speed by more than a few percent is affected in principle and negligibly in
practice; T2.16 → T2.17 is where it stopped being negligible, which is exactly
what T2.17 said.

### 6.2 Not affected

* **The FPS numbers themselves.** `capture_sourceboot_throughput.py` has no
  warm-up and terminates on a *presentation-event count*, so its window is
  simulation-anchored by construction. Measured on `id-c0352f297034f653`: the
  30-event window covers replay ticks **0 → 31**. The `id-a61d5203793986e7`
  window is inferred — not measured, because that release directory's manifest
  points at an `obj/` ELF path the directory does not contain — to cover the
  same opening ~31 ticks, from the parity run's `frame_serial 28 @ tick 30` on
  both builds. **5.3538 vs 6.7181 stands.**
* Any single-build capture read only against itself.
* Host contract tests, build identity, release manifests.

### 6.3 A second wall-clock warm-up that still exists

`capture_softfloat_profile.py --phases 1 --phase-vblanks 1800` is a warm-up
wearing a different name, and it carries the identical defect. It was **not**
converted, because the multi-phase use of that flag ("advance, sample, advance,
sample" across a run) is legitimate and tick targets do not express it. The
single-phase use now prints a warning pointing at `--warmup-ticks`. T2.13 and
T2.14 both profiled this way.

### 6.4 Two tools this task did not convert, and why

`tools/saturn/capture_actor_command_share.py` and
`tools/saturn/capture_terrain_lod_census.py` (both untracked at the time of
writing) each carry `--warmup-vblanks` with default 1800 and the same
free-running chunk loop. **They are owned by other agents working in this
worktree concurrently and were not touched.** Converting them is one line each
plus the `route_warmup.add_warmup_arguments(parser)` / `plan_warmup` /
`execute_warmup` triple; their owners or a follow-up should do it before their
numbers are compared across builds. Until then, any cross-build claim from
those two tools inherits the defect this task removed.

---

## 7. Gates

| Gate | Result |
| --- | --- |
| Host contracts, `test_route_warmup.py` | **28 pass** |
| Existing capture contracts (`test_capture_sourceboot_throughput`, `test_capture_object_pool_occupancy`, `test_archive_a9a_baseline`) | **70 pass**, unchanged |
| Mutation sweep, 10 mutations | **10 killed, 0 survivors** |
| Live: tick warm-up lands on target, two sealed builds | **PASS** (4.1) |
| Live: unreachable target raises rather than sampling | **PASS** (5.1) |
| Live: deprecated flag reproduces prior behaviour | **PASS** (5.2) |
| Live: primary FPS tool unchanged | **PASS** (5.3) |
| Product build | **not run** — out of scope and explicitly excluded |
| Owner observation / GUI | **not run** — explicitly excluded |

---

## 8. Honesty

* **The route is only 600 ticks long in the shipped configuration.** Past
  `bootstrap_ticks`, `ticks_consumed` freezes and "route position" is not
  defined by any counter in the image — the game free-runs on neutral live
  input. Everything captured so far sits in ticks 0–170, so this is headroom;
  but a future task that wants to measure deep into a level cannot use this
  instrument as-is, and would need either a longer replay route or a second
  clock anchored at the bootstrap endpoint.
* **Mario's coordinates do not discriminate at these ticks** (4.1). The strong
  part of the demonstration is the render funnel, not the character state.
* **One run per configuration.** The tick warm-up is deterministic by
  construction, and two independent builds landing bit-identical is strong
  evidence, but run-to-run variance is still unmeasured — the same gap T2.16
  and T2.17 both recorded.
* **The `id-a61d5203793986e7` throughput route span is inferred, not
  measured** (6.2), because that release directory's manifest references
  `obj/sm64-saturn-sourceboot-e2.elf` and the directory has the ELF at its top
  level. That is a pre-existing packaging mismatch, not something this task
  introduced, and it was left alone.
* **This does not retroactively fix anything.** It makes future comparisons
  valid. Every number in section 6.1 stays as confounded as it was; the only
  change is that we can now say by how much.
* **The first live run of this code was wrong.** The coarse step was projected
  from a measured VBlank-per-tick rate, and the first rate sample straddled the
  boot/gameplay transition and came out ~3x too high, so the warm-up overshot
  its target by 4 ticks. It **raised** rather than reporting tick 34 as tick
  30, which is the behaviour this task exists to guarantee. The rate projection
  was then replaced with a hard bound — a coarse step never exceeds
  `remaining - 4` VBlanks, which cannot advance more than `remaining - 4` ticks
  even at one VBlank per tick — so no estimate of the build's speed can make it
  overshoot. `MAX_COARSE_VBLANKS` documents this.
* **The first mutation sweep was contaminated by a stale `__pycache__`.** M8
  changes `24` to `28` — the same file size — and Python reused the cached
  bytecode. The sweep in section 5 clears the cache and runs with `-B`. Anyone
  repeating a mutation sweep in this tree should do the same.

---

## 9. Reproduction

```bash
YMIR=<ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe>
IPL=<sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin>
A=<releases/2026-08-16_t2_13-product/id-a61d5203793986e7>
B=<releases/2026-08-16_t2_17-product/id-c0352f297034f653>

# 1. The two-build demonstration (~4 min, four headless boots).
python tools/saturn/verify_route_warmup_parity.py --ymir "$YMIR" --ipl "$IPL" \
  --build "id-a61d5203793986e7=$A/sm64-saturn-sourceboot-e2.cue::$A/sm64-saturn-sourceboot-e2.elf" \
  --build "id-c0352f297034f653=$B/sm64-saturn-sourceboot-e2.cue::$B/obj/sm64-saturn-sourceboot-e2.elf" \
  --mode vblanks=1800 --mode ticks=30 \
  --output docs/saturn/evidence/reports/sprint2-t2_19d-warmup-parity.json

# 2. Tick-warmed route counters (~50 s). The new default; no extra flags.
python tools/saturn/capture_route_counters.py --ymir "$YMIR" --ipl "$IPL" \
  --game "$A/sm64-saturn-sourceboot-e2.cue" --elf "$A/sm64-saturn-sourceboot-e2.elf" \
  --profile-address 0x2cdac4 --state-address 0x060f0d28 --numcalls-address 0x060dcb64 \
  --output docs/saturn/evidence/reports/sprint2-t2_19d-route-counters-tick30-a61d5203.json \
  --samples 40 --gap-vblanks 11

# 3. The deprecated path, unchanged. Add --warmup-vblanks 1800 to the above.

# 4. Host contracts and the mutation sweep.
python -B -m unittest tools.saturn.test_route_warmup
```

---

## 10. References

Per the standing owner instruction and the reference-code-first rule.

No third-party code was read, adapted or ported for this task. The warm-up loop
is a bounded poll over an existing JSON-RPC surface against counters already
present in the product ELF; there is no upstream to reuse. The one in-tree
precedent consulted was `tools/saturn/capture_route_views.py`, which already
pauses on `sourceboot_route_checkpoint.replay_ticks` — the same quantity, taken
from a publisher that is compiled out of every shipped build (1.4).

| Source | Files inspected | Reuse mode |
| --- | --- | --- |
| this repo, `tools/saturn/capture_route_views.py` | `:190-215` (route-tick pause loop) | pattern-only, in-tree |
| this repo, commit `c28980a` | `tools/saturn/capture_hwtest.py` (`YMIR_MAX_RUN_FOR_FRAMES`, `run_for_requests`) | pattern-only, in-tree — the chunk-and-validate rule reused verbatim in `route_warmup.run_vblanks` |
