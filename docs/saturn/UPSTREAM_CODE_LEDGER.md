# Visual-slice upstream code ledger

Last updated 2026-07-18.

This is the implementation-facing companion to [PROVENANCE.md](PROVENANCE.md).
It answers a narrower question: which reviewed upstream code or design pattern
should inform the next Saturn visual-slice change, where it belongs, and what
is explicitly *not* being imported. Nintendo assets remain local-only inputs;
no extracted ROM asset is committed.

## Rules

- A source may be used only at the pinned revision recorded below.
- "Pattern-only" means the destination implementation is original and keeps
  the target's Yaul interfaces and data layout; it is not a translation.
- Direct-copy, fork, or close-port work needs a source-path note, notices,
  license text, and a change note in `PROVENANCE.md` before it is merged.
- The current visual milestones deliberately prefer small host tools and
  target-specific C over importing another engine runtime.

## M1 — living title face

| Upstream | Pin / license | Inspected code | Reuse mode and concrete destination |
|---|---|---|---|
| [R11/saturn-libs](https://github.com/R11/saturn-libs) | `cecf21a68dfca4388887b28e906bb37b95f0849c` / MIT | `saturn-vdp2/core/saturn_vdp2_bg_core.c`, `saturn-vdp2/saturn/saturn_vdp2_bg_saturn.c`, `saturn-smpc/core/saturn_smpc_core.c`, and their host tests | **Pattern-only.** Use its separable background-state and button-edge-test shape to implement a Yaul NBG1 title backdrop below NBG0 text and a Start edge in `src/port/saturn/introface/main.c`. SGL calls and physical-address conventions are not copied. |
| `yaul-org/libyaul` | `6012f79f237773378c8014e70d8998ad95a38d98` / MIT | VDP1, VDP2, peripheral, DMA, and fixed-point public APIs recorded in `PROVENANCE.md` | **Dependency.** It remains the only target hardware API layer. New title work uses its APIs directly rather than wrapping or importing R11's SGL layer. |
| [yaul-org/libyaul-examples](https://github.com/yaul-org/libyaul-examples) | `66b648eb059bb8bb7392eac70821605a68205b85` / MIT | `vdp2-normal-bitmap/vdp2-normal-bitmap.c` | **Close-port (small configuration pattern).** Its A0/A1 VDP2 VRAM cycle allocation for RGB555 bitmap fetches is adapted in `src/port/saturn/vdp2probe/main.c` and `introface/main.c`; no assets, decoder, or runtime are copied. The probe documents the isolated before/after evidence. |
| `johannes-fetz/joengine` | `556d081146211b6a1cfa6591d70f9487d406758b` / MIT plus BSD-3-Clause-style file headers | `jo_engine/vdp1_command_pipeline.c`, `jo_engine/3d.c` | **Pattern-only.** Retain the existing persistent command lifetime and fixed setup-command prefix; do not add Jo Engine's allocator to the libyaul renderer. |

Implementation commitments:

1. Add an original local-only title-asset converter under `tools/saturn/` when
   the source image/layout is selected. It must emit RGB555/NBG-ready data and
   a manifest with input SHA-256, dimensions, palette/format, and output size.
2. Keep the title face's deformation, eye ordering, and shine in the existing
   VDP1 path. The VDP2 background and `PRESS START` are separate layers so
   their cost is measurable independently.
3. Capture neutral, animated, shine-off, and Start-handoff frames in the
   gallery; record background bytes, VDP1 command count, and frame timing.

## M2–M3 — actor and Castle-lobby asset path

| Upstream | Pin / license | Inspected code | Reuse mode and concrete destination |
|---|---|---|---|
| yaul-org/libyaul libmic3d | 6012f79f237773378c8014e70d8998ad95a38d98 / MIT | libmic3d/render.c, libmic3d/sort.h, libmic3d/mic3d.c | **Dependency / pattern adaptation.** Use its bounded normalized-depth list, near/far rejection, and persistent render-workarea shape to replace M2's bootstrap source-order submission in src/port/saturn/marioturntable/main.c. Keep the actor IR and explicit VDP1 command ownership local; do not adopt libmic3d as an opaque scene runtime. |
| [SaitoTsutomu/Tris-Quads-Ex](https://github.com/SaitoTsutomu/Tris-Quads-Ex) | `f5acd93873728c45d48c3398382aec380a280182` / Apache-2.0 | `__init__.py`, `README.md` | **Pattern-only test oracle.** Its one-selected-edge-per-triangle objective informs an independent regression case for `tools/saturn/quad_pairing.py` and `tools/saturn/test_tools.py`. Keep NetworkX exact matching; do not add Blender or PuLP to the project. Saturn filters for material, winding, convexity, UVs, and deformation stay mandatory. |
| [HailToDodongo/pyrite64](https://github.com/HailToDodongo/pyrite64) | `297a10e606af6149327364d8b694f136c62b506e` / MIT | `src/project/assets/model3d.h`, `collision.h`, `src/renderer/n64Mesh.h`, `animation.h` | **Pattern-only.** Create an original limited Fast3D-to-Saturn IR exporter under `tools/saturn/` with explicit model/material partitions, source primitive IDs, collision data, and animation streams. The C++/desktop/libdragon runtime is not a Saturn runtime candidate and no code is copied. |
| [zeux/meshoptimizer](https://github.com/zeux/meshoptimizer) | `dc9d09ed83e1004aef47a1c3c597e0ec64848a37` / MIT | `src/meshoptimizer.h`, `src/indexgenerator.cpp`, `src/vfetchoptimizer.cpp` | **Pattern-only.** Preserve independent position/attribute/index streams in the new IR so seams and per-material splits survive conversion. Do not introduce a native dependency until host-tool profiling proves it worthwhile. |

Implementation commitments:

1. Define a generated, versioned Saturn IR record before adding a general
   display-list interpreter. Minimum fields: source display-list/primitive ID,
   material and texture key, vertex streams, representation decision and
   rejection reason, plus optional collision and animation references.
2. Keep the current compiler's conservative matching. Add the Apache-pattern
   objective only as a deterministic host regression, never as a reason to
   merge unsafe animated or arbitrary-UV geometry.
3. M2 proves the IR with Mario; M3 uses the same records for Castle Area 1.
   A PC reference renderer and Ymir must capture matching named camera views.

## M3 onward — inspection and debugging evidence

| Upstream | Pin / license | Inspected code | Reuse mode and concrete destination |
|---|---|---|---|
| [VGKintsugi/Ghidra-SegaSaturn-Loader](https://github.com/VGKintsugi/Ghidra-SegaSaturn-Loader) | `c489a190a79d2634b9ecf82e2c0dcec8fd999cf5` / Apache-2.0 | `README.md` and loader layout | **External tool / behavior study.** Use it, if needed, to inspect a generated ISO or emulator save state during M8 debugging. It is not linked to the build and no loader code belongs in the target. |
| `Project12x/Ymir` | `6efc5324943c27f4db7a1b7c8bcf90f51459b12e` / GPL-3.0 | headless JSON-RPC, frame capture, debug-break areas recorded in `PROVENANCE.md` | **External process.** Continue deterministic controller pulses, hashes, and PNG captures. No emulator source or headers enter `sm64-port`. |

## Explicit non-adoptions

- R11's SGL-specific target layer is not a second hardware abstraction; the
  project is Yaul-based.
- Pyrite64's C++/desktop renderer is not an import path for the SH-2 target.
- Tris-Quads-Ex is neither a Blender build dependency nor sufficient geometry
  validation for VDP1.
- The Ghidra loader is a developer-side inspection tool, not a runtime asset
  pipeline.
- GPL engine code remains isolated and attributed under the existing
  `src/port/saturn/gpl/` and `SLAVEDRIVER_ADAPTATION.md` rules; this ledger does
  not expand that adaptation boundary.

## Per-change checklist

Before implementing a row, add the exact source paths used to the commit note
and update `PROVENANCE.md` if the reuse mode changes. Before accepting it,
record a reproducible host test or Ymir capture in `evidence/` and link the
result from the visual timeline.
