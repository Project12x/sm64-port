# Sprint 2 Task T2.16 — the idle, attributed

- Date: 2026-08-16. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `475bb710`.
- Task: attribute every idle SH-2 cycle to a wait site and a cause, and rank
  causes by *releasable* cycles. Diagnosis only — no scheduler change was
  landed, and section 9 says why that is the right call today.
- Target: the shipped product build `id-a61d5203793986e7`
  (`releases/2026-08-16_t2_13-product/id-a61d5203793986e7/...e2.elf`, SHA-256
  `bc49eafa8bc9695ec708a7ffa24884be7084be10b0ad7a1bd1ca87bdbe6ccbad`), the same
  build T2.14 and T2.15 measured. On-target identity **MATCH** on both captures
  (`observed_sha256 == expected_sha256`, `d88b17a9…23b5`).
- Cadence basis unchanged: **5.3538 FPS mean / 11.2069 VB per frame**,
  `summarize_cadence`, T2.13. **No cadence figure is claimed here** — this task
  ran no build and no throughput capture, so every VB/frame number below is a
  *share of that measured frame*, not an independent cadence measurement.
- Evidence: `sprint2-t2_16-idle-attribution-fullframe.json` (authoritative),
  `sprint2-t2_16-idle-attribution.json` (the superseded five-window run, kept
  because section 8.1 is about why it was wrong).
- New instrument: `tools/saturn/capture_idle_attribution.py`, commit
  `e8313e2c`.

---

## Headline — four findings, in the order they change decisions

**1. The 73.33% idle figure is an artifact of how it was sampled. The real
combined idle is 49.34%, and the master's share of it is 19.54%, not 60.1%.**
A contiguous trace of **19.6405 VBlanks (1.7525 frames)** with no gap and no
phase selection measures master idle at **19.544% = 2.1903 VB/frame** and slave
idle at **79.145% = 8.8697 VB/frame**. `capture_softfloat_profile.py` bursts
always begin where `Saturn::RunFrameImpl()` stops — the instant the vertical
phase *enters* `BlankingAndSync` — which is exactly where the master's VBlank
spin lives, and spin instructions cost 8.33 cycles each against 1.2 for cached
work, so a cycle-weighted phase-locked sampler amplifies the spin roughly
sevenfold. Section 8.1 shows the mechanism reproduces the observed 3.1x
over-statement to the right order. **The slave number survives (79.1% measured
against 86.6% sampled); the master number does not.**

**2. The master's entire idle is one wait site, one caller, and one cause: the
frame pipeline's per-field epoch rule.** All 24 traced entries into
`_sm64_saturn_source_runtime_wait_vblank` return to `_main+0xea8` =
`0x0607d508`, disassembled as the return address of the `jsr` at `0x0607d504`,
bracketed by `sourceboot_boot_trace_write(8, …)` and `(9, …)` — i.e.
`src/port/saturn/sourceboot/main.c:2013`, the `SM64_SATURN_FRAME_WAIT_VBLANK`
arm of `sourceboot_frame_pipeline_dispatch`. **Zero** entries come from
`display_and_vsync()` (`src/game/game_init.c:472`), which is unreachable on
this route because `display_suppressed` is held true across the whole game loop
(`main.c:547`). **The brief's leading hypothesis — VRAM/CRAM transfers that can
only run during blanking — is refuted in section 4: the transfer path contains
no blanking gate anywhere, and the wait contains no transfer.**

**3. The master burns 2.19 VB/frame to perform 0.14 VB/frame of work.** The
ordered run log resolves the spin into a single contiguous block, in the same
place in two consecutive frames, of ~3 costly `WAIT_VBLANK` calls separated by
17,270 and 46,100 cycles of transport/publication work. **A work-to-wait ratio
of about 1:14.** No data crosses between CPUs in that block, so it is the one
large idle block that the two hard constraints below **do not tax at all**.

**4. VDP1 is not starved, and it is the next wall.** Fresh witness on this
build: `EDSR.CEF` set in **12.0%** of 300 strided samples ⇒ **VDP1 is plotting
88% of the frame ≈ 9.86 VB**, with `COPR` sweeping command index 6→547 (the
full list). Master *work* is 9.0166 VB/frame. So releasing the master's whole
2.19 VB of stall moves the frame from 11.2069 to roughly **max(9.02, 9.86) ≈
9.86 VB ≈ 6.09 FPS**, and VDP1 becomes binding. **T2.8's demotion of the
fill-rate levers was correct for T2.8's build and expires the moment item 1 of
section 9 lands.** The "552 commands against 1,664 capacity" statistic that
motivated calling VDP1 idle is a *static array occupancy*
(`main.c:767,806` — `vdp1_cmdt_t sourceboot_vdp1_cmdts[2][1664]`) and carries
no information at all about whether the chip is busy.

**Is most of the idle structural? No — but the largest block is the one the
constraints bite hardest on, and the smallest block is free.** Section 6 gives
the split with numbers.

---

## 1. Method — why a new instrument, and what it removes

### 1.1 The three defects in burst sampling, and the fix for each

`capture_softfloat_profile.py` (T2.13) answers "what is executing at this PC".
It cannot answer "what is this processor waiting on", for three reasons.

