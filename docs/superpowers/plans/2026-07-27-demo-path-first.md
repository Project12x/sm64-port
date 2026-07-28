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
- [x] Route capture + screenshot: sky visible, `fault_flags` 0, frame rate
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

**Progress note (2026-07-28, post-cart sky upload):** a fresh dual route
capture now shows the baked NBG1 sky bitmap behind the BOB terrain; the prior
black field was traced to copying `.cart_rodata` during `user_init()` before
`source_cart_load()` populated the DRAM cart. The copy is now deferred until
after cart load. The paired report records `frame_serial=802`,
`fault_flags=0`, `slave_timeouts=0`, and `render_frt_ticks_accum=47,972,047`
(17.8075 ms/frame, effectively unchanged from the 17.8073 ms/frame optimized
dual baseline). Screenshot:
`evidence/screenshots/task3b-bob-sky-fixed-2026-07-28.png`. This is runtime
evidence; owner visual confirmation is still required before gallery promotion.

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
- [x] Drive the turntable's existing joint/Q15-weight deformation from the
  *live* animation frame instead of a canned loop. The animation data is
  already resident (`mario_anim_data` generation precedent). The shared
  bridge selects `gMarioObject->header.gfx.animInfo.animFrame` and indexes the
  source-evaluated pose bank; no turntable-owned frame counter remains in the
  sourceboot path.
- [x] Correctness nets: (a) route checkpoint hash must be **unchanged** —
  the bridge reads sim state, never writes it; any checkpoint drift is an
  instant fail; (b) a pose-differential fixture: the host
  `MarioActorPoseTests.test_promoted_pose_bank_reproduces_source_animation_evaluator`
  regenerates the full C5 and walking pose banks through the source
  GeoLayout/Animation evaluator and requires byte-identical output, while the
  frozen-route comparator requires identical checkpoint SHA-256.
- [x] Visual sanity capture: Mario animating in place via the IR path
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
 differential is now covered by the full source-evaluator regeneration fixture;
 only a clean identifiable Mario animation frame remains open for the visual
 gate.

**Progress note (2026-07-28, camera snapshot):** the read-only actor snapshot
 now also captures the authoritative `gLakituState` position, focus, and mode;
 demo terrain and Mario projection consume those copied camera values rather
 than reading camera globals from the renderer. This closes the bridge-input
 seam; pose deformation, checkpoint proof, and visual acceptance remain open.

**Progress note (2026-07-28, fresh sky-fixed visual capture):** the current
`e2-bob-demo-replay-r2048-slave1-poly0-hot0` image was captured with the
established BIOS handoff-yield sequence, `--dram-cart`, and fresh symbols
(`_sourceboot_fast3d=0x060D11E0`, route probe `0x060D1158`). The paired frame
shows identifiable Mario over the baked BOB sky/terrain; profile decode reports
`frame_serial=802`, `demo_actor_snapshot_valid=1`, `demo_actor_pose_vertices=424`,
`fault_flags=0`, and `vdp2_display_mask=10`. Evidence is retained at
`task4-live-mario-sky-fixed-2026-07-28.{png,json,md}`. Owner visual confirmation
is still required before gallery promotion.

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

**Progress note (2026-07-28, actor Gouraud milestone):** the dual-worker Mario
 pass now allocates one master-owned Gouraud table per visible primitive and
 expands the shared bridge's source-derived compact per-vertex light
 intensities through VDP1's Gouraud combiner, matching Castleviewer. Flat RGB1555
 remains the fallback when the table bank is exhausted or the worker falls back.
 The image builds cleanly and is running in foreground Ymir; owner visual
 confirmation and a saved milestone screenshot remain open. Castleviewer also
 establishes the next texture lesson: textured Mario primitives are lowered as
 an adjacent opaque Gouraud material command plus alpha-keyed texture tile
 overlays, preserving source-primitive ordering.

**Progress note (2026-07-28, Mario texture lowering):** sourceboot now treats
 the ROM-derived `bake_mario_eye_uv.py` output as an explicit generated build
 input, reserves and uploads its 16x16 RGB1555 tile bank after BOB, and reserves
 a second VDP1 command for each textured Mario primitive. The worker emits the
 existing Gouraud material command followed by the alpha-keyed RGB1555 detail
 tile, preserving primitive order and retaining flat/Gouraud fallback for
 untextured primitives. The fresh dual image builds and boots in foreground
 Ymir; visual texture acceptance and route evidence remain open.

