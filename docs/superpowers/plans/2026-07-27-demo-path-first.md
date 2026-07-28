# Demo-Path-First Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax. This plan is also the working handoff: it carries
> the baselines, references, licences, footguns, and negative knowledge a
> fresh agent needs. Read it whole before starting any task.

**Goal:** Real SM64 simulation (authoritative, untouched) under a
castleviewer-class baked-IR renderer — **textured from day one** — at an
absolute frame rate a human can see. First visible milestone: **≥ 5 FPS,
textured BOB terrain + animated Mario on the frozen route.** Sprint exit gate
unchanged: **15 FPS median / 12 FPS 1% low.**

**Architecture:** `game_loop_one_iteration()` owns all game state. The
renderer consumes it: baked terrain/actor IR resident in work RAM, Q16
transform with the landed SH-2 kernels, VDP1 textured distorted sprites via
the proven castleviewer machinery — promoted into shared modules, not
copied. Degradation (view distance first, Croc-style) is a build/runtime
profile, not a fork.

**Tech stack:** C + SH-2 asm, pinned Yaul, host `unittest` (NOT pytest),
Python bake tools, Ymir headless evidence, frozen-route comparator.

**Owner directives this plan encodes:**
1. *Absolute FPS only.* Never report ratios.
2. *Textures are in scope from day one* — castleviewer and the head demo had
   them; the demo path must not regress to Gouraud-only.
3. *Croc feasibility anchor:* comparable view distances make this tractable.
4. *Do not waste castleviewer / Z-Treme / SlaveDriver / Jo Engine* — enforced
   structurally by the Standing Anti-Waste Rules below, not by good
   intentions.

---

## 0. Standing Anti-Waste Rules (hard requirements, audited)

These bind every task in this plan. The peer-audit loop verifies them per
task; a task that ignores them is not done regardless of what it renders.

**AW-1 — Harvest, never copy.** Any castleviewer logic the demo path needs is
**promoted into a shared module** consumed by castleviewer itself first
(proving no regression via its own captures), then by the bridge. Copy-pasted
fragments fork and drift; a linked module cannot. Task 1 exists solely to do
this.

**AW-2 — No new formats where a schema exists.** The bake output is the
existing **Saturn Mesh IR** (`docs/saturn/SATURN_MESH_IR.md`,
`tools/saturn/schemas/saturn-mesh-ir-v1.schema.json`,
`tools/saturn/saturn_mesh_ir.py`) — extended to v2 where needed, versioned.
Inventing a parallel IR is a plan violation requiring written owner-visible
justification. The same applies to the route/report schema
(`tools/saturn/routes/bob_parity_v1.json` — bump per its own
`extension_rule`), and the quad map keying (`(display_list, list_ordinal)`).

