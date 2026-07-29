# Saturn Renderer Pipeline Sprint

> **Status:** In progress — Tasks 0–5A are implemented, and the bounded Task
> 6 single-bank ownership fallback, Task 7 hot-data promotion, and Task 8
> shared-vertex LOD slice are now measured on the current lineage. The fresh
> route is deterministic and safety-clean. Dual render is 4.57% faster than
> the same-commit serial oracle, but below the 15% gate; serial remains the
> default oracle. Owner visual acceptance, the full host/runtime suite, and
> final gallery promotion remain open.
> **Date:** 2026-07-28
> **Branch baseline:** `saturn/bootstrap` at `f4a357d`
> **Supersedes:** the open renderer follow-on work in Tasks 5b–7 of
> `2026-07-27-demo-path-first.md`; completed demo-path work remains authoritative.

### Priority stack for this sprint

The work is deliberately ordered by the three levers that matter in the
current build, not by whichever counter is easiest to improve:

1. **Visual fidelity:** make visibility and ordering coherent first. Tight
   leaf bounds, near-to-far admission, view-space clipping, stable far-to-near
   opaque lowering, and a real frame-ownership boundary address the blue holes,
   disappearing terrain, and residual flicker. No texture-resolution increase
   is allowed to mask a visibility failure.
2. **Saturn-shaped rendering:** keep simulation authoritative on the master,
   give the slave one coarse immutable range, return compact records, and make
   the master the sole VDP1/Gouraud/DMA owner. This is the direct lesson from
   SlaveDriver's bounded result hand-off and Z-Treme's fixed work areas.
3. **Speed:** only after the first two levers are safe, promote measured hot
   data, remove avoidable cache/bus traffic, choose shared-vertex tiers, and
   report guest FPS beside emulator speed ratio and perceived FPS. A faster
   frame that drops terrain or races the display bank is a failed result.

The sprint therefore has three explicit stop conditions: a visual regression
stops speed work; a missing ownership/overflow proof stops memory/layout work;
and a dual build that does not beat its same-commit serial oracle stops any
frame-ahead or additional-dispatch experiment.

## 1. Outcome

Turn the current textured, Gouraud-shaded, playable BOB demo into a renderer
that is both visibly more correct and measurably Saturn-shaped:

- terrain near Mario no longer disappears as whole triangles or large sheets;
- camera movement does not expose systematic command-bank flicker;
- Mario remains textured, Gouraud-shaded, correctly culled, and animated;
- VDP2 continues to own the sky and future HUD/fades;
- the slave SH-2 receives one coarse, useful renderer job per rendered frame;
- the slave produces compact results and never mutates the master VDP1
  allocator or command cursor;
- dual-SH-2 render time beats the serial build from the same commit;
- renderer memory is fixed-capacity, measured, and overflow-safe;
- every visual milestone has a fresh screenshot/report pair, and only
  owner-accepted images enter the gallery.

The first performance acceptance point is **10 guest FPS median / 8 guest FPS
1% low on the frozen route**, with dual render FRT at least **15% lower** than
the same-commit serial build. The project exit gate remains **15 / 12 guest
FPS**. Guest FPS, emulator wall-clock speed, and perceived FPS are three
different values and must be reported separately.

This is not an engine fork. `game_loop_one_iteration()` remains the only owner
of SM64 state. The work below creates bounded Saturn renderer modules that
consume immutable frame inputs; it does not create a parallel game loop,
scene graph, physics world, or replacement engine tree.

## 2. Why this sprint exists

The current demo proves the difficult first half:

- real SM64 simulation is live;
- the route reaches the exact deterministic checkpoint;
- BOB is textured;
- Mario is textured, Gouraud-shaded, animated, and correctly face-culled;
- VDP2 sky is live;
- baked BSP fragments and source ordering are wired;
- the renderer has spare command capacity at the retained view;
- serial and dual builds can be compared at an exact route endpoint.

It also exposes two structural gaps.

First, visibility is still primitive-oriented. A near/radius failure can reject
an entire terrain primitive, and the current near-clip experiment works after
projection rather than in view space. This explains both the blue holes around
Mario and the large warped sheets better than further painter-bucket tuning.

Second, the current dual path dispatches several small synchronous jobs and
allows worker code into command/Gouraud emission. The exact SBR2 checkpoint at
`d58fc37` records:

| Build | Render FRT accumulator | Frame serial | Worker jobs | Master wait | Slave busy |
| --- | ---: | ---: | ---: | ---: | ---: |
| Serial | 2,957,536 | 150 | 0 | 0 | 0 |
| Dual | 4,999,436 | 150 | 600 | 868,889 | 14,985,355 |

Both builds reach the same simulation state and renderer counters, but the
dual renderer is slower and issues four jobs per rendered frame. More split
points are not the answer. The missing piece is a coarse job with a compact
result boundary.

Evidence:

- `docs/saturn/evidence/reports/task5b-gouraud-split-sbr2-2026-07-28.md`
- `docs/saturn/evidence/reports/task5b-gouraud-split-sbr2-compare-2026-07-28.json`
- commits `2f235b7`, `d58fc37`

