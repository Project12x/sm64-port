# State

**Authoritative goal:** [`docs/saturn/PRODUCT_GOAL.md`](docs/saturn/PRODUCT_GOAL.md)
**Active branch:** `saturn/recovery` (worktree `.worktrees/saturn-recovery`)
**Active plan:**
[`docs/superpowers/plans/2026-08-14-saturn-shaped-port-program.md`](docs/superpowers/plans/2026-08-14-saturn-shaped-port-program.md)
**Status:** **W0 COMPLETE and owner-accepted 2026-08-17** on
`id-e8720d58595d9a62` (manifest
`fd9e1ffbf2b2e23e2f14706a6173b1e72cf207f36b4328c02636cb109cc01990`, CUE
`cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`, ELF
`021f5fee2e0d5a24a1077bc7c41728453e9dc6fc4f4d1cc4c595bdf108b96c50`).
Sprint 1 (R0+R1) was historically complete and owner-accepted 2026-08-15.
**Sprint 2 (cadence recovery) — investigation phase COMPLETE 2026-08-15.**

**Historical T2.17 measured cadence: 6.7181 FPS / 8.9310 VBlanks per frame** on
`id-c0352f297034f653`, against A9A's 5.294 FPS / 11.333 VB. The current
owner-accepted W0 candidate is `id-e8720d58595d9a62`, whose identity-bound
normal observation measured 6.6923 FPS mean and 6.0 FPS 1% low.
Median 6.6667, 1% low 6.0. Source:
`docs/saturn/evidence/reports/sprint2-t2_17-epoch-stall.md`, 29 intervals,
`summarize_cadence`. **+25.5% over T2.13 in one scheduler change** -- the
per-field epoch stall collapsed onto the two gates that carry a hardware
guarantee. This T2.17 paragraph is historical headless evidence; the W0
candidate was subsequently desktop-observed and owner-accepted.
**The T2.11 concurrency rail is now needed on 28 of 29 intervals (0 of 29 at
T2.13), so the phase decomposition no longer closes and must not be quoted;
the cadence figures are independent of it and stand.**
Predecessors on the same basis: `id-a61d5203793986e7` 5.3538 / 11.2069
(T2.13, the first build past A9A on both numbers),
`id-05046d9d5d8a5593` 4.9432 / 12.1379 (T2.12), `id-b46f60d0a6d129dd`
4.3176 / 13.8966 (T2.10+T2.11, owner-accepted), `id-6eca5970628d581d`
3.8753 / 15.4828.

> **Build identity moved at T2.22 — the sealed tag for a given tuple is not
> comparable across it.** T2.22 made eight asset generators write-if-changed,
> which removed the four-links-per-build loop (`-j12` 694.9 s → 417.0 s, and a
> no-op rebuild now does nothing at all). Generator scripts and makefiles are
> source-closure inputs and the closure hash is compiled into the ELF, so the
> tuple that sealed `id-0fade22f26a95c0c` now seals `id-bed197e0c5e928d3`.
> Product bytes did not change: 279 of 280 objects, `SOURCE.DAT` and the CUE
> are byte-identical, and the ELF differs only inside the identity blob. Any
> cadence figure above was measured on a pre-T2.22 tag; re-running the same
> tuple today produces a new tag for the same product.
> `docs/saturn/evidence/reports/sprint2-t2_22-build-relink-loop.md`.

> **Retired figures — do not cite.** The 1.0682 (T2.2), 1.0866 (T2.3) and
> 1.4634 (T2.6) FPS numbers were hand-computed from capture *failure
> diagnostics* over `vblanks_advanced`, which includes the ~1,549-VBlank
> pre-gameplay ramp. A9A's 5.294 always came from `summarize_cadence` and
> was never contaminated. All cadence figures must come from the summarizer.

What the sprint established, in order:

- **T2.1** — every capacity-shrink gate measured safe (VDP1 peak 653 of 2048).
- **T2.2** — 67,584 B of HWRAM reclaimed and the full 54,080 B hot working set
  returned to 32-bit memory: **no cadence change.** The memory-tier hypothesis
  is disproved as the lever. Banked anyway: true slack 472 B -> 13,944 B, and a
  real latent `gGfxPool` overflow corruption fixed en route.
- **T2.3** — painter counting sort, 20.6x fewer steps, but only ~+1.7%: the
  reference sweep's step estimate assumed ~1,800 live commands where T2.1 had
  measured 653.
- **T2.4/T2.5/T2.6** — `demo_prepare_mario()` was 21% of the frame because
  `actor_saturating_mul_i64()` **checked overflow by dividing**
  (~14,080 libgcc `___divdi3` calls/frame) and the meshlet walk ran twice.
  Both fixed; that stage fell **95%** (3,725.8 -> 102.9 cycles/visit) with
  bit-identical output across 685,456 equivalence cases.
- **T2.7** — the contamination above; the real gap is one module.
- **T2.8** — **VDP1 measured idle: 0 waits across 1,349 fence events**,
  `vdp1_sync()` proven non-blocking, ~552 commands/frame against a 1,664
  capacity. **We are CPU-bound.** Every fill-rate lever (user clipping,
  command-count LOD, HSS, the Mario double-emit) is demoted as a cadence lever.