**Progress note (2026-07-28, Mario painter ordering):** visible Mario
 primitives are now stably sorted back-to-front by projected depth before
 command reservation. Each material/detail texture pair remains adjacent, so
 front-facing detail cannot leap ahead of the actor's farther surfaces merely
 because source primitive order differs from painter order. Terrain ordering is
 intentionally not claimed fixed by this change; it remains a separate BOB
 painter/flicker investigation.

**Progress note (2026-07-28, BOB painter ordering experiment):** the dual
 emitter now consumes a flattened, stable far-to-near primitive order instead
 of iterating source primitive indices and silently bypassing its depth buckets.
 Each bucket sorts by four-corner average view depth while preserving source
 order for equal-depth coplanar surfaces. The image cross-builds and is running
 in foreground Ymir; terrain visual acceptance and route parity remain open.

**Progress note (2026-07-28, reference-aligned terrain depth key):** the BOB
 painter now keys each primitive by its farthest projected corner, matching the
 castleviewer render queue and the SlaveDriver-style unsplit fallback. This
 keeps a large quad behind a neighboring surface until its far extent has been
 painted; equal-depth source order remains stable. Proper baked BSP traversal
 is still the correctness target, not claimed solved by this fallback.

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
- [x] Sim cadence per the sprint's frame contract: 30 Hz sim independent of
  render rate (Task 0's timer proves the budget).
- [x] Full regression: all host suites, cross-compile, `make verify`, both
  profile flags build.
- [x] Route capture on the demo profile: **checkpoint hash equals the
  interpreted build's** (same sim, same inputs — this is the whole authority
  guarantee). The frozen-endpoint v2 comparator reports identical SHA-256
  `d6f8bb725b0e094b5f659dc81cfedb6b2405b850ab9bea2904fc283781c8861c`, zero
  faults, and zero capacity rejects at 600 ticks; renderer counters remain
  informational. Route schema v2 is recorded in the paired report.

**Gate:** demo profile boots the real game, renders textured BOB + animated
Mario + cannon via the IR path, checkpoint hash identical to interpreted
build, `fault_flags` 0.

**Progress note (2026-07-28, cadence scheduler):** the demo loop now owns a
VBlank-credit scheduler with bounded catch-up. A fresh 3,600-frame capture
records `sim_tick_count=996` alongside `frame_serial=249`, with zero faults,
zero slave timeouts, and valid Mario pose data. The paired frozen-route
comparator remains deterministic (`replay_ticks=600`, `global_timer=601`,
checkpoint hash unchanged). The capture report retains both clocks because
Ymir's capture-frame count is not the source VBlank count.

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

- [x] **Choose the parallel model from Task 0's numbers — both are in
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
    completion note. **Decision:** model A (frame pipeline) is primary for
    the measured baseline: 8.93 ms sim + 15.08 ms render gives an idealized
    `(8.93 + 15.08) / 2 = 12.01 ms` frame ceiling, versus model B's
    `8.93 + 15.08 / 2 = 16.47 ms`; the one-frame render latency is accepted
    for the performance experiment. Runtime timeout/share and A/B parity
    gates remain open.
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
- [x] `slave_timeouts` are 0 on the route; a hung slave degrades to
  serial with a counted fault, never a wedge.
- [x] A/B on the frozen route: slave build vs serial build, absolute FPS
  both, checkpoint hash identical in both (the slave touches render data
  only, never sim state). The paired 600-tick captures have identical
  checkpoint SHA-256; frame-serial differencing over the same 3,360 emulator
  VBlank sequence span gives serial **8.304 FPS** (465 / 3,360 × 60) and
  dual **8.482 FPS** (475 / 3,360 × 60), a measured +0.178 FPS (+2.1%).

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

**Progress note (2026-07-28, fresh serial baseline):** the rebuilt
`-slave0`/`-r2048` image was captured with a fresh ELF/CUE pair and
`--dram-cart`. The 3,600-emulator-frame window reached `frame_serial=317`,
`sim_tick_count=317`, `slave_busy_ticks=0`, `slave_timeouts=0`, and
`fault_flags=0`; the paired screenshot shows identifiable Mario and textured
BOB terrain. This is a serial reference artifact, not yet the frozen-route
A/B/FPS gate or an owner-confirmed gallery milestone.

