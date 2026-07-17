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
`77558234613a8b3c04f6f45d00c5eeb03d58537aef902fbca8de6117f6578f31`.
It found one source file with display-list macros:

| Metric | Count |
|---|---:|
| `gsSP1Triangle` | 2 |
| `gsSP2Triangles` | 2 |
| `gsSP1Quadrangle` | 0 |
| statically declared triangles | 6 |

This is a tooling smoke test over the checked-in source, not a representative
SM64 asset census. A real census requires a user-supplied baserom/extraction
input, which is intentionally not committed or distributed.