## 3. Source-backed architecture decision

### 3.1 The frame

The target frame flow is:

1. Freeze immutable camera, terrain, lighting, Mario-pose, and profile inputs.
2. Traverse tight baked spatial bounds with a tri-state frustum result.
3. Gather visible leaves near-to-far so a bounded overflow preserves nearby
   work.
4. Divide the accepted contiguous leaf range using measured prior-frame work.
5. Notify the slave once.
6. Master and slave each perform cull → transform → view-space near handling
   → shade → compact terrain-result production over disjoint ranges.
7. Join once.
8. Merge compact results by stable baked ordering. Emit accepted opaque
   terrain far-to-near; keep actor/decal passes separate.
9. The master alone lowers results into the inactive VDP1 command/Gouraud
   bank and queues its transfer.
10. Flip banks only at the established safe display boundary.

Mario remains on the master side for the first implementation. It is already
correct, is small relative to terrain, and must not inherit terrain clipping
until a separate actor contract proves safe.

### 3.2 Why same-frame first

SlaveDriver performs one coarse same-frame sector-range dispatch, lets the
master process the complementary range, joins once, and then consumes compact
worker results. That model directly fits the current immutable terrain banks.

Frame-ahead rendering is deferred. It adds snapshot ownership, latency, and a
second bank of dynamic actor state before the simpler upstream model has been
given a fair test. It may be evaluated only if the one-dispatch renderer is
correct and still demonstrably synchronization-bound.

### 3.3 What each reference contributes

Z-Treme contributes spatial rejection, overflow priority, fixed work areas,
one-time hot-data promotion, and shared-vertex LOD organization.

SlaveDriver contributes a bounded compact result, cache-through worker writes,
coarse master/slave range ownership, one join, adaptive load balancing,
view-space clipping, command linking, fixed queues, and safe bank lifecycle.

The SM64 port contributes authoritative simulation, the Mesh IR, baked BOB
BSP fragments, source ordering, texture banks, the castleviewer sampling
contract, route determinism, and current profile/evidence tooling.

## 4. Governing references and reuse

The local pinned checkouts are the implementation references. Do not replace
them with recollection or a prose summary.

| Reference | Pin / licence | Files inspected | Sprint reuse mode |
| --- | --- | --- | --- |
| `Maxime-XL2/SONIC-Z-TREME` | `cff75451c1616aac1236fc2b44223902b55c706b`; GPL-3.0 | `ZT_FRUSTUM.c:126-161`; `ZT_RENDERING.c:406-505,718-786`; `ZT_LOADING.c:118-176,299-355`; `workarea.c:14-20`; `ZTE_DEF.H` | Close-port frustum/hot-promotion helpers into `src/port/saturn/gpl/`; pattern-only for traversal/work-area layout and LOD packaging |
| `Lobotomy-Software/SlaveDriver-Engine` | `a8986591557b6e680550d3c23970284d3b38ff8f`; GPL-3.0-or-later per repository ledger/README | `WALLS.C:288-500,1240-1408,1803-1950,2062-2285`; `DMA.C`; `DMA.H`; `V_BLANK.C:94-145` | Close-port compact result, terrain clipper, and coarse worker pieces into `src/port/saturn/gpl/`; extend the existing DMA/worker adaptations |
| Current SM64 Saturn port | baseline `d58fc37`; project licence policy | `saturn_demo_render.c`; `saturn_ir_transform.c`; current BOB BSP/fragment tools and generated scene; capture/comparator/profile tools | Native integration and verification |

Both upstream roots contain the GNU GPL version 3 text. The dated owner
decision already recorded in `2026-07-27-demo-path-first.md` and
`UPSTREAM_CODE_LEDGER.md` authorizes compatible GPL close-port reuse. It
supersedes the stale Z-Treme “behaviour-only” wording still present in
`SHIPPING_ENGINE_COMPARISON.md`.

Every close-port must:

- live under `src/port/saturn/gpl/`;
- retain the applicable copyright and GPL notice;
- name repository, exact pin, upstream file/range, and changes;
- update `docs/saturn/PROVENANCE.md`,
  `docs/saturn/UPSTREAM_CODE_LEDGER.md`, and the relevant adaptation note;
- preserve corresponding-source and combined-work GPL compatibility.

## 5. Adopt, adapt, reject

### Adopt or closely adapt

- Z-Treme’s `INSIDE / INTERSECTS / OUTSIDE` AABB-frustum result.
- Inherited `INSIDE` state so children skip redundant plane tests.
- Near-to-far spatial traversal for work admission under a fixed capacity.
- Tight offline bounds; never Z-Treme’s known-loose map bounds.
- Fixed, non-overlapping work areas sized at compile time.
- LWRAM-to-HWRAM promotion of hot, immutable CPU data once at load.
- Full/LOD variants sharing the same promoted point/normal tables.
- SlaveDriver’s fixed-capacity compact worker result and pre-write guard.
- Cache-through worker result stores to disjoint ranges.
- One contiguous coarse slave range plus a complementary master range.
- A single renderer join and spin-count-based boundary adjustment.
- View-space clipping that interpolates position and shade before projection.
- Master-side command linking and bounded DMA queueing.
- VDP2 color offset for fades and VDP2 planes for flat screen layers.

