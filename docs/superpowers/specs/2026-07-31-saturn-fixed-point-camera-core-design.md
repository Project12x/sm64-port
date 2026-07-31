# Saturn Fixed-Point Camera Core Design

**Date:** 2026-07-31

**Status:** Owner-approved design; written-spec review pending

**Initial proving ground:** Bob-omb Battlefield (BOB)

**End goal:** Full-game camera coverage

## Decision

Replace the hot original SM64 camera computation in Saturn candidate builds
with a scene-neutral, bespoke fixed-point camera core. The original camera
remains an oracle in separate baseline/replay builds; it is not executed as a
runtime fallback in candidate or production builds.

BOB is the first qualification environment because it is the current playable,
deterministic test map. It is not the architecture boundary. Production camera
code may not contain BOB-specific coordinates, level identities, route checks,
or behavioral exceptions.

The new camera targets hybrid parity and feel equivalence:

- discrete gameplay semantics remain aligned with the source camera;
- presentation trajectories may differ within measured, reviewable envelopes;
- camera-relative controls, targeting, obstruction response, and transitions
  must remain behaviorally sound; and
- the candidate camera closure must contain no runtime soft-float or libm
  helpers.

## Why This Direction

The original `camera.c` is broad, stateful, and expensive on SH-2. Translating
it function by function retains its complexity, requires a large mirrored Q
shadow, and spends substantial effort preserving implementation details that
are not part of the player's observable experience.

The previous week of work is not discarded. It established the measurement,
transport, and audit machinery required to replace the implementation safely.
The design changes what is treated as authoritative: the original camera's
measured behavior is the reference asset, while its internal float
implementation is not.

## Goals

1. Determine quickly how much source-simulation time is attributable to the
   original camera.
2. Implement a compact fixed-point normal-gameplay camera that is useful beyond
   BOB.
3. Remove original float camera computation completely from candidate builds.
4. Preserve source-owned gameplay inputs and the public camera compatibility
   boundary.
5. Validate semantic parity, feel equivalence, determinism, and performance
   through separate baseline and candidate replays.
6. Create a capability model that can expand incrementally until the full game
   is covered.

## Non-Goals for This Sprint

- Reimplement every cutscene, credits spline, cannon view, boss camera, water
  camera, or rare area-specific mode.
- Require per-tick bit equality of camera position or focus.
- Run the original and candidate cameras together during normal validation.
- Add BOB-authored camera rails or coordinates to production code.
- Port SlaveDriver's sector camera, Sonic Z-Treme's renderer, or Jo Engine's
  SGL camera wrapper.
- Delete the original camera before baseline evidence and later capability
  expansion no longer require it.

## Sprint Structure

### Phase A — Fast Attribution Gate

Phase A answers the performance question before production collision or mode
coverage is built. It uses the same deterministic 2,000-tick BOB replay in
three separate builds:

1. **Source baseline:** the current original float camera.
2. **Camera-bypass diagnostic:** seed a valid camera, then hold it stable while
   skipping normal camera computation. This diagnostic build estimates the
   maximum recoverable camera cost and is never a production option.
3. **Minimal fixed candidate:** a scene-neutral fixed-point follow camera with
   target, yaw, pitch, distance, vertical offset, and shift-based smoothing.

An SH-2 FRT probe records camera-stage ticks independently from whole-simulation
ticks. Every role records:

- camera tick total, average, maximum, and invocation count;
- whole-simulation ticks;
- render submissions, transformed/emitted primitives, rejection counters, and
  other culling-sensitive workload measures;
- camera-reachable helper calls; and
- route, ELF, ISO, and source-data identity.

The attribution calculations are:

```text
maximum opportunity = source baseline - camera bypass
realized saving      = source baseline - minimal fixed candidate
```

The decision bands are:

- less than 5% whole-simulation improvement from bypass: stop the replacement
  sprint because the camera is not a dominant contributor;
- 5–10%: continue only when completion cost or code-size pressure independently
  justifies the replacement;
- more than 10%: proceed with the production core; and
- in every case, candidate camera-stage time must be at least 50% lower than
  the source camera stage to justify owning the replacement.

Phase A compares separate artifacts. It never runs source and candidate camera
updates in one timed build.

### Phase B — Production Core

Phase B begins only after the Phase A decision is recorded. It adds:

- fixed-point obstruction handling;
- normal follow/radial capability transitions;
- direct fixed-state consumption by the Saturn renderer;
- integer-only compatibility publication to public source camera structures;
- deterministic baseline/candidate comparison; and
- zero-helper linked-code enforcement for the candidate camera closure.

