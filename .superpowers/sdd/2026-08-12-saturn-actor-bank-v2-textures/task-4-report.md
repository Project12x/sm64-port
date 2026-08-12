# Task 4 evidence report — exact measured BOB materials

Status: fix round 1 source-complete-pending-rereview, 2026-08-12. Original
behavior commit `52c9c1af`; repair commit `86de51cc` passed the full
host/historical gate set. Task 5 is not open until the same independent
reviewer approves the repair.

## Scope and outcome

The host compiler admits exactly 14 measured direct-textured BOB keys and
lowers their closure-attested Fast3D state through Task 2's pointer-free S64B
v2 packer. Real family 29 / `MODEL_CANNON_BASE` `0x0080` / `bhvCannon` is the
required demo key. It packs 30 draw records with eight unpaired textured source
triangles, 1,024 texture bytes, 256 CLUT bytes, eight texture commands, and 30
Gouraud tables per instance. No target runtime, residency, renderer, Make
orchestration, bundle publication, or Ymir code changed or was claimed.

All 34 drawable keys have one frozen outcome: the 14 exact v2 keys compile;
18 `GEO_SHADOW`, one `GEO_SCALE`, and one `GEO_ASM` key reject by name. The
full 47-family replay also freezes 13 capability-unsupported families and the
two `MODEL_NONE` variants. Unapproved, computed, partial, ambiguous, and
unknown material/list states remain fail-closed.

## Source closure and state binding review

- The compiler extends the sole existing `_Fast3DCompiler`; it does not add a
  second command parser. Unknown commands reject.
- Canonical admission hashes bind the evaluated command-local trace and final
  state after the complete reached transfer graph, including ambient/diffuse
  lights and recognized state after the final triangle. They also bind image
  identity, the load tile/block and DXT
  width, render tile/size, masks/shifts/scales, wrap/clamp/mirror, combiner plus
  environment/alpha state, geometry mode, layer/opacity, call/tail topology,
  textured UVs, and exact PNG path/SHA-256. Untextured material states in mixed
  Cannon/barrel lists are also included.
- Texture declarations and their checked-in `.rgba16`/`.ia16` PNGs resolve
  uniquely in `collect_scene_closure.py` for reached `gsDPSetTextureImage` and
  `gsDPLoadTextureBlock` commands. The owning actor record contains the PNG
  path/hash before closure validation and publication. `_SourceIndex` requires
  the exact attested digest and decodes the already-hashed bytes; it no longer
  synthesizes a downstream source record. Transparent PNG pixels canonicalize to RGB1555 zero,
  matching VDP1 CLUT index-zero transparency and deterministic quantization.
- Every textured source triangle supplies one 16x16 or 32x32 tile input and a
  source-triangle-owned binding; pairing is forbidden. Task 2 may deduplicate
  identical tile/CLUT bytes after ownership has been proven.

## Saturn/SH-2 review

Offline recipes explicitly state the hardware tradeoff: VDP1 textured Gouraud
is additive rather than N64 texture-times-shade. Opaque/cutout/half-transparent
recipes therefore use only the measured table. There is no target-side Fast3D
interpretation, allocation, path, pointer, dynamic bake, or new per-instance
record. Cannon's host-packed resource counts are recorded above, but aggregate
446,432-byte VDP1 sharing, 65,536-byte actor arena, live-count, cart, command,
and Gouraud budgets remain Task 5 gates.

## Reference-code provenance

Repository: `https://github.com/Project12x/sm64-port.git`, pinned reconciled
commit `05b77e6472a09facd3d4faf01100ad09b2d9882e`. The inherited repository has no
blanket root license; this task copied no external code.

| File inspected | Last owning commit | Reuse mode |
|---|---|---|
| `tools/saturn/actor_variant_bank.py` | `68ceec9c66070d48d05d6442e11c76e2b9b6b903` | direct extension/close-port of tokenizer and sole Fast3D state machine |
| `tools/saturn/dl_rigid_groups.py` | `a78db8c9b777074e1e60b7260f3aaaa9596f1c10` | direct structural-walk reuse |
| `tools/saturn/vdp1_texture.py` | `2e5b41e6dfc2fe45a5c28d5c2b6c3d4c0b40c161` | direct extension |
| `tools/saturn/bake_castle_uv.py` | `b295928ef54aeadc00b0b578c89bb6e700878134` | direct weight/quantizer reuse |
| `tools/saturn/bake_bob_tiles.py` | `4c60f4fe35ea918361ed6d1ee2c598ca62627516` | close-port of PNG/bake pattern |
| `tools/saturn/actor_bank_v2.py` | `95de64570cc20eefa94839ec13b9a33a9c28096e` | Task 2 dependency/direct reuse |

