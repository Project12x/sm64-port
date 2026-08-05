# Saturn engine-port architecture contract

> **Current render-pipeline architecture:** The approved one-frame-overlapped
> dual-SH2/VDP design and its live decision/status ledger are maintained in
> [`../superpowers/specs/2026-08-03-saturn-overlapped-render-pipeline-design.md`](../superpowers/specs/2026-08-03-saturn-overlapped-render-pipeline-design.md).
> This engine-port contract remains authoritative for source-game ownership;
> the linked document defines how the Saturn renderer consumes snapshots
> without duplicating the original Fast3D construction path.

This document prevents the Castle-lobby visual slice from becoming a second,
disposable game engine. The finished Saturn target must continue to run the
inherited SM64 game, level, object, camera, animation, and behavior code. The
Saturn work supplies platform services, an asset compiler, and a rendering
backend beneath that code.

## Supported-target scope

**Owner decision — 2026-08-03.** This repository is Saturn-exclusive. PC and
N64 builds are unsupported and their compile compatibility is not a gate.
Reviewers may flag non-Saturn behavior only when it affects Saturn code or
host test tooling. Historical PC/N64 references remain source/provenance
context, not supported build contracts.

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

## Source-authority rule

**Owner decision — 2026-07-27.** The project owner approved retiring the old
rule ("proceed as recommended"), with the stated reasoning: "I approve the
engine tree rule, because I think we are hitting the limits of what we can do
without more saturn shaped rewrites." Recorded per this project's decision
convention (see the Z-Treme decision in `THIRD_PARTY_LICENSES.md` for the
format), so the change reads as deliberate policy, not drift.

The former shorthand that the engine tree must remain completely unmodified is
retired. It described a useful preference, not the actual invariant: the
current target already contains narrow `TARGET_SATURN` seams, and a partial
fixed-point island can pay more boundary-conversion cost than it saves.

The production rule is now:

1. SM64 gameplay state, object behavior, level scripts, camera behavior,
   animation semantics, collision, and source assets remain authoritative.
2. Prefer Saturn changes in `src/port/saturn/`, `tools/saturn/`, generated IR,
   and the established graphics, audio, input, and platform boundaries.
3. A change in `src/engine/`, `src/game/`, `levels/`, `actors/`, `lib/src/`, or
   an inherited header under `include/` is allowed only when measured evidence
   shows that a port boundary is insufficient or imposes material runtime
   cost. New target-owned contracts under `include/saturn/` remain part of the
   port layer.
4. Every such exception must be minimal, guarded by `TARGET_SATURN`,
   scene-neutral, documented in the implementing plan or handoff, and covered
   by a differential or behavior test against the original path.
5. Saturn-generated fidelity tiers are reproducible derivatives of the source
   assets. Hand-edited replacement content is not authoritative source data.
6. A target guard may change representation, precision, residency, or
   scheduling. It may not silently change game rules or make a Saturn-only
   scene script the owner of progression.

**A1 enforcement note — 2026-08-03.** `geo_process_root()` is not a
construction-only boundary in this source tree. Its walk advances animation
state and invokes geo callbacks that mutate painting/warp, environment-water,
moving-texture, flying-carpet, camera, and matrix-derived object state.
Suppressing the whole walk is blocked until those mutations have an audited,
behavior-tested state-only path that does not construct source display lists.
Safety commit `77ee306c` enforces that boundary by removing all sourceboot
calls to the reserved scene-graph suppression setter. Accepted Saturn builds
therefore retain the full source geo walk while final display submission alone
may be suppressed at the target presentation seam.

This is a narrow-delta policy, not permission for a broad engine rewrite. The
PS1 port remains useful evidence for fixed point, compact display lists, and
asset preparation, but its wide source fork is not the Saturn integration
model. Older handoffs, completed plans, and design records that say “the engine
tree stays unmodified” describe the constraint of those historical tasks; they
do not override this current contract.

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
src/port/saturn/gfx/        Fast3D front end, shared IR transform/texture jobs,
                            frame queue, VDP1/VDP2 back ends
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

SM64 simulation targets a deterministic 30 Hz stream. Rendering may initially
present below that rate, but overload must not trigger unbounded catch-up or
change input semantics. A presentation-generation lifetime may execute at most
one normal source tick plus one recovery tick; further real-time credit is
dropped explicitly and counted until source simulation itself is optimized.
This is an overload degradation rule, not a claim that sub-30-Hz simulation is
the final shipping cadence. The scheduler therefore owns separate counters for:

1. source update ticks;
2. render submissions accepted or intentionally skipped;
3. VDP1 completion wait;
4. VBlank presentation; and
5. audio production when audio is enabled.

