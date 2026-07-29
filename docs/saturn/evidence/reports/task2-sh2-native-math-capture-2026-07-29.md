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

## Post-`376b032` acceptance recapture

The Q16 `atan2` conversion in `376b032` was rebuilt with the pinned Yaul
0.3.1 / `6012f79f237773378c8014e70d8998ad95a38d98` environment and the same
replay profile. The fresh ELF is
`54d36006080f75119376a0bd16a09d99214f8027a04a63c155374b2d56462305` (the CUE
remains `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`).
`make verify` passed the captured-input Q16 fixture and mutation test, the
SH-2 census (`HOT total 0`), and the dual-CPU coherency gate.

Two independent Ymir build-agent2 DRAM-cart captures are retained as
`task2-post376b032-smc1-run1-2026-07-29.json` and
`task2-post376b032-smc1-run2-2026-07-29.json`. Their deterministic verifier
accepted the retained post-conversion fixture
`task2-post376b032-smc1-fixture-2026-07-29.json`: both reached SBR2 tick
2,000 / global timer 2,001, published 64 samples for each SMC1 function, and
reported identical counters (`atan2s=atan2_lookup=125618`).

Against the pre-conversion run, the final Mario position bits are identical,
so the endpoint L-infinity positional divergence is **0.0 world units**
(passing the `< 1.0` requirement). Replay identity fields, action, fault
flags, and command-capacity rejects also match. The live bounded SMC1 rings
are intentionally not byte-equal after the conversion (128/128 entries
differ) and each counter is `+186` (`125432 -> 125618`); the route's
performance counters also vary with the new image. This is recorded rather
than masked: the independently compiled captured-input differential fixture
continues to accept every pre-conversion SMC1 input/result pair, while the
two post-conversion live captures are mutually deterministic.
