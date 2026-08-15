# Roadmap

This roadmap is subordinate to
[`docs/saturn/PRODUCT_GOAL.md`](docs/saturn/PRODUCT_GOAL.md). Milestones are
playable product capabilities, not technical subsystems. A milestone advances
only with a newly built, uniquely identified CUE and its live evidence.

## Now — cadence recovery (Sprint 2)

**Deliverable:** the accepted R1 capability set at a materially better frame
rate, without losing music, audio, or visual acceptance.

Measured starting point (candidate `id-86d3880727ed1d10`): ~1.1 FPS sustained,
~53 VBlanks per presented frame, dominated by scene construction (~24.5
VBlanks) and master finalization (~5.8).

Known levers, in evidence order (**updated after T2.2's measured result**):

- ~~Committed HWRAM reduction~~ — **DONE and DISPROVED as the lever.** T2.2
  recovered 67,584 B and returned the entire 54,080 B hot working set to
  32-bit HWRAM; cadence moved 1.071 → 1.068 FPS, i.e. not at all. Banked
  wins: slack 472 B → 13,944 B, and a silent `gGfxPool` overflow corruption
  fixed. Do not spend further effort here on this evidence.
- ~~The painter relink is O(bins x commands) per frame~~ — **DONE, kept, and
  much smaller than estimated.** T2.3 replaced the per-bin rescan with a
  counting sort over intrusive per-bin chains (T2.0 L7's separation of sort
  from link write; 128 B of stack, no side buffer), byte-identical output
  verified against the retained predecessor and an independent model on 16
  cases with three mutation kills. 42,900 -> 2,078 record visits per frame
  (20.6x). Cadence 1.0682 -> 1.0866 FPS (+1.72%), all of it attributable to
  master finalization (5.80 -> 4.83 VBlanks/frame) with every other phase
  counter bit-identical. L8's "115,200 steps" assumed ~1,800 live commands;
  T2.1 measured 653, so the stage was only ~4% of construction.
- ~~The unmeasured pre-notification window~~ — **MEASURED, twice, and the
  bottleneck is now named.** T2.4 built T2.0 **L14**'s FRT sub-stage
  profiler and decomposed the window into two stages: `demo_prepare_mario()`
  69.2% and `demo_spatial_admit()` 26.4%, 95.7% together, unattributed
  remainder 0.048%. T2.5 then sub-probed the first and found **97.8% of it
  is `actor_meshlet_live_depth_bounds()`** — 5,245,905 cycles/frame, 11.43
  VBlanks, **20.7% of the entire frame** — because
  `actor_saturating_mul_i64()` checks overflow by *dividing*, emitting
  ~14,080 libgcc `___divdi3` calls per frame, and because the whole walk
  runs twice per frame. T2.0 **L12** is also answered: master spin on the
  slave is zero by construction and the slave is busy 1.02x its own overlap
  window, so there is no idle-slave slack to rebalance.
- **T2.6 is the active lever: fix the depth-bounds walk.** (1) Carry pass
  1's bounds and span into pass 2 — **5.72 VBlanks/frame (10.4% of the
  frame)**, bit-identical by construction, one 620–868 B static array.
  (2) Replace the loop's saturating `int64` arithmetic with per-actor
  algebra (`depth(v) = dot(pos−cam, fwd) + dot(S⊙v, R_yawᵀ·fwd)`) —
  **~11.2 VBlanks combined, ~20% of the frame**; risk is numeric, since
  `depth_bounds` feeds `actor_lod_tier()` and `actor_depth_bin()`, so it
  needs an equivalence oracle committed before the swap (T2.3's pattern)
  and owner sign-off on Mario's appearance. Then re-measure:
  `demo_spatial_admit()` becomes the largest block.
- **Mesh reduction is NOT the lever, with numbers.** Halving Mario's mesh
  leaves the stage at 5.72 VBlanks/frame; fixing the arithmetic at *full*
  424-vertex detail leaves ~0.2. Poly count is a linear factor on a constant
  that is ~25–37x too large. Revisit only for VDP1 fill rate or the
  post-notification emit stage, neither of which has been profiled.
- Not the next step: T2.0 **L10**'s coarser per-BSP-leaf ordering unit. It
  would attack a stage that now costs 2,078 record visits per frame.
- Mario dominates the command stream (638 of 882 visible items; 50 source
  triangles expanding to ~200 VDP1 commands).
- Structural, deferred: SlaveDriver keeps both VDP1 command banks in VRAM
  behind a 10,240 B staging window (T2.0 L3). Adopting it would return
  ~96,256 B of our 106,496 B staging — enough to rehome `_sourceboot_fast3d`
  — but it changes the transport contract and needs its own CUE and gate.

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