| Defect | Consequence | Fix in `capture_idle_attribution.py` |
| --- | --- | --- |
| The two SH-2s are sampled in **alternating bursts** | the pair (master PC, slave PC) is never observed at one emulated instant, so "is the slave idle because the master computes, or because the master is idle too?" is unanswerable | `Saturn::StepMasterSH2()` advances the slave by exactly the master's cycles (`ymir-core/src/ymir/sys/saturn.cpp:594-627`), so pausing after a master step and reading both register files is one coherent snapshot |
| Attribution is **by PC only** | the wait site's caller is invisible; T2.14 §7.1 measured call-edge sampling at 9 entries in 480,000 samples — far too sparse | `_sm64_saturn_source_runtime_wait_vblank` is a leaf (both `vdp2_tvmd_vblank_in_wait` and `..._out_wait` are `__always_inline`, `third_party/libyaul/.../vdp2/tvmd.h:84-97`), so **PR holds the caller for the whole spin**. 100% hit rate, 24 of 24 entries attributed |
| `exec.run_for` is **raster-phase-locked** | `Saturn::RunFrameImpl()` runs until the vertical phase enters `BlankingAndSync` (`saturn.cpp:496-510`), so every burst starts at the beginning of VBlank — precisely where the master's spin lives | trace **contiguously**: no gap, no phase selection, residency exact rather than estimated |

Nothing was rebuilt and nothing was instrumented. Both captures read the
shipped product ELF through `exec.stepi` / `regs.read` / `mem.peek`, exactly as
T2.13 does.

### 1.2 The authoritative capture

