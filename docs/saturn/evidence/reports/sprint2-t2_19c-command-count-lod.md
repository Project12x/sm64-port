# Sprint 2 Task T2.19c — command-count LOD: the premise did not survive measurement

- Date: 2026-08-16. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `eef321fe`.
- Task: make LOD reduce a surviving surface's **command count**, not just its
  material quality, following SlaveDriver's MIPDIST tile-grid halving.
- **Verdict: the change was not built, and should not be built as scoped.**
  Two measured facts kill it, and a third bounds what a rescoped version could
  ever be worth. All three are below with the numbers.
- Commit: `4935c3fa` (measurement tool, LOD mutation gates, anti-popping
  oracle, `verify-hot-promotion` include-path fix). `SKIP_CHANGELOG=1`.
- Evidence: `sprint2-t2_19c-terrain-lod-census.json`.
- **No product build was run and no emulator GUI was opened.** One headless
  Ymir capture was run against the *existing* `id-c0352f297034f653` release
  artifact; section 7 states why and what it does not license.

---

## 1. The three findings, in the order that matters

**F1 — Terrain is 40.5% of the frame's VDP1 commands, not 82%. Actors are
60.3%.** Measured on the shipped product ELF, 120 samples, 119 intervals.
The brief's premise — "terrain is roughly 82% of ~552 commands/frame, so this
is the largest command-reduction lever available" — is false on the current
build. Terrain is the smaller half. Section 3.

**F2 — SlaveDriver's MIPDIST pattern has no structural analogue here, and this
is measurable rather than a judgement call.** MIPDIST halves a wall's tile
grid so the *same surface* costs 4x fewer commands while staying fully
covered. This port's terrain is a flat soup of 867 independent baked
primitives at exactly one VDP1 command each; there is no grid to halve. The
coverage-preserving alternative — merging adjacent coplanar same-material
primitives — is impossible on this data: of 622 edges shared by exactly two
primitives, **exactly 1** joins two primitives with the same texture, and
**0** are both same-texture and coplanar. Section 4.

**F3 — The only command-count LOD mechanism this data supports is a
hole-punching drop, and it is capped at 94 of 867 primitives (10.8%) by a
baked mask whose selection rule is `source0 % 8 == 0`.** That is an arbitrary
stride, not a hole-safety analysis. At best it removes ~10.8% of 209 terrain
commands ≈ 23 commands ≈ **4.3% of the frame's 517 emissions**, in exchange
for arbitrarily-chosen holes in distant terrain. Section 5.

**What did land:** the tier-selection function is now pinned by an oracle and
four mutation gates, including an explicit anti-popping dither oracle. That
work is independent of whether a command-count rule is ever attached, and it
is the piece the next attempt would otherwise have to build first. Section 6.

---

## 2. Reference reading

Per the reference-code-first rule and the standing owner instruction. The
project's position on GPL reuse is `docs/saturn/HANDOFF_2026-07-20.md:31-34`
and `HANDOFF_2026-07-26.md:141-142`: GPL-compatible direct reuse or close-port
**is** permitted when isolated in `src/port/saturn/gpl/` with full attribution.
Nothing was copied here, because nothing was transplantable.

| Repo | Pinned SHA | License | Files inspected | Reuse mode |
| --- | --- | --- | --- | --- |
| `work/upstream/slavedriver-engine` | `a8986591557b6e680550d3c23970284d3b38ff8f` | GPL-3.0-or-later | `WALLS.C:31` (`#define MIPDIST F(256)`), `:38-41` (`MIPMAP`, `mipBase`), `:994-1011` (master `drawRectWall` mip branch), `:1274-1292` (`slave_drawRectWall` mip branch), `:1036-1075` (the tile emission loop the halving feeds), `SRUINS.C:2063` (`mipBase=createMippedPics()`), `PIC.C:24` | **pattern-only** — nothing copied, adapted or ported |
| `work/upstream/sonic-z-treme` | `cff75451c1616aac1236fc2b44223902b55c706b` | GPL-3.0 | already close-ported in-tree as `src/port/saturn/gpl/ztreme_hot_promotion.{c,h}`; re-read for the hysteresis contract this task pins | existing close-port, extended with test-only hooks |