- **Arithmetic census** — divides essentially fixed, 64-bit healthy, but
  **1,827 hot-reachable soft-float sites survive, 150 of them soft-double**,
  including double-precision `sinf`/`cosf` on the per-frame matrix
  path. Also: the in-tree native-math verifier now fails (1,402 helpers vs a
  pinned 582) — number established, cause not.
- **T2.9** — **`spatial_admit` does not need to cost what it costs.**
  `SM64_SATURN_BOB_ADMISSION_NODE_COUNT` is **1**: the spatial index is a
  single node holding all 867 cluster refs, a flat list with a tree's type
  signature, so no early-out is possible and cost never falls with visibility
  (`clusters_tested` = 867, min = max, every sample). 71.8% of it is
  literally constant and 27.7% (a dedup scan that has **never found a
  duplicate**, K(K-1)/2 = 39,903 compares/frame) gets *worse* the more is
  visible. Defensible cost 0.5-0.8 VB; **recoverable ~3.8-4.0 VB = 24-26% of
  the frame, all generically.**

- **T2.10** — T2.9's three generic fixes landed. `demo_spatial_admit()` fell
  **40.5%** (16,588.1 -> 9,871.8 FRT ticks) with **zero** divergences across
  516,090 classifier cases and 1,024 admission poses. Item 3
  (cross-multiplied divides) is bit-identical but a **measured regression**,
  +0.075 VB. No FPS could be reported: the summarizer aborted twice.
- **T2.11** — **the summarizer is repaired and T2.10 is confirmed at the frame
  level: 4.3176 FPS / 13.8966 VB/frame, −1.586 VB and +11.4% against
  `id-6eca5970628d581d`**, with the whole saving landing in `construction`
  where the three fixes were made. Root cause of the abort: `construction`
  subtracts a slave-work window whose opening boundary `retirement_vblank` is
  stamped **on the slave SH-2**, while the master is still inside the source
  tick the frame pipeline admitted — so those crossings were charged to both
  `construction` and `simulation`. The guard now subtracts
  `min(simulation, master_finalization)`, an upper bound on that double count
  read from the same trace, and is unchanged (zero allowance) for v1 traces.
  Control reproduces T2.7 to every digit.
- **T2.14 (diagnostic)** — **shadows are computed and discarded, and so is
  everything else the source display list contains.** Under `SATURN_DEMO_PATH=1`
  (the shipped profile) display submission is suppressed around the whole of
  `game_loop_one_iteration()`, so `src/game/game_init.c:464` never calls
  `exec_display_list`. What reaches the screen is built entirely by
  `saturn_demo_render.c`, which contains **zero occurrences of `shadow`**.
  Three runtime witnesses over one 50 s product capture: `submitted_tasks`
  **0.000/frame** (it increments on every entry, so zero means never entered),
  `command_count` **0.000/frame**, `scene_graph_walks` **1.000/frame** -- the geo
  walk runs, so the shadow really is built, and then thrown away.
- **T2.14 corrected a premise this project had been carrying**: `sourceboot/main.c:2153`
  is the `#else` arm. The live line is **`main.c:2151` -- `sm64_saturn_source_runtime_configure(NULL, NULL)`**. The interpreted F3D
  frontend is linked but **not wired on this route**, deliberately (submitting
  both doubled the render work). Every per-triangle cull stage in
  `saturn_fast3d_frontend.c` -- backface, offscreen, span, near/far -- therefore
  **does not run**. All real culling is upstream and uncounted: `scene_admit`
  2.241%, `actor_meshlet_live_depth_bounds` 1.943%, arena eviction.
- **T2.14: gating shadows is worth +0.01 to +0.03 FPS (0.2-0.5%)** against 5.3538 --
  **below the run-to-run spread**, and emphatically not the several FPS hoped for.
  T2.13 had already banked the expensive part (+8.31%); this is the remainder.
  Each of the top five non-idle symbols is 9-13x the whole shadow path.
  **But the floor-query lead was right and is where nearly all the remainder
  lives:** Mario's shadow issues **11 `find_floor` calls/frame** (1 + 1 + 9
  per-vertex) against a measured 43.471 total -- **>=25.3% of all floor collision
  work** -- while its construction is 0.03%. Two of the 11 duplicate a query
  `mario.c:1322` already made, and `gMarioState->floorHeight` is free.
  **A sprite reading it needs ZERO floor queries: it captures 100% of what
  deletion would, and keeps the depth cue. There is no performance argument for
  deleting over replacing.** SlaveDriver emits a single `COMPO_SHADOW` sprite
  immediately before its own character -- the ordering answer this renderer needs,
  since it has **no depth bias anywhere**; Z-Treme uses a fixed-scale billboard
  with a deliberately cheap floor query.