The matched fresh `-slave1` capture reached `frame_serial=326` with
`slave_jobs_completed=326`, `slave_timeouts=0`, and `fault_flags=0` over the
same 3,600-emulator-frame window (`-slave0` reached 317). This is retained as
an A/B diagnostic signal only: it is not an absolute-FPS result, did not reach
the 600-tick endpoint, and has no paired checkpoint hash yet.

**Progress note (2026-07-28, frozen 600-tick A/B):** paired fresh serial and
dual captures at the same `-r2048/-poly0` profile both reach
`replay_ticks=600`, `global_timer=601`, `fault_flags=0`, and
`command_capacity_rejects=0`. The route comparator reports deterministic
parity with identical checkpoint SHA-256
`d6f8bb725b0e094b5f659dc81cfedb6b2405b850ab9bea2904fc283781c8861c` and
zero renderer-counter deltas. This passes authority/timeout parity and the
absolute-rate A/B checkbox; the ≥50% slave-share utilization acceptance bar
remains open.

The same reports supply the absolute-rate calculation for the A/B checkbox:
serial `frame_serial` 317→782 and dual 326→801 across screenshot sequence
5340→8700. This is emulator-derived FPS evidence, not a retail-hardware
claim; the remaining gate is the utilization table and its ≥50% slave-share
target.

**Progress note (2026-07-28, Task 5b utilization report):** the profile now
publishes cumulative render FRT ticks plus VDP1/VDP2 residency metadata, so the
share denominator is measured rather than inferred from one frame. Fresh paired
captures at `-r2048/-poly0` report dual render **18.0743 ms/frame**, slave busy
**10,308,448 FRT ticks**, and **21.1977%** slave share against the cumulative
render interval (801 jobs, zero timeouts/faults); serial is **2.5638 ms/frame**
with zero slave work. This fails the owner's ≥50% target. The measured limiter
is the renderer's master-side transform/shared-bus/emission work, not a join
stall alone: cumulative master wait is only **22.7467 ms**. The next worker
pass must move or batch more measured render work before this gate can close.

**Progress note (2026-07-28, primitive classification split):** the dual
worker now also classifies the 867 BOB primitives into visibility/depth buckets
over disjoint ranges, preserving the master's source-order scatter and command
ownership. A fresh capture records `slave_jobs_completed=496`,
`slave_busy_ticks=3,395,680`, and `render_frt_ticks_accum=15,029,161` —
22.6% slave share, up from the prior ~20% baseline but still below the ≥50%
bar. The paired route comparator remains deterministic with the same
checkpoint hash and zero faults/rejects. Command emission and texture/VRAM
setup remain the next measured master-side lever.

**Progress note (2026-07-28, direct command emission split):** BOB command
slots are now preassigned in source draw order and encoded by disjoint
master/slave ranges. Textured CLUT setup moved onto the worker; untextured
worker slots use flat RGB1555 while Gouraud allocation remains master-owned.
The fresh capture reaches the same 600-tick checkpoint with zero faults and
records `slave_jobs_completed=744`, `slave_busy_ticks=3,754,302`, and
`render_frt_ticks_accum=15,468,012` — 24.3% slave share. This is below the
≥50% bar; the remaining gap is the master-owned Gouraud/Mario path and final
VDP submission boundary.

**Progress note (2026-07-28, safe Mario command split):** the proven Mario
transform/visibility loop now feeds a bounded, pre-reserved command range to
the dual worker; the existing master emission loop remains the failure
fallback. A fresh dual capture retains the Mario primitive count (136,428),
reaches the same route checkpoint with zero faults/timeouts/rejects, and
records `slave_jobs_completed=992`, `slave_busy_ticks=4,646,864`,
`master_wait_ticks=31,871`, and `render_frt_ticks_accum=14,868,977`.
The screenshot/report pair is retained as evidence; owner visual acceptance
and the ≥50% slave-share gate remain open.

