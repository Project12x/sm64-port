# Sprint 2 Task T2.17 — the per-field epoch stall, collapsed

- Date: 2026-08-16. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `4a3fca24`.
- Task: collapse the per-field epoch stall T2.16 identified, without losing the
  invariant that rule was added to protect.
- Verdict: **collapsed.** Measured **6.7181 FPS mean / 8.9310 VB per frame**
  against the `id-a61d5203793986e7` baseline of **5.3538 FPS / 11.2069 VB** —
  **+25.5% FPS, −2.2759 VB/frame**, `summarize_cadence`, 30 presentation
  events, on-target identity **MATCH**.
- **The frame did not land against VDP1 at ~9.86 VB. It landed at 8.9310 VB,
  past that prediction.** Section 6 says what that does and does not license.
- New build: `id-c0352f297034f653`, ELF SHA-256
  `2933c5d5d6d1399243d5b838b63c16f1b3e1a6ba2a79587edd9c3f7cb8c2fecd`.
- Evidence: `sprint2-t2_17-throughput-30events.json` (cadence, authoritative),
  `sprint2-t2_17-idle-attribution-fullframe.json` (VDP1 re-measurement).
- Commits: `c818ab7e` (scheduler + tests + gate), plus the docs commit carrying
  this report.

---

## 1. What the epoch rule guaranteed, and why it existed

The rule is three stamps, all in `sm64_saturn_frame_pipeline_step()`
(`src/port/saturn/runtime/saturn_frame_pipeline.c`):

| Stamp | Effect before T2.17 |
| --- | --- |
| `render_service_vblank`, set by `SERVICE_RENDER_JOBS` | at most one service per observed field |
| `transfer_poll_vblank`, set by `POLL_TRANSFERS` | at most one poll per observed field |
| **both**, set by `PUBLISH_FRAME` | a promoted bank could start neither until another VBlank was observed |

It is not incidental. It is a **reviewed invariant**, added as a NO-GO repair
during A9 Steps 1--4. The review's exact finding
(`overlapped-render-pipeline-2026-08-03.md:1826-1828`):

> *"Publication also resets generation-local SERVICE/POLL flags, permitting
> more work for a promoted generation during the same observed VBlank."*

and the repair as recorded at `:1845-1847`: *"field-global epochs keep
SERVICE/POLL bounded across publish/promote."* Independent rereview at `:1857`
confirmed "global per-field work epochs" as a GO condition. So the starting
position is that this rule is load-bearing until proven otherwise, which is how
it was treated.

**Restated in terms the hardware cares about, it buys exactly two guarantees:**

- **G1 — one publication per observed field.** `PUBLISH_FRAME` is the only
  action that reaches `sourceboot_present_generation` with a new bank
  (`main.c:1879-1880`), which calls `vdp1_sync_render()` (`main.c:1327`). In
  libyaul's variable-interval mode that writes `PTMR = VDP1_PTMR_PLOT`
  immediately (`third_party/libyaul/.../vdp_sync.c:981-986`), so one
  publication is one plot start and one frame-buffer change request. Two in a
  field would mean a frame displayed for zero fields.
- **G2 — no command-VRAM overwrite in the field whose publication started the
  plot.** The overwrite is issued by the *first* `POLL_TRANSFERS` of a
  generation, which fences on `vdp1_sync_busy()` and then DMAs the new list
  over the shared resident destination (`main.c:1731`, `:1788-1789`).

Both guarantees ride on **`POLL_TRANSFERS` and `PUBLISH_FRAME`**. Neither rides
on `SERVICE_RENDER_JOBS`, and neither rides on a poll that is not the
submitting one. The blanket per-field rule was over-broad by exactly that much,
and T2.16 measured the over-breadth at **2.1903 VB/frame** — a raster spin
interleaving **0.141 VB/frame** of transport work, a 1:14 ratio.

### 1.1 Which waits the over-breadth actually caused

