# Roadmap

This roadmap is subordinate to
[`docs/saturn/PRODUCT_GOAL.md`](docs/saturn/PRODUCT_GOAL.md). Milestones are
playable product capabilities, not technical subsystems. A milestone advances
only with a newly built, uniquely identified CUE and its live evidence.

## Now — cadence recovery (Sprint 2)

**Deliverable:** the accepted R1 capability set at a materially better frame
rate, without losing music, audio, or visual acceptance.

Measured starting point (candidate `id-6eca5970628d581d`, figures from
`summarize_cadence`): **3.81 FPS / 15.76 VBlanks per frame**, against
A9A's 5.294 FPS / 11.333 VB — a **1.37x** gap. Construction is ~66% of the
frame; VDP1 is measured idle.

Levers, in evidence order (**rewritten after T2.2/T2.8/T2.9 measurements**):

- ~~Committed HWRAM reduction~~ — **DONE and DISPROVED.** T2.2 returned the
  entire 54,080 B hot working set to 32-bit HWRAM with no cadence change.
- ~~Fill rate / VDP work~~ — **DISPROVED as a cadence lever.** T2.8 measured
  the VDP1 draw fence at **0 waits in 1,349 frames**; ~552 commands/frame
  against a 1,664 capacity. User clipping, command-count LOD, HSS and the
  Mario double-emit remain real *fidelity/scale* items but buy no frames now.
- ~~Painter relink~~ — **DONE**, +1.7% (T2.3).
- ~~Mario meshlet arithmetic~~ — **DONE**, that stage -95% (T2.5/T2.6).
- **`spatial_admit` — THE ACTIVE LEVER.** 4.565 VB/frame = 29% of frame,
  one call, view-independent because the "spatial index" is a single node over
  867 clusters. Generic fixes, ranked: O(1) dedup via the already-allocated
  seen-array (~1.56 VB); memoise `metadata_valid()` on view identity
  (~0.48 VB); cross-multiply instead of 4 hardware divides per test
  (~0.55 VB); restore a real hierarchy with the INSIDE short-circuit
  (~1.0-1.2 VB, and the only one that makes cost fall with visibility).
  **Total ~3.8-4.0 VB => roughly 5.0-5.5 FPS**, no fidelity cost, D6 intact.
- **Soft-float purge** — 1,827 hot-reachable sites, 150 soft-double, including
  double-precision `sinf`/`cosf` on the per-frame matrix path, in a
  port whose premise is that this compiler's soft-float miscompiles and crawls.
  Correctness *and* speed; a 0x1400-entry Q16 trig table already exists.
- **Slave overlap window** — the slave carries ~7% of frame work because it is
  given a ~3-VB window across four blocking fork-joins; SlaveDriver dispatches
  once and joins after simulation. Structural, deferred behind the above.

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
