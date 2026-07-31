# Saturn Camera Q-Seam Design

**Date:** 2026-07-29  
**Status:** Owner approved, including the non-vacuous BOB acceptance-route amendment; implementation-plan review clarifications incorporated
**Sprint task:** Task 3 of `2026-07-29-sh2-native-math-purge.md`

## Decision

Task 3 will convert the common per-tick default/Lakitu camera loop, not all
11,561 lines of `src/game/camera.c` in one change. The converted path will keep
persistent Saturn-only Q-format state across source ticks, publish the existing
float ABI once per tick, and remain selectable against an unchanged float
baseline build.

The existing `bob-parity-v1` replay remains byte-for-byte unchanged and keeps
its current regression role. It leaves `Camera.mode` at
`CAMERA_MODE_RADIAL`, so it cannot by itself prove that the bounded default
goal seam executed. A separate `bob-default-camera-v1` replay therefore keeps
the proven 120-tick neutral bootstrap, applies one tick of `R_TRIG`, and then
holds neutral input through the same 2,000-tick endpoint. The trigger selects
`mode_mario_camera()` and therefore `mode_default_camera()` even though the
stored `Camera.mode` remains radial. SCC1 binds this route in raw word 23 and
requires the default seam's pinned nonzero bridge counts. The original route
still runs independently as a no-regression gate.

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
- `mode_default_camera`, `mode_lakitu_camera`, `mode_mario_camera`,
  `update_default_camera`, and `update_mario_camera` produce the normal goal
  camera.
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

If the measured decision selects Q20.12, state-state multiplication uses a
format-specialized `dmuls.l` high/low reconstruction with a 12-bit shift; it
must not call the existing Q16-only `xtrct` wrapper unchanged. Mixed state/trig
multiplication may still use the existing 16-bit trig-table shift. Q20.12
division reuses the existing attributed Q16 DIVU start/collect schedule and
performs a proved, signed truncation from the collected Q16 quotient to Q12.
Before narrowing, the sign-restored Q16 quotient must itself fit signed
32-bit; afterward the Q12 result must fit its configured envelope. The host
differential must prove that this equals direct Q12 division for the full
captured and boundary corpus.

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
table. Coverage is a generated transitive direct-call closure from all bounded
roots, including `update_mario_camera`; every indirect/function-pointer edge
and possible callee is reviewed explicitly. A per-function-only brace scan is
not sufficient.

Unconverted floor, ceiling, and collision APIs form explicit bridges. The Q
path exports temporary query vectors through the helper-free bit converter,
calls the unchanged API, imports its result once, and continues in Q. It does
not rebuild the whole shadow from public floats after the call. Bridge counts
are captured so an accidental per-operation conversion loop is detectable.
The pure default operation is an explicit tagged phase machine: it emits one
of eight typed requests, `camera.c` calls the matching noinline direct bridge,
and the Q module resumes only with a same-phase typed result. No indirect
callback or underspecified pause/resume convention is allowed.

The Q variant may use the float implementation for an explicitly out-of-scope
mode, but that transition invalidates the shadow and increments the reseed
counter. The accepted BOB route must remain inside the converted island after
its recorded seed and must report zero unexpected reseeds, saturation,
overflow, and divide faults.

### 3. Ordered per-tick data flow

The Q variant uses this fixed ownership order:

