# Sprint 2 Task T2.24 — quad remeshing for Mario, evaluated against T2.21

- Date: 2026-08-17. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `5b332a5d`.
- Task: evaluate field-aligned quad remeshing for the Mario actor against
  T2.21's triangle-simplification result, and answer one specific question —
  **can quad remeshing deliver 50%'s command count at 75%'s appearance?**
- **Answer: no. The quad route's command floor is 452, above the 50% level's
  418, and it does not beat T2.21's error-per-command curve.** The reasons are
  structural rather than tuning, and they are measured in §4 and §6.
- **Recommendation: keep meshoptimizer.** There is one narrow case where the
  quad route is the safer buy and it is stated plainly in §9.
- **No product build, no emulator, no GUI.** Three new host tools, two evidence
  JSONs, seven PNGs. Nothing under `src/port/saturn` was touched.
- Commits: `55ab955f` (tools and evidence), and this document. `SKIP_CHANGELOG=1`.
- Evidence: `sprint2-t2_24-mario-quad-remesh.json`,
  `sprint2-t2_24-lod-comparison.json`.

---

## 1. The four answers up front

**A1 — The remesher works, and the pose problem the brief flagged is real but
solved.** QuadriFlow produces 100% quads, watertight and manifold, on Mario's
geometry. Poses transfer by barycentric projection and are **exact in kind**:
a barycentric point of a deformed original triangle lies *on* the deformed
original surface by construction, so every transferred vertex has zero
vertex-to-surface error in all 107 frames and the entire residual is the
remesh's own surface approximation (§5). The walking sheets are that claim
executed — they render from *transferred* pose banks, and Mario walks. **Pose
transfer is not the blocker.**

**A2 — The blocker is that 39% of Mario cannot be remeshed at all.** Mario is
not one surface. It is **15 separate closed genus-0 shells**, and its material
patches inside the head and torso are *open*. QuadriFlow does not preserve an
open patch's boundary loop — on three of Mario's five open untextured patches
it **closes the hole**, growing the patch over its neighbour. So the head, the
cap, the face, the torso and all 50 textured triangles — 308 of 788 triangles
— stay as original triangles. They contribute a fixed **320 VDP1 commands**
that no remesh density can touch (§4).

**A3 — Where remeshing does apply, it detaches Mario's limbs.** Every one of
the 13 remeshed patches **shrinks**: mean 84% of its original bounding-box
diagonal, worst 62%, and the figure is roughly constant across densities, so
it is intrinsic and not a coarseness artefact. Because Mario's shells abut
rather than share vertices, a shrinking arm pulls away from the torso and
opens a visible joint gap. It is on the page in
`mario-quadremesh-neutral-solid.png`, rows 2–5, at every yaw (§6).

**A4 — On a single common metric, quads do not beat triangles.** T2.21's error
number is meshoptimizer's own, normalised per job, and a remesher does not
produce one. `compare_mario_lod.py` therefore measures both approaches the
same way from geometry alone. At matched surface error the quad route costs
*more* commands, not fewer (§7).

---

## 2. The pictures

All seven sheets are in `docs/saturn/evidence/screenshots/`. They use the same
cell size, the same four yaws, the same fitted bounds and the same
dependency-free renderer as T2.21, so they can be laid directly beside
`mario-decimation-neutral-solid.png`.

| File | What it shows |
| --- | --- |
| `mario-quadremesh-neutral-solid.png` | **Start here.** Neutral pose, 4 yaws × 5 levels, shaded. |
| `mario-quadremesh-provenance.png` | **Then here.** Green = remeshed quads, orange = untouched. This is the single most informative sheet in the task. |
| `mario-quadremesh-walk-f20-solid.png` | Walking frame 20 — rendered from *transferred* pose banks. |
| `mario-quadremesh-walk-f48-solid.png` | Walking frame 48, a second stride. |
| `mario-quadremesh-neutral-wireframe.png` | Neutral wireframe; the head's wire density is identical in all five rows because it is untouched. |
| `mario-quadremesh-walk-f20-wireframe.png` | Walking frame 20, wireframe. |
| `mario-quadremesh-textured-fixed-cost.png` | The 50 textured primitives in magenta — identical in all five rows. |

### 2.1 What the renders actually say

- **The head, face and cap are pixel-for-pixel the original in every row.**
  That is not a compliment to the remesher; it is because they were excluded
  (§4). It is nonetheless the quad route's one genuine advantage, since T2.21's
  own §2.1 says the 50% level's visible damage is "the cap loses its crisp brim
  edge and the face flattens into fewer planes" — damage the quad route cannot
  inflict.
- **The arms and shoulders break.** In the original row the red sleeve runs
  continuously into the white glove. From `quad_100` down, the sleeve is a
  shrunken stub, and there is a visible gap between shoulder and glove. This is
  §6's shrinkage made visible and it is the reason for the recommendation.
