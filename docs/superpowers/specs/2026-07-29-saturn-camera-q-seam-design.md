# Saturn Camera Q-Seam Design

**Date:** 2026-07-29  
**Status:** Approach approved; written specification awaiting owner review  
**Sprint task:** Task 3 of `2026-07-29-sh2-native-math-purge.md`

## Decision

Task 3 will convert the common per-tick default/Lakitu camera loop, not all
11,561 lines of `src/game/camera.c` in one change. The converted path will keep
persistent Saturn-only Q-format state across source ticks, publish the existing
float ABI once per tick, and remain selectable against an unchanged float
baseline build.

The task will also add an independent raw camera-idle evidence contract. It
must prove that both goal-camera state and render-facing Lakitu state remain
bit-stable for 600 consecutive source ticks under confirmed neutral input.
Existing Task 0-2 route, renderer, mutation, native-math, and output gates stay
unchanged.

Cutscenes, FOV effects, credits splines, player-2 paths, and rare camera modes
remain outside this increment. They are follow-on camera work, not silently
claimed complete.

## Why the scope is bounded

`camera.c` contains 382 statically audited soft-float references and runs every
source tick, but it is a whole simulation subsystem rather than a replaceable
math helper. Its float values form feedback loops across ticks. Converting one
operation at a time through float-to-Q-to-float bridges would retain conversion
helpers, discard fractional state repeatedly, and create precisely the
accumulated bias this task must prevent.

The common BOB replay path is a coherent first island:

- `update_camera` dispatches the active mode and always calls `update_lakitu`.
- `mode_default_camera`, `mode_lakitu_camera`, `update_default_camera`, and
  `update_mario_camera` produce the normal goal camera.
- `update_lakitu` and `next_lakitu_state` hold the smoothing feedback state
  consumed by rendering.
- `calculate_pitch`, `calculate_yaw`, `calculate_angles`, `calc_abs_dist`,
  `calc_hor_dist`, `rotate_in_xz`, `rotate_in_yz`, and the approach helpers are
  the shared numeric perimeter for that path.

This boundary is large enough to remove real hot soft-float work and small
enough to test as one state machine.

## Goals

1. Remove audited soft-float/libm edges from the common default/Lakitu camera
   loop on `TARGET_SATURN`.
2. Preserve the public `Camera`, `LakituState`, and renderer-facing ABI.
3. Keep Q state across ticks so smoothing and polar round trips do not
   repeatedly re-quantize through floats.
4. Prove 600 consecutive source ticks of exact idle-camera stability under
   confirmed neutral input.
5. Preserve the existing 2,000-tick route/output contract and positional gate.
6. Produce mechanically role-bound baseline/Q evidence from distinct build
   artifacts, with two deterministic runs per role.
7. Report the static helper reduction and in-engine simulation timing without
   turning emulator wall time into a retail-performance claim.

## Non-goals

- Converting every camera mode or every `f32` in `camera.c`.
- Changing controller deadzones or adding an idle snap to hide drift.
- Changing public engine struct layouts or source behavior on non-Saturn
  targets.
- Replacing collision, floor, or ceiling APIs in this task.
- Reusing or closely adapting unlicensed `sm64-psx` source.
- Weakening or replacing SBR4, `bob-parity-v1`, sourceboot-route-v6, or the
  Task 2 atan2 evidence roles.
- Claiming a retail Saturn speedup from Ymir measurements.

## Architecture

### 1. Port-owned numeric kernel

Add a small `src/port/saturn/runtime/saturn_camera_q` module containing only
numeric types and operations needed by the bounded camera island:

- signed Q scalar and three-component vector types;
- checked float/Q conversion used only at explicit state boundaries;
- native SH-2 32x32 multiply with a 64-bit product and fixed shift;
- 64-bit square/sum plus integer square root for horizontal and absolute
  distance;
- mixed-format trig multiplication using the existing Saturn trig table;
- polar extract/reconstruct helpers using the existing Task 2 atan2 seam;
- asymmetric and symmetric approach operations with a specified signed
  rounding rule and exact-target snap;
- checked 64/32 division through the existing Saturn DIVU contract where a
  transition genuinely requires division.

No hot helper may express C 64-bit division or modulo. Multiplication may use
64-bit intermediates because SH-2 supplies the required 32x32 product. Every
square sum is widened before accumulation. Float/Q boundary conversion must
use audited integer bit decomposition/construction, not arithmetic casts that
call libgcc helpers.