- **T2.14's real find, worth its own task: the geo walk builds a display list
  nobody reads.** The walk must stay -- it owns animation, camera and matrix
  state -- but the display-list *construction* inside it is pure waste at roughly
  **10x the entire shadow path**. `SATURN_EXPERIMENTAL_SKIP_GEO_WALK=1` already
  exists to bound that prize in a single build.
- **T2.14 also explained why nobody had ever read the route counters**: they are
  unconditional in the product build, but their *publisher* sits behind
  `#if ... && !SATURN_SOURCEBOOT_LIVE_INPUT` and the profile sets
  `live_input_mode: 1`, so `sourceboot_route_checkpoint` is not in the ELF symbol
  table at all. New tool: `tools/saturn/capture_route_counters.py`.
- **T2.13** — **the soft-float purge, measured before it was cut: 5.3538 FPS /
  11.2069 VB, +8.31% and −0.9310 VB** against `id-05046d9d5d8a5593`, and the
  first build past A9A on both numbers. The measurement came first and
  **overturned the census's target**: `guRotateF` never executes on this route,
  and every sampled soft-double call traces to `calculate_vertex_xyz` in
  `src/game/shadow.c`, which spent five double-precision polynomial evaluations
  per shadow vertex recovering an angle `atan2s` had already produced exactly.
  It now indexes the engine's own `sins`/`coss` table. **Not bit-exact and
  owner-visible** — worst shadow-vertex movement 0.147 world units, under a
  tenth of a pixel, bounded by `verify-shadow-trig` (measured 1.486145e-3
  against a derived 1.4871e-3) and cannot accumulate. Two bit-exact wins landed
  beside it (held-object translation, HUD digit ladder) and the report says
  plainly that both are worth approximately nothing dynamically. **Static call
  sites misrank dynamic cost badly**: the census's #1 float caller at 110 sites
  is near the bottom, and six of its top nine targets never executed once in
  240,000 samples. New tooling: `capture_softfloat_profile.py` cycle-attributes
  the running build through `exec.stepi` with no rebuild. **The finding that
  outranks the plan: 70.21% of sampled SH-2 cycles are idle**, so the largest
  remaining lever looks like the master/slave handoff, not arithmetic.
  **T2.16 retires that 70.21% and the 73.33% that followed it** -- both are
  pooled, raster-phase-locked figures; the measured pair is master 19.54% /
  slave 79.15%, combined 49.34%. T2.13's *relative* non-idle ranking stands;
  its absolute shares of the frame are understated roughly twofold.
- **T2.12** — **the spatial index is a real tree at last: 255 nodes over the
  867 BOB clusters, with the INSIDE short-circuit. 4.9432 FPS / 12.1379 VB,
  −1.7587 VB and +14.5%** against `id-b46f60d0a6d129dd`; −1.2414 VB of that
  lands in `construction`, where `demo_spatial_admit()` lives, meeting T2.9's
  1.0-1.2 VB estimate at its upper end. `clusters_tested` finally **varies with
  the view — 0 to 548** against a flat pass that read 867 every frame without
  exception; total tests fall to 31.6% of flat. Both halves fit: the generator
  owned the whole admission section in one function (+95/−20 lines of Python)
  and the node array lives in cart `.cart_rodata` (9,180 B, no work RAM). The
  1,183-node BSP was correctly **rejected** as the data source — it indexes primitive
  spans, not cluster refs. Admitted output is **byte-identical**: 48,200
  comparisons over the real cluster bank, 0 divergences, new gate `verify-admission-hierarchy`.
  Two hazards the brief did not anticipate, both found by failing first: AABB
  quantisation makes OUTSIDE pruning **non-monotone** (fixed with a derived
  8-unit margin on node tests; 102 poses caught), and output order had to stop
  depending on traversal shape (96 mandatory clusters/frame sit in pruned
  subtrees). Mutations: OUTSIDE-inverted, INTERSECTS-dropped and margin-removed
  all KILLED; both INSIDE short-circuit removals SURVIVE **by construction** — a
  correct short-circuit is unobservable in output, which is why the prune and
  descent mutations are the real guards.