Reading T2.16 section 3.3's ordered run log against the state machine, the
2.19 VB is two full-field waits plus change:

| Wait | Size (T2.16) | Cause |
| --- | ---: | --- |
| between `POLL_TRANSFERS` #1 and #2 | 0.9626 VB | the poll epoch. The submitting poll kicks the DMA; the queue has not retired when it returns, and the next poll is a field away |
| after `PUBLISH_FRAME` | 0.8979 VB | the service epoch stamped by publication. The promoted snapshot cannot begin its render |
| tail of the last `SERVICE` | 0.0673 VB | the master finishing a field it had already overrun — genuinely near-free |

The ~10 remaining excursions per frame have a median run length of 30 cycles:
the master calls the wait when it is already at the boundary. Those are not
worth chasing and were not chased.

---

## 2. The bank-ownership safety argument, in full

This is the argument the task required before touching anything.

**Claim: admitting `SERVICE_RENDER_JOBS` for the promoted generation in the
publication field cannot race the plot that publication just started.**

*Step 1 — service writes only into a frame bank, never into VDP1 VRAM and
never to a VDP1 register.* `sourceboot_frame_service_render` (`main.c:1503`)
either polls an in-flight render (`sm64_saturn_demo_render_poll_frame`) or
begins one via `sm64_saturn_render_overlap_phase_begin` +
`sm64_saturn_vdp1_frame_bank_begin_build`. The command bytes it produces land
in `bank->command_storage`. They reach VDP1 only through
`sm64_saturn_vdp1_frame_bank_submit_transfers`, which is called from
`POLL_TRANSFERS` and nowhere else (`main.c:1788`).
`sm64_saturn_render_overlap_phase_begin`
(`saturn_render_overlap_phase.c:21-35`) has no field or VBlank precondition —
it requires only `!phase->active`, and the phase is cleared at the previous
generation's terminal.

*Step 2 — a free bank is guaranteed to exist at that instant.* There are two
banks (`SM64_SATURN_VDP1_FRAME_BANK_COUNT 2`). Service is admitted only after
`sm64_saturn_frame_pipeline_publish_complete(gen, true)`, and that is called
with `succeeded = true` only when `sourceboot_frame_publish` has already run
both of:

- `sm64_saturn_vdp1_frame_bank_publish`
  (`saturn_vdp1_frame_bank.c:446-466`) — which itself *requires*
  `state == TRANSFERRING`, `resident_list_armed`, and
  `command_transfer_obligation == RETIRED`, so **the published generation's DMA
  is provably complete before its plot starts** — and then sets the new bank
  `PUBLISHED` and `banks->published = bank`;
- `sm64_saturn_vdp1_frame_bank_retire(previous_generation)` (`:468-484`), which
  skips the current `banks->published` and returns the old published bank to
  `FREE`.

So immediately after a successful publication exactly one bank is `PUBLISHED`
and one is `FREE`, and `begin_build` (`:164-176`) takes the first `FREE` one.
On the failure path `published == false`, `publish_complete(gen, false)` runs
the reuse arm, **no promotion occurs**, and no service of a promoted generation
is admitted at all.

*Step 3 — the freed bank is not the one VDP1 is reading.* VDP1 plots from the
**resident** command VRAM, not from a bank. The freed bank's contents were
DMA'd into that VRAM one generation earlier and have since been overwritten by
the generation publication just started. Nothing holds a reference to it.

**Claim: admitting follow-up `POLL_TRANSFERS` inside the submit field cannot
overwrite VRAM under a running plot.** `sourceboot_frame_poll_transfers`
(`main.c:1720-1730`) takes the submit branch only when
`sourceboot_vdp1_transfer_pending == NULL`; that pointer is set at the end of
the same branch (`:1789`) and cleared only at publication (`:1854`). Every
later call in the same generation therefore skips the fence, skips
`submit_transfers`, and reaches only
`sm64_saturn_vdp1_frame_bank_poll_transfers` — which bottoms out in
`saturn_dma_queue_poll` (`slavedriver_dma_queue.c:282-310`), a read of
`scu_dma_level_busy(0)` or the CPU-DMAC status word. **A follow-up poll writes
nothing.**

