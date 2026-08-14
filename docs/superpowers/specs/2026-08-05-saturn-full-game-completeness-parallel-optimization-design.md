# Saturn Full-Game Completeness and Parallel Optimization Design

> **Historical design record (superseded 2026-08-13).** It may explain existing code, but it does not authorize new work or define product progress. Current authority is `docs/saturn/PRODUCT_GOAL.md` and the active recovery plan.

**Date:** 2026-08-05  
**Status:** Approved; detailed implementation plan published
**Supersedes:** Task 10/A10 of the overlapped-render-pipeline plan as the active
umbrella sprint. Completed Task 10 commits and evidence remain valid inputs.

## Goal

Produce a representative, increasingly complete Saturn build that exposes the
entire source-selected Mario animation table, renders every dynamic family
required by Bob-omb Battlefield, preserves the original game's music and sound
effect semantics, and continues reducing unfinished renderer/runtime overhead
in parallel. The final sprint build must meet a measured 4.0 mean presentation
FPS gate with all required features enabled (the accepted observed range is
4--6 FPS). A later optimization
sprint targets 12--15 FPS on that representative workload.

BOB is the first complete coverage manifest and manual proving ground. It is
not a permanent engine boundary or the end goal. The same tools and runtime
contracts must make a second level materially easier to adapt and must remain
usable for the full game.

## Approved planning posture

The sprint uses one integration spine with four parallel lanes:

1. complete Mario animation;
2. generic BOB actor, enemy, object, hazard, collectible, and effect closure;
3. full music and SFX semantics over Saturn-native audio execution; and
4. renderer/runtime overhead reduction, inheriting completed Task 10 work.

Intermediate feature increments may temporarily reduce FPS. Each lane remains
independently selectable so the cost can be attributed. Performance
observations do not block intermediate feature merges. The all-features final
integration gate requires a measured mean presentation cadence of at least
4.0 FPS in the pinned comparative Ymir setup. "4--6 FPS" is the accepted
observed band, not a second subjective threshold.

## Source ownership

The inherited SM64 source remains authoritative for:

- input, Mario actions, physics, collision, camera, and level state;
- object spawning, behaviors, interaction, death, respawn, and collection;
- animation selection, animation frame progression, and geo-layout state;
- music and SFX requests, sequence identity, priority, fades, layering,
  interruption, and resume policy; and
- level scripts, macro objects, model/geo identities, and source assets.

Saturn-specific code consumes immutable state and performs presentation,
packaging, scheduling, and hardware transport. It must not recreate gameplay
or level policy. Shared runtime code may not branch on BOB, a specific enemy
name, or a hand-authored level path.

## Architecture

### Complete Mario animation

The Castle demonstration's private idle/walk animation clock is prior
prototype work, not the production owner. Sourceboot consumes the animation ID
and frame selected by the original game/geo state.

Offline generation emits one complete, versioned Saturn Mario animation bank
covering the entire source animation table. The bank records source hashes,
skeleton/channel metadata, frame bounds, vertex/material bank identities, and
maximum scratch requirements. Runtime evaluates only the selected pose for the
current immutable snapshot, then submits it through the existing actor
meshlet/job path. BOB-reachable states provide the initial test coverage, but
there is no BOB-only animation whitelist.

### Generic actor and BOB closure

A deterministic closure tool derives the required dynamic-family set from:

- BOB LevelScript object declarations;
- `MacroObject` presets;
- model and geo roots;
- behavior-spawned children, projectiles, particles, and rewards;
- referenced animation, material, billboard, translucent, and shadow features;
  and
- act-dependent variants and object lifetimes.

This closure includes ordinary enemies, King Bob-omb, hazards, platforms,
switches, gates, cannons, boxes, caps, coins, stars, 1-Ups, shadows,
billboards, particles, and other visible/interactable families needed for
ordinary BOB play. The generated report, not a prose list, is authoritative.

Each actor-family manifest records model ID, geo root, skeleton, animation
table, immutable geometry/material/meshlet data, opacity/effect requirements,
source hashes, maximum instance count, and exact cartridge/WRAM/VDP1 budgets.
Each live object produces a bounded immutable instance snapshot containing:

- family/model identity and snapshot generation;
- transform, scale, and render-active state;
- selected animation ID/frame and `animState`;
- opacity, billboard, shadow type/scale/solidity, and typed effect kind,
  parameters, flags, and lifetime;
- source-owned render-active, render-range minimum/maximum, and draw-distance
  state;
- held/parent node identity and offsets; and
- exact root-package generation plus actor-bank ID/hash.

