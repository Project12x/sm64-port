# Saturn engine-port architecture contract

This document prevents the Castle-lobby visual slice from becoming a second,
disposable game engine. The finished Saturn target must continue to run the
inherited SM64 game, level, object, camera, animation, and behavior code. The
Saturn work supplies platform services, an asset compiler, and a rendering
backend beneath that code.

## Non-negotiable ownership rule

The original engine owns authoritative game state. In the production target:

- `game_loop_one_iteration()` advances the game;
- `level_script_execute()` owns level and area transitions;
- the original object and Mario action loops own movement, jumping, collision,
  animation selection, and behavior;
- the original geo-layout traversal selects actor and level display lists;
- `exec_display_list(struct SPTask *)` is the handoff from game code to the
  Saturn graphics front end; and
- `ControllerAPI` supplies an `OSContPad` without changing SM64's input model.

Saturn-specific code may cache, lower, schedule, clip, sort, or reduce the
fidelity of a submitted render workload. It must not become the authoritative
implementation of Mario actions, camera behavior, object behavior, level
scripts, or scene transitions.

The current `castleviewer` target is therefore a bring-up harness. Its manual
position, jump, camera, animation-frame, scene traversal, and frame-loop state
are temporary diagnostics, not production interfaces.

## Proven internal boundary

This contract follows the inherited port's existing target split rather than
inventing a parallel application architecture. The relevant code inspected is:

| Existing source | Boundary retained for Saturn |
| --- | --- |
| `src/pc/pc_main.c` | platform startup, `produce_one_frame()`, `exec_display_list()`, and the original game-loop call |
| `src/game/game_init.c` | controller read, audio tick, graphics-pool selection, level-script execution, display, and VSync ordering |
| `src/pc/controller/controller_api.h` | target controller backend terminating at `OSContPad` |
| `src/pc/audio/audio_api.h` | eventual target audio transport boundary |
| `src/pc/gfx/gfx_pc.c` | Fast3D state interpretation and dynamic display-list submission model |
| `src/pc/gfx/gfx_rendering_api.h` | separation of graphics front end from hardware command emission |

The inherited PC renderer's source-only redistribution terms are preserved in
`src/pc/gfx/LICENSE.txt`. It is an architectural reference inside this tree,
not code to relicense or silently copy into a new license domain.

## Target layers

```text
Original SM64 source
  game loop / level scripts / objects / actions / collision / camera / geo graph
                              |
                     exec_display_list(SPTask)
                              |
Saturn Fast3D front end
  dynamic state + matrices + source display-list identity + generated IR lookup
                              |
Frame render queue
  world/actor jobs + transform cache + clip + material pass + bounded LOD
                              |
VDP back ends
  VDP1 command arena/cache/upload      VDP2 planes/HUD/fades
                              |
Yaul platform
  boot/VBlank/SMPC/timer/CD/cart/WRAM arenas/SCU transfer
```

The layers become directories as they are extracted; names describe ownership,
not a demand for wrappers around every Yaul call:

```text
src/port/saturn/platform/   startup, frame clock, memory arenas, CD, SMPC, cart
src/port/saturn/gfx/        Fast3D front end, frame queue, VDP1/VDP2 back ends
src/port/saturn/assets/     package loader, residency, texture/animation banks
src/port/saturn/audio/      AudioAPI implementation and SCSP transport
src/port/saturn/harness/    bounded probes such as Castle viewer and turntable
include/saturn/             versioned generated-IR and package contracts
tools/saturn/               source-to-IR, package, report, and disc builders
```

## Rendering contract

The runtime graphics front end consumes the display list submitted by the real
game frame. It interprets dynamic Fast3D state that cannot be baked—matrix
selection, geometry mode, combine/material state, texture selection, and
display-list calls—then resolves source display-list identities to generated,
source-traceable Saturn IR.

Generated IR is an optimization of original assets, not a replacement scene:

- every bank retains source level/area/actor, display-list, material, and
  primitive identity;
- world and actor banks use the same primitive/material records;
- static transforms, compatible triangle-to-quad merges, texture crops,
  quantization, LODs, bounds, and spatial groups may be generated offline;
- dynamic matrices, animation-selected limbs, camera-relative effects, and
  per-frame render state remain runtime inputs;
- every lossy tier is derived from the source bank and can be selected by a
  budget policy; and
- the VDP1 backend owns command construction, clipping, sorting, texture
  residency, and overflow-visible rejection. It never owns game state.

The current one-command Mario texture tier is valid only if represented as a
generated actor LOD selected by this common path. Castle-specific arrays or
branches in a future shared backend fail this contract.

## Frame and cadence contract

SM64 simulation remains a deterministic 30 Hz stream. Rendering may initially
present at 15–20 FPS, but low rendering throughput must not slow simulation or
change input semantics. The scheduler therefore owns separate counters for:

1. source update ticks;
2. render submissions accepted or intentionally skipped;
3. VDP1 completion wait;
4. VBlank presentation; and
5. audio production when audio is enabled.

An optimization is portable only when it reduces a named shared stage—source
update, geo/Fast3D front end, transform/clip, ordering, command build, transfer,
VDP1 wait, or residency—and does not move game behavior into the renderer.

## Asset-package and 4 MiB cartridge contract

The cartridge is a scene/area asset cache, not a heap extension and not a set
of linked Castle globals. Versioned packages use offsets rather than native
pointers so the same payload can live on disc, in WRAM, or in cartridge DRAM.
A package manifest contains:

- format version, source revision, level/area/actor identity, and content hash;
- sections for geometry IR, materials, texture/CLUT data, animation, collision,
  bounds/spatial data, and source-ID maps;
- byte size, alignment, destination tier, and peak staging requirement for
  every section; and
- full/fallback/LOD relationships without duplicating authoritative source
  identity.

The loader performs `CD -> internal-WRAM staging ring -> cartridge bank` and
resolves a read-only package view. Per-frame transform, collision-query, sort,
and command data stay in named internal-WRAM arenas. A missing cart may use a
bounded diagnostic fallback, but a release build must fail visibly when its
declared 4 MiB target cannot be satisfied.

The current hard-coded Castle collision address and startup copy of linked
texture arrays are migration scaffolding. They must become named arena and
package-loader clients before M5 closes.

## Harness retirement ledger

| Current Castle harness responsibility | Production owner | Retirement proof |
| --- | --- | --- |
| manual Mario position, velocity, jump, and floor response | original Mario action and collision code | recorded run/jump trace agrees with PC source build |
| manual following camera | original camera code | identical camera mode and checkpoint state for a deterministic route |
| manual animation-frame selection | original geo/animation state | idle/run/jump limbs selected by the submitted source graph |
| selected Castle root mask | generic geo/Fast3D front end | a second area and an object actor submit without target-specific root tables |
| Castle/Mario combined sort arrays | shared frame render queue | world plus multiple actor/effect jobs use one bounded queue |
| linked texture arrays copied to cart at startup | package loader/residency manager | unload/reload two area packages without stale references |
| fixed LWRAM collision pool address | named platform memory arena | collision allocation is budgeted per area and checked for overflow |
| harness-owned frame timing | platform scheduler/telemetry | 30 Hz source update remains stable while render frames are skipped |

## Migration gates

### E0 — Shared types without behavior change

Extract frame timing, camera/transform records, render-job records, command
arena, and memory-arena declarations from `castleviewer/main.c`. The harness
must build against them with pixel-identical deterministic output.

Implementation checkpoint (2026-07-19): the scene-neutral
`gfx/saturn_transform.h` and `platform/saturn_frame_profile.h` records are now
used by the Castle harness. Their native C contract test covers integer camera
transforms, Q16 normalization, phase totals, and zero-safe rate calculation.
The bounded `platform/saturn_memory_arena.h` now also owns alignment, peak
usage, and overflow state for the Castle collision pool without owning its
backing address. Render-job and command-arena extraction remains open. The
target pixel-identity capture remains required after the Yaul build environment
is available; a host-only pass does not close E0.

### E1 — Shared renderer client

Make Castle and Mario submit independent source-identified jobs to the common
render queue. The turntable and Castle harness must use the same backend and
texture residency code. No backend symbol may contain `castle` or `mario`.

### E2 — Real source frame handoff

Boot enough of the original target loop to call `game_loop_one_iteration()` and
receive its `SPTask` through Saturn `exec_display_list()`. Replace manual Mario
input/actions/camera/animation in the visual slice with source-owned state.
This is the required next gameplay milestone; adding more handcrafted actions
does not satisfy it.

### E3 — Versioned area package

Load Castle Area 1 through the offset-based package API, including collision
and actor dependencies. Unload and reload it under a deterministic soak with
arena, cartridge, VDP1 texture, and source-ID validation.

### E4 — Generalization proof

Run one additional Castle area or bounded Battlefield section plus one dynamic
object actor through the same game-loop, package, and render paths. Any new
scene name in shared runtime code blocks the gate.

### E5 — Harness deletion test

The production target links without `castleviewer/main.c`, its collision-pool
stubs, or its graph-root table. Harnesses remain useful test executables, but
deleting them cannot remove a production capability.

## Change-review checklist

For every Saturn runtime or optimization commit:

1. name the source-engine boundary it preserves;
2. name the shared stage and measured budget it changes;
3. state whether data is authoritative source state, generated IR, hot runtime
   state, or cold package data;
4. reject scene/actor-specific logic from shared modules;
5. keep source IDs and a deterministic reference comparison;
6. record pinned upstream source, license, files inspected, and reuse mode;
7. capture visible before/after evidence for rendering changes; and
8. state which harness responsibility moved closer to deletion.
