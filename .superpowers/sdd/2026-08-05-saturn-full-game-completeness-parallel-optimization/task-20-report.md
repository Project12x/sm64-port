# Task 20 report — bounded BOB actor-effect infrastructure

Status: source-complete for the bounded host-only inventory, descriptor,
lowering, and painter-order slice. The production observer still publishes no
authoritative effect fields, and the nine effect-related records blocked by
`GEO_CULLING_RADIUS` or `GEO_BRANCH_AND_LINK` remain fail-closed. This report
does not claim production cutover, complete BOB effects, target compilation,
Ymir or hardware output, manual playability, or an FPS change.

## Source evidence and exact inventory

The authoritative inputs are the generated BOB Area 1 closure
`build/saturn/packages/bob/1/closure.json` (86 records) and S64F v2 family
report `build/saturn/packages/bob/1/actors/actor-families.json` (47 family
representatives). The independent checked-in oracle pins both family source
digests and every record in the following classes:

```text
BILLBOARD=5  ALPHA=30  TRANSLUCENT=18  SHADOW=34
PARTICLE=0   DECAL=0
projectiles=3  rewards=10  effects=19
unsupported effect records=9
```

The unsupported set remains exactly `bhvBreakBoxTriangle`,
`bhvBreakableBox`, `bhvBreakableBoxSmall`, `bhvChainChompGate`,
`bhvExclamationBox`, `bhvOpenableCageDoor`, `bhvWaterBomb`, and
`bhvWaterBombShadow` through `GEO_CULLING_RADIUS`, plus
`bhvKoopaShellFlame` through `GEO_BRANCH_AND_LINK`. The oracle derives roles
from generated closure fields; runtime code contains no behavior/family-name
branch. PARTICLE and DECAL remain explicit empty source-derived sets.

## Implementation and ownership

- Added a bounded 64-byte pointer-free effect descriptor and 16-byte lowering
  output. Admission rejects stale generations, unknown capability/source bits,
  zero or unresolved source fields, invalid material/depth values, expired
  effects, and unresolved shadow receivers.
- Preserved source simulation ownership: the renderer consumes lifetime and
  visibility but never advances timers or performs collision/floor queries.
- Billboard descriptors select the existing `sm64_saturn_mtxq_billboard`
  contract; no second camera-facing math path was added.
- Binary `LAYER_ALPHA` cutout lowers to replacement color calculation with
  transparent pixels enabled. True translucent/decal/shadow material lowers to
  the explicit VDP1 half-transparent mode. VDP2 ownership is unchanged.
- Master ordering is bounded, atomic on failure, far-to-near by the existing
  64-bin depth contract, and stable by source ordinal at equal depth. Zero
  budget, caller-output overflow, fixed descriptor-capacity overflow, stale
  identity, and unknown descriptor state all fail closed with telemetry.
- Reuse mode is direct reuse of in-tree Task 16/18 contracts and the existing
  renderer's billboard/depth/batch/VDP1 mode vocabulary. No external source
  was copied or closely ported.

## Gates actually run

The serialized DLL/MSYS-preflight wave was:

```text
powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 \
  mingw32-make -f Makefile.saturn.mk -j1 \
  verify-actor-effects verify-actor-batches verify-actor-capability-bank \
  verify-actor-family-bank verify-actor-instance-queue
PASS (61.3 s)
```

Observed sub-results:

```text
strict C11/pedantic/Wall/Wextra/Werror effect admission: PASS
strict C11/pedantic/Wall/Wextra/Werror effect order: PASS
exact effect CLI: PASS (5/30/18/34/0/0; unsupported=9)
exact effect oracle/mutations: 3/3 PASS
actor batches and runtime neutrality: PASS (2 Python tests)
actor capability bank: PASS
actor family bank: PASS (47 families; 13 unsupported representatives / 14 records)
actor instance queue: PASS
```

A preliminary `python -m unittest tools.saturn...` invocation failed before
test collection because that module form omitted `tools/saturn` from
`sys.path`; rerunning from `tools/saturn` passed 3/3 in 0.472 s. The Make target
uses an explicit path insertion and is the authoritative green invocation.

## Remaining gates

- Production graph/behavior/shadow observer capture of opacity, billboard,
  shadow receiver, effect kind/flags/lifetime, and effect parameters.
- Source-faithful support or explicit prerequisite disposition for
  `GEO_CULLING_RADIUS` and `GEO_BRANCH_AND_LINK`.
- Generic actor meshlet/terrain/effect cross-stream production merge, full
  command/Gouraud reservation, and sourceboot cutover.
- Target compile/link, DRAM artifact, Ymir replay, hardware/manual validation,
  zero-overflow runtime telemetry, visual parity, and FPS evidence.
