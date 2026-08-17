# Sprint 2 Task T2.21 — Mario mesh decimation, with pictures

- Date: 2026-08-17. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `442ad4da`.
- Task: decimate Mario with a real tool and produce A/B models the owner can
  look at.
- **Delivered: three decimation levels, six rendered contact sheets, and the
  numbers underneath.** Section 2 is the pictures; section 5 is the table.
- **No product build, no emulator, no GUI.** Two new host tools, one evidence
  JSON, six PNGs. Nothing under `src/port/saturn` was touched.
- Commits: `c9adb159` (tools), and this document. `SKIP_CHANGELOG=1`.
- Evidence: `sprint2-t2_21-mario-decimation.json`.

---

## 1. The three answers up front

**A1 — This is a generator change, but *not* for the reason the brief
expected.** Poses are dense per-vertex arrays keyed by vertex id, so the brief
was right to flag them. But meshoptimizer collapses an edge onto one of its
*existing* endpoints and never invents a position, so the surviving vertex set
is a **subset** of the original 424 ids and **the 107 baked pose frames stay
valid without being rebaked**. Section 4.2 proves this, and the walking-pose
renders in section 2 are the proof made visible: they are decimated index sets
driving the *untouched* shipped pose array. What forces this into the generator
is different and smaller — the meshlet, render-cluster and tile-start tables
are all derived from the primitive list and must be regenerated together
(section 4.3).

**A2 — The lossless quad-pairing lever the coordinator asked for has already
been taken.** Mario is already quad-paired by `quad_pairing.py` at extraction
time: 788 source triangles compile to 644 primitives, 144 of them real quads,
`commands_saved: 144`. The remaining 500 standalone triangles are not an
oversight; an independent census finds only **74** shared edges that are both
same-material and coplanar within 2°, ceiling **72 disjoint pairs** — fewer
than the 144 the pairer already made, because the pairer accepts near-planar
pairs a strict test rejects. Section 3. **There is no free command left in
Mario's encoding.** Every further command costs fidelity.

**A3 — Decimation is a large, real lever, and it buys cart bytes faster than
it buys commands.** At the 50% level Mario drops from 694 to 418 VDP1 commands
(−39.8%) and from 327.6 KB to 184.7 KB of generated tables (−43.6%). The
footprint win is dominated by the pose banks, which are 94.7% of the actor's
table bytes and scale with *vertex* count, not primitive count (section 6).

---

## 2. The pictures

All six sheets are in `docs/saturn/evidence/screenshots/`. Every row is a
decimation level; every column is a camera yaw; every cell in a column uses
the **same** camera and the **same** fitted bounds, so a silhouette difference
on the page is a silhouette difference in the mesh.

| File | What it shows |
| --- | --- |
| `mario-decimation-neutral-solid.png` | **Start here.** Neutral turntable pose, 4 yaws × 4 levels, shaded. |
| `mario-decimation-walk-f20-solid.png` | Walking animation frame 20, same grid. |
| `mario-decimation-walk-f48-solid.png` | Walking animation frame 48, same grid — a second, very different stride. |
| `mario-decimation-neutral-wireframe.png` | Neutral, wireframe over the shaded solid; this is where the density change is legible. |
| `mario-decimation-walk-f20-wireframe.png` | Walking frame 20, wireframe. |
| `mario-decimation-textured-fixed-cost.png` | The 50 textured primitives in magenta, everything else desaturated. They are identical in all four rows. |

### 2.1 What the renders actually say

Read as a curve rather than as four independent options:

- **75%** — the closest thing to free. The cap brim thins and the face loses a
  little definition; the silhouette is intact from every angle and in both
  poses. 130 commands and 72.5 KB cheaper than original.
- **50%** — the honest recommendation if a cut is wanted. The cap loses its
  crisp brim edge and the face flattens into fewer planes, but Mario reads
  correctly at every yaw, the limbs keep their proportions, and the walking
  poses still articulate cleanly. 276 commands and 143 KB cheaper.
- **25%** — visibly broken, and the sheets show exactly how. The cap collapses
  into a wedge, the head becomes a faceted blob, and the hands throw long white
  shards where a collapse has left a sliver triangle spanning what used to be
  two fingers' worth of geometry. This level is included because the brief
  asked for a point on the curve below 50%, not because it is shippable.