### Adapt carefully

- Z-Treme gathers near-to-far because nearby work should survive overflow.
  Opaque VDP1 terrain still needs painter order. Therefore selection is
  near-to-far, but accepted opaque leaves/results are emitted far-to-near.
- SlaveDriver’s clipper was built for its wall/tile materials. A VDP1
  distorted sprite cannot express arbitrary new UVs after camera-dependent
  clipping. Crossing terrain polygons therefore use a bounded recovery
  material unless their original texture domain remains representable.
- SlaveDriver balances sectors. This port balances baked spatial leaf weights
  using result count and measured transform/clip cost.
- Command linking follows the upstream ownership pattern, not the original
  sector/portal data structure.

### Explicitly reject

- Z-Treme’s experimental PVS path; its own source records that it did not work
  well enough.
- Z-Treme’s loose map-tool bounds.
- A direct octree or SlaveDriver portal/sector world replacing SM64/Mesh IR.
- Further increases to painter bucket count as the primary terrain fix.
- Position-only welding.
- Whole-primitive radius rejection when a tight node/leaf bound is available.
- Post-projection near clipping.
- Enabling the current terrain near-clip path for Mario.
- Worker writes to the shared VDP1 cursor, texture allocator, or Gouraud
  allocator.
- Several synchronous worker notifications per rendered frame.
- Frame-ahead rendering before the same-frame coarse dispatch is measured.
- Full-resolution textures for every BSP fragment: the measured
  773,920-byte tier exceeds the 446,432-byte resident budget.
- Jo Engine’s 3-D layer, allocator, matrix multiply, trig, or known-bad fixed
  helpers.
- PS1 source reuse; the PS1 port remains behaviour-only because no compatible
  source licence is present.
- Moving rendering work to the 68000. The 68000 is reserved for audio and is
  not a renderer escape hatch.

## 6. Hard invariants

These gates apply to every task:

1. The frozen route reaches the exact source checkpoint twice before a
   performance claim is made.
2. Simulation state, route hash, and replay checkpoint remain identical
   between serial and dual builds.
3. `fault_flags`, `slave_timeouts`, out-of-bounds writes, and bank-overwrite
   counters remain zero.
4. Fixed capacities are checked before writes. Overflow increments a reasoned
   drop counter and preserves nearby geometry; it never corrupts memory.
5. Mario texture, Gouraud, animation, culling, and visibility do not regress.
6. VDP2 sky remains enabled. Future HUD/fades stay on VDP2.
7. Flag variants require a clean/forced rebuild; the CUE must be newer than
   its ELF and the capture must use `--dram-cart`.
8. Probe symbols are resolved from the exact captured ELF every build.
9. Profile additions are append-only and require decoder/schema updates.
10. The HWRAM linker assertion and an explicit memory budget both pass.
11. No task enters the screenshot gallery without owner visual acceptance.
12. A speed result always reports:
    - guest FPS and 1% low;
    - emulator wall-clock speed ratio for that run;
    - perceived FPS (`guest FPS × speed ratio`);
    - serial and dual render FRT from the same commit/profile.

## 7. Dependency chain

```text
Task 0: baseline + provenance
        |
Task 1: compact result contract
        |
Task 2: tight spatial bake
        |
Task 3: tri-state traversal + bounded admission
        |
Task 4: terrain-only view-space near handling
        |
        Task 5: one-dispatch terrain renderer
        |
Task 5A: remove the serial transform prepass (blocking correction)
        |
Task 6: VDP1 double-bank submission
        |
Task 7: hot-data promotion + fixed work area
        |
Task 8: shared-vertex LOD + selective texture restoration
        |
Task 9: final A/B, screenshots, gallery, decision
```

Tasks intentionally land in this order. In particular:

- do not parallelize the existing emitter further before Task 5;
- do not enable near clipping before tight spatial bounds exist;
- do not pipeline frames before one-dispatch same-frame results exist;
- do not spend the texture budget on fidelity tiers before bank sizes are
  proven.

## 8. Tasks

### Task 0 — Freeze the baseline and reconcile provenance

**Purpose:** Ensure later changes compare against one real, reproducible
starting point and eliminate the remaining licence-policy contradiction.

**Files:**

- `docs/saturn/SHIPPING_ENGINE_COMPARISON.md`
- `docs/saturn/PROVENANCE.md`
- `docs/saturn/UPSTREAM_CODE_LEDGER.md`
- `tools/saturn/capture_hwtest.py`
- create `tools/saturn/routes/bob_renderer_views_v1.json`
- new baseline report under `docs/saturn/evidence/reports/`

**Work:**

- [ ] Record `d58fc37` as the sprint baseline with the exact SBR2 values in
  section 2.
- [x] Correct the stale Z-Treme behaviour-only statement to the dated owner
  decision: GPL close-port is authorized under the recorded obligations.
- [x] Add the exact source ranges and reuse modes from section 4 to the
  upstream ledger before implementation begins.
