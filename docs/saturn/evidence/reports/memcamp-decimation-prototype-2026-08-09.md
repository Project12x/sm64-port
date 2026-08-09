# SeamAwareDecimater offline terrain-decimation prototype — 2026-08-09

Task 7 of `docs/superpowers/plans/2026-08-09-memory-residency-campaign.md`
(parallel lane). This is an **offline, prototype-only** exercise: no
build-system integration, no identity wiring, no `verify-*` target, no
Makefile changes. It exists to give OWNER GATE G3 a real, measured
fidelity/size tradeoff for seam-aware terrain decimation before any decision
to build the identity-wired stage (Task 8, explicitly deferred).

## Status

Complete. Real 50% and 25% decimation runs on the actual BOB area-1 terrain
mesh, real GREEN-twice determinism proof at both levels, real post-pairing
primitive/BSP-node/byte counts from the unmodified downstream compilers, and
a real visual wireframe artifact. Two genuine technical blockers were hit and
fixed with documented, geometry-preserving preprocessing (bowtie-vertex
splitting; post-quantization degenerate-triangle filtering) — both described
below rather than papered over.

## 1. Clone, pin, and build

**SeamAwareDecimater** — `https://github.com/songrun/SeamAwareDecimater`,
commit `c69934356ecdb0dd91070a6fc0520cdb0cc4d983` (2020-03-18, `master`),
**MIT License** (verified: `LICENSE` at that commit is the standard MIT text,
Copyright (c) 2017 Songrun Liu). Cloned into
`work/upstream/seam-aware-decimater/` in this worktree (gitignored via the
repo's existing `/work/` rule — confirmed, not a new convention).

Two build dependencies, pinned era-appropriate (both referenced by the
repo's own `.gitmodules`/`CMakeLists.txt` but not vendored):

- **libigl** — `https://github.com/libigl/libigl`, commit
  `4ce917d424efd941174c6ff46adf15790c2ad12a` (2020-02-29, the last commit at
  or before SeamAwareDecimater's own last commit date). Cloned into
  `work/upstream/seam-aware-decimater/ext/libigl/`. MPL-2.0; used
  header-only (every required header ends in
  `#ifndef IGL_STATIC_LIBRARY / #include "x.cpp" / #endif` — confirmed for
  all 17 headers this build actually needs), so nothing is compiled or
  distributed from it beyond what this offline prototype already does not
  distribute (`work/` stays untracked).
- **Eigen** — `https://gitlab.com/libeigen/eigen`, tag `3.3.7` (commit
  `21ae2afd4edaa1b69782c67a54182d34efe43f9c`, 2018-12-11 — the newest
  release predating SeamAwareDecimater's last commit; 3.3.8/3.3.9 postdate
  it). Cloned into `work/upstream/seam-aware-decimater/ext/eigen/`. MPL-2.0,
  header-only.

**Discrepancy from the plan, flagged as instructed:** the plan states the
repo "has zero CMake today." That is false — a `CMakeLists.txt`
(`cmake_minimum_required(VERSION 2.8.6)`) exists at the pinned commit, with
two custom `cmake/Find{EIGEN,LIBIGL}.cmake` modules expecting a
system-installed dependency layout. The plan's *preference* (hand-rolled
Makefile) was followed anyway — it sidesteps the ancient CMake floor and the
custom Find-modules' assumptions, and is a five-minute, ~50-line file given
both dependencies are header-only. See
`work/upstream/seam-aware-decimater/Makefile.mingw` (created this task,
comment block at its top repeats this same discrepancy note) for the actual
build rules.

**Build:** MSYS2 MinGW64, `g++.exe (Rev5, Built by MSYS2 project) 16.1.0`,
`-std=c++11 -O2 -DIGL_NO_MOSEK`. All 8 source files (the 7 `DEC_LIBS`
objects from the real `CMakeLists.txt` plus `decimater.cpp`) compiled clean
(warnings only — sign-compare, unused-typedef, OpenMP pragmas ignored since
no `-fopenmp` was passed; none are fatal or behavior-relevant for a
single-mesh CLI run). Command:

```
cd work/upstream/seam-aware-decimater
PATH=/mingw64/bin:$PATH make -f Makefile.mingw
```

**Binary:** `work/upstream/seam-aware-decimater/decimater.exe`,
1,002,781 bytes.

```
SHA-256: 0d04b80a5a98d103501c2b0a7439946febdb06765909f0a1f2eae01848759bd9
```

Smoke-tested against the repo's own bundled `models/animal.obj` (50%,
`--strict 2`) before touching any real Saturn data — exit 0, wrote a
1.64 MB decimated OBJ, matches the upstream README's documented usage
exactly.

**Runtime-environment gotcha (fixed in the shim, not just noted):**
`decimater.exe` dynamically links `libstdc++-6.dll` / `libgcc_s_seh-1.dll` /
`libwinpthread-1.dll` from its MSYS2 toolchain directory. Invoked with a bare
`PATH` (e.g. from `.venv-saturn-tools`, outside an MSYS2 login shell), the
process does not fail to start cleanly — it starts and crashes with
`STATUS_ACCESS_VIOLATION` (Windows exit code `3221225477` / `0xC0000005`)
before `main()` runs any user code. Confirmed by direct A/B: identical
invocation, only difference is `C:\msys64\mingw64\bin` on `PATH` — crash vs.
exit 0. `tools/saturn/mesh_ir_obj_shim.py`'s `run_decimater_binary()` now
injects that directory onto the subprocess's `PATH` unconditionally, and also
resolves the binary/input/output paths to absolute (the separately-known
`.venv-saturn-tools` `subprocess.Popen` relative-path defect otherwise fails
closed with `FileNotFoundError`/`WinError 2` even though the file exists).

## 2. The IR↔OBJ shim, RED then GREEN

`tools/saturn/mesh_ir_obj_shim.py` / `tools/saturn/test_mesh_ir_obj_shim.py`.
Confirmed RED first (`ModuleNotFoundError: No module named 'mesh_ir_obj_shim'`
before the implementation existed), then implemented, then GREEN
(`mesh_ir_obj_shim: PASS`, 11 test functions, run via
`.venv-saturn-tools/Scripts/python.exe tools/saturn/test_mesh_ir_obj_shim.py`,
matching this repo's existing plain-assert `test_*.py` convention — no
pytest installed).

**Round-trip byte-identical proof (decimation disabled):** one test runs
the whole real generated mesh —
`build/saturn/sourceboot/generated/bob_area1_mesh_ir_v2.json`
(schema `sm64-saturn-mesh-ir` v2, 1,625 positions, 1,101 triangles, 18
materials, verified real-file numbers, matching the plan's summary) —
through `mesh_ir_to_obj()` then `obj_to_mesh_ir(..., reset_source_ids=False)`
and asserts the canonical (`sort_keys=True, indent=2`) JSON serialization of
the result equals the original byte-for-byte. It also runs the reconstructed
document through the real, unmodified `saturn_mesh_ir.validate_mesh_ir()`.
Both pass. (A second, hand-built two-material fixture exercises the same
path independent of build-tree state, so the test suite is not silently
skipped in a from-scratch checkout without a prior real build.)

Design properties this proves (see the module's own docstring for the full
reasoning): `positions`/`vertex_attributes.uv` re-quantize losslessly through
integer OBJ text (`quantize_component`: deterministic round-half-away-from-
zero); `texture_tile` — confirmed constant per material across all 1,101 real
triangles (`build_material_texture_tile_table` would raise otherwise) —
round-trips via a material-id side channel (OBJ `usemtl material_<id>`
groups) rather than through OBJ text, since OBJ has no field for it; `source`
round-trips exactly when face order is preserved (true for this disabled-
decimation path only).

## 3. Real decimation runs, GREEN-twice determinism

Driven via the shim's CLI (`--mode decimate`), one material at a time
(materials never share a welded vertex in the real mesh — confirmed by
direct check across all 1,625 positions — so per-material decimation cannot
merge geometry across a material/texture boundary).

**Two real blockers hit and fixed, not routed around:**

1. **Bowtie (non-manifold-fan) vertices.** The very first real invocation
   (material 0, 50%) crashed:
   `Assertion failed: (EF(e,1) == ff || EF(e,0) == ff) && "e should touch ff", file ext/libigl/include/igl/circulation.cpp, line 30`.
   Diagnosis (by hand, on material 0's 186-vertex/173-triangle submesh):
   zero non-manifold *edges*, zero duplicate/degenerate faces, but **4
   vertices whose incident triangles do not form one edge-connected fan**
   (e.g. vertex 24: 8 incident triangles split into 2 disconnected fans) —
   real terrain micro-islands of the same material happen to touch at one
   welded vertex. `igl::circulation`/`edge_flaps` assume a single fan per
   vertex. Fix: `_split_bowtie_vertices()` in the shim, applied inside
   `material_submesh_obj()` before emitting OBJ — duplicates the vertex once
   per extra fan component (both copies at the identical position/uv;
   no triangle's shape or texture mapping changes; no new crack is
   introduced, since the fans were already disconnected). Deterministic
   (pure union-find over face order, no hash/set-iteration-order dependence
   in the output). After the fix, all 18 materials decimated successfully at
   both 50% and 25%.
2. **Post-decimation degenerate triangles.** With the bowtie fix in place,
   `compile_bob_bsp.py` (Section 4) failed on the first decimated scene:
   `ValueError: source polygon 470 is degenerate`. Traced to two boundary-
   protected vertices in material 8 that both survived decimation *unmoved*
   at the identical position (observed directly in `decimater.exe`'s own OBJ
   output — exact integer duplicates, not a `quantize_component` rounding
   artifact), which a later triangle-fallback primitive then used as two of
   its three corners. Fix: `reassemble_decimated_mesh_ir()` now checks every
   reassembled triangle's three (already-quantized) positions for zero
   cross-product area and drops it, recording full details
   (material/indices/positions) in the driver's report rather than silently
   discarding. **2 triangles dropped, both material 8** — see
   `docs/saturn/evidence/reports/memcamp-decimation-prototype-2026-08-09.json`
   (`decimated_50_and_25_percent_identical_result.degenerate_triangle_records`)
   for the exact records. `compile_bob_bsp.py`/`saturn_mesh_ir.py` were not modified;
   both blockers were fixed entirely inside the shim, which is exactly where
   this kind of decimator-output sanitization belongs.

**Runs performed** (`--decimater-binary work/upstream/seam-aware-decimater/decimater.exe --strict 2`):

| Run | Target | Output SHA-256 (canonical JSON) |
|---|---|---|
| A | 50% | `97e1e94b6be4a308d33128ab22b603fd5fa6b2a1e1422b6120cf0eb5032c53bf` |
| B | 50% | `97e1e94b6be4a308d33128ab22b603fd5fa6b2a1e1422b6120cf0eb5032c53bf` |
| A | 25% | `97e1e94b6be4a308d33128ab22b603fd5fa6b2a1e1422b6120cf0eb5032c53bf` |
| B | 25% | `97e1e94b6be4a308d33128ab22b603fd5fa6b2a1e1422b6120cf0eb5032c53bf` |

**GREEN-twice determinism: proven.** All four runs are byte-for-byte
identical (`diff` clean, matching SHA-256) — two independent runs each at
50% and 25%, confirmed via `diff` and `sha256sum`, not just report-field
comparison. No nondeterminism of the `__pycache__`-mtime-drift class was
observed; `decimater.exe`'s edge-collapse priority queue and this shim's own
preprocessing (bowtie split, quantization, degenerate filter) are all
functions of file content and face order only.

**A genuinely surprising, real finding:** the 50% and 25% target-vertex
requests produced **byte-identical output** — not just similar, identical.
Root cause, verified directly: `--strict 2` protects every mesh *boundary*
edge the same way it protects a true UV seam (see `decimater.cpp`'s
`seam_vertex_edges` construction), and this per-material terrain data is
heavily fragmented into disconnected micro-islands sharing one texture.
Measured directly across all 18 materials post-bowtie-split: **1,591 of
2,447 unique edges (65.0%) are boundary edges**, i.e. protected from
collapse regardless of strictness target. Both nominal targets (50%, 25%)
sit below the natural floor the algorithm reaches once every non-boundary,
non-foldover-risking collapse is exhausted, so both converge to the same
real result: **1,625 → 1,532 positions** (−5.7%), **1,101 → 934 raw
decimated triangles → 932 after dropping the 2 degenerate slivers**
(−15.3%). This is real, useful information for the G3 fidelity decision:
naive "50%"/"25%" labels do not describe what this algorithm actually does
to this specific, highly-fragmented terrain mesh at `--strict 2`.

## 4. Real downstream tool output (unmodified `saturn_mesh_ir.py` / `compile_bob_bsp.py`)

Invocation pattern taken verbatim from `Makefile.saturn.mk`'s
`compile-bob-area`/`compile-bob-bsp` targets (lines ~1616–1637), run by hand
against the decimated IR (50%/25% are the same file — Section 3):

```
saturn_mesh_ir.py --input <decimated_ir> --output <compiled.json> --report <report.json>
compile_bob_bsp.py --input <compiled.json> --output <bsp_report.json> \
    --header <bsp.h> --manifest build/saturn/sourceboot/generated/bob_tiles_manifest.json
```

The real `bob_tiles_manifest.json` from the current build was reused
unchanged — decimation removes/merges geometry but does not touch material
or texture-tile assignment, so the existing manifest remains valid input.

**Baseline re-verified against the real current generated files** (not
trusted from the plan summary — task's explicit instruction):

| Metric | Real baseline (verified) | Source file |
|---|---|---|
| Positions | 1,625 | `bob_area1_mesh_ir_v2.json` |
| Triangles | 1,101 | `bob_area1_mesh_ir_v2.json` |
| Materials | 18 | `bob_area1_mesh_ir_v2.json` |
| Render primitives (post-pairing) | **867** | `bob_area1_compiled.json` (`report.render_primitive_count`) |
| BSP node count | **1,183** | `bob_bsp.h` (`SM64_SATURN_BOB_BSP_NODE_COUNT`) / `bob_area1_bsp_report.json` |
| BSP ref count | 1,425 | `bob_bsp.h` (`SM64_SATURN_BOB_BSP_REF_COUNT`) |
| `estimated_fragment_resident_bytes` | 773,920 B | `bob_area1_bsp_report.json` |
| `all_16x16_fragment_resident_bytes` | 337,280 B | `bob_area1_bsp_report.json` |
| `vdp1_texture_budget_bytes` (fixed hardware constant) | 446,432 B (**this is the plan's "~447 KB"** figure) | `bob_area1_bsp_report.json` / `compile_bob_bsp.py` `MAX_RESIDENT_BYTES` |

Confirms the plan's cited "867 primitives / 1,183 nodes" exactly, and
identifies the plan's "~447 KB scaled LWRAM" precisely as the fixed
`vdp1_texture_budget_bytes` constant (446,432 B), not a per-run computed
value — it is the same in every run, before and after decimation, by
construction (it is a hardware budget, not a measurement). The
per-run **measured** resident-byte figures that actually move are
`estimated_fragment_resident_bytes` and `all_16x16_fragment_resident_bytes`,
both already over that fixed budget at baseline (this compiler's own
"estimated" field is a worst-case, no-texture-reuse figure — the actual
packed/deduplicated allocation, computed by a *different*, not-run-in-this-
task tool, `bake_bob_bsp_fragments.py`, is a real 326,560 B today, comfortably
under budget; Step 3 named only `saturn_mesh_ir.py`/`compile_bob_bsp.py`, so
this task did not re-run the fragment-bank baker).

**Decimated (50%/25%, identical mesh — Section 3):**

| Metric | Decimated | Δ vs. baseline |
|---|---|---|
| Positions | 1,532 | −93 (−5.7%) |
| Triangles (after degenerate-drop) | 932 | −169 (−15.3%) |
| Render primitives (post-pairing) | **727** | −140 (**−16.2%**) |
| BSP node count | **945** | −238 (**−20.1%**) |
| BSP ref count | 1,191 | −234 (−16.4%) |
| `estimated_fragment_resident_bytes` | 612,832 B | −161,088 B (−20.8%) |
| `all_16x16_fragment_resident_bytes` | 284,800 B | −52,480 B (−15.6%) |
| `vdp1_texture_budget_bytes` (fixed) | 446,432 B | unchanged (fixed constant) |

Quad-pairing report also gained a small material-8-only anomaly worth
recording: before the degenerate-triangle fix, `saturn_mesh_ir.py`'s report
showed a `"degenerate_triangle": 1` rejection reason that does not appear in
the baseline report at all — the tell that led to Section 3's second fix.
After the fix, the rejection-reason breakdown is clean (no `degenerate_triangle`
entry), matching baseline's shape.

## 5. Visual artifact

Dependency-free (no matplotlib/trimesh/numpy installed in
`.venv-saturn-tools`, and this is an offline prototype — not worth adding a
new dependency to the pinned tool venv for one visual) isometric wireframe
SVG renderer, written for this task and run once, not committed as a
separate tool (the two output SVGs are the deliverable):

- `docs/saturn/evidence/screenshots/memcamp-decimation-wireframe-baseline-2026-08-09.svg` — 1,625 vertices / 1,101 triangles / 2,447 unique wireframe edges.
- `docs/saturn/evidence/screenshots/memcamp-decimation-wireframe-decimated-2026-08-09.svg` — 1,532 vertices / 932 triangles / 2,162 unique wireframe edges.

Both use the same isometric projection (`(x−z)cos30°, (x+z)sin30° − y`) and
viewport scale, so they are directly visually comparable — the decimated
render is visibly sparser across the terrain's flatter regions while
retaining the mountain's silhouette and the disconnected micro-island
boundaries (expected, since `--strict 2` protects exactly those boundaries).
Both files are real, inspectable SVG (well-formed, no NaN/Infinity
coordinates, verified). Since 50% and 25% targets converge to the identical
mesh (Section 3), one decimated render covers both requested levels; this is
stated explicitly rather than generating a visually-identical duplicate
under a different filename.

## Blockers and concerns (summary)

- **Plan-premise correction:** SeamAwareDecimater already has a
  `CMakeLists.txt` at the pinned commit; the plan's "repo has zero CMake
  today" is inaccurate. Did not change the outcome (hand-rolled Makefile was
  still the right, and preferred, call) but is flagged per instruction.
- **Two real, fixed technical blockers** (bowtie vertices; post-quantization
  degenerate triangles) — both are genuine properties of decimating a
  fragmented, decomp-derived terrain mesh that a smooth single-manifold demo
  model (`models/animal.obj`) would never surface. Both fixes live entirely
  in `tools/saturn/mesh_ir_obj_shim.py`; `saturn_mesh_ir.py` and
  `compile_bob_bsp.py` were run unmodified throughout.
- **Real fidelity finding for G3:** at `--strict 2`, this specific terrain's
  heavy fragmentation (65% boundary edges) means 50% and 25% vertex targets
  both hit the same natural collapse floor (~94% of vertices survive), while
  downstream compiled metrics move more (primitives −16%, BSP nodes −20%) —
  seam-aware decimation at this strictness is a modest, boundary-respecting
  cleanup on this dataset, not an aggressive LOD reduction. A less strict
  setting (`--strict 0` or `1`) was not run — out of scope for this task,
  which pinned `--strict 2` explicitly — but would be the natural next probe
  if G3 wants a larger reduction.
- `work/upstream/seam-aware-decimater/` (including `ext/libigl`, `ext/eigen`)
  is untracked, matching the existing `/work/` gitignore convention —
  confirmed already covered, no `.gitignore` change needed.