- **T2.16 (measurement)** -- **the idle is attributed, and the 73.33% figure it
  was attributed from is an artifact.** A contiguous 19.6405-VBlank
  (1.7525-frame) trace with no gap and no phase selection measures **master
  idle 19.544% = 2.1903 VB/frame** and **slave idle 79.145% = 8.8697
  VB/frame**, i.e. **combined 49.34%, not 73.33%**. Root cause of the old
  figure: `capture_softfloat_profile.py` bursts always begin where
  `Saturn::RunFrameImpl()` stops -- the instant the vertical phase enters
  `BlankingAndSync` -- which is exactly where the master's spin lives, and a
  spin instruction costs 8.33 cycles against 1.2 for cached work, so a
  cycle-weighted phase-locked sampler amplifies it ~7x. **The slave figure
  survives; the master figure does not.**
  - **The master's entire idle is one site, one caller, one cause:** 24 of 24
    PR-attributed entries return to `main.c:2013`, the
    `SM64_SATURN_FRAME_WAIT_VBLANK` arm. **Zero** from `game_init.c:472`
    (`display_suppressed`). The cause is the frame pipeline's per-field epoch
    rule (`saturn_frame_pipeline.c:147,149,180,206`), which forces
    `WAIT_VBLANK` whenever the one remaining action was already taken this
    field. **The master burns 2.19 VB/frame to interleave 0.14 VB/frame of
    transport and publication work -- a 1:14 ratio**, in one contiguous block
    that repeats identically in consecutive frames.
  - **The "blanking-gated VRAM/CRAM transfer" hypothesis is refuted.** The wait
    contains no transfer, and no transfer path in the port contains a blanking
    gate; `saturn_dma_queue_kick/poll` have exactly one caller, which the
    scheduler permits once per field.
  - **The four fork-joins are dead code.** `_sm64_saturn_dual_worker_run` is
    **absent from the product ELF** -- its three call sites live in two static
    functions nothing calls, so `--gc-sections` removes it. Measured barrier
    cost: 0. Any plan premised on widening those windows is planning against
    code that is not linked.
  - **The joint contingency table has an empty cell:** the master *never*
    spins while the slave works (0.00%). Master work + master idle = 11.2069
    exactly -- **the master is the critical path with no residue.**
  - **VDP1 is not starved and is the next wall.** Fresh witness: `EDSR.CEF` set
    in 12.0% of samples -> **VDP1 plots 88% of the frame (~9.86 VB)**, `COPR`
    sweeping the full list. "552 of 1,664" is *static array occupancy*
    (`main.c:767,806`), not load. **T2.8's demotion of the fill-rate levers
    expires the moment the scheduler stall is released**, since master work is
    9.0166 VB/frame.
  - **Soft-float is 18.485% of the critical path (2.0718 VB/frame)**, not
    7.50% -- that figure pooled in the slave's idle spin. Largest application
    symbol behind the two render-phase bodies:
    `_sm64_saturn_ztreme_frustum_aabb` at **0.649 VB/frame**, the same function
    T2.10 item 3 made 0.075 VB *worse*.
  - **Perfect packing is ~5.68 VB/frame (~10.6 FPS), not 2.99 VB (~20 FPS)**:
    work is 50.66% of two-CPU capacity, not 26.67%.
  - **Measurement caveat that binds every number this project holds** --
    **half of it lifted by T2.26.** Ymir's headless rig defaulted to
    `m_emulateSH2Caches = false` and models no inter-SH-2 bus arbitration.
    T2.26 added `--sh2-cache` to `ymir-headless` (default off) and **measured
    the coherency half for the first time: master-resident work inflates
    +8.11%, slave-resident work +27.30%.** Bus contention is **still**
    unmodelled on either basis (`Bus::GetAccessCycles` is a static per-page
    lookup; `StepMasterSH2Impl` advances the slave by the master's cycle count
    with no arbitration), so slave-offload figures remain upper bounds -- now
    for that one named reason. The master-stall figure is unaffected either
    way, because no data crosses CPUs in it.

- **T2.17 (scheduler)** -- **the epoch stall is collapsed, and it went further
  than predicted.** T2.16 item 1 is **DONE**: **6.7181 FPS / 8.9310 VB**,
  **-2.2759 VB/frame**, slightly more than the 2.1903 VB T2.16 measured (that
  figure was a single 1.75-frame window). The reviewed per-field epoch rule was
  narrowed, not deleted: publication keeps the stamp that bounds it to one
  publication, one plot start and one frame-buffer change per field, and drops
  the service stamp, because service builds into the bank publication just
  retired and touches no VDP1 state; and only the *submitting* poll stays
  epoch-gated, its follow-ups being pure DMA-status reads admitted inside the
  submit field alone. **All eleven pre-existing assertions still pass
  verbatim**; three tests and three mutations were added, and one of the new
  mutations survived the first draft of the suite, which is why the isolating
  test exists. **The predicted VDP1 cap at ~9.86 VB did not bind** -- see
  `sprint2-t2_17-epoch-stall.md` section 6.

