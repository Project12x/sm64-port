# Sprint 3 — Playable BOB, WF from the menu, and the road back past 7 FPS

**Date:** 2026-08-22
**Owner directive:** *"Lets create a large sprint plan to handle all of our
findings, especially getting FPS closer to 10, and making BOB a fully playable
stage, and WF be selectable from menu."*
**Standing policy carried forward:** functional correctness beats numerical
(§2b of the 2026-08-17 phase plan); recreate as Saturn-native where porting
fights the hardware (§2a); degrade to "Saturn shaped" as needed.

This plan synthesizes three same-day audits (all read-only, all grounded in
file:line evidence):

- **Audit A — functional-BOB gaps:** the simulation is essentially complete
  (the real `game_loop_one_iteration()` runs unmodified); the *presentation*
  is ~20-25% complete. 18 of 48 model variants drawable; camera is a
  fixed-yaw stub; every 2D overlay except the VDP2 counter HUD is discarded.
- **Audit B — second level + W7:** terrain is compile-time C, not scene-package
  data, so the pause-menu switcher's first move is dual-linking two levels'
  banks (Shape 1), not CD re-staging. WF is far cheaper than the castle lobby
  and is what `PRODUCT_GOAL.md` already names. The ordinal-keyed material
  tables are the single critical-path tool item.
- **Audit C — performance:** the 6.90 → 5.00 FPS closure regression (55.2 ms)
  is closed to within noise by three mechanisms, the largest being that the
  fast depth kernel requires unit scale and **every closure actor fails that
  precondition**, falling onto a path this project already measured at
  36x the cycles (3,725.8 vs 102.9 per position visit, sprint2-t2_6 report).

**Accepted-for-now defect (owner, 2026-08-22):** the Bob-omb body still does
not display. Tracked in S1.6/S6.1 — the stale-Gouraud-table fix is on the
sprint path anyway and is the leading suspect.

---

## 0. The shape of the sprint

Seven workstreams. S0 and the S3.0 re-keying are prerequisites that everything
else queues behind; S1 (perf) and S2 (visual completeness) then interleave in
measured steps so cadence never regresses silently, with **S7 paying for S2's
added load**; S4 makes BOB *play*; S5 delivers WF behind a pause menu.

```
S0 hygiene gates ──┬─► S1 perf recovery ──► S1.10/S1.11/S1.12 structural levers
                   │
S3.0 re-keying ────┼─► S2 bake batches (star, enemies, trees, props)
                   │            ▲  │
                   │   S7 pays ──┘  ▼
                   │  for S2   S4 camera + dialog/pause overlay
                   │
                   └─► S5 WF bake ──► S5 dual-link + menu (W7 Shape 1)
```

**S7 is the content-side counterweight and it is not optional.** Every S1
figure was measured on a scene holding roughly one Bob-omb and one coin.
S2 populates the level — 17 trees, 13 signposts, ~16 prop families, Goombas,
Koopas, the star, shadows on everything — and actor cost scales close to
linearly with instances. Without S7, S2 can plausibly consume the whole of
S1's gain. S7 cuts cost per instance and per primitive so the two balance.

**Cadence rungs are quantized.** The presentation loop grants simulation
credit in whole field pairs, so reachable frame rates are 60/k:
5.00 (k=12) → 5.45 → 6.00 → 6.67 → 7.50 → 8.57 → **10.00 (k=6)**.
Budget ~60% conversion of removed cycles into frame time (measured on W2).

**FPS arithmetic, stated honestly.** Recovering the full closure regression
returns us to ~6.9 → rung **6.67**. T1+T2 alone are worth ~47 ms of the
55 ms and are low-risk. Reaching **7.50** needs the mechanical tail
(T3/T4/T5/T8) plus the seam scans (T9). **The rungs past 7.50 stand on the
three structural levers** (S1.10 native geo walk, S1.11 slave utilization,
S1.12 VDP2 ground plane):
the walk's recoverable half is worth roughly a rung on its own, the slave's
re-measured idle sets the ceiling for everything offloaded, and T6 is one
tenant of S1.11, not the whole answer. **10.00 means halving today's frame
time (200 → 100 ms); it is the sprint's stretch rung, not a promise** — but
it is now a rung with named levers under it rather than a wish.