**Progress note (2026-07-28, VDP1 upload split):** the LWRAM command-table
copy now uses disjoint master/slave word ranges before the existing master
`vdp1_sync_force_put`; a failed dispatch falls back to the complete serial
copy. The fresh dual capture retains the route checkpoint and visual actor,
records `slave_jobs_completed=1,245`, `slave_busy_ticks=5,952,214`,
`master_wait_ticks=33,504`, and `render_frt_ticks_accum=14,677,538`, with
zero timeouts/faults. This is evidence for the final-submission lever, not a
closure of the ≥50% utilization gate or owner gallery acceptance.

**Progress note (2026-07-28, upload-split A/B):** a same-commit serial
`SATURN_SLAVE_RENDER=0` capture reaches the identical frozen-route checkpoint
with zero faults/rejects. The dual/serial retained windows report 249/246
rendered frames and approximately 58,946/7,527 FRT ticks per rendered frame;
the figures are parity/utilization evidence only, not a new 15 FPS claim. The
≥50% share gate and owner visual acceptance remain open.

**Progress note (2026-07-28, manual free-roam):** a live-input dual build at
`-r6000` now boots with DRAM configured and exposes Mario moving through the
BOB bank. The same scene runs with slave rendering disabled, so the observed
terrain flicker is not attributable to dual ownership; the r6000 diagnostic
also reports zero command-capacity rejects, degenerate rejects, faults, and
timeouts. The baked sky is forced behind VDP1, and the experimental dual VDP1
upload is now opt-in. Flicker is explicitly deferred for a later projection /
single-buffer investigation. The current actor pass remains flat RGB1555 by
design; textured/Gouraud Mario is the next fidelity lever.

**Next-lever note (2026-07-28, dispatch-cost diagnosis):** the upload-split
 dual profile records five bounded worker dispatches per rendered frame
 (transform, classify, emit, Mario, and VDP1 upload). The slave performs useful
 work, but the dual render interval remains approximately 7.8x the serial
 interval in the paired emulator window. The next Task 5b experiment must
 batch dependent work or adopt the accepted frame-pipeline snapshot model;
 adding another independent worker call is not evidence-based. Until that
 experiment has a visual capture and route comparator, the ≥50% share gate
 remains open.

**Progress note (2026-07-28, batched transform capture):** the Task 5b
transform-once path now submits terrain and Mario transform ranges through the
same dual-worker job. A fresh 3,600-frame Ymir capture at `-r6004/-poly0`,
`SATURN_SLAVE_RENDER=1`, and `SATURN_DEMO_NEAR_CLIP=0` completed with
`frame_serial=69`, `sim_tick_count=280`, zero faults, zero slave timeouts, and
`slave_jobs_completed=279`. Mario is present in the saved screenshot, but the
terrain remains visibly warped/overdrawn; this is diagnostic evidence, not
owner visual acceptance and not a 15 FPS gate measurement. The next correctness
lever remains camera-dependent spatial ordering (the existing static-BSP
compiler is the reference-backed source path; runtime integration is still
pending).

**Progress note (2026-07-28, BOB BSP cost pass):** the existing exact-rational
`static_bsp` compiler is now exercised against the actual compiled BOB Mesh IR
v2 bank through `compile_bob_bsp.py` and the normal sourceboot Makefile. The
deterministic result is 867 input polygons → 1,425 convex fragments, 560 split
events, 1,183 nodes, and maximum depth 33. This is the first concrete sizing
evidence for the reference-backed runtime stream. It remains report-only until
the generated scene header carries split fragments, UVs, and node ranges; the
runtime must traverse those ranges from the camera position rather than use the
origin-order digest.

**Progress note (2026-07-28, bounded BSP runtime pass):** the generated BOB
node/reference stream now traverses from the live camera position in sourceboot
(`SATURN_DEMO_BSP_ORDER=1`). Exact host planes remain the deterministic digest;
the generated runtime header quantizes each plane to a bounded 20-bit normal
envelope so SH-2 sign products do not overflow. The fresh BSP-order capture
completed with `frame_serial=69`, `sim_tick_count=276`, zero faults, and zero
slave timeouts. Its screenshot shows Mario and a different terrain ordering,
but still has severe warped-sheet geometry. This is an ordering milestone, not
visual acceptance: source primitives are currently deduplicated through the BSP
references, while split fragments/UV lowering remain the next correctness step.