### 2. Guarded engine seam and shadow ownership

Under `TARGET_SATURN`, `camera.c` owns one private `SaturnCameraQState`. The
public structs remain unchanged. The shadow contains the converted island's
long-lived position, focus, goal, current, speed, distance, pan/zoom, and
transition values plus:

- Camera area-center coordinates;
- `gCameraZoomDist`, `sZoomAmount`, `sPanDistance`, and `sZeroZoomDist`;
- `sYawSpeed`, `sLakituDist`, `sLakituPitch`, `sModeOffsetYaw`, and
  `sAreaYaw`;
- Lakitu `focusDistance`, `oldPitch`, `oldYaw`, and `oldRoll`;

- a validity bit;
- a generation counter;
- the source camera mode and area identity used when it was seeded;
- overflow, saturation, divide-by-zero, and unexpected-reseed counters;
- a range-fallback counter for a mode/state outside the proven Q envelope.

The shadow is seeded from the float state only at a documented state boundary:
camera initialization/reset, warp, area or mode change, cutscene entry/exit,
or an external collision/floor correction that writes the public vectors.
Normal default/Lakitu ticks read and update the shadow directly. At the end of
each tick, one mirror operation publishes the Q results to the existing float
fields for renderer and engine consumers. That mirror uses the helper-free
integer bit bridge above; retaining public float storage must not retain a hot
soft-float conversion edge.

Converted arithmetic lives in dedicated `saturn_camera_q_*` functions rather
than mixed float/Q branches inside one large function. This gives the static
audit unambiguous caller symbols to forbid and keeps the non-Saturn path
unchanged.

Before behavior changes, the implementation records every write to Camera and
Lakitu fields reachable from the bounded default/Lakitu call graph. Each write
is classified as Q-owned, an explicit float-API import, a public mirror, or a
shadow invalidation. The implementation review treats an unclassified writer
as a correctness failure; no direct float write may bypass that ownership
table.

Unconverted floor, ceiling, and collision APIs form explicit bridges. The Q
path exports temporary query vectors through the helper-free bit converter,
calls the unchanged API, imports its result once, and continues in Q. It does
not rebuild the whole shadow from public floats after the call. Bridge counts
are captured so an accidental per-operation conversion loop is detectable.

The Q variant may use the float implementation for an explicitly out-of-scope
mode, but that transition invalidates the shadow and increments the reseed
counter. The accepted BOB route must remain inside the converted island after
its recorded seed and must report zero unexpected reseeds, saturation,
overflow, and divide faults.

### 3. Ordered per-tick data flow

The Q variant uses this fixed ownership order:

| Stage | Authoritative state and permitted writes |
| --- | --- |
| 0. Eligibility | Check the generic camera mode, transition state, and numeric range before Q arithmetic. An unsupported mode or out-of-range input invalidates the shadow and runs the unchanged float path for that tick. It increments `range_fallback_count`; the accepted route requires zero. No level/area name controls eligibility. |
| 1. Seed/import | When invalid, import Camera position/focus/area centers and mode/angle state; all Lakitu current/goal/render vectors, distance/old-angle fields, mode/angle state and speed fields; the four zoom/pan globals; the five yaw/distance/pitch globals; `sOldPosition`, `sOldFocus`; `sModeTransition`; and `sModeInfo` including both transition endpoints. After import, the Q shadow is authoritative. |
| 2. Goal update | The Q default/Lakitu mode reads Q Mario/camera/Lakitu inputs, area centers, zoom/pan values, and yaw/distance/pitch globals. It writes Q Camera goal position, focus, yaw, nextYaw, and any changed pan/zoom/yaw state. Floor, ceiling, and collision calls export temporary query values and import only their returned corrections at the exact call site. |
| 3. Transition | The Q `next_lakitu_state` consumes Q `sOldPosition`, `sOldFocus`, `sModeTransition`, and `sModeInfo`; writes the Q transition result; advances the Q transition state/frame; and updates the Q old-position/focus copies in the same order as the float baseline. |
| 4. Smoothing | The Q `update_lakitu` advances goal/current/render vectors and the four speed coefficients. It owns Q Lakitu focusDistance, oldPitch/oldYaw/oldRoll, yaw, nextYaw, and roll plus `sYawSpeed` for the remainder of the tick. |
| 5. Post-adjustment | The bounded path applies its floor correction and any active shake through Q operations or one named bridge. A nonzero modifier without a converted or named bridge invalidates the shadow before it can write public state. |
| 6. Publish | One helper-free bit bridge writes Camera, Lakitu, the pan/zoom and yaw/distance/pitch globals, `sOld*`, `sModeTransition`, and `sModeInfo` mirrors in source order. This is the only normal-tick write to those public float fields. |
| 7. External writer | A later out-of-island camera writer marks the shadow invalid and increments the generation. The next eligible tick performs a full Stage 1 import; it never partially merges public floats into an otherwise-valid shadow. |