### 2.1 What MIPDIST actually does, and the three things it needs

SlaveDriver's wall is a **parametric tile grid**: `theWall->tileLength ×
theWall->tileHeight` tiles over one planar rectangle. Past the distance test
(`WALLS.C:1001-1010`):

```c
if (currentState.desiredWeapon &&
    coords[0].z>MIPDIST && coords[1].z>MIPDIST &&
    coords[2].z>MIPDIST && coords[3].z>MIPDIST)
   {width>>=1; height>>=1; tileBias=mipBase;}
```

`vWidth`/`vHeight` are then derived as `(corner span)/width`, so halving
`width` and `height` **doubles each tile's footprint** — the wall stays fully
covered by a quarter as many quads. Three preconditions make that safe, and
this port satisfies none of them:

1. **A parametric grid over one surface.** Coverage is recomputed from the
   corners, so any divisor still covers the wall exactly.
2. **A pre-built half-resolution texture set.** `createMippedPics()` produces
   them and `tileBias = mipBase` selects them, so a 2x2 tile group's
   appearance is preserved rather than approximated.
3. **All four corners past the threshold** — a conservative all-corners test,
   so a wall straddling the boundary keeps its full grid.

Two details worth recording because they contradict how MIPDIST is usually
described. It is gated on `currentState.desiredWeapon`, so it is not
unconditional. And in `slave_drawRectWall` the halved variables are declared
`const int width`/`const int height` (`WALLS.C:1267-1268`) while the mip branch
writes `width>>=1`, which cannot compile as C — so the slave-side copy of this
optimisation is, at this pin, dead or unbuilt. The master-side copy at
`:994-1011` is live.

There is also a **fidelity shortcut** in the halved path that the reference
simply accepts: the per-tile texture walk (`tex++` twice per tile from
`theWall->textures`) consumes the *first* `(w/2)·(h/2)` texture entries rather
than a spatially-correct subsample, so a mipped wall's texel assignment is
approximate. This port has no equivalent slack — its texture binding is
per-primitive and exact.

---

## 3. The terrain command share, re-measured

The brief warned that the 82% figure was T2.8-derived and that two
T2.8-derived numbers have already meant something other than how they were
quoted. That warning was correct, and this is a third instance.

### 3.1 Where 82% came from

`sprint2-t2_8-vdp1-fence-attribution.md:406`:

> | Commands per present | **552.2** (actor **97.5 = 17.7%**, textured 56.0) |

82% is `1 - 17.7%`. It is not a terrain measurement — it is **everything that
is not actor**, which lumps in Mario, HUD, sky and every control command. It
was also taken on a `SATURN_DIAGNOSTIC_MODE=2` build in a different sprint,
before Sprint 2's actor work.

### 3.2 The direct measurement

`tools/saturn/capture_terrain_lod_census.py` (new, this task) decodes profile
counters that **already exist in the shipped product ELF and are already
incremented on the product path** — they simply sit past byte 128, which is
where `capture_route_counters.py`'s `PROFILE_BYTES` stops
(`capture_route_counters.py:71`). So this is a pure *reader* change: no target
source touched, no rebuild, and the numbers describe an already-released
artifact.

Offsets were derived by parsing the struct declaration in
`saturn_fast3d_frontend.h` under C alignment rules. The six offsets T2.14
already recorded (0, 4, 44, 48, 80, 84) reproduce exactly, which is the
cross-check that the parse is right.

Run: `id-c0352f297034f653` (T2.17's product build), on-target identity
**MATCH**, 4096 startup VBlanks, 1800 warm-up, 120 samples, gap 11, 58.1 s.

| Quantity | Per frame | Share of VDP1 emissions |
| --- | ---: | ---: |
| `demo_bob_results_master + _slave` (**terrain**) | **209.34** | **40.46%** |
| `demo_actor_primitives_emitted` (**actors**) | **312.03** | **60.31%** |
| `triangles_vdp1_emitted` (total) | 517.35 | — |
| `vdp1_commands_last` (VDP1 list length, raw snapshot) | 543 | — |

**Terrain is 40.5%. Actors are 60.3%.** The brief has the split roughly
inverted.

Three consistency checks, because a single new instrument deserves them:

- `vdp1_commands_last` reads **543** commands in the live list against T2.8's
  **552.2** — two different instruments on two different builds, 1.7% apart.
  The census tool is measuring the right thing.
- Terrain + actor = 521.4 against 517.4 emissions, an overshoot of **4.0**.
  That has a mechanism: `demo_bob_results_*` counts records *reserved* by the
  classify lanes, while `triangles_vdp1_emitted` counts commands actually
  written, and the far-tail arena budget skip at
  `saturn_demo_render.c:4735-4742` drops the difference. A 4/frame overshoot
  is the expected sign and the expected size.
- `demo_bob_results_master` is **0.0** and `_slave` carries all 209.34: all
  terrain classification runs on the slave, which matches
  `SATURN_SLAVE_RENDER=1` in the release config.

`flat_primitives` 60.72 and `gouraud_primitives` 2.13 per frame decompose part
of the terrain material mix; the remainder is textured.

### 3.3 The LOD telemetry does not exist

`demo_lod_tier_near`, `_mid`, `_far`, `demo_lod_transitions`,
`demo_lod_primitives_suppressed`, `demo_lod_texture_downgrades` and
`demo_bob_primitives_visible` are all declared in
`saturn_fast3d_frontend.h`, are listed in `tools/saturn/test_tools.py:2241-2271`
— and **have no writer anywhere in the source tree.** A tree-wide grep finds
exactly one publisher among the demo counters,
`saturn_demo_render.c:4704` for `demo_bob_results_slave`.

They read 0.00 in the capture, and that zero says nothing about whether LOD
fires. **The per-frame tier population is currently unmeasurable without a
target change**, which this task was not permitted to build. That is why every
tier-population figure below is a bound derived from the baked data rather
than an observation, and is labelled as such.

---

## 4. Why the grid-halving pattern has no analogue (measured)

### 4.1 There is no grid

`build/saturn/sourceboot/generated/bob_scene.h`: 867 primitives, 1625
positions, each primitive `{uint16_t indices[4]; source0; source1; rgb[3];
textured; tile_size; tile_offset; clut_offset}`. 633 are triangles (stored as
a degenerate quad, `source1 == 0xFFFF`), 234 are quads. 854 of 867 are
textured.

Fan-out is decided in exactly one place, `saturn_demo_render.c:2856`:

```c
const uint8_t result_count = clipped_count == 5U ? 2U : 1U;
```

**One primitive is one VDP1 command**, except a primitive the near-plane
clipper turns into a pentagon, which becomes two. There is no subdivision or
tessellation anywhere in `src/port/saturn` — a tree-wide grep for
`subdiv|tessell|split_quad` returns nothing. `SM64_SATURN_BOB_CLUSTER_COUNT ==
SM64_SATURN_BOB_PRIMITIVE_COUNT == 867`, and
`saturn_demo_render.c:1009` hard-faults if a cluster's `primitive_count != 1`.

So "halve the grid" has nothing to halve. The only decimation available is
**merge** or **skip**.

### 4.2 Merge is impossible on this data

Merging adjacent coplanar same-material primitives is the only decimation that
preserves coverage — i.e. the only one that cannot punch holes. Measured over
the generated bank:

| Quantity | Count |
| --- | ---: |
| Distinct edges | 2213 |
| Edges shared by exactly two primitives | 622 |
| ...of those, both primitives carry the **same texture** | **1** |
| ...of those, also coplanar within 2.6° | **0** |
| Shared-edge pairs coplanar within 8° regardless of texture | 320 |
| Neighbour pairs with identical texture **and** identical baked RGB | 1 |

The geometry is there — 320 coplanar neighbour pairs — but **the materials
never agree**. BOB's terrain is textured per-face from the SM64 display lists
(`tools/saturn/extract_bob_area.py`), and `tools/saturn/quad_pairing.py` has
already consumed the compatible pairs offline; its own docstring says the
source mesh stays triangulated and it only pairs *compatible* faces. What is
left is, by construction, the incompatible remainder.

Merging two primitives with different textures means picking one texture and
stretching it across both. That is not a distance-graded quality reduction; it
is a visible material error at a fixed screen location. **Merge is off the
table, and it is off the table for a data reason that will not change with
tuning.**

### 4.3 So the only lever is skip, and skip means holes

Dropping a primitive exposes whatever the painter drew beneath it. In a
far-to-near painter over a VDP2 background that is usually another terrain
quad, and sometimes the sky. There is no primitive-granularity drop that can
be proven hole-free.

---

## 5. What the existing mechanism already does, and its ceiling

The port already has a command-count LOD. It is small and it is arbitrary.

`saturn_demo_render.c:1955-1963` calls
`saturn_lod_can_suppress(tier, SATURN_DEMO_POLY_TIER, lod_far_mask[i]==0,
primitive->source0, 128)`, and the policy
(`ztreme_hot_promotion.c:196-201`) is:

```c
return build_role == 2U && tier == SATURN_LOD_FAR &&
       bake_approved_optional && source_id >= mandatory_route_prefix;
