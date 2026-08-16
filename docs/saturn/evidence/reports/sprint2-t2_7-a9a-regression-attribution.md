# Sprint 2 Task T2.7 — A9A regression attribution

- Date: 2026-08-15. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `5258e367`.
- Task: attribute and reverse the apparent 3.6x cadence regression against the
  owner-accepted A9A baseline. Investigation only; no source change, no build,
  no desktop launch.
- Baseline artifacts read-only and unmodified. Verified before use:
  ELF `1905ec8d42ea00ea2c000b5f53dd88f2079ffda8ceb67bcd5879e8e96acfc2e2`,
  ISO `1ccaef4f2a2d379d82879d3e823d84db135fdee1045d69aa8e0a60d150cfaf96`,
  CUE `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`.
  All three match the accepted record. Location on this machine is
  `.worktrees/sh2-native-math-purge/build/saturn/baselines/a9a-2026-08-05/`,
  not the recovery worktree.

## Headline

**There is no 3.6x regression. Roughly two-thirds of the apparent gap is a
measurement-basis artifact, and the entire real remainder is one node.**

- On A9A's own measurement basis the current build reads **3.971 FPS**
  (10 presentation events / 9 intervals) and **3.875 FPS** (30 events /
  29 intervals) — not 1.4634. Cadence is **stable, with no drift**.
- A9A on that same basis is **5.294 FPS**. The real gap is **1.37x**
  (15.48 vs 11.33 VBlanks per frame, **+4.15 VBlanks**), not 3.6x.
- **`spatial_admit` costs 4.565 VBlanks per frame — 77.2% of the
  pre-notification window and ~29% of the whole frame — on one call per
  frame.** It is the generic scene-admission module, which **A9A never ran**.
  Construction accounts for **+3.961 of the +4.150 VBlank delta (95%)**.
- Current construction minus `spatial_admit` is **4.95 VBlanks against A9A's
  5.56** — i.e. removing this one node restores construction to A9A parity.
- **A9A's legacy admission path is still compiled into the current binary**
  as a fail-closed fallback. The remedy is a bypass, not an optimization.
- **~5 FPS is recoverable.** **~8 FPS is not supported by any evidence** in
  this repository; A9A's own best observed interval was 6.00 FPS.

## 1. Step 1 — is 5.294 comparable? Yes, and the tooling is controlled

`5.294` was not produced by a lost or foreign tool. Its source is
`docs/saturn/evidence/reports/a9a-step11-overlap-throughput-repaired-2026-08-05.json`
(also narrated in `overlapped-render-pipeline-2026-08-03.md:2440`, "Nine
measurement intervals"). Controlled factors:

| Factor | A9A (2026-08-05) | Current (today) | Same? |
| --- | --- | --- | --- |
| Tool | `capture_sourceboot_throughput.py` | same | yes |
| Schema | `sm64-saturn-sourceboot-throughput-v1` | same | yes |
| FPS definition | `summarize_cadence`: `intervals * 60 / total_vblanks` | same code path | yes |
| Emulator | ymir-headless `fcc88d82b2ea7afd…3943` | byte-identical binary on disk | yes |
| BIOS | Sega Saturn BIOS (USA) | same | yes |
| Cadence rail | `_sourceboot_cadence_trace`, 76 B, v2 | same 76 B v2 | yes |
| Level / area / route | 9 / 1 / route 0, `camroute0` | 9 / 1 / route 0 | yes |
| Renderer tuple | `boot600 atan2v2 camv3 stage8 r6000 slave1 poly2 hot1 clip1 bsp1 frag0 pipe4` | identical | yes |

A9A's build directory name encodes the tuple, and every token matches the
current build's `effective_config`. This is the same slice, same content,
same camera route, same emulator, same metric. **The comparison is sound.**

Two caveats, both recorded honestly:

1. **A9A cannot be re-run by today's tool.** Its ELF has the cadence trace,
   boot trace, `_s_runtime` (104 B) and `_s_render_job_queue` (232 B), but
   **no `saturn_build_identity` symbol**, and no release manifest exists for
   it. Today's tool hard-requires both (`resolve_build_identity_symbol`,
   `--release-manifest`). A9A predates that identity rail. Re-running it would
   require a gate-bypassing harness; it was not needed, because the archived
   A9A JSON was produced by the *same schema and same summarizer*, and the
   emulator binary is byte-identical. Comparability is established at the
   definitional level rather than by re-execution.