| Stage | Authoritative state and permitted writes |
| --- | --- |
| 0. Eligibility | Check the selected dispatch path, transition state, and numeric range before Q arithmetic. `mode_mario_camera()` is eligible even when the stored `Camera.mode` is radial. Before the first successful seed, an out-of-island bootstrap tick remains on the float path without arming acceptance counters. After the first seed, an unsupported dispatch or out-of-range input invalidates the shadow, enters the named cold float-fallback bridge, and increments `range_fallback_count`; the accepted route requires zero. No level/area name controls eligibility. |
| 1. Seed/import | When invalid, import Camera position/focus/area centers and mode/angle state; all Lakitu current/goal/render vectors, distance/old-angle fields, mode/angle state and speed fields; the four zoom/pan globals; the five yaw/distance/pitch globals; `sOldPosition`, `sOldFocus`; `sModeTransition`; and `sModeInfo` including both transition endpoints. After import, the Q shadow is authoritative. |
| 2. Goal update | The Q default/Lakitu mode reads Q Mario/camera/Lakitu inputs, area centers, zoom/pan values, and yaw/distance/pitch globals. It writes Q Camera goal position, focus, yaw, nextYaw, and any changed pan/zoom/yaw state. Floor, ceiling, and collision calls export temporary query values and import only their returned corrections at the exact call site. |
| 3. Transition | The Q `next_lakitu_state` consumes Q `sOldPosition`, `sOldFocus`, `sModeTransition`, and `sModeInfo`; writes the Q transition result; advances the Q transition state/frame; and updates the Q old-position/focus copies in the same order as the float baseline. |
| 4. Smoothing | The Q `update_lakitu` advances goal/current/render vectors and the four speed coefficients. It owns Q Lakitu focusDistance, oldPitch/oldYaw/oldRoll, yaw, nextYaw, and roll plus `sYawSpeed` for the remainder of the tick. |
| 5. Post-adjustment | The bounded path applies its floor correction and any active shake through Q operations or one named bridge. A nonzero modifier without a converted or named bridge invalidates the shadow before it can write public state. |
| 6. Publish | One helper-free bit bridge writes Camera, Lakitu, the pan/zoom and yaw/distance/pitch globals, `sOld*`, `sModeTransition`, and `sModeInfo` mirrors in source order. This is the only normal-tick write to those public float fields. |
| 7. External writer | After the first successful seed, a later out-of-island camera writer marks the shadow invalid and increments the generation. Pre-seed initialization/invalidation keeps generation zero. The next eligible tick performs a full Stage 1 import; it never partially merges public floats into an otherwise-valid shadow. |

The pre-implementation writer inventory maps every reachable write to one row
of this table. Tests fail if a writer is omitted or assigned to two owners.

### 4. Build variants

Add `SATURN_CAMERA_VARIANT=1|2`:

- `1`: unchanged float camera baseline;
- `2`: bounded Q camera island.

Task 3 builds fix `SATURN_ATAN2_VARIANT=2`; camera evidence must not reuse the
Task 2 legacy/Q16 role names. The camera variant is validated by the Makefile,
embedded in raw target telemetry, exposed as an absolute sibling-ELF marker,
and included in the output tag so builds cannot overwrite each other.

Add `SATURN_SOURCEBOOT_CAMERA_ROUTE=0|1`:

- `0`: the immutable `bob-parity-v1` route;
- `1`: the separate `bob-default-camera-v1` acceptance route.

Route 1 is valid only with replay enabled, is included in the output tag, and
is encoded as raw SCC1 route ID `2`. The route manifest hash, raw route ID,
camera variant, atan2 variant, `gCameraZoomDist == 350.0f`, nonzero Q shadow
generation, and pinned nonzero default-seam bridge counts form the compound
non-vacuity proof.

`SATURN_CAMERA_IDLE_DISCOVERY` and `SATURN_CAMERA_RANGE_CAPTURE` are validated
0/1 build inputs, C preprocessor defines, and route-1 object/output tags.
`SATURN_SOURCE_CART_STAGE_SECTORS` is validated as 4, 8, or 16 and is also an
object/output tag. The default remains 16. Camera evidence artifacts select 8,
or 4 only if the deterministic HWRAM reserve gate requires it; every selected
size must re-prove the cart READY/copied-size/`SOURCE.DAT` hash contract.

## Numeric format decision

The design does not inherit the renderer's Q16.16 format blindly. Before the
Q behavior is enabled, a baseline range capture records the maximum absolute
value of every state coordinate, delta, distance, approach residual,
transition numerator/divisor, and square sum used by the bounded island.

The selection rule is deterministic but has two phases:

1. Raw range evidence nominates Q16.16 and/or Q20.12 candidates. A candidate
   must fit every captured scalar/delta within half its signed range and pass
   all square/sum/division-intermediate proofs. This phase does not write the
   production config.
2. The actual production numeric source is compiled for every nominated
   candidate and differentially tested over literal boundaries, the raw
   captured corpus, every measured extremum, transition division pairs, and
   deterministic neighborhoods.