**What is therefore kept, and what is dropped:**

| | Before | After |
| --- | --- | --- |
| `SERVICE` per field | 1 | 1 (unchanged) |
| Submitting `POLL` per field | 1 | 1 (unchanged) |
| Follow-up `POLL` per field | 1 | unbounded, **inside the submit field only** |
| `SERVICE` in the publication field | refused | **admitted** |
| Submitting `POLL` in the publication field | refused | refused (unchanged) |
| `PUBLISH` per field | 1 | 1 (unchanged) |

G1 survives because publication still stamps `transfer_poll_vblank`: a promoted
generation cannot submit, so it cannot reach `PUBLISH` (which requires
`transfer_completed_valid`) in that field either. G2 survives for the same
reason.

### 2.1 Termination, and what happens when the queue does not retire

The free window is bounded by the field itself: `polling_in_submit_field`
requires `transfer_submit_vblank == last_vblank_count`, and `last_vblank_count`
tracks `sourceboot_vblank_out_count`, which the VBlank-OUT ISR advances
(`main.c:989-995`). When the field ends, the conservative one-poll-per-field
schedule resumes, with its `REUSE_PREVIOUS_FRAME` presentation edges intact.
There is no unbounded spin and no path that starves presentation. This is
deliberate, it is asserted, and it is what the mutation
`SM64_SATURN_FRAME_PIPELINE_TEST_FREE_POLL_EVERY_FIELD` exists to catch.

### 2.2 The one thing this change makes newly load-bearing

The overwrite fence. `vdp1_sync_busy()` is true from `vdp1_sync()` at
presentation until the VBlank-OUT that completes the frame-buffer change
(`vdp_sync.c:299-311`, `:1062-1091`), and `vdp1_sync_wait()` spins with
interrupts unmasked, so it cannot deadlock against its own ISR. **T2.8 measured
it at 0 waits in 1,349 fence events and called it inert (T2.8 section 9
item 3) — but that measurement was taken on a 15.483 VB/frame instrumented
build, where slack hid it.** At 8.93 VB/frame the slack is gone and the fence
is what separates the next DMA from the running plot. It is correct code and it
is now doing a job. Flagged for the owner in section 8.

---

## 3. What changed

`src/port/saturn/runtime/saturn_frame_pipeline.{c,h}`, commit `c818ab7e`.

1. **Publication stops stamping the service epoch.** The `PUBLISH_FRAME` arm
   keeps `transfer_poll_vblank` and drops `render_service_vblank`.
2. **Follow-up polls are freed inside the submit field.** New state
   `transfer_submit_vblank` / `transfer_submit_vblank_valid`, set on the
   submitting poll and cleared everywhere `transfer_started` is cleared. The
   poll gate becomes `polled_this_vblank && !polling_in_submit_field`.

No change to `main.c`, to the transport path, to the frame-bank contract, or to
the render graph. The frame pipeline remains the only file whose behaviour
moved.

### 3.1 What moves that is not a pure win

Two consequences, both benign, both recorded rather than discovered later:

- **The recovery sim tick becomes admissible one field earlier.**
  `render_service_started` gates the simulation arm. Admitting service in the
  publication field sets it a field sooner. The normal-plus-recovery budget is
  unchanged; only its timing moves. Asserted at failure code 165.
- **`transport_presentation_count` rises**, because follow-up polls are counted.
  Measured: 119 calls over 30 presentations, **4.0 per interval — identical to
  the baseline's 4.0**. In practice the queue retires in the same number of
  polls; the polls simply no longer sit a field apart. The *crossings* figure,
  which is the one that feeds phase attribution, is 4 in the whole run.

---

## 4. Assertions and mutation results

`tools/saturn/frame_pipeline_test.c`, gated by `verify-frame-pipeline`.

