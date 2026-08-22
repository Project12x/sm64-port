# W7 — what the master SH-2 now waits on

- Date: 2026-08-21. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `3a3871a1`.
- Task: W6a moved master idle from exactly `0.000000` to 5.22% and shed
  398,163 soft-float cycles/frame while the frame shortened by only 200,589.
  Find the wait site, classify it, and say whether the ~50% absorption is a
  stable property. **Diagnosis only — no product change, no rebuild, no
  emulator GUI.** Every capture reads an already-sealed artifact.
- Targets, both sealed and identity-probed **MATCH** on every capture
  (`observed_sha256 == expected_sha256 == d88b17a9…23b5`):
  - baseline (owner-accepted) `id-e8720d58595d9a62`, ELF SHA-256
    `021f5fee2e0d5a24a1077bc7c41728453e9dc6fc4f4d1cc4c595bdf108b96c50`;
  - candidate `id-586944bb637a920f`, ELF SHA-256
    `03e6f55e2667e7559c8c68e0862b099448a6bd41e9f46fe4f4c9308c93eca8d7`.
    **`provenance.closure_clean: false`** — it was built from the worktree
    before the W6a commit. That is fine for diagnosis (the ELF is sealed,
    hashed and identity-bound) and is stated here rather than hidden.
- New instrument: `tools/saturn/capture_master_wait_cause.py`. It is
  `capture_idle_attribution.py`'s contiguous trace plus, at every entry to the
  spin, a read-out of the whole `sm64_saturn_frame_pipeline_t` from LWRAM and a
  strided VDP1 witness *inside* the wait.
