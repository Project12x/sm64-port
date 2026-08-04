# Animated Actor Generalization Spike — 2026-08-04

## Outcome

**Choose Goomba (`MODEL_GOOMBA`) as the first non-Mario animated-actor proof.**
It is a real Bob-omb Battlefield (BOB) enemy, has source-owned animation and
behavior, and exercises enough of the actor path to prove that the Mario bank
is generalizable without pulling fuse, explosion, held-object, or particle
semantics into the first cutover.

This is an inventory and host-only contract, not a renderer activation.  It
does not change the accepted A3+A4 manual baseline of roughly 3–4 FPS, does
not register a CPU-DUAL callback, and must not be used to claim enemy runtime
support.

## What already transfers from Mario

The existing Mario implementation is an actor-bank prototype, not a one-off
asset format:

| Existing contract | Generalized meaning |
| --- | --- |
| `sm64_saturn_mario_actor_snapshot_t` | Read-only simulation/graph snapshot; replace Mario-specific state with a bounded `Object`/`GraphNodeObject` snapshot. |
| Pose selector and vertex/material bank IDs | Per-family immutable geometry/material/animation bank plus the source-selected animation/frame. |
| Source-provenanced IR, primitive IDs, compatible quad pairing | The exact same offline pipeline; retain source display-list and primitive identities. |
| Bounded material/opacity meshlets and runtime pose AABBs | One actor instance admits only visible meshlets before transform/classify. |
| Queue descriptor -> claimant-owned output lane -> terminal reader | Each instance job must use the A5 descriptor-owned payload contract; no per-enemy fixed split or cache alias. |

Mario has 788 source triangles, 644 compiled primitives and 31 meshlets.  A
first enemy must be kept strictly smaller and must prove *multiplicity* rather
than compete with Mario's animation coverage.

## Why Goomba, not Bob-omb, first

BOB contains two direct `macro_goomba` entries and three
`macro_goomba_triplet_spawner` entries in
`levels/bob/areas/1/macro.inc.c`.  The source triplet behavior spawns three
Goombas per active spawner, so a conservative visible-instance budget for the
first proof is **11 Goombas** (two direct plus nine spawned).  That makes the
workload materially different from one Mario while remaining bounded.

The Goomba source is compact and has one 30-frame animation.  It has a
useful, intentional first feature boundary:

- opaque animated body/head/feet in `goomba_seg8_dl_0801D760`,
  `goomba_seg8_dl_0801B5C8` / `0801B5F0`, `0801CE20`, and `0801CF78`;
- a `GEO_SWITCH_CASE(2, geo_switch_anim_state)` for the two head states;
- a camera-facing (`GEO_BILLBOARD`) alpha eye display list
  `goomba_seg8_dl_0801B690`; and
- the normal `GEO_SHADOW` node, which is a separate effect policy rather than
  source geometry to silently flatten into the opaque mesh.

This means the proof can first ship opaque animated Goombas only, but it may
not call that full visual parity.  The alpha-eye billboard and shadow must be
represented as declared feature requirements and either rendered through a
bounded translucent/billboard path or visibly omitted under an explicit
diagnostic policy.  Do not quietly treat them as ordinary world triangles.

Black Bob-omb is the second recommended family.  It is abundant in BOB too,
but adds two animations, a two-state alpha eye branch, multiple articulated
limbs, fuse smoke, explosion effects, respawn handling, and held-object
interaction.  It is a better phase-two stress case after Goomba proves the
generic instance bank.

## Exact source/provenance inventory