2. **Symbols are underscore-prefixed in both ELFs** (`_s_runtime`); the tool
   resolves "local-or-underscore" names. A naive symbol check reports them
   absent. Noted so the next reader does not repeat the false alarm.

### 1.1 Where 1.4634 came from — the artifact

`1.4634` is **not** `summarize_cadence` output. T2.6's sustained run aborted
(`ObservationError: phase VBlank crossings exceed the observed interval`) and
the figure was computed by hand from the failure diagnostics as
`60 events x 60 Hz / 2460 vblanks_advanced = 1.4634`.

`vblanks_advanced` counts **from the start of observation, including the
pre-gameplay ramp before the first presentation edge**. In the current build
that ramp is ~1,549 VBlanks. Excluding it, the same run's own numbers give
`50 events / 784 VBlanks = 15.7 VBlanks/frame = 3.83 FPS` — which agrees with
both fresh captures below. A9A's archived run shows the same structure
(`vblanks_advanced` 1,272 against a 102-VBlank measured window), but its
headline was taken from the summarizer, so it was never contaminated.

**The two numbers were never on the same basis.** T2.3's 1.0866 and T2.2's
1.0682 are presumably contaminated the same way and should be re-derived
before any of them is quoted again.

### 1.2 Fresh measurements (this task)

Both runs are new, on the product build `e2-bob-identity-id-6eca5970628d581d`
(the T2.6 product image), release-manifest bound, `--startup-vblanks 4096
--max-vblanks 3600`.

| Run | Events | Intervals | Window (VB) | VB/frame | **FPS mean** | Median | 1% low |
| --- | --- | --- | --- | --- | --- | --- | --- |
| A9A archived | 10 | 9 | 102 | 11.333 | **5.294** | 5.000 | 5.000 |
| Current, matched basis | 10 | 9 | 136 | 15.111 | **3.971** | 4.000 | 3.750 |
| Current, long window | 30 | 29 | 449 | 15.483 | **3.875** | 3.750 | 3.750 |

Evidence: `sprint2-t2_7-current-10events.json`,
`sprint2-t2_7-current-30events.json`. Both `status: complete`.

**No drift.** Over 29 intervals the first-10 mean is 15.200 VBlanks and the
last-10 mean is 15.300. The build does not degrade over time; there is no leak
or fragmentation signature to chase.

## 2. Flat vs variable — the owner's diagnostic, refined

The hypothesis was that current cadence is flat where A9A's varied, implying a
fixed cost swamping the view. **The evidence partly refutes and usefully
sharpens this.**

| | Per-interval VBlank deltas | Min–max | Swing | FPS range |
| --- | --- | --- | --- | --- |
| A9A | 11,10,10,11,12,12,12,12,12 | 10–12 | **2 VB** | 6.00–5.00 |
| Current (10ev) | 14,14,14,15,15,16,16,16,16 | 14–16 | **2 VB** | 4.29–3.75 |
| Current (30ev) | 14…16…15…16 | 14–16 | **2 VB** | 4.29–3.75 |

Current cadence is **not flat** — it varies with the view by exactly the same
absolute amount as A9A (2 VBlanks), and both trend heavier along the route as
the camera moves into denser geometry. What changed is the **floor**: A9A's
cheapest frame is 10 VBlanks, the current build's cheapest is 14.

**A view-independent ~4-VBlank cost has been added to every frame while the
view-dependent component is untouched.** That is a precise signature of fixed
per-frame work, and it points directly at a once-per-frame node — which is
what the profile shows.

## 3. Step 3 — attributing the delta

### 3.1 Phase decomposition (per frame, VBlank crossings)

A9A from its archived per-interval `phases`; current from the 30-event run.

| Phase | A9A | Current | Delta |
| --- | --- | --- | --- |
| **frame (`vblank_delta`)** | **11.333** | **15.483** | **+4.150** |
| construction | 5.556 | 9.517 | **+3.961** |
| simulation | 4.556 | 5.828 | +1.272 |
| source tick | 4.556 | 5.828 | +1.272 |
| master finalization | 3.667 | 4.517 | +0.850 |
| slave work overlap | 2.778 | 2.931 | +0.153 |
| dropped VBlank credit | 4.667 | 6.759 | +2.092 |
| attributed | 10.111 | 15.345 | +5.234 |
| unattributed | 1.222 | 0.138 | −1.084 |

Phases overlap, so they do not sum to the frame. The load-bearing line is
**construction: +3.961 against a total frame delta of +4.150 — 95%.**

### 3.2 The single cause

