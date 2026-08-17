# Roadmap

This roadmap is subordinate to
[`docs/saturn/PRODUCT_GOAL.md`](docs/saturn/PRODUCT_GOAL.md). Milestones are
playable product capabilities, not technical subsystems. A milestone advances
only with a newly built, uniquely identified CUE and its live evidence.

## Now — cadence recovery (Sprint 2)

**Deliverable:** the accepted R1 capability set at a materially better frame
rate, without losing music, audio, or visual acceptance.

Measured baseline (candidate `id-a61d5203793986e7`, figures from
`summarize_cadence`): **5.3538 FPS / 11.2069 VBlanks per frame** --
**past A9A on both numbers** (5.294 / 11.333) for the first time in the
project's history. Median 5.4545, 1% low 5.0. The Sprint 2 series, all on the
same basis: 3.8753 (`id-6eca5970628d581d`) -> 4.3176 (`id-b46f60d0a6d129dd`,
owner-accepted at 4-5 FPS by eye) -> 4.9432 (`id-05046d9d5d8a5593`) -> 5.3538 --
**+38.2% across the sprint.**

**The ranking below is substantially superseded by measurement.** T2.13 built a
cycle profiler (`exec.stepi`, 240,000 samples on the shipped ELF, no rebuild)
and T2.14 added the route counters. Together they establish: ~~**73.33% of
sampled SH-2 cycles are idle**~~ -- **RETIRED by T2.16**, which traced 19.6405
VBlanks contiguously and measured **master idle 19.544% / slave idle 79.145%,
combined 49.34%**; the 73.33% was pooled across both CPUs *and* raster-phase-
locked, because `exec.run_for` stops exactly where the master's spin lives.
For the same reason, **all float is 18.485% of the critical path (the master),
not 7.50%**; all integer divide is 0.12% with
`___sdivsi3` never appearing; and **the interpreted F3D frontend is not wired
on this route at all** (`main.c:2151` passes `NULL, NULL` under `SATURN_DEMO_PATH`),
so every per-triangle cull stage in `saturn_fast3d_frontend.c` is dead code here.
**Treat static call-site counts as leads, not as a cost ranking** -- six of the
census's top nine float targets never execute on this route.

Levers, in evidence order (**rewritten after T2.2/T2.8/T2.9 measurements, and
again after T2.12 closed `spatial_admit` out**):

- ~~Committed HWRAM reduction~~ — **DONE and DISPROVED.** T2.2 returned the
  entire 54,080 B hot working set to 32-bit HWRAM with no cadence change.
- ~~Fill rate / VDP work~~ — **DISPROVED as a cadence lever.** T2.8 measured
  the VDP1 draw fence at **0 waits in 1,349 frames**; ~552 commands/frame
  against a 1,664 capacity. User clipping, command-count LOD, HSS and the
  Mario double-emit remain real *fidelity/scale* items but buy no frames now.
- ~~Painter relink~~ — **DONE**, +1.7% (T2.3).
- ~~Mario meshlet arithmetic~~ — **DONE**, that stage -95% (T2.5/T2.6).
- ~~`spatial_admit`~~ — **DONE, all four items (T2.10 + T2.12), and this
  section's own prediction landed.** It forecast ~3.8-4.0 VB for roughly
  5.0-5.5 FPS; measured 4.9432. T2.12's hierarchy alone was -1.7587 VB and
  +14.5%, and `clusters_tested` finally varies with the view (0-548)
  instead of reading all 867 every frame. One miss worth remembering: item 3
  (cross-multiplied divides) came in a **+0.075 VB regression** against a
  -0.55 VB estimate, because T2.9 priced the SH-2 DIVU at serial datasheet
  latency when the divider actually runs concurrently with the pipeline.
  **Revert-vs-keep on those 168 lines is still open.**
- ~~Soft-float purge~~ — **SLICE 1 DONE (T2.13), and the census was wrong about
  where.** `guRotateF` **never executes on this route** -- its callers are
  ingame_menu, paintings and the goddard Mario head. The double-precision trig
  was reached through `calculate_vertex_xyz` in `src/game/shadow.c`, which
  recovered an angle `atan2s` had already produced exactly; it now indexes the
  engine's own `sins`/`coss` table (+8.31%). Soft-float that is *not* double is
  still ~4.6% of sampled cycles, led by `_saturn_geo_enter_object` -- which has a
  fully-Q16 sibling, `_saturn_geo_enter_camera`, beside it as the template.
  **The 1,827 figure is static reachability and misranks dynamic cost; census
  items 2 and 5 are phantoms here.** The native-math verifier's failure is
  diagnosed: a **stale contract, not a regression** -- 582 was pinned against an
  oracle declaring zero indirect edges, which now declares 115; the project had
  already pinned v3/v4 at 700 and excluded the v2 gate from releases on
  2026-08-10.
