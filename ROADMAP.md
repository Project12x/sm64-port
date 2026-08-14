# Roadmap

This roadmap is subordinate to
[`docs/saturn/PRODUCT_GOAL.md`](docs/saturn/PRODUCT_GOAL.md). Milestones are
playable product capabilities, not technical subsystems. A milestone advances
only with a newly built, uniquely identified CUE and its live evidence.

## Now — recover the product feedback loop

**Deliverable:** a trustworthy comparison between the immutable A9A baseline
and one current candidate.

- Preserve and hash-check the accepted A9A artifact.
- Preserve the current dirty tree as a donor; do not reset unrelated work.
- Record the exact current CUE, source state, profile, build time, and hashes.
- Capture after gameplay renders, not at an arbitrary early frame.
- Compare Mario scale/animation/materials/Gouraud/occlusion, controls, camera,
  terrain, Bob-omb, audio output, failures, and FPS.
- Select one causal defect and one donor path. Build and boot immediately.

**Stop rule:** two failed causal attempts or two hours without a new live
observation forces rollback, bypass, or a smaller transplant.

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