- **The shoes and gloves lose volume** the same way, most obviously at yaw 200.
- **The walking sheets articulate correctly.** Whatever else is wrong, the
  barycentric pose transfer is not.

### 2.2 The renderer, and one thing it cannot show

`tools/saturn/render_mario_quad_ab.py` reuses `render_mario_ab.py`'s rasteriser
and PNG encoder wholesale. Same caveats as T2.21 §2.2: these are host renders
of the geometry, flat-shaded from the RGB555 material table, not Saturn frames.

**One caveat is new and matters here.** This renderer draws a quad as two flat
triangles. **VDP1 does not** — it rasterises a four-vertex command as a
bilinear patch, so a non-planar quad warps on hardware in a way these pictures
cannot show. That is precisely why §8 measures the planarity distribution
instead of asking the reader to look for it.

---

## 3. Licensing — the constraint that survived, and the one that did not

The brief corrected T2.21 on two points and both corrections were applied.

- **Using a GPL tool does not invoke its licence.** Every mesh tool was
  eligible for this evaluation. T2.21's rejection of MeshLab and Blender "for
  licensing" was wrong, and §10's candidate table does not repeat it.
- **This project is GPL-compatible**, so copying GPL source is permitted where
  `UPSTREAM_CODE_LEDGER.md` authorises it.

**The surviving constraint did not bind:** nothing was vendored. QuadriFlow is
BSD-3-Clause, and it is used as a **dependency** — cloned into gitignored
`work/`, compiled to a host binary, never copied into the tree. No file landed
in `src/port/saturn/gpl/` because no GPL-derived code landed anywhere.

One upstream change notice, recorded because it is a modification to a
third-party source even though it stays in `work/`:

> `work/upstream/quadriflow/src/dedge.cpp:18` — the `#if defined(_WIN32)` guard
> around the MSVC intrinsic `_InterlockedCompareExchange` was changed to
> `#if defined(_WIN32) && !defined(__GNUC__)` so that MinGW takes the
> `__sync_bool_compare_and_swap` branch instead. One line; without it the
> project does not compile with g++ 13.1 on Windows. Recorded in the evidence
> JSON as `remesher.local_build_patch`.

---

## 4. Mario's topology — why this evaluation is decided before any density is chosen

This is the section that determines the outcome, and it is pure measurement.

### 4.1 The mesh is unusually clean

| Census over the shipped 788 triangles | Result |
| --- | ---: |
| distinct vertex positions | **424 of 424** — no duplicates to weld |
| distinct edges | 1,182 |
| edges with exactly two incident triangles | **1,182 of 1,182** |
| boundary edges | **0** |
| non-manifold edges | **0** |
| connected components | **15** |
| Euler characteristic of every component | **χ = 2** (closed, genus 0) |

Fifteen closed, manifold, genus-0 shells, symmetric in pairs (34/34, 20/20,
17/17, 16/16, 15/15, 12/12) — head+cap, torso, pelvis, two gloves, two upper
arms, two forearms, two thighs, two lower legs, two shoes. **This is textbook
input for a field-aligned remesher.** Nothing here is the obstacle.

### 4.2 The obstacle is the material partition

Every material is *either* wholly textured *or* wholly untextured — a cleaner
separation than T2.21's structure suggested:

| Set | Materials | Triangles |
| --- | --- | ---: |
| textured | 1, 3, 4, 5, 6 | **exactly the 50** |
| untextured | 0, 2, 7, 8, 9, 10 | 738 |

Splitting by (component, material) gives 23 patches in three classes:

| Class | Patches | Triangles | Remeshable? |
| --- | ---: | ---: | --- |
| closed, untextured | **13** | **480** | **yes** |
| open, untextured | 5 | 258 | no — §4.3 |
| textured | 5 | 50 | no — held out by design |

The five open untextured patches are `c00/m2` (43), `c00/m7` (80), `c00/m8`
(27), `c01/m0` (83), `c01/m2` (25) — i.e. **all of the head and all of the
torso**. They are open precisely because the textured patches are cut out of
them.

### 4.3 QuadriFlow does not preserve an open boundary — measured, not assumed

Run on the five open patches with `-boundary`:

| Patch | input boundary vertices | output boundary vertices | original boundary vertices reproduced exactly |
| --- | ---: | ---: | ---: |
| `c01/m0` | 35 | 34 | **0 / 35** |
| `c01/m2` | 23 | 14 | **0 / 23** |
| `c00/m7` | 37 | **0** | **0 / 37** |
| `c00/m2` | 15 | **0** | **0 / 15** |
| `c00/m8` | 9 | **0** | **0 / 9** |

Two failure modes, both fatal. Where the boundary survives at all it is
**resampled**, so not one original boundary vertex is preserved — and a
resampled seam between two differently coloured surfaces is exactly the crack
T2.21 locked 84 vertices to prevent. Where it does not survive, QuadriFlow
**closes the hole** (`c00/m7` went from a 37-vertex boundary to a watertight
shell), meaning the skin patch grows straight over the region the cap and the
textured face tiles occupy.