- ~~The geo walk's display list as a lever~~ — **CLOSED by T2.15: worth
  +0.004 to +0.011 FPS.** T2.14's "~10x the entire shadow path" is **retired as
  a 24-fold over-estimate** — it counted `_saturn_geo_enter_object` (1.7048%)
  and `_saturn_mtxq_refresh_float_mirror` (0.5854%) as list construction, and
  T2.15's site classification shows both are dominated by state the demo
  renderer reads. The genuinely removable set is **0.0737% of cycles**, which is
  **0.41x** the shadow path, not 10x. Not worth the build slot or the blast
  radius (`gDisplayListHead` is written at 443 sites across 13 files).
- **The geo walk ITSELF is the largest lever measured to date — +0.9735 FPS
  (+18.18%) CEILING (T2.15).** `SATURN_EXPERIMENTAL_SKIP_GEO_WALK=1` measures
  **6.3273 FPS / 9.4828 VB** against 5.3538 / 11.2069 on
  `id-137ecb7d231a34d6`. That is a sealed diagnostic and can never ship — it
  invalidates animation, warp, camera, water, moving-texture, carpet and matrix
  state, and (newly documented) the HUD power-meter snapshot field, because
  `area.c:400` gates `render_hud()` and `render_text_labels()` in the same `if`.
  **99.5% of the ceiling is state-owning work.** Attack it by making that state
  cheaper, not by deleting output: `_saturn_geo_enter_object` is 85,486
  cycles/frame over 18.6 objects, `_sm64_saturn_matrix_mul` 40,349 and
  `_saturn_mtxq_refresh_float_mirror` 29,355 (both at 43.5 calls/frame).
  Named starting point, bit-exact and cheap: `obj_is_in_view`
  (`rendering_graph_node.c:1477-1479`) recomputes a frame-constant `halfFov`
  plus its `sins`/`coss` pair **per object** — hoist it to the perspective node.
  Caveat for anyone who captures a large share of this: the T2.11 concurrency
  allowance goes from needed in **0 of 29** intervals to **29 of 29** at the
  ceiling, so the rail must be re-checked.
- ~~Shadows as a cadence item~~ — **CLOSED by T2.14: worth +0.01 to +0.03 FPS,
  below run-to-run spread.** Not several FPS: T2.13 had already banked the
  expensive part. Shadows are now a *fidelity direction*, not a performance one --
  the owner has chosen a generic painted sprite that does not follow light cues.
  **T2.14 shows the sprite beats deletion outright**: Mario's shadow issues 11
  `find_floor` calls/frame (>=25.3% of all floor collision work) while its
  construction is 0.03%, and a sprite reading `gMarioState->floorHeight` needs
  **zero** floor queries — capturing 100% of deletion's saving while keeping the
  platforming depth cue. SlaveDriver's `COMPO_SHADOW` sprite is emitted immediately
  before its character, which is also the ordering answer this renderer needs
  (it has no depth bias anywhere).
- ~~**Master/slave handoff**~~ — **CLOSED BY T2.25, and the lever evaporated when
  T2.17 landed.** T2.16 ranked it 4th on the strength of 8.8697 VB/frame of slave
  idle. But **master idle is now 0.000000**, so frame time equals master work
  exactly — the joint table has only two occupied cells. **A wider dispatch
  window changes *when* the slave runs, not how much the master does; overlap
  converts zero VB when the critical path contains no stall.** The "four blocking
  fork-joins" premise was already dead code (T2.16: `_sm64_saturn_dual_worker_run` is
  absent from the product ELF, measured barrier cost 0).
  **SlaveDriver validates our current configuration rather than improving it.**
  Its adaptive controller drives the partition from the master's join spin count,
  which is identically 0 here, so its rule saturates at "give the slave
  everything" — **its fixed point is what we already do.** Upstream parallelises
  exactly one stage and keeps visibility determination and command emission
  master-only; we split the same stage at 100% rather than `slaveSize`, so we are
  **already more aggressive than the reference**. The blocking dependencies —
  the admission BFS over shared visited/queued/seen state (~0.996 VB) and the
  sequential VDP1 command arena plus shared Gouraud bank (~1.435 VB) — are the
  same two upstream also leaves serial. Largest genuinely offload-eligible item
  is **0.367 VB and it is a redesign**.