```

The shipped product profile is `polygon_tier: 2`
(`tools/saturn/profiles/sourceboot-bob-demo-v1.json`), so this arm **is** live
in the release build. Its ceiling is set by the bake, and the bake rule is
`tools/saturn/emit_bob_scene.py:196-204`:

```python
lod_mid = [1] * len(primitives)
lod_far = [0 if int(primitive["source0"]) >= 128 and
           int(primitive["source0"]) % 8 == 0 else 1 ...]
```

Counted directly from the generated header:

| Mask | Primitives kept | Primitives droppable |
| --- | ---: | ---: |
| `lod_mid_mask` | 867 of 867 | **0 — the MID tier is inert** |
| `lod_far_mask` | 773 of 867 | **94** (93 of them past the route prefix) |

The baked per-tier position streams agree: `lod_position_ref_offsets` is
`[0, 1625, 3250, 4758]`, i.e. tier 0 and tier 1 both reference all 1625
positions and tier 2 references 1508. **There is no decimated LOD mesh in this
bake.** The tiers exist; the geometry behind them does not.

### 5.1 The ceiling, arithmetically

Best case, if every one of the 93 eligible primitives were simultaneously at
FAR and visible:

- 93 commands removed from a terrain population of 209.34/frame → **−44% of
  terrain in the impossible limit**.
- But the eligible set is 10.8% of the scene chosen by `source0 % 8`, and a
  primitive must *also* satisfy `depth >= 7168` **and** `projected_span <= 40`
  to be at FAR at all. On a route where terrain averages 209 visible
  primitives out of 867, the realistic simultaneous-FAR-and-eligible
  population is a small fraction of 93.
- Taking the whole 93 anyway as a generous bound: 93 of 517.35 emissions is
  **17.98% of frame commands**; taking a plausible tenth of it, ~1.8%.
  The honest range for a rescoped "grow the far mask" change is **roughly
  2–5% of frame commands**, for arbitrarily-selected holes in distant terrain.

That is the number the owner should weigh. It is not nothing, but it is not
"the largest command-reduction lever available" either — and it is being spent
on the 40% half of the frame.

### 5.2 Tier boundaries, in owner-visible terms

For completeness, since the owner approved the tradeoff and is entitled to
know where it bites. `saturn_lod_default_thresholds()`,
`ztreme_hot_promotion.c:58-88`:

| Transition | Enters when | Leaves when |
| --- | --- | --- |
| NEAR → MID | `depth >= 4096` **and** `span <= 96 px` | `depth < 3584` or `span > 112 px` |
| → FAR | `depth >= 7168` **and** `span <= 40 px` | `depth < 6144` or `span > 48 px` |

`depth` is the **farthest** of the four corners' view-space z
(`saturn_demo_render.c:1942-1945`) — note this is the *opposite* of
SlaveDriver's all-corners-past test, so a primitive straddling a boundary
demotes here where SlaveDriver would keep it whole. The `span` gate is what
keeps that from mattering much: 40 px is a genuinely small object.

Worst-case geometric error at a FAR boundary today is **zero** for a surviving
primitive — the existing FAR arm changes material, not vertices, so no
vertex moves at any tier. The error introduced by a *dropped* primitive is not
geometric at all; it is a hole of up to 48×48 screen pixels showing whatever
lies behind it. That is the whole fidelity cost of the mechanism as it exists,
stated plainly.

---

## 6. What landed: the oracle, the mutations, and the anti-popping gate

Commit `4935c3fa`. All host-only.

### 6.1 The anti-popping oracle

The coordinator's instruction was that popping is a defect to engineer
against, not a cost to disclose, and that hysteresis is the standard
mitigation. **The hysteresis already exists** — the enter/exit bands in
section 5.2 are exactly it, on both the depth and span axes, and
`saturn_lod_select` is a hysteretic state machine over `previous`. What did
not exist was anything pinning it.

`tools/saturn/hot_promotion_test.c` gains a dither oracle: it drives depth
across the MID enter threshold (4096↔4095), then across the FAR enter
threshold (7168↔7167), then span across the FAR span threshold (40↔41), each
for 64 alternating frames, and requires **zero tier changes** after the first
entry. It then requires that a genuine departure past the exit window still
demotes, so the gate cannot be satisfied by a band so wide the tier never
moves.

This is the direct answer to "a camera dithering across the boundary must not
oscillate": with the shipped bands it cannot, and the test now proves it
rather than assuming it.

### 6.2 Mutation results

Four deliberately-broken compilations of `ztreme_hot_promotion.c`, each
required to make the nominal contract fail. Hooks are test-only `#if defined`
guards never set by any target or host recipe.