**No existing assertion was weakened, changed in expectation, or removed.** All
eleven pre-existing `WAIT_VBLANK` assertions still hold verbatim. Three of them
(failure codes 13, 77, 125) assert that publication does not reopen the field's
service slot — and they still pass, because in each of those fixtures the
field's single service slot had already been consumed by the *previous*
generation's own service. That is the honest reason they survive, and it is
precisely why a new fixture was needed to exercise the changed path at all.

**Three tests added:**

| Test | What it pins |
| --- | --- |
| `test_publication_field_admits_service_but_not_transfer` | publication admits exactly one service for the promoted generation, and still refuses its command-VRAM overwrite |
| `test_publication_field_consumes_the_transfer_slot` | the isolating case — a frame whose transfer retires after its last poll publishes in a field that issued **no poll of its own**, so nothing but publication's stamp refuses the promoted generation's overwrite |
| `test_followup_polls_are_bounded_to_the_submit_field` | follow-ups admitted in the submit field, never publishing an unretired transfer; the conservative schedule and its reuse edges resume in the next field; the submit itself stays one per field |

**Mutation matrix.** Three new mutations were added to `verify-frame-pipeline`
alongside the three already there. Each perturbs one of the three things the
task named.

| Mutation | Perturbs | Result |
| --- | --- | --- |
| `..._PUBLISH_OPENS_TRANSFER` | the bank-ownership condition (publication stops consuming the transfer slot) | **FAIL** (code 204) |
| `..._FREE_POLL_EVERY_FIELD` | the swap point (free polling in every field, not only the submit field) | **FAIL** (code 34) |
| `..._SUBMIT_UNGATED` | the transport interleave (the submitting poll itself ungated) | **FAIL** (code 163) |
| `..._FOUR_TICK` (pre-existing) | catch-up budget | **FAIL** (code 20) |
| `..._READD_CREDIT` (pre-existing) | repeated-observation credit | **FAIL** (code 96) |
| `..._PUBLISH_INCOMPLETE` (pre-existing) | incomplete-bank publication | **FAIL** (code 136) |
| nominal | — | **PASS** |

**The first of those three survived the suite's first draft**, and that is the
most useful thing in this section.
`test_publication_field_admits_service_but_not_transfer` alone could not catch
it: in that fixture the publication field had also issued a poll, so
`transfer_poll_vblank` matched the field either way and the refusal was
over-determined. `test_publication_field_consumes_the_transfer_slot` exists
solely to remove that over-determination. An assertion suite that passes under
a broken schedule is worthless, and this one was, for one mutation, until it
was not.

**Cross-check against vacuity:** the pre-change pipeline compiled against the
post-change suite **fails** (code 136), so the new assertions are not trivially
satisfied.

---

## 5. Measured cadence

`summarize_cadence` only. 30 presentation events, 29 intervals, on-target
identity **MATCH** (`observed_sha256 == expected_sha256`, `d88b17a9…23b5`),
`status: complete`.

| | Baseline `id-a61d5203793986e7` | **T2.17 `id-c0352f297034f653`** | Delta |
| --- | ---: | ---: | ---: |
| FPS mean | 5.3538 | **6.7181** | **+25.5%** |
| FPS median | 5.4545 | **6.6667** | +22.2% |
| FPS 1% low | 5.0 | **6.0** | +20.0% |
| VB per frame | 11.2069 | **8.9310** | **−2.2759** |
| target VBlank delta over 29 intervals | 325 | 259 | −66 |

**Interval distribution.** Baseline `{10: 2, 11: 19, 12: 8}`; T2.17
`{7: 1, 8: 2, 9: 24, 10: 2}`. The mode moved from 11 fields to 9, and the worst
interval improved from 12 fields to 10.

**`presentation_generation_delta == 1` on all 29 intervals in both runs.** No
generation is skipped and none is published twice — the direct scheduler-level
check against a dropped or duplicated field.