- **T2.25 (reference read + two defect fixes)** -- **SlaveDriver's
  dispatch-once schedule was read in full at the pin and deliberately NOT
  ported, because on this build it converts zero VB.** The frame equals the
  master's work exactly (master idle **0.000000** at both route positions,
  `sprint2-t2_20-idle-attribution-tick{30,180}`), so a wider dispatch window
  has no stall to fill; **only moving work off the master shortens the frame.**
  Upstream's adaptive controller (`WALLS.C:2277-2284`) drives its partition
  from the master's join spin count, which is always 0 here, so its fixed point
  is "give the slave everything" -- **where this port already is.** Upstream
  splits exactly one stage (transform/light of an already-determined list) and
  keeps visibility determination and command emission master-only; **we split
  the same stage harder than they do, at 100%.** The two master blocks large
  enough to matter are the two upstream also leaves serial: our admission BFS
  with shared visited/queued/seen state (~0.996 VB) and our sequential VDP1
  command arena (~1.435 VB with its merge sort), **2.43 VB of the 6.11 VB
  window.** An offload-eligibility census in the report ranks the rest; the
  largest genuinely eligible item is the terrain depth-bin sort at ~0.367 VB
  (+4.2% predicted), and it is a redesign, not a port. **Every offload figure
  is an upper bound: Ymir models neither cache coherency nor bus contention.**
  Two defects found and fixed instead, both under where a dispatch change would
  have landed. (a) **A latent cross-SH-2 coherency bug on the dispatch path**
  (`f25206c7`): `sm64_saturn_render_job_graph_propagate_failures()` -- reached
  by the slave from `poll_slave()` and the master from `drain_master()` -- read
  `graph->count`, `graph->queue` and `graph->dependency_mask[]` through the
  **cached** alias while the master publishes them through cache-through,
  because the opening `graph_current()` re-aliases only its own parameter.
  Consequence is bounded to a missed quarantine, but **no emulator run could
  ever have found it** (`m_emulateSH2Caches = false`); it was found by auditing
  our discipline against SlaveDriver's `WALLS.C` purge/alias pair. A new
  `--graph-source` mode of `verify_dual_cpu_coherency.py` now fails on the
  pre-fix source and self-tests five incoherent variants. (b) **Five host gates
  over the cross-SH-2 render-job scheduler had not compiled for 130 commits**
  (`3b6079a1`) -- the same include-path class as `verify-render-clusters`,
  reintroduced by `73851b3d`. Sprint 2 has been reasoning about master/slave
  partitioning with the scheduler's own assertions switched off.
  `docs/saturn/evidence/reports/sprint2-t2_25-slave-dispatch.md`.

**Next, ranked by releasable VB/frame per unit of constraint tax
(T2.16 section 9, as amended by T2.17).** **(1) DONE (T2.17): the per-field
epoch stall.** **(1a) Complete; owner-accepted (W0): bound the overwrite
fence.**
`sourceboot_frame_poll_transfers()` now observes busy once,
defers the exact `READY` bank through the scheduler, and starts no DMA or
re-presentation on busy; the old normal/diagnostic wait and spin are gone.
`verify-frame-pipeline`, `verify-vdp1-frame-bank`,
`verify-vdp1-transfer-pipeline`, and `verify-render-overlap-integration` pass.
The normal target link passed both memory floors and its identity-bound Ymir
observation passed 30 events/29 intervals at 6.6923 mean and 6.0 1% low FPS.
The diagnostic-mode-2 arm now links and passes both floors through the existing
direct Makefile handoff, at a tool/docs-only descendant revision rather than
the frozen normal candidate revision; it is target-equivalent, not
artifact-identical. The busy branch remains `host-proven; target-compiled; live
occurrence unproven`. No diagnostic capture was needed under its stated
condition. Independent review found no critical or important issue, authorized
desktop owner launch, and its two review-evidence findings are closed. The exact
staged normal candidate (`id-e8720d58595d9a62`, manifest
`fd9e1ffbf2b2e23e2f14706a6173b1e72cf207f36b4328c02636cb109cc01990`, CUE
`cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`) launched
through desktop Ymir. The owner stated `I see 6-7 FPS and things look the
same`, `things look and sound good`, and `I tested the controls; camera,
collision, and actors behave normally; there is no tearing, flicker,
partial/duplicate plotting, or permanent freezing. I accept this W0
candidate.` W0 is therefore `complete; owner-accepted`. The busy branch
remains `host-proven; target-compiled; live occurrence unproven`; the known
pre-W0 presentation-boundary literal drift remains a retained limitation.
**(2) Continue the soft-float purge on the master** -- 2.0718 VB/frame, now
correctly priced, master-local, no constraint tax. **(3) VDP1 command
reduction -- promoted, but strictly after (1)**; before (1) it is worth zero
frames. **(4) ~~Widen the slave's 4-job render graph~~ -- CLOSED by T2.25 as
stated.** Window width is not the binding constraint and has not been since
T2.17 removed the master's stall: the frame is identically the master's work,
so overlap converts nothing and only *offload* does. What survives of item 4 is
a single ~0.367 VB candidate (the terrain depth-bin merge as a fifth graph job)
and a heavy-scene safety valve (SlaveDriver's adaptive controller plus
unblocking `drain_master` before `slave_retired`), which is inert on this route
and must not be sold as a cadence change. **The coherency cost is now bounded (T2.26) and it does not block the
offload.** `ymir-headless --sh2-cache` prices it: the frame goes 8.9310 ->
9.6552 VB (6.7181 -> 6.2143 FPS), the 6.11 VB slave-idle window survives at
6.0632 VB, and the differential penalty for moving a unit of work from master
to slave is ~1.18x -- so the ~0.367 VB depth-bin candidate keeps its payoff
(+4.29% -> +4.47%). Bus contention is still unmodelled, so the figure is still
an upper bound, but for one named reason instead of two.
`docs/saturn/evidence/reports/sprint2-t2_26-cache-emulation.md`. **Shadows remain settled direction, not a
cadence item.** **Census items 2 and 5 stay deprioritised -- static phantoms.**
The Mario double-emit and T2.9's items 5-8 remain open. The BOB bypass stays
demoted to diagnostic value only. The geo-walk display-list refactor stays
declined (T2.15: +0.005 FPS).