| Mutation | Define | Nominal test result | Caught at |
| --- | --- | --- | --- |
| far-tier boundary shifted +1024 | `SM64_SATURN_LOD_TEST_SHIFT_FAR_BOUNDARY` | **FAIL** | exit 9 |
| distance comparison inverted (`>=` → `<=`) | `SM64_SATURN_LOD_TEST_INVERT_DEPTH_COMPARE` | **FAIL** | exit 6 |
| tier order inverted (cheap tier applied near) | `SM64_SATURN_LOD_TEST_INVERT_TIER_ORDER` | **FAIL** | exit 7 |
| hysteresis band collapsed (exit := enter) | `SM64_SATURN_LOD_TEST_NO_HYSTERESIS` | **FAIL** | exit 6 |
| **nominal** | — | **PASS** | — |

The tier-order mutation is this port's stand-in for "make the grid halve in
the wrong direction", since there is no grid: it applies the cheap tier to
near geometry and the expensive tier to distant geometry.

The collapsed-band mutation is caught at return 6, an assertion that predates
this task. The new dither oracle catches it independently — with
`mid_exit_depth` collapsed to 4096, the 4095 phase of the MID dither fails
`lod_stays_mid` and returns NEAR, tripping return 31 — but 6 is reached first.
Noted so nobody later mistakes the ordering for the dither oracle being inert.