- ~~**Frame-pipeline per-field epoch stall — THE ACTIVE LEVER (T2.16).**~~
  **CLOSED by T2.17.** Was 2.1903 VB/frame (19.5% of the frame) at
  `main.c:2013`. Measured after: **6.7181 FPS / 8.9310 VB, -2.2759 VB/frame,
  +25.5%** -- slightly more than the block T2.16 sized, because a little master
  work (previous-frame reuse presentations) left with it. **The predicted
  landing at ~9.86 VB against VDP1 did not bind**; see
  `sprint2-t2_17-epoch-stall.md` section 6. The reviewed epoch invariant was
  narrowed to its two hardware-carrying gates rather than removed, with the full
  bank-ownership argument in section 2 of that report.
- **Bound the VDP1 overwrite fence — live-observed; independent review PASS
  WITH FINDINGS; desktop launched; owner gate pending.**
  `sourceboot_frame_poll_transfers()` now observes busy once and defers the
  exact `READY` bank to a later observed field instead of waiting. The busy
  branch starts no DMA or re-presentation, and profile v4 reports its
  historical wait field as busy deferrals. Normal `id-e8720d58595d9a62` passed
  both memory floors and the identity-bound 30-event Ymir capture. The separate
  diagnostic arm was completed through the existing direct inner-Make handoff
  and passes both floors at a tool/docs-only descendant revision: it is
  target-equivalent, not artifact-identical to the frozen normal candidate.
  The busy branch remains `host-proven; target-compiled; live occurrence
  unproven`; no diagnostic capture condition arose. Independent review found no
  critical or important issue and authorized desktop owner launch; its memory
  accounting and source-contract findings are closed. The exact staged normal
  candidate remained alive after a 20-second desktop Ymir monitor, but launch
  is not an owner visual/audio/gameplay verdict. The known pre-W0
  presentation-boundary literal drift is explicitly not a W0 PASS. Owner
  partial wording, `I see 6-7 FPS and things look the same` and `things look
  and sound good`, supports the >=4 FPS floor, no obvious visual regression,
  and audible output only; the rest of the owner checklist remains pending.
- **VDP1 command reduction — NOW THE ACTIVE LEVER. The stall fix landed (T2.17)
  and VDP1 is the wall.** Re-measured on `id-c0352f297034f653`: `EDSR.CEF` is
  set in **6.33%** of 300 samples, so **VDP1 plots 93.67% of the frame** and
  only ~0.57 VB/frame remains in which it is idle. The full command list still
  sweeps to completion, so it is neither starved nor cut short. T2.8's own list
  — per-command user clipping, command-count LOD, the Mario double-emit — is
  live for the first time with a cadence success criterion. "552 of 1,664" is
  static array occupancy, not load. **Do not quote an absolute plot time**:
  T2.16 read ~9.86 VB and T2.17 reads ~8.37 VB, and the two captures warm up by
  a fixed VBlank count, so a 25% faster build samples a different point on the
  replay route (`sprint2-t2_17-epoch-stall.md` section 6.2). Fixing that
  warm-up to count simulation ticks is a prerequisite for sizing this work.
- **`_actor_meshlet_live_depth_bounds` — 3.662% of sampled cycles: the largest
  single non-idle symbol in the profile, and not floating-point at all.** No
  census item names it; sampling alone found it.
- **Mario double-emit** — every textured primitive emits **two** VDP1 commands
  with the same sort key: a Gouraud polygon, then a distorted sprite drawn
  over it (`saturn_demo_render.c:3871-3931`). The polygon underneath is
  invisible by construction. Neither reference engine does this. It buys no
  VDP1 time (idle), but it is CPU *construction* work — which is the half that
  costs frames.
- **Command-count LOD** — LOD currently downgrades material but never reduces
  a surviving surface's command count. SlaveDriver halves its tile grid past
  MIPDIST for 4x fewer commands. Generic across levels, so it serves S1/S6.
- **Slave overlap window** — ~~the slave carries ~7% of frame work because it
  is given a ~3-VB window across four blocking fork-joins~~ **superseded by
  T2.16**: the fork-joins are not in the shipped ELF, and the slave measures
  **2.3372 VB/frame of work against 8.8697 VB/frame idle** in a single
  once-per-frame window bounded by a 4-job render graph
  (`saturn_demo_render.c:4534-4562`). The SlaveDriver comparison still holds --
  it dispatches once and joins after simulation, with a ±1 load controller --
  but the fix is *more claimable jobs*, not *wider fork-join windows*. Deferred
  behind the scheduler stall and the float purge, and blocked on a way to price
  the cache-coherency cost that Ymir does not model.