**Progress note (2026-07-28, textured companion alignment):** sourceboot's
textured triangle emission now uses the generated affine companion as the
fourth VDP1 distorted-sprite corner, matching castleviewer; flat triangles keep
the repeated-C polygon. Direct and dual-worker emission share the rule. The
fresh cross-build/capture completed cleanly, but the screenshot is materially
unchanged, so this closes a proven contract mismatch without claiming it fixed
the warped terrain. Split-fragment texture baking remains the active lever.

**Progress note (2026-07-28, split-texture budget audit):** applying the
existing BOB tile manifest to the 1,425 BSP fragments and triangulating convex
fragments for VDP1 yields 2,108 commands: 876 16×16, 1,165 32×32, and 67 flat.
That is 773,920 bytes of texture/CLUT versus the 446,432-byte VDP1 resident
budget. Full-resolution per-fragment UV baking therefore cannot be enabled
blindly. An all-16×16 textured fragment tier is estimated at 326,560 bytes;
flat fragments retain their RGB path. The next
implementation must bake that tier and verify its UV error before wiring split
fragments into sourceboot.

**Progress note (2026-07-28, 16×16 BSP fragment bake):**
`bake_bob_bsp_fragments.py` now samples interpolated split UVs, fan-triangulates
convex fragments, canonicalizes transparent RGB, and emits a deterministic
bank/CLUT/scene artifact through `compile-bob-bsp-fragments`. The result is
2,108 fragment commands, 2,041 textured tiles. The verified artifact is
261,248 bytes of texture plus 65,312 bytes of CLUT = 326,560 bytes,
under the 446,432-byte resident gate. It is still host-side; runtime adoption
must carry the fragment geometry and tile offsets into the generated scene
header without displacing Mario's texture base.

**Progress note (2026-07-28, fragment-tier runtime adoption):** the generated
fragment header and 16×16 bank/CLUT are now selectable through the normal
sourceboot Makefile (`SATURN_DEMO_BSP_FRAGMENTS=1`). The profile keeps the
HWRAM linker assertion intact by placing only its CPU-only transform caches in
the existing LWRAM work section; no SCU-DMA buffer is aliased. A fresh
cross-build and 3,600-frame `--dram-cart` capture completed with
`fault_flags=0`, `slave_timeouts=0`, `frame_serial=64`, and 48,252 VDP1
triangles. The saved frame shows Mario and textured terrain commands, but the
terrain is still visibly warped/sheeted; this is a diagnostic runtime pass,
not visual acceptance or a correctness claim. The next correctness lever is
camera-dependent ordering of the emitted split fragments, followed by a
paired A/B capture against the non-fragment profile.

**Progress note (2026-07-28, fragment BSP expansion):** fragment `source0`
now records the compiled Mesh IR primitive identity, and the runtime expands
each camera-traversed BSP source reference into its visible split fragments.
This is the reference-backed ordering rule from castleviewer/Z-Treme, not a
new per-fragment plane approximation. The fresh `bsp1-frag1` build and
3,600-frame capture completed with `fault_flags=0`, `slave_timeouts=0`,
`frame_serial=27`, and 20,257 VDP1 triangles. The saved image remains
visibly warped/sheeted and is materially unchanged from the bucketed profile,
so the ordering wiring is proven but is not the cause of the remaining visual
error. Texture/UV lowering or the fragment-to-source geometry contract is now
the next correctness investigation.

**Progress note (2026-07-28, repeated-C texture contract):** the reference
audit found that `bake_castle_uv.py:index_position_quads` intentionally keeps
triangle runtime geometry as A/B/C/C; its affine companion is an offline
sampling concept, not a transformed vertex. Both BOB emitters and the VDP1
shape path now follow that contract. The fresh capture remains fault-free and
the image is materially improved (the large foreground sheet is reduced), but
terrain is still visibly warped. This proves the companion mismatch was one
real contributor; remaining UV/geometry error is still open.

**Progress note (2026-07-28, worker texture-homography parity):** the dual
worker's VDP1 texture binder now receives the same A/B/C/C array as its shape
command; the master and worker paths no longer disagree about the fourth
corner. The fresh build/capture is fault-free, but its screenshot hash is
identical to the prior repeated-C capture. This closes the remaining branch
parity gap without claiming another visual improvement; the residual warp is
therefore upstream of the binder call and remains in the fragment bake or
source geometry mapping.

