# Fixed-camera role-3 Ymir boot/capture evidence — 2026-08-01

## Result

The single authorized retry succeeded. The existing pinned role-3 CUE booted
far enough in Ymir to publish complete SCC1 and SCAR windows, and the repaired
host capture path accepted both reports. No target rebuild or second emulator
attempt occurred.

This is **boot/capture-only evidence**. It is not an FPS measurement, camera
feel evaluation, visual acceptance result, or retail-hardware validation.

The compact machine-readable report is
[`reports/e2-sourceboot-bob-fixed-camera-role3-capture-2026-08-01.json`](reports/e2-sourceboot-bob-fixed-camera-role3-capture-2026-08-01.json).

## Role-3 identity

Three independent fields agree on the fixed-camera candidate:

- requested capture role: `camera-fixed-candidate`;
- ELF absolute camera-variant marker: `3`;
- raw SCC1 header camera variant: `3`.

The ELF camera-route marker is `1`, while the pinned BOB replay manifest and
raw SCC1 header both identify route ID `2` (`bob-default-camera-v1`). This is
the intended role-3 camera-route-1 artifact consuming the BOB route manifest.

## Accepted capture

- SCC1 address/size: `0x002CBB20`, 194,496 bytes.
- SCC1 samples: 600 stable neutral-input samples, source ticks 2000 through
  2599.
- Expected/observed idle-start tick: `0` / `0`.
- Role-aware raw zoom witness: `0x44480000` (`800.0f`).
- Route window address/size: `0x060DDD58`, 176 bytes.
- Replay ticks: 2,000; camera invocations: 2,000.
- Route fault flags: `0`.
- SCAR transport: `READY` / `OK`, with all 3,184,016 expected source-data
  bytes copied.

The capture process exited `0` after 277.9 seconds and left no Ymir process
running.

## Pinned artifact identity

The hashes were confirmed before launch and match the earlier failed-run
report:

| Input | SHA-256 |
| --- | --- |
| Ymir | `fcc88d82b2ea7afdf400bcf67d45139d02354379388f7f9dba731b63a38d3943` |
| BIOS | `96e106f740ab448cf89f0dd49dfbac7fe5391cb6bd6e14ad5e3061c13330266f` |
| CUE | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` |
| ISO | `d495b116e01adfd19a5ed3e375a00345d4556f7dd6ac132a46596f05b57a008e` |
| ELF | `397e603a1ef812a04c66334de6138d244b6c8e69d11696afed3e1c376faa8a1b` |
| SOURCE.DAT | `45ddc102ef09a7ad88b1e1ceb0abe4edcc99cd7b81346bbb53c173e094cafc7f` |
| Route manifest | `656cb9df8ca7a38d04adef0273349c91922db2b9c94d956ea83667b8d07a168f` |

The captured SCC1 report hash is
`c35b4767835d4cf9dfd0a5c6c5797fa2d5163a137b7419c1b13a1ca18423c0d0`;
the SCAR proof hash is
`843087dbd96d3df8d34af419903454779ecf1ab16b4a847ae5b4486b045d1c2e`.

## Scope boundary

The route telemetry includes raw camera timing counters (`last=681`,
`accum=1362961`, `max=788` across 2,000 invocations), but this task does not
interpret them as FPS or compare them with a baseline. The evidence establishes
that the fixed-camera candidate is selected, reaches the deterministic BOB
capture window, and satisfies the role-aware raw capture contract.