| Role | Source path | Required observation |
| --- | --- | --- |
| Model geometry / materials | `actors/goomba/model.inc.c` | Flatten the source Fast3D display lists; record every emitted primitive's leaf display list. |
| Geo hierarchy | `actors/goomba/geo.inc.c` | Evaluate scale `16384`, all `GEO_ANIMATED_PART` transforms, switch case, billboard, alpha layer, and shadow separately. |
| Animation table / data | `actors/goomba/anims/table.inc.c`, `actors/goomba/anims/data.inc.c`, `actors/goomba/anims/anim_0801DA34.inc.c` | Preserve source animation `goomba_seg8_anim_0801DA34`, whose frame count is `0x1E` (30). |
| Source behavior | `data/behavior_data.c` (`bhvGoomba`), `src/game/behaviors/goomba.inc.c` | Read only live `Object`/graph state after the original behavior selected its animation and pose. No behavior, spawning, collision, or death policy moves into the renderer. |
| BOB placement / multiplicity | `levels/bob/areas/1/macro.inc.c`, `include/macro_presets.h` | Preserve two direct Goombas plus three triplet spawners; budget up to 11 active instances before a source-owned draw-distance rejection. |
| Model identity | `include/model_ids.h` | Bind `MODEL_GOOMBA` (`0xC0`) to `goomba_geo`; do not match model names or display-list strings at runtime. |

SHA-256 is calculated from the checked-out source inputs before extraction and
stored in both generated header and JSON report, as `extract_mario_actor.py`
does for Mario.  The inherited `Project12x/sm64-port` baseline has no root
license file, per `THIRD_PARTY_LICENSES.md`; these are already-in-tree SM64
source/assets, not an external code import.  Reuse mode is **direct data
conversion** from the checked-out source tree.  No external implementation was
needed or copied for this spike.

## Required extractor/runtime changes (future work)

1. Refactor only the offline extractor's actor-independent parsing and IR
   formatting into a shared module; retain a small family manifest rather than
   hard-coding Goomba names in renderer code.
2. Add a Goomba manifest containing source revision/hash, model ID, geo root,
   animation table, maximum instance count, opaque and alpha/billboard feature
   sets, and all generated section sizes/alignment.
3. Generate immutable geometry/material/meshlet/LOD tables and a JSON report.
   Animation must evaluate the source skeleton per selected frame; neutral
   pose bounds cannot be treated as safe for a walking enemy.
4. Snapshot the original object's transform, scale, animation ID/frame,
   `animState`, and render-active state after source update.  The renderer
   consumes that immutable snapshot only.
5. Submit one bounded actor-instance job per admitted Goomba through the same
   A5 queue descriptor/output-lane/DONE contract as Mario.  Master retains
   final painter ordering and VDP1 lowering.
6. Establish bounded VDP1 budgets before alpha eyes/shadows are enabled:
   opaque meshlets, alpha billboard commands, translucent ordering slots,
   per-instance pose/transform scratch, command count, and cartridge bank
   residency must each fail visibly rather than overrun.

## Acceptance ladder and open gates

1. **Host extraction gate:** deterministic Goomba header/report; each source
   display list and animation input is hashed; fixture proves source model,
   geo, animation, BOB placement, and special render features still match this
   manifest.
2. **Actor-bank gate:** generated Goomba bank passes source primitive,
   material, opacity, frame-range, meshlet-bound, and per-instance capacity
   tests.  Document exact WRAM/cart/VDP1 byte and command budgets—no estimate
   may be promoted to a target claim.
3. **Runtime correctness gate:** source behavior drives multiple Goombas,
   walking pose changes, size scale, draw distance, and despawn/respawn while
   the renderer stays read-only.  Alpha-eye and shadow policy is visibly
   accounted for.
4. **Target gate:** a fresh serial build and desktop Ymir manual result proves
   BOB terrain + Mario + multiple Goombas.  It is a visual/correctness gate,
   not a promise of an FPS increase; the current 3–4 FPS build remains the
   rollback baseline until then.

## Explicit non-goals

- Do not add target code, a new CPU-DUAL callback, or a Ymir build in this
  spike.
- Do not use a Goomba-only renderer branch or make BOB a permanent scene
  special case.
- Do not claim full enemy support, alpha parity, sound, particles, or a
  playable full-game object set from a static inventory.
