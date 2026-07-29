# Task 2 SMC1 corpus and 2,000-tick replay evidence — 2026-07-29

Status: evidence/tooling complete; **no engine-math conversion in this task**.

## Capture contract

- Route: `bob-parity-v1`, extended deterministically from 600 to 2,000
  source-input ticks in both `tools/saturn/routes/bob_parity_v1.json` and the
  sourceboot `sBobParityV1` table.
- SMC1 v2 retains a bounded 64-entry ring for each of `atan2s` and
  `atan2_lookup`. Every entry records original `y`/`x` IEEE-754 binary32 bits
  and the returned unsigned 16-bit angle; this replaces v1's last-tuple-only
  telemetry.
- The fixture verifier rejects a missing/incomplete per-function corpus,
  requires the exact 2,000-tick SBR2 endpoint, artifact equality, and equality
  of all SMC1 fields and corpus entries across two independent captures.

## Reproducible sourceboot/Ymir result

Build profile:

```text
SATURN_DEMO_PATH=1 SATURN_SOURCEBOOT_ROUTE_REPLAY=1
SATURN_DEMO_VIEW_RADIUS=6000 SATURN_DEMO_POLY_TIER=0
SATURN_DEMO_HOT_PROMOTION=1 SATURN_DEMO_NEAR_CLIP=1
SATURN_DEMO_BSP_ORDER=1 SATURN_DEMO_BSP_FRAGMENTS=0
```

`make ... verify` passed with the native-math census reporting `HOT total 0`
and the dual-CPU coherency gate reporting success. The fresh replay ELF
(`acbe122bac3320834224fd8bfd8b563b874f15cc8b7e6010330e2320f9d0065c`)
published SMC1 at `0x060dcbd4` and SBR2 at `0x060dd1f0`.

Two independent sourceboot/Ymir captures reached the identical frozen
endpoint: `replay_ticks=2000`, `global_timer=2001`, zero route faults,
zero command-capacity rejects, and `atan2s_calls=atan2_lookup_calls=125432`.
Each function published all 64 bounded samples. The verifier accepted the
full corpus and emitted
`tools/saturn/fixtures/bob_parity_v1_atan2_smc1_v1.json` from:

- `tools/saturn/fixtures/bob_parity_v1_smc1_run1.json`
- `tools/saturn/fixtures/bob_parity_v1_smc1_run2.json`

## Verification

```text
.venv-saturn-tools/Scripts/python.exe tools/saturn/test_verify_math_route_capture.py
# 2 tests OK

.venv-saturn-tools/Scripts/python.exe tools/saturn/verify_math_route_capture.py \
  tools/saturn/fixtures/bob_parity_v1_smc1_run1.json \
  tools/saturn/fixtures/bob_parity_v1_smc1_run2.json \
  --fixture tools/saturn/fixtures/bob_parity_v1_atan2_smc1_v1.json
# accepted complete deterministic v2 corpus at tick 2,000
```

The corpus is a bounded deterministic differential-input fixture, not an
exhaustive trace. A subsequent conversion remains responsible for its
host-differential tolerance and for the stated <1.0-world-unit positional
divergence A/B gate, using this 2,000-tick route.