- **Occlusion culling — absent entirely.** The portal structure exists with
  full counters (`saturn_scene_admission.h:100`) and a 1,183-node BSP exists,
  but neither is active: nothing is rejected for being *behind* something.
  Defensible on open BOB; **not** defensible for the castle interior or any
  indoor course, so this is an S1/S5/S6 prerequisite rather than a cadence
  item today.

**Host-gate defects, MSYS path class (22 recipes remain):** `verify-render-clusters`'s
include path is repaired (099ce72a) -- it had been dying at the preprocessor
since `1ec76248`, verifying nothing from inside `verify-all` -- but its final
step still execs the built binary via Python `subprocess.run`, which will not
resolve a missing `.exe`. `verify-softfp-bitexact` hands an MSYS-form root to
native Windows Python and created a stray `D:\d\Code\...` tree instead of its build
directory. Run by hand it PASSES: 43.3 billion checks, 0 failures.

**Watch item resolved:** T2.11's cadence allowance was needed on 14 of 29
intervals at T2.12 and is needed on **0 of 29** at T2.13.

**Gate:** owner-observed cadence improvement with music, audio, and visuals
still accepted. The >=4 FPS floor becomes binding again once a cadence
baseline is re-established.

## Milestone 1 — presentation BOB

**Visible result:** normal BOB is a credible SM64 gameplay demonstration.

- Sourceboot runs the normal source game loop and BOB level script.
- Mario has correct scale, animation, textures, fixed Gouraud, occlusion, and
  painter order, with working controls/camera/collision.
- A normally spawned Bob-omb renders recognizable source textures at the
  correct ground height through a shared actor path.
- One real music sequence and one game-triggered SFX are audible through the
  existing MC68000/SCSP path.
- Unsupported objects are skipped individually; terrain/Mario/input/audio keep
  running.
- Mean presentation cadence never falls below 4.0 FPS during integration and
  reaches at least 6.0 FPS before acceptance.

**Not required before this gate:** all 34 BOB drawable selections, a new actor
format, release resealing, exhaustive mutation tests, or generalized levels.

## Milestone 2 — Whomp’s Fortress in the same executable

**Visible result:** the presentation CUE can run a second source level without
a level-specific runtime.

- Select or transition to Whomp’s Fortress without rebuilding.
- Run WF terrain, collision, Mario, camera, input, and the same audio backend.
- Reuse the scene/package/renderer path proven by BOB.
- Permit telemetry-visible per-object omission for unsupported first-pass WF
  content; never stall the complete scene.
- Maintain the 4.0 FPS integration floor and target 6–10 FPS.

BOB and WF together form the approved end-of-week presentation artifact.

## Milestone 3 — retail entry and course loop

**Visible result:** the port behaves like a game rather than a direct-level
demo.

- Restore the retail title, menu, and file-select flow.
- Enter BOB or WF through the source level script.
- Collect a star and return through the source transition path.
- Preserve HUD, music, SFX, camera, controls, and save/progression state.

## Milestone 4 — content expansion

Expand ordinary actors, effects, audio, and levels incrementally. Each addition
uses the same executable and preserves all prior live gates. Add a new format or
architecture only when two real content consumers prove the existing boundary
cannot represent the source semantics within measured Saturn memory and timing
limits.

## Milestone 5 — performance and release

- Profile only the current accepted all-feature artifact.
- Improve the dominant measured cost while preserving visual/audio/gameplay
  gates after every change.
- Validate on physical Saturn with the required DRAM cartridge.
- Run hermetic reproduction, sealing, exhaustive capacity, and release audits
  only after the playable content milestone they protect exists.

## Work explicitly paused

- New actor/package wire-format generations.
- Full BOB actor enumeration before Milestone 1.
- Release identity/reproduction campaigns.
- Broad memory or audit campaigns not blocking the next CUE.
- Parallel Saturn architecture lanes.
- Standalone demonstrations offered as substitutes for the source game.

## Definition of progress

Progress is one of:

- a newly visible correct game behavior;
- a newly audible correct game behavior;
- a second source level using an existing shared path;
- a measured performance improvement on the current accepted artifact; or
- removal of a blocker followed immediately by the live observation it blocked.

Documentation volume, commit count, host-test count, format completeness,
review closure, and release reproducibility are not product progress by
themselves.