3. Freeze Q16.16 when it passes. Q20.12 may be frozen only when it passes and
   Q16 was either not range-qualified or has a reviewed representational-bound
   failure. A code, mutation, overflow, or unexplained mismatch is a hard stop,
   not grounds to choose the other format.
4. If neither format passes, Task 3 stops; it does not add silent clamps,
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

The host raw-decodes SCC1/SCR1 and prints that discovered tick as the only
input to a fixed non-discovery rebuild. It then raw-decodes the fixed capture,
requires its first 60 semantic samples to equal the discovery proof, and only
after those gates atomically emits the SCC1 idle fixture plus a hash-bound
verification report. Candidate selection requires both artifacts. No manually
transcribed tick or pre-existing fixture can become authoritative. Both
baseline and Q builds then use the same fixed offset. The Q build may not
choose a later start to hide settling or drift.

The authoritative source tick is sourceboot's existing
`sourceboot_sim_tick_count`, incremented once immediately after
`game_loop_one_iteration()`. The recorder receives that value after simulation
timing/accounting. No second tick is added to the generic replay object;
`input_replay_ticks` keeps its existing frozen-at-2,000 meaning. The runtime
snapshot adds only the actual post-replay applied pad. The camera probe keeps
exactly 77 SCC state words and carries Q source dispatch out of band solely so
sourceboot can latch SQT1; the dispatch value is never packed as an extra SCC
word.

### SCR1 range-discovery companion

The discovery build appends one exact `0x2000`-byte raw SCR1 block to the same
LWRAM capture section. It is present only when route 1 and range capture are
both enabled. Its fixed 32-word header, 256 three-word maxima records, 256
four-word corpus records, and zero-reserved tail are specified in
`docs/superpowers/plans/2026-07-29-saturn-camera-q-seam.md`, Task 5. SCR1 uses
magic `0x53435231`, version 1, explicit route/source-tick anchors, coverage and
fault counts, and magic-last publication. The capture reads SCC1, SBR4, and
SCR1 from the same paused emulator instance and independently decodes every
raw window.

### SQT1 first-seed trace

Task 9 adds a separate 16-byte HWRAM record named
`sourceboot_camera_q_seed_trace`. Sourceboot observes the Q probe after each
game-loop tick and latches it once when the trace is unpublished, generation
is nonzero, and the probe reports that the current tick completed a valid
Q-active Mario seam. The probe reports `MARIO` only for that successful
Q-active tick and `NONE` for mere selection, bootstrap, invalid, or fallback
ticks. Pre-seed initialization/invalidation leaves generation zero; only a
successful seed arms later invalidation-generation accounting. Its four
big-endian words are magic `0x53515431` published last, packed version
1/dispatch 3, route ID 2, and the existing sourceboot source tick. The host
derives the one-based first Mario-dispatch tick from the route manifest's
unique one-tick R-trigger segment and requires the raw target tick to match.
Baseline leaves all four words zero. SQT1 adds exactly 16 bytes to HWRAM and
does not change the fixed SCC1 LWRAM ABI.

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
| 23 | input route ID, required to be `2` (`bob-default-camera-v1`) |

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
without overlapping source data. Fixed capture ends at `0x002FB2E0` and must
leave at least `0x4000` LWRAM; discovery adds exactly `0x2000` and must leave
`0x2D20`.

HWRAM is a separate gate. The reviewed pre-task image had
`___end=0x060FDCB0`, only `0x2350` below the top and only `0x1350` above the
mandatory `0x1000` TLSF floor. Before Q implementation, camera evidence builds
reduce the boot-only cart staging buffer from 16 sectors to 8, or to 4 only
when the deterministic post-transport reserve requires it, and re-prove cart
load/hash behavior. The complete transport must leave at least `0x5B00` total
HWRAM, reserving `0x4000` for later camera code. Every target-code increment
records its exact map delta, and the final image must leave at least `0x1B00`
(`0x1000` TLSF plus `0x0B00` safety). Neither floor may be weakened.

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
   are zero, Q generation is nonzero only for variant 2, and the Q role's
   default-seam bridge counts match pinned nonzero fixture values;
7. two runs of the same role have byte-identical SCC1 windows;
8. baseline and Q roles use distinct ELF and ISO hashes and their raw variant
   fields match their declared roles;