## Tests actually run

### Fix round 1

- RED/GREEN: post-final Cannon `gsDPSetEnvColor` mutation, distinct
  command-local trace with equal final state, missing/ambiguous/computed/
  unsupported/noncanonical texture paths, real `gsDPLoadTextureBlock`, real
  Cannon PNG closure, unattested PNG, byte-decoder equivalence, and bounded
  scalar shift operand/count all failed before their repair and pass after it.
- Focused all-34/all-47/mutation/variant/source wave: 45 tests PASS.
- Full synthetic plus real-BOB closure wave: 38 tests PASS; focused final
  texture-closure rerun: 3 tests PASS.
- Task 2 host parser/packer: 16 tests PASS; freestanding v2 validator: 86
  mutations PASS.
- Mixed S64F-v3: 54 mutations PASS; actor pose PASS; meshlets PASS plus the
  invalid-span mutation; variant/source Make gate: 40 tests PASS.
- Historical Mario remains 596,896 bytes / SHA-256
  `242ecd7a91ddbfb49e65a0f04949168f1de9c24d66070c299b8889d6604ce539`;
  JSON SHA-256 `3f0f2dd965e7fbe9e73d9b791053478d9b3fe73199087bb827b76912e4206bf0`;
  source identity
  `60f942e6f30d4a153393a47ac53626ee53d90ebeb750ee5d244d5ef2a16925c1`.
- Approved Cannon bytes remain exact at 2,952 bytes, source identity
  `bb972afe2022977f4b7290d11e28e081f869c7cc7b62b378e28bac17bf76dc4f`,
  and payload SHA-256
  `2c8eee36768f0a42063949298eca8351bacb74838165a8188520a4b180d63c6d`.
- Final compileall, scoped diff check, and post-document reruns are recorded
  before handoff.

### Original implementation

- RED: prescribed three-suite command — 37 historical tests PASS; new module
  ERROR on missing `actor_material_v2`, as intended.
- GREEN: `python -m unittest tools.saturn.test_actor_material_v2
  tools.saturn.test_actor_variant_bank tools.saturn.test_actor_source -v` — 42
  PASS, including all real keys, all 47 family outcomes, named Cannon mutations,
  source/path/hash binding, deterministic repeat, and historical v1 behavior.
- `verify-actor-pose-bank verify-actor-meshlets verify-actor-family-bundle
  verify-actor-variant-bank` — PASS; invalid-span mutation caught, 54 mixed-S64F
  mutations PASS, and 38 historical variant/source tests PASS.
- `compileall` for all six Task 4 Python files — PASS.
- Historical Mario S64B — 596,896 bytes, SHA-256
  `242ecd7a91ddbfb49e65a0f04949168f1de9c24d66070c299b8889d6604ce539`.
- Final scoped `git diff --check` and post-document reruns are recorded before
  handoff.

## Independent scoped rereview

Scoped rereview of `20e5484b..f5a03808` found all three original findings
**ADDRESSED**. Exact admission includes evaluated command trace and final
state; closure collection attests both texture-load forms and exact PNG bytes
before decode; shift operands/counts are bounded before evaluation. Fresh
focused/material/variant/source/closure coverage passed 83/83, including the
real 34-key/47-family Cannon replay; compileall and diff check pass. No target,
runtime, renderer, residency, Ymir, or Task 5 behavior changed. Spec PASS /
Quality PASS, C0/I0/M0.

Task 4 is complete and approved. Task 5 may open; no later gate is claimed.

## Remaining gates

- [x] Independent Task 4 spec/quality review and verdict: PASS C0/I0/M0.
- [ ] Task 5 real mixed BOB S64F orchestration and aggregate budget proof.
- [ ] Every target residency/renderer/dual-SH-2 execution gate.
- [ ] Ymir/demo/visual/release/reseal/retail/total-game evidence.
