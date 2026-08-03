# Saturn Dual-SH2 / Dual-VDP Performance Sprint Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement all nine identified frame-path levers while making both SH-2 CPUs, VDP1, and VDP2 do explicit, non-duplicated work; assess visible speed only after the architecture is complete.

**Architecture:** The master SH-2 remains the sole owner of SM64 game state, frame scheduling, final VDP1 command ordering, VRAM allocation, and presentation. The slave SH-2 consumes immutable frame snapshots and owns bounded transform/classification/command-patch jobs through uncached publication records. VDP1 draws terrain, actors, and sprites; VDP2 draws the sky/background, HUD/profiling overlay, and final layer composition. Static content becomes generated templates; each frame reduces to visibility, required transforms, compact ordering, coordinate patches, and asynchronous uploads.

**Tech Stack:** C11, SH-2 assembly/Q16.16, Yaul `6012f79f237773378c8014e70d8998ad95a38d98`, VDP1, VDP2, SCU DMA, Python asset generators/tests, Ymir with DRAM cart.

## Global Constraints

- Worktree: `D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge`.
- Preserve unrelated dirty audit files and evidence artifacts; stage only task-owned files.
- Experimental CUE builds do not require the static native-math census to pass.
- Target builds are serial: `make -B -j1`; do not run target builds concurrently on the owner machine.
- Batch host verification by task; run Ymir only for Task 12's automated comparison and final manual acceptance.
- Every Ymir/sourceboot run uses the project USA BIOS and DRAM/memory cart.
- BOB is the deterministic proving ground, not a production scene special case. Runtime APIs and generated formats must accept arbitrary scene data.
- Master SH-2 alone may read/write live SM64 game state, allocate VDP1/Gouraud slots, commit final command order, or initiate presentation.
- Slave SH-2 receives immutable integer snapshots and disjoint output ranges only. It may not dereference `MarioState`, graph nodes, texture residency state, VDP1 VRAM, or VDP2 registers.
- Cross-CPU completion, counts, and span headers use uncached storage. Cached bulk banks must use explicit cache-through publication/read aliases; no implicit coherency assumptions.
- VDP1 owns geometry and sprites. VDP2 owns sky/background, HUD/profiling text, and layer composition. Neither VDP renders a duplicate copy of the same scene geometry.
- The source gameplay state and final Mario action/position remain authoritative and must match the pinned replay baseline.
- Preserve near clipping, texture/material identity, painter/BSP correctness, command-arena bounds, and zero overflow/fault counters.
- Reference decisions: `malucard/sm64-psx@3073845688ea273da78d539b20c45110d8a868c3` is pattern study only; SlaveDriver `a8986591557b6e680550d3c23970284d3b38ff8f` and Sonic Z-Treme `cff75451c1616aac1236fc2b44223902b55c706b` are GPL-compatible pattern sources; Jo Engine `556d081146211b6a1cfa6591d70f9487d406758b` and Yaul are attributed implementation sources under their recorded notices.
- Keep the current predecessor CUE and report immutable for comparison. Every promoted wave records ELF/CUE SHA-256, source commit, build flags, and counters.
- Counters are diagnostic evidence, not percentage-based promotion gates. A missing or invalid sourceboot telemetry probe must not silently become a performance claim.

## Ownership Contract

| Stage | Master SH-2 | Slave SH-2 | VDP1 | VDP2 |
| --- | --- | --- | --- | --- |
| Source tick | Owns SM64 simulation and input | Idle or finishes prior bounded render job | Displays previous completed list | Displays sky/HUD/background |
| Visibility | Traverses top-level BSP and publishes immutable work spans | Traverses assigned leaf spans and marks required positions | — | — |
| Transform/classify | Processes master range | Processes slave range | — | — |
| Order/patch | Merges fixed bins and patches final master-owned slots | Produces disjoint compact refs/command patches | — | — |
| Upload | Enqueues and retires SCU DMA at explicit lifetime boundary | Never touches VRAM | Receives command/Gouraud data and draws geometry | Receives sky/HUD updates and composites layers |
| Present | Waits for required DMA/VDP fences, flips banks | Publishes done before frame retirement | Draw-end is authoritative | VBlank commits scroll/HUD/layer state |

---

### Task 1: Add minimal dual-SH2 and dual-VDP ownership evidence