The pre-implementation writer inventory maps every reachable write to one row
of this table. Tests fail if a writer is omitted or assigned to two owners.

### 4. Build variants

Add `SATURN_CAMERA_VARIANT=1|2`:

- `1`: unchanged float camera baseline;
- `2`: bounded Q camera island.

Task 3 builds fix `SATURN_ATAN2_VARIANT=2`; camera evidence must not reuse the
Task 2 legacy/Q16 role names. The camera variant is validated by the Makefile,
embedded in raw target telemetry, and included in the output tag so builds
cannot overwrite each other.

## Numeric format decision

The design does not inherit the renderer's Q16.16 format blindly. Before the
Q behavior is enabled, a baseline range capture records the maximum absolute
value of every state coordinate, delta, distance, approach residual,
transition numerator/divisor, and square sum used by the bounded island.

The selection rule is deterministic:

1. Prefer Q16.16 only if every captured scalar and delta fits within half of
   its signed range, every derived 64-bit square/sum is proven in range, and
   its host differential meets the derived error bounds.
2. Otherwise use Q20.12, subject to the same half-range, intermediate, and
   differential checks.
3. If neither format passes, Task 3 stops; it does not add silent clamps,
   deadzones, or a looser behavioral threshold.

This decision proves the guarded Q envelope, not every camera state in every
level. Generic runtime bound checks use conservative integer-width proofs for
all accepted operands and fall back before Q arithmetic when a value or mode
is outside that envelope. BOB evidence proves that the acceptance route stays
inside the envelope with zero range fallbacks; it does not authorize a
scene-named branch or claim unmeasured mode coverage.

The selected format, observed ranges, headroom, rounding rule, and error
derivation are committed in the Task 3 evidence report and native-math audit
contract. This is the Task 3-local state decision; Task 5 may still decide a
different project-wide simulation format from its broader telemetry.

Approach operations round to the nearest representable value with ties to
even, then snap exactly to the target whenever the remaining
absolute delta is no larger than one step. This prevents a one-LSB residual
from surviving forever without introducing a directional tie bias. Host tests
must cover positive and negative values, zero crossings, both tie parities,
one-LSB residuals, and exact-target behavior.

## Camera-idle evidence contract

### Why SBR4 remains unchanged

The existing 2,000-tick replay ends with 200 ticks of forward input and SBR4
stores only a frozen endpoint. It cannot prove a neutral 600-tick interval or
intermediate stability. SBR4 remains the route and output anchor; Task 3 adds a
parallel replay-only contract named SCC1.

### Baseline quiescence pin

First, an instrumentation-only baseline trace runs after replay completion,
when the runtime applies buttons/stick `(0, 0, 0)`. The trace identifies the
earliest source tick at which all of these are true:

- the last applied pad is neutral;
- no camera transition or cutscene is active;
- Mario's position, velocity, forward velocity, action, action state/action
  timer, and face angles are unchanged for the next 60 source ticks;
- all SCC1 camera words are unchanged for the next 60 source ticks.

That baseline offset is committed as `idle_start_tick` in the SCC1 fixture.
Both baseline and Q builds then use the same fixed offset. The Q build may not
choose a later start to hide settling or drift.

### SCC1 raw layout

SCC1 is linked only into the sourceboot replay build and stored in LWRAM. Its
header is exactly 24 big-endian `u32` words:

| Word | Meaning |
| ---: | --- |
| 0 | magic `0x53434331` (`SCC1`), published last |
| 1 | version `1` |
| 2 | header word count `24` |
| 3 | sample stride in words `81` |
| 4 | sample count `600` |
| 5 | camera variant (`1` baseline, `2` Q) |
| 6 | atan2 variant, required to be `2` |
| 7 | route magic `0x53425234` (`SBR4`) |
| 8 | route version `4` |
| 9 | replay ticks, required to be `2000` |
| 10 | fixed `idle_start_tick` |
| 11 | first sampled source tick |
| 12 | last applied input: buttons bits 31:16, signed stick X bits 15:8, signed stick Y bits 7:0 |
| 13 | final state flags |
| 14-18 | overflow, saturation, divide-fault, unexpected-reseed, and range-fallback counts |
| 19-20 | bridge export and import counts |
| 21 | Q shadow generation (`0` for baseline) |
| 22 | payload word count `48600` |
| 23 | reserved, required to be zero |