**Queue health, identical to baseline:** `qn = qr = 30`, `qw = qf = qq = 0`,
`master_failures = slave_failures = 0`, `qm = [0,0,0,0]`, `qs = [1,1,1,1]`.

**The measured saving (2.2759 VB) slightly exceeds T2.16's measured stall
(2.1903 VB).** Two contributions, in order of confidence. T2.16's figure came
from one contiguous window spanning 1.7525 frames and is a single-sample
estimate with no repeat capture — its own section 8.4 says so. And the master
now runs fewer `REUSE_PREVIOUS_FRAME` actions per frame, each of which carried a
`sourceboot_present_generation` call and a telemetry pass, so a little master
*work* left the frame alongside the stall. The frame at 8.9310 VB is below
T2.16's master-work figure of 9.0166 VB, which is only consistent with work
having fallen too. **This is reasoning, not measurement**, and it is offered as
such.

### 5.1 The T2.11 concurrency rail — read this before quoting any phase number

**The allowance is needed on 28 of 29 intervals, against 0 of 29 at baseline.**
Excess over the interval runs 1--3 fields (mean 2.07) with an allowance of
2--4. This is the regime the T2.15 skip-geo-walk ceiling diagnostic hit at
9.48 VB (29 of 29), and it is now the normal operating point.

| Per interval | Baseline | T2.17 |
| --- | ---: | ---: |
| simulation crossings | 4.8276 | 4.8276 |
| construction crossings | 5.6897 | 6.0000 |
| transport/presentation crossings | 0.0000 | 0.1034 |
| concurrent-phase allowance | 3.6897 | 3.8276 |
| attributed | 10.5172 | 10.9310 |
| **actual interval** | **11.2069** | **8.9310** |

**Consequence, stated plainly: the phase profile is no longer a decomposition
of this frame and must not be quoted as one.** Attributed exceeds the interval
because `master_finalization` is stamped from the *slave* and double-charges
its overlap with `simulation`. T2.11's own comment
(`capture_sourceboot_throughput.py:539-563`) predicts exactly this, and the
overlap grows as the frame tightens.

**The cadence figures are unaffected.** `summarize_cadence` derives FPS from
`observed_vblank_generation` deltas between adjacent presentation edges
(`:638-664`); it never reads the phase counters. The FPS and VB/frame numbers
above stand; the four phase rows do not.

Note also that the transport phase measured **zero** VBlank crossings at
baseline. The stall was never inside a phase bracket — `WAIT_VBLANK` is
dispatched outside `sourceboot_phase_accumulate` — which is exactly why it took
a new instrument (T2.16) to find it, and why it was invisible to every cadence
report before that.

---

## 6. VDP1 re-measurement — the prediction did not hold

T2.16 predicted the frame would land at **~9.86 VB against VDP1**, from
`EDSR.CEF` set in 12.0% of 300 strided samples. **It landed at 8.9310 VB**, and
the re-measurement says the picture is both better and murkier than that.

Same instrument, same parameters as T2.16 (`capture_idle_attribution.py`, one
contiguous window of 6,000,000 master instructions, slave stride 16, VDP witness
stride 20,000), run on `id-c0352f297034f653`. On-target identity **MATCH**.
Traced span **7,318,661 cycles = 16.3566 VBlanks = 1.83 frames**.

### 6.1 The stall is gone. This is measured, not inferred.

| Quantity | T2.16 (`id-a61d5203793986e7`) | **T2.17 (`id-c0352f297034f653`)** |
| --- | ---: | ---: |
| master idle share | 19.544% | **0.000000** |
| master idle VB/frame | 2.1903 | **0.0000** |
| `_sm64_saturn_source_runtime_wait_vblank` excursions traced | 24 | **0** |
| `master_wait_call_sites` | `{_main+0xea8: 24}` | **`{}` (empty)** |
| joint `idle:vblank-spin \| idle:notification-wait` | 19.545% / 2.1903 VB | **cell absent** |
| joint `idle \| work` | 0.00% | 0.00% (unchanged) |
| slave idle | 79.145% / 8.8697 VB | 73.040% / 6.5232 VB |
| combined idle | 49.34% | **36.52%** |