- **The burst sampler is not used anywhere here** (T2.20: ±2.6 pp per symbol,
  and it reported soft-float's trend with the wrong sign).

---

## 0. The answer, in one paragraph

**The wait site is unchanged and singular, the wait is a dependency stall, and
the 50% absorption is not a rate.** There is exactly one reachable call site —
`_main+0xe14`, the `SM64_SATURN_FRAME_WAIT_VBLANK` arm of
`sourceboot_frame_pipeline_dispatch` at `main.c:1929` — the same site T2.16
found, relocated by edits since. Every traced entry classifies as
**`transfer-epoch`**: the render is complete, the command-VRAM DMA has been
**submitted**, and the pipeline's one-follow-up-poll-per-field rule will not
let the master look at the queue again until the raster hands it another field.
**VDP1 does not move during the wait** — `EDSR` and `COPR` are byte-identical
in all 119 strided samples across it — so this is **not** hardware-gated; it is
T2.16's "the reason is a polling rate, not a hardware window", surviving in the
one case T2.17's fix did not cover. **But the quantity is much smaller than
5.22%.** A 4.83-frame contiguous window at the same route position contains the
**same single wait block** and reports **2.1450%**; at route tick 210 the same
build reports **exactly `0.000000`** over 2.63 frames; **at route tick 30 — the
position W6a's own +5.26% cadence pair was measured at — it reports
`0.000000` over 3.84 frames**; under `--sh2-cache` at tick 150 it reports
`0.000000` over 26.55 VBlanks. **And where the master stays saturated, the
arithmetic win converts at full value**: at ticks 30 and 210 the frame
shortened by 200,577 and 262,294 cycles with **zero candidate idle**, so
absorption there is **≤ 0%** — and tick 210, the heaviest scene sampled, shows
the *largest* FPS gain of the three, **+6.30%**. The ~50% figure came from
pairing a tick-30 cadence with a tick-150 idle share measured in a 1.98-frame
window; the remaining fixed-point programme is worth close to its
face value, not half of it. The slave workstream **does not reopen** (§5).

---

## 1. Deliverable (1) — the wait site, attributed

### 1.1 Every call site in the linked ELF, not in source

`--gc-sections` is live in this build, so the ELF is the authority. Scanning
the full disassembly of the sealed candidate for literal-pool references to
`_sm64_saturn_source_runtime_wait_vblank` (`0x0607c17c`) returns **exactly two
call sites**, and one of them is unreachable on this route:

| # | Call site (PC of the `jsr`) | Symbol | Source | Reachable? |
| --- | --- | --- | --- | --- |
| 1 | `0x06030f58` | `_display_and_vsync+0x90` | `src/game/game_init.c:471` | **No.** `display_suppressed` is held true across the whole of `game_loop_one_iteration()` (`main.c:546`, cleared at `:559`), and the call is inside `if (!display_suppressed)`. Zero traced entries in every capture, exactly as T2.16 found. |
| 2 | `0x0607d554` | `_main+0xe10` (return address `_main+0xe14` = `0x0607d558`) | `src/port/saturn/sourceboot/main.c:1929`, the `SM64_SATURN_FRAME_WAIT_VBLANK` arm of `sourceboot_frame_pipeline_dispatch` | **Yes — the only one.** |

Resolved with `sh-elf-addr2line` against the sealed candidate ELF:
`0x0607d558 → sourceboot_frame_pipeline_dispatch, ./src/port/saturn/sourceboot/main.c:1930`
(the instruction after the `jsr`; `0x0607d554 → :1929`, the call itself).
The disassembly brackets it exactly as T2.16 described — a
`sourceboot_boot_trace_write(8, …)` before and a `(9, …)` after.

**So the site did not move in kind.** T2.16 reported `_main+0xea8` →
`main.c:2013` on `id-a61d5203793986e7`; the offset and line number differ only
because `_main` and `main.c` have changed since. T2.17 removed the *reason*
this site was being hit; it did not add a new site. W0 rewrote the transfer
path and added `vdp1_sync_busy()` + `sm64_saturn_frame_pipeline_transfer_deferred()`
(both verified live in the candidate ELF at `0x0607d302` and `0x0607d318`) —
also without adding a wait site. **`vdp1_sync_wait` is absent from both ELFs**,
confirming the retraction in `a583e9a9`.

### 1.2 Where the wait appears, and where it does not

Contiguous traces, caches off unless stated, warm-up by **route tick** so two
builds at different frame rates are compared at the same point on the route
(T2.19d; T2.17 §6.2 is the measurement that repair exists for).

| Route tick | Basis | Build | Traced span | Master idle | Wait entries | Source |
| ---: | --- | --- | ---: | ---: | ---: | --- |
| 150 | caches off | baseline `e8720d58` | 16.27 VB = 1.79 frames | **0.000000** | 0 | W6a, `w6a-idle-before-tick150-e8720d58.json` |
| 150 | caches off | candidate `586944bb` | 17.08 VB = 1.98 frames | **5.2248%** | 5 (one block) | W6a, `w6a-idle-after-tick150-586944bb.json` |
| 150 | caches off | candidate `586944bb` | **41.62 VB = 4.83 frames** | **2.1450%** | **5 (the same one block)** | this task, `w7-wait-cause-tick150-586944bb.json` |
| 150 | **caches ON** | candidate `586944bb` | 26.55 VB | **0.000000** | **0** | this task, `w7-wait-cause-tick150-cacheon-586944bb.json` |
| 210 | caches off | candidate `586944bb` | 24.47 VB = 2.63 frames | **0.000000** | **0** | this task, `w7-wait-cause-tick210-586944bb.json` |
| **30** | caches off | candidate `586944bb` | **32.69 VB = 3.84 frames** | **0.000000** | **0** | this task, `w7-wait-cause-tick30-586944bb.json` |
| 180 | caches off | T2.17 `c0352f29` | 1.83 frames | 0.000000 | 0 | T2.26 |
| 180 | caches on | T2.17 `c0352f29` | 1.83 frames | 0.000000 | 0 | T2.26 |

**Three of the four new contiguous captures on the candidate found the master
saturated — including the one at route tick 30, which is the exact route
position W6a's +5.26% cadence pair was measured at.** Over 3.84 contiguous
frames there, the candidate build does not enter the spin once. The caches-off
/ caches-on pair is reported separately and is never mixed into a caches-off
table; caches-off remains the primary basis.

### 1.3 The primary capture — and the correction it forces

The primary run traces **15,000,000 master instructions = 18,620,915 cycles =
41.6163 VBlanks**, which at the tick-150 local cadence measured in §3.2
(8.6207 VB/frame) is **4.83 contiguous frames** — 2.4× W6a's window at the same
route position, from the same warm-up, on the same deterministic replay.

It found **exactly the same five wait entries and exactly the same 399,412
cycles**. Not one additional excursion in the extra 2.8 frames.

| | W6a window | this window |
| --- | ---: | ---: |
| traced | 17.08 VB = 1.98 frames | **41.62 VB = 4.83 frames** |
| wait entries | 5 | **5** |
| total wait cycles | 399,412 | **399,412** |
| master idle share | **5.2248%** | **2.1450%** |
| wait per frame | 201,536 cycles (0.450 VB) | **82,738 cycles (0.185 VB)** |

The five entries are one logical wait, split by the VBlank-IN ISR exactly as
T2.16 described: `0.7503 VB` + three sub-30-cycle fragments + `0.1422 VB`
(the blanking period), 0.893 VB in total, ending at the VBlank-OUT that
increments `sourceboot_vblank_out_count` from 2812 to 2813.

**So the master's wait is a single event, not a per-frame cost, and the
5.2248% figure is a short-window artifact.** It is the *same* block seen
through a 1.98-frame window instead of a 4.83-frame one. Combined with the
`0.000000` results at ticks 30 and 210 and under `--sh2-cache`, the honest
statement is: **the master is still essentially saturated. Across 11.3
contiguous frames of caches-off tracing at three route positions it loses one
field, once.** What makes the master reach the transfer poll with time left in
the field at tick 150 and not at ticks 30 or 210 is **not established here** —
it is not simply scene weight, since tick 30 is the *lightest* of the three
(8.5172 VB/frame against 8.6207 and 9.3103) and shows no wait at all. Treat the
occurrence as sporadic rather than as a function of load.

---

## 2. Deliverable (2) — the classification

**Verdict: a dependency stall — specifically a self-imposed polling rate
limit on an already-submitted SCU DMA. It is not hardware-gated.**

`sm64_saturn_frame_pipeline_step()` has four distinct paths that return
`WAIT_VBLANK`, and each leaves a different `sm64_saturn_frame_pipeline_t`
behind. The instrument reads the whole 76-byte struct out of LWRAM
(`_sourceboot_frame_pipeline`, `0x002cda28`, size `0x4c` — the size nm reports
is the check that the decode layout is right) at every entry and re-derives the
branch from the same predicates the target evaluated, in the same order.

**All 5 entries, 100% of the idle cycles, classify as `transfer-epoch`.** Zero
entries are `publish-pending`, `service-epoch` or `frame-complete`.

The struct at the entry to the long block:

| field | value | reading |
| --- | ---: | --- |
| `render_generation` / `render_completed_generation` | 149 / 149 | the render is **finished** |
| `transfer_generation` / `transfer_completed_generation` | 149 / **148** | generation 149's command-VRAM transfer is **outstanding** |
| `transfer_started` | **true** | the submitting poll ran; this is **not** a W0 VDP1-busy deferral |
| `sourceboot_vdp1_transfer_pending` | `0x0026e280` (non-NULL) | a bank is in flight |
| `transfer_submit_vblank` | **2811** | the DMA was submitted **in the previous field** |
| `transfer_poll_vblank` / `last_vblank_count` | **2812 / 2812** | this field's one permitted follow-up poll has already run and did not retire the queue |
| `presentation_pending` | false | no presentation opportunity to spend on `REUSE_PREVIOUS_FRAME` |
| `queued_snapshot_valid` / `queued_snapshot_generation` | **true / 150** | **the next generation is already queued behind this transfer** |
| `available_sim_credit` / `sim_ticks_this_presentation` | 0 / 2 | the frame's simulation budget is spent |

**The hardware did not move during the wait.** 100 strided VDP1/VDP2 samples
across the 0.7503 VB block, plus 19 across the 0.1422 VB tail:

```
EDSR  0x0001 at entry, 0x0001 at exit, 0x0001 in every one of the 119 samples
LOPR  2092 -> 2092 -> 2092    COPR  2092 -> 2092 -> 2092
```

`EDSR.CEF` is clear at entry and still clear at exit, and `COPR` — the command
index VDP1 is currently plotting — **does not advance by a single command over
the entire 0.89 VB**. Whatever VDP1 is doing, the master's wait neither begins
nor ends because of it. The wait ends when `sourceboot_vblank_out_count` goes
2812 → 2813, i.e. **on the raster, because a new field is what the scheduler
requires**, not because a device became ready.

**The mechanism, in source.** `saturn_dma_queue_poll()`
(`src/port/saturn/gpl/slavedriver_dma_queue.c:282-310`) retires **at most one
descriptor per call** — it records the completion, advances `_tail`, clears
`_active` and returns — and `saturn_dma_queue_kick()` then starts the next.
`sm64_saturn_vdp1_frame_bank_poll_transfers()`
(`saturn_vdp1_frame_bank.c:326-353`) requires **both** the command-list ticket
and the Gouraud ticket to be retired before it reports the transfer complete.
So a two-descriptor queue needs **two poll calls**. T2.17 freed follow-up polls
**only inside the submit field** (`polling_in_submit_field` requires
`transfer_submit_vblank == last_vblank_count`); once that field ends the
conservative one-poll-per-field schedule resumes. Here the submit was in field
2811 and the queue had not retired by the end of it, so the tail of the
transfer is now costing **one whole field per remaining descriptor**.

This is precisely T2.16 §4 item 3 — *"a queue of two descriptors therefore
needs two field-separated polls to retire… the spill across successive VBlank
windows is real; the reason is a polling rate, not a hardware window"* —
surviving T2.17's partial fix in the case the fix did not cover.

**Why "not hardware-gated", stated against the three candidates:**

1. **Not a blanking window.** There is no blanking gate anywhere on the
   transfer path (T2.16 §4, re-checked here: `saturn_dma_queue_kick`,
   `saturn_dma_queue_poll` and `sm64_saturn_vdp1_frame_bank_poll_transfers`
   read `scu_dma_level_busy(0)` / the CPU-DMAC status word and never TVSTAT).
2. **Not the VDP1 overwrite fence.** W0's `vdp1_sync_busy()` deferral is a
   non-blocking check, and it **did not fire**: `transfer_started` is true and
   `sourceboot_vdp1_transfer_pending` is non-NULL, so the submit succeeded.
   `vdp1_sync_wait` is absent from the ELF entirely.
3. **Not the DMA itself.** The descriptor had already had a full field
   (≥ 16.7 ms) to run. An SCU DMA of a ~523-command list plus a Gouraud bank
   is tens of microseconds of work. **Caveat, stated as such: this instrument
   does not read `scu_dma_level_busy(0)` directly, so "the DMA had finished and
   only the queue's bookkeeping had not" is an inference from the descriptor
   count and the elapsed time, not a direct observation.** It is the only
   reading consistent with the frozen VDP1 registers and the one-descriptor-
   per-poll code, but a follow-up that peeks the SCU DMA status register inside
   the wait would settle it outright and is cheap.

**What this is worth, honestly: very little.** One field lost in 4.83 frames at
tick 150, and zero at ticks 30 and 210 and under `--sh2-cache`. Releasing it entirely
buys at most **2.15% of the master's cycles at one route position and 0% at
another**. It is a real defect with a clear fix, and it is **not** the reason
W6a's win was smaller than its arithmetic saving — §3 shows that reason is
field quantisation, which is a different thing.

---

## 3. Deliverable (3) — is the 50% absorption stable? **No.**

### 3.1 The measurement W6a's figure rests on, restated honestly

W6a's absorption arithmetic combined a **tick-30** cadence (8.9655 → 8.5172
VB/frame) with a **tick-150** idle share (5.2248%). Those are different route
positions. The agreement it reported (197,574 residual against 199,115 implied
by the idle share, 0.8%) is therefore looser than it looks: it is a cadence
from one part of the route reconciled against an idle share from another.
Additionally, the 5.2248% comes from a window containing **one** contiguous
wait block; a window one block longer or shorter would have reported roughly
double or zero. This is not a criticism of the conclusion W6a drew — it drew
the right conclusion, which is that the question needed asking — but the
number itself carries a much wider band than three decimal places suggest.

