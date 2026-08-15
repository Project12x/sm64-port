# Sprint 2 Task T2.4 — decomposing the pre-notification window

- Date: 2026-08-15. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `1e46b684`.
- Plan: `docs/superpowers/plans/2026-08-15-sprint2-cadence-recovery.md`,
  Task T2.4. Instruments pinned by T2.0's reference sweep
  (`sprint2-t2_0-reference-sweep.md`, lessons **L14** and **L12**).
- Why now: T2.3 (`sprint2-t2_3-painter-counting-sort.md`) recovered 1.72%
  of cadence and, in doing so, proved that the counting sort was ~4% of
  construction. It left the **pre-notification window at 18.92 of the
  frame's 55.22 VBlanks — 34% of the whole frame — never decomposed**, and
  bit-identical across three consecutive builds.
- Question answered: what is actually inside those 18.92 VBlanks, and is
  the master/slave split leaving the slave idle?
- **This is a measurement task. Nothing was optimised.** Findings for T2.5
  are recorded, not acted on.

## Headline

The pre-notification window is **two stages and nothing else.** Over 798
windows on the scripted route:

- **`demo_prepare_mario()` — Mario actor meshlet preparation — is 69.24%
  of the window** (41,784 FRT ticks = 5.35 M SH-2 cycles = 11.67
  VBlanks/frame). It is the largest single measured block anywhere in this
  port: roughly **21% of the whole frame**.
- **`demo_spatial_admit()` — the BSP frustum admission walk — is 26.42%**
  (4.45 VBlanks/frame).
- Together: **95.7%.** The other twelve named stages plus the measured
  residue are **4.3%**, and ranks 6–15 total 0.15 VBlanks/frame.
- **Unattributed remainder: 0.048%.** The parts do sum to the whole.

**L12, measured:** the master's spin-wait on the slave is **zero by
construction** — the lifecycle returns `PENDING` and the master services
other frame actions; the port's only blocking spin (`dual_worker_run`) sits
in three functions the compiler reports as dead in this tuple. The slave is
**busy 1.02× its own overlap window** (3.15 vs 3.085 VBlanks-equivalent),
so there is no idle-slave slack inside the split. The imbalance is that the
master keeps 16.85 VBlanks of pre-notification work to itself *before the
slave is notified at all*.

**Two of the published fields are invalid and are not used** — see
§7 item 1. Nothing was optimised.



## 1. The boundaries, established before instrumenting

`sm64_saturn_render_overlap_phase_terminal()`
(`src/port/saturn/runtime/saturn_render_overlap_phase.c:95-113`) derives
`construction = start_construction + finalization`, where

- `start_construction = notification_vblank - construction_begin_vblank`
  — **the pre-notification window**;
- `finalization = terminal_vblank - retirement_vblank` — master
  finalization; and
- the slave overlap (`retirement - notification`) is a *sibling*, excluded
  from construction.

So the window runs from `sm64_saturn_render_overlap_phase_begin()`
(`src/port/saturn/sourceboot/main.c`, in `sourceboot_frame_service_render`'s
first-entry branch, which stamps `construction_begin_vblank`) to the
`SM64_SATURN_RENDER_JOB_RUNTIME_MARKER_NOTIFIED` observer (same file,
`sourceboot_render_runtime_marker`, which stamps `notification_vblank`).
It is one **synchronous master-serial block**: nothing yields inside it.

Walking that path produced the candidate sub-stage list **before** any
probe was written. In execution order:

| # | Node | What runs | Where |
| --- | --- | --- | --- |
| — | `phase_begin` | stamps `construction_begin_vblank` | `main.c` `sourceboot_frame_service_render` |
| 1 | `snapshot_acquire` | `sm64_saturn_render_snapshot_acquire_ready()` — claim the tick's ready scene snapshot | `main.c` |
| 2 | `actor_pose` | copy the Mario actor snapshot, then `sm64_saturn_mario_actor_pose()` (selector variant compiled out at `complete_mario_animation=0`) | `main.c` |
| 3 | `bank_open` | `vdp1_sync_busy()` probe, `sm64_saturn_vdp1_frame_bank_begin_build()`, phase bind, camera + HUD snapshot publish, `sm64_saturn_vdp1_backend_bind_frame_bank()` | `main.c` |
| 4 | `spatial_admit` | `demo_spatial_admit()` — Z-Treme-derived BSP frustum admission (`bsp_order=1`, `bsp_fragments=0`) | `saturn_demo_render.c` `demo_render_prepare_publish` |
| 5 | `work_order` | `demo_prepare_render_work_order()` — scene admission, per-primitive work-order build | same |
| 6 | `position_set` | `demo_build_visible_position_set()` — the visible position set the transform jobs consume | same |
| 7 | `frame_reset` | `memset(s_position_valid)`, `sm64_saturn_dual_frame_reset()`, three LOD-state `memset`s | same |
| 8 | `prepare_mario` | `demo_prepare_mario()` — actor meshlet preparation (`sm64_saturn_actor_meshlets_prepare`), draw/texture/command counting | same |
| 9 | `actor_closure` | `demo_generic_actor_prepare()` — **compiled out** in this tuple (`dynamic_actor_closure=0`) | same |
| 10 | `mario_ctx` | `demo_snapshot_mario_transform_context()` — the actor transform context the queue publishes | same |
| 11 | `queue_reset` | `demo_render_queue_reset_frame_banks()` — callback-context bank init, two output-bank inits, four metadata `memset`s | same |
| 12 | `graph_publish` | `sm64_saturn_render_job_graph_publish()` — 2 or 4 job descriptors + dependency mask | same |
| 13 | `queue_contexts` | `demo_render_queue_prepare_contexts()` — terrain queue context snapshot + per-job payload publication | same |
| 14 | `notify` | the lifecycle controller's `ops->notify` → `sm64_saturn_render_job_runtime_notify()` → `cpu_dual_slave_notify()` and the marker publish | `saturn_render_lifecycle.c` / `saturn_render_job_runtime.c` |
| 0 | `window_residue` | **everything not named above**: the job-descriptor array construction, `demo_camera()`/job setup, `vdp1_vram_partitions_get()`, `sm64_saturn_lod_lifetime_begin()`, `sm64_saturn_render_lifecycle_start()` bookkeeping, and any control flow between probes | — |

Node 0 is deliberately the residue, so the "unattributed remainder" is a
measured quantity rather than an arithmetic leftover.


## 2. What was added, and why the product build cannot see it

### The instrument (L14)

`src/port/saturn/runtime/saturn_prenotify_profile.h` (new) plus probe
call sites. Design taken from SlaveDriver Engine `PROFILE.C:11-30,41-45,66-80`
(`work/upstream/slavedriver-engine` @ `a898659`, GPL — **reference only, no
source copied**; reuse mode: pattern-only), which T2.0 recorded as L14.

Reused shape:

- **Fixed node table, zero allocation.** 16 nodes, 8-deep stack, all
  statically sized.
- **Nestable push/pop that charges at every transition.** SlaveDriver's
  `currentNode->totalTime += (getTimer()-lastTime)&0xffff` on both push and
  pop is the whole trick: every accumulation interval is one probe gap, so
  a parent's self time is assembled from short pieces and no single
  accumulation ever spans a whole stage.
- **Raw FRT register reads** (`0xFFFFFE10`, FRC high/low at +2/+3, TCR at
  +6 — SlaveDriver `PROFILE.C:5-10`), not a library call, and no per-sample
  I/O.

Deliberate divergences, stated so they are not re-litigated:

1. **Node identity is a compile-time id, not a string pointer.**
   SlaveDriver grows a 60-node tree at runtime and searches children by
   pointer identity. The nesting here is static and known, so the search
   loop and the `assert(nmNodes<MAXNMNODES)` failure mode buy nothing.
2. **φ/128, not SlaveDriver's φ/32.** `setFastTimer()` selects cycles/32
   (`PROFILE.C:11-22`). A 16-bit FRT at φ/32 wraps every 2,097,152 SH-2
   cycles ≈ **4.7 VBlanks** — smaller than sub-stages plausibly are here.
   φ/128 wraps every 8,388,608 cycles ≈ **18.7 VBlanks**, comfortably
   above any one stage. Resolution is 128 cycles ≈ 4.8 µs; one VBlank is
   ~3,509 ticks, so a stage worth 1% of the window still reads ~660 ticks.
3. **32-bit totals.** The window itself is ~66,000 ticks — just past one
   16-bit wrap — so totals are accumulated in 32 bits by summing
   `(uint16_t)(now - last)` per probe.