**Files:**
- Modify: `src/port/saturn/gfx/saturn_fast3d_frontend.h`
- Modify: `src/port/saturn/gfx/saturn_demo_render.c`
- Modify: `src/port/saturn/sourceboot/main.c`
- Modify: `tools/saturn/fast3d_profile_decode.py`
- Test: `tools/saturn/runtime_contract_test.c`

**Interfaces:**
- Produces appended counters: `master_worker_started`, `slave_worker_started`, `vdp1_commands`, `vdp2_active_layers`, and `pipeline_faults`.
- Counter fields are appended to the profile; no existing field offset moves.

- [ ] Add a failing `offsetof`/decode fixture asserting the new fields append in the exact order above and that `vdp2_active_layers` records NBG1 and NBG3.
- [ ] Run `.venv-saturn-tools/Scripts/python.exe tools/saturn/test_tools.py` and confirm the new decode assertions fail because fields are absent.
- [ ] Append the fields to `sm64_saturn_fast3d_profile_t`; set the worker-start markers at dispatch, record VDP1 command count and VDP2 display mask at present, and increment `pipeline_faults` for timeout/overflow paths without changing scheduling.
- [ ] Extend `fast3d_profile_decode.py` with those exact names and integer values.
- [ ] Run `make -f Makefile.saturn.mk verify-tools verify-runtime-contracts`; require all prior offsets unchanged and all new synthetic fixture values decoded exactly.
- [ ] Commit: `git commit -m "perf: expose dual-SH2 and dual-VDP frame stages"`.

### Task 2: Transform only positions referenced by admitted leaves

**Files:**
- Create: `src/port/saturn/gfx/saturn_visible_position_set.h`
- Modify: `src/port/saturn/gfx/saturn_demo_render.c`
- Create: `tools/saturn/visible_position_set_test.c`
- Modify: `Makefile.saturn.mk`

**Interfaces:**
- Produces `sm64_saturn_visible_position_set_reset`, `_mark_primitive`, `_test`, and `_count` over a caller-owned bitset.
- Consumes the bounded admitted primitive-ID work list plus generated primitive indices; output is immutable before either SH-2 begins transforming. Task 7 may replace the producer of that work list without changing this API.

- [ ] Write a host test with two quads sharing one vertex; assert six unique positions are marked, an excluded primitive marks nothing, and out-of-range indices fail closed.
- [ ] Add `verify-visible-position-set` to `Makefile.saturn.mk`, run `make -f Makefile.saturn.mk verify-visible-position-set`, and expect failure because the header does not exist.
- [ ] Implement the bitset with `uint32_t` words and no allocation. Build it from admitted primitive IDs after BSP traversal and before `demo_prepare_position_owners()`.
- [ ] Change `demo_transform_owned_positions()` to skip unmarked positions; keep actor positions separate.
- [ ] Record marked count in `required_positions` and assert it never exceeds `SM64_SATURN_BOB_POSITION_COUNT`.
- [ ] Run `make -f Makefile.saturn.mk verify-visible-position-set verify-ir-transform`; require identical projected coordinates for every position still marked.
- [ ] Commit: `git commit -m "perf: transform only BSP-required terrain positions"`.

### Task 3: Replace full-frame merge sort with stable fixed depth bins

**Files:**
- Create: `src/port/saturn/gfx/saturn_terrain_depth_bins.h`
- Modify: `src/port/saturn/gfx/saturn_terrain_fused.h`
- Modify: `src/port/saturn/gfx/saturn_demo_render.c`
- Create: `tools/saturn/terrain_depth_bins_test.c`
- Modify: `Makefile.saturn.mk`

**Interfaces:**
- Produces `sm64_saturn_terrain_depth_bins_build(records, count, refs, scratch, capacity)` returning a stable far-to-near stream.
- Key is `(clamped_depth_bin, bsp_leaf, primitive_id)`; equal keys preserve producer order.

- [ ] Write fixtures for empty, one-record, all-equal, reverse-depth, clipped-fan siblings, and mixed master/slave streams. Compare output to the existing stable merge sort.
- [ ] Add `verify-terrain-depth-bins` to `Makefile.saturn.mk`, run `make -f Makefile.saturn.mk verify-terrain-depth-bins`, and confirm failure because the bin API is absent.
- [ ] Implement a two-pass stable 64-bin count/prefix/scatter using fixed arrays and no comparison sort.
- [ ] Preserve a compile-time comparison role that runs both algorithms on host and rejects any key-order mismatch.
- [ ] Switch the target demo path to bins; retain the old merge implementation only for the comparison fixture.
- [ ] Run `make -f Makefile.saturn.mk verify-terrain-depth-bins verify-terrain-command-template`; require identical record multiset and deterministic order.
- [ ] Commit: `git commit -m "perf: replace terrain merge sort with stable depth bins"`.