`c00/m2` also collapsed 43 triangles to **6 quads** at a requested 21 — the
face regions are small and the remesher has nothing to hold onto.

**Consequence: the head, cap, face and torso stay as original triangles.** In
this pipeline they go through the project's own `quad_pairing.py` and come out
as **270 primitives, 50 of them textured, for a fixed 320 VDP1 commands.**
That is the floor, and the 418-command target lies only 98 commands above it.

---

## 5. Pose transfer — the verdict, with numbers

T2.21's cheapness came from `meshoptimizer.h:504`: the simplifier collapses to
an existing endpoint, so decimated index sets stayed subsets of the original
424 ids and all 107 frames survived untouched. **A remesher invents vertices.
That property is gone**, and the brief was right to make this the deciding
question.

### 5.1 The mechanism

Every new vertex is anchored once, in the base pose, to
`(original triangle, barycentric weights)` by an exact point-triangle closest-
point test restricted to its own patch. Each pose frame is then re-evaluated as
the same barycentric combination of that triangle's *deformed* corners. Both
light-intensity banks transfer identically as a scalar barycentric blend. A
vertex that was **not** remeshed is its own anchor with weight 1, so its pose
data is bit-exact — only the 13 remeshed patches are rebaked.

### 5.2 Why the transfer is exact in kind

The pose banks encode a **piecewise-linear** deformation of the original
surface. A barycentric point of a deformed original triangle therefore lies
*exactly on* the deformed original surface. **Every transferred vertex has
zero vertex-to-surface error in every one of the 107 frames.** The transfer
introduces no error of its own; the entire residual is the remesh's surface
approximation — the same class of error meshoptimizer reports.

### 5.3 The 107-frame sweep T2.21 could not run

T2.21 §4.2 recorded honestly that its collapse decision was made on the neutral
pose only and that "nothing here proves that across all 107 frames". Barycentric
transfer is cheap enough to close that gap, so it was closed. Every frame's
deformed surface is reconstructed from the shipped bank and the remeshed quads
are measured against it:

| Level | frames swept | worst frame | worst deviation | worst relative | mean relative | max RMS |
| --- | ---: | --- | ---: | ---: | ---: | ---: |
| `quad_100` | **107** | `animation` f17 | 7.50 | **5.17%** | 4.64% | 1.83 |
| `quad_75` | **107** | `animation` f17 | 7.01 | **4.83%** | 4.33% | 2.27 |
| `quad_50` | **107** | `animation` f12 | 5.98 | **4.12%** | 3.72% | 2.33 |
| `quad_35` | **107** | `animation` f12 | 6.84 | **4.72%** | 4.24% | 2.59 |

The worst frame is never the neutral pose — it is turntable frame 17 or 12 —
so **a neutral-only check would have understated the error at every level.**
That is a finding about the *method*, and it applies to the triangle route too:
T2.21's spot check should become this sweep before either approach ships.

### 5.4 Cost of the transfer, stated plainly

- **Generator cost only.** Nothing at runtime changes; the banks are still
  dense per-vertex arrays, just narrower and rebaked.
- **All 107 frames plus both light banks must be regenerated** whenever the
  remesh changes. T2.21 needed none of this.
- **Verdict: pose transfer is not expensive and not lossy, and it is not the
  reason to reject the quad route.** §4 and §6 are.

---

## 6. Shrinkage — the failure that shows on screen

Measured per patch, both on QuadriFlow's raw output and after projecting each
vertex onto the original surface, as bounding-box diagonal retained:

| Level | raw mean | raw worst | projected mean | projected worst | max snap distance |
| --- | ---: | ---: | ---: | ---: | ---: |
| `quad_100` | 87.3% | 65.6% | **84.3%** | **62.2%** | 3.35 |
| `quad_75` | 87.6% | 73.3% | **84.1%** | **65.6%** | 3.41 |
| `quad_50` | 88.1% | 74.0% | **82.7%** | **70.9%** | 3.90 |
| `quad_35` | 86.6% | 66.1% | **80.6%** | **63.7%** | 5.83 |

**Not one remeshed patch retains its size at any density**, and the figure
barely moves between `quad_100` and `quad_35`. That constancy is the
diagnostic: this is not coarseness, it is intrinsic. A coarse quad layout
simply does not place vertices at a shape's extremities — an 8-vertex shell has
no reason to put a vertex on a fingertip — whereas edge collapse keeps whatever
extremal vertices it does not remove. **This is the deep reason decimation
holds a silhouette better than remeshing at these budgets.**

Projection onto the original surface makes it slightly *worse*, not better,
which confirms the diagnosis: the vertices are not misplaced off the surface so
much as absent from the extremities. Projection was kept anyway, because it
makes the base vertex table and the 107 transferred banks the same function of
the same anchors — a consistency property this would need in order to ship.