**Over 1.83 contiguous frames the master did not enter the raster spin once.**
This is the confirming measurement T2.16 section 9 item 1 asked for, and it is a
direct observation rather than a difference of estimates.

The rest of the master profile is undisturbed, which is what "the stall was
removed and the work was not" looks like:

| Master symbol | T2.16 VB/frame | T2.17 VB/frame |
| --- | ---: | ---: |
| `_sm64_saturn_source_runtime_wait_vblank` | **2.1903** | **absent** |
| `_demo_render_finalize` | 1.0044 | 0.9655 |
| `___mulsf3` | 0.6269 | 0.6730 |
| `_sm64_saturn_ztreme_frustum_aabb` | 0.6490 | 0.6209 |
| `_demo_render_prepare_publish` | 0.5652 | 0.5398 |
| `___addsf3` | 0.4711 | 0.5014 |
| `_sm64_saturn_matrix_mul.isra.0` | 0.4141 | 0.4365 |

Every row except the first is within ~7% of where it was in absolute VB. The
top entry simply left the table.

### 6.2 VDP1 is now nearly saturated — but the absolute number moved, and the comparison is confounded

| Quantity | T2.16 | **T2.17** |
| --- | ---: | ---: |
| `EDSR.CEF` set | 36 of 300 (12.0%) | **19 of 300 (6.33%)** |
| **VDP1 plotting** | 88.0% of the frame | **93.67% of the frame** |
| implied plot time | ~9.86 VB | **~8.37 VB** |
| `COPR` range (command index = `COPR/4`) | 6 → 547, 30 distinct | 2 → 548, **43 distinct** |
| `TVSTAT` VBlank set (bias check, ~14.8% expected) | 15.0% | 15.33% |

**The direction T2.16 predicted is confirmed and then some: VDP1 now occupies
93.67% of the frame, and only ~0.57 VB/frame remains in which it is idle.** The
command list still sweeps end to end and `CEF` is still reached, so VDP1 is
completing a full plot every frame — it is not being cut short.

**But the absolute plot time apparently fell from ~9.86 VB to ~8.37 VB, and
this change cannot have caused that.** Nothing here touches the command list.
Two explanations, neither proven:

1. **Route-position confound, and this is the more likely one.** Both captures
   warm up a *fixed* 1,800 VBlanks after the identity match. The game now runs
   25.5% faster, so 1,800 VBlanks now advances the replay ~25% further along the
   route. **The two captures are not looking at the same scene**, and plot time
   is a function of what is on screen. The `COPR` ranges (6→547 against 2→548)
   show a similar list *length*, but list length is not fill area.
2. **Binomial sampling error.** 36 of 300 has a standard error of 1.9
   percentage points, so T2.16's 12.0% carries a 2σ band of roughly 8.2--15.8%,
   i.e. a plot of **9.4--10.3 VB**. 8.37 VB is outside that, so sampling alone
   does not close the gap — but it widens both figures' error bars enough that
   neither should be quoted to three digits.

**What this licenses, and what it does not.** It licenses the promotion of the
fill-rate levers T2.8 demoted — user clipping, command-count LOD, the Mario
double-emit — **more strongly than T2.16 argued**, because VDP1 is now 93.67%
occupied and is plainly the wall. It does **not** license quoting "VDP1 plots
8.37 VB/frame" as this build's constant: that figure is scene-dependent and was
measured at a route position the baseline never visited.

**Recommended repair for the next task that needs this number:** warm up by
*simulation ticks* or presentation generations rather than by a fixed VBlank
count, so two builds at different frame rates are compared at the same point on
the route. Every idle-attribution capture taken so far has this defect; it did
not matter while builds were within a few percent of each other, and it matters
now.

---

## 7. Gates