### 3.2 The same comparison, on three route positions, each internally consistent

`summarize_cadence` only, 30 presentation events / 29 intervals per run,
caches off, on-target identity **MATCH**, `presentation_generation_delta == 1`
on every interval, `status: complete`. The two arms are warmed to the **same**
route tick and observe the **same** route span (ticks *N*→*N*+29).

| Route span | | baseline `e8720d58` | candidate `586944bb` | Δ FPS |
| --- | --- | ---: | ---: | ---: |
| 30 → 59 | FPS mean | 6.6923 | 7.0445 | **+5.26%** |
| | VB/frame | 8.9655 | 8.5172 | −0.4483 |
| | field distribution | 8:2 9:26 10:1 | 8:14 9:15 | |
| 150 → 179 | FPS mean | 6.5909 | 6.9600 | **+5.60%** |
| | VB/frame | 9.1034 | 8.6207 | −0.4827 |
| | field distribution | 9:26 10:3 | 8:11 9:18 | |
| 210 → 239 | FPS mean | 6.0627 | 6.4444 | **+6.30%** |
| | VB/frame | 9.8966 | 9.3103 | −0.5863 |
| | field distribution | 9:6 10:20 11:3 | 8:1 9:19 10:8 11:1 | |

(The tick-30 row is W6a's own pair, unchanged. The tick-150 and tick-210 rows
are new: `w7-throughput-tick150-*.json`, `w7-throughput-tick210-*.json`.)