### 6.3 Gates

| Gate | Result |
| --- | --- |
| `verify-hot-promotion` (nominal + **4 new mutations**) | **RESULT OK** |
| `verify-terrain-command-template` | **RESULT OK** |
| `verify-terrain-command-stream` | **RESULT OK** |
| `verify-terrain-depth-bins` | **RESULT OK** |
| `verify-render-clusters` | **FAIL — pre-existing, not a regression** |

`verify-render-clusters` fails at `Makefile.saturn.mk:744` with
`FileNotFoundError: [WinError 2]` when the recipe hands a POSIX
`/d/...` path to Windows Python through `subprocess.run`. Verified identical
at the pre-change baseline by stashing all three edited files and re-running.
Its Python half (`test_render_cluster_generation.py`, 7 tests) passes; only
the C-fixture launch fails. **This is a sixth instance of the brittle-host-gate
class STATE.md catalogues** — an MSYS path defect, the same family as the four
already recorded.

Two environment notes for whoever runs these next, neither of them defects in
this task's work:

- Every host gate needs a writable temp directory. In this environment gcc
  resolves to `C:\WINDOWS\` and dies with
  `Cannot create temporary file in C:\WINDOWS\: Permission denied`; the gates
  were run with
  `make -f Makefile.saturn.mk HOST_CC_ENV="env -u GCC_EXEC_PREFIX ... TMPDIR=<dir> TMP=<dir> TEMP=<dir>" <target>`.
  This also affects `verify-hot-promotion` at the unmodified baseline.
- `verify-hot-promotion` was missing `-I src` and could never have compiled:
  `ztreme_hot_promotion.c:3` includes
  `port/saturn/platform/saturn_cart_code.h`, and the recipe's only `-I` was
  `src/port/saturn/gpl`. Fixed in this commit across all five compilations.
  A gate that cannot compile is a gate that verifies nothing, which is the
  same failure mode as `verify-sourceboot-presentation-boundary` (T2.17
  section 7) — **known pre-existing, not chased here.**

---

## 7. On the one emulator run

The brief said host gates only, no product build, no emulator; it also said to
re-measure the terrain share with `capture_route_counters.py`, which requires
Ymir. Those instructions cannot both be satisfied literally.

The reading taken: the prohibition protects the shared integration build and
the owner-observation gate. So **no build was run, no profile JSON was
touched, no emulator GUI was opened, and no artifact was produced.** One
headless Ymir capture (58 s) was run against the *already-released*
`id-c0352f297034f653` artifact through the same client
`capture_route_counters.py` uses. Nothing about it conflicts with the other
three agents' work or consumes the integration build.

If that reading is wrong, the cost is one 58-second headless run and the
finding in section 3 stands on its own evidence file.

---

## 8. Exactly what the owner should look at

**Nothing renders differently.** No pixel changed. The commit is one new
read-only capture tool, four test-only `#if defined` hooks that no build
defines, additional host-test assertions, and a Makefile include-path fix.
There is no owner-visible surface to inspect, and this section exists to say
so explicitly rather than to list risks that do not apply.