### 2.2 The renderer, and what it is not

`tools/saturn/render_mario_ab.py` is a dependency-free z-buffered rasteriser
with its own PNG encoder. `.venv-saturn-tools` has no PIL, numpy or
matplotlib, and mutating a venv three other agents are using was not an
acceptable side effect; the precedent for writing the renderer instead is
`memcamp-decimation-prototype-2026-08-09.md` §5, which did the same thing in
SVG for the same reason.

**These are not Saturn frames.** They are host renders of the *geometry*, flat-
shaded per primitive from the baked RGB555 material table with a fixed light.
They are the right instrument for judging silhouette and shape fidelity and
the wrong instrument for judging colour, Gouraud banding, texture, or anything
about VDP1. The textured primitives are drawn as flat material colour, not
textured, so the face detail the sprite supplies is absent from every row
including the original.

---

## 3. Quad pairing — already done, and the ceiling is measured

The mid-flight steer was that VDP1 draws four-vertex commands natively, that a
triangle fallback is a degenerate quad paying a quad's price, and that merging
coplanar same-material triangle pairs is therefore a free command. All three
statements are correct. The conclusion drawn from them — that Mario has not had
this applied the way terrain has — is not.

`extract_mario_actor.py:916` runs `compile_mesh_ir`, which runs
`quad_pairing.pair_triangles` with a `networkx.max_weight_matching` over every
admissible pair. Its report on the shipped mesh:

| Field | Value |
| --- | ---: |
| `source_triangle_count` | 788 |
| `candidate_count` | 164 |
| `quad_count` | **144** |
| `standalone_triangle_count` | 500 |
| `render_primitive_count` | **644** |
| `commands_saved` | **144** |
| `pairing_forbidden_triangle_count` | 50 |

788 → 644 is an **18.3% command reduction already banked**, losslessly, before
anything in this task.

### 3.1 Why the other 500 cannot pair

The pairer's own rejection tally on the shipped mesh, and an independent
census that does not use the pairer at all:

| Rejection reason | Count |
| --- | ---: |
| `normal_divergence` | 443 |
| `projection_not_convex` | 436 |
| `textured_pairing_not_implemented` | 105 |
| `material_mismatch` | 34 |

| Independent census over the 738 untextured triangles | Count |
| --- | ---: |
| distinct edges | 1,137 |
| edges shared by exactly two untextured triangles | 1,077 |
| ...of those, both triangles same material | 1,043 |
| ...of those, also coplanar within 2° | **74** |
| greedy disjoint pairs from that set (ceiling) | **72** |

The geometry is adjacent and same-material almost everywhere — 1,043 of 1,077
shared edges — and almost nowhere flat. Mario is a curved, faceted character
mesh; adjacent triangles genuinely disagree on normal. Merging them anyway
would hand VDP1 a non-planar quad, which it draws as a bilinear warp, and the
result is a visible crease, not a saved command.

**Note the direction of the comparison.** A strict 2° coplanarity test finds a
ceiling of 72 pairs; the shipped pairer made 144. The existing pass is already
*more* permissive than strict coplanarity, using a normal-divergence tolerance
plus a planar-convex projection test rather than exact flatness. So the honest
statement is not "72 pairs of headroom remain" — it is that the pairer has
already gone past the strict-coplanarity ceiling and the remaining 500
triangles are rejected on geometry that a looser test would only accept at a
fidelity cost.

### 3.2 Decimation destroys pairability, and the ordering in this tool is right

meshoptimizer is triangle-only. It has no notion of the quads downstream, and
it happily collapses the near-coplanar neighbours that made a pair possible.
Measured, at every level, with pairing run *after* decimation as the steer
required:

| Level | triangles | quads formed | primitives | quads as % of primitives |
| --- | ---: | ---: | ---: | ---: |
| original | 788 | **144** | 644 | 22.4% |
| 75% | 600 | 86 | 514 | 16.7% |
| 50% | 418 | 50 | 368 | 13.6% |
| 25% | 248 | 25 | 223 | 11.2% |