**The scene at tick 210 is heavier** — both builds are ~0.8–0.9 VB/frame slower
there than at tick 150 — and that is exactly where the candidate's gain is
*largest*, not smallest.

### 3.3 The absorption, computed per route position

One field is `26,846,590.91 / 60 = 447,443.2` master cycles. Master work per
frame = frame × (1 − idle share).

| Route tick | Δframe (cycles/frame) | candidate master idle | Δ master work | **absorption** |
| ---: | ---: | ---: | ---: | ---: |
| **30** | 200,577 | **0.000000, over 3.84 contiguous frames** | ≤ 200,577 | **≤ 0%** |
| 150 | 216,007 | W6a's 1.98-frame window: 5.2248% = 201,536/frame | 417,543 | 48.3% *(superseded)* |
| 150 | 216,007 | **this task's 4.83-frame window: 2.1450% = 82,738/frame** | **298,745** | **27.7%** |
| 210 | **262,294** | **0.000000, over 2.63 contiguous frames** | ≤ 262,294 | **≤ 0%** |

Two things to read off this.

**The tick-150 row is quoted twice deliberately**: the only thing that changed
between the two is the length of the window the idle was measured in. A
quantity that moves from 48.3% to 27.7% when you look at it 2.4× longer is not
a rate.

**And tick 30 is where W6a's headline was measured.** W6a's residual argument
paired a tick-30 cadence with a tick-150 idle share and found them to agree to
0.8%. Measured at tick 30 itself, over 3.84 contiguous frames, **the
candidate's master idle there is exactly `0.000000`** — there is no waiting at
that route position at all, and the agreement was a coincidence of two
route-mixed numbers. **Absorption is ≤ 0% at two of the three sampled route
positions.**