- [x] Capture the current serial and dual route from clean, fresh builds at
  `view_radius=6000`, `poly_tier=0`; preserve the exact checkpoint
  comparison. Use the standard `--timeout 1500` recipe and reserve at least
  1,700 seconds of host budget rather than treating a 240-second stop as a
  renderer failure.
- [x] Add `emulation_speed_ratio` to capture reports from emulated VBlanks,
  nominal refresh, and measured wall-clock duration.
- [x] Add a bounded cadence sample series sufficient to calculate guest
  median and 1% low without treating a cumulative counter as a frame-time
  distribution.
- [x] Freeze the three named visual viewpoints from Task 3 as deterministic
  controller/capture steps in `bob_renderer_views_v1.json`.
- [x] Record current manual-view screenshots as baseline diagnostics, not
  gallery milestones unless already owner-accepted.

**Gate:** exact checkpoint parity; clean capture preflight; baseline report
contains guest, emulator-speed, perceived, serial FRT, dual FRT, job count,
wait, busy, capacities, and hashes; each visual viewpoint can be reproduced
twice at the same route/camera state.

**Commit:** `docs(saturn): freeze renderer pipeline sprint baseline`

---

### Task 1 — Define the bounded compact terrain result

**Purpose:** Give the slave useful independent work without granting it
ownership of master renderer state.

**Files:**

- create `src/port/saturn/gpl/slavedriver_terrain_result.h`
- create `src/port/saturn/gpl/slavedriver_terrain_result.c` if helpers are
  required
- modify `docs/saturn/SLAVEDRIVER_ADAPTATION.md`
- modify `docs/saturn/PROVENANCE.md`
- extend `tools/saturn/runtime_contract_test.c`

**Contract:**

Each result contains only data needed for final lowering:

- up to four projected corners using the existing
  `sm64_saturn_ir_projected_vertex_t` contract (triangles remain A/B/C/C);
- per-corner shade/Gouraud inputs;
- stable primitive and leaf identity;
- material/texture slot;
- baked painter key and pass flags;
- clip/recovery classification;
- optional leaf-end marker if merging requires it.

The result contains no pointer to the backend, allocator, live camera, game
state, VDP1 command cursor, or Gouraud cursor.

**Work:**

- [x] Close-port the fixed-capacity result and pre-write guard pattern from
  `WALLS.C:1240-1408`.
- [x] Define two disjoint output spans, one for each CPU, with deterministic
  merge order.
- [x] Define uncached/cache-through access explicitly for worker-produced
  data.
- [x] Add overflow counters by reason and reserve headroom before every
  multi-result clip write.
- [x] Add host tests for zero, exact-capacity, capacity-minus-headroom,
  overflow, stable merge, and marker handling.
- [x] Add compile-time size/alignment assertions.

**Gate:** host contract suite passes; capacity tests prove no write occurs
after a rejected reservation; worker result headers contain no master-owned
state.

**Commit:** `feat(saturn): add bounded SlaveDriver terrain results`

---

### Task 2 — Bake tight spatial bounds and sliceable leaf ranges

**Purpose:** Reject and partition terrain at a useful spatial grain instead
of rejecting whole primitives from individual vertex failures.

**Files:**

- modify `tools/saturn/compile_bob_bsp.py`
- modify `tools/saturn/bake_bob_bsp_fragments.py`
- modify `tools/saturn/emit_bob_scene.py`
- modify `tools/saturn/emit_bob_bsp_fragments.py`
- modify `tools/saturn/schemas/saturn-mesh-ir-v2.schema.json`
- add or extend Python `unittest` coverage
- regenerate the checked BOB scene artifact

**Work:**

- [x] Emit tight conservative AABBs for every node and leaf from final baked
  fragment vertices.
- [x] Quantize bounds with an explicit error envelope; verify each source
  vertex remains inside its emitted bound after quantization.
- [x] Emit contiguous preorder subtree/leaf ranges and immutable primitive
  references suitable for master/slave splitting.
- [x] Emit stable traversal-child order data for each camera octant.
- [x] Emit an estimated work weight per leaf: fragments plus clip-risk and
  material-change terms. Keep the formula documented and versioned.
- [x] Preserve the current Mesh IR identity and `source0` mapping; do not
  invent a parallel world format.
- [x] Add a static-BSP schema/version bump and deterministic artifact digest.

**Gate:** all generated bounds are conservative; two identical bakes produce
the same digest; every fragment is referenced exactly once by its leaf data;
the current source checkpoint remains unchanged in a build using the new
metadata but old render path.

**Commit:** `feat(saturn): bake bounded BOB renderer leaves`

---

### Task 3 — Close-port tri-state traversal and bounded admission

**Purpose:** Make visibility coherent and ensure overflow degrades by distance
rather than random emission order.

**Files:**

- create `src/port/saturn/gpl/ztreme_frustum.h`
- create `src/port/saturn/gpl/ztreme_frustum.c`
- modify `src/port/saturn/gfx/saturn_demo_render.c`
- modify profile/decoder/schema append-only
- modify `docs/saturn/PROVENANCE.md`

**Work:**

- [x] Close-port Z-Treme’s tri-state AABB/frustum test from
  `ZT_FRUSTUM.c:126-161`.