**AW-3 — Reference-consumption table, per task, in the implementing commit.**
Every task below carries a table of the exact upstream inputs (file, line
range, licence, reuse mode). The implementing commit must state what was
consumed, and **any from-scratch implementation despite a named reference
must state why** (architecture mismatch, licence, measured cost — per the
repository's reference-code-first rule). The auditor checks this row by row.

**AW-4 — Negative knowledge is binding.** Do not re-adopt:
- Jo Engine `jo_fixed_dot` (miscompiling `mac.l` constraints),
  `jo_matrix_mul` (accumulates fixed point into a `float`), `jo_fixed_pow`
  (returns x^(n+1)), all Jo trig, the whole `jo_3d_*` layer (wraps a
  proprietary SGL binary). Jo's DIVU sequence (`math.c:109-144`) is
  pattern-only and **must be split start/collect** — its as-written form
  stalls the full 39 cycles.
- `sm64-psx` code in any form (no licence → all rights reserved;
  behaviour-lessons only).
- Position-only vertex welding (unsafe: 278/980 Mario positions carry
  differing normals — hard edges). Attribute-exact welding only.
- Comparing `triangles_transformed` across captures as a rate (it is a
  mid-submit-phase-dependent snapshot). Use `frame_serial` differencing and
  the route comparator.

**AW-5 — The lesson-infrastructure keeps running.** The frozen route's sim
checkpoint (Mario position bits, action, `gGlobalTimer`), the Q16-vs-float
differential gate (`tools/saturn/fast3d_q16_diff_test.c`), the profile
decoder (`tools/saturn/fast3d_profile_decode.py`), and the disassembly gate
(`tools/saturn/verify_q16_sh2_disassembly.py`) all continue to run against
the demo path. Renderer counters that change meaning get a route-schema
version bump, not a comparator bypass.

**AW-7 — VDP2 owns flat pixels.** Every pixel VDP2 can draw is VDP1 fill and
CPU transform not spent. Standing rules: the **HUD never goes through VDP1**
— when SM64's HUD lands, it goes on an NBG plane above the sprite layer
(the debug text already proves the path); **screen fades use VDP2 color
offset**, never rendered geometry; sky is VDP2 (Task 3b). Implementing any
of these through VDP1 is a plan violation.

**AW-6 — Licence discipline.** GPL adoptions (Z-Treme, SlaveDriver) are
close-ported **only** into `src/port/saturn/gpl/` with notices and change
notes, recorded in `docs/saturn/PROVENANCE.md`. Jo Engine BSD file notice
retained verbatim (precedent: `src/port/saturn/gfx/saturn_q16_sh2.h`).
Tool-only GPL use does not touch the ROM's licence
(`THIRD_PARTY_LICENSES.md`).

---

## 1. Measured starting point (do not trust older figures)

| Measure | Value | Source |
| --- | ---: | --- |
| Sourceboot render rate | **0.6718 FPS** | paired long captures, MVP-cache evidence |
| Castleviewer render rate | **14.93 FPS**, ~1,741 cyc/primitive, textured, no SM64 tick | `SHIPPING_ENGINE_COMPARISON.md` |
| Shipping-engine budget | ~500–660 cyc/primitive | same study |
| Frozen route | `bob_parity_v1`, 600 ticks, checkpoint sha `f0687607…`, 2,068 transformed / 542 emitted | Task 0 evidence, `ac1382d` |
| **Sim tick cost** | **UNMEASURED** — this plan's Task 0 exists to fix that | — |
| BOB terrain | 1,101 triangle commands, 100% textured, 0 quad-merge eligible today | quad-map + texture spec measurements |
| Texture milestone 1 | 1,077/1,101 tris, two CLUT16 tile classes, **333,696 B** | `2026-07-26-vdp1-textures-design.md` |
| VDP1 VRAM free | **446,432 B** (`vdp1_vram_partitions_set(2048, 0, 1536, 0)`; texture partition currently **0** — widen at `sourceboot/main.c:240`, multiple of 8) | texture spec, measured |
| LWRAM free | ~576 KiB (1 MiB − 64 KiB cmdts − 384 KiB main pool) | segment decision doc |
| HWRAM free | ~126,940 B at `60cadcb`; **re-measure** — Task 1 kernels moved `___end` | `sh-elf-nm … ___end`; margin = `0x06100000 − ___end`; linker `ASSERT` floor 4,096 B is inviolable |
| Profile | 248 B at `b51ebff`; grows append-only; decoder tracks the header automatically | `fast3d_profile_decode.py` |
| Landed kernels | `saturn_q16_sh2.h` (Jo BSD), `gpl/slavedriver_projection.{h,sx}` (GPL), disassembly-gated | `843ecb6` |

Evidence anchors: `docs/saturn/evidence/reports/e2-sourceboot-bob-parity-v1-*.json`,
`…/e2-sourceboot-mvp-cache-ppf{12000,36000}-2026-07-27.json`,
`…/task1-q16-kernel-probe-2026-07-27.{md,json}`.

---

## 2. Master reference map (what exists; consume, don't reinvent)

### In-repo (AW-1 harvest sources and tools)

| Reference | Location | What it is |
| --- | --- | --- |
| DIVU-scheduled Q16 projection | `src/port/saturn/castleviewer/main.c:434-491` (`world_to_view_project`) | The proven fast transform. Harvest target #1 |
| Texture machinery | `castleviewer/main.c` texture paths + `saturn_texture_residency.h` | Tile atlas, CLUT16, per-primitive Gouraud, residency, cart→WRAM staging. Harvest target #2 |
| Castle bake pipeline | `tools/saturn/extract_castle_area.py`, `compile_castle_area.py`, `compile_castle_bsp.py`, `bake_castle_uv.py`, `vdp1_texture.py`, `export_rgba16_textures.py` | The offline pattern Task 2/3 re-points at BOB |
| Mesh IR + pairing | `tools/saturn/saturn_mesh_ir.py`, `quad_pairing.py`, schema v1 | AW-2's mandated format; exact blossom matching — do not reimplement |
| Mario actor IR + anim | `src/port/saturn/gfx/saturn_mario_actor_mesh.h`, `tools/saturn/extract_mario_actor.py` | Quad-optimized animated Mario, already renders; Task 4 bridges it to live state |
| Q16 kernels (landed) | `src/port/saturn/gfx/saturn_q16_sh2.h`, `src/port/saturn/gpl/slavedriver_projection.{h,sx}` | Use as-is; disassembly gate protects them |
| libmic3d | `third_party/libyaul/libmic3d/render.c`, `sort.c` (MIT) | Transform pools/buckets — selective close-port if it fits |
| Harness/verification | `fast3d_profile_decode.py`, `compare_route_reports.py`, `fast3d_q16_diff_test.c`, `verify_q16_sh2_disassembly.py`, `runtime_contract_test.c` | AW-5; all keep running |

### External (licence dictates mode; ledger in sprint plan §4, provenance in `PROVENANCE.md`)

| Repo (pin) | Licence | Mode | Take |
| --- | --- | --- | --- |
| `sonic-z-treme` (`cff7545`) | GPL-3.0 (owner decision: clean; no-sale clause is Sega-IP disclaimer, same as our Nintendo position) | close-port → `gpl/` | **LWRAM→HWRAM hot promotion** (`ZT_LOADING.c`), tri-state frustum culling, near-to-far traversal |
| `slavedriver-engine` (`a898659`) | GPL-3.0-or-later | close-port → `gpl/` | DIVU schedule (landed); dual-SH2 worker (`WALLS.C:1806-1950`) — later task |
| `joengine` (`556d081`) | MIT root + BSD file notices (Johannes Fetz) | direct copy w/ notice | `jo_fixed_mult` (landed). See AW-4 for the do-not-adopt list |
| `SCSP_poneSound` (`31782e4`) | MIT (ponut64) | close-port | Audio — out of scope here, cloned and pinned for the sprint's Task 10 |
| `sm64-psx` (`3073845`) | none → all rights reserved | behaviour only | Lessons already extracted; no code, ever |

---

## 3. Tasks

Dependency shape:

```text
Task 0 (sim timer)     Task 1 (harvest)     Task 2+3 (bake)     Task 3b (VDP2 sky)
      \                     |                    /                   |
       \----- Task 4 (Mario live-anim bridge) --/                    |
                       |                                             |
              Task 5 (renderer swap) --------------------------------+
                       |
              Task 5b (slave SH-2 split, A/B-gated)
                       |
              Task 6 (degradation profile)
                       |
              Task 7 (measure + user gate: FPS + utilization report)
```

Task 3b is independent of everything except the sky bake tooling — it can
land first and makes every subsequent screenshot look dramatically better.

Tasks 0, 1, and 2/3 are mutually independent — run in parallel with separate
agents; they share no files. Task 4 needs 1 and 2/3. Never run an emulator
capture concurrently with a build or another capture.

### Visual milestone evidence (mandatory)

Every runtime visual gate produces a **fresh screenshot and paired report**;
the screenshot is not inferred from counters or borrowed from an older build.
Save them as `docs/saturn/evidence/screenshots/<milestone>-YYYY-MM-DD.png`
and `docs/saturn/evidence/reports/<milestone>-YYYY-MM-DD.json`. The report
records the exact ELF/CUE paths, SHA-256 hashes and mtimes, capture flags
(including `--dram-cart` and the freshly resolved probe symbol), route and
checkpoint, frame hash, and image dimensions. `capture_hwtest.py`'s stale-CUE
preflight stays enabled; use `--allow-stale` only for a labelled diagnostic.

Present each image to the owner for visual confirmation. The owner’s eyes are
the acceptance gate: only after confirmation may `evidence/TIMELINE.md` and
the repository screenshot gallery, `evidence/index.html`, receive a gallery
entry. Failed or diagnostic frames remain labelled evidence and are not
promoted. Keep each gallery card paired with its same-basename report, and
record the owner-confirmation date. The first demo-path visual label is
`task5-textured-bob` (textured BOB terrain plus animated Mario on the frozen
route); Task 3b sky and Task 4 live-Mario captures use their own labels.

---

### Task 0: Sim-tick phase timer — the number that decides everything

Castleviewer's 14.93 FPS runs **no SM64 tick**, and the sim is
soft-float-heavy (`camera.c` 382 call sites, `behaviors/*` 1,218). If one
`game_loop_one_iteration()` costs most of a 33 ms budget, the demo path's
ceiling is sim-bound and mitigation opens *before* Task 5, not after.

**Files:** Modify `src/port/saturn/gfx/saturn_fast3d_frontend.h` (profile,
append-only), `src/port/saturn/sourceboot/main.c`. FRT pattern:
`src/port/saturn/hwtest/main.c:163-190`.

- [x] Append to `sm64_saturn_fast3d_profile_t` (END of struct, never
  reorder — recorded offsets everywhere): `uint32_t sim_frt_ticks_last`,
  `uint32_t sim_frt_ticks_accum`, `uint32_t sim_tick_count`,
  `uint32_t render_frt_ticks_last`. Note the FRT is 16-bit with a prescaler —
  handle wrap; document the tick→µs conversion in a comment beside the
  fields.
- [x] In `main.c`'s frame loop, bracket the sim call and the render/submit
  call with FRT reads. No behavior change; counters only.
- [x] Host: `verify-runtime-contracts` green (offset probe recompiles
  automatically); note the new `sizeof` for `--probe-count`.
- [x] Capture on the frozen route (fresh symbol! it moves), decode with
  `fast3d_profile_decode.py`.
- [x] **Report: sim ms/tick, render ms/frame, both absolute.** Add the two
  fields to the route schema per its `extension_rule` (version bump).

**Gate:** the sim-tick number exists in a committed capture. Decision matrix
recorded in the completion note: sim ≤ ~15 ms → proceed unchanged; 15–33 ms →
proceed, flag sim-side soft-float work as a parallel track; > 33 ms → owner
decision before Task 5 (the 30 Hz sim itself can't hold rate — options:
sim-rate decouple per sprint gate 1, or targeted sim float fixes from the
call-site map in `2026-07-26-soft-float-replacement-design.md`).

**Completed evidence:** `docs/saturn/evidence/reports/e2-sourceboot-task0-timing-preserved-2026-07-27.{json,md}`;
average simulation phase **8.93 ms/tick**, last render phase **15.08 ms**,
`fault_flags=0`. The timing fields are preserved across the frontend profile
reset so a frame-boundary capture cannot erase the last completed sample.

**References consumed (AW-3):** FRT harness `hwtest/main.c:163-190`
(in-repo); profile append pattern (Task 6 quad counters precedent).

---

### Task 1: Harvest castleviewer into shared modules (AW-1's centerpiece)

Castleviewer's `main.c` is a ~1,300-line harness wrapping proven pieces.
Extract the pieces; leave the harness. **Castleviewer must consume the
shared modules itself and render identically before the bridge ever links
them** — that is the no-regression proof and the anti-drift guarantee.

**Files:** Create `src/port/saturn/gfx/saturn_ir_transform.{c,h}` (the
`world_to_view_project` family + its DIVU scheduling), create
`src/port/saturn/gfx/saturn_ir_texture.{c,h}` (tile/CLUT upload, residency,
per-primitive binding), modify `src/port/saturn/castleviewer/main.c` (consume
the modules, delete the moved bodies), modify `castleviewer/Makefile`.

- [x] Inventory pass first: list every function/table moving, with line
  ranges, in the commit message. Anything *not* moved that Task 4/5 will need
  gets named now (so the bridge never "quickly copies" something).
- [x] Move `world_to_view_project` (+ helpers) into `saturn_ir_transform.c`
  **verbatim first** — no improvements in the move commit. Improvements are
  separate commits after the identity proof.
- [x] Move texture residency/upload/binding into `saturn_ir_texture.c`, same
  discipline. Keep `saturn_texture_residency.h`'s generation-stamp model —
  the segment-addressing decision requires it (address stability only while
  a cart slot is resident).
- [x] Castleviewer builds; its own capture reproduces the retained baseline
  (screenshot hash or documented benign delta with pixel-diff bbox, per the
  quad-merge precedent).
- [x] Host contract tests for the moved transform (the
  `fast3d_q16_diff_test.c` corpus pattern) so the shared module is pinned
  independently of either consumer.
- [x] Commit(s); update the inventory in `docs/saturn/ENGINE_PORT_ARCHITECTURE.md`'s
  layer table (gfx layer now lists the shared modules).

**Dual-CPU design constraints — bind NOW, at the harvest (owner directive:
slave underutilization is the project's downfall; do not build this layer
twice):**

- `saturn_ir_transform`'s public API is **job records**, not calls over
  shared state: an input batch (bank slice + snapshot of matrix/viewport/
  light state) in, an output region out. No globals read or written inside
  the transform. A function with this shape runs identically on either CPU;
  a function without it forces a Task 5b rewrite.
- **No shared mutable state between prospective master/slave work.** Output
  regions are per-job, disjoint, cache-line-aware (SH7604 is write-through,
  4 KB unified cache per CPU, one shared bus — data placement is where 2
  CPUs become 2×, or don't; SlaveDriver's work/result discipline is the
  model).
- Task 2's bank format must be **sliceable**: contiguous primitive runs with
  per-slice bounds, so a job is a range, not a traversal.
- [x] **Slave smoke gate, in this task:** run the harvested transform on the
  slave CPU over a fixture bank and compare outputs **bit-exact** against
  the master running the same job (Yaul dual-CPU API; behaviour reference
  `work/upstream/libyaul-examples/cpu-dual`, behaviour-only). This proves
  the module is CPU-agnostic *before* anything depends on it, and surfaces
  bus/cache surprises months earlier than Task 5b would.

**Gate:** castleviewer renders identically from the shared modules; the
modules have their own host tests; the slave smoke gate passes bit-exact;
nothing the bridge needs remains harness-private.

**References consumed (AW-3):** `castleviewer/main.c:434-491` and texture
paths (in-repo harvest, MIT-lineage per its libmic3d ancestry — retain any
notices found); `saturn_q16_sh2.h` / `gpl/slavedriver_projection` (landed
kernels the transform may now call — keep the disassembly gate passing).

---

### Task 2: Bake BOB terrain geometry into Mesh IR v2

Point the castle bake pattern at BOB. Output is Mesh IR (AW-2), extended to
v2 only for what BOB actually needs.

**Files:** Create `tools/saturn/extract_bob_area.py` (pattern:
`extract_castle_area.py`), modify `tools/saturn/saturn_mesh_ir.py` +
`schemas/` (v2: static world-space pre-transform flag, per-primitive texture
tile reference — coordinate with Task 3), create generated bank under
`build/saturn/sourceboot/generated/` via `Makefile.saturn.mk` (precedent:
quad map + `mario_anim_data` dependency wiring), modify
`tools/saturn/test_tools.py`.

- [x] Extract BOB's terrain display lists + vertices (the
  `dl_rigid_groups.py` walker already parses these files; reuse its
  parsing, not a new parser — note its known limitation: bracketed-expression
  macro regex, fix if hit).