Wrap safety has two guards. The extended clock is **re-seeded at every
window begin**, so the long idle between frames is never accumulated
across; and the largest single inter-probe interval seen anywhere in the
run is published as `max_raw_interval`. A value approaching 0xFFFF means an
interval nearly aliased and the totals must be discarded; the harness gates
on it (`frt_wrap_headroom_ok`, threshold 61,440).

### The companion measurement (L12)

Three fields, all on the same instrument:

- `notify_to_retire` — master-clock wall time from the NOTIFIED marker to
  the RETIRED marker, i.e. the slave-overlap window.
- `finalize_ticks` — RETIRED marker to phase terminal, i.e. master
  finalization (the stage T2.3 moved).
- `slave_busy` — measured **on the slave's own FRT block** (the FRT is
  CPU-local), around `sm64_saturn_render_job_runtime_poll_slave()` inside
  `render_job_slave_entry`. The slave selects its own φ/128 divider once,
  on first entry.

### Storage and publication

Working state is ordinary cached HWRAM `.bss` (fast); the 288-byte record
lives in NOLOAD `.lwram_bss` and is written through the SH-2 P2
cache-through alias **once per window**, not per sample — the
`sourceboot_cadence_trace` / `saturn_peak_probe` pattern, so Ymir's
`mem.peek` observes backing LWRAM rather than a stale cache line.

### Gating, and the proof it is product-clean

Everything that can emit code or data is behind
`SATURN_DIAGNOSTIC_MODE != 0 && defined(__sh__)`, mirroring T2.1's
`saturn_peak_probe.h`. Mode **2**, not mode 1, for T2.1's reason: mode 1
also compiles the animation sweep, whose Mario-animation override would
perturb the very route being measured.

The proof is not a grep this time. Each of the three modified translation
units was compiled **at `SATURN_DIAGNOSTIC_MODE=0`, with the build's own
flag set, twice from the same path** — once with `HEAD:<file>` content and
once with the working-tree content — and the objects compared:

| Translation unit | Mode-0 object at `-g0` |
| --- | --- |
| `saturn_render_job_runtime.c` | **byte-identical** (5,356 B) |
| `sourceboot/main.c` | **byte-identical** (660,904 B) |
| `saturn_demo_render.c` | section sizes identical, **symbol table identical**, **`.text` disassembly identical**, all section contents identical **except two ASCII line-number literals** |

Those two literals are `1019`→`1020` and `1793`→`1794` inside the
`assert()` message strings this file already carried: `assert` expands
`__LINE__`, and the probe statements shift the lines below them. Both
literals are four digits before and after, so even the string lengths are
unchanged. **No instruction, no data byte, and no symbol in the product
build changes.** (Full `-g` objects do differ, in `.debug_line` only, for
the same line-shift reason.)

Preprocessing corroborates: at mode 0, the working-tree translation units
differ from their predecessors only by the profiler's `enum`, its
`typedef`, and one `_Static_assert` — none of which emit a byte.

### Perturbation — quantified, and where it is not

A probe is two byte reads of an on-chip I/O register plus a handful of ALU
ops; call it ~40 SH-2 cycles. **28 probe events per frame** (13 push/pop
pairs plus begin/end) is ~1,120 cycles against a pre-notification window of
~8.5 million — **~0.013%**. Publication is one pass over 72 words through
P2, once per window.

Crucially, **no probe is inside a loop over scene data.** Every probe sits
at a stage boundary in `demo_render_prepare_publish` or
`sourceboot_frame_service_render`. The one place a probe could have
perturbed what it measures — inside the per-primitive admission or classify
loops — was deliberately left unprobed, which is also why `work_order`,
`position_set` and `prepare_mario` are reported as single opaque stages
rather than broken down further. That is a limitation of this pass, stated
plainly, not an oversight.

Two structural caveats on the instrument itself:

1. **Selecting φ/128 retunes the FRT for the whole diagnostic build.** The
   pre-existing `sim_frt_ticks_*`, `render_frt_ticks_*` and
   `dma_wait_ticks_*` telemetry therefore reads in φ/128 units in this
   build and φ/8 units in a product build. Nothing in the port makes a
   decision from an FRT count (they are published telemetry only —
   verified by reading every consumer), so this changes no behaviour, but
   it does change what those numbers mean and is recorded here so nobody
   compares them across build modes.
2. **Those pre-existing FRT rails were already unreliable for long
   intervals.** At the product build's φ/8, a 16-bit FRT wraps every
   ~1.17 VBlanks, so `render_frt_ticks_last` — which brackets the whole
   render service — has been aliasing for its entire existence. That is a
   pre-existing defect this task discovered rather than caused; it is not
   fixed here.