9. every paired baseline/Q sample compares every listed Camera, Lakitu,
   area-center, pan/zoom, yaw/distance/pitch, `sOld*`, and transition field:
   packed integer fields are exact; each float is within
   `max(selected-Q ulp, one f32 ulp at the baseline magnitude)`; and every
   position/focus component is additionally below one world unit of
   divergence.
10. raw route ID is `2`, every sample has
    `gCameraZoomDist == 350.0f` (`0x43AF0000`), and the capture report's
    route-manifest digest matches the immutable `bob-default-camera-v1`
    manifest.

An equality summary, target-side boolean, or first/last-only comparison is not
sufficient; the host must compare every selected word at every tick.

## Existing route and output gates

Every Task 3 role runs the new camera acceptance route and also reruns the
existing immutable route and renderer contract. The original
`bob-parity-v1` fixture is never edited to fit the new route. Both 2,000-tick
A/B comparisons continue to require:

- final Mario positional divergence below one world unit;
- identical timer, action, face angles, camera mode, and required camera
  fields;
- identical triangle and renderer reject/fault behavior under the hardened
  Task 2 comparator;
- independent raw SBR4 decoding and artifact identity checks.

Task 3 may add SCC1 fields and a new camera-specific comparator, but may not
edit old fixtures to make a Q result fit.

The existing `compare_route_reports.py` remains unchanged because it
intentionally requires legacy/Q16 roles and atan2 variants 1/2. The
camera-specific SBR4 comparator instead requires camera-baseline/camera-Q
roles, atan2 variant 2 for both, sibling-ELF camera markers 1/2, independently
decoded 160-byte SBR4 windows, same-role equality of non-timing
behavior/output fields, and the same hardened renderer gates. Timing and host
provenance are recorded separately.

## Linked-ELF audit parser correction prerequisite

Task 3 starts with an evidence-driven correction to the native-math verifier,
before any transport build or camera behavior change. The existing v2 result
is not immutable: route 1 changes only layout, yet the linked
`sh-elf-objdump -d` stream decodes in-function literal pools as SH
instructions and the current linear scanner treats those data halfwords as
control flow and register writes.

The proved examples are binding regression fixtures:

- `_find_floor` spans `0x0600AF20..0x0600B07B`, including its literal pool at
  `0x0600B040..0x0600B07B`. Route 0's BSS-address low halfword `0xAEC0`
  decodes as `bra`; route 1's `0xB4A0` decodes as a fake
  `bsr ... <_load_static_surfaces+0x9a>`. The current regex strips the
  `+0x9a` and fabricates a direct edge.
- Exact-target-only filtering is forbidden. Real code deliberately calls
  internal entries such as `___movmemSI52+0x2` and
  `div0+0x6`, `div0+0x8`, and `div0+0x18`; the linked image contains 1,024
  textual offset-`bsr` decodes mixing real instructions with pool data.
- Literal data also corrupts indirect-call state. In
  `_guLookAtReflectF`, route 0 pool halfword `0x68AC` decodes as
  `extu.b r10,r8`, falsely clearing `r8` and hiding four real
  `jsr @r8` calls to `___mulsf3`; route 1's different pointer bits do not.
  Even an exact-target-only simulation therefore reports the same 201
  functions but unequal totals 560 and 564.
- The real internal targets are not all owned by `STT_FUNC`. All eight
  legitimate internal-offset `bsr` instructions in `___udivsi3` target
  `div0+0x6`, `div0+0x8`, or `div0+0x18`, while readelf reports `div0` at
  `0x0600437E` as a zero-size `STB_LOCAL STT_NOTYPE` symbol in `.text`
  preceding the `___udivsi3` function range. A function-only target map
  therefore rejects proven executable assembly even though broad `STT_NOTYPE`
  admission would also misclassify data.

