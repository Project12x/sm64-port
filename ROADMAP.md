# Roadmap

This roadmap is subordinate to
[`docs/saturn/PRODUCT_GOAL.md`](docs/saturn/PRODUCT_GOAL.md). Milestones are
playable product capabilities, not technical subsystems. A milestone advances
only with a newly built, uniquely identified CUE and its live evidence.

## Now — cadence recovery (Sprint 2)

**Deliverable:** the accepted R1 capability set at a materially better frame
rate, without losing music, audio, or visual acceptance.

Measured baseline (candidate `id-05046d9d5d8a5593`, figures from
`summarize_cadence`): **4.9432 FPS / 12.1379 VBlanks per frame**, against
A9A's 5.294 FPS / 11.333 VB — a **1.071x** gap, 0.805 VB/frame, with the two
medians equal at 5.0. The Sprint 2 series, all on the same basis:
3.8753 (`id-6eca5970628d581d`) -> 4.3176 (`id-b46f60d0a6d129dd`,
owner-accepted at 4-5 FPS by eye) -> 4.9432 — **+27.5% across the sprint.**
Construction is ~66% of the frame; VDP1 is measured idle.

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
- **Soft-float purge — THE ACTIVE LEVER.** 1,827 hot-reachable sites, 150 of
  them soft-double, including double-precision `sinf`/`cosf` called
  from `guRotateF` and `calculate_vertex_xyz` on the per-frame
  matrix path — in a port whose founding premise is that this compiler's
  soft-float both miscompiles and crawls. **Correctness as much as speed.** A
  0x1400-entry Q16 trig table already exists in tree, and four
  `saturn_geo_enter_*` functions have fully-Q16 siblings to use as the
  template. Counts are static reachability, so 1,827 is a lower bound.
  Related and unexplained: the in-tree native-math verifier now fails
  (1,402 sim-route helpers against a pinned contract of 582) — the number is
  established, the cause is not.
- **Mario double-emit** — every textured primitive emits **two** VDP1 commands
  with the same sort key: a Gouraud polygon, then a distorted sprite drawn
  over it (`saturn_demo_render.c:3871-3931`). The polygon underneath is
  invisible by construction. Neither reference engine does this. It buys no
  VDP1 time (idle), but it is CPU *construction* work — which is the half that
  costs frames.
- **Command-count LOD** — LOD currently downgrades material but never reduces
  a surviving surface's command count. SlaveDriver halves its tile grid past
  MIPDIST for 4x fewer commands. Generic across levels, so it serves S1/S6.
- **Slave overlap window** — the slave carries ~7% of frame work because it is
  given a ~3-VB window across four blocking fork-joins; SlaveDriver dispatches
  once and joins after simulation. Structural, deferred behind the above.
- **Occlusion culling — absent entirely.** The portal structure exists with
  full counters (`saturn_scene_admission.h:100`) and a 1,183-node BSP exists,
  but neither is active: nothing is rejected for being *behind* something.
  Defensible on open BOB; **not** defensible for the castle interior or any
  indoor course, so this is an S1/S5/S6 prerequisite rather than a cadence
  item today.

**Known RED gate, filed:** none — `verify-render-clusters`'s include-path
defect was repaired. **22 Makefile recipes still carry the MSYS path defect**
T2.10 fixed in three of them.

**Watch item:** T2.11's cadence allowance is spent on **14 of 29** intervals
at T2.12 versus 4 of 29 at T2.10, though still only one VBlank crossing each
inside a 4.66 allowance. Not blocking at 12 VB/frame; it will be at 8.

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