### Failure paths

If `demo_render_prepare_publish` returns false, the window is abandoned:
the NOTIFY node is never pushed and `..._end()` is never reached, so the
next `..._begin()` finds the window still open, increments `faults`, and
re-seeds. The capture harness gates on `faults == 0`, so an abandoned
window cannot silently contaminate the means.

## 3. Build and identity

Same 27-variable invocation as `sprint1-stage1-link-smoke.md` / T2.2 / T2.3
(pool 208), with **exactly one variable different: `SATURN_DIAGNOSTIC_MODE`
0 → 2**. Via `tools/saturn/with-msys-toolchain.ps1` → MSYS
`sh --noprofile --norc -l`, sourcing `../../.yaul.env`, then
`unset COMPILER_PATH`.

The identity bootstrap requires Make-provided config to equal the profile's
`release_config`, so `tools/saturn/profiles/sourceboot-bob-demo-v1.json`
had `diagnostic_mode` flipped 0 → 2 as an **uncommitted, byte-canonical
(LF, no CRLF) edit**, reverted immediately after the build — T2.1's
established precedent. The committed product profile still reads
`diagnostic_mode: 0`.

**One build attempt, exit 0.** The g15 package-staleness cascade did not
require the documented repair: the actor bundle regenerated cleanly at
generation 15 within the same invocation.

Sealed identity **`id-5b28a329c1e8f9de`**.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `obj/sm64-saturn-sourceboot-e2.elf` | 9,984,072 | `b86d8bfe1124ae9c49390542692422a9edfd1a5b811c6e73e4c09d9262f0a2ec` |
| `sm64-saturn-sourceboot-e2.iso` | 5,171,200 | `286d1655e87b92470af705844624eb5433662c06c8fbe18b096ae44ee66999ff` |
| `sm64-saturn-sourceboot-e2.cue` (88 B, not identity-bearing) | 88 | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` |
| `saturn-release-manifest-v1.json` | 3,271 | `6da31fdc6a3d764167933795ab678dbcda59d3e11edb300f1fd8bfb728c405cf` |

Preservation (mandatory rule): the accepted and current candidates
(`id-aa57d83c898e3af1`, `id-6b7c7e5d5f71e809`, `id-86d3880727ed1d10` —
ELF/ISO/CUE/manifest) copied to
`releases/2026-08-15_1610_t2_4-pre-build/` **before** the build ran; this
build's artifacts to `releases/2026-08-15_1625_t2_4/`.

Probe symbols in the linked image (`sh-elf-nm`):
`g_sm64_saturn_prenotify_profile` at `0x002D8958` (LWRAM, NOLOAD),
`g_sm64_saturn_prenotify_profile_state` at `0x060F68D8` (HWRAM).

### Gate — `verify-memory-map` on the diagnostic build (verbatim)

```
verify-memory-map: checking .../e2-bob-identity-id-5b28a329c1e8f9de/obj/sm64-saturn-sourceboot-e2.elf
  ___end          = 0x060FB128
  hwram_remaining = 0x4ED8 bytes (required >= 0x1F00)
  lwram_end       = 0x002E8B20
  lwram_remaining = 0x174E0 bytes (floor >= 0x4000)
  RESULT          = OK
