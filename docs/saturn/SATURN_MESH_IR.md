# Saturn Mesh IR v1

The Saturn mesh IR is the host-side boundary between source-specific SM64
extractors and target-specific VDP1 compilation. Version 1 is intentionally
small: it proves topology preservation and animation-safe quad selection
before texture conversion and runtime skeletal deformation are added.

The normative machine-readable shape is
`tools/saturn/schemas/saturn-mesh-ir-v1.schema.json`. The stricter target
validation and compiler live in `tools/saturn/saturn_mesh_ir.py`.

## Source contract

A source document records:

- signed 16-bit base positions;
- stable source IDs for every triangle;
- material IDs and RGB555 fallback colors;
- optional per-vertex UVs;
- optional variable-length Q15 deformation influences and an explicit
  `linear_blend` or `goddard_weighted_accumulation` mode;
- zero or more fully evaluated deformation poses used only for offline safety
  validation.

Attribute seams must use distinct vertex indices. This follows the
multi-stream/equivalence lesson from meshoptimizer without copying its code:
vertices are only topologically identical when every representation-relevant
stream agrees.

## Compiled contract

`saturn_mesh_ir.py` validates the source document and emits version 1 of
`sm64-saturn-compiled-mesh`. Every primitive is explicitly classified as:

- `quad`: two source triangles compiled to one ordered VDP1 quadrilateral; or
- `triangle_fallback`: one source triangle represented by repeating its final
  vertex in a four-corner VDP1 command.

Each primitive retains its original source triangle IDs. A quad is eligible
only when material, topology, winding, normal alignment, and sampled-camera
convexity are safe in the base mesh **and every supplied deformation pose**.
The exact NetworkX matcher then maximizes command savings among the candidates.

Validation poses are compiler evidence, not animation data shipped to Saturn.
Future source adapters should evaluate important animation extrema and feed
those positions to the compiler. Runtime deformation will consume the compact
joint and Q15 weight streams instead.

`linear_blend` requires each vertex's Q15 weights to sum to 32768. Goddard's
intro-face deformation is different: the original engine subtracts every
explicit weight from an implicit base coefficient and then accumulates each
joint's transformed offset. Its explicit totals may exceed 100%, so
`goddard_weighted_accumulation` deliberately does not normalize or impose a
four-influence cap.

## Commands

Bootstrap and test the host tools:

```sh
make -f Makefile.saturn.mk bootstrap-host-tools
make -f Makefile.saturn.mk verify-tools
```

Regenerate the intro face header, quad report, and compiled generic IR:

```sh
make -f Makefile.saturn.mk compile-introface-mesh
```

Compile any conforming source document directly:

```sh
.venv-saturn-tools/bin/python tools/saturn/saturn_mesh_ir.py \
  --input source.mesh.json \
  --output compiled.mesh.json \
  --report compiled.report.json
```

On Windows, use `.venv-saturn-tools/Scripts/python.exe`.

## Current limits

- Version 1 validates deformation streams but does not yet evaluate joint
  matrices.
- UV streams are preserved, but version 1 deliberately forces textured
  triangles to explicit fallbacks until VDP1 rectangular texture compatibility
  is implemented. Texture conversion, CLUT placement, draw state, and
  near-plane clipping remain work for the in-game Mario milestone.
- Safety is bounded by supplied deformation poses and the documented camera
  sample grid; untested poses can still invalidate a quad.
- The intro face currently has no validation poses, so its 156 quads prove the
  neutral camera envelope only. Adding source-derived face motion samples is
  the next visible step.
