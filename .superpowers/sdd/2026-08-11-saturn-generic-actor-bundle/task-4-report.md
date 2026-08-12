# Task 4 Report — Real BOB S64F-v3 Bundle

Date: 2026-08-11

Status: **blocked-before-RED / no Task 4 production edits**.

## Outcome

The final read-only BOB inventory at reviewed implementation base
`44c903950632df17de24ec0b4781ca8d13d14563` contains no drawable variant that
the reviewed strict S64B-v1 compiler can encode. The current BOB S64F-v2
metadata order has 47 families. Thirteen families are already explicitly
unsupported by the v2 capability report. The remaining inventory contains 34
nonzero drawable `(family ordinal, model ID)` keys, and every one reaches a
named `UnsupportedActorSourceError`: 18 first hit `GEO_SHADOW`, 14 first hit
unrepresentable textured rigid/material state, one first hits `GEO_SCALE`, and
one first hits `GEO_ASM`. Two closure model-variant entries are the exact
non-drawable `MODEL_NONE` sentinel. Compiler-supported drawable count is zero.

The approved S64F-v3 writer deliberately requires `variant_count` in `1..128`.
`pack_bundle` therefore rejects the only truthful document with
`ValueError: variant limit is 1..128`. Task 4 cannot emit, relocate, hash,
host-validate, target-C-validate, or atomically publish a real BOB v3 artifact
without first changing an upstream data-format/capability decision. No empty,
Mario, first-family, stale, approximate, or synthetic bank was substituted.

## Exact canonical inventory

Ordinals below are the existing canonical S64F-v2 family order. A dash means
the entire family is already v2-unsupported and therefore has no v3 variant.