```

**RESULT OK.** True slack over the `0x1F00` floor is **12,248 B** against
the product build's 13,848 B — the whole diagnostic tuple (T2.1's peak
probe, mode 2's pre-existing job telemetry, and this profiler) costs
1,600 B of HWRAM, and only in a diagnostic build.

### Gate — host contracts (on the diagnostic tree)

| Suite | Result |
| --- | --- |
| `verify-memory-map` | **RESULT OK** |
| `verify-vdp1-painter-chain` | **PASS** |
| `verify-audio-loop-contracts` | **OK — 24 tests** |
| `verify-pcm68k-model` | **OK** |
| `verify-terrain-depth-bins` | **PASS** |
| `verify-vdp1-frame-bank` | **OK — 4 tests** |
| `verify-demo-render-overlap` | **PASS** (mutation caught by fixture) |
| `verify-render-overlap-integration` | **PASS** (mutation caught by fixture) |
| `test_dual_sh2_work_storage_contract.py` | **OK — 4 tests** |
| `test_vdp1_staging_relocation.py` | **OK** |
| `test_render_job_runtime_source.py` | **OK — 5 tests** |

**Pre-existing failures, not caused here.** `verify-render-job-runtime`
fails to compile with
`fatal error: port/saturn/platform/saturn_cart_code.h: No such file or
directory` — the recipe (`Makefile.saturn.mk:973-983`) is missing `-I src`,
the same gap base HEAD `1e46b684` fixed for the render/dma/pipeline
recipes but not for this one. The failing include is in
`saturn_render_job_queue.c`, a file this task does not touch.
`tools/saturn/test_render_snapshot_source.py` has **three** failures
(`vdp1_painter_chain_uses_all_existing_master_depth_tags`,
`mario_textured_path_uses_fixed_gouraud_tables`,
`non_diagnostic_bob_build_omits_optional_job_telemetry`); all three were
reproduced against `HEAD:src/port/saturn/gfx/saturn_demo_render.c` with
the identical result, so none is new. (T2.1–T2.3 recorded two of the three;
the script aborts at the first assertion, which is why the third had not
been seen before.)

## 4. Capture

- Instrument: headless Ymir **build-agent2**
  (`ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe`),
  BIOS `sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin`,
  absolute `--cue`, release-manifest bound, venv Python.
- Harness: `tools/saturn/capture_prenotification_profile.py` (new;
  pattern-copied from `capture_sprint2_peaks.py` — same release binding,
  sealed-identity wait, 300-frame `exec.run_for` chunking, DLL-safe
  `sh-elf-nm` resolver, Mario-position movement witness).
- Run shape: proven USA-BIOS handoff, wait for the sealed identity in
  target RAM, then **24,000 post-BIOS frames** in 300-frame chunks with a
  full 288-byte record read at every boundary. **26,181 emulated frames
  total, 305 s wall, 89 samples (77 with a valid record).**
- **Route: `ROUTE_REPLAY=1`, movement positively witnessed** — 42 distinct
  sampled Mario world positions; the cadence trace reached simulation tick
  **788**, i.e. the whole 600-tick scripted-replay window plus an idle tail.
  An idle boot would not have been representative and was not used.
- On-target identity **MATCH**; the ELF the harness bound hashes to
  `b86d8bfe…a2ec`, the build above.
- **Zero SH-2 exceptions** — `sourceboot_exception_record.magic` was `0` in
  every one of the 77 valid samples.
- **No desktop launch. The owner holds the observation gate.**
- **798 completed pre-notification windows** contributed to the means.

Artifact of record:
`docs/saturn/evidence/reports/sprint2-t2_4-prenotification-profile.json`.

### Two instruments, cross-checked

The profiler's window and the cadence rig's `construction − master_finalize`
crossings measure the *same* interval, so they calibrate each other:

| | This build, same run |
| --- | ---: |
| Rig: pre-notification VBlank crossings / frame | **16.848** |
| Profiler: window mean, FRT ticks | **60,350** |
| Implied ticks per VBlank (measured) | **3,582.0** |
| Ticks per VBlank from libyaul's own NTSC-320 constants | 3,509.1 |
| Agreement | **+2.1%** |

A 2.1% gap between an FRT-derived duration and a VBlank-crossing count is
the accuracy of the NTSC field-rate / SH-2 clock pair assumed for the
nominal figure, not a disagreement between the instruments. The
VBlank-equivalent column below is therefore computed as
*share × the rig's own 16.848*, so it sums to the rig's number exactly and
carries no assumed constant.

Other rig figures from the same terminal sample (per frame, 798 frames):
construction **22.049**, master finalization **5.201**, slave overlap
**3.085**, simulation **6.307**.

## 5. Results — the ranked cost table

Mean and max are per pre-notification window, over 798 windows.

| Rank | Sub-stage | Mean FRT ticks | Mean SH-2 cycles | VBlank-equiv | % of window | Max ticks | Max cycles |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | `prepare_mario` | 41,784 | 5,348,377 | 11.665 | **69.24%** | 54,192 | 6,936,576 |
| 2 | `spatial_admit` | 15,943 | 2,040,647 | 4.451 | **26.42%** | 18,591 | 2,379,648 |
| 3 | `work_order` | 1,887 | 241,595 | 0.527 | **3.13%** | 2,385 | 305,280 |
| 4 | `position_set` | 472 | 60,419 | 0.132 | **0.78%** | 592 | 75,776 |
| 5 | `mario_ctx` | 162 | 20,685 | 0.045 | **0.27%** | 164 | 20,992 |
| 6 | **`window_residue` (unattributed)** | **29** | **3,711** | **0.008** | **0.05%** | 32 | 4,096 |
| 7 | `queue_contexts` | 20 | 2,613 | 0.006 | 0.03% | 22 | 2,816 |
| 8 | `frame_reset` | 16 | 2,075 | 0.005 | 0.03% | 18 | 2,304 |
| 9 | `graph_publish` | 13 | 1,627 | 0.004 | 0.02% | 14 | 1,792 |
| 10 | `queue_reset` | 12 | 1,549 | 0.003 | 0.02% | 14 | 1,792 |
| 11 | `bank_open` | 5 | 652 | 0.001 | 0.01% | 6 | 768 |
| 12 | `actor_pose` | 3 | 360 | 0.001 | <0.01% | 3 | 384 |
| 13 | `snapshot_acquire` | 2 | 262 | 0.001 | <0.01% | 3 | 384 |
| 14 | `notify` | 2 | 233 | 0.001 | <0.01% | 2 | 256 |
| 15 | `actor_closure` | 0 | 0 | 0 | 0 | 0 | 0 (compiled out) |
| | **window total** | **60,350** | **7,724,804** | **16.848** | 100% | 71,894 | 9,202,432 |

### The parts do sum to the whole

**Unattributed remainder: 29 ticks, 0.048% of the window.** The named
stages account for **99.95%** of it. This is not a case where the parts
fail to reconstruct the total — the enumeration in §1 is complete, and the
`window_residue` node measured that directly rather than inferring it.

### Two stages are the window

**`prepare_mario` (69.2%) and `spatial_admit` (26.4%) are 95.7% of the
pre-notification window between them.** Ranks 3–15 together are 4.3%, and
ranks 6–15 together are **0.15 VBlanks/frame** — less than a third of one
percent. There is no diffuse cost here to chase; there are two stages.

Placed against T2.3's 55.22-VBlank frame (approximate — see Honesty),
`prepare_mario` alone is roughly **21% of the entire frame**, which makes
it the largest single measured block anywhere in this port.

### Stability across the route

Shares are not an artefact of the idle tail. Restricting the aggregate to
the 756 windows recorded while the simulation tick was still advancing
through the scripted route gives a window mean of 60,378 ticks with
`prepare_mario` **69.32%** and `spatial_admit` **26.35%** — within 0.1
percentage points of the whole-run figures. Per-sample window cost varies
between ~37,000 and ~54,000 ticks for `prepare_mario` as the scene and pose
change, but its share does not.

## 6. The master/slave split (L12)

**The master never spins on the render-job slave.** The lifecycle's
`slave_retired()` returning false makes `sm64_saturn_demo_render_poll_frame()`
return `PENDING`, and `sourceboot_frame_service_render()` returns to the
frame pipeline, which dispatches other actions. There is no spin loop to
measure on that path.

The port's only blocking spin-wait is `sm64_saturn_dual_worker_run()`
(`slavedriver_dual_worker.c:139-154`, which already carries
`master_wait_ticks`). **All three of its call sites are dead in this
tuple** — the compiler says so directly, and this is not an inference:

```
saturn_demo_render.c:1440: warning: 'demo_dispatch_mario_transform' defined but not used
saturn_demo_render.c:3961: warning: 'demo_upload_vdp1_dual'        defined but not used
saturn_demo_render.c:1025: warning: 'demo_choose_work_split'       defined but not used
saturn_demo_render.c:616:  warning: 's_slave_begin'                defined but not used
```

So **measured master spin on this route is zero, by construction.**

What the slave actually does, measured on the slave's own FRT:

| | Value |
| --- | ---: |
| Slave busy, mean | **11,043 ticks = 1,413,496 cycles = 3.15 VBlank-equiv** |
| Slave busy, max | 12,702 ticks = 3.62 VBlank-equiv |
| Rig: slave overlap window (notify → retire) | **3.085 VBlanks/frame** |
| Slave busy ÷ slave window | **1.02** (i.e. ~100%, within the same 2.1% calibration offset) |

**The slave is busy for essentially the whole window it is given.** The
split is not leaking time to an idle slave *inside* the overlap. The
imbalance is upstream: the master performs **16.85 VBlanks of
pre-notification work before the slave is notified at all**, and 95.7% of
that is the two stages above. Over a frame the slave is occupied for ~3.1
of ~55 VBlanks — but the fix for that is not a ±1 rebalance of the existing
job split, it is the work that never enters the job graph in the first
place.

## 7. Honesty — what is wrong with these numbers

**1. `notify_to_retire` and `finalize_ticks` are INVALID and are not used
above.** The instrument published them; they must be discarded. Reason:
`runtime_publish_retirement_marker()` is called from `render_job_slave_entry`
(`saturn_render_job_runtime.c:140`), so the RETIRED marker observer — and
therefore the profiler's `mark_retired()` — **executes on the slave SH-2**.
The FRT is a per-CPU on-chip block, so those two fields subtract one CPU's
free-running counter from the other's. The values they produced (4.58 and
8.18 VBlank-equiv against the rig's 3.085 and 5.201, with
`notify_to_retire`'s max pinned at 65,531 ≈ the 16-bit limit) are exactly
what that mistake looks like. **The rig's VBlank figures stand; these two
FRT fields do not.** `slave_busy` is unaffected — it is begun and ended on
the slave, and published straight to LWRAM through P2.

**2. That same path makes the slave write three bytes of master-owned
cached state** (`retire16`, `notified`, `retired` in
`g_sm64_saturn_prenotify_profile_state`, which lives in cached HWRAM). The
shipped cadence rig avoids precisely this by declaring
`sourceboot_render_overlap_phase` `__uncached`
(`sourceboot/main.c:266-267`); this profiler did not, and should. **No
contamination of the window measurement is visible**: every field the
ranked table depends on (`last16`, `elapsed`, `node_ticks[]`, `stack`,
`depth`) is written only by master paths, the parts sum to 99.95% of the
whole, `max_raw_interval` equals `prepare_mario`'s own maximum exactly, and
the window total agrees with an independent instrument to 2.1%. That is
strong evidence, not proof. **T2.5 must move this state to `__uncached` (or
drop the two invalid fields) before anything reads them.**

**3. The acceptance gate FAILED, on purpose-adjacent grounds, and the
harness exited 1.** `faults == 798 == windows`. Cause: the NOTIFY node is
deliberately left pushed at the end of `demo_render_prepare_publish` so
that `..._end()` charges the interval up to the marker to it — but
`..._end()` also counts `depth != 0` as a fault. That is one fault per
window by construction, not one abandoned window. The arithmetic settles
it: an abandoned window would add a *second* fault at the next `begin()`,
so `faults == windows` exactly proves **zero abandoned windows**. Every
other acceptance check passed. The fault accounting is wrong, not the
measurement; it is recorded here rather than quietly re-gated.

**4. Wrap headroom is 17%, not comfortable.** `max_raw_interval` = 54,192
of 65,535 — and it equals `prepare_mario`'s maximum exactly, so no
interval wrapped on this route. But φ/128 is the coarsest internal FRT
clock the SH-2 offers; a scene ~21% heavier in that one stage would alias
with **no coarser divider available**. Any future measurement of a heavier
scene needs probes *inside* `prepare_mario`, or an FTCSR-overflow-flag
extension. Do not reuse this instrument unchanged on a heavier route
without re-reading `max_raw_interval` first.

**5. This build's window is 16.85 VBlanks/frame; T2.3 reported 18.92.**
They are not the same measurement and must not be differenced. Different
build (diagnostic mode 2, +1,600 B HWRAM, the mode-2 job-telemetry block),
different run shape (26,181 frames including boot and an idle tail, versus
60 presentation events), different sample. The *composition* is what this
task delivers, and it is stable across the run (§5), so the ranking is
robust even though the absolute VBlank figure is not comparable to T2.3's.

**6. Three stages remain opaque.** `prepare_mario`, `spatial_admit` and
`work_order` are reported as single blocks because no probe was placed
inside a loop over scene data — that was the condition for the perturbation
figure of 0.013%. This task establishes **where** 95.7% of the window goes;
it does not establish **why** inside those two stages. T2.5's first job is
to look inside `prepare_mario`, and it should expect to pay some
perturbation to do it.

**7. Route coverage, as always.** BOB entry, ~600 ticks of scripted
movement, and an idle tail. Not owner free-roam, not object interactions
(`dynamic_actor_closure=0`, which is why `actor_closure` reads exactly
zero), not other levels or camera angles. Ymir, not hardware.

## 8. What T2.5 should attack, and why

Grounded only in the table above.

**Primary: `demo_prepare_mario()` — 69.2% of the window, 11.67
VBlanks/frame, ~21% of the whole frame.** It is the largest single measured
block in the port. Its body is Mario actor **meshlet preparation**
(`sm64_saturn_actor_meshlets_prepare()` plus the draw/texture/command
counting that follows). Two independent avenues, both to be *measured*
before either is committed to:

- **Reduce it.** Sub-probe the stage first — the enumeration inside
  `demo_prepare_mario` (meshlet snapshot assembly, the meshlets_prepare
  call itself, the two-pass opaque/translucent primitive walk) has never
  been separated, and 69% of a window is not a thing to guess at. The
  obvious hypothesis worth testing is reuse: the meshlet set is derived
  from the pose and the view, so frames where neither changed materially
  may not need to rebuild it.
- **Move it.** It depends only on the scene snapshot, the pose and the
  camera — all available before the job graph is published — so it is
  structurally eligible to become a slave job, or to run after an earlier
  notification. This is also the only credible route to the split problem:
  the slave is already ~100% busy inside its 3.1-VBlank window, so the
  only slack to win is work the master currently refuses to share.

**Secondary: `demo_spatial_admit()` — 26.4%, 4.45 VBlanks/frame.** The
Z-Treme-derived BSP frustum admission walk. Same discipline; second in
line only because it is 2.6x smaller.

**Explicitly not next:**

- **Any ±1 master/slave rebalance of the existing job split (L12's
  controller).** Measured: the slave is busy 1.02× its own window. There is
  no idle-slave slack inside the overlap to reclaim. Revisit only after
  work moves into the graph.
- **Ranks 3–15.** Together they are 4.3% of the window, and ranks 6–15 are
  0.15 VBlanks/frame in total. Removing all ten completely would be
  unmeasurable at the cadence rig's resolution.
- **Further memory-tier work** (T2.2 disproved it) and **further painter
  ordering work** (T2.3 measured its remaining upside at a fraction of
  1.7%).

**Instrument debt to clear first:** items 2 and 3 in §7 —
`__uncached` the profiler state, drop or re-derive the two cross-CPU
fields, and fix the fault accounting so the acceptance gate means what it
says.

## 9. Reproduction

```
# Diagnostic build: the 27-variable product invocation from
# sprint1-stage1-link-smoke.md with SATURN_DIAGNOSTIC_MODE=2, and
# tools/saturn/profiles/sourceboot-bob-demo-v1.json's
# release_config.diagnostic_mode temporarily 0 -> 2 (canonical LF JSON,
# uncommitted, reverted after the build), via
# tools/saturn/with-msys-toolchain.ps1 -> MSYS sh --noprofile --norc -l,
# source ../../.yaul.env, unset COMPILER_PATH,
# make -f Makefile.saturn.mk -j1 sourceboot <27 vars>