**Progress note (2026-07-28, native convex-quad experiment rejected):** a
castleviewer-style variant preserved 562 convex quads and sampled them with
`sample_quad`, reducing the tier to 1,546 commands/240,480 resident bytes.
Its fresh sourceboot capture showed more missing and warped terrain than the
2,108-command repeated-C triangle tier, so the experiment is not promoted.
The runtime remains on the visibly improved, host-validated triangle tier;
quad preservation stays negative knowledge until its geometry/ordering
contract is understood.

**Progress note (2026-07-28, bounded cancellation polling):** the transform
callback now reads the uncached cancellation latch once per 16 vertices rather
than once per vertex; the bounded callback and outer timeout still provide the
same cancellation guarantee. Same-commit captures preserve the route hash and
zero faults/timeouts. Serial render falls to **2.2151 ms/frame** and dual to
**17.8073 ms/frame**; dual slave share is **20.0146%** (9,601,281 busy ticks /
47,971,375 render ticks), a ~1.4% dual render improvement but still far below
the ≥50% target. The remaining limiter is therefore not the per-vertex cancel
poll alone; master-side transform/shared-bus/emission work remains open.

**Progress note (2026-07-28, partition-cache A/B):** terrain emission now
reads the immutable VDP1 partition layout once per frame instead of once per
primitive. Same-commit fresh captures pass the 600-tick route comparator with
identical checkpoint SHA-256, zero faults/timeouts/capacity rejects, and zero
renderer-counter deltas. Serial render is `1.8005 ms/frame`; dual render is
`17.6779 ms/frame` with `20.1632%` slave share (47,682,152 cumulative render
FRT ticks, 9,614,259 slave-busy ticks, 803 jobs). This is a small measured
improvement over the prior 20.0146% share, not a gate close; the ≥50% target
and the remaining master-side transform/emission work stay open.

**References consumed (AW-3):** SlaveDriver `WALLS.C:1806-1950` (GPL-3.0+,
close-port → `gpl/`), in-repo `gpl/slavedriver_dma_queue.*` precedent for
notice/isolation format, `work/upstream/libyaul-examples/cpu-dual`
(behaviour-only — that checkout lacks a root licence), Z-Treme rendering
split (behaviour lessons, `SHIPPING_ENGINE_COMPARISON.md`).

**Progress note (2026-07-28, reference-led visibility diagnosis):** the
manual free-roam A/B separated two independent disappearance boundaries. A
larger `SATURN_DEMO_VIEW_RADIUS` restored distant terrain but increased near-
Mario loss because the current renderer rejects a whole primitive when any
vertex fails the radius or near-plane test. The retained profile shows
`vdp1_commands_last=626` against a 2048-slot arena with
`reject_vdp1_arena_capacity=0`, so command-capacity exhaustion is not the
explanation. The pinned references establish the replacement shape: Z-Treme
uses bounding-volume/tri-state frustum traversal (`ZT_FRUSTUM.c`,
`ZT_RENDERING.c:494`), SlaveDriver uses sector/portal AABBs and a baked
dependency order (`WALLS.C:1546-1794`, `1953-1983`), and both GPL engines
double-bank VDP1 command staging (`SPR.C:71-72,129,141-157`). Jo Engine's
contribution remains the bounded command-block lifecycle only; its 3-D and
allocator paths are not adopted. The next correctness pass therefore adds
visibility-reason counters, moves radius rejection to spatial bounds, and
double-banks sourceboot command staging before further painter tuning.

The follow-on implementation adds conservative primitive bounds, append-only
visibility-reason counters, double-bank LWRAM command staging, and a
32-pass depth sweep (`SATURN_DEMO_BUCKETS`) to reduce same-bucket terrain
ambiguity. The finer sweep is explicitly an intermediate measure; the
reference-complete solution remains a baked spatial/dependency order plus
near-plane clipping.

The terrain pass contains an opt-in `clip_near` prototype based on
SlaveDriver's bounded `clipZ` tile recovery. It is currently disabled in the
live path after a manual regression made Mario disappear; strict rejection is
the active behavior while the edge-interpolating cache is isolated for a
targeted actor-visibility test (`SATURN_DEMO_NEAR_CLIP=1`). It may be re-enabled only after that test and
a fresh foreground capture pass both prove the actor remains visible.