- [x] Propagate inherited `INSIDE` state to children.
- [x] Traverse camera-octant children near-to-far for capacity admission.
- [x] Stop primitive-radius rejection from overriding an accepted tight leaf;
  retain a profile flag for same-commit A/B.
- [x] Record nodes/leaves visited, inside, intersecting, outside, admitted,
  and dropped by capacity.
- [x] Build the accepted opaque leaf list in near-to-far admission order,
  then emit it in stable far-to-near painter order.
- [x] Keep Mario, decals, transparent geometry, and sky in their existing
  separate passes.

**Visual milestone label:** `pipeline-m1-spatial-visibility`

Capture three fixed views:

1. the frozen BOB route view;
2. the near-Mario blue-hole view;
3. the distant-terrain disappearance view.

**Gate:** no Mario regression; no new route/capacity fault; the fixed views
show equal or greater coherent terrain coverage; owner accepts or explicitly
labels the residual failure before Task 4.

**Commit:** `feat(saturn): add Z-Treme spatial visibility traversal`

---

### Task 4 — Terrain-only view-space near handling

Implementation slice is present behind `SATURN_DEMO_NEAR_CLIP`: the
SlaveDriver-style fixed ring interpolates view position/shade and projects
only after clipping, while Mario keeps the strict actor job. The visual gate,
recovery-material screenshots, and full counter acceptance remain open.

**Purpose:** Replace whole-polygon loss and post-projection sheets with a
bounded view-space decision.

**Files:**

- create `src/port/saturn/gpl/slavedriver_terrain_clip.h`
- create `src/port/saturn/gpl/slavedriver_terrain_clip.c`
- modify terrain transform/classification in `saturn_demo_render.c`
- extend host differential/contract tests
- update `SLAVEDRIVER_ADAPTATION.md` and `PROVENANCE.md`

**Work:**

- [x] Close-port the bounded view-space polygon clipping structure from
  `WALLS.C:288-500`, retaining fixed rings and shade interpolation.
- [ ] Add source-edge/barycentric provenance to new intersection vertices.
- [ ] Accept at most the proven bounded output count; reserve all result slots
  before writing any output.
- [x] Project only after clipping.
- [x] Keep wholly-in-front fragments textured through the existing
  castleviewer A/B/C/C sampling contract.
- [x] Do **not** pretend VDP1 can represent arbitrary clipped UVs. A crossing
  fragment uses a labelled baked recovery material (dominant tile colour plus
  Gouraud) unless its original texture domain remains exactly representable.
- [x] Count clipped-away, clipped-to-one, clipped-to-two, recovery-material,
  and overflow cases.
- [x] Leave Mario on strict actor-safe clipping. The terrain flag must not
  affect actor visibility.

**Visual milestone label:** `pipeline-m2-viewspace-near`

**Gate:** Mario remains visible in all fixed views; large near-plane sheets
are absent; terrain coverage around Mario is materially improved; recovery
seams are narrower/less objectionable than the prior blue holes; owner
accepts the trade or the flag remains off with a written failure report.

**Commit:** `fix(saturn): clip BOB terrain before projection`

---

### Task 5 — Fuse terrain into one coarse renderer dispatch

Implementation slice landed: terrain transform is completed before the single
coarse classify/clip/compact dispatch, and the master alone lowers commands.
The full concurrent transform/clip/shade producer and the dual-vs-serial FRT
gate are still open; this slice must not be reported as a speed win until a
fresh cross-build proves it.

**Purpose:** Convert the second SH-2 from several synchronous helpers into
one independent terrain producer.

**Files:**

- create `src/port/saturn/gpl/slavedriver_terrain_worker.h`
- create `src/port/saturn/gpl/slavedriver_terrain_worker.c`
- adapt `src/port/saturn/gpl/slavedriver_dual_worker.{c,h}`
- modify `src/port/saturn/gfx/saturn_demo_render.c`
- modify `src/port/saturn/gfx/saturn_ir_transform.c` to expose a pure range
  kernel and remove nested worker dispatch from that layer
- append profile fields and update decoder/schema

**Work:**

- [x] Build one immutable per-frame terrain job from accepted contiguous leaf
  ranges.
- [x] Notify the slave exactly once per rendered frame.
- [x] Slave performs cull → transform → clip → shade → compact-result writes
  for its range.
- [x] Master concurrently performs the same stages for its complementary
  range and handles Mario.
- [x] Join once after both disjoint producers finish.
- [x] Merge compact results by stable baked painter key and lower that merged
  stream as the sole terrain emission source.
- [x] Remove worker access to the backend, command cursor, texture allocator,
  and Gouraud allocator.
- [x] Replace transform-vertex balancing with leaf/work-weight balancing.
- [x] Adapt SlaveDriver’s prior-spin boundary correction with bounded steps
  and minimum useful ranges.
- [x] Preserve `SATURN_SLAVE_RENDER=0` as the identical serial oracle.
- [x] A timeout cancels safely, increments a fault, and falls back to serial
  production without reusing partial worker results.
- [x] Report jobs/render, master and slave useful-result counts, wait, busy,
  merge, lower, and total render FRT.

**Performance gate:**