### Task 4: Remove whole-cache purges from the cross-SH2 transform barrier

**Files:**
- Create: `src/port/saturn/gfx/saturn_dual_frame_bank.h`
- Modify: `src/port/saturn/gfx/saturn_demo_render.c`
- Modify: `tools/saturn/verify_dual_cpu_coherency.py`
- Create: `tools/saturn/dual_frame_bank_test.c`
- Modify: `Makefile.saturn.mk`

**Interfaces:**
- Produces cached producer pointers, cache-through consumer pointers, uncached ready/count words, and `sm64_saturn_dual_frame_publish(lane, sequence, count)`.
- Publication order is bulk writes, compiler barrier, sequence/count, ready flag last.

- [ ] Add verifier failures for a cached completion flag, missing cache-through peer read, ready-before-count publication, and any `cpu_cache_purge()` in the accepted frame path.
- [ ] Add `verify-dual-frame-bank` to `Makefile.saturn.mk`, run `make -f Makefile.saturn.mk verify-dual-frame-bank`, and confirm the current whole-cache purge is rejected by its source/disassembly fixture.
- [ ] Implement physical-to-cache-through alias helpers for `s_view`, `s_projected`, and `s_position_valid`; each CPU reads its own range cached and the peer range through the cache-through alias after the uncached fence.
- [ ] Remove the whole-cache purge only after the verifier sees all cross-owner reads use the correct alias.
- [ ] Run `make -f Makefile.saturn.mk verify-dual-frame-bank`; at the wave build, run `tools/saturn/verify_dual_cpu_coherency.py <candidate.sym>` and inspect target disassembly to prove no accepted-frame `cpu_cache_purge` call remains.
- [ ] Commit: `git commit -m "perf: use cache-through dual-SH2 frame handoff"`.

### Task 5: Make the SlaveDriver-derived DMA queue asynchronous

**Files:**
- Modify: `src/port/saturn/gpl/slavedriver_dma_queue.h`
- Modify: `src/port/saturn/gpl/slavedriver_dma_queue.c`
- Modify: `src/port/saturn/sourceboot/main.c`
- Modify: `src/port/saturn/gfx/saturn_fast3d_vdp1_emit.c`
- Create: `tools/saturn/dma_queue_test.c`
- Modify: `Makefile.saturn.mk`

**Interfaces:**
- Produces `saturn_dma_queue_submit`, `saturn_dma_queue_kick`, `saturn_dma_queue_poll`, `saturn_dma_queue_wait`, and `saturn_dma_queue_idle`.
- Queue descriptors copy addresses/lengths; source memory remains owned until the matching completion sequence retires.

- [ ] Write a mock-SCU test proving submit does not copy, kick starts exactly one transfer, poll retires completed entries in order, wraparound is bounded, and wait drains all entries.
- [ ] Add `verify-dma-queue` to `Makefile.saturn.mk`, run `make -f Makefile.saturn.mk verify-dma-queue`, and confirm the current submit-and-immediate-drain behavior fails.
- [ ] Split queue submission from transfer start/wait. Use Yaul SCU DMA completion state; do not spin after every request.
- [ ] Double-buffer VDP1 command and Gouraud staging so the CPU can build bank N+1 while DMA/VDP1 consumes bank N.
- [ ] Wait only before a bank is reused or before VDP1 draw begins without its required uploads.
- [ ] Run `make -f Makefile.saturn.mk verify-dma-queue`; at the wave build compare `sh-elf-readelf -S` and the sourceboot VDP1 partition counters against the predecessor, requiring zero queue overflow and unchanged command/texture/Gouraud VRAM ranges.
- [ ] Commit: `git commit -m "perf: overlap SCU DMA with frame construction"`.

### Task 6: Finish compile-once VDP1 command state