The tick-210 row needs no assumption about the size of the soft-float saving.
If the candidate's idle is zero then its work *equals* its frame; the
baseline's idle can only be ≥ 0, so its work is ≤ its frame; therefore
Δwork = Δframe − idle_baseline ≤ Δframe, and absorption
= (Δwork − Δframe)/Δwork = −idle_baseline/Δwork **≤ 0**. Every prior contiguous
census of a baseline-class build — T2.17, T2.26 caches-off and caches-on, and
W6a's own tick-150 baseline — measured baseline idle at exactly `0.000000`, so
the honest reading is **absorption ≈ 0 at ticks 30 and 210**.

**Absorption is therefore not a rate. It is 48% at one route position and ≤ 0%
at another, on the same pair of builds.**

### 3.4 What a further arithmetic saving of N cycles is actually worth

The master is the critical path with no residue — frame = master work + master
idle, exactly, as T2.16 §2.1 established and every capture here reproduces.
**So the absorbed cycles and the §2 wait are the same cycles.** The 82,738 that
did not reach the frame at tick 150 *are* the transfer-epoch stall, seen from
the other side, and the ≤ 0% at ticks 30 and 210 is simply the absence of any
stall there. **The two deliverables therefore have one answer: absorption is
not a law, it is the measured frequency of one specific scheduler stall.**