Quads fall faster than triangles at every step. Running pairing before
decimation would be strictly worse — the pairs would be collapsed away
afterwards and the pairing information discarded — so the pipeline order used
here (decimate, then pair with the project's own pairer) is the correct one,
and it is what produced the command counts in section 5.

---

## 4. The three constraints, resolved with evidence

### 4.1 Textured primitives are structurally special — enforced, not hoped for

`tools/saturn/extract_mario_actor.py:911` marks every textured source triangle
`pairing_forbidden`:

```python
"pairing_forbidden_triangles": [index for index, triangle in enumerate(triangles)
                                if triangle["texture"] is not None],
```

and `:945-950` refuses to emit a tile start unless the primitive owns exactly
one source triangle:

```python
if len(sources) != 1 or sources[0] not in textured_source_rank:
    raise ValueError("textured source triangles must remain individual VDP1 primitives")
```

T2.19a proved both of the resulting commands reach the screen: the Gouraud
polygon is forced opaque by `vdp1_cmdt_draw_mode_set`, the distorted sprite is
not, 56.38% of texture words are the transparent code, and 6 of 50 tiles are
*entirely* transparent so their polygon is 100% of what is drawn.

`decimate_mario_actor.py` therefore does three things rather than trusting a
weight: textured triangles are **excluded from every simplifier job**, every
vertex they reference is passed to meshoptimizer with
`meshopt_SimplifyVertex_Lock`, and `compile_level` re-asserts after pairing
that no textured triangle was absorbed into a quad. The count is checked
against `SM64_MARIO_TEXTURED_SOURCE_TRIANGLE_COUNT` at load. All 50 survive at
every level — see `mario-decimation-textured-fixed-cost.png`, where the
magenta set is identical in all four rows.

A second lock class was added on the same principle: **84 vertices** are locked
in total, being the textured set plus every vertex shared by two materials. A
material-boundary vertex that moved would open a crack between two differently
coloured surfaces, which is the character-mesh analogue of the material
smearing the steer warned about.

### 4.2 Poses are vertex-indexed — and survive anyway

The pose banks are dense three-dimensional arrays keyed by the same vertex
identifier space as the base mesh
(`src/port/saturn/gfx/saturn_mario_actor_mesh.h:440`, `:13223`):

```c
static const int16_t sm64_mario_animation_vertices[30][SM64_MARIO_VERTEX_COUNT][3];
static const int16_t sm64_mario_walking_animation_vertices[77][SM64_MARIO_VERTEX_COUNT][3];
```

plus `sm64_mario_animation_light_intensity[30][424]` (`:46028`) and
`sm64_mario_walking_animation_light_intensity[77][424]` (`:46631`). That is
**107 baked frames**, all column-indexed by vertex id. If the simplifier moved
a vertex to a new position, all 107 would be invalidated and this would be a
rebake.

It does not. `work/upstream/meshoptimizer/src/meshoptimizer.h:504`, at the
pinned commit, states the contract for `meshopt_simplifyWithAttributes`:

> The resulting index buffer references vertices from the original vertex
> buffer.

meshoptimizer's simplifier collapses an edge onto one of its existing
endpoints; it has no optimal-placement mode. Confirmed empirically on a
four-vertex smoke case (6 indices in, 3 out, surviving ids `{1,2,3}` all
original) and then on the real mesh: **every index in every decimated level is
an original Mario vertex id.**

The consequence is the single most useful fact in this report. **The decimated
meshes animate on the shipped pose data with no rebake at all.** The walking
sheets in section 2 are that claim executed: `render_mario_ab.py` feeds
`sm64_mario_walking_animation_vertices[20]` and `[48]` — untouched, 424 columns
wide — to the decimated index sets, and Mario walks. Had this constraint bitten,
those two sheets could not exist.

Two honest qualifications:

- **Compaction is a separate, mechanical step.** Leaving the pose arrays at 424
  columns keeps them valid but keeps them *large*; the footprint numbers in
  section 6 assume the columns are compacted to the referenced subset, which is
  a pure index remap with no geometric decision in it.
- **The collapse decision is made on the neutral pose only.** meshoptimizer sees
  one vertex buffer. A collapse that is cheap in the turntable pose is not
  guaranteed cheap at walking frame 48. The renders are the check that was
  actually run, and at 75% and 50% the walking poses hold up; nothing here
  proves that across all 107 frames. A generator implementation should either
  drive the simplifier with `validation_poses` (the mesh IR already has the
  field, and `pair_triangles` already accepts `deformation_poses`) or run a
  per-frame error sweep as a gate.

### 4.3 Meshlets — this is what actually forces a generator change

`extract_mario_actor.py:648-655` builds meshlets by walking the primitive list
**in order** and cutting a record whenever `(material, opacity)` changes or 32
primitives accumulate:

```python
for primitive_index in range(len(compiled_primitives)):
    key = primitive_key(primitive_index)
    if current and (key != current_key or len(current) == 32):
        flush()
```

Meshlet identity is therefore a *contiguous span of primitive index*. Removing
any primitive renumbers every later one, so every span moves and the whole
table — `sm64_mario_meshlet_lod_primitive_offsets`,
`_primitive_list`, `_position_offsets`, `_position_list`, `_bounds`,
`_material`, `_opacity`, `_source_ordinal` — has to be regenerated. It cannot
be patched. The same is true of
`sm64_mario_texture_tile_start`, which is indexed by primitive, and of the 23
render clusters.

The runtime is fine with this: `saturn_actor_meshlets.c:396-423` reads the
offsets straight out of the generated tables and bounds-checks them against the
declared counts, so a consistently regenerated bank needs no source change. One
runtime requirement does constrain the generator — `:601-607` derives per-
meshlet depth bounds from the **tier-0** span and fails if
`position_count == 0`, so no meshlet may decimate to empty. `mario_meshlets`
already raises `"Mario meshlet cannot be empty"`, and no level produced here
came close.

Two pathologies in the existing meshlet table are worth recording because
decimation interacts with both:

- **Tier 0 and tier 1 are identical.** `primitives = current if tier < 2` — the
  MID tier is inert, exactly as T2.19c found for terrain.
- **Tier 2 is `primitive % 8 == 1`,** an arbitrary stride over the *global*
  primitive index. Decimation renumbers primitives, so it reshuffles the tier-2
  set arbitrarily and without correlation to anything geometric. That is not a
  regression introduced here — it is the same arbitrary-stride defect T2.19c
  documented at `emit_bob_scene.py:196-204` — but a decimation generator should
  replace the stride rather than inherit it.

### 4.4 The correctness check

`decimate_mario_actor.py` rebuilds the mesh at ratio 1.0 through the *same*
path the decimated levels take — reconstruct triangles from the generated
primitive records, run `compile_mesh_ir`, run `mario_meshlets` — and the result
reproduces the shipped header exactly:

| Quantity | Shipped header | Rebuilt at 1.0 |
| --- | ---: | ---: |
| `SM64_MARIO_PRIMITIVE_COUNT` | 644 | **644** |
| `SM64_MARIO_QUAD_COUNT` | 144 | **144** |
| `SM64_MARIO_MESHLET_COUNT` | 31 | **31** |
| `SM64_MARIO_MESHLET_LOD_PRIMITIVE_LIST_COUNT` | 1,369 | **1,369** |
| `SM64_MARIO_MESHLET_LOD_POSITION_LIST_COUNT` | 1,659 | **1,659** |

**GREEN-twice determinism.** Two independent full runs produce a byte-identical
report, SHA-256 `81aa00672da7557232c7376c6466750592d58fff45e487bcf6a73f6a1402fa30`,
matching the committed `sprint2-t2_21-mario-decimation.json`. meshoptimizer's
collapse queue and this tool's material ordering are both functions of file
content only.

This is the reason the decimated numbers in section 5 can be quoted as counts
rather than estimates. It also caught a real defect during development: an
earlier version emitted textured primitives first and materials in sorted
order, which reproduced 644 primitives and 144 quads but only **27** meshlets.
The emission schedule now mirrors the original primitive order deliberately
(`build_jobs`), because meshlet count is order-sensitive and a plausible-looking
rebuild was quietly wrong.

---

## 5. The levels

Ratio is the requested fraction of untextured triangle indices; the achieved
triangle count differs because the simplifier stops on topology and because the
50 textured triangles are excluded from the request and added back whole.

| Level | triangles | primitives | quads | textured | **VDP1 commands** | vertices | meshlets | worst rel. error | degenerates dropped |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| **original** | 788 | 644 | 144 | 50 | **694** | 424 | 31 | — | 0 |
| **75%** | 600 | 514 | 86 | 50 | **564** | 330 | 22 | 0.0616 | 0 |
| **50%** | 418 | 368 | 50 | 50 | **418** | 239 | 18 | 0.1163 | 0 |
| **25%** | 248 | 223 | 25 | 50 | **273** | 154 | 13 | 0.1874 | 0 |

VDP1 command count is `primitive_count + textured_primitive_count`, because
T2.19a established that a textured primitive emits **two** commands — a Gouraud
polygon and a distorted sprite over it — and both are visible.

| Level | Δ commands | Δ commands % | Δ vertices % | Δ bytes | Δ bytes % |
| --- | ---: | ---: | ---: | ---: | ---: |
| 75% | −130 | **−18.7%** | −22.2% | −74,229 | −22.1% |
| 50% | −276 | **−39.8%** | −43.6% | −146,282 | −43.6% |
| 25% | −421 | **−60.7%** | −63.7% | −213,705 | −63.7% |

`worst_relative_error` is meshoptimizer's own reported error relative to mesh
extents, taken as the maximum across the eleven per-material jobs. No level
produced a zero-area triangle, so the post-quantization degenerate filter that
the terrain prototype needed never fired here.

### 5.1 What this is worth per frame — and why the denominator is provisional

T2.19c measured actors at **60.31%** of VDP1 emissions (312.03 of 517.35 per
frame) on `id-c0352f297034f653`. **Treat that as provisional.** It is one
capture, one route, no repeat run, and — the brief's own warning, which is
correct — it was taken at a route tick that may sit in a near-empty part of the
scene. **T2.20 is re-measuring it right now.** Every per-frame figure below
should be recomputed against T2.20's number before anyone sizes a decision on
it.

Two further reasons the static counts above do not convert directly:

- `312.03` is **all actors**, not Mario alone. Mario is one actor among the
  scene's population, so his share of that 60.31% is smaller and is not
  measured anywhere.
- The static counts are the **whole mesh**. Per frame, only the primitives that
  survive meshlet admission, tier selection and back-face rejection are
  emitted, so the realised saving is a fraction of the table delta and varies
  with camera distance.

With those caveats stated, the arithmetic bound: if Mario were the entire
actor population and entirely visible, the 50% level's −276 commands would be
−53.3% of frame emissions. That number is a ceiling constructed from an
inapplicable premise and is quoted only to bound the range from above. **No
per-frame measurement was taken by this task** — no emulator was run.

---

## 6. Footprint — the pose banks dominate

Generated-table bytes, computed from the declared array shapes. Assumes the
pose columns are compacted to the referenced vertex subset (section 4.2).

| Table | original | 75% | 50% | 25% |
| --- | ---: | ---: | ---: | ---: |
| `sm64_mario_walking_animation_vertices` (77 frames) | 195,888 | 152,460 | 110,418 | 71,148 |
| `sm64_mario_animation_vertices` (30 frames) | 76,320 | 59,400 | 43,020 | 27,720 |
| `sm64_mario_walking_animation_light_intensity` | 32,648 | 25,410 | 18,403 | 11,858 |
| `sm64_mario_animation_light_intensity` | 12,720 | 9,900 | 7,170 | 4,620 |
| `sm64_mario_primitives` | 6,440 | 5,140 | 3,680 | 2,230 |
| `sm64_mario_meshlet_lod_position_list` | 3,318 | 2,562 | 1,850 | 1,248 |
| `sm64_mario_meshlet_lod_primitive_list` | 2,738 | 2,186 | 1,564 | 948 |
| `sm64_mario_vertices` | 2,544 | 1,980 | 1,434 | 924 |
| `sm64_mario_texture_tile_start` | 1,288 | 1,028 | 736 | 446 |
| `sm64_mario_meshlet_metadata` + offsets | 903 | 642 | 526 | 381 |
| `sm64_mario_primitive_cull_back` | 644 | 514 | 368 | 223 |
| **total** | **335,451** | **261,222** | **189,169** | **121,746** |
| | **327.6 KB** | **255.1 KB** | **184.7 KB** | **118.9 KB** |

**The four pose banks are 94.7% of the total (317,576 of 335,451 bytes), and
they scale with vertex count, not primitive count.** That reframes the lever:
decimation's cart and RAM win is a *vertex* win, and it is much larger than the
command win. Anyone weighing this should notice that 50% decimation frees
143 KB — comparable to a third of the 446,432-byte VDP1 texture budget that
`compile_bob_bsp.py` treats as the hardware ceiling.

The one table that does **not** shrink is the texture data itself: 50 tiles ×
16 × 16 × 2 bytes = 25,600 bytes, fixed at every level, because the 50 textured
primitives are untouched.

---

## 7. What decimation cannot recover

- **The 50 textured primitives are fixed cost: 100 of the 694 baseline
  commands, 14.4%, and at the 25% level they are 36.6%.** They cannot be
  merged, split or reordered without breaking the polygon/sprite pairing, and
  T2.19a established that both halves of that pairing are visible. Changing it
  is a fidelity decision about how Mario's face and cap detail are drawn, not
  an optimisation, and it is out of scope here.
- **Six of those 50 sprite commands are genuinely redundant** — T2.19a §10
  found six tiles that are 256/256 transparent, so their sprite writes zero
  pixels at any size. That is a maximum of 6 commands per frame, is conditioned
  on ROM-derived data, and needs a guard at extraction time rather than a
  source edit. Recorded, not attempted; it is the only free command left in
  this path.
- **The lossless quad merge is spent** (section 3). 144 commands were already
  taken; the strict-coplanarity ceiling is below what was taken.
- **Decimation makes pairing worse, not better** (section 3.2). Some of the
  command reduction in section 5 is offset by quads reverting to triangle
  fallbacks; the table already accounts for this because pairing was re-run
  after each level.

---

## 8. Post-process or generator change?

**Generator change — but a narrower one than the brief anticipated.**

Not required, because meshoptimizer collapses to existing endpoints:

- rebaking the 30 turntable pose frames;
- rebaking the 77 walking pose frames;
- rebaking either light-intensity bank;
- any change to `saturn_actor_meshlets.c` or any other runtime source.

Required, because they are all derived from the primitive list:

- `sm64_mario_primitives`, `_primitive_cull_back`, `sm64_mario_texture_tile_start`;
- the full meshlet table (8 arrays) — spans are contiguous primitive-index runs;
- the 23 render clusters and their per-tier vertex lists;
- pose-column compaction, if the footprint win in section 6 is wanted — a pure
  index remap.

The natural shape is a `--decimate <ratio>` option on
`extract_mario_actor.py` that runs the simplifier between `compile_mesh_ir` and
`mario_meshlets`, so every derived table is generated from the decimated
primitive list in one pass. `decimate_mario_actor.py` already calls both of
those functions in that order and is effectively a prototype of that stage.

Scope, honestly: the extractor is 1,101 lines and the insertion point is one
place, but the change needs (a) the simplifier binary as a build-time
dependency, which is a new toolchain requirement for a repo that currently
needs only Python and the SH-2 toolchain, (b) a decision on the tier-2 stride
(§4.3), and (c) a multi-pose validation gate (§4.2). It is not a one-line
option.

---

## 9. Tool selection

Verified live on **2026-08-17** with `gh api`, per the reference-code-first
rule. Star counts and dates are observations on that date.

### 9.1 Chosen

| Field | Value |
| --- | --- |
| Repo | `https://github.com/zeux/meshoptimizer` |
| Pinned SHA | **`97bbdce4716f6257c9527b051515136882f33e79`** (committed 2026-08-08) |
| License | **MIT** — `LICENSE.md`, Copyright (c) 2016-2026 Arseny Kapoulkine |
| Stars / activity | 8,226★, pushed 2026-08-08, 7 open issues, not archived |
| Entry point | `meshopt_simplifyWithAttributes` |
| Files inspected | `src/meshoptimizer.h:465-494` (`meshopt_SimplifyX` options, `meshopt_SimplifyVertex_*` lock flags), `:497-537` (the simplify contract; the load-bearing sentence is `:504`), `:539-576` (`simplifyWithUpdate` at `:542`, `simplifySloppy` at `:563` — the alternatives, both rejected below), `LICENSE.md` |
| Reuse mode | **dependency** — cloned under gitignored `work/`, compiled to a host CLI, **nothing vendored into this tree** |

Why it wins on the requirement that actually mattered — attribute and
constraint awareness, not abstract simplification quality:

1. **`vertex_lock` with `meshopt_SimplifyVertex_Lock`.** The 50 textured
   primitives' vertices and the material-boundary vertices are locked by name,
   not by a weight that might be overcome. 84 locks in total.
2. **Collapse-to-existing-endpoint.** This is the property that saved the pose
   banks (§4.2). `meshopt_simplifyWithUpdate` was rejected precisely because it
   *does* move positions — its header says it "destructively updates positions
   and attribute values", which would invalidate all 107 frames.
3. **`meshopt_simplifyWithAttributes` carries a real attribute metric** with up
   to 32 weighted attributes, so a future generator stage can feed the Gouraud
   vertex colours in directly rather than approximating with per-material jobs.
   This task did not need it — per-material jobs make cross-material merging
   *structurally impossible* rather than merely expensive — but it is the path
   if the material split proves too restrictive.
4. **`meshopt_buildMeshlets` exists**, which matters because
   `saturn_actor_meshlets.c` already consumes a meshlet layout. Not used here:
   the port's meshlets are `(material, opacity)` runs capped at 32 primitives
   with hand-rolled tiers, not spatial clusters with vertex/triangle caps, so
   the two are not the same object. Recorded as an option for a later task
   rather than forced into this one.

`meshopt_simplifySloppy` (`:563-576`) was also rejected. It takes a
`vertex_lock` array, but its own header says "The algorithm doesn't preserve
mesh topology" — which is exactly the guarantee the material-boundary locks
exist to provide.

### 9.2 Rejected — with reasons, so this search is not re-run

| Candidate | Verified 2026-08-17 | Why not |
| --- | --- | --- |
| `songrun/SeamAwareDecimater` | MIT, 529★, last push **2020-03-19**, HEAD `c69934356ecdb0dd91070a6fc0520cdb0cc4d983` | **The project's own prior art** (`memcamp-decimation-prototype-2026-08-09.md`), and it works — but it needs libigl + Eigen built from source, its `--strict 2` boundary protection made 50% and 25% converge to an identical mesh on the terrain data, and it has been dormant six years. No vertex-lock API; protection is all-or-nothing per boundary. |
| `libigl/libigl` | 5,068★, GitHub reports SPDX **GPL-3.0**, last push 2026-08-04 | Its own description says MPL-2.0 but GitHub's detector resolves the repo to GPL-3.0, so its license status is not clean enough to pull source into this tree. Also a large geometry-processing framework where one function is wanted. |
| `sp4cerat/Fast-Quadric-Mesh-Simplification` | MIT, 1,758★, last push **2024-07-28**, HEAD `65df07dc54766e3ee480482f1c881a62767831cc` | The classic single-header QEM. Two years stale, and — decisively — **no vertex locking and no attribute metric**. It would merge across material boundaries and across the textured set. This is the "first classic QEM implementation" the steer warned against settling for. |
| `pyvista/fast-simplification` | MIT, 207★, active (2026-08-12) | Pure-Python install would have been convenient, but adding a package to `.venv-saturn-tools` while three agents share it was not acceptable, and its API exposes no per-vertex lock. |
| `isl-org/Open3D` | **NOASSERTION**, 13,889★ | Unresolved SPDX plus a very large install for one function. `simplify_quadric_decimation` also uses optimal placement, which would invalidate the pose banks. |
| `cnr-isti-vclab/meshlab` / `pymeshlab` | **GPL-3.0**, 5,792★ | Copyleft. No source may enter this tree. Behaviour-only or clean-room would be the only legal modes and neither was needed. **Recorded as rejected-for-licensing, not skipped.** |
| Blender decimate modifier | **GPL** | Same. Behaviour-only if ever wanted. |
| `Whinarn/UnityMeshSimplifier`, `RamType0/Meshia.MeshSimplification`, `simplestargame/RuntimeMeshSimplification` | MIT, active | C#/Unity-bound. No path to a host CLI here. |
| `Xrvitd/CWF` | **AGPL-3.0** | Copyleft, and a research-grade SIGGRAPH 2024 implementation aimed at high-quality surface reconstruction, not a constrained low-poly budget. |
| `Zielon/ParallelQSlim` | BSD-2-Clause, 157★, last push 2026-07-12 | Permissive and viable, but its selling point is parallel throughput on large meshes. Mario is 788 triangles; the whole run takes under a second single-threaded. No lock API. |
| `fogleman/simplify` (Go), `jannessm/quadric-mesh-simplification` | MIT | Language/runtime mismatch, no lock API, far smaller adoption. |

`find-library`'s `references/vetted-register.md` has **no mesh or geometry
rows** — checked, and this is the gap that made a live search necessary. The
meshoptimizer row above is the candidate to add.

---

## 10. Honesty — what is wrong with this task

- **No emulator, no build, no frame was measured.** Every command figure is a
  static count from the generated tables. The per-frame conversion in §5.1 is
  bounded, not measured, and its denominator is a provisional T2.20-superseded
  number.
- **The renders are host geometry renders, not Saturn frames** (§2.2). No
  texture, no Gouraud interpolation, no VDP1 semantics. They answer "does the
  silhouette hold" and nothing else.
- **The collapse decision is made on one pose** (§4.2). The three pose sheets
  are a spot check across three frames of 107, not a sweep. A generator
  implementation needs a per-frame error gate; this task did not build one.
- **The footprint table assumes pose-column compaction** (§6), which is a
  mechanical remap this task did not implement. Without it the pose banks stay
  at 424 columns and the byte savings shrink to almost nothing — the primitive
  tables alone are 2.5% of the total.
- **No host gate was added.** `decimate_mario_actor.py` self-checks by
  reproducing the shipped header (§4.4), but nothing runs it. It has no
  `verify-*` target because `Makefile.saturn.mk` is owned by T2.18 for the
  duration and the brief forbids touching it. This is the same gap T2.19a §3.1
  recorded, and it has the same fix.
- **The simplifier binary is not reproducible from this tree.** It is built
  from a clone under gitignored `work/` with MSYS2 g++. The SHA is pinned and
  the driver source is small, but a clean checkout cannot rebuild it without
  re-cloning.
- **`quad_pairing.py` was accepted as correct rather than audited.** The claim
  that 144 quads is the pairer's true maximum rests on its own report plus an
  independent coplanarity census, not on re-deriving the matching.
- **The 25% level is included because the brief asked for three levels below
  original, not because it is defensible.** Section 2.1 says what it looks
  like. Nobody should read its row in section 5 as an option.

---

## 11. What remains

- **Re-run §5.1 against T2.20's actor share** once it lands, and separate
  Mario's share from the whole actor population.
- **If a level is approved, build the generator stage** (§8): a `--decimate`
  option on `extract_mario_actor.py`, plus pose-column compaction, plus a
  replacement for the `primitive % 8 == 1` tier-2 stride, plus a multi-pose
  error gate.
- **Add `verify-mario-decimation`** once `Makefile.saturn.mk` is free: run
  `decimate_mario_actor.py` at ratio 1.0 and assert it reproduces the five
  shipped counts in §4.4. That single assertion is a real gate on the pairing
  and meshlet generators, independent of whether decimation ever ships.
- **The six fully transparent tiles** (§7) remain the only free command in this
  path. Still unattempted, now for the third report running.
- **Add meshoptimizer to `find-library`'s vetted register** (§9.2).

---

## 12. References

| Repo | Pinned SHA | License | Files inspected | Reuse mode |
| --- | --- | --- | --- | --- |
| `zeux/meshoptimizer` | `97bbdce4716f6257c9527b051515136882f33e79` | MIT | `src/meshoptimizer.h:465-576` (esp. `:504`, `:490`, `:542`, `:563`), `LICENSE.md` | **dependency** — host CLI under gitignored `work/`, nothing vendored |
| `songrun/SeamAwareDecimater` | `c69934356ecdb0dd91070a6fc0520cdb0cc4d983` | MIT | none this task; read through `memcamp-decimation-prototype-2026-08-09.md` | rejected (§9.2) |
| `work/upstream/slavedriver-engine` | not re-cloned | GPL-3.0-or-later | none | — |
| `work/upstream/sonic-z-treme` | not re-cloned | GPL-3.0 | none | — |

In-tree prior art relied on: `tools/saturn/quad_pairing.py`,
`tools/saturn/saturn_mesh_ir.py`, `tools/saturn/extract_mario_actor.py`
(`mario_meshlets`) — all called directly rather than reimplemented, which is
why §4.4's equality check is meaningful.