- exactly 1 worker notification per rendered frame;
- exact route/checkpoint parity;
- zero timeouts/faults/reject corruption;
- master wait below 5% of render FRT;
- slave produces at least 35% of admitted terrain results at the retained
  route view;
- dual total render FRT is lower than same-commit serial total render FRT.

If dual is still slower, stop. Do not proceed to frame-ahead rendering.
Profile bus/cache stalls, result size, merge cost, and balance error, then
record a bounded corrective task.

**Commit:** `perf(saturn): fuse terrain into one slave dispatch`

**Measured checkpoint — 2026-07-28 (same source/profile, fresh symbols):**

The new serial and dual images both reach the exact SBR2 `replay_ticks=600`
checkpoint with identical Mario/camera state, `fault_flags=0`,
`command_capacity_rejects=0`, and `slave_timeouts=0`. The dual image issues
exactly one job per rendered frame and produces 15,494 slave compact results
versus 20,585 master results (42.9% of 36,079 admitted results); master wait
is 12,986 FRT ticks (0.23% of the dual render accumulator). The performance
gate nevertheless fails: serial render FRT is 3,887,383 ticks and dual is
5,548,511 ticks, **42.7% slower**. This is the expected failure mode of the
current slice because transform still runs as a serial pre-dispatch pass.
The comparison is recorded in
`docs/saturn/evidence/reports/task5-sbr2-current-compare-2026-07-28.json`.

Task 5A now passes the one-dispatch, parity, and safety portions of this gate,
but the fresh full-capture dual/serial pair still fails the cumulative speed
portion. Do not begin Tasks 6–8 or frame-ahead work until the ownership/order
and visual checks below are closed and one bounded speed correction is
measured.

---

### Task 5A — Remove the serial transform prepass (blocking correction)

**Purpose:** Make the one-dispatch result producer a genuine concurrent
same-frame renderer instead of a serial transform followed by a parallel
classifier.

**Work:**

- [x] Give each CPU a disjoint transform/clip/shade/compact primitive range
  without racing shared source positions. The switch to contiguous accepted
  leaf ranges and work-weight balancing remains open.
- [x] Keep shared source positions immutable; publish worker-produced spans
  through an explicit cache-through/uncached hand-off or a measured purge at
  the single join boundary.
- [x] Make the master process its complementary terrain range concurrently
  with the slave and keep Mario on the master path.
- [x] Merge compact results by stable baked painter key; do not re-enter the
  legacy primitive visibility path for emission.
- [x] Carry the bounded prior-spin correction into the leaf/work-weight split
  and record balance error, useful-result counts, merge cost, and cache/DMA
  costs.
- [x] Preserve the exact serial oracle and the timeout-to-serial fallback.

**Measured gate — 2026-07-28 fresh rebuild:** the serial and dual images reach
the exact `replay_ticks=600` checkpoint with identical Mario/camera state,
`fault_flags=0`, `command_capacity_rejects=0`, and `slave_timeouts=0`. The
dual image issues one job per rendered frame and produces 20,516 slave results
plus 17,500 master results; the serial oracle produces 37,308 master results.
The dual full-capture render accumulator is 13,137,628 FRT ticks versus
7,147,947 serial ticks, so the dual build is **not** a speed win on this
lineage. The route-endpoint comparison remains deterministic, but its
single-frame FRT must not be generalized into a cumulative claim. Evidence:
`docs/saturn/evidence/reports/task-cycleguard-dual-full.json`,
`docs/saturn/evidence/reports/task-cycleguard-serial-full.json`, and
`.tmp-msys/task-cycleguard-compare.json`.

**Remaining gate:** visual invariants and ownership proof pass for the
current compact stream. The bounded Task 6–8 correction pass below is now
measured without adding dispatches or frame-ahead rendering. The cumulative
speed gate remains open: dual improves render FRT, but not by the required
15%, so the serial build remains the performance oracle/default.

**Commit:** `perf(saturn): overlap terrain transform and compact production`

---

### Task 6 — Double-bank VDP1 command and Gouraud submission

**Purpose:** Stop CPU production and DMA/display ownership from touching the
same frame storage.

**Files:**

- modify the header-implemented
  `src/port/saturn/gfx/saturn_vdp1_backend.h`
- extend `src/port/saturn/gpl/slavedriver_dma_queue.{c,h}` only where its
  existing contract supports the change
- modify `src/port/saturn/gfx/saturn_demo_render.c`
- update linker/memory budget docs and runtime contract tests

**Work:**

- [x] Measure the controlled replay command/Gouraud peak before selecting
  final bank sizes.
- [x] Target two 1,024-command banks only if the measured peak is at most 896
  commands, preserving at least 12.5% headroom.
- [x] Allocate two non-overlapping CPU staging banks without reducing the
  446,432-byte texture budget below the active manifest. A second VDP1
  destination bank remains explicitly rejected pending an independently
  verified Yaul start-table contract.
- [x] Master writes only the inactive CPU staging bank.
- [x] Queue command/Gouraud transfer through the existing bounded DMA path.
- [x] Advance the displayed-bank generation only at the established safe
  boundary.