- **Where the master does not hit the stall, a saving of N cycles returns the
  full N.** Ticks 30 and 210; every build before W6a; the caches-on basis.
- **Where it does, the saving is reduced by whatever that stall costs** — here
  one field, once, in 4.83 frames, which is the 27.7% at tick 150.

The frame is also genuinely quantised (a presentation edge lands on an observed
VBlank; measured field deltas are 8, 9, 10 or 11 and never anything else across
174 intervals in six runs), so a *residual* rounding loss must exist in
principle. **It does not show up in these measurements**: at the two positions
where the stall is absent, master idle is not "small", it is exactly
`0.000000` across 6.5 contiguous frames. Whatever slack the quantisation
creates, the pipeline currently absorbs it into admissible work rather than
into the spin.

**Prediction, stated with its confidence.** For the next arithmetic win — W2 or
W6b — **expect close to full face value.** Two of the three sampled route
positions convert at ≥ 100% (absorption ≤ 0%), the third at ~72%, and the
route positions that convert best are the *heavy* ones, which is also where the
1% low lives. A reasonable planning figure is **80–100% of face value**, not
50%. **Confidence: moderate-to-good.** It rests on three cadence pairs and five
contiguous censuses on one build pair and one route; the three zero-idle
results (ticks 30 and 210 caches-off, tick 150 caches-on) are direct
observations over 3.84, 2.63 and 26.55 VB rather than differences of estimates. **The one number still resting on a short
window is the baseline's `0.000000` at tick 150, taken over 1.79 frames** — the
candidate's own idle fell by more than half when its window was lengthened, so
the baseline's zero should be read as "no wait in 1.79 frames", not as proof of
saturation. If the baseline also loses a field every four or five frames there,
the tick-150 absorption is smaller still. What would raise confidence cheaply:
re-run the baseline census at tick 150 with `--steps 15000000`, and repeat the
tick-150/tick-210 pair on a third build with a known arithmetic delta (T2.17's
`c0352f29` is already sealed and is such a build).

**This retires the phase plan's stated consequence 1.** "Roughly half of any
further arithmetic win will be absorbed the same way" is not supported: it is
partly true at tick 150 and false at ticks 30 and 210, and the gain W6a
actually delivered rises with scene weight (+5.26% / +5.60% / +6.30%).

---

## 4. A correction that matters for any future per-frame symbol table

Normalising a contiguous window's symbol residency to "cycles per frame" by
scaling on traced VBlanks is only valid for symbols whose cost is spread
through the frame. For once-per-generation symbols it is not, and the two W6a
censuses show the failure directly: `_sm64_saturn_ztreme_frustum_aabb` records
251,338 cycles in the baseline window and 502,739 in the candidate window —
**a ratio of 2.00025**, i.e. one cull pass captured against two, not a doubling
of cost. Any "per frame" figure derived for that symbol from those windows is
wrong by 2×, and a naive per-frame diff of the two censuses appears to show
+113,082 cycles/frame of new work that does not exist.

**Consequence: window length must be an integer number of frames, or bursty
symbols must be normalised by pass count rather than by traced VBlanks.**
W6a's aggregate soft-float totals are not affected in direction — those
routines are called throughout the frame, and its own sanity check (unreplaced
double-precision routines reporting identical absolute counts in both windows)
holds — but the individual once-per-frame rows in any such table should not be
differenced.

---

## 5. Does the slave workstream reopen? **No.**

The phase plan's amended non-goal says: *"Do not reopen this on the idle number
alone — first establish what the master waits on, because if it is a
hardware-gated wait rather than a dependency stall, offload still converts
nothing."*

**The wait is a dependency stall, so that clause is satisfied on its face — and
the workstream still does not reopen, for three reasons that are stronger than
the one T2.25 gave.**

1. **There is almost nothing to overlap into.** The stall is **82,738 cycles
   per frame at tick 150 and exactly 0 at ticks 30 and 210** — one field in
   4.83 frames at one of three sampled route positions, not the 0.45 VB/frame
   the 5.22% figure implied. T2.25's largest currently
   eligible offload item is ~0.367 VB/frame of terrain depth-bin merge. The
   available stall (0.185 VB/frame, and only on part of the route) is **half
   that**, and it is not even contemporaneous with the work that would move.