| Parameter | Value |
| --- | --- |
| Target | `releases/2026-08-16_t2_13-product/id-a61d5203793986e7/...e2.elf` |
| Warm-up | 1,800 VBlanks after the ELF identity match (T2.13's value; the identity matches while the CD loader is still running) |
| Trace | **one contiguous window, 6,000,000 master instructions** |
| Emulated span | **8,787,989 cycles = 19.6405 VBlanks = 1.7525 frames** |
| Slave stride | every 16 master instructions |
| VDP witness stride | every 20,000 master instructions (300 samples) |
| Wall time | 607.5 s |
| Evidence | `sprint2-t2_16-idle-attribution-fullframe.json` |

**Why one window and not several.** Section 8.1: the five-window run covered
only 70.4% of the frame and four of its five windows contained *zero* master
idle, because the spin is one contiguous block rather than spread through the
frame. A single window longer than the frame period removes the question.

### 1.3 The sampler's own bias, calibrated

The VDP witness is strided by master *instructions*, which are cheaper during
work than during the spin, so it could over-sample the work phase. It does not,
measurably: **TVSTAT's VBlank bit is set in 15.0% of samples against ~14.8%
expected** from 39 non-active lines of 263 in NTSC 224-line mode. The strided
sampler is essentially unbiased in time, which is what licenses the `EDSR.CEF`
figure in section 5.

`mem.peek` is Ymir's side-effect-free debug read, not a bus read: `EDSR`'s
`BEF` bit reads set on all 300 samples, where a real hardware read would have
cleared it. The witness therefore does not perturb libyaul's sync machine.

---

## 2. Deliverable (1): the wait-site attribution table

Every idle cycle, attributed to a site and a cause. Shares are **per CPU**;
each SH-2 executes the whole frame at the same clock, so machine capacity is
50/50 between them regardless of sample counts (the two `per_target` cycle
totals in T2.14's profile differ by 0.4%, confirming this).

| Wait site | CPU | Share of that CPU | VB/frame | What it is waiting for | Class |
| --- | --- | ---: | ---: | --- | --- |
| `_sm64_saturn_source_runtime_wait_vblank` ← `main.c:2013` | master | **19.544%** | **2.1903** | the raster. `while (vdp2_tvmd_vblank_out()); for (; vdp2_tvmd_vblank_in(););` — the frame scheduler had no action it was permitted to take this field | **releasable** (§6.1) |
| `_sm64_saturn_source_runtime_wait_vblank` ← `game_init.c:472` | master | 0% | 0.0000 | — | unreachable: `display_suppressed` (`main.c:547`) |
| `___slave_polling_entry` → `cpu_dual_notification_wait()` | slave | **79.145%** | **8.8697** | one `cpu_dual_slave_notify()` per frame; the 4-job render graph exhausts after 2.34 VB and there is nothing else it may claim | **releasable, but constraint-taxed** (§6.2) |
| `_sm64_saturn_dual_worker_run` fork-join barrier | master | 0% | 0.0000 | **not in the shipped ELF at all** (§3.3) | does not exist |
| `vdp1_sync_wait()` overwrite fence | master | 0% | 0.0000 | T2.8: 0 waits in 1,349 fence events, mean 0.331 FRT ticks | not a wait today (§5.3) |

**No other wait site exists.** Of the 688 master symbols with non-zero
residency, exactly one matches `wait|sync|spin|poll|drain|busy|dma|retire` with
non-trivial cost, and it is the row above.

### 2.1 The joint contingency table — the cell that is not there

Cycle-weighted, over the whole traced span. This is the table burst sampling
could not produce.

| Master | Slave | Share | VB/frame |
| --- | --- | ---: | ---: |
| work | **idle** | **59.60%** | **6.6794** |
| work | work | 20.86% | 2.3372 |
| **idle** | **idle** | **19.54%** | **2.1903** |
| **idle** | **work** | **0.00%** | **0.0000** |

The accounting closes exactly: `6.6794 + 2.3372 = 9.0166` master work;
`6.6794 + 2.1903 = 8.8697` slave idle; every column sums to 11.2069.

**The empty cell is the finding.** The master never spins while the slave is
doing useful work. The master's VBlank stall is not the price of dual-CPU
overlap; it buys the slave nothing. And the frame equals **master work + master
idle exactly** — 9.0166 + 2.1903 = 11.2069 — so **the master is the critical
path with no residue**, and every VB below is a VB of frame time.

### 2.2 Per-CPU class breakdown

| Class | Master share | Master VB/frame | Slave share | Slave VB/frame |
| --- | ---: | ---: | ---: | ---: |
| idle | 19.544% | 2.1903 | **79.145%** | **8.8697** |
| application | 58.139% | 6.5158 | 19.952% | 2.2360 |
| **all float, summed** | **18.485%** | **2.0718** | **0.000%** | **0.0000** |
| shift | 2.497% | 0.2798 | 0.903% | 0.1012 |
| divide | 1.334% | 0.1495 | 0.000% | 0.0000 |

**Soft-float is 18.485% of the critical path — 2.0718 VB/frame, marginally
larger than the entire scheduler stall.** T2.13 reported "all float, summed:
7.50% of sampled cycles" on a *pooled* denominator half of which was the slave
spinning. On the processor that actually determines the frame, float is 2.5x
that. This is a correction to T2.13's own conclusion, made with T2.13's data
re-normalised, and it re-promotes the float purge (§9 item 2).

**The slave executes zero floating point.** Its half of the pipeline is
entirely Q16 integer, which is worth recording as the existing proof that the
port's Q16 conversion works end to end.

---

## 3. Deliverable (2): the master-VBlank-wait cause

This is the number the brief called "NOT understood, and therefore the most
valuable here". It is now understood, and the leading hypothesis was wrong.

### 3.1 The wait itself contains no transfer

```c
void sm64_saturn_source_runtime_wait_vblank(void) {
    vdp2_tvmd_vblank_in_wait();
    vdp2_tvmd_vblank_out_wait();
}
```
(`src/port/saturn/runtime/saturn_source_runtime.c:145-148`.) Both callees are
`__always_inline` spin loops on TVSTAT
(`third_party/libyaul/libyaul/scu/bus/b/vdp/vdp2/tvmd.h:84-97`). There is no
DMA, no VRAM write, no CRAM write, and no fence in it. It is a pure raster
spin, and it costs **8.33 cycles per instruction** — an in-tree measurement of
what an off-chip register poll costs on this machine.

### 3.2 The cause is a scheduler policy: one action per field

`sm64_saturn_frame_pipeline_step()`
(`src/port/saturn/runtime/saturn_frame_pipeline.c:116-221`) is a state machine
that returns **one action per call** and rate-limits the two render actions to
**at most one each per observed VBlank**:

- `SERVICE_RENDER_JOBS` stamps `render_service_vblank = last_vblank_count`
  (`:180`); on the next call `serviced_this_vblank` (`:166-168`) forces
  `SM64_SATURN_FRAME_WAIT_VBLANK` (`:177`).
- `POLL_TRANSFERS` stamps `transfer_poll_vblank` (`:206`);
  `polled_this_vblank` (`:191-193`) forces `WAIT_VBLANK` the same way (`:202`).
- `PUBLISH_FRAME` stamps **both** (`:147`, `:149`) — its own comment says so:
  *"Publication consumes this field's remaining service opportunities. A
  promoted bank cannot start SERVICE/POLL until another VBlank is actually
  observed."* (`:143-146`).
- Every fall-through returns `WAIT_VBLANK` (`:121`, `:126`, `:220`).

The dispatcher then executes the raster spin for that action
(`main.c:2010-2015`). So when the only remaining step is a short one, the
master is told to do nothing for the rest of the field, and it does that by
spinning on the raster.

### 3.3 What the timeline actually looks like

The ordered run log (runs ≥ 20,000 cycles), origin = trace start, in VBlanks:

```
   1.9261  +0.0513  _demo_render_finalize
   2.8709  +0.0809  _demo_render_finalize
   3.0590  +0.0673  WAIT_VBLANK
   3.1649  +0.8204  WAIT_VBLANK
   3.9881  +0.1422  WAIT_VBLANK
   4.2334  +0.7558  WAIT_VBLANK
   4.9921  +0.1421  WAIT_VBLANK
   6.0231  +0.0722  _sm64_saturn_scene_admit_with_scratch
  ...
  12.9739  +0.0468  _demo_render_finalize      <-- next frame, +11.048 VB
  13.9303  +0.0820  _demo_render_finalize
  14.1196  +0.0501  WAIT_VBLANK
  14.2083  +0.8203  WAIT_VBLANK
  15.0314  +0.1422  WAIT_VBLANK
  15.2769  +0.7556  WAIT_VBLANK
  16.0354  +0.1421  WAIT_VBLANK
  17.0664  +0.0722  _sm64_saturn_scene_admit_with_scratch
```

Two consecutive frames, identical to three decimals. Reading it:

- The idle is **one contiguous block**, ~3.06→5.13 VB into the frame,
  immediately after `demo_render_finalize` completes the VDP1 command list.
- The `(0.8204, 0.1422)` and `(0.7558, 0.1421)` pairs are **single
  `WAIT_VBLANK` calls split by the VBlank-IN interrupt** — the 0.0028 VB gap
  between each pair is the ISR. Each pair is one full field. VBlank is visible
  as `[3.988, 4.130]`, 0.142 VB ≈ the 39/263 blanking fraction.
- So the block is **~3 costly `WAIT_VBLANK` calls per frame**, ~1.93 VB logged,
  2.1903 VB measured including sub-threshold excursions. The remaining ~10
  excursions per frame are near-free (median run length **30 cycles**): the
  master calls the wait when it is already at the boundary.
- Between the waits it runs **17,270 and 46,100 cycles** — 0.141 VB/frame.

**That is the whole indictment: ~2.19 VB/frame of raster spin to interleave
~0.14 VB/frame of work.** Cross-checked against symbol residency: the entire
transport, publication, DMA-queue, frame-bank and pipeline-step machinery costs
**31,113 cycles/frame = 0.070 VB/frame** on the master, of which ~20,800 is the
VDP2 HUD text — itself diagnostic furniture, as T2.8 §7 already noted.

### 3.4 The fork-join premise is dead code

`sprint2-reference-technique-gaps.md` §1.1 describes "four independent
short-lived fork-joins per frame, each blocking the master immediately"
(`saturn_demo_render.c:1474,1532,3972` + `slavedriver_terrain_worker.c:14`),
each spinning at `slavedriver_dual_worker.c:141-148`. **None of that is in the
shipped build.**

- `_sm64_saturn_dual_worker_run` is **absent from the product ELF's symbol
  table**, while `_sm64_saturn_dual_worker_cancelled` from the same translation
  unit is present — the signature of `-ffunction-sections -Wl,--gc-sections`
  (`src/port/saturn/sourceboot/Makefile:739,755`) removing a function nothing
  references.
- The reason nothing references it: its three call sites
  (`saturn_demo_render.c:1506,1564,4004`) live inside
  `demo_dispatch_mario_transform` and `demo_upload_vdp1_dual`, and **neither
  static function is called from anywhere in the file**. GCC drops them, and
  the linker then drops the worker.
- `_render_job_slave_entry` (`0x06077d18`) *is* present, and the measured slave
  symbols are all render-job-graph callbacks
  (`_demo_terrain_queue_world_lower`, `_sm64_saturn_ir_transform_one`,
  `_demo_actor_lower_compat_wrapper`, …).
- Direct measurement: `barrier_cycles: 0`, `barrier_share: 0.0`.

The reference study's structural diagnosis was sound about the *source*; it is
simply describing code the product does not link. Any plan built on "widen the
four fork-join windows" is planning against dead code.

---

## 4. The leading hypothesis, tested and refuted

> *Leading hypothesis, to be tested and not assumed: VRAM/CRAM transfers that
> can only run during blanking, so a frame's transfers spill across successive
> VBlank windows.*

**Refuted, three ways.**

1. **The wait contains no transfer** (§3.1). It is `while (TVSTAT…);` twice.
2. **The transfer path has no blanking gate anywhere.**
   `saturn_dma_queue_kick()` (`src/port/saturn/gpl/slavedriver_dma_queue.c:232-268`)
   starts the SCU DMA the moment level 0 is free; `saturn_dma_queue_poll()`
   (`:281-310`) checks `scu_dma_level_busy(0)`. Neither reads TVSTAT, a VBlank
   counter, or any blanking condition, and neither does
   `sm64_saturn_vdp1_frame_bank_poll_transfers()`
   (`src/port/saturn/gfx/saturn_vdp1_frame_bank.c:326-353`).
3. **The transfers are rate-limited by the scheduler, not by hardware.**
   `saturn_dma_queue_poll()` and `saturn_dma_queue_kick()` have **exactly one
   caller in the tree** — `sm64_saturn_vdp1_frame_bank_poll_transfers`, reached
   only from `sourceboot_frame_poll_transfers`, which the pipeline permits
   **once per field** (§3.2). A queue of two descriptors (command list +
   Gouraud bank) therefore needs two field-separated polls to retire, and a
   third field for publication, even though the DMA itself completes in
   microseconds and the master's share of that work measures 0.023 VB/frame.

The premise was reasonable — it is the standard Saturn constraint — and it is
not what is happening here. **The spill across successive VBlank windows is
real; the reason is a polling rate, not a hardware window.**

There *is* one genuine hardware property nearby, and it is not this: VDP1
changes frame buffers only at a field boundary. But T2.8 measured
`vdp1_sync()` at **one FRT tick** and proved it non-blocking, so field
quantisation delays *when the image appears*, not when the CPU may continue.
It does not account for the 2.19 VB.

---

## 5. Deliverable (3): the VDP1 verdict

**Confirm or refute that VDP1's idleness is fully explained by CPU-side
starvation?** — **Refuted, and the premise it rests on is a category error.**

### 5.1 VDP1 is busy, not idle

| Quantity | Value | Basis |
| --- | ---: | --- |
| `EDSR.CEF` set (VDP1 has finished its list) | **12.0%** of 300 samples | this task, `id-a61d5203793986e7` |
| **VDP1 plotting** | **88.0% ≈ 9.86 VB/frame** | same |
| `COPR` range observed | index **6 → 547**, 30 distinct | same; command index is `COPR/4` per `vdp1/cmdt.h:234-239` |
| Commands emitted per frame | **502.8** mean | `sprint2-t2_14-route-counters.json` |
| VBlanks with `EDSR.CEF` set | 1.48% | T2.8, on the slower instrumented build |

VDP1 starts one plot per present (`vdp1_sync_render()`, `main.c:1327`, reached
only from `sourceboot_present_generation`) — i.e. once per ~11.2 fields — and
takes about 9.86 of them to sweep a ~503-command list to completion. It is at
**~88% load on a single pass**, with `CEF` genuinely reached (12%), so it is
neither starved nor silently over-subscribed on this build. T2.8's inferred
"~1.4x over-subscription" was drawn from a `COPR`-retirement rate on a
different, slower, instrumented build, sampled once per field at VBlank-OUT
rather than uniformly in time; this measurement supersedes it for
`id-a61d5203793986e7` and should not be read as contradicting T2.8's headline,
which was about the *fence* and remains correct.

### 5.2 "552 of 1,664" is a storage statistic, not a load statistic

`SOURCEBOOT_VDP1_COMMAND_CAPACITY` is 1,664 (`main.c:767`) because
`sourceboot_vdp1_cmdts[2][1664]` is a **static array** (`main.c:806`). Using
552 of 1,664 slots says how much command VRAM is spare. It says nothing about
whether VDP1 has time to plot them. The two questions were conflated, and the
conflation is what made VDP1 look idle.

### 5.3 What this does to the fill-rate levers

- **Today, as a cadence lever: still demoted.** The CPU never waits for VDP1
  (T2.8: 0 waits / 1,349 fence events), and §2.1 shows the frame is exactly
  master work plus master stall. Cutting commands today buys no frames.
- **After §9 item 1: promoted, and binding.** Master work is 9.0166 VB/frame
  and VDP1's plot is ~9.86 VB/frame. **Releasing the master's stall puts the
  frame against VDP1 at ~9.86 VB.** The fill-rate work T2.8 correctly declined
  becomes the *next* lever rather than a dead one, and the ordering matters:
  doing it first still buys nothing.
- **As a fidelity dividend: there is only ~12% of headroom**, not a multiple.
  Adding commands lengthens a plot that already occupies 88% of the frame.

---

## 6. Releasable versus structural, with numbers

| Block | VB/frame | % of frame | Releasable? | Reasoning |
| --- | ---: | ---: | --- | --- |
| Master raster spin, `main.c:2013` | **2.1903** | 19.5% | **Releasable — essentially all of it** | Caused by a scheduler policy (§3.2), not by hardware. **No data crosses between CPUs**, so neither hard constraint applies. The work it interleaves costs 0.070–0.141 VB/frame. The one genuine hardware property nearby (field-quantised frame-buffer change) does not block the CPU: `vdp1_sync()` measures one FRT tick (T2.8). |
| Slave notification wait, `___slave_polling_entry` | **8.8697** | 79.1% of the *slave* | **Releasable in principle, heavily taxed in practice** | Only 6.6794 VB/frame of it overlaps master *work* and is therefore convertible at all. Every VB converted moves data across an incoherent cache boundary and onto a shared bus, and §7 shows this rig prices both at zero. |
| Fork-join barrier | 0.0000 | 0% | n/a | Not in the shipped ELF (§3.4) |
| VDP1 draw fence | 0.0000 | 0% | n/a | 0 waits in 1,349 events (T2.8) |
| Field quantisation of presentation | ≤ 1 field of *display* latency | — | **Structural** | VDP1 swaps buffers at a field edge. Costs latency, not CPU time. |
| VDP1 plot time (~9.86 VB/frame) | — | — | **Structural at fixed command count** | Not idle; not CPU time; becomes the binding constraint once the master drops below it (§5.3) |

### 6.1 What releasing the master stall is worth

Frame = master work + master idle = 9.0166 + 2.1903 = 11.2069, exactly.
Removing all master stall at constant work gives **9.0166 VB/frame**, but VDP1
needs ~9.86, so the realistic landing point is **~9.86 VB ≈ 6.09 FPS, about
+13.7%**. If VDP1 command count also falls, the floor is 9.0166 VB ≈ 6.65 FPS
(+24%). **Both are estimates and are labelled as such** — they assume the
master's work is unchanged by the scheduler change, which is the assumption
this codebase's estimate history says to distrust (T2.10 item 3 predicted
−0.55 VB and measured +0.075; T2.14 over-estimated by 24x). Nothing here is a
cadence measurement.

### 6.2 What perfect packing is actually worth — the brief's figure corrected

The brief's arithmetic was 73.33% idle ⇒ 26.67% work ⇒ 11.2069 × 0.2667 =
2.99 VB ≈ 20 FPS. With the measured idle:

- machine capacity: 2 × 11.2069 = 22.4138 VB-equivalents per frame;
- work actually done: master 9.0166 + slave 2.3372 = **11.3538**, i.e.
  **50.66% of capacity**, not 26.67%;
- perfect packing across both CPUs: 11.3538 / 2 = **5.677 VB/frame ≈ 10.6
  FPS**, not 2.99 VB / 20 FPS.

**The theoretical ceiling is roughly half what the sprint has been assuming**,
and it is unreachable for the reasons in §7. The honest reading: the frame is
about half full, not three-quarters empty.

---

## 7. What each hard constraint costs — and the measurement gap that matters most

The brief asked what each constraint costs. The most important answer is that
**this rig cannot price two of the three, and that is a property of the rig.**

### 7.1 Cache coherency: unpriced here, and the tax falls entirely on the slave plan

Ymir's headless configuration sets `m_emulateSH2Caches = false`
(`ymir-core/src/ymir/sys/saturn.cpp:156`) and the headless app never enables
it. **The SH-2 caches are not emulated.** Per-region access latency *is*
modelled (`SH2::AccessCycles`, `sh2.cpp:965-988` — which is why the TVSTAT poll
measures 8.33 cycles/instruction), but a cache-through P2 access and a cached
P0 access to the same HWRAM cost the same in this emulator.

Consequences, stated plainly:

- **Every cadence and profile number this project holds is blind to the
  cache-through cost of handing work to the slave.** That includes T2.13,
  T2.14, T2.15, the cadence rig, and this report.
- The port already pays that cost on hardware: the render-job graph publishes
  through cache-through aliases (`graph_cache_through()` in
  `saturn_render_job_graph.c`, `runtime_fence()` in
  `saturn_render_job_runtime.c`, `cpu_cache_purge()` in libyaul's
  `_slave_init`, `cpu_dual.c:122-140`), and SlaveDriver does the same thing at
  `WALLS.C:1272-1273`.
- Therefore **the 6.6794 VB/frame "master works while slave idles" block is an
  upper bound on what slave offload could return**, and the true figure is
  strictly lower by an amount this rig cannot report. Cost: **unmeasured, and
  unmeasurable without either hardware or a cache-emulating Ymir run.**
- **It costs the master-stall block nothing at all**, because that block moves
  no data between CPUs. This asymmetry is the single most decision-relevant
  fact in the report.

### 7.2 HWRAM bus contention: also unpriced here

`StepMasterSH2Impl` advances the slave by `slaveSH2.Advance(masterCycles, …)`
(`saturn.cpp:614`) with no arbitration between the two CPUs' external
accesses. **Inter-SH-2 bus contention is not modelled.** Same consequence: it
taxes the slave-offload block and not the scheduler block, and the taxed figure
is an upper bound.

This is *why* the existing split is coarse rather than fine, and it is a
defensible design rather than an oversight: the render-job graph hands the
slave whole jobs whose outputs are disjoint, append-only, lane-owned record
runs — the same four properties `sprint2-reference-technique-gaps.md` §1.2
extracted from SlaveDriver. The narrowness is in the *number* of jobs, not the
ownership discipline.

### 7.3 Blanking-gated transfers: measured, and they are not present

§4. There is no blanking gate on any transfer path in this port. Cost: **zero,
because the constraint is not being obeyed anywhere — the rate limit is
self-imposed.**

---

## 8. Honesty — what did not pay off, and what is still unknown

### 8.1 The five-window run was wrong, and why

The first capture used five 500,000-step windows separated by 5 VBlanks
(`sprint2-t2_16-idle-attribution.json`). It reported **master idle 15.11%**,
against the full-frame run's 19.54%. It was under-covered and phase-selected:

| Window | VB covered | Master idle |
| ---: | ---: | ---: |
| 0 | 1.3567 | **0.0%** |
| 1 | 1.4153 | **0.0%** |
| 2 | 1.4078 | **0.0%** |
| 3 | 1.3290 | **0.0%** |
| 4 | 2.3814 | 50.06% |

Total 7.8902 VB against an 11.2069 VB frame. Projecting each window's start
modulo the frame period, the covered set is
`[0,1.357] ∪ [1.565,2.973] ∪ [3.095,5.476] ∪ [6.357,7.772] ∪ [7.973,9.302]` —
**70.4% of the frame, with a 1.905 VB hole immediately before the frame
boundary.** Because the spin is *one contiguous block* (§3.3) rather than
spread, four of five windows missed it entirely.

**Lesson, recorded so it is not repeated: for a quantity that is contiguous in
the frame, window count does not substitute for window length.** Size the
window against the frame period, not against wall-clock convenience. Both runs
are kept in evidence; the five-window file is superseded on every number.

### 8.2 How the 73.33% figure arose, quantitatively

`exec.run_for` stops at the entry to `BlankingAndSync`, so every burst begins
at the start of a VBlank. The master spins through 3 of the frame's ~11.2
VBlank periods (§3.3), so P(a burst lands in the spin) ≈ 3/11.2 ≈ 26.8%. The
profiler weights by cycles, and a spin instruction costs **8.33 cycles**
against ~1.2 for cached work — a **6.9x** amplification. Weighted share:

```
0.268 x 3332 / (0.268 x 3332 + 0.732 x 480) = 71.8%
```

against the 60.1%-of-master the burst profiler reported. Same mechanism, right
order of magnitude. **The slave figure survives** (79.1% measured against 86.6%
sampled) because the slave changes state only about twice per frame — one wake,
one retire; its wait runs average **3,477,623 cycles = 7.77 VB** — so
phase-locking barely moves it.

**Consequence for the sprint's records: `70.21%` (T2.13 §1.5) and `73.33%`
(STATE.md) should be read as pooled, phase-locked figures. The corrected pair
is master 19.54% / slave 79.15%, combined 49.34%.** T2.13's and T2.14's
*non-idle* rankings are unaffected in relative order — they are within-CPU
comparisons — but their absolute shares of the *frame* are understated roughly
twofold, which is what §2.2 shows for float.

### 8.3 Things attempted that did not work

1. **Estimating the cache-through penalty from per-CPU cycles-per-instruction.**
   The tool charges *master* cycles to the slave's last-observed symbol, so
   slave cycles measure residency time, not slave instruction cost. A
   slave-stepping pass would fix that — and would still return nothing, because
   §7.1 shows the emulator does not model the effect. Abandoned twice, for two
   different reasons, and the second one is fatal.
2. **Attributing the wait site by call-edge sampling**, as T2.14 §7.1 tried for
   `find_floor`. Not attempted here after reading that section; PR reading was
   chosen from the start and returned 24 of 24 entries. Recorded as prior art
   that paid off.

### 8.4 What remains unmeasured

- **The cache-coherency and bus-contention cost of any slave offload.** §7. The
  6.6794 VB block cannot be priced on this rig at all.
- **Which specific actions occupy the 17,270- and 46,100-cycle gaps.** They are
  inferred to be `POLL_TRANSFERS` and `PUBLISH_FRAME` from the state machine
  and from symbol residency (31,113 cycles/frame of transport machinery), not
  from an action-tagged trace. An action tag would need a diagnostic build.
- **VDP1's exact plot time.** 9.86 VB/frame is `(1 − CEF share) × 11.2069` from
  300 strided samples with the §1.3 calibration, not a plot-start/plot-end
  bracket.
- **Whether the per-field epoch rule is load-bearing for correctness.** It is
  encoded in 11 `WAIT_VBLANK` assertions in `tools/saturn/frame_pipeline_test.c`
  and described in `overlapped-render-pipeline-2026-08-03.md:1845-1847,2069` as
  a reviewed invariant ("field-global epochs keep SERVICE/POLL bounded across
  publish/promote"). Its stated purpose is bank-ownership safety, which the
  frame-bank double buffer and the overwrite fence also provide — but that is
  an argument, not a proof, and §9 item 1 must discharge it.
- **Run-to-run variance.** One contiguous window over 1.7525 frames. The two
  frames it contains agree to three decimals on every logged run (§3.3), which
  is a strong internal consistency check but is not a repeat capture.

---

## 9. Ranked next actions, with sizes

Ordered by **releasable VB/frame per unit of risk and per unit of constraint
tax**. Every size is an estimate and labelled as one; none is a cadence
measurement.

**1. Collapse the per-field epoch stall. ~1.9–2.19 VB/frame; the only large
block the hard constraints do not tax.**
Let the scheduler run `POLL_TRANSFERS` and `PUBLISH_FRAME` as soon as their
preconditions hold instead of once per field, or make the `WAIT_VBLANK` arm
yield to the next admissible action rather than spinning. Expected landing:
**~9.86 VB/frame ≈ 6.09 FPS (+13.7%)**, capped by VDP1, not by the CPU.
*Cost:* `saturn_frame_pipeline.c` plus 11 assertions in
`frame_pipeline_test.c`. *Risk:* the epoch rule is a reviewed invariant
(§8.4); the change must show that bank ownership is still safe without it —
the double-buffered frame bank plus a **repaired** overwrite fence is the
argument to make, and T2.8 §9 item 3 already has repairing that fence on the
list. *Confirming measurement:* `summarize_cadence` on 30 events, plus a
re-run of this tool showing the block gone.
*Do this one first, and do not start item 3 before it.*

**2. Continue the soft-float purge on the master. 2.0718 VB/frame is float, on
the critical path.**
Larger than item 1 and previously mis-priced at 7.5% because the denominator
pooled in the slave's idle spin. Leaders, in VB/frame: `___mulsf3` 0.627,
`___addsf3` 0.471, `___divsf3` 0.303, `___subsf3` 0.257, `___udiv_qrnnd_16`
0.149.

The named consumer to look at first is **`_sm64_saturn_ztreme_frustum_aabb` at
0.649 VB/frame — 5.79% of the critical path, the largest application symbol on
the master after the two render-phase bodies**, and absent from T2.14's top-50
entirely because the pooled denominator halved it. It carries history: it is
exactly the function T2.10 item 3 rewrote to cross-multiplied divides, a change
that was predicted at −0.55 VB and **measured at +0.075 VB — a regression**
(`sprint2-t2_10-spatial-admit-fixes.md:147,181`). It is now correctly priced
for the first time, and the prior attempt is the standing warning about pricing
SH-2 costs serially. Behind it: `_saturn_geo_enter_object` (0.241 VB/frame) and
`_saturn_mtxq_refresh_float_mirror` (0.226).

*Risk:* low and well-rehearsed — T2.13's shadow-trig change is the template,
and the slave already runs Q16-only end to end, which is the existence proof
that the conversion works. *No constraint tax:* master-local, no data crosses
CPUs.

**3. VDP1 command reduction — promoted from T2.8's demotion, but strictly after
item 1.** At ~9.86 VB/frame of plot against 9.0166 VB/frame of master work,
VDP1 becomes binding the moment the stall is released. Before item 1 it is
worth zero frames; after it, it is the wall. T2.8's own list (user clipping,
command-count LOD, the Mario double-emit) becomes live again with a *cadence*
success criterion for the first time. *Size:* not estimated here — it needs a
plot-time bracket (§8.4) before anyone prices it.

**4. Widen the slave's job graph. Targets 6.6794 VB/frame; the largest block
and the least safely sizeable.**
The graph is **four jobs** — `WORLD_ADMIT → WORLD_LOWER`, `ACTOR_ADMIT →
ACTOR_LOWER` (`saturn_demo_render.c:4534-4562`) — and the slave drains all of
them in 2.3372 VB, then sleeps for 8.87. `drain_master` runs only *after*
`slave_retired` (`saturn_render_lifecycle.c:65-66` gates, `:72-73` runs), so the master normally
claims nothing from the graph at all. The reference answer is SlaveDriver's:
split the coarse spatial unit into many claimable jobs and add a load
controller. *Why it is ranked fourth despite being the biggest:* §7 — every VB
converted is taxed by an incoherent cache and a shared bus, and **this rig
prices both at zero**, so any number produced for it here would be an upper
bound presented as an estimate. It also needs the master's serial tail
(`demo_render_finalize`, 1.004 VB/frame, all VDP1 lowering) to stay
master-owned, which both this port and SlaveDriver deliberately require.
*Prerequisite:* a way to measure or bound the coherency cost — hardware, or
Ymir with `m_emulateSH2Caches` enabled.

**Not recommended:** anything premised on widening the four fork-join windows
(§3.4 — dead code), and anything premised on VDP1 being starved (§5).

---

## 10. References

Per the standing owner instruction, and per the reference-code-first rule.

| Repo | Pinned SHA | License | Files inspected | Reuse mode |
| --- | --- | --- | --- | --- |
| `work/upstream/slavedriver-engine` | `a8986591557b6e680550d3c23970284d3b38ff8f` | GPL-3.0-or-later (`LICENSE.txt`) | `WALLS.C` (`:1803-1834`, `:1921-1934`, `:2242-2246`, `:2247-2268`, `:2273-2285`), `SRUINS.C` (`:2094-2160`, `:2239-2267`), `V_BLANK.C` (`:36`, `:130`) | **behaviour/pattern comparison only** — nothing copied, adapted, or ported in this task |
| `third_party/libyaul` (vendored) | as vendored | MIT | `scu/bus/b/vdp/vdp2/tvmd.h:84-97`, `scu/bus/cpu/cpu_dual.c:98-140` (the slave's `while (true) { cpu_dual_notification_wait(); _slave_entry(); }`), `scu/bus/b/vdp/vdp1/cmdt.h:234-239`, `scu/scu/map.h:90-122` | dependency, read for semantics |
| `ymir-agent` (the measurement rig) | working tree | — | `libs/ymir-core/src/ymir/sys/saturn.cpp:150-156,496-510,594-627`, `libs/ymir-core/src/ymir/hw/sh2/sh2.cpp:965-1011`, `apps/ymir-headless/src/debug_service.cpp:535-700` | read to establish what the rig does and does not model (§7) |

**Z-Treme contributes nothing here and was not opened.** Both its `slSlaveFunc`
sites are commented out; it is a single-CPU application, as
`sprint2-reference-technique-gaps.md` already records.

### 10.1 The one contrast worth carrying forward

SlaveDriver paces with an **interrupt-maintained field counter and an adaptive
target**, exactly once per frame:

```c
while (vtimer<smoothVTime) ;     /* SRUINS.C:2252 */
SCL_DisplayFrame();              /* SRUINS.C:2255 */
...
framesElapsed=vtimer; vtimer=0;  /* SRUINS.C:2265, :2267 */
```

`vtimer` is incremented by the VBlank ISR (`V_BLANK.C:36,130`) and
`smoothVTime` is clamped to ≤ 2 fields and adapted in both directions
(`SRUINS.C:2240-2248`, `:2258-2263`). **When the engine is already behind, the
wait is zero** — the comparison simply fails and execution continues.

This port has the identical primitive and does not use it that way.
`sourceboot_vblank_out_count` is maintained by the VBlank-OUT handler
(`main.c:989-995`, registered at `main.c:2046`) and is read by the scheduler on
every step — yet the port spins on the raster **once per field, unconditionally,
with no notion of being behind**. That is item 1 of section 9, stated as a
one-line difference against the one engine that solved this on this hardware.