State flags use bit 0 `gCamera != NULL`, bit 1 replay complete, bit 2 neutral
input, bit 3 no cutscene, bit 4 no active mode transition, and bit 5 Mario
quiescent. Bits 6-31 are reserved and must be zero. These booleans are the
pointer/state sentinels; SCC1 does not expose or compare an address that can
change between builds.

The header is followed by exactly 600 samples of 81 words each:

| Words | Meaning |
| --- | --- |
| 0 | source tick |
| 1 | applied input, packed exactly as header word 12 |
| 2 | state flags, packed exactly as header word 13 |
| 3 | reserved, required to be zero |
| 4-6 | `Camera.pos[3]` float bits |
| 7-9 | `Camera.focus[3]` float bits |
| 10 | signed Camera yaw in bits 31:16, signed nextYaw in bits 15:0 |
| 11 | Camera mode, defMode, cutscene, and doorStatus in bits 31:24, 23:16, 15:8, and 7:0 |
| 12-14 | `LakituState.curPos[3]` float bits |
| 15-17 | `LakituState.curFocus[3]` float bits |
| 18-20 | `LakituState.goalPos[3]` float bits |
| 21-23 | `LakituState.goalFocus[3]` float bits |
| 24-26 | render-facing `LakituState.pos[3]` float bits |
| 27-29 | render-facing `LakituState.focus[3]` float bits |
| 30 | signed Lakitu yaw in bits 31:16, signed nextYaw in bits 15:0 |
| 31 | signed Lakitu roll in bits 31:16, mode in bits 15:8, defMode in bits 7:0 |
| 32-35 | focHSpeed, focVSpeed, posHSpeed, and posVSpeed float bits |
| 36-38 | `sOldPosition[3]` float bits |
| 39-41 | `sOldFocus[3]` float bits |
| 42 | signed `sModeInfo.newMode` in bits 31:16, signed lastMode in bits 15:0 |
| 43 | signed `sModeInfo.max` in bits 31:16, signed frame in bits 15:0 |
| 44-46 | transition-start focus float bits |
| 47-49 | transition-start position float bits |
| 50 | transition-start distance float bits |
| 51 | signed transition-start pitch in bits 31:16, signed yaw in bits 15:0 |
| 52-54 | transition-end focus float bits |
| 55-57 | transition-end position float bits |
| 58 | transition-end distance float bits |
| 59 | signed transition-end pitch in bits 31:16, signed yaw in bits 15:0 |
| 60 | signed `sModeTransition.posPitch` in bits 31:16, signed posYaw in bits 15:0 |
| 61 | `sModeTransition.posDist` float bits |
| 62 | signed `sModeTransition.focPitch` in bits 31:16, signed focYaw in bits 15:0 |
| 63 | `sModeTransition.focDist` float bits |
| 64 | `sModeTransition.framesLeft` signed 32-bit value |
| 65-67 | `sModeTransition.marioPos[3]` float bits |
| 68-70 | Camera areaCenX, areaCenY, and areaCenZ float bits |
| 71-74 | `gCameraZoomDist`, `sZoomAmount`, `sPanDistance`, and `sZeroZoomDist` float bits |
| 75 | signed `sYawSpeed` in bits 31:16, signed `sLakituDist` in bits 15:0 |
| 76 | signed `sLakituPitch` in bits 31:16, signed `sModeOffsetYaw` in bits 15:0 |
| 77 | signed `sAreaYaw` in bits 31:16; bits 15:0 reserved and zero |
| 78 | Lakitu `focusDistance` float bits |
| 79 | signed Lakitu oldPitch in bits 31:16, signed oldYaw in bits 15:0 |
| 80 | signed Lakitu oldRoll in bits 31:16; bits 15:0 reserved and zero |

Every float word is an exact IEEE-754 bit copy. Every signed packed value uses
two's-complement representation. Raw length must equal
`(24 + 600 * 81) * 4` bytes; reserved words/bits must be zero. Magic is
published last. The host reads bounded chunks if the emulator response limit
cannot return the complete window at once.
Before SCC1 is accepted, the link map must prove that the complete block fits
without overlapping source data and leaves the existing sourceboot runtime
headroom unchanged outside the new replay-only allocation.