The A9 field-resolution trace proved why this lifetime scope is mandatory. On
exact ELF `1ffb47cc...e4edfe6`, the old per-outer-iteration cap reset three
times before one presentation: six simulation ticks consumed 283 of 333
measured fields (85.0%), construction consumed 49, transport/presentation one,
and zero fields were unattributed. Nine intervals also dropped 222 credits.
Scheduler budgets therefore reset only after a new complete frame is
presented, never merely because the outer loop observes another VBlank.

**A9A lifetime correction — Fix Round 4 source-repaired, 2026-08-05.**
The accepted A9 adapter formerly constructed one frame synchronously. A9A
splits generation `N` into a
notify-only start and a positive-retirement poll/finalize. While `N` is
PENDING, the renderer retains its immutable snapshot, descriptor payloads, and
BUILDING command/Gouraud source bank; the master may run the one queued
authoritative source tick for `N+1`, but `N+1` cannot become an active render
until `N` retires and publishes. Exactly one render generation is active.

Only after positive slave retirement may the master drain remaining work,
validate and stably merge results, reserve Gouraud state, lower VDP1 commands
once, and retire `N`. Failure quarantines `N` and preserves the previous
complete frame without a full-frame replay. Simulation, input, live state,
allocation, final ordering, VDP1/VRAM, VDP2, and presentation remain master-
owned. A8 retains transfer ownership, and A9's nonzero wrap, two-field 30 Hz
remainder, normal-plus-recovery budget, field epochs, previous-frame reuse,
and exact publish acknowledgement are unchanged. Generic lifecycle state has
no BOB, Mario, Castle, or demo-renderer dependency. Focused host/source and
versioned-cadence tests are green; review and target evidence remain open.

Independent A9A review exposed one exception to that immutable-lifetime claim:
the N+1 source tick could reset the global LOD tier/cluster state while N's
slave lower job still used it. The renderer now defers those scene resets in a
generation gate and applies them only after exact N retirement.
The small generation gate is a linker-owned P2 `.uncached` object. The bulk
primitive-tier and cluster-LOD arrays share one `.lwram_bss` object; both SH-2s
use the same cache-through P2 accessor, so the slave never consumes an ordinary
cached master static. Keeping those bulk arrays uncached had pushed the exact
reviewed ELF `0x40D0` bytes beyond physical HWRAM while the old linker margin
subtraction underflowed. The linker and ELF verifier now reject HWRAM/LWRAM
upper-bound overflow before applying their margin floors. A shared scene-
neutral overlap controller is stamped at the runtime notify and positive-
retirement release sites; its phase record is published before slave wake or
retirement visibility. Terminal telemetry scans quarantine state before queue
reset. A production-linked fixture combines active deferred scene state with
failure quarantine, proves reset ordering and nonzero `QQ`, and catches late-
marker plus wrong-generation mutations. Fresh source rereview and a repaired
target build remain required before placement, boot, or performance evidence.

At that terminal boundary, VDP2 composes only the immutable sky camera carried
by the displayed VDP1 bank and a small displayed/rendered/simulation generation
record. The HUD names those exact generations; camera/bank or
displayed/rendered disagreement fails closed before VDP2 state changes. This
keeps VDP2 geometry-free while making any bounded simulation lead visible
rather than silently pairing new metrics with an older framebuffer. A changed
generation record bypasses the HUD's normal metric refresh interval so the sky
and its displayed tuple always change together.

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

### Direct-source compatibility bank

Before generated Saturn IR has replaced every original static data reference,
the direct source-game bootstrap needs a narrower compatibility mechanism.
Original SM64 source tables contain native pointers to model/display-list data,
behavior callbacks, and animation records; an offset-only IR package cannot be
substituted for those symbols transparently. The bootstrap therefore builds a
separate, read-only source-data image linked for the detected cart address.

The executable remains a compact HWRAM first-read binary. At boot it validates
the required `DRAM_CART_ID_4MIB`, reads the named source-data file from the
disc through a bounded WRAM staging ring, copies it to the cartridge, and only
then enters `thread5_game_loop()`. The first implementation validates exact
linked file size; it must add content-hash/header validation before accepting
the transfer as integrity-proven. The final code link imports the source-data
image's symbols without embedding its bytes in the first-read binary. This is
a compatibility bridge, not permission to use cartridge DRAM as a heap or to
make new scene-specific globals.

Implementation checkpoint (2026-07-19): `sourceboot-cart.x` now places
original immutable source data in `.cart_rodata` at `0x22400000` and the
sourceboot Makefile emits it as `SOURCE.DAT`. `source_cart.c` performs the
CDFS-to-16-KiB-HWRAM-stage-to-cart lifecycle before the original game loop,
and exports a linked work-RAM loader probe for Ymir/hardware diagnostics. This
is package/build proven; visual E2 stays open until that exact source loop
submits real VDP1 work.

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

### A5.8 render-job cutover boundary (active)