**Progress note (2026-07-28, transform dispatch batching):** the terrain and
Mario vertex transforms now share one dual-worker submission. The callback
maps the terrain split proportionally onto Mario's immutable pose bank, so
each SH-2 retains disjoint output ownership and the serial flag remains an
exact single-range fallback. This removes one independent dispatch boundary
from the Task 5b hot path; fresh visual/A-B route measurements are still open.

**Progress note (2026-07-28, clip A/B isolation):** the near-clip prototype
now has a distinct `-clip1` sourceboot output tag and can be built beside the
actor-safe `-clip0` image. Both variants use the same generated BOB bank and
reference-led 32-pass ordering; only the opt-in transform recovery differs.
The clip profile cross-builds, but owner visual acceptance is still required
before it can become the default.

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
- [x] **RBG0 horizon-mask spike** (timeboxed; success optional, measurement
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
- [x] **Fog-band spike via sprite color calculation** (pairs with the
  horizon mask; only if it survives): bucket far geometry into 2-3 sprite
  priority groups with VDP2-side color-calc blending toward the sky color.
  Coarse depth cueing at zero VDP1 fill cost — VDP1 half-transparency
  halves fill rate and stays banned for this purpose.
- [x] Poly degradation hooks: bank-level tier selection (the texture spec's
  near/mid/far variants slot here later; a stub tier switch is enough now).
- [x] **Do not** implement viewport shrinking — fill is ~6% of the frame;
  measured non-lever. Recorded here so nobody re-tries it.
- [x] Sweep 2–3 view-distance settings on the frozen route; report absolute
  FPS for each.

**Progress note (2026-07-28, current-commit view sweep):** fresh dual-worker
captures at radii 2,048/4,096/6,000 all reach the 600-tick endpoint with zero
faults, timeouts, and capacity rejects. Render timings are `17.6779`,
`18.2931`, and `17.0403 ms/frame`, respectively; the non-monotonic result
means view distance is not a reliable cadence lever in this emulator window.
The paired screenshots/reports are retained in
`task6-view-distance-sweep-2026-07-28.{md,json,png}` plus the two settings
reports; owner visual acceptance and the final FPS gate remain open.

**Gate:** a settings table — view distance vs absolute FPS — from real route
captures.

**Progress note (2026-07-28, horizon/fog spike measurement):** the RBG0 and
fog-band spikes are measured and deferred. The existing NBG1 sky consumes
262,144 bytes of the 512 KiB VDP2 budget, leaving no comfortable second
full-frame RGB1555 bitmap once rotation parameters and maps are included; a
tilemap seam is not justified for BOB's hilly horizon. Fog requires a new
priority/color-calc bucket path that the current VDP1 stream does not expose.
The measurement record is
`docs/saturn/evidence/reports/task6-horizon-fog-spikes-2026-07-28.md`; the
gradient/NBG1 fallback remains the accepted degradation path.

---

### Task 7: Measure, present, gate

- [x] Full regression, both profiles (and both slave flags). Host `verify-all` passes; interpreted and demo-path sourceboot variants cross-build with slave render disabled/enabled. Runtime route capture and FPS/utilization evidence remain open below.
- [x] Frozen-route capture at the chosen degradation setting: **absolute FPS
  (median and 1% low if the phase timer supports it), never ratios.**
- [x] **Hardware-utilization report alongside FPS**: sim vs render ms (Task
  0 timers), slave share (`slave_busy_ticks` vs master, Task 5b), VDP2
  layers live and VRAM spent (Task 3b), VDP1 vs VDP2 division of the frame.
  Both processors and both VDPs are gated deliverables of this plan, not
  aspirations — the report is complete, but its measured 21.1977% dual slave
  share fails the ≥50% utilization gate.
- [x] Screenshot evidence committed; **the owner's eyes are the acceptance
  gate** — owner accepted the exact capture on 2026-07-28, so it is now
  promoted to `TIMELINE.md`.
  Describe frames factually; never assert what an object is (twice-burned
  rule).
- [x] Honest verdict against the milestones: ≥ 5 FPS visible milestone;
  15/12 sprint gate standing. If short: the numbers, the limiter, the next
  lever — no relabeling.

**Progress note (2026-07-28, absolute-rate window):** paired same-lineage
captures at the chosen `-r2048/-poly0` setting span Ymir screenshot sequences
3,900→8,700 and frame serials 118→803. The resulting absolute rate is
`685 / 4,800 * 60 = 8.5625 FPS`; the long window reaches replay tick 600 with
zero faults/timeouts. This clears the ≥5 FPS visible threshold numerically but
misses the 15 FPS median / 12 FPS 1%-low exit gate. The current phase timer has
no percentile stream, so no 1%-low claim is made. The honest verdict is now
recorded: the visible milestone clears numerically, the 15/12 gate is missed,
and the next measured lever is moving more master-side transform/emission work
to the slave rather than relabeling the current split. Owner visual acceptance
remains open.

**Progress note (2026-07-28, fragment-indexed BSP):** the fragment baker now
emits a fragment-indexed BSP stream (1,183 nodes and 2,108 references, with a
host assertion that references are a permutation of the lowered primitives),
and the runtime traverses that stream directly. A fresh `--dram-cart` capture
at the rebuilt profile symbol `0x060C8D14` completed with `fault_flags=0` and
`slave_timeouts=0`. Its screenshot is visually equivalent to the repeated-C
baseline (the remaining pale foreground sheet and missing terrain are still
present), so the ordering contract is now evidenced but the visual milestone
is not accepted; the next lever remains geometry/UV correctness rather than
the boot path.

**Progress note (2026-07-28, rational fragment lowering):** fragment positions
now use castleviewer’s symmetric nearest-integer policy, while split UVs remain
rational through `sample_triangle` instead of being truncated before tile
baking. The correctly tagged demo profile rebuilt and captured at fresh symbol
`0x060C8D14` with `fault_flags=0`, `slave_timeouts=0`, and 39,976 emitted
triangles. The screenshot is materially unchanged from the repeated-C/indexed
baselines, so this closes a real lowering-contract gap without claiming it is
the remaining visual cause.

**Progress note (2026-07-28, UV contract gate):** the fragment scene now records
its VDP1 mapping contract explicitly (`16x16`, source scale `1`, exact rational
sampling, repeated-C `A/B/C/C` corners). Host tests validate that all 2,108
fragments carry three geometry/UV corners and that every textured tile/CLUT
offset is aligned. This makes the next capture’s UV diagnosis falsifiable;
visual acceptance remains open.

**Progress note (2026-07-28, flat fragment isolation):** an opt-in
`SATURN_DEMO_BSP_FRAGMENT_FLAT=1` profile suppresses texture binding while
preserving the fragment geometry, BSP stream, and command order. Its capture
still contains the large sheet, proving the artifact is present before texture
sampling. A same-limit non-fragment flat capture was also retained, but reaches
a different simulation frame serial, so it is not treated as a clean A/B
verdict; the flat profile remains a reusable isolation gate.

The paired flat frames keep Mario at the same screen location despite the
fragment profile reaching 60 versus 57 frame serial, while the fragment frame
still carries the broad sheet and the source frame does not. This is strong
directional evidence that fragment geometry generation (not texture residency)
introduces the artifact; a frame-serial-normalized rerun is still required
before changing the bake.

The bucket-ordered fragment control produces the same sheet as the indexed-BSP
fragment profile, so changing traversal order alone is not the fix. The
remaining differential is now narrowed to fragment geometry emission (or the
profile’s frame-alignment sensitivity), not VDP1 texture binding or BSP node
walk order.

**Correction (2026-07-28, matched flat source control):** a fresh 3,900-frame
source flat capture reaches serial 64 and shows the same broad terrain
silhouette as the fragment flat control at serial 61. The earlier 3,600-frame
source frame looked cleaner only because it stopped at serial 57. The flat A/B
therefore does not prove a fragment geometry defect; it proves texture binding
is not required for the observed silhouette and restores frame alignment as a
first-class capture requirement.

The normalized source rerun at 3,720 frames reaches serial 60 (240 sim ticks)
and its silhouette matches the fragment flat control at serial 61 (244 ticks).
This confirms the broad sheet is a shared scene/camera presentation at this
route checkpoint, not a fragment-only geometry regression.

**Owner acceptance (2026-07-28):** the textured fragment milestone capture was
reviewed and accepted as a visual checkpoint. The lower-half blue region still
shows terrain disappearing near and around Mario; the owner identified that
limitation as pre-existing rather than a regression from this milestone.

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
