# Host-side source scan — 2026-07-17

The classifier was run against the repository’s available `src` tree without
requiring a Nintendo baserom or extracted assets:

```sh
python tools/saturn/asset_classifier.py \
  --root src \
  --report docs/saturn/evidence/reports/sm64-src-classifier-2026-07-17.json
```

The report is stored at
`reports/sm64-src-classifier-2026-07-17.json` with SHA-256
`ec0a4cfe671c339c1c91c47b66f7d30c005e35417f1074eff409b84bab138539`.
It found one source file with display-list macros:

| Metric | Count |
|---|---:|
| `gsSP1Triangle` | 2 |
| `gsSP2Triangles` | 2 |
| `gsSP1Quadrangle` | 0 |
| statically declared triangles | 6 |

The same scan inventories material/geometry state macros: 28 texture-image
declarations, 4 tile setups, 7 render-mode changes, 6 texture toggles, 4
vertex loads, and 12 display-list calls. These counts are useful compiler
inputs, but do not imply that the checked-in source is a complete SM64 asset
set.

This is a tooling smoke test over the checked-in source, not a representative
SM64 asset census. A real census requires a user-supplied baserom/extraction
input, which is intentionally not committed or distributed.

The six-way decision rules are exercised with the checked-in fixture
`tools/saturn/fixtures/primitives-six-way.json`; its expected report is stored
at `reports/six-way-fixture-classifier-2026-07-17.json`.
Its SHA-256 is
`c9b32c297ad99f15d94df7d3a9bac97823f41821aec8ae49aed5962ade0a2f77`.