- [x] Static world-space pre-transform: terrain is static; bake vertices to
  world space so runtime skips the model matrix entirely (castleviewer
  precedent).
- [x] Quad pairing via `quad_pairing.py` unchanged. Textured quads now pair
  under the texture spec's four conditions (identical tile state, existing
  convexity/normal gates, consistent UV cycle — free with attribute-exact
  weld — bounded affine error). Expected recovery: ~220–285 commands.
- [x] Residency: bank targets work RAM per the cartridge policy. Adopt
  Z-Treme's LWRAM→HWRAM hot promotion (`ZT_LOADING.c`) into
  `src/port/saturn/gpl/` with notices; budget against the measured 576 KiB
  LWRAM / re-measured HWRAM figures. **The frame loop never chases cart
  pointers** (`CARTRIDGE_ASSET_POLICY.md`).

**Progress note (2026-07-28):** the generated BOB positions/primitives now
 copy from cart-linked source data into `.lwram_bss` before the demo frame loop
 (`task2-lwram-residency-2026-07-28`). The frame renderer therefore consumes
 work-RAM arrays, and the texture bank is uploaded once to VDP1 VRAM. The
 Z-Treme-style optional LWRAM→HWRAM hot-promotion layer and its budget proof
 are now implemented and host-verified; runtime parity/performance of the
 optional hot variant remains a later capture gate.
