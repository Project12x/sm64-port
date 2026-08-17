# Sprint 2 Task T2.25 — SlaveDriver's dispatch-once schedule, read and measured against ours

- Date: 2026-08-17. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `5b332a5d`. T2.24 was working host-side mesh
  tooling in the same tree throughout; nothing here touches its files and
  nothing of its uncommitted work was staged.
- Task: port SlaveDriver's dispatch-once slave scheduling at
  `WALLS.C:2246`/`:2273` to convert the frame's largest idle block —
  **slave idle, 79.145% = 8.8697 VB/frame** at T2.16, **6.1095 VB/frame** at
  the representative route position measured here.
- **Verdict: the schedule was not ported, because it cannot convert anything
  on this build, and the reason is a measurement rather than a licence or a
  design objection.** Section 3 gives it in one line: **this port's master
  never waits, so widening the slave's window has nothing to widen.**
  Section 4 gives the offload-eligibility census that says what *would*
  convert and what blocks each item.
- **Two defects were found and fixed instead**, both in the machinery a slave
  dispatch change would have landed on top of. One is a latent cross-SH-2
  cache-coherency bug on the dispatch path that no emulator run can observe;
  the other is five host gates over the render-job scheduler that have not
  compiled for 130 commits.
- Commits: `3b6079a1` (gate restoration), `f25206c7` (coherency fix + new
  gate), plus the docs commit carrying this report and the
  `SLAVEDRIVER_ADAPTATION.md` update.
- Build: `id-49894e8e2d3ea415`, ELF SHA-256
  `0a494205f2cc1e69178e9b757913267b15d2fe5ad974bb63cfdc1a09fa55ed62`.
  **Measured 6.7181 FPS / 8.9310 VB — bit-identical to the T2.17 baseline**,
  which is the predicted and correct result for a cache-alias change on an
  emulator that does not model caches. Section 5.
- Evidence: `sprint2-t2_25-throughput-30events.json` (cadence, this task);
  `sprint2-t2_20-idle-attribution-tick{30,180}-c0352f29.json` (the exact
  census section 4 sizes with, per T2.20 — **not** the burst profiler, which
  T2.20 measured at +/-2.6 pp per symbol and which reported the soft-float
  trend with the wrong sign).

---

## 0. Headline, in the order it changes decisions

**1. The frame equals the master's work, exactly. There is no stall on the
critical path for a wider dispatch window to fill.**
`capture_idle_attribution` measures master idle at **0.000000** at both route
positions (`sprint2-t2_20-idle-attribution-tick30` and `-tick180`), and the
joint contingency table has only two occupied cells: `work|idle` 6.1095 VB and
`work|work` 2.8215 VB. Frame time = master work = 8.931 VB. Overlap cannot
shorten a path with no stall in it; **only moving work off the master can.**
Every VB of the slave's idle is a *consequence* of the master's serial stages,
not a cause of the frame length.

**2. SlaveDriver's adaptive controller, ported faithfully, is provably a
no-op here — because our configuration is already its fixed point.**
Upstream drives its partition from the master's join spin count `i`
(`WALLS.C:2277-2284`): `i > 100` shrinks the slave's share, `i < 100` grows it.
On this build `i` is always 0, so the rule saturates at "give the slave
everything", and the slave already has 100% of the partitionable list. Nothing
to port; the port is where the controller would drive us.

**3. Upstream splits exactly one stage, and it is the stage we already split
harder than they do.** SlaveDriver's slave does transform/light for
`slaveSize` of the visible sectors; its master does visibility determination
before the dispatch and *all* command emission after the join. Ours gives the
slave 100% of transform/lower and keeps the same two stages master-only.
**We are strictly more aggressive than the reference already.**

**4. The two master blocks big enough to matter are blocked by dependencies
SlaveDriver did not have to solve — because SlaveDriver keeps both stages
serial too.** Spatial admission is a BFS with shared `visited`/`queued`/`seen`
state (`saturn_scene_admission.c:487-745`), ≈0.996 VB/frame; VDP1 lowering is a
sequential bump-allocating command arena plus a shared Gouraud bank
(`saturn_demo_render.c:4729-4775`), ≈1.435 VB/frame with its merge sort.
Together **2.43 VB of the 6.11 VB window**, and neither is a port — each is a
redesign the brief excludes.

**5. A real cross-SH-2 coherency defect was on the slave dispatch path, and
Ymir can never show it.** `sm64_saturn_render_job_graph_propagate_failures()`
— called by the slave from `poll_slave()` and by the master from
`drain_master()` — read `graph->count`, `graph->queue` and
`graph->dependency_mask[]` through the **cached** alias, while the master
publishes them through cache-through. Found by auditing our discipline against
SlaveDriver's (`WALLS.C:1272-1273`, `:1806-1807`, `:1824-1825`), not by
measurement, and it is exactly the class of bug the brief warns is invisible on
this rig. Fixed in `f25206c7` with a gate that fails on the pre-fix source.