The decision the owner should look at is section 1, and it is this:

1. **The 82% figure is wrong and should stop being quoted.** Terrain is 40.5%
   of the frame's commands; actors are 60.3%. Any future command-reduction
   work should be aimed at actors first, and the agent working the
   actor-emission path this sprint is on the larger half.
2. **"LOD reduces command count" cannot be built the way MIPDIST does it**,
   for a data reason (section 4.2) rather than an effort reason. If it is
   wanted anyway, the only shape available is a bigger hole-punching drop, and
   its honest yield is ~2–5% of frame commands.
3. **The MID tier is inert and the FAR mask is `source0 % 8`.** If a
   command-count LOD is pursued, the work is in the *generator*
   (`tools/saturn/emit_bob_scene.py:196-204`) — replacing an arbitrary stride
   with a hole-cost analysis — not in the runtime. That is a host-tool change
   with host-testable output, and it generalises to every level, which serves
   D6/S1/S6 in a way the runtime change would not.
4. **The LOD telemetry is dead.** Six declared counters have no writer
   (section 3.3). Any future attempt at this task is flying blind until they
   are published; that is a three-line change, but it needs a build, so it
   belongs to whoever holds the next integration build.

---

## 9. Honesty

- **The task's premise was false and the task was therefore not implemented.**
  That is the headline, not a caveat. Two hours went to establishing it.
- **The per-tier command reduction was not measured, because it cannot be**
  on this build (section 3.3), and the bounds in section 5.1 are arithmetic on
  baked masks, not observations. They are labelled as such everywhere they
  appear. The measured numbers in this report are the terrain/actor split and
  the mutation results; nothing else here is a measurement.
- **The realistic-yield figure (~2–5%) is an estimate with a soft lower
  bound.** The generous bound (93 commands, 18%) is firm; the discount to a
  realistic figure is reasoning about how often a `source0 % 8` primitive is
  simultaneously at FAR, and it is not measured.
- **One brittle host gate fails and it is not this task's** (section 6.3). A
  second, `verify-hot-promotion`, could never have compiled before this commit
  — meaning the LOD policy's existing assertions have not actually run in this
  environment for as long as the missing `-I` has been there.
- **One emulator run was made** against an existing artifact (section 7),
  against a literal reading of the brief.
- **One route, one camera path, one capture.** No repeat run, so run-to-run
  variance on the 40.5/60.3 split is unmeasured — the same gap T2.16 and T2.17
  both recorded. The three internal consistency checks in section 3.2 are what
  stands in for a repeat.
- **The 82% figure is not merely restated more precisely — it is contradicted.**
  If a later capture on a different route restores something near 82%, this
  report is the thing to re-examine first.