2. **The stall is not waiting for compute.** It is waiting for a *poll* of an
   already-submitted DMA. Moving master work to the slave does not make that
   poll arrive sooner — the poll is gated on an observed VBlank, not on the
   master being free. **Offload converts this stall at exactly the same rate it
   converted at 0.000000 idle: zero.**
3. **Offload makes it worse, not better, wherever the master has slack.**
   Every cycle moved off the master at tick 150 moves the master further below
   the field count its pipeline needs, which is the same mechanism that turned
   half of W6a's saving into rounding loss (§3.4). At tick 210, where offload
   would convert at full value, the master idle is `0.000000` and T2.25's
   original argument applies unchanged.

The rest of T2.25's position is untouched: the two blocking dependencies
(admission BFS over shared visited/queued/seen state; the sequential VDP1
command arena with its shared Gouraud bank) are the same two SlaveDriver leaves
serial, and T2.26's 99.24%-survival measurement stands. **The phase plan's
"PARTIALLY REOPENED by W6a" should be closed again**, and the reason recorded
is that the premise it reopened on — 5.22% master idle — does not survive a
longer window or a second route position.

---

## 6. Ranked list — what would release the wait

Ranked by value per unit of risk. **The top item is deliberately "do not spend
on this".**

| # | Release | What it buys | Risk / cost | Verdict |
| ---: | --- | --- | --- | --- |
| **1** | **Nothing — continue the arithmetic programme (W2, W6b) at full expected value.** | The measured cost of this wait is 82,738 cycles/frame at tick 150 and 0 at ticks 30 and 210. The measured cost of *not* correcting the discount is the whole remaining fixed-point programme being ranked at half its worth. | zero | **Do this.** The most valuable output of W7 is that consequence 1 of the phase plan's W6a note is withdrawn. |
| **2** | **Fix the measurement protocol before quoting another per-frame idle or per-frame symbol figure.** Size contiguous windows at ≥4 frames of the *local* cadence, and take the local cadence at the same route tick as the census (§3.1, §4). | Stops the next task repeating this: 5.22% → 2.15% purely from window length, and a phantom +113,082 cycles/frame from normalising a once-per-generation symbol on traced VBlanks. | one afternoon, tools only | **Do this next.** Highest confidence, no product risk. |
| **3** | **Let a follow-up poll retire the *rest* of a multi-descriptor queue outside the submit field.** T2.17 freed follow-up polls only while `transfer_submit_vblank == last_vblank_count`; the observed stall is exactly the case where the queue outlives that field. A follow-up poll writes nothing (T2.17 §2: it bottoms out in `scu_dma_level_busy(0)` / the CPU-DMAC status word), so the ownership argument that licensed T2.17's change appears to cover this case verbatim — **but G1 (one publication per observed field) rides on `transfer_poll_vblank`, and re-deriving it for the post-submit-field case is the actual work.** | Up to the whole 82,738 cycles/frame at tick 150 (~+2.1% FPS there), 0 at ticks 30 and 210 — **and, because these are the same cycles the absorption is made of (§3.4), it is the one lever that would make future arithmetic wins convert at full value wherever the stall does occur.** | Small, localised, and `verify-frame-pipeline` already carries six mutations including `..._FREE_POLL_EVERY_FIELD`, which exists precisely to catch over-widening this. | **The only structural release worth scoping.** Its direct FPS value is small; its value as a discount-remover on W2/W6b is larger. |
| **4** | **Collapse the two-descriptor queue to one retirement.** The command list and the Gouraud bank are submitted as two serial descriptors and `saturn_dma_queue_poll()` retires one per call. Retiring all *completed* descriptors in a single poll (loop instead of early return) removes the second field without touching the epoch rule at all. | Same as #3, by a different and possibly safer route. | Changes GPL-derived `slavedriver_dma_queue.c` semantics; needs its own mutation coverage. | **Evaluate against #3; pick one, not both.** |
| **5** | **Peek `scu_dma_level_busy(0)` inside the wait** to settle whether the DMA was genuinely still running or only un-retired. | Converts §2's inference into an observation, and decides between #3 and #4. | ~20 minutes of instrument work, one capture | **Do it as part of #3/#4, not before.** |
| **6** | **VDP1 fill-rate / command-count work (W3).** | Nothing *for this wait* — VDP1's `COPR` did not advance during it. It remains the right lever for the frame as a whole once the master drops below the plot. | — | **Not a release for this wait.** Do not justify W3 on the master's idle. |
| **7** | **Slave offload.** | Zero, and negative where the master already has slack. | — | **Closed.** §5. |