From T2.6's profile (`sprint2-t2_6-meshlet-arithmetic.json`, 1,350 windows,
diagnostic build), ranked by share of the pre-notification window
(window mean 5.915 VBlank-equivalent):

| Node | Calls/frame | Share of window | **VBlank-equiv/frame** |
| --- | --- | --- | --- |
| **`spatial_admit`** | **1** | **77.18%** | **4.565** |
| `work_order` | 1 | 9.36% | 0.554 |
| `meshlet_depth_admit` | 31 | 5.46% | 0.323 |
| `meshlet_admit` | 1 | 2.53% | 0.150 |
| `position_set` | 1 | 2.21% | 0.131 |
| `meshlet_emit` | 1 | 1.26% | 0.075 |
| all others (16 nodes) | — | <1% each | 0.107 total |
| `actor_closure`, `meshlet_depth_emit` | **0** | 0.00% | 0.000 |

`spatial_admit` is `demo_spatial_admit()` at
`src/port/saturn/gfx/saturn_demo_render.c:4426`, gated by
`SATURN_DEMO_BSP_ORDER && !SATURN_DEMO_BSP_FRAGMENTS` (both satisfied in this
tuple: `bsp_order 1`, `bsp_fragment_flat 0`).

Adjusting for diagnostic instrumentation overhead (diagnostic construction
10.25 VB vs product 9.517, ~7%), `spatial_admit` costs **≈4.2–4.6 VBlanks per
frame in the product build** — which brackets the measured +3.961 construction
regression. **Current construction minus `spatial_admit` = 4.95 VBlanks,
against A9A's 5.556.** The arithmetic closes.

### 3.3 Why it is new — the structural diff

`fa21d44a..HEAD` is 641 commits, 157 files, +23,581 lines under `src/`.
Symbol-table diff of the two ELFs (A9A 8,168 symbols, current 8,482):

| Subsystem | A9A | Current | Runs per frame in this tuple? |
| --- | --- | --- | --- |
| **scene admission / residency** | **absent** | present | **yes — 4.565 VB** |
| actor identity / bank / bundle | absent | present | partly (`actor_closure` = 0 calls) |
| actor texture residency | absent | present | not in prenotify profile |
| **semantic audio** | **absent** | present | **yes — `semantic_audio: 1`** |
| HUD publish (layout/atlas) | absent | present | not in prenotify profile |
| geo-walk runtime | absent | present | not in prenotify profile |
| actor meshlets | absent | present | yes — 0.548 VB total |

The decisive one is scene admission. `demo_spatial_admit()` carries this
comment in-tree:

> "The generic package view is the production admission owner. BOB's legacy
> recursive painter remains below as a fail-closed compatibility fallback for
> a malformed generated header, but normal frames never enter that
> scene-specific path."

A9A ran that legacy recursive painter as its *production* path. The current
build routes every frame through the generic
`sm64_saturn_scene_admit_with_scratch()` (`saturn_scene_admission.c`, 428
lines, new since A9A) and keeps the cheaper legacy path compiled in but
unreachable on normal frames. **The regression is a path swap, and the old
path is still in the binary.**

### 3.4 Workload, not code speed? No

