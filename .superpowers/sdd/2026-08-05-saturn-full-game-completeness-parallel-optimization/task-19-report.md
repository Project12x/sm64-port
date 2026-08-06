# Task 19 report — bounded articulated/enemy capability admission

Status: source-complete for the narrowed capability-admission slice; Task 19
overall remains source-incomplete until the later actor cutover/closure proves
the runtime articulated path. The generic capability-mask admission API and
closure diagnostics are complete; existing unsupported geo-node families
remain fail-closed. This report does not claim runtime pose/queue/lane/parent/
held/model/despawn/reward integration, complete enemies or bosses, animation
presentation, target/Ymir output, manual playability, or an FPS change.

## Source and closure evidence

The authoritative input is the generated BOB Area 1 closure at
`build/saturn/packages/bob/1/closure.json`: 86 closure actor records and 47
generated family representatives, including behavior children/rewards through
the closure's existing record set. No behavior or family name is used for
runtime admission. Requirements are derived from the source-owned animation
table, geo-node vocabulary, model-variant count, and `spawn:` object roots.

The current closure-derived query reports:

```text
capability    closure records  family representatives  unresolved records
ANIMATED                 8                 7                    0
SWITCH                  33                22                    5
PARENTED                52                34                    8
HELD                     0                 0                    0
MODEL_MUTATION          34                23                    5
LOD                      0                 0                    0
```

The unresolved IDs are emitted directly from the generated report. SWITCH and
MODEL_MUTATION both name:

```text
bhvBreakBoxTriangle[0x08324fd7]
bhvBreakableBoxSmall[0xed463998]
bhvBreakableBox[0xb826b5f0]
bhvExclamationBox[0xcbaa8d1a]
bhvKoopaShellFlame[0x548d47cb]
```

PARENTED names exactly:

```text
bhvBreakBoxTriangle[0x08324fd7]
bhvBreakableBoxSmall[0xed463998]
bhvCheckerboardPlatformSub[0xfc68327b]
bhvKoopaShellFlame[0x548d47cb]
bhvOpenableCageDoor[0x38d3445c]
bhvWaterBombShadow[0xc156e2f8]
bhvWaterBomb[0x8192ed5b]
bhvWoodenPost[0x3d85e20f]
```

The checkerboard family ID is shared by closure records, so these are
diagnostic records/representatives rather than a hand-maintained allow-list.
PARENTED is provenance inventory only and has zero runtime admission because
Task 14 still publishes `NO_PARENT`. HELD and LOD are explicit zero-admission
results: no authoritative `GEO_HELD_OBJECT` or `GEO_RENDER_RANGE` record is
present in this BOB closure.

## Prior-art and reuse record

Reuse mode is an in-tree direct reuse of the Task 18 S64F validation, resealing,
and independent-oracle patterns. This slice does not call or extend the Task 10
pose evaluator or Task 14 snapshot/queue runtime. It adds no enemy-specific
renderer, behavior branch, Mario-only wrapper, or level policy. No external
source was copied or closely ported.

## Implementation

- Added stable C capability constants matching the generated Python class bits
  for ANIMATED, SWITCH, PARENTED, HELD, MODEL_MUTATION, LOD, and the existing
  generic family vocabulary.
- Added `sm64_saturn_actor_family_capability_mask_supported(...)`; family-bank
  validation rejects unknown capability bits, and family selection rejects an
  unknown required mask before scanning records.
- Added `actor_articulated_capability_test.c`, which proves generic admission
  for supported ANIMATED/SWITCH/MODEL_MUTATION records; PARENTED/HELD/LOD and
  unknown query bits fail closed. A stored capability-mask mutation is resealed
  before the validator rejects it, proving the content hash was not the reason.
- Added `test_bob_articulated_capabilities.py`, which derives all six queries
  from closure facts and compares their exact class bits, counts,
  representatives, full record-set digests, unresolved IDs, admission counts,
  and availability dispositions with an independent checked-in oracle. The
  test explicitly retains a behavior-spawned child and boss reward in the
  PARENTED provenance set, while runtime admission remains zero.
- Added the standardized serialized `verify-actor-capability-articulated` Make
  target and the prescribed `test_bob_actor_capabilities.py --class
  articulated` entry point.

## Gates actually run

```text
with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-actor-capability-articulated PASS
  - actor articulated capability C test: PASS
  - six-query exact oracle and closure diagnostic: PASS
  - Python tests: PASS (2 tests)
```

## Fix round 1 — review findings resolved

The prescribed public interface was restored and the complete bounded serial
regression wave was run without parallel make jobs:

```text
powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-actor-capability-articulated verify-actor-pose-bank verify-actor-instance-queue
PASS (132.6 s)
  actor articulated capability: PASS
  exact BOB closure diagnostic: PASS (86 records / 47 representatives)
  Python articulated tests: 2/2 PASS
  actor pose bank fixture: PASS
  actor instance queue: PASS (exit 0)

.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_bob_actor_capabilities.py --class articulated
PASS (5.7 s)
```

The exact oracle pins all six complete query sets with canonical SHA-256 set
digests, not only nonzero counts. It separately pins every unresolved stable
ID/family ID and the explicit unavailable PARENTED/HELD/LOD dispositions. The
stored S64F mutation changes record offset `+4`, reseals the payload digest,
then reaches the unknown-capability-mask validation branch. The prior
`read_file` error-path leaks were also closed.

The full runtime articulated evaluator and all pose/queue/lane/parent/held/
model/despawn/reward/stale-parent proof remain unchecked and deferred to actor
cutover/closure once Task 14 publishes typed immutable state. No target
compiler/link, Ymir, hardware, manual, completeness, or FPS gate was run or
claimed here.