- [x] Host tests: v2 schema round-trip, bank size budget assertion, unittest
  (NOT pytest — bare `test_*` functions silently never run).
- [x] Mutation-test the safety-relevant compiler logic (project standing
  rule; the quad-map precedent lists the mutation catalogue style).

**Gate:** BOB bank generates reproducibly as a build dependency, fits the
stated budgets, and its primitive count reconciles with the known 1,101
(± documented merges).

**References consumed (AW-3):** `extract_castle_area.py` /
`compile_castle_area.py` (in-repo pattern), `saturn_mesh_ir.py` schema v1
(extend), `quad_pairing.py` (unchanged), Z-Treme `ZT_LOADING.c`
(GPL close-port → `gpl/`), `dl_rigid_groups.py` parser (reuse).

---

### Task 3: Texture bake — milestone 1 of the committed texture spec

The design is **already written and audited**:
`docs/superpowers/specs/2026-07-26-vdp1-textures-design.md`. Implement its
milestone 1 against the IR path (it was drafted for the interpreted path;
the offline bake is identical — only the runtime binding differs, and that
now goes through Task 1's `saturn_ir_texture` module).

**Files:** Create `tools/saturn/bake_bob_tiles.py` (pattern:
`bake_castle_uv.py` + the spec's resampler), modify
`src/port/saturn/sourceboot/main.c:240` (VDP1 partition `texture_size` 0 →
sized, multiple of 8), generated tile bank + manifest, tests.

- [x] Per-primitive tile bake: resample source texture through each
  primitive's own UVs/tile state → the fixed rectangle VDP1 demands (the
  spec's core insight: the rectangle is one *we* choose). Two classes:
  16×16 CLUT16 (k ≤ 2), 32×32 (2 < k ≤ 16); the 24 anisotropic strips stay
  Gouraud, honestly.
- [x] Reuse castleviewer's proven pieces via the spec's transfer list: the
  RGB1555 box filter, transparent-code canonicalisation, `sample_raw` tile
  resolver, CLUT16 quantizer. Re-key from castleviewer's scene-global id to
  `(display_list, list_ordinal)` / IR primitive id.
- [x] Budget assertion: ≤ 333,696 B tiles against 446,432 B free (leave the
  Gouraud partition untouched at 1,536).
- [x] Manifest: which primitive binds which tile, versioned with the bank
  (stale manifest = loud failure, the quad-map sentinel philosophy: absent
  never means "textured").
- [x] Host tests + a golden-image style check if cheap (offline resample is
  deterministic — hash the tile bank).

**Gate (passed offline):** tile bank + manifest generate as build dependencies within budget;
1,077/1,101 coverage confirmed by count, with the 24 exceptions listed by id.

**References consumed (AW-3):** the texture spec (its measurements are the
contract), `vdp1_texture.py` / `export_rgba16_textures.py` /
`bake_castle_uv.py` (in-repo), castleviewer transfer list above (harvested in
Task 1).

---

### Task 3b: VDP2 skybox — the idle coprocessor's first job

**Measured current state:** VDP2 does a black back color and the NBG3 debug
text — nothing else. The skybox does not render at all (no `GEO_BACKGROUND`
path exists in the frontend); the ~47% black in every capture is the unused
VDP2. Shipping Saturn titles put sky (and often ground) on VDP2 planes —
zero CPU polygons, zero VDP1 fill. This is the cheapest large visual win in
the plan and it runs in parallel with everything (offline tooling + a
bounded `main.c` init change).

**Files:** Create `tools/saturn/bake_bob_sky.py` (source: BOB's skybox
tiles under the real asset tree via the existing texture export tooling),
modify `src/port/saturn/sourceboot/main.c` (NBG0 or NBG1 tilemap/bitmap
setup + scroll tied to camera yaw/pitch from sim state, read-only),
generated sky bank as a build dependency.

- [x] **First step, before any bake: per-line back-screen gradient.** VDP2's
  back screen accepts a per-line color table — a sky gradient for a few
  hundred bytes of VRAM and zero per-frame cost. The sourceboot now uploads
  224 deterministic RGB1555 entries at boot; capture remains the visual gate
  before this task is considered fully complete. If the textured sky slips,
  the gradient alone already retires the black void.
- [x] Bake BOB's sky to a VDP2-native format (tilemap preferred for VRAM;
  measure both against remaining VDP2 VRAM and state the budget — VDP2 has
  its own 512 KiB, essentially untouched today).
- [x] Wire NBG plane behind VDP1 sprites (priority below the 3-D layer,
  above back color). Scroll from camera yaw — the sim's camera, read-only,
  same authority rule as everything else.
- [ ] Route capture + screenshot: sky visible, `fault_flags` 0, frame rate
  unchanged or better (sky costs VDP2, not the frame budget).
- [x] Record VDP2 layer usage in the completion note (which planes are now
  live, VRAM spent).

**Gate:** sky visible in a committed route screenshot at no measured frame
cost; owner visual confirmation before any TIMELINE entry, as always.

**Progress note (2026-07-28, offline sky bake):** `bake_bob_sky.py` now
 decodes the checked-in 248x248 BOB water sky and emits a deterministic
512x256 RGB1555 NBG1 bitmap (262,144 bytes), with an auditable manifest and
 `compile-bob-sky` build target. The remaining open work is runtime NBG1
 wiring, camera scroll, and the capture/visual gate.

**Progress note (2026-07-28, NBG1 wiring):** sourceboot now links the baked
 sky in `.cart_rodata`, copies it once to VDP2 VRAM at boot, allocates NBG1's
 bitmap fetch cycles, and places NBG1 below the VDP1 sprite layer while
 preserving NBG3 diagnostics. The per-frame camera yaw/pitch scroll now comes
 from the copied actor snapshot; only the runtime visual gate remains open.

The current layer budget is NBG1 RGB1555 bitmap: 262,144 bytes in VDP2 VRAM;
the back-screen gradient uses 448 bytes; NBG3 remains the debug text plane.

**References consumed (AW-3):** existing texture export tooling
(`export_rgba16_textures.py`), Yaul VDP2 scroll-screen API (existing
dependency), shipping-engine precedent (`SHIPPING_ENGINE_COMPARISON.md` —
VDP2 offload noted for both GPL engines).

---

### Task 4: Mario — live animation into the actor IR

The least-proven seam in the plan; the correctness nets matter most here.

**Files:** Create `src/port/saturn/gfx/saturn_actor_bridge.{c,h}`, reuse
`gfx/saturn_mario_actor_mesh.h` (promoted from the turntable into a shared/
generated location rather than including across targets), modify bake
tooling if the mesh needs re-generation with texture tiles (Mario textures
are milestone-2 in the spec — **Gouraud Mario over textured terrain is
acceptable for the first visible build**, per the spec's staging; his
existing material colors already read well).

- [x] Bridge inputs, strictly read-only from sim state: `gMarioState`
  position/action, `gMarioObject` animation id + frame, camera
  (`gLakituState` / camera focus per the real engine), area index.
- [ ] Drive the turntable's existing joint/Q15-weight deformation from the
  *live* animation frame instead of a canned loop. The animation data is
  already resident (`mario_anim_data` generation precedent).
- [ ] Correctness nets: (a) route checkpoint hash must be **unchanged** —
  the bridge reads sim state, never writes it; any checkpoint drift is an
  instant fail; (b) a pose-differential fixture: for N sampled route ticks,
  compare bridge-computed joint matrices against the engine's own
  `geo_process_animated_part` results within stated tolerance (the
  host-differential pattern, `mtxq_ctor_diff_test.c` style).
- [ ] Visual sanity capture: Mario animating in place via the IR path
  (turntable-style scene is fine at this step).

**Gate:** checkpoint hash unchanged; pose differential within tolerance;
animated Mario renders via the shared modules.

**Progress note (2026-07-28):** the read-only snapshot/pose bridge is now
consumed by the demo renderer and emits a live flat-material Mario pass
(`1cc84b5`). A fresh intro-camera capture proves the bridge snapshot is valid,
but the actor is inside the current 128-unit near clip; it is retained as
diagnostic evidence, not as a visual-gate pass. The route checkpoint,
pose-differential fixture, and later-phase visual capture remain open.

**Progress note (2026-07-28, fresh-symbol capture):** the bridge now tolerates
 the interval where `gMarioState` exists before its graph object by selecting
 the neutral generated pose (`d13906f`). Frontend profile reset now preserves
 the bridge diagnostics (`b2e0724`). A paired probe at fresh symbols reports
 `demo_actor_snapshot_valid=1`, `demo_actor_pose_vertices=424`, and nonzero
 actor vertex/primitive counts at replay tick 25; the screenshot remains a
 terrain/ordering diagnostic and is not gallery-accepted. The pose
 differential and a clean identifiable Mario frame are still open.

**Progress note (2026-07-28, camera snapshot):** the read-only actor snapshot
 now also captures the authoritative `gLakituState` position, focus, and mode;
 demo terrain and Mario projection consume those copied camera values rather
 than reading camera globals from the renderer. This closes the bridge-input
 seam; pose deformation, checkpoint proof, and visual acceptance remain open.

**Progress note (2026-07-28, near-plane cull):** the shared transform now
 rejects vertices on/behind the near plane instead of projecting them with a
 clamped reciprocal. The fresh paired capture is the first identifiable Mario
 frame in the demo path (`task5-near-cull-2026-07-28`); actor counters and
 `fault_flags=0` confirm the live bridge. The terrain still has black/fragmented
 composition and the route sample is only tick 25, so Task 5's visual and
 checkpoint gates remain open.

**Progress note (2026-07-28, 600-tick authority gate):** the demo/replay
 capture now reaches the exact 600-tick endpoint
 (`task5-route-600-final-2026-07-28`) with zero renderer faults and capacity
 rejects. A same-commit interpreted replay oracle was captured and the paired
 comparator was extended to consume route data from `extra_probe_window`. The
 authority gate fails: demo `global_timer=602` vs interpreted `611`, the Mario
 checkpoint signature differs, and renderer counters differ (`3393/1118` vs
 `2074/531`). This is the current Task 5 blocker; no gallery promotion is
 justified.

**Progress note (2026-07-28, frozen endpoint authority pass):** replay input is
 held neutral until authoritative Mario state exists, and the route checkpoint
 freezes on the first complete replay publication. The v2 route schema and
 paired comparator now treat renderer counters as informational while keeping
 source state strict. Fresh demo/interpreted captures both reach tick 600 with
 identical checkpoint hash `d6f8bb72…`, zero faults, and zero capacity rejects;
 the renderer deltas are `+1323` transformed / `+615` VDP1 commands. The
 authority portion of Task 5 is therefore passed; visual owner confirmation,
 renderer-rate work, and the 5 FPS gate remain open.

**Progress note (2026-07-28, Task 6 radius sweep):** the build-profile radius
 is now structural (`SATURN_DEMO_VIEW_RADIUS`, variant object directories).
 Captures at 6,000/4,096/2,048 reduce render FRT work but leave frame cadence
 unchanged at 53 frames per 3,600 emulator frames; sim FRT remains 25,829.
 The sweep is therefore a measured non-lever for the current bottleneck, and
 the next performance track is simulation/dual-SH2 work rather than a claimed
 view-distance FPS improvement.

**Progress note (2026-07-28, Task 6 pacing correction):** sourceboot no longer
 blocks on the bring-up-only `vdp2_sync_wait()`/`vdp1_sync_wait()` pair after
 arming the sync state machines. The stock game loop already performs one raw
 VBlank-IN/OUT wait, and the backend retains its guarded pre-copy VDP1 wait;
 the removed pair was an independent second synchronization wait per loop.
 The demo image rebuilds successfully and the 168-test host suite remains
 green. A fresh visual capture is still required once the Ymir/BIOS artifacts
are available; no gallery promotion is made from this code-only result.

**Progress note (2026-07-28, Task 5b worker boundary):** the IR transform bank
 now has a Yaul polling-mode dual worker with disjoint master/slave ranges,
 cancellation checks, a serial `SATURN_SLAVE_RENDER=0` build, structural
 `-slave0`/`-slave1` output variants, and append-only utilization counters.
 Both demo variants and the interpreted route build successfully; the host
 suite remains green at 168 tests (one skipped). The runtime A/B gate,
 checkpoint identity, timeout-zero proof, and screenshot still require a
 Ymir/BIOS capture. No Task 5b performance claim or gallery promotion is made
 from build evidence alone.

**Progress note (2026-07-28, Task 6 tier metadata):** the demo build now has
 a structural `SATURN_DEMO_POLY_TIER` bank-selection boundary and `-polyN`
 output variant, with tiers 1/2 deliberately aliasing the current complete
 source bank until alternate baked assets exist. `capture_hwtest.py` now
 records the compiled view radius and poly tier in every report when supplied;
 it validates those settings so captures cannot silently omit their declared
 degradation profile. This is a hook and schema improvement, not an FPS or
 visual claim; the real-route settings sweep remains open.

**Progress note (2026-07-28, Task 2 optional hot promotion):** the existing
 Z-Treme-derived bounded promotion helper is now wired behind
 `SATURN_DEMO_HOT_PROMOTION`, with `-hot0`/`-hot1` output variants. The hot
 build copies the 19,500-byte position bank and 24,276-byte primitive bank
 once into HWRAM and the frame loop reads only the promoted pointers; the
 default remains the LWRAM-resident baseline. Cross-link measurements for the
hot build put the arrays at `0x060BEE00`/`0x060C4CE0` and `___end` at
`0x060FD5C0`, leaving 10,816 bytes before the `0x06100000` HWRAM ceiling;
both arena bases are 16-byte aligned.
 This proves the budgeted optional path; runtime parity and performance still
need the unavailable capture rig.

**Progress note (2026-07-28, Task 5 demo submit boundary):** demo-profile
 sourceboot now configures a null task consumer while retaining the public
 `exec_display_list` ABI symbol; the interpreted profile still registers the
 Fast3D frontend. This removes the previously duplicated interpreted render
 submission from every IR demo frame and restores the planned
 sim → bridge → IR frame ownership. Both profiles cross-build successfully;
 the resulting FPS and checkpoint comparison still require a runtime capture.

The promotion contract now has a standing host target (`verify-hot-promotion`)
 covering alignment, bounded capacity, copied bytes, and source immutability;
 it is included in `verify-all`.

**References consumed (AW-3):** `marioturntable` mesh + anim bridge
(in-repo), `extract_mario_actor.py`, Mesh IR deformation contract
(`SATURN_MESH_IR.md` — `linear_blend` / Q15 weights), Task 1 modules.

---

### Task 5: The renderer swap in sourceboot

**Files:** Modify `src/port/saturn/sourceboot/main.c` (+ a new
`saturn_demo_render.{c,h}` orchestrator), `sourceboot/Makefile`
(`SATURN_DEMO_PATH ?= 1` profile flag; the interpreted path remains
buildable at `=0` for differential runs — flag-variant hazard: toggling
flags does NOT rebuild stale objects; force rebuild or variant object dirs).

- [x] Frame loop: sim tick (unchanged) → bridge reads state → IR renderer
  draws terrain bank + Mario + cannon (cannon IR is trivial; include it —
  it has been the control object all project). Dynamic objects beyond that:
  **deferred, listed by name** in the completion note (coins, goombas, etc.)
  — deliberate coverage debt per the pivot handoff.
- [x] The interpreted frontend no longer runs per-frame in demo profile —
  but keep `exec_display_list` reachable so the differential build still
  works. Do not delete anything.
- [ ] Sim cadence per the sprint's frame contract: 30 Hz sim independent of
  render rate (Task 0's timer proves the budget).
- [x] Full regression: all host suites, cross-compile, `make verify`, both
  profile flags build.
- [ ] Route capture on the demo profile: **checkpoint hash must equal the
  interpreted build's** (same sim, same inputs — this is the whole
  authority guarantee). Route schema v-bump for the renderer counters.

**Gate:** demo profile boots the real game, renders textured BOB + animated
Mario + cannon via the IR path, checkpoint hash identical to interpreted
build, `fault_flags` 0.

**References consumed (AW-3):** everything above; `ENGINE_PORT_ARCHITECTURE.md`
ownership contract (the swap must satisfy its "renderer consumes, never
owns" rule verbatim).

---

### Task 5b: Slave SH-2 transform split — measured, not aspirational

**Measured current state: zero references to the slave CPU anywhere in the
port.** This was structural, not neglect — sequential display-list
interpretation with shared frontend state has no independent work to hand a
second CPU. The IR path changes that: baked banks are independent job
batches, which is exactly how SlaveDriver and Z-Treme kept both CPUs busy.
**Task 1's shared `saturn_ir_transform` module is the prerequisite** — the
split happens at that module's boundary, in one place, for every consumer.

**Files:** Create `src/port/saturn/gpl/slavedriver_dual_worker.{c,h}`
(close-port of `WALLS.C:1806-1950` incl. the spin-count auto-balancer; GPL
notices + `PROVENANCE.md`), modify `src/port/saturn/gfx/saturn_ir_transform.c`
(job partitioning), `saturn_demo_render.c` (dispatch/join), profile
(append-only): `uint32_t slave_jobs_completed`, `uint32_t slave_busy_ticks`,
`uint32_t master_wait_ticks`, `uint32_t slave_timeouts`.

- [ ] **Choose the parallel model from Task 0's numbers — both are in
  scope; the data decides which is primary:**
  - **(A) Frame pipeline:** master sims frame N+1 while the slave renders
    frame N from an **immutable snapshot** (Mario pos/action/anim, camera,
    copied at the frame boundary — the slave never reads live sim state,
    which preserves the authority contract by construction). After its sim
    tick, the master **joins the render** and takes remaining jobs. Ceiling:
    `(sim + render) / 2` per frame. Adds one frame of render latency —
    acceptable, state it.
  - **(B) Per-frame data split:** both CPUs transform the same frame's bank
    slices, join before linking. Ceiling: `sim + render/2`. No added
    latency.
  - If sim and render are comparable, (A) with master-join dominates; if sim
    is small, they converge. Record the decision and its arithmetic in the
    completion note.
- [x] Close-port the worker loop: bounded jobs from Task 2's sliceable
  banks; slave transforms/lights/culls into per-job disjoint output regions
  (no shared writes — SlaveDriver's work/result discipline); master joins
  and links commands. Cache coherency per the write-through SH7604 reality
  (`saturn_vdp1_backend.h:200-203` states it correctly). Task 1's slave
  smoke gate means this task is **wiring and scheduling, not restructuring**
  — if it turns into restructuring, Task 1's constraints were violated and
  that is the bug to fix.
- [x] The auto-balancer adjusts the split from measured spin counts, exactly
  as upstream does.
- [x] **Serial fallback flag** (`SATURN_SLAVE_RENDER ?= 1`, `=0` builds the
  identical serial path) — required for the A/B gate and by the sprint's
  own exit-gate wording. Flag-variant stale-object hazard applies: force
  rebuild on toggle.
- [ ] `slave_timeouts` must be 0 on the route; a hung slave degrades to
  serial with a counted fault, never a wedge.
- [ ] A/B on the frozen route: slave build vs serial build, absolute FPS
  both, checkpoint hash identical in both (the slave touches render data
  only, never sim state).

**Gate:** checkpoint hash unchanged; **measured absolute FPS improvement
over the serial build of the same commit** (the sprint's gate 8, inherited
verbatim); and a **slave-share target: slave busy ≥ 50% of frame time** at
the chosen degradation setting, reported as a percentage in the utilization
table. `> 0` is not the bar — the owner's directive is *proper* use, and 50%
is what the frame-pipeline arithmetic predicts when the model fits. If the
measured share lands below target, the completion note states the limiter
(bus contention, join stalls, job granularity) with numbers — that analysis
is the deliverable, not a relabeled gate. If the split doesn't pay at all on
this workload, the honest number is the deliverable and the serial flag
stays default.

**Progress note (2026-07-28, worker implementation):** the bounded
 SlaveDriver-derived worker, disjoint transform ranges, cancellation timeout,
 monotonic spin-count balancer, and `SATURN_SLAVE_RENDER=0` serial path are
 implemented and cross-compiled. Route timeout, checkpoint parity, utilization,
 and FPS A/B remain runtime gates.

**References consumed (AW-3):** SlaveDriver `WALLS.C:1806-1950` (GPL-3.0+,
close-port → `gpl/`), in-repo `gpl/slavedriver_dma_queue.*` precedent for
notice/isolation format, `work/upstream/libyaul-examples/cpu-dual`
(behaviour-only — that checkout lacks a root licence), Z-Treme rendering
split (behaviour lessons, `SHIPPING_ENGINE_COMPARISON.md`).

---

### Task 6: Degradation profile — Croc's levers, measured

**Files:** `saturn_demo_render.c` (view-distance clamp, per-bank LOD hooks),
route/report schema (degradation settings recorded in every report so no
capture is ever compared across unstated settings).

The route comparator now rejects `null`/omitted view-radius or poly-tier
metadata, so an unprofiled capture cannot silently enter a parity comparison.

- [x] View distance: single clamp on baked-bank spatial groups (the bake
  emits bounds — Mesh IR v2 already carries them from Task 2). Near-to-far
  traversal per the Z-Treme pattern.
- [ ] **RBG0 horizon-mask spike** (timeboxed; success optional, measurement
  mandatory): a perspective-correct VDP2 rotation plane at the horizon,
  rotation parameters computed from the sim camera each frame, filling the
  world beyond the draw-distance clamp — so cut geometry reads as art
  direction, not pop-out against void. This is the Saturn-native equalizer
  for BOB's open sightlines versus Croc's enclosed rooms (Panzer Dragoon /
  Sonic R precedent). Honest costs, stated up front: the camera→rotation-
  parameter math is real work; RBG0 consumes VDP2 VRAM access slots that
  constrain other layers; the polygon/plane seam over hilly terrain needs
  care. If the spike misses its box, commit the findings and fall back to
  the gradient horizon — the view-distance clamp still works, it just looks
  cheaper.
- [ ] **Fog-band spike via sprite color calculation** (pairs with the
  horizon mask; only if it survives): bucket far geometry into 2-3 sprite
  priority groups with VDP2-side color-calc blending toward the sky color.
  Coarse depth cueing at zero VDP1 fill cost — VDP1 half-transparency
  halves fill rate and stays banned for this purpose.
- [x] Poly degradation hooks: bank-level tier selection (the texture spec's
  near/mid/far variants slot here later; a stub tier switch is enough now).
- [x] **Do not** implement viewport shrinking — fill is ~6% of the frame;
  measured non-lever. Recorded here so nobody re-tries it.
- [ ] Sweep 2–3 view-distance settings on the frozen route; report absolute
  FPS for each.

**Gate:** a settings table — view distance vs absolute FPS — from real route
captures.

---

### Task 7: Measure, present, gate

- [x] Full regression, both profiles (and both slave flags). Host `verify-all` passes; interpreted and demo-path sourceboot variants cross-build with slave render disabled/enabled. Runtime route capture and FPS/utilization evidence remain open below.
- [ ] Frozen-route capture at the chosen degradation setting: **absolute FPS
  (median and 1% low if the phase timer supports it), never ratios.**
- [ ] **Hardware-utilization report alongside FPS**: sim vs render ms (Task
  0 timers), slave share (`slave_busy_ticks` vs master, Task 5b), VDP2
  layers live and VRAM spent (Task 3b), VDP1 vs VDP2 division of the frame.
  Both processors and both VDPs are gated deliverables of this plan, not
  aspirations — a report without these numbers is incomplete.
- [ ] Screenshot evidence committed; **the owner's eyes are the acceptance
  gate** — no `TIMELINE.md` or gallery entry before their confirmation.
  Describe frames factually; never assert what an object is (twice-burned
  rule).
- [ ] Honest verdict against the milestones: ≥ 5 FPS visible milestone;
  15/12 sprint gate standing. If short: the numbers, the limiter, the next
  lever — no relabeling.

---

## 4. Shared verification commands

Host suites (from repo root, Git-Bash):
```bash
export PATH="/c/msys64/usr/bin:/c/msys64/mingw64/bin:$PATH"
for t in verify-tools verify-runtime-contracts verify-mtxq-ctors verify-mtxf-lookat-host-diff; do
  make -f Makefile.saturn.mk OS=Windows_NT $t SATURN_TOOLS_PYTHON=$PWD/.venv-saturn-tools/Scripts/python.exe >/dev/null 2>&1
  echo "$t EXIT: $?"
done
```

Cross-compile (msys64 starts at `/home/estee`; env does not survive the
boundary — `cd` and `source` in ONE invocation):
```bash
/c/msys64/usr/bin/bash.exe -lc "cd /d/Code/RetroDev/sm64-saturn-port/sm64-port; source .yaul.env; cd src/port/saturn/sourceboot; make -j2 && make verify"
```

Capture (each ~15–20 min; ONE foreground command, tool timeout ≥ 1700 s;
never concurrent with a build or another capture):
```bash
./.venv-saturn-tools/Scripts/python.exe tools/saturn/capture_hwtest.py \
  --ymir "D:/Code/RetroDev/sm64-saturn-port/ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe" \
  --ipl "C:/Users/estee/AppData/Local/Temp/Sega Saturn BIOS (USA).bin" \
  --game "build/saturn/sourceboot/e2-bob/sm64-saturn-sourceboot-e2.cue" \
  --dram-cart --bios-input --frames 240 --handoff-yield \
  --post-poke-frames <N> --probe-address <FRESH> --probe-count <sizeof(profile)> \
  --allow-invalid --timeout 1500 --output <dest>.json
```

Footguns (every one has burned this project at least once):
- Resolve `_sourceboot_fast3d` **fresh every build**; it moves.
- **CUE mtime must be newer than the ELF's** before capturing (stale-CUE
  trap, twice). Adding this check to `capture_hwtest.py` is a welcome
  drive-by fix.
- `--dram-cart` is required; `--probe-count` is a **byte** count.
- Rate = `frame_serial` differencing between two ppf values (12000→36000
  span; 36,000 is a hard cap). A complete frame needs
  `triangles_vdp1_emitted > 0`.
- `pytest` is not installed; bare `test_*` functions never run.
- `git commit` takes the whole index — check `git status` first on this
  shared branch.
- The HWRAM linker `ASSERT` (≥ 4,096 B) is inviolable; if it fires, stop and
  report — never weaken it or shrink an unrelated buffer.
- Do not background captures behind polling monitors; use plain commands
  with the harness's own completion notification.

---

## 5. Out of scope (deliberate, revisit after Task 7)

68000/SCSP audio (poneSound cloned and pinned, awaits `m68keb-elf`
toolchain); dynamic objects beyond the cannon; Mario texture tiles
(milestone 2); VDP2 rotation-plane ground (RBG0 — a real candidate for
flat-floor areas per the Sonic-R-style precedent, but BOB's hilly terrain
makes it a poor first target; revisit for interior/flat areas); second-area
load proof (sprint gate 13 — stands, later); retail-hardware validation
(emulator evidence only, as always).

Note what is *no longer* out of scope: slave SH-2 rendering (Task 5b) and
VDP2 sky (Task 3b) were deferred in earlier drafts; the owner's directive
that both processors and both VDPs be properly used pulled them into this
plan as measured, gated tasks.

## 6. Self-review (performed at write time)

- Owner directives all encoded: absolute FPS (Tasks 0/6/7 wording), textures
  day-one (Task 3 is not optional or last), Croc anchor (Task 6), anti-waste
  (§0 + per-task AW-3 tables).
- Every named reference carries location + licence + mode; negative
  knowledge carried (AW-4).
- No new formats introduced: Mesh IR v2 extension, route schema v-bump,
  existing generated-bank Makefile pattern.
- Placeholder scan: no TBDs; deferred items are named and gated, not vague.
- Type/contract consistency: `(display_list, list_ordinal)` keying, Q15
  weights, generation stamps, and checkpoint fields match their defining
  documents.
- Known tension stated honestly: Task 0 may reveal a sim-bound ceiling; the
  decision matrix routes that to the owner instead of burying it.