The corrected analyzer separates code-address discovery from register
dataflow. `sh-elf-readelf -SW` supplies executable-section bounds and
`sh-elf-readelf -sW` supplies canonical call-graph owners only from
`STT_FUNC` symbols whose section has `SHF_EXECINSTR`. Every derived range must
remain inside that section. A nonzero function owns its exact half-open range.
A zero-size function ends at the next greater function start in the same
section, or at the section end. Same-start functions are aliases only when
their effective ends agree; a same-start/end group has one deterministic
canonical owner:
`GLOBAL` binding before `WEAK` before `LOCAL`, then non-hidden before hidden,
then shortest name, then bytewise lexical name. All other names are recorded
as aliases. Different effective ends at one start, or a partial overlap
between different starts, is ambiguous and fails.

The symbol pass also indexes, but does not automatically admit,
`STB_LOCAL STT_NOTYPE` labels with nonempty names in executable sections.
Their candidate half-open range starts at the label value and ends at the
next strictly greater defined symbol value in that section, or section end,
then is capped at the first canonical `STT_FUNC` start it would overlap.
Duplicate identical symbol rows collapse; different same-start names,
zero-length ranges, overlapping label ranges, or any remaining function
overlap are ambiguous and fail. `OBJECT`, `TLS`, `SECTION`, and `FILE`
symbols, all GLOBAL/WEAK `STT_NOTYPE` symbols, undefined/absolute labels, and
empty names are never candidates. A `.L*` compiler temporary is eligible
only when the reachable objdump operand names that exact symbol byte-for-byte
and its displayed `+offset` exactly reconstructs the decoded target.

A candidate becomes a local executable-label island only when an actual
reachable direct branch/call target lies inside its already bounded range.
If the target already belongs to a canonical function, ordinary function
ownership wins and preserves its function-relative offset; no island is
fabricated. Otherwise candidate selection must be unique, and a `.L*` target
must satisfy the exact-name rule above. The cap is never relaxed to capture a
target, and an unmatched target is never assigned to a nearby or unrelated
containing function. DWARF decoded-line rows seed only aligned addresses
strictly inside a canonical function range; they do not independently seed
label islands. `end_sequence` and one-past-end rows are ignored.

Function entries and filtered line rows first seed structural code discovery;
only a seed or a proven successor can promote an objdump row to an
instruction. Dataflow then runs on that discovered structure and may add a
resolved indirect successor only when it remains inside a proven function
range. Entry seeds use the defined ABI entry state; every general register,
`mach`, `macl`, and fixed `r15` spill slot starts `UNKNOWN`. Each non-entry
line seed is a
disconnected code-discovery seed with the same all-`UNKNOWN` state, never a
copy of state from an earlier linear row. These disconnected seeds make C
switch case blocks discoverable without inheriting stale literal-target
registers across jump tables. Arbitrary decoded objdump rows never become
code merely because they exist. A function with no decoded-line rows may
still be proved from its entry CFG, but any unresolved indirect transfer or
unknown instruction effect in that function fails closed.

Successor construction distinguishes fallthrough, `bt`/`bf`,
`bt/s`/`bf/s`, `bra`, `bsr`, `rts`/`rte`, `jmp`/`jsr`, and
`braf`/`bsrf`, and executes the one SH delay-slot instruction before applying
every delayed transfer. Direct call targets owned by a canonical function
retain the exact nonzero function-relative target offset. A reachable `bsr`
to a label island is instead a summary-style intra-owner implementation
transfer. For a call at `PC`, execute its delay slot exactly once;
architecturally `PR=PC+4`. Independently schedule the caller continuation at
`PC+4` with the post-delay state after applying the ABI caller-clobber
abstraction, and enqueue the exact island target with the post-delay
callee-entry state before those return clobbers. The island keeps the
originating canonical caller for attribution and is not a closure
caller/callee node.

Calls from the island to ordinary functions or helpers are recorded under
that originating owner with an island-relative site. If island A `bsr`-calls
island B, the same split schedules A's own `PC+4` continuation with clobbers
and independently enqueues B with the same canonical origin. Each call site
schedules its own continuation even when island-body analysis is memoized.
Island entry states merge conservatively by
`(origin owner, island label, exact entry address)`; internal program-point
states join/widen by `(origin owner, island label, instruction address)`.
Because no return state is propagated, these keys need no return context,
and A-to-B-to-A cycles terminate under the finite fixed point without
suppressing either caller continuation. A direct `bra` to an island uses the
same origin and post-delay state but replaces ordinary fallthrough; it is not
given a synthetic return.