Every admitted instance becomes a descriptor-owned actor job in the shared
render scheduling domain. Implementation planning exposed a capacity boundary:
the proven world/phase graph has eight descriptors and an eight-bit dependency
mask, so it cannot honestly represent the complete live BOB population.
Rather than widening every A5/A9 graph ABI, actor instances use a companion
bounded work-stealing queue with the same exact-generation/P2 publication and
claimant-owned-output laws. The existing eight-entry world graph remains
unchanged. The claiming SH-2 writes only its assigned output lane; the master
retains final painter ordering, VDP1 lowering, and publication. No family
receives a private CPU-DUAL callback or fixed master/slave split.

### Full music and SFX semantics

The sourceboot audio no-op is replaced by a semantic transport. Original
game-facing calls and identifiers remain authoritative. Offline tools compile
the complete source sequence and sample banks into a versioned Saturn audio
package without requiring continuous SH-2 PCM mixing.

The existing big-endian SH-2/MC68000 mailbox evolves to a versioned semantic
protocol. It carries sequence start/stop, player selection, fades, tempo and
layer changes, jingles, interruption/resume state, SFX start/stop, priority,
volume, pan, pitch, and bank-residency events. The MC68000 owns sequence
scheduling, voice allocation, pitch/envelope updates, and SCSP register
control. The SH-2 emits bounded semantic events from the inherited game state.

Music-control capacity is reserved and cannot be displaced by ordinary SFX.
When SFX pressure exceeds the bounded queue or voice budget, the lowest-
priority eligible SFX is dropped with telemetry. Invalid banks or a stalled
MC68000 mute audio and report a fault without blocking simulation or rendering.

The sprint must compile the full music/SFX content set and preserve the
original semantic interface. BOB supplies the first end-to-end behavioral
coverage: level music, fades/transitions, Mario action sounds, enemy/object
sounds, jingles, and interruption/resume. Future levels may select different
packages without changing the runtime audio backend.

### Renderer/runtime optimization lane

The existing Task 10/A10 source-contract work, A9A frame-lifetime overlap,
descriptor-owned shared queue, immutable snapshot banks, explicit VDP1/VDP2
ownership, CPU-DMAC/SCU-DMA transport, fixed camera, and native-math gates are
inherited rather than restarted.

The lane continues scene-neutral work in these categories:

- separate stateful geo updates from redundant display construction only
  after differential evidence proves state parity;
- admit world clusters and actor meshlets before transform/classify/lower;
- add per-level BSP/frustum/portal-window metadata without requiring a full
  arbitrary PVS system;
- batch actors/effects by compatible immutable family/material records;
- reduce VDP1 commands, Gouraud work, sort/merge work, and repeated memory
  traffic;
- improve opportunistic dual-SH2 job granularity and eliminate fixed joins;
- overlap command/Gouraud transfers and presentation with useful work; and
- make package residency and level transitions bounded and observable.

Each recorded gain identifies whether it reduced simulation, source graph,
render construction, VDP1 command count, transfer wait, or audio work.

## Scene packages and future-level adaptation

A scene-package compiler consumes a source level/area and produces one
versioned atomic Saturn root package, a content-addressed payload set, and a
human-readable closure report. "One package" means one S64P root and one
generation commit; it does not require every multi-megabyte asset to be
embedded or simultaneously resident. Inputs include
LevelScript, GeoLayout, macro objects, static geometry, collision, referenced
actor families, behavior-spawned children, textures/materials, animations,
music IDs, and sound banks.

The S64P root contains generic records:

- world/static geometry;
- collision;
- sky/background;
- BSP/portal metadata;
- sorted actor-payload dependency descriptors;
- sorted animation-payload dependency descriptors;
- sorted audio-payload dependency descriptors; and
- residency records: CD/cartridge location, WRAM destination, alignment,
  lifetime, dependency, and eviction policy.

Every dependency descriptor carries stable ID, kind, byte count, destination,
lifetime, maximum scratch, and SHA-256. The root hash covers the descriptors;
a dependency-set hash covers the sorted actor, animation, and audio hashes.
Root and all payload descriptors validate together. The root and its complete
feature-active payload set commit under one master-owned generation, or fail
closed without exposing a partial scene; inactive-feature descriptors remain
hash-validated but nonresident so the diagnostic matrix can attribute costs.
Generation N retains every resident render and voice payload until its render,
bank, and voice lifetimes retire.

The normal adaptation workflow is:

1. select a source level/area;
2. generate its dependency closure;
3. resolve declared unsupported generic actor/effect features;
4. build the scene/audio packages;
5. run deterministic host validation; and
6. load the packages through the same runtime, queue, and audio backend.

Whomp's Fortress is the second-level portability fixture because it supplies a
different world, moving platforms, boss behavior, and different actor
dependencies. This sprint need not make it fully playable. It must generate,
validate, load/unload, and reach the generic runtime without adding a level-
specific frame loop, renderer branch, animation evaluator, or audio backend.

## Feature identity and diagnostic matrix

The build exposes four independently selectable feature identities:

- complete source-selected Mario animation;
- generated dynamic actor/effect closure;
- semantic music/SFX runtime; and
- the reviewed renderer pipeline selector.

The authoritative candidate enables all four. Diagnostic builds cover:

1. renderer-only rollback baseline;
2. renderer plus complete Mario animation;
3. renderer, animation, and BOB actor closure; and
4. all features including music and SFX.

Every ELF/CUE identity records the exact feature tuple, source/root/payload
hashes, live/replay input mode, bootstrap ticks, route, camera route/variant,
route/input/camera artifact hashes, full cart profile hash and stage size, hot
promotion, clipping, BSP ordering, polygon/LOD tier, fragmentation mode,
diagnostic mode, and renderer pipeline. A canonical effective-config digest
covers all behavior-affecting values, and each field is independently mutation
tested. Intermediate
regressions are measured and attributed but are not arbitrary merge blockers.

## Integration waves

### Wave 1: foundation and truthfulness

- Preserve completed Task 10 evidence and make this sprint authoritative.
- Reconcile the dirty native-math renderer-oracle blocker.
- Add independent feature identities and package hashes.
- Preserve the current accepted 4--6 FPS CUE as rollback evidence.
- Generate the authoritative BOB closure report.
- Implement the S64P schema/compiler and synthetic/provisional fixtures only;
  do not claim a final BOB root before actor, animation, and audio payloads.

### Wave 2: complete Mario animation and audio control plane

- Activate the complete source-selected Mario animation table.
- Replace audio no-ops with bounded semantic event production.
- Extend and model the 68K/SCSP protocol for sequences, voices, fades, layers,
  and SFX.
- Reduce redundant source-graph and pose/geometry work in parallel.

### Wave 3: first integrated actors and audible BOB

- Bring representative opaque, articulated, translucent/billboard, and
  collectible families through generic manifests and instance snapshots.
- Play BOB music with correct start/fade/interruption behavior.
- Route Mario and initial enemy/object SFX from original gameplay calls.
- Reduce per-instance transform, ordering, and VDP1 command costs.

### Wave 4: complete BOB object/effect closure

- Resolve every family and feature declared by the generated BOB closure.
- Link/reseal the final BOB root from the completed actor, animation, and audio
  payload hashes, byte counts, lifetimes, and scratch limits.
- Complete the BOB music/SFX semantic coverage report.
- Add broad actor batching, early visibility rejection, effect budgets, and
  bounded package residency.

### Wave 5: second-level portability proof

- Generate and validate Whomp's Fortress with the same tools.
- Resolve only generic missing family/effect capabilities.
- Exercise package load/unload and audio-bank transitions.
- Reject level-specific shared-runtime branches.

### Wave 6: full integration and budget recovery

- Run the four-build diagnostic matrix.
- Remove or quarantine diagnostic paths that cannot serve the full game.
- Verify controls, camera, all Mario animations, complete BOB closure,
  music/SFX semantics, geometry stability, and package transitions.
- Recover the all-features build to at least 4.0 mean presentation FPS in the
  pinned setup before beginning the 12--15 FPS optimization sprint.

## Failure and capacity policy

Unknown families, unsupported geo features, animation mismatches, missing
samples, invalid sequence commands, stale generations, capacity overruns, and
residency conflicts fail closed with named diagnostics and counters. During
development an affected object/effect may be visibly omitted under an explicit
diagnostic policy. A failure may not corrupt another bank, silently substitute
unrelated content, replay an entire frame serially, or block simulation.

Every actor, effect, command, voice, sample, sequence, package, scratch, WRAM,
cart, and VDP1 capacity is explicit and tested at its boundary. Zero admitted
instances are an ordinary result. Overflow and unknown feature classes are not.

## Evidence and acceptance

The evidence ladder is cumulative:

1. deterministic generator tests for scene, actor, animation, and audio
   packages;
2. host models for source animation selection, behavior-driven snapshots,
   sequence semantics, voice allocation, mailbox ownership, capacity failure,
   and level transitions;