| Gate | Result |
| --- | --- |
| `verify-frame-pipeline` (nominal + 6 mutations) | **RESULT OK** |
| `verify-dual-frame-bank` | **RESULT OK** |
| `verify-vdp1-frame-bank` | **RESULT OK** |
| `verify-render-overlap-integration` | **RESULT OK** |
| `verify-memory-map` | **RESULT OK** |
| `verify-sourceboot-presentation-boundary` | **FAIL — pre-existing, not a regression** |

`verify-sourceboot-presentation-boundary` is a **source-text** gate: its only
input is `src/port/saturn/sourceboot/main.c`
(`tools/saturn/test_sourceboot_presentation_boundary.py:18-19`), matched against
literal C snippets. This task did not modify `main.c` — `git status` shows it
unmodified throughout — so the gate fails identically at `4a3fca24`. It broke
when `main.c` was last edited (`c9476fd6`, T2.8's diagnostic fence work) and its
text stopped matching `BOOTSTRAP_VDP2_BEGIN`. **This is a fifth instance of the
brittle-host-gate class STATE.md already catalogues** — the first that is a
literal-text drift rather than an MSYS path defect — and it has been sitting in
`verify-all`'s neighbourhood verifying nothing since T2.8. It is reported, not
worked around.

Build: the 27-variable product tuple from `sprint1-stage1-link-smoke.md` with
`SATURN_OBJECT_POOL_CAPACITY=208` and `SATURN_DIAGNOSTIC_MODE=0`, matching
`tools/saturn/profiles/sourceboot-bob-demo-v1.json`'s `release_config`. All
tracked source was committed before `sourceboot` was invoked; no profile JSON
diagnostic flip was made, and `git diff` is empty. Exit 0, no repair needed —
g15 republished cleanly and the identity gate passed first time. Artifacts
preserved to `releases/2026-08-16_t2_17-product/id-c0352f297034f653/`.

**Identity and hashes:**

| Artifact | SHA-256 |
| --- | --- |
| ELF | `2933c5d5d6d1399243d5b838b63c16f1b3e1a6ba2a79587edd9c3f7cb8c2fecd` |
| ISO | `49b68a07144b2b58ad6781dadb35404e038e0f9d75eaf7c354f8fa88e1d4c9cc` |
| CUE | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` |
| release manifest | `1d73590985a2702db2ccdbc93bb85e376248c9bd0808ea2aed5092938d3b35f3` |
| on-target identity probe | `d88b17a9fd1c42e57e3a0c76cd790a506b6a7e778f0a37dfc63f1850850d23b5` (**match**) |

---

## 8. What the owner should watch for

The screen content should be unchanged. What moved is *when* the master issues
each action, not what it draws. Two independent scheduler-level checks agree:
`presentation_generation_delta == 1` on all 29 intervals, and the frame-bank
queue retired 30 of 30 generations with zero faults.

**Still, this is a frame-scheduler change and it is owner-visible if it is
wrong.** In order of what this change could plausibly break:

1. **Tearing, flicker, or a partially drawn frame.** The failure mode would be
   the next generation's DMA landing in resident command VRAM while VDP1 is
   still plotting. The frame is now 2.28 VB shorter with the command list
   unchanged, so the margin that used to hide this is gone and the
   `vdp1_sync_busy()` fence is what holds the line. It is real code
   (section 2.2), but T2.8 never saw it fire. **This is the single most
   important thing to look at.**
2. **A hang or freeze, most likely at a scene transition or a heavy view.** If
   VDP1 ever fails to reach `EDSR.CEF`, `vdp1_sync_wait()` has no deadline.
   That is pre-existing code, but this change is what can first make it block.
3. **A duplicated or dropped field.** Not seen in 29 intervals, but 29 intervals
   is roughly 4.3 seconds of one replay route.
4. **Anything scene- or content-dependent.** Everything here was measured on the
   BOB replay route at one camera path. A heavier scene has a longer plot and
   therefore less margin, not more.

A hang, visible tearing, or flicker should be treated as this change's fault
first and reverted at `c818ab7e`, which is self-contained.

---

## 9. Honesty

- **The VDP1 cap prediction did not hold and the reason is not proven.**
  Section 6. The frame went past ~9.86 VB to 8.93 VB.
- **The saving exceeded the measured stall** (2.2759 vs 2.1903 VB). Section 5
  gives the reason it can, and labels it as reasoning rather than measurement.
- **The phase profile is no longer usable** at this frame length (section 5.1).
  That is a real loss of instrument, caused by this change's success, and it
  affects every task that follows.
- **One gate fails and it is not this task's** (section 7). It was already
  failing.
- **No live owner observation was made.** No emulator GUI was launched. Every
  number here is from headless Ymir through `summarize_cadence` and
  `capture_idle_attribution`. The product gate is an owner-observed CUE, and
  this is not one.
- **One route, one camera path, one capture each.** No repeat run, so
  run-to-run variance is unmeasured — the same gap T2.16 recorded.

---

## 10. References

Per the standing owner instruction and the reference-code-first rule.

| Repo | Pinned SHA | License | Files inspected | Reuse mode |
| --- | --- | --- | --- | --- |
| `work/upstream/slavedriver-engine` | `a8986591557b6e680550d3c23970284d3b38ff8f` | GPL-3.0-or-later | `SRUINS.C:2235-2270` (the pacing loop), `V_BLANK.C:36,130` (`vtimer`), `FILE.C:241`, `FLASH/INITMAIN.C:471` (`SPR_WaitDrawEnd` call sites) | **pattern-only** — nothing copied, adapted, or ported |
| `third_party/libyaul` (vendored) | as vendored (`6012f79f`) | MIT | `scu/bus/b/vdp/vdp_sync.c:170-191` (the documented VDP1 state machine), `:279-322` (`vdp1_sync`, `vdp1_sync_busy`, `vdp1_sync_wait`), `:386-392` (`vdp1_sync_force_put`), `:478-500` (`vdp1_sync_render`), `:981-986` (`_vdp1_mode_variable_sync_render`), `:996-1091` (variable-mode VBlank handlers) | dependency, read for semantics |

**The one contrast that shaped this change.** T2.16 section 10.1 recorded that
SlaveDriver paces with an interrupt-maintained field counter and an adaptive
target, and that **when the engine is already behind, its wait is zero** —
`while (vtimer<smoothVTime);` simply falls through (`SRUINS.C:2252`). It also
fences on draw-end once per frame (`SPR_WaitDrawEnd`, e.g.
`FLASH/INITMAIN.C:471`) rather than on the raster. This port had the identical
`vtimer` primitive in `sourceboot_vblank_out_count` and was spinning on the
raster once per field unconditionally, with no notion of being behind. After
T2.17 the raster spin is gone from the transport and publication path and the
draw-end fence is the synchroniser — which is SlaveDriver's shape, arrived at
independently and for the same reason.

**Nothing was copied.** SlaveDriver is GPL and this is a pattern-only reading.

---

## 11. What remains

- **Confirm the frame is still correct in a desktop observation.** Section 8.
  This is the gating item; everything below it is behind it.
- **Give the overwrite fence a deadline.** `vdp1_sync_wait()` is unbounded. Now
  that it is load-bearing, a bounded fence that fails soft — skip the transfer
  this field and reuse the previous frame — would remove failure mode 2 in
  section 8. T2.8 section 9 item 3 already had this on the list as a correctness
  item; it is no longer optional.
- **T2.16 item 2 — the soft-float purge on the master**, 2.0718 VB/frame on the
  old critical path, master-local, no constraint tax. It is now the largest
  named block that is unambiguously still there.
- **Repair the T2.11 phase rail, or replace it.** Section 5.1: the phase
  decomposition no longer closes at this frame length, and it is the instrument
  the sprint has been steering by.
- **`verify-sourceboot-presentation-boundary`** — a fifth brittle host gate,
  failing since T2.8, verifying nothing.