### SCC1 pass conditions

The host verifier independently decodes raw bytes and rejects decoded/raw
disagreement. A capture passes only if:

1. the SBR4 route anchor is present at 2,000 replay ticks;
2. all 600 samples are present at consecutive source ticks;
3. every sample's applied-input word is neutral;
4. every float word is finite and every required state-flag sentinel is set;
5. within each role, every state word in samples 1-599 exactly equals sample
   0; only source tick advances;
6. overflow, saturation, divide, unexpected-reseed, and range-fallback counts
   are zero, and bridge counts match their pinned fixture values;
7. two runs of the same role have byte-identical SCC1 windows;
8. baseline and Q roles use distinct ELF and ISO hashes and their raw variant
   fields match their declared roles;
9. every paired baseline/Q sample compares every listed Camera, Lakitu,
   area-center, pan/zoom, yaw/distance/pitch, `sOld*`, and transition field:
   packed integer fields are exact; each float is within
   `max(selected-Q ulp, one f32 ulp at the baseline magnitude)`; and every
   position/focus component is additionally below one world unit of
   divergence.

An equality summary, target-side boolean, or first/last-only comparison is not
sufficient; the host must compare every selected word at every tick.

## Existing route and output gates

Every Task 3 role also runs the existing route and renderer contract. The
2,000-tick A/B comparison continues to require:

- final Mario positional divergence below one world unit;
- identical timer, action, face angles, camera mode, and required camera
  fields;
- identical triangle and renderer reject/fault behavior under the hardened
  Task 2 comparator;
- independent raw SBR4 decoding and artifact identity checks.

Task 3 may add SCC1 fields and a new camera-specific comparator, but may not
edit old fixtures to make a Q result fit.

## Static and performance evidence

The existing simulation native-math audit v2 remains immutable. After the new
ELF is measured, Task 3 creates a hash-pinned v3 contract that:

- has a lower exact helper total than v2;
- keeps `_atan2_lookup` and `_atan2s` forbidden;
- declares exact roots for the two `camera.c` seam wrappers,
  `saturn_camera_q_default_tick`, `saturn_camera_q_lakitu_tick`,
  `saturn_camera_q_next_lakitu_state`, and the publish bridge;
- generates and hash-pins the complete transitive caller set reachable from
  those roots, stopping only at the named floor/ceiling/collision bridge
  functions;
- requires zero audited helper edges in every generated closure caller,
  including the `camera.c` wrappers;
- records each stopped bridge in an exact allowlist with its reason and
  per-symbol helper count, and permits no increase over the baseline bridge
  count;
- rejects a non-shrinking global total, a missing root/caller, or a caller
  removed only from the audit manifest;
- includes mutations that restore a helper edge, drop a closure caller, and
  inflate a bridge count, proving each failure is caught.

For each role, two target runs record `sim_frt_ticks_accum`, render timing,
wait/busy counters, wall time, and emulator speed ratio. Both independent A/B
comparisons must show a lower Q-variant simulation accumulator. Wall time is
reported as comparative emulator evidence only and is not a retail claim.

## Test strategy

Implementation follows red-green-refactor. Production behavior is not written
until the corresponding failing test has been observed.

### Numeric differential tests

Compile the original float functions and the Q kernel in a host fixture. Feed
literal boundary cases plus real captured operands from the baseline route.
Expected values and tolerance bounds are derived independently from the float
path and chosen representation. Tests cover:

- zero, axis-aligned, near-zero, and maximum captured vectors;
- positive/negative approach and one-LSB residual cases;
- polar extract/reconstruct and angle wrap boundaries;
- 64-bit distance-square accumulation and integer square root;
- conversion saturation and checked divide faults;
- Q shadow seed, tick persistence, invalidation, and mirror behavior.

Each new operation names the production mutation it catches. The mutation
pass changes scale, shift, rounding, comparison, or field inclusion and must
make the fixture fail.

### SCC1 and role-binding tests

Host tests begin with synthetic raw fixtures and must reject:

- wrong magic/version, truncated or oversized data;
- a changed word in any of the 599 later samples;
- an omitted or mispacked field at any defined sample offset 0-80;
- non-finite bits, bad sample count, non-neutral input, or missing route
  anchor;
- camera role/raw variant mismatch;
- same-image baseline/Q comparison;
- decoded data that disagrees with raw bytes.