**Owner gate CLOSED 2026-08-16.** `id-b46f60d0a6d129dd` was look-and-listened
on desktop Ymir: **visuals good, sound working,
owner observed 4-5 FPS by eye.** This closes the gate on `id-6eca5970628d581d`
as well — it is superseded on cadence and bit-identical in output. The
naked-eye reading independently corroborates `summarize_cadence`'s 4.3176 FPS
mean (1% low above 4), which is further confirmation that the T2.7
measurement contamination is behind us: observed and measured cadence now agree.

**Open owner gate: `id-a61d5203793986e7` (T2.13).** 5.3538 FPS / 11.2069 VB,
the first build past A9A on both. **Unlike every candidate before it, this one
is NOT byte-identical in emitted geometry.** T2.13 replaced the shadow path's
double-precision `sinf`/`cosf` with the engine's own trig table, so Mario's
shadow is built from vertex positions that differ by up to 0.147 world units
(under a tenth of a screen pixel at a typical camera distance, and it cannot
accumulate across frames). What to look at: Mario's shadow on sloped ground
while he moves across the slope. Nothing else in the frame is affected; the
other two T2.13 changes are bit-exact and proven so.

`id-05046d9d5d8a5593` (T2.12) is superseded on cadence and was never
look-and-listened.

**T2.13's cadence-rail note:** the T2.11 concurrency allowance, needed on 14
of 29 intervals at T2.12 and flagged there as trending badly, is needed on
**0 of 29** in this build.

**Host-gate brittleness, updated by T2.25.** Five more gates --
`verify-render-job-queue`, `verify-render-callback-context`,
`verify-render-job-bridge`, `verify-render-job-payload-bank`,
`verify-render-job-graph` -- were found dead since `73851b3d` (2026-08-14) with
the **same include-path defect** repaired for `verify-render-clusters` at
`099ce72a`, and are repaired at `3b6079a1`. A seventh instance is reported but
**not** fixed: on MSYS2 driving a native mingw64 `gcc`, every
`$(HOST_CC_ENV) $(HOST_CC)` rule dies with `Cannot create temporary file in
C:\WINDOWS\`; `verify-softfp-bitexact` already carries the local workaround
(`Makefile.saturn.mk:2106-2113`) and it was never generalised. Until it is,
append `"HOST_CC_ENV=env -u GCC_EXEC_PREFIX -u COMPILER_PATH -u LIBRARY_PATH -u
C_INCLUDE_PATH -u CPLUS_INCLUDE_PATH -u CFLAGS -u CPPFLAGS -u LDFLAGS
TMP=D:/tmp TEMP=D:/tmp"` to any host-gate make invocation.

**Two host gates are silently un-runnable in some shells -- the MSYS path class
T2.10 flagged, 22 recipes still to go.** `verify-render-clusters`'s include-path
defect is **repaired** (099ce72a): it passed only `-I src/port/saturn/gfx` while
`1ec76248` gave `ztreme_hot_promotion.c` a `src`-relative include, so the gate
had been dying at the preprocessor -- sitting in `verify-all` and verifying
nothing. Its final step still execs the built binary through Python `subprocess.run`,
which will not resolve a missing `.exe` suffix the way the MSYS shell does.
`verify-softfp-bitexact` hands an MSYS-form `SATURN_REPO_ROOT` to a
native-Windows Python, so its `mkdir` step created a stray `D:\d\Code\...` mirror
tree (6 files, 16 KB) instead of its build directory. Run by hand it **PASSES:
43.3 billion checks, 0 failures** -- and it pins the soft-fp *library* only, so
T2.13's change was outside its scope regardless.

**T2.15 found two more, making four known instances of the same class.**
`verify-source-geo-state-diff` (`Makefile.saturn.mk:623`) hands the MSYS-form
path of its built test to a native-Windows Python `subprocess.run` and dies with
`FileNotFoundError [WinError 2]`; run by hand the binary **passes, rc=0**.
`verify-graph-q16-contract` (`Makefile.saturn.mk:1925-1930`) omits
`build/saturn/sourceboot/generated` from its `-I` list while
`src/port/saturn/runtime/saturn_geo_walk_storage.h:8` includes the generated
`saturn_geo_depth_manifest.h`, so it dies at the preprocessor -- the identical
shape as the repaired `verify-render-clusters` defect, and it has been sitting
in `verify-all` verifying nothing. Neither is a regression: T2.15 modified no
tracked source.

## Cadence ceiling

**The geo walk is the largest lever measured to date, and it is mostly not
removable.** T2.15 built the sealed `SATURN_EXPERIMENTAL_SKIP_GEO_WALK=1`
diagnostic (`id-137ecb7d231a34d6`) and measured **6.3273 FPS mean / 9.4828 VB
per frame** -- **+0.9735 FPS, +18.18%** against 5.3538 / 11.2069. Median 6.6667,
1% low 5.4545. That build **can never ship**: `area.c:400` suppresses the whole
walk plus `render_hud()` and `render_text_labels()`, invalidating animation,
warp, camera, water, moving-texture, carpet and matrix state and the HUD
power-meter snapshot field.