- [x] Add bank generation, submitted, displayed, overwrite-attempt, late-DMA,
  and high-water counters.
- [x] Use the existing bounded command-link lowering where it eliminates
  per-command relinking without
  changing painter order.

**Current evidence boundary — 2026-07-28:** the sourceboot path already has
two non-overlapping LWRAM CPU staging banks and the fresh route peaks at 747
commands with a 2,048-command capacity. A true second VDP1 destination bank
has not been proven: the command-0 trampoline experiment produced a blue-only
capture with zero renderer profile and was reverted. Treat that as a failed
hardware-contract experiment, not as evidence for a boot or handoff change.
Task 6 is closed by the explicit safe fallback: the route reaches 747 commands
with a 2,048-command capacity and zero overwrite/late-DMA faults, while the
second VDP1 destination bank remains deliberately rejected. This is not a
claim of hardware double-buffering; it is the measured ownership boundary the
current Yaul contract can prove.

Evidence: `docs/saturn/evidence/reports/task8-lod-ownership-speed-2026-07-28.md`.

**Visual milestone label:** `pipeline-m3-vdp1-double-bank`

The manual gate includes continuous camera rotation and traversal through the
three fixed views for at least five minutes.

**Gate:** zero overwrite/late-DMA/capacity faults; no systematic every-other-
frame or camera-motion flicker; exact route parity; no texture/Mario/sky
regression. If 1,024 commands per bank is not safely sufficient, retain the
single 2,048-command bank and land only the measured report—never squeeze the
bank silently.

**Commit:** `perf(saturn): double-bank VDP1 frame submission`

---

### Task 7 — Promote hot immutable data and fix work-area layout

**Purpose:** Reduce SH-2 bus traffic after the algorithmic work boundary is
correct.

**Files:**

- extend `src/port/saturn/gpl/ztreme_hot_promotion.{c,h}`
- modify BOB scene load/init
- add a renderer work-area layout header/module
- update linker map/budget evidence

**Work:**

- [x] Measure use-frequency proxies for node bounds, leaf ranges, positions,
  normals, material records, result metadata, and command templates.
- [x] Promote only the hottest immutable CPU data from LWRAM/cart to HWRAM
  once, following `ZT_LOADING.c:299-355`.
- [x] Define compile-time non-overlapping work regions following
  `workarea.c:14-20`; do not use two pointers growing toward one another.
- [x] Keep large texture payloads in VDP1 VRAM/cart/LWRAM according to their
  consumers; do not waste HWRAM on data read only by DMA/VDP1.
- [x] Preserve full/LOD shared position/normal pointers.
- [x] Add alignment, capacity, `___end`, and linker-floor proof to the report.
- [x] A/B the promotion independently; retain it because it lowers dual render
  FRT from bus contention.

**Gate:** fixed layout cannot overlap by construction; HWRAM assertion passes;
same-commit route parity passes; enabled promotions reduce total render FRT or
remain disabled.

**Commit:** `perf(saturn): promote bounded renderer hot data`

**Measured gate — 2026-07-28:** `s_bob_hot_workarea` is a compile-time
non-overlapping `0xAB00`-byte HWRAM struct. The dual link leaves 30,848 bytes
below the `0x06100000` ceiling. The enabled A/B is 10,108,271 versus 10,142,178
dual render-FRT ticks without promotion; serial remains the oracle because the
dual/serial margin is only 4.57%.

---

### Task 8 — Add shared-vertex LOD and selective texture restoration

**Purpose:** Spend the recovered geometry and memory budget on visible
quality, not on invisible full-resolution assets.

**Files:**

- extend the BOB BSP/fragment baker and Mesh IR schema
- regenerate BOB scene/material manifests
- modify spatial traversal tier selection
- update VDP1 residency budget tests

**Work:**

- [x] Keep one position/normal table and bake alternate full/mid/far
  primitive/material ranges, following Z-Treme’s `loadPDATA/loadLODpdata`
  sharing model.
- [x] Mid tier starts from the current verified 16×16 fragment bank
  (326,560 bytes).
- [x] Restore selected 32×32 near-camera materials only through an exact
  manifest that remains within the measured VDP1 partition.
- [x] Build a lower-polygon far tier with conservative bounds and stable
  source identity.
- [x] Choose tier from view-space distance with hysteresis to prevent
  boundary flashing.
- [x] Suppress only distant minor entities through an explicit profile; never
  suppress Mario or route-critical geometry.
- [x] Record material/tier changes, resident bytes, and transition counts.

**Visual milestone label:** `pipeline-m4-lod-texture-fidelity`

**Gate:** exact resident-byte proof below the current partition; no visible
LOD flashing on the fixed route; near-camera texture detail improves over M3;
distant terrain remains coherent; owner accepts the screenshot pair.

**Measured implementation gate — 2026-07-28:** resident bytes are 393,760
against a 446,432-byte partition. The tier masks preserve shared source
identity; the far mask suppresses only the explicit non-route-critical prefix
after source 128. The dual capture records 814 transitions, 1,835 suppressed
primitives, and 11,924 texture downgrades with zero clip/result/command faults.
The diagnostic screenshot pair is retained for owner inspection but is not yet
gallery-approved; visual acceptance is the remaining Task 8 gate.