---

## 7. Standing hazards observed

- **Ymir models no inter-SH-2 bus arbitration** (`StepMasterSH2Impl` advances
  the slave by the master's cycles with no arbitration). Any cross-CPU figure
  quoted anywhere in this report is an **upper bound**. Cache coherency *is*
  modelled under `--sh2-cache` but miss cost is under-priced.
- **Caches-on is a different basis, not a correction.** The one caches-on
  capture here is reported on its own and never mixed into a caches-off table.
- **The T2.11 concurrency rail needs its allowance on 28 of 29 intervals**, so
  the phase decomposition does not close and is not quoted anywhere here.
  `summarize_cadence` reads presentation-edge deltas independently and is the
  only cadence source used.
- **The candidate's `provenance.closure_clean` is `false`.** Stated in the
  header; it does not affect a measurement taken against the sealed ELF, but a
  promotion of this artifact should use the post-commit rebuild.
- **Known pre-existing gate failures were not run and not chased**:
  `verify-render-clusters`, `verify-sourceboot-presentation-boundary`,
  `verify-vdp1-transfer-pipeline`, `verify-render-snapshot-bank`,
  `verify-graph-q16-contract`, `test_gen_actor_identity_registry.py`,
  `verify-tools`, `verify-bob-bsp-header`. This task ran no gates: it changed
  no product code.

---

## 8. Evidence

| File | What |
| --- | --- |
| `w7-wait-cause-tick150-586944bb.json` | primary: contiguous trace, candidate, tick 150, caches off, with pipeline-state decode at every wait entry |
| `w7-wait-cause-tick210-586944bb.json` | candidate, tick 210, caches off — **0 wait entries in 24.47 VB** |
| `w7-wait-cause-tick150-cacheon-586944bb.json` | candidate, tick 150, **caches on** — **0 wait entries in 26.55 VB** |
| `w7-wait-cause-tick30-586944bb.json` | candidate, tick 30, caches off — the route position W6a's cadence pair was measured at |
| `w7-throughput-tick150-586944bb.json`, `w7-throughput-tick150-e8720d58.json` | cadence pair at route span 150→179 |
| `w7-throughput-tick210-586944bb.json`, `w7-throughput-tick210-e8720d58.json` | cadence pair at route span 210→239 |
| `w6a-idle-before-tick150-e8720d58.json`, `w6a-idle-after-tick150-586944bb.json` | W6a's censuses, re-read here |
| `w6a-throughput-after-30events-586944bb.json`, `w0-vdp1-overwrite-fence-throughput.json` | the tick-30 cadence pair |

Reproduce (PowerShell for Make; `$G` **must** be absolute — `YmirClient` sets
`cwd=game.parent` and passes the same string to Ymir, so a relative CUE is
resolved twice and the boot fails with `Ymir closed its protocol stream`):

```sh
python tools/saturn/capture_master_wait_cause.py --ymir "$YMIR" --ipl "$IPL" \
  --game "$G/sm64-saturn-sourceboot-e2.cue" --elf "$G/obj/sm64-saturn-sourceboot-e2.elf" \
  --nm "$NM" --warmup-ticks <30|150|210> --steps 15000000 --wait-io-stride 400 \
  --vblanks-per-frame 8.5172 --timeout 5400 --output <out>.json

python tools/saturn/capture_sourceboot_throughput.py --ymir "$YMIR" --ipl "$IPL" \
  --game "$G/...cue" --elf "$G/obj/...elf" --release-manifest "$G/saturn-release-manifest-v1.json" \
  --warmup-ticks <150|210> --startup-vblanks 4096 --max-vblanks 600 \
  --presentation-events 30 --timeout 1800 --output <out>.json
```