**6. Five host gates over the cross-SH-2 scheduler have not compiled since
`73851b3d` (2026-08-14, 130 commits ago).** `verify-render-job-queue`,
`verify-render-callback-context`, `verify-render-job-bridge`,
`verify-render-job-payload-bank`, `verify-render-job-graph`. Sprint 2 has been
reasoning about master/slave partitioning with the scheduler's own assertions
switched off. Restored in `3b6079a1`.

---

## 1. Upstream, read at the pin

`work/upstream/slavedriver-engine`, `git rev-parse HEAD` =
`a8986591557b6e680550d3c23970284d3b38ff8f`, working tree clean,
`LICENSE.txt` = GPL-3.0-or-later. Direct reuse is authorised for this
component by `UPSTREAM_CODE_LEDGER.md`; the tree is mixed, so any ported code
would have landed in `src/port/saturn/gpl/`. None was, for the reason in
section 3.

Files read in full for this task, all at that commit:

| File | Lines | What it establishes |
| :--- | :--- | :--- |
| `WALLS.C` | `2240-2271` | sizing (`slaveSize`), dispatch (`*0x21000000 = 0xffff`), the master's own share of the draw list, the command-list stitch |
| `WALLS.C` | `2273-2299` | the join (`while (!(*FTCSR & 0x80)) i++`), **the adaptive controller**, the master-only merge, the bounding-box merge |
| `WALLS.C` | `1795-1817` | `slaveDrawStart`, `slaveDraw()`, the slave's cache purge |
| `WALLS.C` | `1822-1835` | `drawSlaveWalls()`, the master's cache purge at the join |
| `WALLS.C` | `1918-1934` | `wallRenderSlaveMain()` — the permanent slave loop and its `*0x21800000` reply |
| `WALLS.C` | `1936-1950` | `startSlave()` — SMPC SSHOFF/SSHON and `SYS_SETSINT(0x94, ...)` |
| `WALLS.C` | `1255-1273`, `1376-1379` | the `+0x20000000` cache-through alias on the streaming result array |
| `WALLS.C` | `1876-1890` | per-sector `JUMP_CALL` command fixups during the merge |
| `SRUINS.C` | `2094-2160` | **what sits between dispatch and join: all of simulation** |
| `V_BLANK.C` | `36`, `130` | `vtimer`, the interrupt-maintained field counter |

The mechanism, stated once: `0x21000000` raises the slave's FRT input-capture
interrupt and `0x21800000` raises the master's; both sides poll `FTCSR` bit 7
(ICF) and clear it by writing 0. One dispatch, one join, per frame.

### 1.1 The two upstream properties that bound everything below

1. **Visibility determination is master-only and serial.** `drawWalls()`
   builds `updateList` by leaf plucking and sorting (`WALLS.C:2180-2239`)
   *before* the dispatch. The slave never participates.
2. **Command emission is master-only.** `slaveDraw()` writes projected/lit
   records into `slaveResult[]`; only the master converts them to EZ_
   commands and stitches its run to the slave's with
   `EZ_linkCommand(lastWallCmd, JUMP_ASSIGN, EZ_getNextCmdNm())`
   (`WALLS.C:2270`).

So upstream parallelises exactly one stage — transform/light over an
already-determined list — and keeps the stage before and the stage after
serial on the master. **That is the same shape this port has, with the slave's
share already at 100% instead of `slaveSize`.**

---

## 2. Coherency — the part worth porting most, audited

### 2.1 What upstream does

Two mechanisms, and it matters which one is for correctness:

- **Whole-cache purge at each boundary.** `slaveDraw()` opens with
  `*CACHECNTRL = 0x10; *CACHECNTRL = 0x01;` (`WALLS.C:1806-1807`);
  `drawSlaveWalls()` opens with the same pair (`WALLS.C:1824-1825`).
  `CACHECNTRL` is CCR at `0xfffffe92`, `0x10` is CP (purge every entry),
  `0x01` re-enables. **This is the correctness mechanism**: the reader drops
  every line before reading what the peer wrote.
- **Cache-through aliasing of the streaming result array.**
  `cacheThruResult = (struct slaveDrawResult *)(((int)slaveResult) + 0x20000000)`
  (`WALLS.C:1272-1273`, `:1377-1378`). The SH-2 cache is write-through, so
  this is not needed for the write to *land*; it is there so a large
  write-only stream does not evict the slave's hot read set. **A performance
  mechanism, not a correctness one.**

### 2.2 What this port does, and why it is finer