**The display-list construction inside that region -- the thing T2.14 named as
"~10x the shadow path" -- is 0.479% of the ceiling**, 3,695.7 of 771,443 cycles
per frame, worth about **+0.005 FPS**. T2.15 landed no code change on that
basis and retired the estimate. The remaining 99.5% is state the demo renderer
reads; see `docs/saturn/evidence/reports/sprint2-t2_15-geo-walk-display-list.md`
sections 2 and 6 for the full site classification and where to attack instead.

## Product truth

**Historical prior owner-accepted CUE: `id-86d3880727ed1d10`.** ELF
`b8754557…b501`, ISO `d952aea4…521a`, preserved at
`releases/2026-08-15_0705/id-86d3880727ed1d10/`.

**Current owner-accepted W0 CUE: `id-e8720d58595d9a62`.** Manifest
`fd9e1ffbf2b2e23e2f14706a6173b1e72cf207f36b4328c02636cb109cc01990`; CUE
`cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`; ELF
`021f5fee2e0d5a24a1077bc7c41728453e9dc6fc4f4d1cc4c595bdf108b96c50`.

### Historical R1 observations for `id-86d3880727ed1d10`

The following owner-gate observations, cadence, feature tuple, and host-audio
investigation describe the preserved prior R1 artifact `id-86d3880727ed1d10`.
They do not describe the current owner-accepted W0 candidate
`id-e8720d58595d9a62` above.

Owner-observed and accepted for historical R1:

- **Audible looping music**, started by the source game's own `play_music`
  call through the semantic API into the MC68000/SCSP driver — the first game
  audio in this project's history.
- **Visuals accepted** as non-regressed against the A9A oracle (Mario,
  terrain, camera, input).
- Cadence ~1.1-2 FPS: recorded, and explicitly **non-blocking for R1** by
  owner instruction (`0ad5fb31`). It is Sprint 2's first objective.

The A9A slice (5.294 FPS) remains the immutable historical oracle at
`build/saturn/baselines/a9a-2026-08-05/`. The regressions it was contrasted
against were diagnosed as configuration-attributable and are neutralised in
`id-86d3880727ed1d10`'s historical feature tuple.

One historical R1 owner-observed artifact — a periodic piercing noise — was investigated to
mechanism and **dispositioned as a Ymir host-audio underrun, not a port
defect** (Ymir's `ProcessAudioCallback` drains without underrun detection;
this build is a pathological slow producer). Full elimination chain:
[`docs/saturn/evidence/reports/sprint1-r1-owner-gate.md`](docs/saturn/evidence/reports/sprint1-r1-owner-gate.md).

## What Sprint 1 delivered

- The donor worktree's entire unlanded state preserved in git (117 tracked
  modifications + 53 zero-history files), byte-verified.
- `AGENTS.md` product-gate constitution restored.
- MC68000 driver diet: state 2,080 B -> 104 B off a 1,020 B stack — the root
  cause of the `0x0340` audio failure; image 13,520 B -> 5,883 B.
- Music as a hardware-looped SCSP sample (`wav_to_pcm8.py`, packager
  `--music-pcm`, loop-bit validator), plus `render_m64_wav.py` to produce the
  owner's ROM-derived music asset.
- `SEQ_START`/`SEQ_STOP` handlers, music pinned to slot 0, SFX on slots 1-3.
- Audio boot fail-open, hardened for never-zeroed LWRAM.
- Three-way work-storage split after measuring 49,648 B of committed HWRAM
  overflow; memory-map margin is now a build output (`verify-memory-map`).
- Continuous SFX no longer re-keyed every frame.

Host tests, SH-2 compilation, manifest verification, and review verdicts do not
override those observations.

## Accepted rollback artifact

Path: `build/saturn/baselines/a9a-2026-08-05/`

- ELF:
  `1905ec8d42ea00ea2c000b5f53dd88f2079ffda8ceb67bcd5879e8e96acfc2e2`
- ISO:
  `1ccaef4f2a2d379d82879d3e823d84db135fdee1045d69aa8e0a60d150cfaf96`
- CUE:
  `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`

This artifact must not be rebuilt or overwritten.

## Active presentation milestone

Produce one newly built and uniquely identified CUE that:

1. runs normal BOB with correct controllable Mario, correct fixed Gouraud,
   correct occlusion/order, a normally spawned textured Bob-omb at the correct
   height, real music, and a game-triggered SFX;
2. runs a minimum Whomp’s Fortress proof in the same executable using the same
   game loop, renderer, Mario path, scene/package path, and audio backend;
3. never falls below 4.0 mean FPS during integration and reaches at least 6.0
   mean FPS before presentation acceptance; and
4. has no exception, allocation failure, stale generation, or scene-wide stall
   caused by unsupported content.