Later sprints add capability families until full-game coverage is complete.

## Architecture

### Fixed Camera State

The candidate owns one explicit state record:

```text
SaturnCameraState
  desired_focus
  desired_position
  rendered_focus
  rendered_position
  linear_velocity
  yaw, pitch, roll
  distance, vertical_offset
  active_capability
  transition_state
  obstruction_state
  diagnostics
```

World-space scalar/vector values use a measured fixed-point format selected by
the existing range-evidence process. Angles use the inherited 16-bit binary
angle convention. The design does not assume Q16 or Q12 before captured ranges
and differential tests choose the format.

### Per-Tick Data Flow

1. **Import authoritative inputs.** Read Mario position, face angle, action,
   controller state, area/camera events, and relevant level state. Existing
   IEEE-754 fields are decoded to fixed point through integer bit operations;
   this import performs no soft-float arithmetic.
2. **Select a capability.** Map source events to a supported scene-neutral
   capability. Initial support covers ordinary follow/radial gameplay and
   their normal transitions.
3. **Compute desired framing.** Apply target offsets, capped angle motion,
   distance rules, fixed trig, and shift-based vertical/distance feedback.
4. **Resolve obstruction.** Query source-derived collision geometry through a
   fixed-point interface and adjust the camera to retain line of sight.
5. **Advance presentation.** Smooth the resolved pose into the rendered pose
   with bounded fixed feedback.
6. **Publish once.** Supply fixed position/focus/orientation directly to the
   Saturn renderer. Where inherited code still reads `Camera` or
   `LakituState`, publish compatible IEEE-754 bit patterns through an
   integer-only bridge.

### Capability Model

Camera coverage is organized by behavior, not course:

- ordinary third-person follow;
- radial/orbit input;
- fixed or bounded area framing;
- transition blending;
- obstruction behavior;
- cutscene/spline playback;
- special gameplay views such as cannon, swimming, bosses, and credits.

This sprint implements the first two plus their ordinary transitions and the
shared obstruction system. Unsupported capabilities in candidate builds set a
named diagnostic and enter a safe held/reset pose. They never call the float
camera. Accepted routes require zero unsupported-capability events.

### Camera-Relative Gameplay

The camera yaw affects player input interpretation. Therefore camera-relative
movement direction is a semantic output, not a presentation-only detail. The
candidate must preserve the intended movement quadrant and transition behavior
even when its rendered trajectory differs from the baseline.

### Obstruction Interface

The production core uses a scene-neutral fixed-point query over source-derived
collision data. It must not call the original float camera collision helpers.
The initial interface supports:

- line-of-sight testing from focus to desired camera position;
- nearest blocking wall/terrain result;
- camera distance shortening and bounded vertical correction;
- hysteresis to prevent alternating hit/no-hit oscillation; and
- last-valid-pose recovery for degenerate geometry.

Collision implementation details remain isolated from follow/smoothing logic
so they can be optimized or replaced without changing the camera state
machine.

## Baseline and Candidate Roles

The build roles are intentionally separate:

- **camera-source-baseline:** original SM64 camera, used only to produce
  reference behavior and timing;
- **camera-bypass-diagnostic:** held camera state, used only to estimate the
  maximum opportunity;
- **camera-fixed-candidate:** bespoke fixed-point camera, with no original
  camera update or runtime float fallback.

The immutable `bob-parity-v1` and `bob-default-camera-v1` routes retain their
existing identities. New routes may be added for capability coverage, but an
existing route is never edited to make a candidate pass.

## Acceptance Contract

### Semantic Parity

- Source tick count, Mario action, area, controller inputs, and relevant game
  events match between roles.
- Camera capability transitions occur in the corresponding semantic windows.
- Camera-relative input produces the same intended movement direction.
- Unsupported capabilities, resets, and discontinuities are explicit.
- Candidate execution is deterministic across two same-role captures.

### Feel Equivalence

Exact per-tick camera vectors are not required. Reports instead evaluate:

- Mario's screen-space framing region;
- camera distance and pitch envelopes;
- yaw velocity and response latency;
- settling time after movement or input changes;
- line-of-sight continuity;
- obstruction penetration and recovery;
- oscillation and one-tick discontinuities; and
- recorded visual review at named route checkpoints.

Tolerance values are frozen from baseline distributions and reviewed candidate
captures. They are not tuned from a single final frame.

### Performance and Technical Closure

- Candidate camera-stage time is at least 50% below baseline.
- Whole-simulation improvement is evaluated against the Phase A decision
  bands.