3. source-contract tests rejecting BOB-specific shared-runtime branches;
4. serial SH-2 builds and linked-image inspection for memory placement,
   unresolved native math, dual-CPU ownership, DMA paths, and package hashes;
5. automated Ymir feature-attribution runs with queue/audio telemetry;
6. manual Ymir acceptance for controls, camera, animation appearance, enemy
   behavior, music/SFX behavior, geometry stability, and pacing; and
7. Whomp's Fortress generation/load proof.

Host evidence cannot close a target or manual gate. A narrow green test cannot
substitute for the complete host gate. The plan and execution ledger are
updated at every task transition, with failed or unexecuted gates left open.

## Prior art and reuse boundary

Existing pinned references retain their documented licenses and reuse modes:

- `Lobotomy-Software/SlaveDriver-Engine` at
  `a8986591557b6e680550d3c23970284d3b38ff8f`, GPL-3.0-or-later: existing
  attributed close ports remain isolated in `src/port/saturn/gpl/`; additional
  renderer ideas require an exact-file review and ledger update.
- Sonic Z-Treme at `cff75451c1616aac1236fc2b44223902b55c706b`, GPL-3.0:
  authorized pattern study/compatible close-port only within the recorded
  source ranges and obligations.
- Jo Engine at `556d081146211b6a1cfa6591d70f9487d406758b`, MIT repository with
  BSD-3-Clause per-file headers: existing fixed-math adaptation and pattern
  study retain their recorded attribution; its SGL allocator/runtime is not
  adopted.
- Yaul at `6012f79f237773378c8014e70d8998ad95a38d98`, MIT: retained as the
  pinned target dependency and API/memory-map authority.
- `ponut64/SCSP_poneSound` at
  `31782e4c61337327f23eb9aa45ecd37fe0944ea0`, MIT: existing vector/linker
  close-port and protocol pattern study remain recorded in
  `docs/saturn/audio/PCM68K_PROVENANCE.md`. Full semantic sequencing is new
  project work unless a separately pinned compatible source is approved.
- Inherited Project12x SM64 audio at repository pin `36d015fb`: Tasks inspect
  `src/audio/external.c`, `seqplayer.c`, `playback.c`, `synthesis.c`, `heap.c`,
  `load.c`, `data.c`, and their headers before implementation. Reuse mode is
  in-tree semantic close-port: preserve public IDs/policy, sequence control
  flow, layer/note rules, ADSR/release, priority, and tuning. Rewrite only the
  N64 pointer/task/RSP ABI and synthesis backend required by bounded big-endian
  package offsets and MC68000/SCSP slot events. The inherited Project12x tree
  has no root license file, so preserve existing notices and do not claim or
  export a new license for these files.
- `malucard/sm64-psx` at
  `3073845688ea273da78d539b20c45110d8a868c3`, with no repository-wide
  license established: behavior/architecture study only. Its compact
  generated IR/assets, target-native lowering, whole-loop profiling, and
  prepared residency remain useful constraints; no PS1 source, GTE math,
  ordering tables, VRAM layout, or packet format may be copied.

Eyepatch Entertainment's private 3DO rewrite and reported Saturn work are
feasibility evidence only. No repository or distributable source is available,
so this sprint neither depends on nor claims reuse of it. Any later code shared
by its author enters through a separate license, provenance, architecture-fit,
and differential-review gate before adaptation.

## Non-goals

- Do not rewrite SM64 gameplay, enemy behaviors, collision, camera, animation
  policy, or audio policy in the Saturn renderer.
- Do not make BOB a permanent runtime special case.
- Do not require every level to be playable in this sprint.
- Do not claim 12--15 FPS from the sprint's 4--6 FPS recovery gate.
- Do not hide unsupported actor/effect/audio features or capacity failures.
- Do not copy private, unlicensed, Sega SDK, or otherwise incompatible source.

## Approved decisions

- Full music and SFX are in scope; original game-facing semantics are retained.
- Every source-selected Mario animation is available; BOB is test coverage,
  not an animation whitelist.
- Every dynamic family required by BOB is in scope, including spawned children
  and required effects identified by generated closure.
- Temporary measured regressions are allowed during integration.
- The final all-features build must measure at least 4.0 mean presentation FPS
  in the pinned setup; 4--6 FPS is the accepted observed band.
- Whomp's Fortress proves future-level adaptation through the same package
  compiler and runtime.
- This umbrella sprint supersedes active Task 10 tracking while preserving all
  completed Task 10 evidence and commits.
