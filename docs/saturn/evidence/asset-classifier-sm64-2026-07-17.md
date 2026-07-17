# SM64 geometry/UV classifier baseline — 2026-07-17

The host-side classifier was run over the checked-out SM64 source tree and the
six-way decoded fixture. This is a source-inventory and conversion-planning
baseline; it does not claim that the original N64 display lists are already
decoded into Saturn command lists.

## Source inventory

The scan found 733 files containing triangle macros and 735 files containing
geometry/state macros:

| Macro | Count |
|---|---:|
| `gsSP1Triangle` | 3,798 |
| `gsSP2Triangles` | 29,119 |
| `gsSP1Quadrangle` | 2 |
| static triangles represented | 62,036 |
| `gsSPVertex` | 8,580 |
| `gsSPTexture` | 2,116 |
| `gsDPSetTextureImage` | 2,287 |

## Six-way fixture classification

The decoded fixture produced one candidate for each of the six planned
representations: one direct textured quad, one direct untextured triangle, one
textured degenerate triangle, one split/cropped surface, one baked surface,
and one effect fallback. Two additional pair candidates were rejected for
material mismatch or non-quad topology.

The machine-readable report is
[`asset-classifier-sm64.json`](reports/asset-classifier-sm64.json). Re-run it
from the repository root with:

```sh
python tools/saturn/asset_classifier.py \
  --root . \
  --primitives tools/saturn/fixtures/primitives-six-way.json \
  --report docs/saturn/evidence/reports/asset-classifier-sm64.json
```

Report SHA-256: `c5f9a90beb83ac0e26e3966527700dc7a1b64c3032caa1fea5c99d8f208742a2`.