Only the island entry and successors proved by the same SH CFG rules become
instructions; the analyzer never linearly scans the island's bounded bytes.
Literal halfwords inside the range therefore remain data. An island `rts`
executes its delay slot exactly once and terminates that island path. It never
propagates callee abstract state to any caller continuation; that continuation
was already scheduled syntactically at its own `bsr`. `PR` therefore need not
be a value in the abstract lattice. Unresolved island effects/transfers fail
exactly like unresolved function code.

The finite abstract domain for each general register, `mach`, `macl`, or fixed
spill slot has
`UNREACHED` as bottom, `UNKNOWN` as top, a `ConstSet` of integer constants or
symbol-address atoms, and a typed 32-bit `Interval(signed|unsigned, lo, hi)`.
Numeric `ConstSet` values carry the same signed/unsigned interpretation tag;
symbol atoms are never coerced into numeric intervals. Identical atoms
survive a join. `ConstSet` joins are exact through 256 members; an oversized
all-integer set becomes its same-signedness interval hull, while an oversized
symbol set, mixed symbol/integer set, or incompatible signedness becomes
`UNKNOWN`. Interval joins use their hull, and a compatible integer
`ConstSet` joins an interval by hull; every other combination becomes
`UNKNOWN`.

Branch refinement intersects a `ConstSet` or interval with the proved
signed/unsigned predicate and discards an empty path. There is no later
narrowing phase. At a CFG backedge, the first expansion joins normally; on
the next expansion, widening sends each expanding lower or upper bound to
the corresponding signed or unsigned 32-bit minimum or maximum. Each bound
widens at most once, so the finite CFG and finite product domain terminate.
The work list applies join/widening per program point until no state changes;
`UNREACHED` locations are not scheduled.

Only affine constant operations propagate numeric facts. Unsupported
arithmetic kills its destination to `UNKNOWN`; if a transfer needs that
value, the audit fails closed. A computed jump is enumerated only when its
post-refinement `ConstSet` or interval represents at most 256 two-byte-aligned
targets and every target belongs to owned executable code. A larger,
misaligned, non-enumerable, or non-owned target set emits
`unresolved_transfer`. Modeled PC-relative loads, register moves, and
spill/reload operations propagate their source facts. Every other recognized
register-writing instruction kills its destination. `jsr`, `bsr`, and
`bsrf` resolve the pre-delay target, execute the delay slot once, and schedule
their syntactic `PC+4` continuation from the post-delay state after setting
`r0-r7`, `mach`, and `macl` to `UNKNOWN`; unwritten `r8-r15` survive. `PR`
is an architectural `PC+4` side effect, not a lattice value. An unparseable
destination or unknown register effect emits
`unresolved_effect` and rejects an audited closure. It never falls back to
linear scanning.

Resolved indirect tail calls and computed switch destinations become
successors. A missing line table is therefore acceptable only when the
entry-seeded walk completes with no unresolved diagnostic.

Synthetic tests independently mutate a fake pool `bsr`, the
`_guLookAtReflectF`-style pool register clobber, a real internal-offset
`bsr`, ordinary and delayed conditional branches, unconditional branches,
call/return delay slots, a resolved indirect tail call, and the
`mova`/indexed-load/`braf` switch form present in the audited closure. Each
mutation must distinguish data from code without dropping a real edge.
Additional fixtures cover stale-value merges, the caller-clobber set, an
unmodeled destination kill, an unresolved effect, disconnected line seeds,
loop-carried widening and convergence, signed/unsigned branch refinement, an
oversized unresolved interval, bounded jump-table enumeration, zero-size
functions, same-start aliases, ambiguous overlaps, `end_sequence` rows, and
an absent line table. Local-label-island fixtures model the real
preceding-`div0` zero-size LOCAL/NOTYPE symbol and its three `+offset`
targets and helper attribution to the originating function. They also call
one island from two distinct `bsr` PCs and require both `PC+4` continuations;
exercise nested A-to-B calls with A's continuation; terminate an A-to-B-to-A
cycle; execute every call/`rts` delay slot exactly once; prove island `rts`
state leaks into neither caller; and skip literal data inside a bounded
island. Negative fixtures reject data/nonlocal/unnamed labels, ambiguous
aliases/overlaps, and unrelated containing-function mapping.