make -f Makefile.saturn.mk verify-memory-map
make -f Makefile.saturn.mk verify-vdp1-painter-chain verify-audio-loop-contracts
make -f Makefile.saturn.mk verify-pcm68k-model verify-terrain-depth-bins
make -f Makefile.saturn.mk verify-vdp1-frame-bank verify-demo-render-overlap
make -f Makefile.saturn.mk verify-render-overlap-integration
python tools/saturn/test_dual_sh2_work_storage_contract.py
python tools/saturn/test_vdp1_staging_relocation.py

python tools/saturn/capture_prenotification_profile.py \
  --ymir <abs>/ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe \
  --ipl  "<abs>/sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin" \
  --game <abs>/build/saturn/sourceboot/e2-bob-identity-id-5b28a329c1e8f9de/sm64-saturn-sourceboot-e2.cue \
  --elf  <abs>/build/saturn/sourceboot/e2-bob-identity-id-5b28a329c1e8f9de/obj/sm64-saturn-sourceboot-e2.elf \
  --release-manifest <abs>/.../saturn-release-manifest-v1.json \
  --output docs/saturn/evidence/reports/sprint2-t2_4-prenotification-profile.json
# exits 1 on this build: the `no_profiler_faults` check fails for the
# by-design reason in section 7 item 3.  Every other check passes.
```

Product-cleanliness proof (mode-0, `-g0`, same path compiled twice from
`HEAD:` and working-tree content, objects compared):
`saturn_render_job_runtime.o` and `main.o` byte-identical;
`saturn_demo_render.o` identical in section sizes, symbol table,
disassembly and all section contents except two four-digit `assert`
`__LINE__` literals.