The port does not purge. It aliases per object, and it selects the alias by
lane:

| Concern | SlaveDriver | This port |
| :--- | :--- | :--- |
| release record | FRT ICF flag + whole-cache purge | explicit `.uncached` section — `DEMO_CROSS_CPU_SHARED`, `saturn_demo_render.c:204-214`, whose comment records the "Pipe 5" incident where a cached span header dropped the slave's entire record run |
| peer payload read | purge, then ordinary reads | per-object cache-through: `sm64_saturn_render_job_queue_cache_through()`, `graph_cache_through()`, `sm64_saturn_dual_frame_cache_through()`, and `access->cache_through = reader_lane != release->producer_lane` (`saturn_render_callback_context.c:78`) |
| streaming result writes | `+0x20000000` alias | lane-owned arenas with reserve-before-write (`gpl/slavedriver_terrain_result.h`, itself a close-port of `WALLS.C:1240-1408`) |
| slave wake | purge in `slaveDraw()` | libyaul purges once in `_slave_init()` (`cpu_dual.c:122-140`), not per dispatch — correct here because no peer payload is read through a cached alias |
| enforcement | convention | build-time gates (`tools/saturn/verify_dual_cpu_coherency.py`) |

**The divergence is deliberate and is a strict improvement.** A whole-cache
purge costs the reader its entire working set, twice per frame, on a machine
whose frame is already memory-bound (T2.2's LWRAM eviction cost ~4x). Per-object
aliasing pays only for the words that actually cross. `--source` mode already
rejects a whole-cache purge appearing in the accepted transform frame path.

### 2.3 Where the port had lapsed from its own rule

`sm64_saturn_render_job_graph_propagate_failures()` opened with

```c
bool changed = false;
if (!graph_current(graph, generation)) return false;
```

and then dereferenced `graph->count`, `graph->queue` and — through
`predecessor_failed()` — `graph->dependency_mask[]` in its loop.
`graph_current()` re-aliases **its own** parameter; C is call-by-value, so the
caller's pointer stayed cached and every `graph->` in the loop went through P0.
Both SH-2s reach this function: the slave from
`sm64_saturn_render_job_runtime_poll_slave()`, the master from
`..._drain_master()`.

**Severity, honestly bounded.** `graph->generation` is read correctly (through
`graph_current`'s alias), so a stale generation cannot be mistaken for a live
one. The fields actually read stale are `count` — which changes only between 4
(Mario visible) and 2 (Mario culled) — and `dependency_mask[]`, which changes
with it. The consequence is a **missed quarantine of a dependent job after a
producer failure**, not wrong geometry: `propagate_failures` only quarantines.
It is still an incoherent read on the dispatch path, and it is the exact defect
class the brief names.

**Why no capture found it and none ever could.** Ymir sets
`m_emulateSH2Caches = false` (`ymir-core/src/ymir/sys/saturn.cpp:156`) and
models no inter-SH-2 bus arbitration. A cached P0 read and a cache-through P2
read of the same HWRAM are the same access at the same latency
(T2.16 §7.1). Every cadence, profile and idle-attribution figure this project
holds is blind to this class.

---

## 3. Why the dispatch schedule was not ported

### 3.1 The measurement

`capture_idle_attribution.py`, one contiguous window of 6,000,000 master
instructions on the sealed `id-c0352f297034f653` ELF, at both route positions
T2.20 established:

| Quantity | tick 30 (stationary) | **tick 180 (locomotion)** |
| :--- | ---: | ---: |
| traced span | 1.8222 frames | 1.8270 frames |
| **master idle** | **0.000000** | **0.000000** |
| slave idle | 6.0554 VB/frame | **6.1095 VB/frame** |
| joint `work \| idle` | — | **6.1095 VB (68.41%)** |
| joint `work \| work` | — | **2.8215 VB (31.59%)** |
| joint `idle \| *` | — | **absent** |

Two cells, and one of them is empty. **Frame = master work = 8.931 VB,
exactly.** The brief's "6.68 VB overlaps master work" is T2.16's figure on the
pre-T2.17 build; at the representative position it is **6.1095 VB**, and it is
*all* of the slave's idle rather than part of it, because the master never
idles.

### 3.2 The consequence, stated plainly

A wider dispatch window changes *when* the slave runs relative to the master.
It does not change how much work the master does. Since the frame is
identically the master's work, **overlap is worth exactly zero VB here.**

This is the single most important correction this task makes to the sprint's
model. T2.16 §9 item 4 ("widen the slave's job graph", targeting 6.6794 VB) is
correctly ranked fourth but is mis-described as a *packing* problem. It is an
*offload* problem, and packing levers do not address it.

### 3.3 The adaptive controller's fixed point is where we already are

`WALLS.C:2277-2284`:

```c
i = 0;
while (!(*FTCSR & 0x80)) i++;
*FTCSR = 0x0;
if (i > 100 && slaveSize > 0)  slaveSize--;
if (i < 100 && slaveSize < 50) slaveSize++;
```

`i` is the master's join spin count. On this build the master never spins at
the join at all — the slave retires 2.8215 VB into a window the master fills
with simulation and transport — so `i ≡ 0`, the shrink arm never fires, and
`slaveSize` climbs until it clamps at `updateListSize - 1`. **Our render graph
already hands the slave every job in it.** Porting the controller reproduces
the state we are in.

It is not worthless: it is a *heavy-scene safety valve*. If a scene ever makes
the slave's transform work exceed the master's simulation window, the master
would begin waiting at the join and nothing today would rebalance. That is
listed in section 7 as future work rather than smuggled in here as a cadence
change, because on this route it is unobservable and an unobservable change
cannot be validated.

### 3.4 The dependency theirs did not have

The brief asked: if the port is blocked, name the dependency. There are two,
and the honest framing is that **upstream did not solve them either — it left
both stages serial on the master.**

- **Spatial admission is a work-queue BFS with shared mutable state.**
  `sm64_saturn_scene_admit_with_scratch()` (`saturn_scene_admission.c:487-745`)
  walks a node hierarchy through `s_admission_queue[]` with
  `s_admission_visited[]`, `s_admission_queued[]` and
  `s_admission_cluster_seen[]`, then emits in one ordered pass whose order is
  an input to the downstream depth-bin tie-break. Splitting it across
  incoherent caches means sharing three bitmaps and a queue cursor. Upstream's
  equivalent (`drawWalls`'s leaf plucking and sorting, `WALLS.C:2180-2239`) is
  likewise master-only and serial, and upstream splits the *result*, which is
  what we already do.
- **VDP1 lowering is a sequential bump allocator plus a shared Gouraud bank.**
  `demo_render_finalize()` (`saturn_demo_render.c:4729-4775`) walks
  `s_terrain_emit_refs[]` in a stable far-to-near painter order, allocating
  from `backend->commands` and reserving from `gouraud_bank` with dedup,
  then links depth bins. Upstream's `drawSlaveWalls()` is exactly this and is
  exactly as master-only, for exactly this reason. Its own comment in our
  tree already says so: *"The master owns all VDP1 lowering"*.

---

## 4. The offload-eligibility census — what would actually convert

This is the deliverable that replaces the port. Source:
`sprint2-t2_20-idle-attribution-tick180-c0352f29.json`'s
`master_during_slave_idle` block — the **exact, unsampled** cycle census T2.20
established as the correct sizing instrument, *not* the burst profiler.
Converted at `cycles / traced_frames_equivalent / cycles_per_vblank`.

The listed top-20 sums to **4.7303 VB of the 6.1095 VB window (77.4%)**; the
remaining 1.38 VB is the census tail, which the tool does not enumerate. Stage
attribution is by **source reading of the call graph, not by an action-tagged
trace** — labelled as inference, as T2.16 §8.4 labelled its own.

| Master symbol (slave-idle window) | VB/frame | Stage | Verdict |
| :--- | ---: | :--- | :--- |
| `_demo_render_finalize` | 0.9765 | VDP1 emission, post-join | **blocked** — sequential command arena |
| `_sm64_saturn_ztreme_frustum_aabb` | 0.6498 | admission BFS, pre-dispatch | **blocked** — shared visited/queued state |
| `_demo_render_prepare_publish` | 0.3496 | pre-dispatch, self | mixed |
| `_sm64_saturn_matrix_mul.isra.0` | 0.3166 | mixed | mixed |
| `_saturn_geo_enter_object` | 0.2789 | geo-layout walk | **blocked** — SM64 global game state |
| `_memcpy` | 0.2550 | mixed | mixed |
| `_sm64_saturn_terrain_depth_bins_scatter` | 0.1867 | merge sort, post-join | **eligible (redesign)** |
| `_sm64_saturn_terrain_depth_bins_digit.isra.0` | 0.1805 | merge sort, post-join | **eligible (redesign)** |
| `___mulsf3` | 0.1782 | soft float | separate lever (T2.16 item 2) |
| `_support_radius_q16` | 0.1771 | inside `frustum_aabb` | **blocked** — with its caller |
| `_sm64_saturn_scene_admit_with_scratch` | 0.1693 | admission BFS | **blocked** |
| `_saturn_mtxq_refresh_float_mirror` | 0.1469 | pre-dispatch | candidate |
| `_actor_meshlet_live_depth_bounds` | 0.1409 | Mario prep, pre-dispatch | **eligible (redesign)** |
| `_memset` | 0.1353 | mixed | mixed |
| `_sm64_saturn_render_job_queue_job` | 0.1145 | scheduler overhead | inherent |
| `___addsf3` | 0.1095 | soft float | separate lever |
| `___ashrsi3` | 0.1055 | integer shift | inherent |
| `_demo_actor_result_read` | 0.0913 | merge, post-join | with the merge |
| `___subsf3` | 0.0843 | soft float | separate lever |
| `_actor_meshlet_core` | 0.0841 | Mario prep | **eligible (redesign)** |

Rolled up:

| Class | VB/frame | Share of the 6.1095 VB window |
| :--- | ---: | ---: |
| **blocked — sequential VDP1 arena / merge tail** | ~1.435 | 23.5% |
| **blocked — sequential admission BFS** | ~0.996 | 16.3% |
| **blocked — SM64 game state (geo walk, simulation)** | ~0.279+ | 4.6%+ |
| **eligible, but a redesign not a port** | ~0.592 | 9.7% |
| soft float (T2.16 item 2's territory, master-local) | ~0.372 listed | 6.1% |
| mixed / census tail | remainder | ~40% |

**The largest genuinely eligible item is 0.367 VB** — the terrain depth-bin
radix sort inside `demo_terrain_queue_assemble_merge_spans()`, which reads only
the slave's own lane-owned published spans and writes a single-writer output.
Making it a fifth graph job dependent on `WORLD_LOWER` would move it into the
slave's already-open window with no second dispatch. Predicted landing
**8.564 VB ≈ 6.999 FPS, +4.2%** — *before* the coherency tax, which is the
next paragraph and which could plausibly consume all of it.

**Every figure in this section is an upper bound.** Ymir sets
`m_emulateSH2Caches = false` and models no inter-SH-2 bus arbitration
(T2.16 §7.1-7.2). Moving that sort to the slave means either publishing
`s_terrain_emit_refs[]` through the cache-through alias — an ~8x cost per
access on an SH-2 that would otherwise hit cache, on a radix sort that touches
every element three times — or purging the master's whole cache before it
reads them, SlaveDriver-style. **On this emulator both cost zero. On hardware
neither does, and a 0.367 VB block accessed through P2 can easily exceed
0.367 VB.** That is not a hedge; it is the reason this item is ranked below
the soft-float purge, which is master-local and pays no such tax.

---

## 5. Measured cadence

**No cadence change was attempted and none was expected.** The only product
source change in this task is the cache-alias fix in `f25206c7`, which on Ymir
is provably neutral: a P2 access and a P0 access to the same HWRAM cost the
same in this emulator (T2.16 §7.1). It is a hardware-correctness change with no
emulator-observable timing.

It was built and measured anyway, because a product-source change without a
live observation is not evidence. `summarize_cadence` only, 30 presentation
events, 29 intervals, on-target identity **MATCH**
(`observed_sha256 == expected_sha256`, `d88b17a9…23b5`), `status: complete`.

| | Baseline `id-c0352f297034f653` (T2.17) | **T2.25 `id-49894e8e2d3ea415`** | Delta |
| :--- | ---: | ---: | ---: |
| FPS mean | 6.7181 | **6.7181** | **0.0000** |
| FPS median | 6.6667 | **6.6667** | 0.0000 |
| FPS 1% low | 6.0 | **6.0** | 0.0000 |
| VB per frame | 8.9310 | **8.9310** | 0.0000 |
| target VBlank delta over 29 intervals | 259 | **259** | 0 |
| interval distribution | `{7:1, 8:2, 9:24, 10:2}` | **`{7:1, 8:2, 9:24, 10:2}`** | identical |

**Bit-identical on every reported figure**, which is the expected and correct
result: the change moves reads from the P0 alias to the P2 alias of the same
HWRAM, and Ymir prices both the same. It is not evidence that the change is
free on hardware — there it costs a handful of uncached reads per job per
frame, which is the price of removing the incoherent read.

**`presentation_generation_delta == 1` on all 29 intervals.** No generation
skipped, none published twice — the direct scheduler-level check against a
dropped or duplicated field, and the one that matters most for a change on
this path. Queue health identical to baseline: `qn = qr = 30`,
`qw = qf = qq = 0`, `master_failures = slave_failures = 0`, `qm = [0,0,0,0]`,
`qs = [1,1,1,1]`.

**The T2.11 concurrency rail, as the brief requires.** The allowance is needed
on **28 of 29 intervals**, exactly as at T2.17. Per-interval means are
unchanged to four decimals: simulation 4.8276, construction 6.0000,
transport/presentation 0.1034, concurrent-phase allowance 3.8276, attributed
10.9310 against an actual interval of 8.9310. **The phase decomposition still
does not close and is still not quoted as one** — it is reported only to show
this build did not move it.

**Route span and warm-up.** `capture_sourceboot_throughput.py` is
event-counted and T2.20 section 8 explicitly says not to move its default; the
run therefore covers route ticks 0–32, as every accepted cadence baseline in
this sprint does. T2.20's `--warmup-ticks 150` standard was applied to the
*sizing* instrument instead, which is where it belongs and where section 4's
census comes from: `sprint2-t2_20-idle-attribution-tick180` is the tick-180
capture, past the 120-tick stationary block, in steady-state locomotion.
Section 5 of T2.20 established the cadence is flat across the route (8.9727 VB
stationary against 9.0429 running, 0.8% apart), which is what makes the two
instruments comparable at different positions.

**Identity and hashes:**

| Artifact | SHA-256 |
| :--- | :--- |
| ELF | `0a494205f2cc1e69178e9b757913267b15d2fe5ad974bb63cfdc1a09fa55ed62` |
| ISO | `dc986500774538b560f3148cd214e4e272a2a4505e9fe2811ae591718b0eb350` |
| CUE | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` |
| `SOURCE.DAT` | `0a0f5bb3a0329ad3717d4c77c60cfc90dda7865d35ef1da064ed1288bc8c2854` |
| release manifest | `5925c4671c1556ab8961ebb3f0b2e03f98673cd9e4b4c31f72fef13ca9dac59e` |
| on-target identity probe | `d88b17a9fd1c42e57e3a0c76cd790a506b6a7e778f0a37dfc63f1850850d23b5` (**match**) |

Build: the 27-variable product tuple from `sprint1-stage1-link-smoke.md` with
`SATURN_OBJECT_POOL_CAPACITY=208` and `SATURN_DIAGNOSTIC_MODE=0`, `-j12`,
exit 0, no repair needed. All tracked source was committed before the build.
Artifacts preserved to `releases/2026-08-17_t2_25-product/id-49894e8e2d3ea415/`;
the pre-existing build tree was snapshotted to
`releases/2026-08-17_0806_t2_25-preserve/` first, per the build-preservation
rule. The CUE is byte-identical to T2.17's; the ELF, ISO and `SOURCE.DAT` are
not, which is correct — one instruction moved.

**Note the identity tag is not comparable across T2.22** (STATE.md records
this): generator scripts are source-closure inputs, so the same tuple seals a
different tag before and after `f6a35508`. `id-49894e8e2d3ea415` is a
post-T2.22 tag; `id-c0352f297034f653` is a pre-T2.22 tag. The *cadence*
figures are directly comparable — same tuple, same route, same summarizer.

**The emulator-upper-bound caveat, attached as the brief requires:** every
number above and below is from headless Ymir, which models neither SH-2 cache
coherency nor inter-SH-2 bus contention. Any figure quoted for slave offload is
an **upper bound** and will be worse on hardware by an amount this rig cannot
report. The coherency fix in this task moves reads from P0 to P2 on a path both
CPUs execute; on hardware that is a small real cost, paid to remove a real
correctness hazard, and it is invisible here.

---

## 6. Tests, gates and mutation results

### 6.1 The five restored gates

Dead since `73851b3d` (2026-08-14) with
`fatal error: port/saturn/platform/saturn_cart_code.h: No such file or directory`.
That include spelling resolves only against `-I<repo>/src`;
`verify-render-job-runtime` passed that flag and kept working, the five below
did not. Restored in `3b6079a1` by adding the one flag. No test source and no
product source changed.

| Gate | Before | After |
| :--- | :--- | :--- |
| `verify-render-job-queue` | compile error | **RESULT OK** — fixture PASS, coherency source gate OK, mutation gate OK (six unsafe queue variants rejected) |
| `verify-render-callback-context` | compile error | **RESULT OK** |
| `verify-render-job-bridge` | compile error | **RESULT OK** |
| `verify-render-job-payload-bank` | compile error | **RESULT OK** |
| `verify-render-job-graph` | compile error | **RESULT OK** |

### 6.2 The new gate

`tools/saturn/verify_dual_cpu_coherency.py --graph-source`, wired into
`verify-render-job-graph` with `--self-test`. It splits the translation unit
into functions with a line scanner and requires every entry point that
dereferences `graph->` to select the cache-through alias **in its own body** —
delegating to a helper that aliases its own parameter does not count. It also
checks `graph_publish()`'s release order (dependency mask, then fence, then
queue publish) and `graph_claim()`'s producer-completion gate.

`predecessors_done()` and `predecessor_failed()` are whitelisted with the
reason recorded in the source: they receive a pointer their callers have
already aliased, take a `const` parameter, and must not re-alias it away. A
sweep of the four sibling files found no second instance; `span_valid()` in
`saturn_render_payload_bank.c` reads only written-once-at-cold-init fields.

**Run against the pre-fix source it fails with the exact defect:**

```
render-job graph coherency source gate FAILED:
  sm64_saturn_render_job_graph_propagate_failures() reads graph-> without selecting cache-through
```

### 6.3 Mutation matrix

The brief named three mutations. Each is mapped onto the machinery that
actually exists, and each fails.

| Brief's mutation | Realised as | Gate | Result |
| :--- | :--- | :--- | :--- |
| **move the join earlier** | `SM64_SATURN_RENDER_LIFECYCLE_TEST_FINALIZE_BEFORE_RETIREMENT` — finalize before the slave has retired | `verify-demo-render-overlap` | **FAIL** (pre-existing, re-run and confirmed) |
| **drop a purge/cache-through on shared data** | `propagate_failures()` reads the dependency mask through P0 | `verify-render-job-graph` (new) | **FAIL** |
| " | the claim path reads the job count through P0 | " | **FAIL** |
| " | `CPU_CACHE_THROUGH` removed outright | " | **FAIL** |
| " | dependency mask published without a release fence | " | **FAIL** |
| **let two jobs write the same bank** | claim path drops its producer-completion gate (a consumer claims before its producer is DONE and reads an unwritten lane) | `verify-render-job-graph` (new, source) | **FAIL** |
| " | same, as `(predecessors_done(...) || true)` | `verify-render-job-graph` (C fixture) | **FAIL** |
| nominal | — | all | **PASS** |

**One mutation initially survived and that is the useful part of this
section.** Dropping the cache-through from `graph_claim()` was invisible to the
C fixture, because `graph_cache_through()` compiles to the identity function
off `__sh__` — the host build cannot distinguish P0 from P2 at all. That is a
structural blind spot in every host fixture this project has for cross-CPU
aliasing, and it is why the new gate is a **source** gate rather than another
runtime fixture. It is also, precisely, why the defect in §2.3 survived 130
commits of review.

### 6.4 Full gate sweep

Run after both commits, all **RESULT OK**: `verify-render-job-queue`,
`verify-render-callback-context`, `verify-render-job-bridge`,
`verify-render-job-payload-bank`, `verify-render-job-graph`,
`verify-render-job-runtime`, `verify-dual-frame-bank`, `verify-frame-pipeline`
(nominal + 6 mutations), `verify-render-overlap-integration`,
`verify-demo-render-overlap`, `verify-dma-queue`, `verify-memory-map`.

**No existing assertion was weakened, changed in expectation, or removed.**
T2.17's eleven prior `WAIT_VBLANK` assertions and its three added tests all
still hold verbatim; the frame pipeline was not touched.

### 6.5 A seventh brittle-host-gate instance, reported not fixed

On an MSYS2 shell driving a native mingw64 `gcc`, **every** rule using
`$(HOST_CC_ENV) $(HOST_CC)` dies with
`Cannot create temporary file in C:\WINDOWS\: Permission denied`. MSYS2
rewrites `TMPDIR` on the way across and the recipe shell's `TMP`/`TEMP` do not
survive. `verify-softfp-bitexact` already diagnoses this in a comment and works
around it locally (`Makefile.saturn.mk:2106-2113`); the workaround was never
generalised. Every gate result above was obtained with an explicit override:

```
make -f Makefile.saturn.mk <target> \
  "HOST_CC_ENV=env -u GCC_EXEC_PREFIX -u COMPILER_PATH -u LIBRARY_PATH \
   -u C_INCLUDE_PATH -u CPLUS_INCLUDE_PATH -u CFLAGS -u CPPFLAGS -u LDFLAGS \
   TMP=D:/tmp TEMP=D:/tmp"
```

Not fixed here because it is one line in ~60 rules and would collide with a
concurrently-running task's tree. Reported so the next task does not
re-diagnose it.

---

## 7. Ranked next actions

Reordered by this task's measurement. Every size is an estimate and is
labelled as one.

**1. The soft-float purge on the master (T2.16 item 2) is now unambiguously
first.** Master-local, no coherency tax, no bus tax, and the only large block
whose size Ymir reports honestly. Listed float in the slave-idle window alone
is ~0.372 VB, and T2.20's full-master census puts the family at ~2.03 VB/frame
across the whole frame. **It is the only item on this list that a Ymir number
can size correctly.**

**2. VDP1 command reduction.** T2.17 §6.2 measured VDP1 at 93.67% occupancy.
Unchanged in rank by this task, and unaffected by any of its findings.

**3. The terrain depth-bin merge as a fifth graph job. ~0.367 VB, +4.2%
predicted, upper bound.** Section 4. The one genuinely eligible offload, and it
is a redesign rather than a port. **Do not start it without first bounding the
coherency cost** — either on hardware or with a Ymir run that enables
`m_emulateSH2Caches`. A radix sort reading its input through P2 can cost more
than the block it removes, and this rig will report that as a win.

**4. SlaveDriver's adaptive controller as a heavy-scene safety valve.**
Provably inert on this route (§3.3), so it cannot be validated here and must
not be sold as a cadence change. Its value is that if a scene ever pushes the
slave's transform work past the master's simulation window, the master starts
waiting at the join and nothing today rebalances. Pair it with unblocking
`drain_master` before `slave_retired`, which is currently gated
(`saturn_render_lifecycle.c:65-72`). Land it only with a heavy-scene fixture
that makes it fire.

**Not recommended:** any further work premised on "widen the slave's window".
Section 3. Window width is not the binding constraint and has not been since
T2.17 removed the master's stall.

---

## 8. Honesty

- **The task's stated goal was not achieved, and the finding is that it could
  not be.** The owner's brief said this was a direct port rather than a
  redesign. The port is faithful to read and inert to apply: our master has no
  join stall for a wider window to remove, and our slave already holds 100% of
  the one stage upstream partitions. Nothing was invented to manufacture a
  number.
- **No cadence improvement is claimed and none was attempted**, and the
  measurement returned exactly that: 6.7181 / 8.9310, bit-identical to
  baseline on every reported figure. Section 5. A reader should treat that as
  a control, not as evidence that the coherency fix is free on hardware.
- **Every offload figure is an upper bound**, because Ymir models neither cache
  coherency nor bus contention. Stated in §0.4, §4, §5 and §7.3 rather than
  once.
- **Stage attribution in §4 is inference from source reading**, not from an
  action-tagged trace. An instrumented build would tighten it and was not
  built, per AGENTS.md item 10.
- **The census covers 77.4% of the slave-idle window.** The tool enumerates 20
  symbols; the remaining ~1.38 VB is unclassified tail, and a redesign sized
  against the classified part alone would be sized against 77% of the problem.
- **The coherency defect's severity is argued, not measured.** §2.3 reasons
  from what `propagate_failures` does with the fields it reads stale. It cannot
  be measured on this rig by construction, and no hardware run was made.
- **The `+4.2%` in §4 and §7.3 is arithmetic on a census, not a prediction with
  a track record.** This codebase's estimate history says to distrust it:
  T2.10 predicted −0.55 VB and measured +0.075; T2.14 over-estimated by 24x.
- **No emulator GUI was launched and no owner observation was made.** The
  product gate is an owner-observed CUE and this is not one.
  `id-49894e8e2d3ea415` is `source-complete` plus a headless cadence control,
  not `live-observed`. The accepted artifact remains T2.17's
  `id-c0352f297034f653` and was not replaced.
- **The build and capture were run once each.** The emulation is
  deterministic, so a literal repeat would be bit-identical and would measure
  nothing; wall-clock run-to-run variance remains unmeasured, the same gap
  T2.16, T2.17 and T2.20 all recorded.
- **One route, one camera path.** Everything rests on the BOB replay route at
  ticks 30 and 180. §3's structural conclusion (master idle 0) held at both,
  which is two samples, not a distribution.

---

## 9. References

Per the standing owner instruction and the reference-code-first rule.

| Repo | Pinned SHA | Licence | Files inspected | Reuse mode |
| :--- | :--- | :--- | :--- | :--- |
| `work/upstream/slavedriver-engine` | `a8986591557b6e680550d3c23970284d3b38ff8f` | GPL-3.0-or-later (`LICENSE.txt`, verified at the pin) | `WALLS.C:1255-1273`, `:1376-1379`, `:1795-1817`, `:1822-1835`, `:1876-1890`, `:1918-1950`, `:2061-2062`, `:2180-2239`, `:2240-2271`, `:2273-2299`; `SRUINS.C:2094-2160`; `V_BLANK.C:36,130` | **structure and coherency discipline read in full; nothing copied** (§3 says why). Direct GPL reuse *was* authorised for this component. |
| `third_party/libyaul` (vendored) | `6012f79f` | MIT | `scu/bus/cpu/cpu_dual.c:90-140` (`__slave_polling_entry`, `_slave_init`'s single `cpu_cache_purge()`) | dependency, read for semantics |
| `ymir-agent` | working tree | — | `libs/ymir-core/src/ymir/sys/saturn.cpp:150-156` (`m_emulateSH2Caches = false`) | read to establish what the rig cannot model |

**Z-Treme was not opened.** It is single-CPU on both `slSlaveFunc` sites; it
contributes nothing to a dispatch question.