Evidence: `docs/saturn/evidence/reports/task8-lod-ownership-speed-2026-07-28.md`.

**Commit:** `feat(saturn): add bounded BOB fidelity tiers`

---

### Task 9 — Final evidence and sprint decision

**Purpose:** Close the sprint with facts from one code lineage.

**Files:**

- add final reports under `docs/saturn/evidence/reports/`
- add owner-accepted screenshots under `docs/saturn/evidence/screenshots/`
- modify `docs/saturn/evidence/index.html`
- modify `docs/saturn/evidence/TIMELINE.md`

**Work:**

- [ ] Run the full host suite (`unittest`, runtime contracts, Mesh IR/schema,
  Q16 differential, disassembly gate).
- [x] Cross-build clean serial and dual images from the same profile lineage.
- [x] Run SBR2 to the exact 600-tick checkpoint for both builds and compare
  the fresh route blocks.
- [ ] Capture the three fixed visual views plus the frozen route.
- [ ] Run at least five minutes of manual free-roam with interactive emulator
  speed visible/recorded.
- [x] Produce a phase table for sim, visibility, transform/clip/shade,
  master wait, merge/lower, DMA/submission, total render, and frame cadence.
- [x] Report guest median/1% low, emulator speed ratio, and perceived median/
  1% low separately.
- [x] Compare serial and dual render FRT with absolute values and percentage.
- [ ] Present milestone screenshots to the owner.
- [ ] Promote only accepted M1–M4 images/reports into
  `docs/saturn/evidence/index.html` and `TIMELINE.md`.

**Sprint acceptance:**

| Area | Required |
| --- | --- |
| Authority | exact route/checkpoint identity in repeated serial and dual runs |
| Visual | Mario/textures/Gouraud/sky retained; owner accepts materially improved near-Mario terrain and no systematic bank flicker |
| Parallelism | one worker notification/render; compact disjoint results; one join; no worker allocator mutation |
| Safety | zero faults/timeouts/overwrites; bounded drops only; linker/memory gates pass |
| Speed | dual render FRT ≥15% lower than serial; guest ≥10 median / ≥8 1% low |
| Reporting | guest, emulator-speed ratio, and perceived values all present |

The 15/12 project exit gate is a stretch close for this sprint, not a licence
to relabel 10/8. If 10/8 passes but 15/12 does not, the final report identifies
the largest measured remaining phase and proposes exactly one follow-on
sprint. If dual does not beat serial, the serial renderer remains default.

**Commit:** `evidence(saturn): close renderer pipeline sprint`

## 9. Screenshot and gallery protocol

Every M1–M4 capture must be fresh and paired:

- `docs/saturn/evidence/screenshots/<milestone>-YYYY-MM-DD.png`
- `docs/saturn/evidence/reports/<milestone>-YYYY-MM-DD.json`

The report includes:

- exact commit and dirty-state disclosure;
- ELF/CUE paths, hashes, and mtimes;
- freshly resolved profile address;
- capture flags including `--dram-cart`;
- build profile and all renderer feature flags;
- route/checkpoint/hash;
- frame hash and image dimensions;
- command/result high-water values;
- faults, drops, timeouts, and bank generation;
- guest FPS, emulator-speed ratio, and perceived FPS.

A diagnostic screenshot may be committed, but it must be labelled diagnostic.
Only the owner’s explicit visual acceptance authorizes a gallery card.

## 10. Rollback switches

Every structural change must retain a same-commit oracle:

| Feature | Required fallback |
| --- | --- |
| spatial traversal | current primitive visibility path |
| view-space terrain clip | strict reject / no recovery |
| slave result renderer | `SATURN_SLAVE_RENDER=0` |
| double bank | proven single 2,048-command bank |
| hot promotion | LWRAM-resident source arrays |
| LOD | current fixed 16×16 fragment tier |

Flags are diagnostic and rollback tools, not permanent combinations to
support indefinitely. After sprint acceptance, retain serial and the safest
visual fallback; remove combinations that no longer provide an oracle.

## 11. Reader check

A new implementer should be able to answer these without asking the owner:

1. **What lands first?** Baseline/provenance, then the bounded compact result.
2. **What code may be close-ported?** Only the pinned GPL ranges in section 4,
   into `src/port/saturn/gpl/`, with notices and provenance.
3. **What is the parallel model?** One same-frame terrain dispatch, disjoint
   compact results, one join, master-only VDP1 lowering.
4. **Why not frame-ahead?** It adds state/latency before the directly
   applicable upstream model has passed.
5. **How is near clipping safe?** Terrain only, in view space, bounded output,
   no fake arbitrary UVs, actor path unchanged, owner screenshot gate.
6. **How is opaque order reconciled with near-first overflow?** Admit
   near-to-far; emit accepted opaque work far-to-near with stable baked keys.
7. **How is a dual win proven?** Same commit/profile/checkpoint, absolute
   serial and dual FRT, one job/render, and ≥15% improvement.
8. **What happens if a structural experiment loses?** Its required fallback
   remains the default and the measured failure is recorded.