- Render/culling workload is reported beside timing.
- Candidate camera closure contains zero soft-float/libm helper edges.
- Overflow, saturation, divide-fault, unsupported-mode, and invalid-state
  counters are zero on accepted routes.
- Production camera code contains no BOB identifiers or coordinates.

## Error Handling

- Unsupported capability: set a diagnostic, retain a safe pose, and fail the
  acceptance run.
- Fixed-point overflow or saturation: increment a named counter and fail the
  acceptance run.
- Degenerate obstruction query: retain the last valid pose for the tick and
  record the failure.
- Invalid import state: reset from a documented safe seed; repeated resets fail
  the route.
- Missing evidence identity or role mismatch: reject the report before any
  performance comparison.

Candidate builds never hide errors by invoking the source float camera.

## Preservation of Existing Work

| Existing artifact | Role in this design |
| --- | --- |
| `bob-default-camera-v1`, `bob-parity-v1` | Deterministic baseline/candidate route corpus |
| SCC1 camera snapshot and raw-first decoder | Observable behavior and diagnostic contract |
| Camera route/config/runtime separation | Non-vacuous role selection and build identity |
| SH linked-code parser and helper audit | Proof that candidate camera closure is float-free |
| Baseline/Q role infrastructure | Reused as source/bypass/fixed role separation |
| Camera range capture | Fixed-format and saturation-envelope selection |
| HWRAM/SCC transport and memory verifier | Evidence transport and camera-state budgeting |
| Writer-closure analysis | Inventory of authoritative external camera inputs/events |
| MSYS-safe SH tool launcher | Reproducible linked-object and ELF inspection |

The retired work is limited to the proposed function-by-function Q-shadow
translation, its per-tick float import/publish model, and its runtime float
fallback bridge. Additive probes, routes, transports, audits, and reports remain
valid sprint inputs.

## Prior Art and Reuse Decisions

The project is deliberately GPL-compatible, but the camera core remains a new
implementation because the available references do not match SM64's behavior
or data model.

| Reference | Pinned revision and license | Files inspected | Reuse mode |
| --- | --- | --- | --- |
| Sonic Z-Treme | `cff75451c1616aac1236fc2b44223902b55c706b`, GPL-3.0 | `SRC/game.c` (`update_camera`), `SRC/controls.c`, `ZTE/ZTE_DEF.H` (`camera_t`), `ZT_RENDERING.c` | Pattern-only for compact follow state, capped yaw/distance response, binary angles, and fixed trig. Direct copying is rejected because the behavior is Sonic-specific, lacks SM64 obstruction/mode semantics, and delegates transformation to SGL. |
| SlaveDriver Engine | `a8986591557b6e680550d3c23970284d3b38ff8f`, GPL-3.0-or-later | `SRUINS.C`, `WALLASM.S`, `UTIL.C` | Pattern-only for zero-float discipline, shift feedback, approximate distance, fixed view transforms, and SH-2 scheduling. Its camera is a first-person player/sector object and is an architectural mismatch for SM64. Existing attributed projection helpers remain separate reuse. |
| Jo Engine | `556d081146211b6a1cfa6591d70f9487d406758b`, root MIT with BSD-style file notices | `jo_engine/jo/3d.h`, `jo_engine/3d.c`, `jo_engine/math.c` | Reuse the already attributed in-tree `dmuls.l`/`xtrct` fixed multiply. Its camera is only an SGL `slLookAt` wrapper and supplies no gameplay-camera behavior. |
| In-tree SM64 camera | Current branch/project license | `src/game/camera.c`, camera headers, BOB route/capture tools | Behavioral oracle and compatibility contract; no function-by-function transliteration requirement. |

## Full-Game Expansion

After this sprint, capability coverage expands through additional deterministic
routes. Each new capability must pass the same semantic, feel, performance,
determinism, and zero-helper gates before being enabled in production.

BOB closes the first normal-gameplay capability set. It does not declare the
camera complete. Full-game completion requires every reachable camera
capability to be implemented or deliberately redesigned with owner-approved
behavior and evidence; no production float fallback remains.

## Sprint Exit

The sprint exits successfully when:

1. Phase A records source, bypass, and minimal-candidate attribution from the
   same BOB replay;
2. the decision to continue is justified by the recorded thresholds;
3. the production fixed camera supports normal follow/radial gameplay and
   obstruction handling without BOB-specific logic;
4. semantic and feel-equivalence evidence passes;
5. candidate camera closure has zero float/libm helpers;
6. camera and whole-simulation timing improvements are durable and
   workload-bound; and
7. the next unsupported capability family is named for the following sprint.