Task 3 captures legacy observations first, then corrected observations from
the same route-0 and route-1 pure-layout ELFs built in a clean detached
worktree at the parser/test commit. That worktree is retained through
observation, independent review, v2 finalization, and evidence commit; no
partial SCC/cart/camera transport file or diff may exist in it. A
deterministic comparator requires the corrected closure, normalized
direct-call facts (including owner/island-relative sites and callee-relative
target offsets), implementation-island transfers, helper facts, and helper
total to be identical across the two layout-only variants. It records both
legacy and corrected totals, complete corrected
closure/direct/helper/implementation-island arrays, complete sorted
added/removed facts, the parser base/commit/range, the pre-build isolation
checks, and hashes of every input.
All four observations, the equality proposal, and the review record are
committed evidence, not scratch-only inputs. The review record fixes the
parser commit, reviewed commit range, and complete reviewed-file inventory.
Only after that equality report receives an independent clean review may the
implementer change the single v2 `EXPECTED_TOTAL` line and install its new
SHA-256 pin. The corrected parser, tests, reviewed inputs, historical
verifier bytes, and re-pinned contract are committed first as the exact
`repin_source_commit`; the durable final report is generated and committed in
a subsequent commit because a commit cannot contain its own SHA. The report
hashes every committed input and every historical source byte via
`git show <repin_source_commit>:<path>`, and requires that source commit to be
an ancestor of the report and current commits.

Later `verify-final` runs the current verifier's v2 analysis against the
retained route ELFs and compares its full closure/direct/helper facts with the
frozen corrected observations. Thus Task 8 may change current verifier bytes
to add v3 while it must preserve v2 behavior; current byte equality with the
historical Task 3 verifier is neither expected nor required. The comparator
helper and its compatibility tests remain byte-identical to their historical
`repin_source_commit` versions through Task 15; they are the stable
historical/current bridge and are not a Task 8 extension surface. Dirty,
substituted, non-ancestor, or hash-mismatched evidence fails. The contract and
parser are re-pinned exactly once and then frozen for the rest of the sprint;
a later behavioral mismatch is a regression, not permission for another
quiet re-pin.

Disabling the post-link audit is forbidden because object or source checks
cannot prove the linked closure. Rejecting all `+offset` targets is forbidden
because it removes the real internal-entry calls above. Broadening ordinary
ownership to all `STT_NOTYPE` symbols, linearly scanning label ranges, or
mapping `div0+offset` to an unrelated containing function is equally
forbidden. Pinning route 1's inflated total is forbidden because it preserves
layout-dependent false facts and the route-0 register-clobber false negative.

## Static and performance evidence

The corrected and deliberately re-pinned simulation native-math audit v2 is
the baseline for all subsequent work. The final route-1 Q ELF is first linked
without invoking the unavailable v3 gate. Task 14 then
generates/reviews/commits a hash-pinned v3 contract, selects it only for that
role, and runs `make verify`; the ELF hash must remain unchanged before any
capture. Any later target-affecting change regenerates v3 and invalidates all
captures. The contract:

- has a lower exact helper total than v2;
- keeps `_atan2_lookup` and `_atan2s` forbidden;
- declares exact roots for the two `camera.c` seam wrappers,
  `saturn_camera_q_default_tick`, `saturn_camera_q_lakitu_tick`,
  `saturn_camera_q_next_lakitu_state`, and the publish bridge;
- generates and hash-pins the complete transitive caller set reachable from
  those roots, stopping only at the named floor/ceiling/collision bridge
  functions and the named cold float-fallback bridge;
- records raw-object and canonical `sh-elf-objdump -dr` SHA-256 values for
  the `camera`, `math_util`, `saturn_camera_q`, and
  `saturn_camera_q_math` objects from a generated four-row object manifest;
- requires zero audited helper edges in every generated closure caller,
  including the `camera.c` wrappers;
- records each stopped bridge in an exact allowlist with its reason and
  per-symbol helper count; an independent code-owned map fixes every stopped
  wrapper's maximum direct soft-float/libm/64-bit helper count at zero, so a
  bloated bridge is rejected before generation and cannot become its own
  baseline;