The existing Saturn tool suite and target `make verify` profile run unchanged
for both camera variants.

## Error handling and rollback

Overflow, saturation, divide-by-zero, invalid state, and unexpected shadow
reseeds are visible counters and acceptance failures. The Q path never hides
one with a deadzone or silent clamp.

The compile-time camera variant is the rollback boundary. If any differential,
mutation, SCC1, SBR4, static-audit, or target build gate fails, variant 1
remains the behaviorally unchanged build and the Q conversion does not become
the default.

## Reference consumption and provenance

| Source | Pin and license | Files/behavior inspected | Reuse mode for Task 3 |
| --- | --- | --- | --- |
| In-tree SM64 camera | current branch; project source | `src/game/camera.c`, `camera.h`, current sourceboot replay and capture tools | Direct in-tree modification behind `TARGET_SATURN` |
| `malucard/sm64-psx` | `27d80c0b6fc0be8d3b71dfb46486c14333d28a8d`; no root `LICENSE`, `LICENCE`, or `COPYING` | `include/types.h`, `src/engine/math_util.c/.h`, `src/game/camera.c`, `src/game/game_init.c`, `src/port/float_math.c`, `src/port/fract_math.c`, and `Makefile.psx.mk` for Q20.12 architecture and the neutral-stick defect | Behavior study only; no copy, close port, or adaptation |
| Jo Engine | `556d081146211b6a1cfa6591d70f9487d406758b`; root MIT license, with the upstream `math.c` BSD-style notice retained in the existing adaptation | `work/upstream/joengine/jo_engine/math.c:57-70`; existing destination `src/port/saturn/gfx/saturn_q16_sh2.h`, whose header records the source range, pin, notice, and material changes | Reuse the existing attributed in-tree `dmuls.l`/`xtrct` primitive; no new source copy planned |
| SlaveDriver Engine | `a8986591557b6e680550d3c23970284d3b38ff8f`; GPL-3.0-or-later in `work/upstream/slavedriver-engine/LICENSE.txt` | `work/upstream/slavedriver-engine/WALLASM.S:253-353`; existing close-port destinations `src/port/saturn/gpl/slavedriver_projection.h` and `.sx`, with GPL notice, source range, pin, and material-change record | Reuse the existing GPL-isolated start/collect API and scheduling pattern; no new camera source copy |
| Sega SGL record | `SGL302J.ZIP` SHA-256 `429d729952b6837e2af221a5a6e0de4ca58d2ed1ce2471bd65411a62954ff811`; proprietary documentation | `MATH.TXT` and the recorded `SL_DEF.H:105` Q16.16 convention | Documentation study only; no code/header/sample copy |

The camera state machine and tests are clean-room work from the in-tree SM64
contract and recorded behavior requirements. Any newly discovered direct
reuse requires a provenance/notice update before merge.

## Implementation decomposition

The detailed implementation plan will split Task 3 into reviewable increments:

1. SCC1 decoder/verifier and role-binding tests, then target probe plumbing.
2. Baseline post-route trace, fixed quiescence pin, range telemetry, and
   recorded format decision.
3. Camera Q numeric kernel with host differential and mutation gates.
4. Guarded persistent shadow plus the default/Lakitu converted island.
5. Static audit v3, two-run-per-role target captures, A/B comparison, and
   evidence report.

Each increment receives its own implementation report and independent review.
No behavior-changing increment begins until the additive SCC1 gate and format
decision are committed.

## Acceptance checklist

Task 3 is complete only when all of the following are true:

- The bounded default/Lakitu Q island is active only for camera variant 2.
- Public camera/Lakitu layouts and non-Saturn behavior are unchanged.
- Numeric differential and mutation tests pass on captured and boundary data.
- The selected Q format has recorded ranges, headroom, and error derivation.
- SCC1 proves exact 600-tick stability for every selected Camera and Lakitu
  word under neutral input, twice per role.
- Baseline/Q artifacts are distinct and mechanically role-bound.
- Existing SBR4 route/output and Task 0-2 gates pass unchanged.
- Native-math audit v3 is lower than v2 and enforces the complete generated
  Q-island caller closure plus its exact bridge boundary.
- Both independent A/B comparisons reduce `sim_frt_ticks_accum`.
- Saturation, overflow, divide, unexpected-reseed, and range-fallback counters
  are zero.
- Evidence records source pins, licenses, inspected files, and reuse modes.
