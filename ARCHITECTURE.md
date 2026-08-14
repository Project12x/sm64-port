# Architecture

The architecture serves the playable SM64 Saturn port defined in
[`docs/saturn/PRODUCT_GOAL.md`](docs/saturn/PRODUCT_GOAL.md). This document
distinguishes live-proven behavior from candidate machinery. Historical queue,
package, release, and overlap designs remain in Git and under
`docs/superpowers/`; they are not automatically active architecture.

## Product boundary

The inherited SM64 engine owns:

- the game loop and level scripts;
- Mario and object state/actions;
- collision, camera, animation, and progression;
- source display-list and audio semantics; and
- transitions between title, menus, courses, and rewards.

The Saturn port owns:

- controller, CD, cartridge, timing, and memory services;
- deterministic conversion of source assets into Saturn-ready data;
- bounded SH-2 transform, clipping, lighting, sorting, and command lowering;
- VDP1/VDP2 composition and transfer;
- MC68000/SCSP playback beneath the source audio API; and
- telemetry required to verify the assembled game.

Saturn code may reduce fidelity in a documented, common, telemetry-visible way.
It may not replace source gameplay with a demo-specific game loop, injected
actors, or level-specific runtime behavior.

## Target shape

The target is constrained deliberately by Saturn and dual-SH-2 ownership:

- immutable bulk content resides in the 4 MiB DRAM cartridge;
- HWRAM holds the master game state and small hot control structures;
- LWRAM holds explicitly bounded worker/output arenas;
- VDP1 VRAM holds command, texture, CLUT, and Gouraud partitions with one master
  publisher;
- the master SH-2 owns game state, final painter order, VDP publication, scene
  transitions, and audio command production;
- the slave SH-2 consumes immutable generation-bound work and publishes bounded
  scalar results; and
- the MC68000 owns sequence/voice cadence so audio does not inherit the slow
  presentation rate.

Every cross-region path records source/destination, maximum bytes, alignment,
lifetime, owner, transport, first consumer, failure behavior, and earliest live
observation. Those records prevent memory debt; they do not delay that live
observation.

## Shared game-path data flow

The intended shared path is:

```text
source level script and game tick
        ↓
authoritative Mario/object/geo/audio state
        ↓
scene-selected immutable source assets
        ↓
Saturn-ready resident world/actor/audio data
        ↓
bounded SH-2 work and master painter/audio policy
        ↓
VDP1/VDP2 frame + MC68000/SCSP output
```

BOB and Whomp’s Fortress must use this path in one executable. If selecting WF
requires a `wf` renderer, frame loop, Mario path, or audio backend, the boundary
is not generic and must be simplified rather than hidden behind another schema.

## Failure domains

Corrupt, out-of-bounds, stale-generation, or physically unsafe data fails
closed before mutation.

Incomplete source-feature coverage fails at the smallest safe unit:

- an unsupported actor omits that actor;
- an unsupported material omits that draw or uses an explicitly approved common
  reduction;
- an unavailable SFX mutes/rejects that event; and
- a failed new frame retains the last complete frame when physically safe.

Feature incompleteness must not stall simulation, blank the level, disable
input, or silence unrelated audio. Whole-scene rejection is reserved for a
package whose integrity or memory safety cannot be established.

## Live-proven foundation

The following have useful target evidence and may be retained:

- source SM64 gameplay ownership and direct BOB execution;
- the accepted A9A BOB visual/control artifact;
- SH-2 native-math and cadence repairs;
- fixed memory-region/linker checks and selected DMA/address primitives;
- source Mario animation/render donors from the accepted slice; and
- the standalone owner-accepted MC68000/SCSP sound implementation.

“Live-proven” applies only to the behavior and artifact actually observed. It
does not automatically validate later integrations of the same module.

## Candidate architecture on probation

Recent work produced deterministic scene/actor/audio packages, mixed actor-bank
versions, family/variant compilers, generic actor queues, meshlet workspaces,
texture residency, generation publication, and render-overlap machinery.

These are candidate donors because the current assembled CUE has not preserved
Mario fidelity, normal Bob-omb rendering, audible output, or accepted
performance. Their tests prove useful local properties but not composition.

A candidate component is adopted only after it enables the next live product
gate within two causal attempts or two hours. Otherwise retain its research and
tests, bypass its integration, and use the smallest proven path.

## Renderer contract

- Mario keeps the accepted source animation and texture path until a replacement
  demonstrates byte/content equivalence and equal live fidelity.
- Terrain, Mario, and actors share one master-owned painter order because VDP1
  has no depth buffer. Producer-local ordering is insufficient.
- Fixed bounded Gouraud tables are acceptable; flat replacement is not an
  accepted substitute where the source result requires lighting.
- Actors enter through the normal object registry and source behavior/model
  identity. Forced records and injected witnesses are diagnostics only.
- Each behavior change is captured after the level has visibly rendered.

## Audio contract

- Preserve the source-facing music/SFX API and game-triggered events.
- Reuse the existing MC68000/SCSP implementation before designing a replacement.
- Validate sound RAM bounds and package identity before enabling the sound CPU.
- Prove audible or captured waveform output; mailbox consumption, heartbeat,
  and `SNDON` alone are insufficient.
- Audio faults mute/report without blocking the game.

## Portability contract

Genericity is demonstrated by real reuse, not by schemas:

1. BOB proves the first shared gameplay path.
2. Whomp’s Fortress proves the second level in the same executable.
3. The retail title/menu and star-return loop prove source transitions.

No component is called full-game-capable until at least two materially different
source consumers use it without a level/object-specific runtime branch.

## Verification order

For a product change:

1. focused regression for the observed defect;
2. target compile/link and memory bounds;
3. uniquely identified development CUE;
4. Ymir visual/audio/input/failure/FPS observation;
5. keep or revert;
6. focused safety/code review; and
7. broad/release verification only after a playable milestone.

This order is architectural. Reversing it produced locally rigorous components
that regressed the assembled port.