**On 10 FPS with a FULL playable BOB, stated plainly:** the S1 ladder above
targets **7.50 confidently and 8.57 plausibly**, and every figure in it was
measured with about one Bob-omb and one coin on screen. A populated BOB is
several times that primitive load. **S1 alone does not reach 10 on a full
stage — S7 is what makes 10 arithmetically reachable rather than
aspirational**, because it attacks cost per primitive and per instance,
which is the term that grows with content. The three structural levers help
here too: geo-walk cost scales with object count, so deleting it pays MORE
on a populated level; the VDP2 ground plane removes fill and CPU together
exactly where props cluster; slave headroom is a fixed pool more work can
move into. Each S2 bake batch still adds VDP1/master work that must be paid
for as we go — which is why S1, S2 and S7 interleave with measurements.

---

## S0 — Gates and hygiene (first; everything queues behind these)

| # | Task | Mechanism / file | Effort |
|---|---|---|---|
| S0.1 | **The one measurement that steers the sprint:** `capture_idle_attribution.py` on the closure build at ticks 30 and 150, `--attribute-callers-of` `_actor_saturating_mul_i64` / `___divdi3` / `_demo_generic_actor_world_vertex` **plus `_geo_process_node_and_siblings` / `_saturn_mtxq_refresh_float_mirror` / `_alloc_display_list`**, plus the master×slave contingency table | one capture prices all three structural questions: confirms Audit C's accounting, **prices the discarded N64 geo walk (S1.10 go/no-go)**, and sizes today's slave idle (S1.11 budget; the 79% figure is T2.16, pre-closure — re-measure, never assume) | capture run |
| S0.2 | **DONE 2026-08-22 (`d34574a8`, `id-333e380c68636a58`).** Instance-level skip instead of frame-level `return false` in `demo_generic_actor_emit`/`prepare` — one failing bank/residency lookup currently drops the whole frame | `saturn_demo_render.c:4288-4295` and the prepare loop; add skip telemetry. Violates ROADMAP M2's "never stall the complete scene" as-is, and **must land before any bake batch grows the roster** | small RT |
| S0.3 | **Raise/handle `SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE=64`** — the observer hard-fails the entire capture on overflow rather than degrading | `saturn_actor_instance.h:14`, `saturn_actor_instance.c:111-114`; degrade = drop farthest, count it | small RT |
| S0.4 | Fix build-config drift: `build-sourceboot.ps1` still defaults `DYNAMIC_ACTOR_CLOSURE=0`; make the coins profile the default profile | `tools/saturn/build-sourceboot.ps1:70`, `tools/saturn/profiles/` | one-liner |
| S0.5 | Correct STATE.md: HUD is live (not "outstanding"), glyphs are cart-resident (not the HWRAM figure quoted) | `STATE.md:33-34,85-87` | doc |
| S0.6 | **DONE 2026-08-22 — and the answer is that the instrumentation is DEAD, not that the wait is zero.** All four tick counters (`vdp1_overwrite`, `vdp1_terminal_fence`, `gouraud_scu_dma`, `command_cpu_dmac`) are hardcoded `= 0U` every frame in `main.c:1680,1853,1855` (the terminal fence is additionally behind `SATURN_DIAGNOSTIC_MODE != 0`, off in shipped builds). **Reporting 0% would have been a false negative.** What IS live: `vdp1_bank_late_dma` at **0.98/render-frame** and `vdp1_transfer_queued_not_started` at **1.00/render-frame** over a 49-frame window — i.e. VDP1 is still busy from the previous frame essentially every frame, while `vdp1_bank_overwrite_attempts` stays 0 (soft deferral, never outright starvation). Circumstantial evidence that VDP1 throughput brushes the cadence; NOT proof it dominates. Superseded by S0.8. Original text: read the VDP1-wait counters that already exist — `vdp1_overwrite_wait_ticks_accum`, `vdp1_terminal_fence_wait_ticks_accum`, `gouraud_scu_dma_wait_ticks_accum`, `command_cpu_dmac_wait_ticks_accum` (all in `sm64_saturn_fast3d_profile_t`; peek live via the host-offsetof method, no rebuild) | establishes whether/when VDP1 fill is the wall. Today T2.16 says the master is CPU-bound, so S1.12's payoff grows exactly as S1 relieves the CPU — this reading, repeated after S1.1/S1.2 land, tells us when the fill wall arrives and sizes S1.12's win before any VDP2 work is committed | counter peek |
| S0.8 | **CANCELLED as specified — there is no wait to time — and REPLACED.** Verified two ways. (1) ELF: `vdp1_sync_wait`, `scu_dma_wait` and `cpu_dmac_wait` are **absent from the shipped binary** (`sh-elf-nm` on `id-333e380c68636a58`); only the non-blocking poll `vdp1_sync_busy` and `..._transfer_deferred` are linked. (2) W0's own design spec (`docs/superpowers/specs/2026-08-17-vdp1-overwrite-fence-design.md:246-247`) states outright: *"fence/overwrite-wait tick fields are zero because the runtime performs no wait in this path."* Wiring them would produce correct zeros. **Replacement task: measure `previous_frame_reuse_count`** (`saturn_frame_pipeline.c:182,231,255,307`), which that same spec (`:255-257`) names as *"the product-level count of missed presentation opportunities"* — the number that actually decides whether VDP1 gates cadence | counter peek |
| S0.7 | **HWRAM waste audit (owner-directed), scoped against the FULL port** — three items the residency audit flags as N64-isms burning 32-bit RAM: (a) **`gGfxPools` 32,844 B — S0.7a PROVED THIS CANNOT BE RECLAIMED EARLY; the "shrink it now" assumption was WRONG.** Findings: it is **not** double-buffered on Saturn (`GFX_NUM_POOLS` is 2 only under `TARGET_N64`, else 1 — `src/buffers/buffers.h:32-37`), and the 32,768 B `Gfx buffer[]` is **not dead memory**: it is a live *same-frame* CPU scratch heap the geo walk reads back constantly (master-list matrices via `DisplayListNode.transform`, generated-list geo callbacks, `obj_orient_graph`'s `throwMatrix`). `exec_display_list` is indeed never reached on the demo path, so the pool's *original RSP purpose* is dead — but the arena's *current* role is load-bearing. Two hard blockers on shrinking: the **top-down `Mtx`/viewport allocation peak was never measured** (T2.1 measured only the bottom-up `Gfx` side at 443/4096; both directions share the one array), and **several call sites null-deref rather than degrade on exhaustion** (`level_geo.c:37-42`, `skybox.c:247,293` — both plausibly live in BOB). **Reclaimable today: the 76 B `spTask` member only** (write-only dead weight, matching the existing `gGfxSPTaskStack`/`YieldBuffer` `[1]`-byte stub precedent at `buffers.c:24-26`). **The full 32,844 B still comes back — with S1.10, not before.** No cross-frame pool read was found; one inherited vanilla stale-`throwMatrix`-on-area-mismatch pattern is noted for the walk rewrite and is independent of pool size; (b) **the dbgio trio ~7,950 B** (`__vt_parse_state_table` + `__state` + `__default_font_cg`) — a VT100 parser and debug font resident in a retail-shaped build; keep boot logging, move the tables cold/LWRAM; (c) **`gMatStack` 2,048 B float twin** of `gMatStackQ` — dies with S1.10. **Full-port framing is the audit's second mandate:** every residency decision is judged against what fifteen courses + castle + the full actor roster + saves will need resident, not against the BOB demo — e.g. the `gObjectPool` fix at full scale is the **struct diet** (608 B of union'd float fields per object, W6b territory), NOT an LWRAM demotion that S5.5's dual-linked terrain and future levels will need the room for; and do NOT demote `gGfxPools` into LWRAM space the full port needs — shrink or delete it. **Recoverable NOW is ~8 KB (dbgio trio + spTask), not the ~40 KB first estimated** — the 32,844 B of `gGfxPools` is real, in-use scratch until S1.10 deletes its consumer | audit DONE for (a); (b)/(c) remain | audit + small RT |

## S1 — Performance recovery (Audit C, in its recommended order)

Every task cites Audit C's target id. Measure cadence (ticks 30/150/210,
window sweep — single-tick readings mislead) after each landing.

| # | Target | Mechanism | Expected | Risk |
|---|---|---|---|---|
| S1.1 | **T1 — unit-scale gate** | Fold instance scale into the pose root joint matrix (`saturn_actor_pose.c:140-152`) so `transform.scale_q16` is unit and the existing fast depth kernel (`saturn_actor_meshlets.c:541-569`) lights up. Bound int16 saturation for growing scales (explosion) | **~+1.0 FPS**, grows with actor count | med (saturation bound) |
| S1.2 | **T2 — `demo_generic_actor_world_vertex`** | Hoist per-corner sin/cos to per-instance; replace 4 saturating 64-bit muls with plain `dmuls.l` widening under per-instance bounds (the T2.6 §4 pattern); transform each admitted position once via `output->positions[]` instead of 4 corners × primitive | **~+0.5 FPS** | low-med |
| S1.3 | **T3 — depth carry for generic banks** | Pass a (non-Mario-aliasing) carry instead of `NULL` at `saturn_actor_meshlets.c:1002-1006` | halves the depth walk; cheap insurance after T1 | low |
| S1.4 | **T4 — resolve dedup** | Delete the duplicate variant binary search inside `_runtime_resolve`; delete resolve #1 in prepare (it reads one u32 — cache `draw_records_per_instance` per model); optional per-frame resolved-bank cache keyed `(family_id, model_id)` invalidated on `residency_generation` | ~30k cycles/frame at 2 instances, scales linearly | low |
| S1.5 | **T5 — pose cache + staging** | 17 of 18 banks are 1-joint/1-animation: their pose is a bank constant. Stamp-skip on `(bank_id, anim, frame)`; stage invariant vertex/joint slices to LWRAM (the `4bfe4afb` Mario precedent — `_pose_evaluate_staged` already exists); hoist the 16-B animation-record re-read out of the per-channel loop | ~120k cycles/frame | low-med |
| S1.6 | **T8 — stop DMA-ing unwritten Gouraud tables** | Advance `gouraud_bank->used` by `gouraud_ordinal` (actual writes), not the full per-instance reservation (56 for a Bob-omb) | small cycles; **removes the stale-table hazard = leading invisible-body suspect** | low |
| S1.7 | **T9 — geo-seam scans** | `saturn_source_object_pool_slot` O(208) scan → pointer subtraction; `saturn_source_model_id` O(255) scan → reverse map built at level load (`rendering_graph_node.c:93-111`) | ~70k cycles/frame; pre-existing (not part of the 2 FPS) | low |
| S1.8 | **T7 — switch filter into the bake** | Emit per-case primitive lists so unselected switch cases never produce output records. Today a coin reserves 16 records where 2 draw — 8x record-budget pressure that bites at a screenful of coins. **Depends on S3.0** (touches the pinned tables) | record budget, future-proofing | med |
| S1.9 | **T6 — slave offload of the process phase** | `drain_slave` + the second workspace lane exist unwired. **Go/no-go from S0.1's contingency table only.** Emit stays master-owned | up to the whole process phase | **high** |

**NEW FINDING (S0.8 replacement measurement): the simulation is being starved.**
`dropped_sim_tick_credits` runs at **5.164 per rendered frame** (377 over 73
frames). A frame spans 12.33 VBlanks, so roughly six 30 Hz sim-tick credits
accrue per frame while the pipeline's normal-plus-recovery budget authorises at
most two — the rest are discarded by the bounded-state safe pause. This is a
*consequence* of slow rendering, not an independent cause, but it has a
gameplay-feel dimension raw FPS hides: game logic advances far less per
wall-clock second than intended. **Every frame-time reduction in S1 recovers
sim credits too**, so cadence work pays twice. Re-measure alongside cadence at
each S1 landing.

### The three structural levers (added 2026-08-22, owner-directed)

Everything above tunes the Saturn renderer. These three attack what the
frame leaves on the table — the N64 renderer we still run every frame, the
CPU we barely use, and the video processor we barely use. They are what the
8.57 → 10.00 rungs actually stand on.

| # | Target | Mechanism | Expected | Risk |
|---|---|---|---|---|
| S1.10 | **Replace the discarded N64 geo walk with a Saturn-native walk** | Every frame still runs the full `rendering_graph_node.c` traversal and display-list build, then `display_suppressed` (`game_init.c:450`) discards the output — two renderers paid per frame. The walk cannot be *skipped* (W1: it feeds animation, camera and the observer; its 18.18% headline was withdrawn as *removable*, not as *sized*), but it can be *recreated* per §2a: a Q16-only walk computing ONLY what has consumers — observer state (the registry seam IS the consumer list), animation advance, camera — with no float mirror (`saturn_mtxq_refresh_float_mirror` burns 16 `q16_to_float` per matrix write), no `alloc_display_list`, no matrix work for subtrees nothing reads. Prior art: `docs/superpowers/plans/2026-08-06-saturn-iterative-geo-walk.md`. **Acceptance criterion (owner ruling 2026-08-22): `gGfxPools` is deleted, not replaced — it is the N64 rendering ABI itself (each pool carries an RSP `SPTask`), and no successor arena is ever built.** Its 20 client files are the accounted inventory: the walk's 17 `alloc_display_list` sites die here; shadows → S2.5; HUD/text/menu → S4.2; fades → S4.4; envfx snow/bubbles → effect emitters (the pipeline already draws baked effect quads); `moving_texture.c` water surfaces and `paintings.c` are the two genuinely new full-port emitters, targeting the EXISTING VDP1 command arena; `geo_misc`/`mario_misc`/`level_geo`/`bowser`/`obj_behaviors` are per-case calls in the S0.7 audit annex. | region upper bound ~18% of master; if half is recoverable, **~18 ms/frame ≈ one full rung**, compounding with every actor S2 adds | **high** — the W1 failure is the cautionary tale; land behind a tick-by-tick equivalence oracle on observer/animation/camera state against the shipped walk |
| S1.11 | **Spend the slave's idle cycles (T6 is one tenant, not the answer)** | T2.16 measured the slave **79% idle** with the master near-saturated — close to half the machine's combined cycles unused. This workstream owns the budget; tenants claim from it. Priced tenant: the **terrain depth-bin offload, +4.3% / ~0.367 VB** (STATE.md's own post-T2.17 ranking; dropped from this plan's first draft by omission). Second tenant: T6 (actor process phase). Candidate third, after S1.10: slave-side portions of the native walk. Gate on every tenant: no SH-2 cache coherency — slave-visible state goes through the uncached mirror or explicit lines; the existing terrain master/slave split is the pattern to copy. | depth-bin offload alone ≈ +4.3%; the ceiling is S0.1's re-measured contingency table | med per tenant; T6 stays **high** |
| S1.12 | **Put VDP2 to work: the ground on an RBG0 rotation plane** — **SETTLED BY MEASUREMENT: the benefit is CPU-side, not fill relief.** The master never blocks on VDP1 (0.000% DMA wait, no blocking primitive linked), which first looked like "VDP1 is not the wall". That inference was WRONG: W0's spec (`:24-40`) states that when VDP1 is still plotting the runtime defers, and *"the current presentation opportunity is consumed in the scheduler as previous-frame retention"*, with simulation, input and audio **pausing** once the two-tick budget is exhausted. `vdp1_bank_late_dma` fires at **1.02/frame** — VDP1 is still busy at essentially every frame boundary. So That deferral mechanism is real but **never fires in practice**: measured on `id-333e380c68636a58` over 73 rendered frames, `previous_frame_reuse_count` delta = **0** — VDP1 costs **zero** presentations. `vdp1_bank_late_dma` at 1.000/frame is VDP1 still drawing while the CPU *starts building* the next frame, which is pipelining working as designed, not a stall. **VDP1 is therefore NOT the limiter; the master CPU is.** S1.12 keeps its full value but for the honest reason: moving ground quads to VDP2 deletes their entire master-CPU pipeline cost (transform, admission, depth bin, command build, DMA) out of 540 triangles/frame emitted. Do not justify it as fill relief. | VDP2 today draws only the sky bitmap (NBG1) and the HUD tilemap (NBG0); **VDP1 draws every polygon in the scene, including the largest fill consumers — the ground quads**. RBG0 is a hardware-rotated, perspective-correct textured plane (coefficient tables give true 3D slope, not mode-7), and it is THE canonical Saturn technique: both reference engines in `work/upstream/` (SlaveDriver/PowerSlave, Sonic Z-Treme) render their ground this way — GPL, project-compatible per standing owner ruling, `src/port/saturn/gpl/` convention for any copied code. Every ground quad moved to VDP2 is a **double win**: VDP1 fill time freed AND the master CPU cost of that quad (transform, admission, depth bin, command build, DMA) deleted outright. BOB caveat stated honestly: the terrain is hilly — the mountain stays on VDP1; the candidate set is the flat-ish base ring and low-slope clusters, selected at bake time by a cluster flatness test. Interaction with S2/S4: the same VDP2 budget must still fund dialog/pause (S4.2), so plane + text layers are planned together. | removes the largest VDP1 fill items and their full CPU pipeline cost; sized by S0.6 before any commitment | med-high (VDP2 VRAM/bandwidth budget vs NBG0/NBG1; per-line coefficient table; bake-side cluster split) |

**Anti-target:** the soft-float tail (Audit C T10). Ranks 2-5 total 7.8% over
962 functions. Do not spend sprint time there; W6b remains a separate program.

**Milestone S1-A (owner check):** route rung ≥ 6.67 with coins + Bob-ombs on.
**Milestone S1-B:** rung 7.50 after the mechanical tail.
**Stretch:** 8.57 on the structural levers (S1.10/S1.11/S1.12); 10.00 only if
both land AND S2's added load is paid for.

## S3.0 — Re-key the material/digest tables (critical path for S2 AND S5)

The four pinned tables (`BOB_DIRECT_TEXTURED_KEYS`, `_EXPECTED_CAPTURE_HASHES`,
`_EXPECTED_CATEGORY_HASHES`, `_EXPECTED_TRANSFER_PREFIXES`,
`actor_material_v2.py:33-157`) are keyed by **1-based family ordinal within
one closure report**, and `compile_actor_family_bundle.py:303-304` hard-pins
`len(families) == 47`. Adding ONE family (a tree) renumbers everything;
a second level can never compile.

- Re-key on the ordinal-independent **`family_key`** that already exists in
  the reports (validated at `compile_actor_family_bundle.py:322-324`).
- Replace the literal 47 with a per-level expected-family manifest.
- Parameterize `PROFILE`/`BUNDLE_NAME`/`DEPENDENCY_NAME`/`_scene_package_bytes`
  and the `13/13/14` census literals (`Makefile.saturn.mk:1279`).
- Update the pinned tests (`test_actor_material_v2.py:133`,
  `test_compile_actor_family_bundle.py:182-193`) — and triage the **12
  pre-existing failures + 1 error** in `test_actor_material_v2` while in there.

**Do this BEFORE any bake batch** — otherwise the ordinal re-derivation is
paid on every batch instead of once. Effort: the largest single tool item in
the program (Audit B), but pure host Python.

## S2 — BOB reads as BOB (bake batches, each behind a cadence measurement)

| # | Batch | Contents | Blockers removed | Effort |
|---|---|---|---|---|
| S2.1 | **Billboard observation** | one `observer_record_billboard` call + observer field; today `billboard_state` is copied but never SET, so billboarded actors (Goomba, star, 1-Up, heart, coin sparkle) would draw with fixed model-space orientation | `rendering_graph_node.c:1078` feeds only the discarded walk | small RT |
| S2.2 | **Enemy + star keys** | Star `(0x007A)`, Goomba `(0x00C0)`, Koopa + flag + shell, Bob-omb Buddy — all confirmed to compile once keyed (r2 §6). Pin fresh capture digests; budget against the 27,808 B unclaimed VDP1 pool | `BOB_DIRECT_TEXTURED_KEYS` allowlist | bake batch |
| S2.3 | **Trees** | `collect_scene_closure.py` learns the 3 `SPECIAL_OBJECT*` forms from `collision.inc.c` (17 trees; preset `0x79` → `{MODEL_BOB_BUBBLY_TREE, bhvTree}`); geometry is trivial (one DL + culling radius, reduction already landed) | collector never opens collision.inc.c | tool + bake |
| S2.4 | **Props** | census-A one-liner (12 `GEO_CULLING_RADIUS` families) + ~16 material keys: seesaw, elevator, chomp gate, purple switch, bars, posts, breakable/exclamation boxes, **13 signposts**, water bombs, flames, heart, 1-Up | census + allowlist | one-liner + bake batch |
| S2.5 | **Blob shadows** | one depth-cue billboard sprite per actor from `shadow_type/scale/solidity` already in the snapshot; Mario's from `floorHeight` (T2.14 costed this; owner already chose painted-sprite shadows) | nothing draws `GEO_SHADOW` | small RT |
| S2.6 | **Special cases (decide, not default):** bowling ball (`GEO_SCALE` range one-liner), King Bob-omb (`GEO_ASM` needs a reduction policy decision), Chain Chomp (multi-root joints — real evaluator work; **candidate to defer**), held objects (`GEO_HELD_OBJECT` — Bob-omb carrying is core to act 1; small-to-medium draw-path change) | various | per-item |

**Milestone S2 (owner check):** stand at BOB spawn and see star, Goomba,
trees, signposts, gates — "the level reads as BOB." Measure cadence before/after
every batch; if a batch costs a rung, it waits for S1 headroom.

## S4 — BOB plays as BOB

| # | Task | Mechanism | Effort |
|---|---|---|---|
| S4.1 | **Camera control** | Plumb C-button/analog into the existing `input_yaw`/`input_pitch` args (`saturn_camera_role.c:61` passes literal `0,0`); call `update_camera_hud_status(c)` before the early return (unfreezes the camera HUD icon). ~15 lines. Full Lakitu port is explicitly OUT of this sprint | small RT |
| S4.2 | **Dialog + pause on VDP2** | The game pauses today with no visible menu — a soft-lock to the player. Reuse the HUD atlas/layout path (20×14 cell grid is enough for dialog text) for: pause text, dialog boxes (Bob-omb Buddy conversations, course intro), star-get card, course name | new subsystem (the largest S4 item) |
| S4.3 | **View radius** | Raise `SATURN_DEMO_VIEW_RADIUS 6000` toward the mountain-visible value; it is a deliberate perf lever, so land with a cadence measurement | one-liner + measure |
| S4.4 | Warp/fade visibility | level exit currently loops invisibly; minimum: VDP2 fade at transitions | small RT |

**Milestone S4 (owner check):** walk to the summit with camera control,
talk to a Bob-omb Buddy, collect the star, see the card — "BOB act 1 is
playable end to end."

## S5 — WF selectable from the pause menu (W7 Shape 1)

Order per Audit B; castle lobby is explicitly **out of this sprint** (Phase 3).

| # | Task | Key facts | Effort |
|---|---|---|---|
| S5.1 | Tool generalization: symbol-prefix through BSP/scene emitters; per-level texture table (retire the 567/510/24 pin); parameterize the four `bob_area1_*` Makefile inputs and the sourceboot `bob/1` paths | Audit B Phase 0.6-0.9 | tool (mechanical) |
| S5.2 | WF terrain bake | geo root is structurally identical to BOB's (flat DL list); `BOB_AREA_DIR=levels/wf/areas/1`, sky `cloud_floor.png` exists | config + bake |
| S5.3 | **WF spawn rules (~6-10, evidence-heavy)** | tumbling bridge, piranha plants, whomps, bullet bill (+cannon), tower group, splash spawner — each a source-attested rule per `_attested_capacity` | bake (the WF long pole) |
| S5.4 | WF closure + banks + bundle + profile | post-S3.0 all generic; product gate explicitly allows skipping unsupported WF actors **with visible telemetry** (add the skip counter) | bake + small RT |
| S5.5 | **Dual-link terrain (Shape 1)** | Replace `SM64_SATURN_BOB_*_COUNT` compile-time sizes with MAX() capacities + runtime counts; `s_bob_positions_active`/`s_bob_primitives_active` are ALREADY indirection pointers; swap BSP/cluster/admission/tile tables together. **Measure the cart delta first** (~1.67 MB free; WF is smaller than BOB) | RT (the biggest S5 item) |
| S5.6 | Scene teardown + respawn | drive the ALREADY-LINKED two-slot `saturn_scene_residency` machinery (zero callers today) + pose-pool `release_owner`; respawn Mario at the WF entry warp | RT (medium) |
| S5.7 | **Pause menu** | START is already mapped (`controller_saturn.c:37`); HUD atlas/layout/publish all live — the menu is a second VDP2 tilemap + a d-pad state machine. Smallest W7 item, not a peer of the others | small RT |

**Milestone S5 (owner check):** press START in BOB, pick Whomp's Fortress,
land in WF. Rebuild-free. (CD re-staging / Shape 2 is the follow-on program,
unlocked by S5.5's data-driven terrain either way.)

## S7 — Content-side cost reduction (the counterweight to S2)

Owner-approved earlier and **not yet landed**: mesh decimation at 75% with
per-material ratios. It was queued behind the actor-pipeline work and then
dropped from the first draft of this plan by omission. It is the missing
content-side lever: it cuts primitives at the source, so transform cost,
VDP1 command count and fill all fall together, and it compounds with every
item in S1.

| # | Task | Mechanism | Expected | Risk |
|---|---|---|---|---|
| S7.1 | **Terrain mesh decimation at 75%, per-material ratios** (owner-approved, unlanded) | Decimate the baked terrain mesh with a per-material ratio table so texture-critical surfaces keep density and flat/low-detail ones lose it. Owner already reviewed the visual result: **75% looks nearly identical; 50% does not** — 75% is the approved setting, not a starting guess. Lands in the bake, so it costs nothing at runtime | fewer primitives through transform, admission, command build, DMA and fill — the only lever that hits all five stages at once | med (visual acceptance is owner-gated; re-bake invalidates pinned terrain figures) |
| S7.2 | **Actor LOD by distance** | Bake 2 tiers per actor bank (full / reduced) and select per instance from the distance the admission path already computes. A signpost or tree at the far edge of a 6,000-unit view radius does not need its full primitive set. Pairs with S4.3 (raising the view radius) — LOD is what makes a larger radius affordable | cuts the per-instance term that S2 multiplies; strongest exactly when the level is populated | med (bake-side tier generation; tier selection must be stable to avoid popping) |
| S7.3 | **Distance-ordered pose-slot claiming** | The pose pool is first-come today, so under pressure a distant actor can hold a slot while a near one renders at bind pose. Noted in the pose-pool design (r2 §6 item 3) and never built. With S2's roster this becomes reachable, not hypothetical | correctness under load, not raw FPS — prevents the worst-looking failure mode | low-med |
| S7.4 | **Per-actor primitive budget in the manifests** | The Bob-omb manifest (`tools/saturn/manifests/actors/bobomb.json`) already declares and enforces a Saturn budget. Extend the pattern: every actor admitted in an S2 batch declares its primitive/texture/CLUT ceiling, and the build fails if a re-bake breaks it | stops S2 from silently regressing cadence one batch at a time | low |

**Sequencing rule:** S7.1 lands **before or alongside the first S2 bake
batch**, not after — decimating after the roster grows means re-baking
everything twice. S7.2/S7.4 land with the batch that first needs them.

**Milestone S7 (owner check):** a populated BOB (post-S2) holds the cadence
rung the empty BOB reached post-S1. That is the real test of whether the
counterweight works.

## S6 — Tracked but deferred (owner-accepted or out of sprint)

| # | Item | Status |
|---|---|---|
| S6.1 | **Bob-omb body invisible** | accepted for now; S1.6 (stale Gouraud DMA) is the leading suspect — re-test after it lands, else root-cause the generic gouraud upload path |
| S6.2 | Flat actor lighting (no form) | needs baked per-vertex light intensity in the S64B; the flat-replace path already tests measured corners, so it self-retires when data arrives |
| S6.3 | Castle lobby | Audit B Phase 3: room-scoped geo selection, 23-texture VDP1 budget measurement, solid-color background, hermetic texture path |
| S6.4 | Full Lakitu camera | S4.1's rotatable chase cam is the sprint deliverable |
| S6.5 | Chain Chomp multi-root joints | defer unless S2 leaves room |
| S6.6 | W6b Q16 simulation purge | separate program; do not chase inside this sprint (Audit C T10) |

---

## Process rules for this sprint

1. **Measure after every S1 landing and before/after every S2 batch** —
   cadence at ticks 30/150/210, window sweep, on sealed builds only.
2. **Owner acceptance at the four milestones** (S1-A, S2, S4, S5); no
   milestone advances on my say-so.
3. **Build preservation + CHANGELOG discipline** per global rules; every
   behavior-changing commit carries its entry.
4. **S3.0 lands before any table-touching work** (S2 batches, S1.8, S5.4).
5. Two-hour rule: any task that stalls twice gets re-chartered, not pushed.
6. **Full-port residency principle (owner, 2026-08-22):** HWRAM/LWRAM/cart
   placement decisions are judged against the FULL port's needs — fifteen
   courses, the castle, the complete actor roster, saves — not against what
   the BOB demo happens to fit today. Prefer deleting waste (gGfxPools, the
   float mirror) over demoting it into tiers future levels will need, and
   prefer structural diets (the object struct) over migrations that merely
   relocate an N64-shaped cost.