**Files:**
- Modify: `src/port/saturn/gfx/saturn_terrain_command_template.h`
- Modify: `src/port/saturn/gfx/saturn_terrain_command_template.c`
- Modify: `src/port/saturn/gfx/saturn_terrain_fused.h`
- Modify: `src/port/saturn/gfx/saturn_demo_render.c`
- Modify: `tools/saturn/terrain_command_template_test.c`

**Interfaces:**
- Each generated/resolved template contains every immutable VDP1 word plus an exact patch mask for link, four XY pairs, Gouraud address, and dynamic texture source when required.
- Per-frame patch code may write only fields named by that mask.

- [ ] Extend the template fixture to poison all immutable words and prove coordinate/link/Gouraud patching changes only allowed offsets.
- [ ] Add clipped, recovery-material, textured-flat, textured-Gouraud, and texture-suppressed fixtures; run and observe missing-template failures.
- [ ] Resolve all static material/texture/draw-mode words at load time. Store only dynamic patch data in worker outputs.
- [ ] Replace fallback command reconstruction for supported cases with template copy/patch; keep a counted fallback only for malformed or genuinely dynamic state.
- [ ] Require `demo_bob_terrain_legacy_fallbacks == 0` on the accepted BOB route.
- [ ] Run template, terrain, and command-arena tests.
- [ ] Commit: `git commit -m "perf: finish compile-once terrain command templates"`.

### Task 7: Generate compact BSP leaf spans and stop scanning all primitives

**Files:**
- Modify: `tools/saturn/compile_bob_bsp.py`
- Modify: `tools/saturn/emit_bob_scene.py`
- Modify: `src/port/saturn/gfx/saturn_demo_render.c`
- Modify: `tools/saturn/test_tools.py`

**Interfaces:**
- Generated data provides `leaf_first_ref[]`, `leaf_ref_count[]`, and a packed `primitive_refs[]` stream with each primitive appearing in the exact conservative leaves required by the bake.
- Runtime traversal emits work spans directly; it does not clear or scan an 867-entry admission array.

- [ ] Add generator tests for deterministic leaf spans, in-range refs, no duplicate ref within one leaf, complete conservative coverage, and identical output under repeated generation.
- [ ] Run the tests and confirm generated span symbols are absent.
- [ ] Emit the packed spans and update `demo_spatial_admit()` to append admitted leaf refs into the bounded work list.
- [ ] Remove the all-primitives spatial admission scan from the accepted BSP path; preserve a host comparison against the old admission set.
- [ ] Run `tools/saturn/test_tools.py` and `bob_bsp_header_smoke.c`; require exact set equality with fewer runtime primitive visits.
- [ ] Commit: `git commit -m "perf: drive terrain work from compact BSP leaf spans"`.

### Task 8: Enable conservative Z-Treme-style LOD and early material degradation

**Files:**
- Modify: `src/port/saturn/gpl/ztreme_hot_promotion.h`
- Modify: `src/port/saturn/gpl/ztreme_hot_promotion.c`
- Modify: `src/port/saturn/gfx/saturn_demo_render.c`
- Modify: `src/port/saturn/sourceboot/Makefile`
- Modify: `tools/saturn/hot_promotion_test.c`

**Interfaces:**
- Produces hysteretic tiers `NEAR`, `MID`, `FAR` from projected size/depth with separate enter/exit thresholds.
- FAR may suppress only bake-approved optional primitives; MID may suppress expensive texture/Gouraud work while preserving silhouette and collision-independent geometry.

- [ ] Add tests for threshold hysteresis, camera jitter, mandatory route-prefix preservation, and deterministic tier reset on level change.
- [ ] Run the fixture and confirm current depth-only behavior fails the projected-size cases.
- [ ] Compute tier before clipping/material work. Apply generated masks before texture lookup and Gouraud allocation.
- [ ] Add build roles `SATURN_DEMO_POLY_TIER=0|1|2`, with tier 0 as visual reference and tier 2 as candidate; tag outputs distinctly.
- [ ] Require nonzero suppression/downgrade counters in tier 2, zero mandatory-primitive drops, and no oscillation on the fixed camera replay.
- [ ] Commit: `git commit -m "perf: apply hysteretic terrain LOD before material work"`.

### Task 9: Give the slave SH-2 bounded Mario transform/classification work