- requires the cold fallback bridge to remain linked for rollback safety but
  proves from raw target counters that it executes zero times after the
  acceptance route's first successful Q seed;
- rejects a non-shrinking global total, a missing root/caller, or a caller
  removed only from the audit manifest;
- includes mutations that restore a helper edge, drop a closure caller, and
  inflate a bridge count, proving each failure is caught. The verifier owns
  the exact six-root, nine-stop, and two-forbidden-symbol sets independently
  of the generated contract, so deleting one directive or inserting a new
  stop cannot weaken coverage;
- emits a deterministic JSON report only after the ELF, contract, closure,
  and all four object records pass. Route-0 Q uses a separate
  object-reference-only mode that compares both its object manifest and the
  captured route-1 reference manifest with those same four pinned records;
  ordinary v3 verification never relaxes the route-1 ELF hash.

For each role, two target runs preserve the same-paused-instance raw SBR4
window and record `frame_serial`, `sim_frt_ticks_accum`,
`render_frt_ticks_accum`, `render_frt_ticks_last`, `master_wait_ticks`,
`slave_busy_ticks`, `slave_jobs_completed`, `slave_timeouts`, host wall time,
observed VBlank rate, and emulator speed ratio. Baseline run 1 pairs with Q run
1 and run 2 with run 2; both comparisons must show a lower Q-variant
simulation accumulator. Wall time is comparative emulator evidence only and
is not a retail claim.

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
- Q SQT1 magic/version/dispatch/route/tick disagreement or a nonzero baseline
  SQT1;
- same-image baseline/Q comparison;
- decoded data that disagrees with raw bytes.

The existing comparator/contracts remain unchanged and their tests keep
running. Camera-specific raw comparators, map gates, and audit-v3 selection
are additive. Variant 1 uses the corrected, re-pinned audit v2; the exact
route-1 Q artifact uses its hash-pinned v3; route-0 Q proves its Q closure
objects match the audited route-1 objects before the remaining target
verification runs.

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

The executable 15-task plan is
`docs/superpowers/plans/2026-07-29-saturn-camera-q-seam.md`. Its reviewable
increments are:

1. route/build-role configuration, pure SCC1/SBR4 host contracts, the
   reviewed linked-ELF parser correction and one-time v2 re-pin, target
   transport, and HWRAM/LWRAM reclamation gates;
2. generated writer closure plus exact SCR1 range/quiescence capture;
3. candidate arithmetic, full production differential, and only then the
   frozen Q config;
4. audit-v3 tooling, persistent shadow, Lakitu slice, pure default core,
   typed eight-bridge phase machine, and one-publish integration;
5. v3 pinning before capture, two runs per role, original-route regression,
   performance pairs, final evidence, and independent review.

Each increment receives its own implementation report and independent review.
No behavior-changing camera increment begins until the additive raw transport,
writer closure, and provisional range candidates are committed. Production
variant 2 remains unbuildable until the differential freezes the final config.

## Acceptance checklist

Task 3 is complete only when all of the following are true:

- The delay-slot-aware linked-ELF analyzer rejects literal-pool decodes,
  preserves real internal-offset calls, reports no unresolved transfer or
  effect in the audited closure, admits the exact reachable `div0` local-label
  implementation transfers without making `div0` a closure node, and
  produces identical corrected route-0/route-1
  closure/call/helper/implementation-transfer facts.
- V2 is re-pinned exactly once after the equality report and independent
  review; the durable report names an ancestor `repin_source_commit`, records
  old/new totals and digests plus sorted added/removed facts, validates its
  historical bytes, and proves the current verifier still produces the
  frozen v2 facts.
- The bounded default/Lakitu Q island is active only for camera variant 2.
- `bob-parity-v1` remains immutable; raw route ID 2 and the compound
  Mario/default-dispatch witness bind all SCC1 acceptance evidence to
  `bob-default-camera-v1`.
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
- The fixed SCC leaves at least `0x4000` LWRAM; the selected cart staging size
  passes READY/copied-size/hash evidence; final HWRAM leaves at least
  `0x1B00`.
- Saturation, overflow, divide, unexpected-reseed, and range-fallback counters
  are zero.
- Evidence records source pins, licenses, inspected files, and reuse modes.