Object pool is *smaller* now (208 vs A9A's 240), the level/area/route/camera
are identical, and `slave_work_overlap` is essentially unchanged
(2.778 → 2.931). The build is not drawing materially more; it is spending
more master-side time deciding what to draw. This is a cost regression, not a
workload regression.

## 4. Step 4 — bisect: deliberately not run

The brief authorized a ~10-step cadence bisect if the cliff was not obvious.
**It is obvious and single-cause**, and the mechanism is identified in source
with the replacement path still present. A ~10-build bisect (~3 hours) would
confirm a commit SHA for a regression already attributed to a named function
with a measured per-frame cost. Skipped as pure cost. If the owner wants the
exact commit for the record, the cheap query is the first commit touching
`saturn_scene_admission.c` / the `demo_spatial_admit` call site, not a
cadence bisect.

## 5. Ranked REMOVE / BYPASS / RESTORE list

Recovery estimates convert VBlanks saved against the measured 15.483 VB/frame
long-window baseline (3.875 FPS).

| # | Action | Kind | VB/frame | Est. cadence | Risk |
| --- | --- | --- | --- | --- | --- |
| **1** | **Bypass generic scene admission; restore A9A's legacy BOB recursive painter as the production path for this tuple** | **RESTORE** | **−4.0 to −4.6** | **≈5.2–5.5 FPS** | **Low–Med** |
| 2 | Attribute and gate the simulation regression (+1.272 VB); `semantic_audio: 1` is the prime suspect and A9A ran none — it is a compile-time gate (`SATURN_FEATURE_SEMANTIC_AUDIO`) | BYPASS | −0.0 to −1.3 | +0.0 to +0.35 | Low (gate exists) |
| 3 | `work_order` (`demo_prepare_render_work_order`) — 9.4% of window, 1 call/frame; likely redundant with admission output once #1 lands | REMOVE | −0.55 | +0.14 | Med |
| 4 | Residual meshlet + position nodes (`meshlet_depth_admit` 0.323, `meshlet_admit` 0.150, `position_set` 0.131) | OPTIMIZE | −0.60 | +0.15 | Med |
| 5 | Dead-but-linked subsystems absent from A9A (actor texture residency, HUD publish, geo-walk runtime, actor bank) — confirm zero per-frame cost, then gate out for size/margin | REMOVE | ~0 | 0 (memory only) | Low |

Notes on ranking:

- **#1 is the whole task.** It alone restores construction to A9A parity and
  lands cadence at or near the accepted 5.294 baseline. Everything else is
  rounding by comparison. The bias the brief asked for — delete or gate rather
  than optimize — is fully satisfied by #1, because the replacement path is
  already compiled in.
- Risk on #1 is bounded but real: the generic module exists to serve
  multi-level/generic scenes, which is the stated D6 renderer strategy. Falling
  back to the BOB-specific painter is a **tuple-scoped bypass**, not a
  permanent architecture reversal. It must be gated so generic scenes still
  use the generic path. Visual equivalence against the A9A route must be
  confirmed before acceptance (per constitution §3).
- `dropped_vblank_credit` (+2.092) is a **symptom**, not an item: it is credit
  lost because the frame overran, and should fall out with #1.
- `actor_closure` and `meshlet_depth_emit` read **0 calls / 0 ticks** — the
  feature-off tuple is already clean there. Nothing to reclaim.

## 6. Verdict

**~5 FPS: recoverable, and by a bypass rather than a grind.** Item #1 is a
single gated path swap to code already present in the binary, worth
~4.0–4.6 VBlanks/frame, landing ≈5.2–5.5 FPS. It does not require the dozen
T2.6-sized wins the sprint was heading toward.

**~8 FPS: not supported by any evidence in this repository.** 8 FPS is 7.5
VBlanks/frame. A9A's own archived capture ranges 10–12 VBlanks — its best
single interval is **6.00 FPS**, its mean 5.294, its 1% low 5.00. No capture
found in `docs/saturn/evidence/` records A9A above 6.00. The recollection is
most likely of a peak on a light view, and the closest corroborated figure is
6.00 FPS. Recovering that would need item #1 **plus** items #2–4 landing near
their optimistic ends, and even then ~5.5–6.0 is the honest ceiling for this
architecture on this route without cutting content.

**What the sprint should stop doing:** quoting `1.4634`, `1.0866`, and
`1.0682` as cadence. They are ramp-contaminated ratios, not the summarizer's
metric, and they overstate the regression by ~2.7x. All future cadence claims
should come from `summarize_cadence` at a stated interval count, or explicitly
subtract the pre-presentation ramp.

## 7. Reproduction

```
# Baseline hashes (read-only; do not rebuild or relabel)
sha256sum .worktrees/sh2-native-math-purge/build/saturn/baselines/a9a-2026-08-05/*

# Current build, A9A's exact basis (9 intervals)
B=<abs>/build/saturn/sourceboot/e2-bob-identity-id-6eca5970628d581d
python tools/saturn/capture_sourceboot_throughput.py \
  --ymir <abs>/ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe \
  --ipl  "<abs>/sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin" \
  --game $B/sm64-saturn-sourceboot-e2.cue \
  --elf  $B/obj/sm64-saturn-sourceboot-e2.elf \
  --release-manifest $B/saturn-release-manifest-v1.json \
  --output docs/saturn/evidence/reports/sprint2-t2_7-current-10events.json \
  --startup-vblanks 4096 --max-vblanks 3600 --presentation-events 10
# long window: --presentation-events 30, output ...-30events.json

# A9A's archived measurement (do not re-run; no build identity symbol)
docs/saturn/evidence/reports/a9a-step11-overlap-throughput-repaired-2026-08-05.json
#   observation.measurement.guest_fps_mean = 5.294117647058823
```

Emulator identity for all runs:
`fcc88d82b2ea7afdf400bcf67d45139d02354379388f7f9dba731b63a38d3943`
(3,255,296 B) — identical to the binary recorded in A9A's own artifact block.