**Files:**
- Modify: `src/port/saturn/gfx/saturn_demo_render.c`
- Modify: `src/port/saturn/gpl/slavedriver_dual_worker.h`
- Modify: `src/port/saturn/gpl/slavedriver_dual_worker.c`
- Create: `tools/saturn/dual_actor_worker_test.c`
- Modify: `Makefile.saturn.mk`

**Interfaces:**
- Slave consumes a copied `sm64_saturn_mario_actor_snapshot_t`, copied pose, immutable mesh/material tables, and a disjoint result span.
- Master retains Gouraud/texture/VDP1 slot allocation and final actor insertion relative to terrain.

- [ ] Add a host fixture that compares serial and split transform/classification outputs for every Mario vertex/primitive and rejects live game-state pointers in the worker context.
- [ ] Add `verify-dual-actor-worker` to `Makefile.saturn.mk`, run `make -f Makefile.saturn.mk verify-dual-actor-worker`, and confirm the split API is absent.
- [ ] Add a second bounded worker phase only when terrain slave work has retired; do not overlap two slave jobs or grant the slave VDP access.
- [ ] Publish compact actor refs through the same sequence/uncached contract as terrain.
- [ ] Run `make -f Makefile.saturn.mk verify-dual-actor-worker`; require identical actor command order/colors/coordinates between serial and split host roles and zero simulated slave timeouts.
- [ ] Commit: `git commit -m "perf: split Mario transform work across both SH2s"`.

### Task 10: Expand flat/material fast paths and avoid unnecessary Gouraud work

**Files:**
- Modify: `src/port/saturn/gfx/saturn_terrain_emit_policy.h`
- Modify: `src/port/saturn/gfx/saturn_gouraud_bank.h`
- Modify: `src/port/saturn/gfx/saturn_demo_render.c`
- Modify: `tools/saturn/terrain_command_template_test.c`

**Interfaces:**
- `sm64_saturn_terrain_shade_path()` decides `FLAT_REPLACE`, `TEXTURED_REPLACE`, or `GOURAUD` before any table reservation/upload.
- Equal post-light colors and LOD-suppressed gradients never allocate Gouraud entries.

- [ ] Add fixtures for equal colors, near-equal but distinct colors, clipped interpolated colors, textured flat material, and LOD-suppressed gradient.
- [ ] Run and confirm unsupported cases currently take the Gouraud path.
- [ ] Move shade-path classification into worker classification and encode the result in the compact record/template patch flags.
- [ ] Reserve and upload Gouraud tables only for `GOURAUD`; record saved-table and saved-byte counters.
- [ ] Require unchanged pixel inputs for real gradients and zero Gouraud allocation for flat fixtures.
- [ ] Commit: `git commit -m "perf: reject flat polygons before Gouraud allocation"`.

### Task 11: Make VDP2 an explicit performance participant

**Files:**
- Create: `src/port/saturn/gfx/saturn_vdp2_frame.h`
- Create: `src/port/saturn/gfx/saturn_vdp2_frame.c`
- Modify: `src/port/saturn/sourceboot/main.c`
- Modify: `tools/saturn/runtime_contract_test.c`

**Interfaces:**
- `sm64_saturn_vdp2_frame_begin(snapshot, profile)` updates sky scroll and prepares a compact HUD tile/string update.
- `sm64_saturn_vdp2_frame_commit()` performs one VBlank-synchronized commit for NBG1 sky, NBG3 HUD, priorities, and display mask.
- VDP2 never receives terrain or Mario geometry.

- [ ] Add a host contract asserting NBG1 and NBG3 are enabled, VDP1 sprite priorities remain visible, sky scroll derives only from the copied camera snapshot, and HUD update rate is bounded to once per 30 source ticks.
- [ ] Run and confirm the consolidated VDP2 frame API is absent.
- [ ] Move sky/HUD/layer updates behind the new frame API and coalesce them into one commit per presented frame.
- [ ] Display total FPS plus master transform, slave transform, ordering, DMA wait, and VDP1 wait counters without invoking float formatting.
- [ ] Require `vdp2_active_layers` to show both NBG1 and NBG3 and verify no VDP2 bitmap/polygon path duplicates VDP1 scene geometry.
- [ ] Commit: `git commit -m "perf: coalesce VDP2 sky HUD and layer commits"`.

### Task 12: Integrate, balance both SH-2s, and publish the experimental CUE