**Why it is visible:** Mario's 15 shells abut and interpenetrate; they do not
share vertices. A limb that contracts ~16% pulls out of its socket. That is the
shoulder and hip gap in `mario-quadremesh-neutral-solid.png`.

---

## 7. The level table, beside T2.21

VDP1 command count is `primitive_count + textured_primitive_count`, computed
exactly as T2.21 did, because T2.19a established that a textured primitive
emits two commands — a Gouraud polygon and a distorted sprite — and both are
visible.

### 7.1 Quad remesh

| Level | primitives | remeshed | paired | quads | textured | **VDP1 commands** | vertices | meshlets | KB | QF failures |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| **original** | 644 | 0 | 644 | 144 | 50 | **694** | 424 | 31 | 327 | 0 |
| **quad_100** | 472 | 202 | 270 | 240 | 50 | **522** | 386 | 22 | 295 | 0 |
| **quad_75** | 424 | 154 | 270 | 192 | 50 | **474** | 338 | 20 | 259 | 0 |
| **quad_50** | 414 | 92 | 322 | 144 | 50 | **464** | 309 | 21 | 237 | **3** |
| **quad_35** | 402 | 80 | 322 | 132 | 50 | **452** | 297 | 20 | 228 | **3** |

### 7.2 Beside T2.21's triangle result

| Approach | Level | **commands** | Δ cmd | Δ cmd % | vertices | KB | Δ bytes % |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| — | original | 694 | 0 | 0.0% | 424 | 327 | 0.0% |
| triangle | decimated_75 | **564** | −130 | −18.7% | 330 | 255 | −22.1% |
| **quad** | **quad_100** | **522** | −172 | **−24.8%** | 386 | 295 | −9.7% |
| **quad** | **quad_75** | **474** | −220 | **−31.7%** | 338 | 259 | −20.8% |
| **quad** | **quad_50** | **464** | −230 | −33.1% | 309 | 237 | −27.5% |
| **quad** | **quad_35** | **452** | −242 | −34.9% | 297 | 228 | −30.3% |
| triangle | decimated_50 | **418** | −276 | −39.8% | 239 | 184 | −43.6% |
| triangle | decimated_25 | 273 | −421 | −60.7% | 154 | 118 | −63.7% |

**The quad route's whole usable range sits between T2.21's 75% and 50%
levels, and cannot reach either end.** `quad_35` at 452 commands is the floor,
and pushing further is pointless: 320 of those 452 are the untouchable head,
torso and textured set.

### 7.3 The direct answer to the owner's question

> **Can quad remeshing deliver 50%'s command count (418) at 75%'s appearance?**

**No.**

- **On cost:** the floor is 452. 418 is not reachable. Even if QuadriFlow's
  three failures at `quad_50`/`quad_35` were repaired and every remeshable
  patch went to its 6-quad minimum, the arithmetic bound is ~398 commands with
  the remeshable 65% of the untextured mesh reduced to rubble.
- **On appearance:** at 474 commands `quad_75` has detached limbs (§6) and 4.8%
  worst-frame surface error (§5.3), against `decimated_75`'s 2.41%. It is not
  75%'s appearance.

**The 75-vs-50 decision is not settled by this route. It stands where T2.21
left it.**

---

## 8. Two structural results that outlive the recommendation

### 8.1 Quads cost more vertices per command — and the bytes are vertices

T2.21 §6 established that the four pose banks are 94.7% of the actor's table
bytes and scale with *vertex* count. That holds here — pose banks are 95.1–95.4%
of every quad level — and it is exactly why the quad route's byte column is
weak. Compare at matched command count:

| | commands | vertices | KB |
| --- | ---: | ---: | ---: |
| `quad_75` | 474 | **338** | **259** |
| `decimated_75` | 564 | 330 | 255 |
| `decimated_50` | 418 | **239** | **184** |

`quad_75` saves 90 commands against `decimated_75` for the same bytes. But
`decimated_50` costs 56 *fewer* commands than `quad_75` **and 75 KB less**.
The reason is topological: a closed quad mesh has about one vertex per face,
an unpaired triangle mesh about half a vertex per face. **A quad is the better
buy in commands and the worse buy in bytes**, and on this actor bytes are
94.7% pose bank.

### 8.2 Remeshed quads are far less planar than the pairer's