The A5 graph runtime will replace the existing range worker only as one
atomic transition. Before that point, a terrain or actor callback must prove
that its descriptor is the queue's current exact claim, publish the matching
output-bank release, and derive every physical payload address from that
descriptor's `output_offset`, `output_capacity`, and recorded claimant lane.
Range begins, static slave splits, and source-order position cannot select a
payload cache alias. The A5.8 terrain binding seam implements this rule but is
not registered with CPU-DUAL or called by the default frame path. Mario must
gain the same producer/read contract before graph runtime activation; master
retains final VDP1 lowering and painter ordering throughout.

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
backing address. `gfx/saturn_render_queue.h` now carries source-bank,
source-primitive, generated-IR, depth, kind, and pass identity through one
bounded queue; Castle world tiles and Mario actor primitives are its first two
clients. `gfx/saturn_command_arena.h` now owns the bounded VDP1 command
capacity, fixed setup prefix, live upload length, prior END slot, peak use, and
visible overflow state. Castle is its first backend client and initializes the
two Yaul setup commands once. The native SH-2 build and deterministic Ymir
capture exactly match Stage 115's framebuffer and PNG hashes. E0 is closed;
E1 is the active migration gate.

### E1 — Shared renderer client

Make Castle and Mario submit independent source-identified jobs to the common
render queue. The turntable and Castle harness must use the same backend and
texture residency code. No backend symbol may contain `castle` or `mario`.

Implementation checkpoint (2026-07-19): `gfx/saturn_vdp1_backend.h` now owns
the pinned-Yaul persistent command-list lifecycle for both targets, while
`gfx/saturn_texture_residency.h` provides one capacity-checked SCU transfer
destination for linked, staged, and eventual package-loaded texture sources.
Both SH-2 targets build and boot through these scene-neutral symbols. Castle's
ISO and deterministic framebuffer remain byte-identical to Stage 116. E1
still requires the turntable to submit source-identified render jobs through
the common queue rather than its older private depth buckets.

### E2 — Real source frame handoff

Boot enough of the original target loop to call `game_loop_one_iteration()` and
receive its `SPTask` through Saturn `exec_display_list()`. Replace manual Mario
input/actions/camera/animation in the visual slice with source-owned state.
This is the required next gameplay milestone; adding more handcrafted actions
does not satisfy it.

The target must preserve the source-frame ownership visible in the inherited
PC path: platform input enters `OSContPad`; the source game/level/object loop
advances; the submitted display work crosses `exec_display_list()`; and the
Saturn backend lowers it to VDP1/VDP2 work. Audio may be a temporary no-op
transport for the visual slice, but it remains a source-frame boundary rather
than a renderer responsibility. This is the architecture lesson from the PS1
port study, not PS1 code reuse; see
[`PSX_PORT_ARCHITECTURE_LESSONS.md`](PSX_PORT_ARCHITECTURE_LESSONS.md).

Implementation checkpoint (2026-07-19): `game_init.c` now has a
`TARGET_SATURN` platform seam for controller collection, source-frame audio
tick accounting, `exec_display_list()` submission, and VBlank presentation.
`runtime/saturn_source_runtime.c` provides those services without changing
source loop ownership. `gfx/saturn_fast3d_frontend.c` is the first shared
consumer: it walks a submitted Fast3D display-list graph with command and call
depth bounds, retaining source workload telemetry but not yet lowering a task
into VDP1 commands. The Castle harness registers the consumer only so the
eventual source loop has no scene-specific submission path. This checkpoint is
build-proven, but E2 stays open until a Saturn target boots the source loop and
replaces the harness-owned movement/camera/animation state.

The N64 queue implementation in `src/game/main.c` is explicitly excluded for
`TARGET_SATURN`; its public `exec_display_list()` symbol is supplied only by
the target runtime. This prevents a later full-source link from accidentally
using the N64 SP/DP task queue instead of the Saturn front end.

The current disc calls a one-command, non-visual `G_ENDDL` preflight through
that public dispatcher at boot. Its only purpose is to retain and verify the
actual source-task path while E2 is not yet driving the loop; it neither draws
nor introduces an alternate game/render path.

E2 acceptance additionally requires a deterministic input/state replay: at
least one idle/run/jump route records source action, Mario position, camera
mode, and final state against the PC reference. A post-run screenshot alone
does not prove source-owned locomotion.

### E3 — Versioned area package

Load Castle Area 1 through the offset-based package API, including collision
and actor dependencies. Unload and reload it under a deterministic soak with
arena, cartridge, VDP1 texture, and source-ID validation.

The package is the target equivalent of a prepared level working set. It must
batch geometry, texture/CLUT, and animation-bank residency by area rather than
performing per-texture loads in a frame. The 4 MiB cartridge stores cold,
read-mostly banks; hot decoded metadata, transforms, sort keys, and command
patches remain in bounded internal-WRAM records.

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
