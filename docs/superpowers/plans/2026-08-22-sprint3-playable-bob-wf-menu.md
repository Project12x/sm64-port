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

Six workstreams. S0 and the S3.0 re-keying are prerequisites that everything
else queues behind; S1 (perf) and S2 (visual completeness) then interleave in
measured steps so cadence never regresses silently; S4 makes BOB *play*; S5
delivers WF behind a pause menu.

```
S0 hygiene gates ──┬─► S1 perf recovery (T1..T8) ──► S1 stretch (T6/T7/T9)
                   │
S3.0 re-keying ────┼─► S2 bake batches (star, enemies, trees, props)
                   │            │
                   │            ▼
                   │       S4 camera + dialog/pause overlay
                   │
                   └─► S5 WF bake ──► S5 dual-link + menu (W7 Shape 1)
```

**Cadence rungs are quantized.** The presentation loop grants simulation
credit in whole field pairs, so reachable frame rates are 60/k:
5.00 (k=12) → 5.45 → 6.00 → 6.67 → 7.50 → 8.57 → **10.00 (k=6)**.
Budget ~60% conversion of removed cycles into frame time (measured on W2).

**FPS arithmetic, stated honestly.** Recovering the full closure regression
returns us to ~6.9 → rung **6.67**. T1+T2 alone are worth ~47 ms of the
55 ms and are low-risk. Reaching **7.50** needs the mechanical tail
(T3/T4/T5/T8) plus the seam scans (T9). Reaching **8.57** most likely needs
the slave offload (T6) — gated on the S0.1 measurement. **10.00 means
halving today's frame time (200 → 100 ms); it is the sprint's stretch rung,
not a promise**, and each S2 bake batch adds VDP1/master work that must be
paid for as we go — which is why S1 and S2 interleave with measurements.

---

## S0 — Gates and hygiene (first; everything queues behind these)

| # | Task | Mechanism / file | Effort |
|---|---|---|---|
| S0.1 | **The one measurement that steers the sprint:** `capture_idle_attribution.py` on the closure build at ticks 30 and 150, `--attribute-callers-of` `_actor_saturating_mul_i64` / `___divdi3` / `_demo_generic_actor_world_vertex`, plus the master×slave contingency table | confirms Audit C's cycle accounting AND decides whether T6 (slave offload) is worth its risk | capture run |
| S0.2 | **Instance-level skip instead of frame-level `return false`** in `demo_generic_actor_emit`/`prepare` — one failing bank/residency lookup currently drops the whole frame | `saturn_demo_render.c:4288-4295` and the prepare loop; add skip telemetry. Violates ROADMAP M2's "never stall the complete scene" as-is, and **must land before any bake batch grows the roster** | small RT |
| S0.3 | **Raise/handle `SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE=64`** — the observer hard-fails the entire capture on overflow rather than degrading | `saturn_actor_instance.h:14`, `saturn_actor_instance.c:111-114`; degrade = drop farthest, count it | small RT |
| S0.4 | Fix build-config drift: `build-sourceboot.ps1` still defaults `DYNAMIC_ACTOR_CLOSURE=0`; make the coins profile the default profile | `tools/saturn/build-sourceboot.ps1:70`, `tools/saturn/profiles/` | one-liner |
| S0.5 | Correct STATE.md: HUD is live (not "outstanding"), glyphs are cart-resident (not the HWRAM figure quoted) | `STATE.md:33-34,85-87` | doc |

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

**Anti-target:** the soft-float tail (Audit C T10). Ranks 2-5 total 7.8% over
962 functions. Do not spend sprint time there; W6b remains a separate program.

**Milestone S1-A (owner check):** route rung ≥ 6.67 with coins + Bob-ombs on.
**Milestone S1-B:** rung 7.50 after the mechanical tail.
**Stretch:** 8.57 post-T6; 10.00 only if S2's added load is paid for.

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