**Files:**
- Modify: `src/port/saturn/gfx/saturn_demo_render.c`
- Modify: `src/port/saturn/sourceboot/Makefile`
- Modify: `tools/saturn/fast3d_profile_decode.py`
- Create: `docs/saturn/evidence/reports/dual-sh2-vdp-pipeline-sprint-2026-08-02.md`

**Interfaces:**
- Final build role uses Q16 camera variant 3, BSP leaf spans, required-position transform, stable bins, cache-through handoff, asynchronous DMA, template patching, tier-2 LOD, split actor transform, and coalesced VDP2 commits.

- [ ] Run all host fixtures added above plus existing camera, Q16, terrain-template, memory-map, and coherency suites. Require all green before target build.
- [ ] Build the reference role and candidate role serially with identical route/input. Candidate command:

```powershell
make -C src/port/saturn/sourceboot -B -j1 SATURN_DEMO_PATH=1 SATURN_SOURCEBOOT_ROUTE_REPLAY=1 SATURN_SOURCEBOOT_LIVE_INPUT=1 SATURN_SOURCEBOOT_LIVE_INPUT_BOOTSTRAP_TICKS=600 SATURN_SOURCEBOOT_CAMERA_ROUTE=0 SATURN_CAMERA_VARIANT=3 SATURN_SOURCE_CART_STAGE_SECTORS=8 SATURN_DEMO_HOT_PROMOTION=1 SATURN_DEMO_NEAR_CLIP=1 SATURN_DEMO_BSP_ORDER=1 SATURN_DEMO_POLY_TIER=2 SATURN_RENDERER_PIPELINE=3
```

- [ ] Inspect the candidate ELF: no camera/transform soft-float or generic 64-bit division calls; no accepted-frame `cpu_cache_purge`; both master and slave worker symbols present; VDP1 and VDP2 commit paths reachable.
- [ ] Run one automated Ymir DRAM-cart comparison at the pinned BOB route. Reject black/sky-only evidence, invalid sourceboot probe addresses, gameplay-state mismatch, timeout, arena overflow, DMA overflow, or slave timeout.
- [ ] Tune only `slave_begin`, depth-bin count, and LOD thresholds from correctness captures and any valid counters. Require both SH-2s to execute their assigned bounded jobs, no timeout/overflow/fault counter, and no source-state or visible-order regression; do not impose percentage targets before representative measurements exist.
- [ ] Run one final manual Ymir DRAM-cart test with live input after bootstrap. Require visible terrain and Mario, responsive controls, VDP2 sky/HUD, and an obvious improvement over the predecessor by feel.
- [ ] Write the evidence report with source commits, reference provenance, exact flags, SHA-256 identities, counter table, screenshots, known visual tradeoffs, and rollback roles.
- [ ] Add `EXPERIMENTAL_BUILD.txt` beside the CUE stating: dual-SH2 pipeline, VDP1 geometry, VDP2 sky/HUD, tier-2 LOD, 600-tick BOB bootstrap then live input, DRAM cart required, static census intentionally not required.
- [ ] Commit: `git commit -m "perf: publish dual-SH2 dual-VDP experimental pipeline"`.

## Promotion Gate

The sprint is promoted only when all nine levers are either active in the candidate or rejected with source-backed reasoning, both SH-2s perform their assigned bounded work, VDP1 and VDP2 both contribute their assigned layers, and the final gameplay state/visible ordering match the reference. Manual feel and valid counters are recorded after that architecture is intact, not used as invented percentage gates. A black/sky-only frame, duplicated VDP geometry, a timeout, or unverifiable telemetry may not support a performance claim.

## Single Execution Prompt

```text
Execute docs/superpowers/plans/2026-08-02-saturn-dual-sh2-vdp-pipeline-sprint.md using superpowers:subagent-driven-development. Work task-by-task in the existing sh2-native-math-purge worktree. Dispatch one implementation agent and then independent spec/quality reviewers per task; never let agents run target builds concurrently. Preserve unrelated dirty files. Enforce the global master/slave and VDP1/VDP2 ownership contracts. Use the pinned PS1, SlaveDriver, Z-Treme, Jo Engine, and Yaul references exactly as documented, including provenance and reuse limits. Run host tests per task, but batch target builds and Ymir runs at the named gates. Do not require the static native-math census for the experimental CUE. Do not stop after one attractive micro-optimization: complete or evidence-reject every task, publish the labeled DRAM-cart CUE, and report counter-backed performance plus manual feel.
```