| Ordinal | Stable representative | Model key | First truthful reason |
|---:|---|---|---|
| 1 | `bhvSeesawPlatform` | — | `UNSUPPORTED_GEO_NODE:GEO_CULLING_RADIUS` |
| 2 | `bhvBreakBoxTriangle` | — | `UNSUPPORTED_GEO_NODE:GEO_CULLING_RADIUS` |
| 3 | `bhvMessagePanel` | `MODEL_WOODEN_SIGNPOST` / `0x007c` | `unsupported GeoLayout node: GEO_SHADOW` |
| 4 | `bhvExplosion` | `MODEL_EXPLOSION` / `0x00cd` | `unsupported rigid-group source: textured` |
| 5 | `bhvRedCoin` | `MODEL_RED_COIN` / `0x00d7` | `unsupported GeoLayout node: GEO_SHADOW` |
| 6 | `bhvOrangeNumber` | `MODEL_NUMBER` / `0x00db` | `unsupported rigid-group source: textured` |
| 7 | `bhvBobomb` | `MODEL_BLACK_BOBOMB` / `0x00bc` | `unsupported GeoLayout node: GEO_SHADOW` |
| 8 | `bhvSparkle` | `MODEL_SPARKLES_ANIMATION` / `0x008f` | `unsupported rigid-group source: textured` |
| 9 | `bhvBlueCoinJumping` | `MODEL_BLUE_COIN` / `0x0076` | `unsupported GeoLayout node: GEO_SHADOW` |
| 10 | `bhvFloorSwitchGrills` | — | `UNSUPPORTED_GEO_NODE:GEO_CULLING_RADIUS` |
| 11 | `bhvKoopa` | `MODEL_KOOPA_WITH_SHELL` / `0x0068` | `unsupported GeoLayout node: GEO_SHADOW` |
| 12 | `bhvChainChompGate` | — | `UNSUPPORTED_GEO_NODE:GEO_CULLING_RADIUS` |
| 13 | `bhvObjectWaveTrail` | `MODEL_WAVE_TRAIL` / `0x00a3` | `unsupported rigid-group source: textured` |
| 14 | `bhvKoopaFlag` | `MODEL_KOOPA_FLAG` / `0x006a` | `unsupported GeoLayout node: GEO_SCALE` |
| 15 | `bhvOpenableCageDoor` | — | `UNSUPPORTED_GEO_NODE:GEO_CULLING_RADIUS` |
| 16 | `bhvRedCoinStarMarker` | `MODEL_TRANSPARENT_STAR` / `0x0079` | `unsupported GeoLayout node: GEO_SHADOW` |
| 17 | `bhvWoodenPost` | — | `UNSUPPORTED_GEO_NODE:GEO_CULLING_RADIUS` |
| 18 | `bhvBobombExplosionBubble` | `MODEL_WHITE_PARTICLE_SMALL` / `0x00a4` | `unsupported rigid-group source: textured` |
| 19 | `bhvCannonClosed` | `MODEL_DL_CANNON_LID` / `0x00c9` | `unsupported rigid-group source: textured` |
| 20 | `bhvKoopaShellFlame` | — | `UNSUPPORTED_GEO_NODE:GEO_BRANCH_AND_LINK` |
| 21 | `bhvWhitePuffExplosion` | `MODEL_BUBBLE` / `0x00a8` | `unsupported rigid-group source: textured` |
| 21 | `bhvWhitePuffExplosion` | `MODEL_MIST` / `0x008e` | `unsupported GeoLayout node: GEO_ASM` |
| 22 | `bhvGoomba` | `MODEL_GOOMBA` / `0x00c0` | `unsupported GeoLayout node: GEO_SHADOW` |
| 23 | `bhvBobBowlingBallSpawner` | `MODEL_NONE` | non-drawable sentinel; no variant |
| 24 | `bhvCannonBarrel` | `MODEL_CANNON_BARREL` / `0x007f` | `unsupported rigid-group source: textured` |
| 25 | `bhvWaterBomb` | — | `UNSUPPORTED_GEO_NODE:GEO_CULLING_RADIUS` |
| 26 | `bhv1Up` | `MODEL_1UP` / `0x00d4` | `unsupported GeoLayout node: GEO_SHADOW` |
| 27 | `bhvKoopaShell` | `MODEL_KOOPA_SHELL` / `0x00be` | `unsupported GeoLayout node: GEO_SHADOW` |
| 28 | `bhvRotatingExclamationMark` | `MODEL_EXCLAMATION_POINT` / `0x0084` | `unsupported rigid-group source: textured` |
| 29 | `bhvCannon` | `MODEL_CANNON_BASE` / `0x0080` | `unsupported rigid-group source: textured` |
| 30 | `bhvSpawnedStar` | `MODEL_STAR` / `0x007a` | `unsupported GeoLayout node: GEO_SHADOW` |
| 31 | `bhvBreakableBox` | — | `UNSUPPORTED_GEO_NODE:GEO_CULLING_RADIUS` |
| 32 | `bhvBobombBullyDeathSmoke` | `MODEL_SMOKE` / `0x0096` | `unsupported rigid-group source: textured` |
| 33 | `bhvRecoveryHeart` | `MODEL_HEART` / `0x0078` | `unsupported GeoLayout node: GEO_SHADOW` |
| 34 | `bhvWaterBombShadow` | — | `UNSUPPORTED_GEO_NODE:GEO_CULLING_RADIUS` |
| 35 | `bhvChainChomp` | `MODEL_CHAIN_CHOMP` / `0x0066` | `unsupported GeoLayout node: GEO_SHADOW` |
| 36 | `bhvChainChompChainPart` | `MODEL_METALLIC_BALL` / `0x0065` | `unsupported GeoLayout node: GEO_SHADOW` |
| 36 | `bhvChainChompChainPart` | `MODEL_NONE` | non-drawable sentinel; no variant |
| 37 | `bhvExclamationBox` | — | `UNSUPPORTED_GEO_NODE:GEO_CULLING_RADIUS` |
| 38 | `bhvBowlingBall` | `MODEL_BOWLING_BALL` / `0x00b4` | `unsupported GeoLayout node: GEO_SHADOW` |
| 39 | `bhvBubbleSplash` | `MODEL_SMALL_WATER_SPLASH` / `0x00a5` | `unsupported rigid-group source: textured` |
| 40 | `bhvCoinSparkles` | `MODEL_SPARKLES` / `0x0095` | `unsupported rigid-group source: textured` |
| 41 | `bhvBreakableBoxSmall` | — | `UNSUPPORTED_GEO_NODE:GEO_CULLING_RADIUS` |
| 42 | `bhvKingBobomb` | `MODEL_KING_BOBOMB` / `0x0056` | `unsupported GeoLayout node: GEO_SHADOW` |
| 43 | `bhvBobombBuddy` | `MODEL_BOBOMB_BUDDY` / `0x00c3` | `unsupported GeoLayout node: GEO_SHADOW` |
| 44 | `bhvWingCap` | `MODEL_MARIOS_WING_CAP` / `0x0087` | `unsupported GeoLayout node: GEO_SHADOW` |
| 45 | `bhvCoinFormationSpawn` | `MODEL_YELLOW_COIN` / `0x0074` | `unsupported GeoLayout node: GEO_SHADOW` |
| 46 | `bhvObjectWaterWave` | `MODEL_IDLE_WATER_WAVE` / `0x00a6` | `unsupported rigid-group source: textured` |
| 47 | `bhvCheckerboardElevatorGroup` | — | `UNSUPPORTED_GEO_NODE:GEO_CULLING_RADIUS` |

The v2 report has 86 closure records. Its 13 unsupported family
representatives cover 14 closure records. No record is silently dropped:
remaining records resolve to one of the 34 named unsupported drawable keys or
the explicit model-less sentinel grouping. There is no supported bank to
deduplicate or embed.

## Secondary read-only diagnostics

The sole first-hit `GEO_SCALE` key is ordinal 14, model
`MODEL_KOOPA_FLAG` / `0x006a`, behavior `bhvKoopaFlag`, source
`actors/koopa_flag/geo.inc.c`, exact construct
`GEO_SCALE(0x00, 16384)`. An isolated temporary-root diagnostic replaced only
that node with `GEO_NODE_START()`, resealed the copied source attestation, and
then reached `UnsupportedActorSourceError: unsupported rigid-group source:
textured`; scale is not the key's only blocker.

