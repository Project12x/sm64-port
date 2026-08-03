# Saturn Dual-SH2 / Dual-VDP Performance Sprint Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement all nine identified frame-path levers while making both SH-2 CPUs, VDP1, and VDP2 do explicit, non-duplicated work; assess visible speed only after the architecture is complete.

**Architecture:** The master SH-2 remains the sole owner of SM64 game state, frame scheduling, final VDP1 command ordering, VRAM allocation, and presentation. The slave SH-2 consumes immutable frame snapshots and owns bounded transform/classification/command-patch jobs through uncached publication records. VDP1 draws terrain, actors, and sprites; VDP2 draws the sky/background, HUD/profiling overlay, and final layer composition. Static content becomes generated templates; each frame reduces to visibility, required transforms, compact ordering, coordinate patches, and asynchronous uploads.

**Tech Stack:** C11, SH-2 assembly/Q16.16, Yaul `6012f79f237773378c8014e70d8998ad95a38d98`, VDP1, VDP2, SCU DMA, Python asset generators/tests, Ymir with DRAM cart.

**Approved architecture correction (2026-08-03):** The manual BOB candidate
showed that the source-complete components below remain connected as a mostly
same-frame serial pipeline. The approved follow-on architecture is
[`2026-08-03-saturn-overlapped-render-pipeline-design.md`](../specs/2026-08-03-saturn-overlapped-render-pipeline-design.md).
It preserves the completed components while superseding immediate waits,
fixed-range joins, full-span fallback, post-transform LOD, duplicate source
render preparation, and unbounded catch-up assumptions. This sprint's Task 12
publication gates remain honestly open; they do not block an experimental
duplicate-render-removal CUE.

**A1 quality gate correction (2026-08-03):** Full-range review of
`4a8fe1ce^..70cb3fe1` blocks that experimental CUE. `geo_process_root()` also
owns animation progression and invokes gameplay/visual-state callbacks for
paintings/DDD warp, environment water, moving textures, flying carpet, camera,
and matrix-derived object positions. No target checkpoint may use the current
whole-walk suppression until a bounded state-only seam and behavioral
differential tests are independently approved. See the active A1 plan and
evidence report for the exact inventory and unexecuted gates.

Safety commit `77ee306c` removes every scene-graph suppression setter call
from `sourceboot_run_source_tick()`. The reserved runtime policy and counters
remain ABI-compatible but dormant; accepted Saturn builds now record normal
walks only. This closes the production-safety exposure without completing A1
or authorizing the experimental CUE.

## Live execution status — update on every task transition

This checklist is the operational source of truth.  A task is not complete
until its implementation/review evidence and this status are updated in the
same task transition.  `source-complete` means host/static evidence and
independent review are clean; it is not a target/Ymir promotion claim.