Both levels are required for the end-of-week artifact. The immediate target is
6–10 FPS; performance claims bind one exact CUE/profile/capture.

## Architecture status

### Proven and retained

- Original SM64 gameplay, level, Mario, object, camera, collision, and animation
  ownership.
- The immutable A9A artifact and its measured cadence.
- SH-2 native-math and cadence corrections with target evidence.
- Cartridge/linker placement and bounded address/DMA primitives with target
  evidence.
- The standalone owner-accepted MC68000/SCSP sound path as an integration donor.

### Probationary

- S64P/S64F/S64B actor packages and material compilers.
- Generic actor queues, meshlet workspaces, texture residency, and scene
  publication.
- Scene-derived audio bundles and semantic event transport.
- Render overlap/publication changes not preserved by a current accepted CUE.

Probationary work is reused only when it advances the next live artifact within
two causal attempts or two hours. Otherwise it is bypassed or selectively
transplanted onto the accepted baseline.

### Not yet proven

- Whomp’s Fortress scene generation/load; no generated WF scene closure exists.
- Retail title/menu/file-select flow; sourceboot currently bypasses it for BOB.
- A normal, audible, visually correct generic actor path.
- Current all-features performance or release readiness.

## Immediate execution boundary

The working tree contains extensive uncommitted product and infrastructure
changes. Preserve it as a donor; do not flatten, reset, or broadly stage it.
Before further behavior work:

1. verify the immutable A9A artifact and capture its visual oracle;
2. record the exact current candidate source/profile/artifact identity;
3. compare current Mario/BOB/audio/FPS against A9A;
4. choose the smallest donor transplant for the first failing product gate; and
5. build and boot after that one change.

No release reproduction, new wire format, all-actor campaign, generalized level
framework, or broad review wave is active.

## Task 1 evidence status — 2026-08-13

The A9A archive hashes were recomputed and exactly match the immutable values in
`PRODUCT_GOAL.md`; it remains the historical accepted rollback oracle. Its
parent Ymir profile configuration is historically pinned and currently matches
SHA-256 `33a155e765dac9bd2871ca725ed7f444d1fbbb95876d43c955c0b688e6931566`.
The worktree-local profile was not used.

Task 1 launched bounded, identity-bound headless observations using explicit
USA-BIOS, game-CUE, and `--dram-cart` arguments with build-agent2 headless
Ymir. The historically pinned parent `Ymir.toml` is comparator evidence only;
the headless client did not consume it. The A9A archive layout cannot satisfy
the diagnostic tool's required `obj/<cue>.elf` layout, so the same-hash original
sibling-layout tuple was used without changing the archive; its boot-trace
diagnostic failed after 1,680 emulated frames. The newest generated tuple
reached exact linked-code and embedded build-identity matches, then failed
cadence decode before any presentation event. Neither headless diagnostic
captured video, so neither establishes a Mario/BOB visual gate or advances
acceptance. Exact reports and hashes are in
`docs/saturn/evidence/reports/current-product-gate.json`.

## Task 2 status — scene-ready donor observed; no acceptance

The current sourcebuild graph still rejects its actor-family package before
SH-2 compilation even when dynamic actors are disabled. That failure blocks a
new current-tree CUE.

An isolated historical donor at `d7b04d61` plus donor-only extractor
compatibility commit `5808cdbf` did build and boot a separate CUE. At 3,420
emulated VBlanks it produced BOB terrain and a Mario draw; the exact CUE/ELF/ISO
and VDP1 command evidence are recorded in
`docs/saturn/evidence/reports/2026-08-13-bob-convergence-handoff.md` and
`docs/saturn/evidence/reports/current-product-gate.json`. The current manual
desktop launch uses that named donor CUE under the parent Ymir profile whose TOML
still hashes to the pinned `33a155e7…931566`.

This is **diagnostic evidence only**. The observed donor frame does not prove
correct Mario scale, animation, face order, occlusion, controls/camera,
normally spawned Bob-omb rendering, audible game audio, or cadence. A source
comparison and target command probe closed the proposed duplicate Mario
color-mode patch: both donor and current lowerers already emit the accepted
per-material Gouraud plus RGB1555 `CC_REPLACE` contract. Do not build a duplicate
renderer-only CUE. The next behavior change must follow either a manual verdict
on the named donor or a distinct target-observed discrepancy.

The four direct sourceboot-audio CUE variants are terminal negative evidence;
their rebuilt sound-active guard remained zero after target observation. The
next audio task is target telemetry for the game-entry-to-driver activation
boundary—not a fifth sourceboot audio variant. Normal Bob-omb, audio,
performance, and Whomp's Fortress remain closed until a current generic BOB
artifact is observed and accepted.

## Documentation authority

The authority order is:

1. current owner instruction;
2. `docs/saturn/PRODUCT_GOAL.md`;
3. this file and `ROADMAP.md`;
4. the single active product plan and ledger;
5. architecture/build documentation; and
6. historical plans, audits, handoffs, and evidence.

Historical documents retain useful measurements and source research but cannot
authorize implementation.