Exactly 18 keys first hit `GEO_SHADOW`. A temporary-root diagnostic removed
only the shadow node and resealed each copied attestation. Zero became
compiler-supported: 12 next hit `GEO_SCALE`, and six next hit textured
rigid/material state. These diagnostics did not modify the workspace and are
not authorization to discard shadow, bake scale approximately, or omit
material state.

## RED/GREEN and validation status

- Required missing-module RED: **not started**. The format contradiction was
  proven first, before creating the test module.
- Task 4 production/test/Make/CLI files: **not created or modified**.
- GREEN, relocation, deterministic report identity, atomic/no-clobber
  publication, host v3 validation, real-artifact target C validation, exact
  bundle hashes/counts/lane/scratch, and dependency document: **not run / not
  available**, because no valid S64F-v3 input document exists.
- Final read-only gate: native-forward-slash command-line
  `SATURN_REPO_ROOT=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge`
  `compile-actor-banks` exited zero, then the complete strict variant probe
  exited zero with 0 supported / 34 named unsupported / 2 model-less entries.

## Decision required

1. **Recommended: revise the plan around an additive, texture/material-capable
   Saturn/SH-2 actor-bank format.** Define exact serializable texture
   coordinates, texture/palette identity and placement, combine/tile/cull/env/
   alpha state, shadow and scale ownership, source identity, host/target
   validation, workspace impact, and migration/versioning. Keep S64B-v1 and
   its historical Mario bytes unchanged; do not retrofit an ambiguous semantic
   into v1. Resume Task 4 only after the new format/compiler boundary has TDD
   coverage and independent approval.
2. **Permit an empty S64F-v3 bundle.** This requires a deliberate format and
   validator change from `1..128` to `0..128`, but it supplies no actor bank,
   cannot unblock the Task 16 production path, and is not a full-port path.
3. **Stop the prerequisite lane.** Preserve all existing fail-closed behavior
   and leave Task 10/Task 16 blocked.

## Owner-approved design resolution

The owner selected option 1 with an explicit Saturn/SH-2 constraint: implement
BOB-first semantics in a full-game-shaped additive S64B v2 and keep every
unapproved later-game state fail-closed. The normative written design is
`docs/superpowers/specs/2026-08-11-saturn-actor-bank-v2-textures-design.md`.

The approved correction does not embed an N64 material interpreter. It adds
target-level recipe records, per-primitive offline-baked VDP1 tiles, and cold
CLUT16/RGB1555 payload spans while retaining the v1 pose/geometry prefix. It
requires separate texture/CLUT accounting, scene-aggregate proof against the
shared 446,432-byte VDP1 ceiling and fixed cart/command/Gouraud/runtime budgets,
S64B-owned v1/v2 parsing, master-only all-resident uploads and generation
publication, and unchanged worker scalar output records. Textured triangles are
not paired in v2.

The acceptance bar includes a normally spawned recognizable BOB non-Mario
actor textured through the production mixed-bank path in Ymir. A synthetic
fixture, Mario substitution, stale bank, first record, or empty S64F remains
invalid. Target bytes will require the Task 9 reproducibility/release/native-
math chain and Task 10 smoke/visual/manual gates to reopen.

The written specification passed scoped self-review, exact extension-layout
arithmetic, production-v1-prefix comparison, in-tree reference checks, and
reconciliation of the prior v1-only design clause. It was committed as
`62f16de8` (`docs(saturn): design textured actor banks`).

The owner approved the committed written specification on 2026-08-12. The
self-reviewed dependency-ordered implementation plan is
`docs/superpowers/plans/2026-08-12-saturn-actor-bank-v2-textures.md`. Its
self-review clarified that the real generic BOB S64F may contain only v2 banks
while historical v1 Mario and v2 generic actors prove the mixed-version scene
path; it also retains S64P alignment 4 and explicitly separates bounded CLUT
upload, global lane stride, and active texture generation.

This resolution does not unblock code yet. Task 4 remains
`blocked-before-RED` until the implementation plan is committed and dispatched.
No production, test, Make, CLI, CHANGELOG, target, or release behavior changed
in this design/plan transition.

## Reference/reuse record

Read-only inspection and direct diagnostic use were based on repository HEAD
`44c90395` and these in-tree sources: `tools/saturn/actor_family_bundle.py`,
`tools/saturn/actor_variant_bank.py`, `tools/saturn/compile_actor_bank.py`,
`tools/saturn/collect_scene_closure.py`, the generated current BOB closure/v2
report, and the approved S64F-v3 design/Task 4 brief. Reuse mode is direct-use
diagnostic only. No external source, copied code, new license obligation, or
implementation-from-scratch decision was introduced.

## Review and open gates

Independent Task 4 code review is not applicable because no Task 4 behavior
diff exists. The owner/controller directed this blocked status transition.
Task 4, Tasks 5–11, Task 16 Tasks 2–5, target/release/reseal, sourceboot
selection, cart/LWRAM/HWRAM map/capacity, P2/Ymir, heterogeneous lanes,
feature-off identity, transition, smoke, visual, desktop, owner-manual, and
total-game gates all remain open and unchecked.