- [x] **Task 1** — ownership/profile evidence; source-complete at `575ca92`.
- [x] **Task 2** — admitted-position transform set; source-complete at `fb0a7ea`.
- [x] **Task 3** — stable depth bins; source-complete at `8ccb6a7`.
- [x] **Task 4** — cache-through frame handoff; source-complete at `20011a5`; target disassembly gate pending.
- [x] **Task 5** — asynchronous DMA queue; source-complete at `0d1a495`; target partition/ownership gate pending.
- [x] **Task 6** — compile-once VDP1 templates; source-complete at `cc78d82`; BOB fallback/fault target gate pending.
- [x] **Task 7** — compact BSP node spans (renamed from the plan's initially inaccurate “leaf spans”); complete at `79923ce`.
- [x] **Task 8** — conservative hysteretic LOD; source-complete at `5a5e850`; target replay/visual gate pending.
- [x] **Task 9** — bounded slave Mario transform plus compact classification; source-complete at `6731a4c`; target disassembly/Ymir gate pending.
- [x] **Task 10** — pre-reservation shade classification; source-complete at `1d31f00`; target integration gate pending.
- [x] **Task 11** — VDP2 frame API and measured HUD telemetry; source-complete at `1850d18`; full runtime contract remains a Task 12 gate.
- [ ] **Task 12** — integration/evidence/publication is active.  Reference and candidate artifacts build; coherency passes.  All-path/stack-state repairs through `14567bbf`, worker callback repair `bf1e7c13`, and BOB camera parser repair `b29d9397` + `6cfefc74` are independently approved (shared verifier 219/219; exact-tip verifier 214/214).  The exact candidate gate advances past the camera edge to `_geo_process_held_object -> _geo_switch_mario_hand_grab_pos`.  Per the sprint's experimental-CUE exception, the existing `poly2/pipe3` candidate was launched manually in Ymir with the 32-Mbit DRAM-cart profile on 2026-08-03.  This is an experimental visible-performance check, not final acceptance; held-object-edge diagnosis remains active.

### Design decisions landed during execution

- Counters are diagnostic evidence only; no percentage-based promotion gate.
- The 2026-08-03 manual BOB test showed no sufficient visible recovery. Tasks
  1–11 are preserved as components, but their same-frame serial integration is
  not the production architecture. The approved living architecture spec is
  linked above and must be updated during each follow-on task transition.
- Generated BSP data is **node spans**, not leaf spans; content identity is stamped across generated artifacts and checked fail-closed.
- VDP1 owns geometry; VDP2 owns sky/HUD/layer composition only.
- The master owns game state, allocation, ordering, and presentation.  Slave work uses immutable snapshots, disjoint outputs, uncached publication, cache-through peer reads, and positive retirement before any fallback reuse.
- The target native-math gate is closure-based, not a raw-symbol grep.  It must prove the actual bounded route before Ymir can support a performance claim.

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

- [x] Add the failing `offsetof`/decode fixture; it also asserts NBG1|NBG3.
- [x] Record the red decode evidence before implementation.
- [x] Append the profile fields and diagnostic-only timeout/overflow accounting.
- [x] Extend `fast3d_profile_decode.py`.
- [ ] Run the exact aggregate Make target — deferred to Task 12 because mixed Windows/MSYS recipes are environment-blocked; focused ABI/runtime evidence passed.
- [x] Commit `9aa0821` plus the reviewed fault-coverage follow-ups `513c8f2` and `575ca92`.

### Task 2: Transform only positions referenced by admitted leaves

**Files:**
- Create: `src/port/saturn/gfx/saturn_visible_position_set.h`
- Modify: `src/port/saturn/gfx/saturn_demo_render.c`
- Create: `tools/saturn/visible_position_set_test.c`
- Modify: `Makefile.saturn.mk`

**Interfaces:**
- Produces `sm64_saturn_visible_position_set_reset`, `_mark_primitive`, `_test`, and `_count` over a caller-owned bitset.
- Consumes the bounded admitted primitive-ID work list plus generated primitive indices; output is immutable before either SH-2 begins transforming. Task 7 may replace the producer of that work list without changing this API.

- [x] Add shared-vertex, excluded-primitive, and fail-closed out-of-range host coverage.
- [x] Add the host target and record red/green evidence.
- [x] Implement the caller-owned `uint32_t` bitset before dual-SH2 dispatch.
- [x] Skip unmarked terrain positions while retaining separate actor positions.
- [x] Publish bounded `required_positions`.
- [x] Run the focused host fixture and transform-equivalence checks (Windows-native equivalent; aggregate Make route remains a Task 12 environment gate).
- [x] Commit `e8a38b8` and fail-closed test follow-up `fb0a7ea`.

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

- [x] Add empty/equal/reverse/clipped/mixed fixtures and predecessor-order evidence.
- [x] Add the target and record red/green evidence.
- [x] Implement stable fixed-bin count/prefix/scatter with no comparison sort in the target path.
- [x] Preserve host comparison with the predecessor raw-key merge and explicitly test intended ordering deltas.
- [x] Switch the demo path; keep merge only as host oracle.
- [x] Run focused bin/template/runtime-contract host tests; exact aggregate Make remains a Task 12 environment gate.
- [x] Commit `62f5cde` and runtime/oracle correction `8ccb6a7`.

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

- [x] Add source/mutation verifier failures for cached flags, peer aliasing, publication order, and purge.
- [x] Add the host target and red/green source verification.
- [x] Implement cached-owner/cache-through-peer aliases plus uncached release records.
- [x] Remove accepted-frame whole-cache purge after verifier coverage.
- [ ] Complete target disassembly proof of the new section/alias and no accepted-frame purge — candidate coherency symbol gate passes; final native-math route gate is active in Task 12.
- [x] Commit `2ad40a0` and consumer-alias verifier hardening `20011a5`.

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

- [x] Add mock-SCU descriptor/FIFO/wrap/wait tests plus later stale-sequence and unsafe-address coverage.
- [x] Add the target and red/green host evidence.
- [x] Split submit/kick/poll/wait/idle with validated SCU request ownership.
- [x] Double-buffer VDP1 command and Gouraud staging.
- [x] Wait only at actual VDP1/bank lifetime boundaries.
- [ ] Complete target section/partition/counter comparison — hard Task 12 gate.
- [x] Commit `54731bb` and DMA contract hardening `0d1a495`.

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

- [x] Poison immutable template words and enforce exact patch masks.
- [x] Add clipped/recovery/textured/texture-suppressed resolved-image coverage.
- [x] Resolve static state at load time; workers publish dynamic patch payloads only.
- [x] Use master template copy/patch for supported cases; retain counted malformed fallback only.
- [ ] Prove zero BOB legacy fallbacks on the accepted target route — Task 12/Ymir gate.
- [x] Run focused template/depth host fixtures (command-arena target evidence remains Task 12).
- [x] Commit `e50fc47` and mask/lifecycle follow-up `cc78d82`.

### Task 7: Generate compact BSP leaf spans and stop scanning all primitives

**Files:**
- Modify: `tools/saturn/compile_bob_bsp.py`
- Modify: `tools/saturn/emit_bob_scene.py`
- Modify: `src/port/saturn/gfx/saturn_demo_render.c`
- Modify: `tools/saturn/test_tools.py`

**Interfaces:**
- Generated data provides `leaf_first_ref[]`, `leaf_ref_count[]`, and a packed `primitive_refs[]` stream with each primitive appearing in the exact conservative leaves required by the bake.
- Runtime traversal emits work spans directly; it does not clear or scan an 867-entry admission array.

- [x] Add deterministic/in-range/unique/conservative generator tests for packed **node** spans (the original “leaf” wording was corrected).
- [x] Record the absent-symbol red test.
- [x] Emit packed node spans and append them directly to bounded work order.
- [x] Remove the accepted-path all-primitive scan and preserve predecessor set/order oracle.
- [x] Run focused generator regression suite and native BSP header smoke; generated content identity rejects stale same-size artifacts.
- [x] Commit `524c48c` and audit correction `79923ce`.

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

- [x] Add hysteresis/jitter/mandatory-prefix/scene reset tests, including per-tick catch-up transition coverage.
- [x] Record projected-size red evidence.
- [x] Compute projected-span/depth tier before clipping/material allocation.
- [x] Add exact tier roles `0|1|2` and distinct tags; invalid roles degrade nothing.
- [ ] Record fixed-replay suppression/no-oscillation evidence — counters are diagnostic only and target replay remains Task 12.
- [x] Commit `5dd39e1` with scene-boundary and placement-guard follow-ups through `5a5e850`.

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

- [x] Add serial/split snapshot and live-pointer-rejection fixture, later strengthened with deterministic delayed-worker fallback coverage.
- [x] Add Windows-native `verify-dual-actor-worker` target and red/green evidence.
- [x] Add strictly post-terrain, non-overlapping transform and compact classification phases; positive retirement precedes fallback reuse.
- [x] Publish compact refs through uncached/cache-through contract.
- [x] Verify host serial/split output, timeout fallback, and cached all-master ownership mutation; target proof remains Task 12.
- [x] Commit `edc3e78` and corrective series through `6731a4c`.

### Task 10: Expand flat/material fast paths and avoid unnecessary Gouraud work

**Files:**
- Modify: `src/port/saturn/gfx/saturn_terrain_emit_policy.h`
- Modify: `src/port/saturn/gfx/saturn_gouraud_bank.h`
- Modify: `src/port/saturn/gfx/saturn_demo_render.c`
- Modify: `tools/saturn/terrain_command_template_test.c`

**Interfaces:**
- `sm64_saturn_terrain_shade_path()` decides `FLAT_REPLACE`, `TEXTURED_REPLACE`, or `GOURAUD` before any table reservation/upload.
- Equal post-light colors and LOD-suppressed gradients never allocate Gouraud entries.

- [x] Add equal/near-equal/clipped/textured/LOD fixtures and red evidence.
- [x] Record pre-change unsupported-path behavior.
- [x] Encode worker shade path safely alongside clip/recovery/LOD flags.
- [x] Allocate/upload only `GOURAUD`; append saved-table/byte diagnostics to profile.
- [x] Prove real master lowering preserves four post-light gradient pixels; ordinary flat paths allocate zero tables.
- [x] Commit `f96ada9` with payload/lowering corrections through `1d31f00`.

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

- [x] Add NBG1/NBG3, VDP1-priority, copied-camera, HUD-cadence host contract.
- [x] Record red absence of the frame API.
- [x] Coalesce sky/HUD/layers behind master-only begin/commit API.
- [x] Display measured integer FPS/MT/ST/ORD/DMAW/VDP1W telemetry (not construction-time aliases).
- [x] Prove both VDP2 layers and no VDP2 geometry API/path in focused contract/static checks.
- [x] Commit `687e76b` with telemetry and decoder-fixture follow-ups through `1850d18`.

### Task 12: Integrate, balance both SH-2s, and publish the experimental CUE

**Files:**
- Modify: `src/port/saturn/gfx/saturn_demo_render.c`
- Modify: `src/port/saturn/sourceboot/Makefile`
- Modify: `tools/saturn/fast3d_profile_decode.py`
- Create: `docs/saturn/evidence/reports/dual-sh2-vdp-pipeline-sprint-2026-08-02.md`

**Interfaces:**
- Final build role uses Q16 camera variant 3, BSP node spans, required-position transform, stable bins, cache-through handoff, asynchronous DMA, template patching, tier-2 LOD, split actor transform, and coalesced VDP2 commits.

- [ ] Run the entire aggregate host gate — focused fixtures/profile ABI pass, but `verify-tools` and mixed-shell Make routes remain environment-blocked and broad generator/runtime checks are still pending.
- [x] Build reference and candidate roles serially with identical route/input through the audited DLL/TMP wrapper; both artifact chains are published and hashed. Candidate command:

```powershell
make -C src/port/saturn/sourceboot -B -j1 SATURN_DEMO_PATH=1 SATURN_SOURCEBOOT_ROUTE_REPLAY=1 SATURN_SOURCEBOOT_LIVE_INPUT=1 SATURN_SOURCEBOOT_LIVE_INPUT_BOOTSTRAP_TICKS=600 SATURN_SOURCEBOOT_CAMERA_ROUTE=0 SATURN_CAMERA_VARIANT=3 SATURN_SOURCE_CART_STAGE_SECTORS=8 SATURN_DEMO_HOT_PROMOTION=1 SATURN_DEMO_NEAR_CLIP=1 SATURN_DEMO_BSP_ORDER=1 SATURN_DEMO_POLY_TIER=2 SATURN_RENDERER_PIPELINE=3
```

- [ ] Inspect the candidate ELF — coherency passes and required worker/VDP symbols are linked; reviewed bounded native-math route verification is still resolving real ELF metadata, so this gate is not complete.
- [ ] Run automated Ymir DRAM-cart comparison — blocked until the candidate ELF gate is green.
- [ ] Tune only from valid correctness captures — no valid candidate capture yet.
- [ ] Run final manual Ymir live-input test — blocked until automated capture passes.
- [ ] Finish evidence report — preflight/build hashes are recorded; counter table/screenshots/tradeoffs/rollback remain pending capture.
- [ ] Add `EXPERIMENTAL_BUILD.txt` beside the published candidate CUE — publication remains pending acceptance.
- [ ] Commit the experimental pipeline publication — pending all Task 12 gates.

## Promotion Gate

The sprint is promoted only when all nine levers are either active in the candidate or rejected with source-backed reasoning, both SH-2s perform their assigned bounded work, VDP1 and VDP2 both contribute their assigned layers, and the final gameplay state/visible ordering match the reference. Manual feel and valid counters are recorded after that architecture is intact, not used as invented percentage gates. A black/sky-only frame, duplicated VDP geometry, a timeout, or unverifiable telemetry may not support a performance claim.

## Single Execution Prompt

```text
Execute docs/superpowers/plans/2026-08-02-saturn-dual-sh2-vdp-pipeline-sprint.md using superpowers:subagent-driven-development. Work task-by-task in the existing sh2-native-math-purge worktree. Dispatch one implementation agent and then independent spec/quality reviewers per task; never let agents run target builds concurrently. Preserve unrelated dirty files. Enforce the global master/slave and VDP1/VDP2 ownership contracts. Use the pinned PS1, SlaveDriver, Z-Treme, Jo Engine, and Yaul references exactly as documented, including provenance and reuse limits. Run host tests per task, but batch target builds and Ymir runs at the named gates. Do not require the static native-math census for the experimental CUE. Do not stop after one attractive micro-optimization: complete or evidence-reject every task, publish the labeled DRAM-cart CUE, and report counter-backed performance plus manual feel.
```