VDP1 draws a four-vertex command as a bilinear patch, so out-of-plane offset of
the fourth corner (as a fraction of the quad's diagonal) is the quantity that
decides whether a quad reads as a surface or as a warp:

| Quad source | count | median | p90 | max |
| --- | ---: | ---: | ---: | ---: |
| shipped pairer (`original`) | 144 | **0.024** | **0.092** | 0.533 |
| QuadriFlow (`quad_100`) | 202 | **0.103** | **0.337** | 0.785 |
| QuadriFlow (`quad_75`) | 154 | 0.094 | 0.332 | 0.808 |
| QuadriFlow (`quad_50`) | 92 | 0.061 | 0.162 | 0.590 |

**Remeshed quads are ~4× less planar at the median and ~3.6× at p90.** The
pairer selects for planarity; QuadriFlow optimises field alignment and accepts
whatever warp the surface curvature implies. T2.21 §3.1 argued that handing
VDP1 a non-planar quad yields "a visible crease, not a saved command" — that
argument applies with roughly four times the force to these quads, and the host
renderer in §2.2 **cannot show it** because it draws quads as two flat
triangles. This is an unquantified hardware risk sitting on top of an already
negative result.

---

## 9. Recommendation

**Keep meshoptimizer. Do not adopt quad remeshing for the Mario actor.**

Reasons, in order of weight:

1. **It cannot reach the target.** 452-command floor against 418 (§7.3).
2. **It loses on the common metric.** More commands at matched error (§7, §10.2).
3. **It breaks the limbs.** Universal, density-independent shrinkage detaches
   Mario's arms and legs from his torso (§6), visible at every yaw.
4. **The byte win — which is the larger win on this actor — is worse** (§8.1).
5. **It adds an unmeasured hardware risk** in quad warp (§8.2).
6. **It costs a 107-frame rebake** that T2.21 does not need (§5.4).

**The one case where the quad route is the better buy, stated fairly.** If the
owner's "75% near-identical / 50% visibly degraded" judgement is driven by the
*face and cap* — and T2.21 §2.1's own description says it is — then `quad_100`
is worth a look as an alternative to `decimated_75`: **522 commands against
564, with the head, face, cap and torso byte-identical to the original.** It
buys 42 more commands than T2.21's 75% level while making the region the owner
is most sensitive to strictly untouchable. It costs 40 KB against `decimated_75`
and it still has the limb-detachment problem, so it is a real option only if
that can be repaired. §11 says what repairing it would take.

**A cheaper idea that this task did not pursue and that dominates both:** the
same "protect the face, coarsen the limbs" effect is available from
meshoptimizer directly, by driving per-material target ratios instead of one
global ratio — decimate materials 0, 9 and 10 hard, leave 2, 7 and 8 alone. That
needs no new dependency, no rebake, no remesher, and keeps
collapse-to-existing-endpoint. It is a small change to `decimate_mario_actor.py`
and it is the recommendation this task would make if asked what to do next.

---

## 10. Tools evaluated

Verified live on **2026-08-17** with `gh api` and `gh search`, per the
reference-code-first rule. Star counts and dates are observations on that date,
not training-data recall.

### 10.1 Chosen

| Field | Value |
| --- | --- |
| Repo | `https://github.com/hjwdzh/QuadriFlow` |
| Pinned SHA | **`810b7a0967c35b0dc85b4464e3835e26a756c967`** (committed 2019-12-07) |
| License | **BSD-3-Clause** — `LICENSE.txt`, © 2018 Huang, Zhou, Niessner, Shewchuk, Guibas, plus a non-exclusive enhancement grant. GitHub's detector reports `NOASSERTION`; the file itself is the standard three-clause text. Blender's `extern/quadriflow/README.blender` labels it `SPDX:MIT`, which is **inaccurate** — worth knowing if anyone reuses Blender's vendoring. |
| Stars / activity | 859★, pushed **2019-12-07**, 9 open issues, not archived — **dead upstream for six years** |
| Entry point | `quadriflow -i in.obj -o out.obj -f <faces> -seed <n>` — pure CLI, no GUI dependency |
| Files inspected | `src/main.cpp:15-45` (option parsing), `src/dedge.cpp:1-40` (the MinGW patch site), `src/flow.hpp:11-18` (Boost.Graph + lemon use), `CMakeLists.txt`, `LICENSE.txt` |
| Build | CMake + MinGW g++ 13.1; Eigen 3.4.0 (`3147391d946bb4b6c68edd901f2add6ac1f31f8c`, MPL-2.0) and MSYS2 Boost headers; lemon/pcg32/pss vendored by upstream. `post-solver.cpp` is **not** in `quadriflow_SRC`, so Boost.program_options is never needed. |
| Reuse mode | **dependency** — cloned under gitignored `work/`, compiled to a host CLI, **nothing vendored into this tree**. One-line local build patch recorded in §3. |

Why it won: it is the only candidate that is both permissively licensed **and**
buildable headless without a GUI toolkit. It delivered 100% quads, watertight
and manifold, on every patch it accepted.

Why it is nonetheless a poor dependency, recorded now so this is not
rediscovered: **it is unmaintained since 2019 and it is not robust at these
sizes.** On Mario's small closed patches roughly a quarter of runs produce no
output at a given seed; the tool retries seeds 0–7 and takes the first success,
which is deterministic but still left **3 patches unremeshed at `quad_50` and
`quad_35`**. Below ~6 quads no seed succeeds at all.

### 10.2 Rejected — with reasons, so this search is not re-run

| Candidate | Verified 2026-08-17 | Why not |
| --- | --- | --- |
| `wjakob/instant-meshes` | BSD-3 (`NOASSERTION` per API), 6,183★, pushed 2022-01-03, HEAD `7b31608` | The obvious field-aligned candidate and it *has* a batch mode (`src/batch.cpp`, `-o/-f/-b/-D`). But `CMakeLists.txt` issues `FATAL_ERROR` without `ext/nanogui/ext/glfw` and unconditionally links `nanogui` — **there is no no-GUI build switch**, so a headless build means bypassing its build system, not configuring it. Effectively unmaintained (HEAD commit 2019-11-03, 97 open issues). Its `-D`/dominant mode would emit non-quads, defeating the premise. |
| `nicopietroni/quadwild` | **GPL-3.0**, 417★, pushed 2022-09-09 | Licensing is **not** the objection — the project is GPL-compatible. The objection is buildability: qmake `.pro` only, with hand-edited paths in `libs/libs.pri`, and **Gurobi (commercial) paths hardcoded**. |
| `cgg-bern/quadwild-bimdf` | **GPL-3.0**, 138★, pushed **2026-08-03** — the live fork | The strongest *unexplored* candidate. CMake, Gurobi now optional via a Bi-MDF solver, prebuilt binaries and a Dockerfile. Not pursued because §4.3's boundary finding and §6's shrinkage finding are properties of *coarse quadrangulation on 15 small abutting shells*, not of QuadriFlow specifically, so a better remesher would not change the verdict. **This is the candidate to revisit if the verdict is ever challenged.** |
| `huxingyi/autoremesher` | root LICENSE **MIT**, 3,226★, pushed 2026-08-16 | Actively maintained, but `autoremesher.pro:1` is `QT += core widgets opengl` — Qt5 GUI, qmake, **no CLI entry point**. Also a licence trap: it bundles Geogram and libigl, so the effective licence is the composite, not the root MIT file. |
| `pmp-library/pmp-library` | **MIT**, 1,500★, pushed 2026-08-03 | Healthy and permissive, but `src/pmp/algorithms/` has remeshing, decimation, subdivision, triangulation, parameterization — **no quadrangulation module at all**. |
| `cnr-isti-vclab/vcglib` | GPL-3.0, 1,292★, pushed 2026-08-17 | Triangle-focused by its own README; no quad remesher, no CLI. |
| `zeux/meshoptimizer` | MIT, 8,226★, HEAD **`97bbdce4…`** — *unchanged* from T2.21's pin | Re-verified. **Has no quad output.** Its new `src/remesher.cpp` `meshopt_remesh` is `MESHOPTIMIZER_EXPERIMENTAL` and is a *voxel* remesher producing triangles. Not a quad path. |
| Blender / QuadriFlow remesh modifier | GPL | **Eligible under the corrected rule** and would have been used if needed. Not needed: Blender vendors QuadriFlow at `extern/quadriflow` pinned to `27a6867` — *older* than upstream HEAD — so going direct to upstream is strictly better. Blender is not installed on this host. |
| `Graphic-Kiliani/Tri2Quad-…`, `digicreatures/quadriflow_remesher`, `angjminer/jremesh-tools`, `microdevweb/blender-quad-remesher`, `Pentacode-IAFA/Quad-Remeshing`, `joelhi/mesh-quadrangulation-gh` | 3–23★, several unlicensed | Search results from `gh search repos` on "quad remesh"/"quadrangulation". All are thin wrappers around QuadriFlow or Blender, Grasshopper/Unity-bound, or unlicensed (all-rights-reserved). None is a distinct algorithm. |

Searches run: `"quad remesh"`, `"quadrangulation"`, `"field aligned quad mesh"`
(**1 result total**), `"tri to quad mesh"` (**2 results total**). The
permissive-and-headless quad-remesh field is genuinely narrow — essentially
QuadriFlow and Instant Meshes.

---

## 11. What it would take to make the quad route work

Recorded so the negative result is actionable rather than merely negative.

1. **A boundary-preserving remesher** (§4.3). Without it, 39% of Mario is off
   limits and the 452-command floor stands. `quadwild-bimdf` is the candidate.
2. **An anti-shrinkage constraint** (§6). Either feature-vertex pinning at each
   shell's extremities, or a post-remesh inflation that restores per-patch
   volume. Nearest-surface projection does **not** fix it — measured.
3. **A shell-contact constraint.** Even with 1 and 2, nothing currently keeps a
   remeshed arm in contact with an unremeshed torso, because the shells do not
   share vertices.
4. **A planarity term** (§8.2), or a measurement on real hardware of what VDP1
   does with a 0.34-p90 warped quad. Neither exists.
5. **A texturing redesign** if the head is ever to be remeshed. The 50 textured
   triangles are `pairing_forbidden` by generator enforcement and both their
   commands are visible (T2.19a); a remesher cannot respect that.

Items 1–3 are each larger than the entire triangle-decimation path.

---

## 12. Correctness, determinism and gates

### 12.1 The baseline reproduces the shipped header

`quad_remesh_mario.py` at ratio 1.0 runs the same path the remeshed levels take
— reconstruct triangles from the generated primitive records, run
`compile_mesh_ir`, run `mario_meshlets`:

| Quantity | Shipped header | Rebuilt at 1.0 |
| --- | ---: | ---: |
| `SM64_MARIO_PRIMITIVE_COUNT` | 644 | **644** |
| `SM64_MARIO_QUAD_COUNT` | 144 | **144** |
| `SM64_MARIO_MESHLET_COUNT` | 31 | **31** |
| `SM64_MARIO_MESHLET_LOD_PRIMITIVE_LIST_COUNT` | 1,369 | **1,369** |
| `SM64_MARIO_MESHLET_LOD_POSITION_LIST_COUNT` | 1,659 | **1,659** |
| `SM64_MARIO_VERTEX_COUNT` | 424 | **424** |
| footprint total | — | **335,451 bytes** (identical to T2.21 §6) |

**This check caught a real defect during development,** the same one T2.21 §4.4
hit: an earlier emission schedule grouped untouched triangles by patch rather
than by original triangle index and produced **27** meshlets instead of 31,
while reproducing primitives and quads correctly. Meshlet identity is a
contiguous primitive-index span, so a plausible-looking rebuild was quietly
wrong. The fix is `assemble()`'s `sorted(level["untouched"])` plus splicing each
material's quads before that material's first paired primitive.

### 12.2 Determinism

- **GREEN-twice.** Two independent full runs produce a byte-identical report,
  SHA-256 `5f7bd4753225ce3bd7ca53fa27e2b500f3155d007954e3a427ca3557c48c0063`
  (levels 1.00/0.75/0.50/0.35, pose sweep skipped for run time). QuadriFlow's
  `-seed` is explicit and the seed-retry loop takes the first success in
  ascending order, so the output is a function of file content only.
- **T2.21 independently reconfirmed.** Re-running `decimate_mario_actor.py`
  reproduced `sprint2-t2_21-mario-decimation.json` **byte-identically**, SHA-256
  `81aa00672da7557232c7376c6466750592d58fff45e487bcf6a73f6a1402fa30`, matching
  the committed file and T2.21's own claim.

### 12.3 Gates

Run from PowerShell, per the standing environment note.

| Gate | Result |
| --- | --- |
| `verify-actor-meshlets` | **PASS** (exit 0) |
| `verify-actor-pose-bank` | **PASS** (exit 0) |
| `verify-actor-material` | **PASS** (exit 0) |
| `verify-vdp1-painter-chain` | **PASS** (exit 0) |
| `verify-tools` | **FAIL — pre-existing, proven not a regression** |

`verify-tools` fails with `failures=2, errors=17, skipped=1`. All 17 errors are
`BobParityRouteTests`, and the 2 failures are
`BobMeshIRTests.test_bob_scene_emitter_preserves_ir_counts_and_manifest_offsets`
and `Fast3dProfileLayoutTests.test_offsets_match_a_compiled_offsetof_probe` —
none related to this task. **Proven pre-existing by removing this task's three
new tool files and re-running: identical `failures=2, errors=17, skipped=1`
over the same 208 tests.** Not chased, per the brief.

**No `verify-*` target was added** for the new tools. `Makefile.saturn.mk`
remains owned by T2.18 and the brief forbids touching it. This is the same gap
T2.19a §3.1 and T2.21 §10 recorded, now for the third report running.

---

## 13. Honesty — what is wrong with this task

- **No emulator, no build, no frame was measured.** Every command figure is a
  static count from generated tables. **No per-frame command number appears in
  this report**, deliberately: T2.19c measured actors at 312.03 emissions per
  frame against a mesh carrying 694, because culling drops much of it, so mesh
  count is not frame cost and this task measured neither.
- **The renders are host geometry renders, not Saturn frames**, and this
  renderer **draws quads as two flat triangles, which VDP1 does not do** (§2.2).
  The warp risk in §8.2 is measured as a distribution and never seen.
- **The pose sweep covers all 107 frames but only quad centroids.** The denser
  five-samples-per-face metric was run on the neutral pose only, for run time.
  That is still strictly more pose coverage than T2.21 had, but it is not a
  dense sweep.
- **`quad_50` and `quad_35` have 3 unremeshed patches each** because QuadriFlow
  failed on them at all 8 seeds. Those levels are therefore *not* clean points
  on a density curve — their command counts are inflated by ~52 primitives that
  fell back to triangles. The `quad_100` and `quad_75` rows are the trustworthy
  ones, and the §7.3 conclusion rests on them.
- **Only one remesher was actually run.** `quadwild-bimdf` was verified live and
  rejected on reasoning (§10.2), not on measurement. The §4.3 and §6 findings
  are argued to be general; that argument is not proven.
- **The shrinkage metric is bounding-box diagonal**, which is a coarse proxy for
  volume loss. It is well correlated with what the renders show but it is not a
  volume measurement.
- **The comparison metric is one-sided Hausdorff** (§ `compare_mario_lod.py`
  docstring): samples on the output measured to the original. It will not detect
  an original feature that vanished entirely, only output that strays.
- **The `original` row of the comparison table shows 1.13 max deviation, not
  zero.** That is real and is a useful calibration: it is the shipped pairer's
  own quad non-planarity, since a quad's centroid does not lie on the two
  triangles it replaced.
- **`quad_pairing.py` was again accepted as correct rather than audited**, as in
  T2.21.
- **The QuadriFlow binary is not reproducible from this tree.** It is built from
  a clone under gitignored `work/` with a one-line local patch (§3). The SHA is
  pinned and the patch is recorded, but a clean checkout cannot rebuild it
  without re-cloning and re-patching.

---

## 14. What remains

- **If a level is ever approved, the §9 alternative is the cheap next step:**
  per-material decimation ratios in `decimate_mario_actor.py`, protecting the
  face and cap while coarsening limbs. No new dependency, no rebake.
- **Apply §5.3's 107-frame sweep to the triangle route.** T2.21's neutral-only
  check is the same gap, and this task shows the worst frame is never neutral.
  That is a real, cheap improvement to the recommended approach.
- **`verify-mario-decimation` and a `verify-mario-quad-remesh`** once
  `Makefile.saturn.mk` is free — both tools self-check by reproducing the
  shipped header (§12.1) and nothing runs them.
- **Revisit `quadwild-bimdf`** only if the §7.3 verdict is challenged (§10.2).
- **The six fully transparent tiles** (T2.19a §10, T2.21 §7) remain the only
  free command in this path. Still unattempted, now for the fourth report
  running.
- **Add QuadriFlow and meshoptimizer to `find-library`'s vetted register** —
  it still has no mesh or geometry rows.

---

## 15. References

| Repo | Pinned SHA | License | Files inspected | Reuse mode |
| --- | --- | --- | --- | --- |
| `hjwdzh/QuadriFlow` | `810b7a0967c35b0dc85b4464e3835e26a756c967` | BSD-3-Clause | `src/main.cpp:15-45`, `src/dedge.cpp:1-40`, `src/flow.hpp:11-18`, `CMakeLists.txt`, `LICENSE.txt` | **dependency** — host CLI under gitignored `work/`, nothing vendored; one-line MinGW build patch (§3) |
| `libeigen/eigen` (3.4.0) | `3147391d946bb4b6c68edd901f2add6ac1f31f8c` | MPL-2.0 | none — build dependency only | dependency, headers only |
| `zeux/meshoptimizer` | `97bbdce4716f6257c9527b051515136882f33e79` | MIT | `src/simplifier.cpp:577-602`, `:2366`, `:2945` (the error-normalisation contract, to establish that T2.21's number is per-subset and not directly comparable) | dependency — re-run unchanged to reproduce T2.21 |
| `wjakob/instant-meshes` | `7b3160864a2e1025af498c84cfed91cbfb613698` | BSD-3-Clause | none — rejected on build shape (§10.2) | rejected |
| `cgg-bern/quadwild-bimdf` | `e722c7e961982cf61db7c10812329dd0fc7d60df` | GPL-3.0 | none | rejected, revisit candidate (§10.2) |
| `nicopietroni/quadwild` | `9d1f27ad3c6e8b2800df27e2c14f70c5baf0250e` | GPL-3.0 | none | rejected (§10.2) |
| `huxingyi/autoremesher` | `ff8a00ca25402dd46d5d72091785b0922618db87` | MIT root, composite in practice | none | rejected (§10.2) |
| `pmp-library/pmp-library` | `af4725ccf6aa308e7ffad9a7bb927c6381b7c858` | MIT | none | rejected — no quadrangulation (§10.2) |
| `cnr-isti-vclab/vcglib` | `5cae2abc2f9056785b0537dcbe156c40da2aea20` | GPL-3.0 | none | rejected (§10.2) |

In-tree prior art relied on rather than reimplemented:
`tools/saturn/quad_pairing.py`, `tools/saturn/saturn_mesh_ir.py`
(`compile_mesh_ir`), `tools/saturn/extract_mario_actor.py` (`mario_meshlets`),
`tools/saturn/decimate_mario_actor.py` (`load_mesh`, `primitive_triangles`,
`footprint`), `tools/saturn/render_mario_ab.py` (rasteriser and PNG encoder) —
all called directly, which is why §12.1's equality check is meaningful and why
the two approaches' footprint numbers are computed by the same function.
