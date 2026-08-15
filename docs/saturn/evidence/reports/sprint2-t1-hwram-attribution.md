# Sprint 2 Task 1 — HWRAM growth attribution and ranked reclamation candidates

- Date: 2026-08-15. Read-only analysis; no source changes, no builds.
- Tree: `.worktrees/saturn-recovery`, branch `saturn/recovery`, HEAD `6a7e4318`
  (tag `sprint1-r2-audio-clean`).
- Question answered: symbol-by-symbol, where did committed HWRAM go between
  the accepted A9A baseline and the accepted R1 candidate, and what is the
  ranked reclamation list that funds undoing the three-way split
  (`src/port/saturn/gfx/saturn_demo_render.c`, commit `49370e31`).

## Inputs and identity verification

| Artifact | Path | SHA-256 | Expected | Match |
| --- | --- | --- | --- | --- |
| Current accepted ELF (R1, `id-782c9c8f323a01a0`) | `build/saturn/sourceboot/e2-bob-identity-id-782c9c8f323a01a0/obj/sm64-saturn-sourceboot-e2.elf` | `d3c2bcaf903598e8eaf7c96ec83bcb2fd74c64602fdb144b1fac8aab7d7f2bd4` | same | YES |
| A9A baseline ELF (immutable oracle) | `.worktrees/sh2-native-math-purge/build/saturn/baselines/a9a-2026-08-05/sm64-saturn-sourceboot-e2.elf` | `1905ec8d42ea00ea2c000b5f53dd88f2079ffda8ceb67bcd5879e8e96acfc2e2` | same | YES |

The A9A baseline directory was found in the donor worktree
(`.worktrees/sh2-native-math-purge/build/saturn/baselines/a9a-2026-08-05/`);
it does not exist in the main checkout's untracked `build/` tree. Nothing in
the baselines directory was copied, moved, or modified.

The R1 feature tuple, from the accepted candidate's
`saturn-release-manifest-v1.json` (`effective_config`): level 9 (BOB),
`features/complete_mario_animation=0`, `features/dynamic_actor_closure=0`,
`semantic_audio=1`, `hot_promotion=1`, `object_pool_capacity=208`,
`renderer_pipeline=4`, `slave_render=1`, `demo_path=1`, `live_input_mode=1`,
`experimental_skip_geo_walk=0`.

## Methodology (exact commands)

Toolchain: `D:/Code/RetroDev/sm64-saturn-port/work/yaul-install/bin/sh-elf-readelf.exe`
(same binutils the map gate `tools/saturn/verify_sourceboot_memory_map.py`
invokes).

```
sha256sum <each ELF>                                  # identity verification
sh-elf-readelf.exe -SW <elf>  > {cur,base}_sections.txt   # section headers
sh-elf-readelf.exe -sW <elf>  > {cur,base}_syms.txt       # full symbol tables
python diff_syms.py ...                               # scratchpad script
```

The diff script parses `readelf -sW` rows (`Num: Value Size Type Bind Vis
Ndx Name`), keeps `OBJECT`/`FUNC` symbols with a defined section index, maps
`Ndx` to section names via `-SW`, aggregates duplicate local names, and
classifies every symbol as NEW / REMOVED / GROWN / SHRUNK / MOVED across the
HWRAM boundary. HWRAM-resident sections were enumerated from
`src/port/saturn/sourceboot/sourceboot-cart.x` (`ram` region, origin
`0x06004000`, length `0xFC000`): `.text`, `.bootdata`, `.rodata`, `.data`,
`.bss`, `.uncached` (the `.uncached` VMA is the P2 mirror but its load
address and footprint are HWRAM). Parser note: `readelf` prints sizes
>= 0x10000 in hex (`0x20000`); the first parser pass silently dropped such
rows and missed `_sourceboot_vdp1_cmdts` — fixed before any numbers below
were taken. Local-symbol file attribution uses the preceding `FILE` symtab
entries (valid for locals only; globals were attributed by `grep -rn` in
`src/`).

## Section-level diff (from `readelf -SW`, bytes)

HWRAM (`ram` region):

| Section | A9A | Current | Delta |
| --- | ---: | ---: | ---: |
| `.text` | 529,032 | 539,896 | +10,864 |
| `.bootdata` | 147 | 647 | +500 |
| `.rodata` | 7,681 | 7,677 | −4 |
| `.data` | 40,224 | 34,172 | −6,052 |
| `.bss` | 444,880 | 437,392 | −7,488 |
| `.uncached` | 1,736 | 3,960 | +2,224 |
| **Net HWRAM** | | | **+44** |

`___end` (bss end + uncached): A9A `0x060FDED8`, current `0x060FDF08`
(+48 with alignment). Margins against top `0x06100000`: A9A
`hwram_remaining = 0x2128` (8,488), current `0x20F8` (8,440). Floor
`0x1F00` (7,936) → true slack today = **504 B**.

Context sections (not HWRAM):

| Section | A9A | Current | Delta |
| --- | ---: | ---: | ---: |
| `.cart_rodata` | 3,247,536 | 3,775,280 | +527,744 |
| `.lwram_cmdts` | 131,072 | 0 | −131,072 |
| `.lwram_bss` | 887,568 | 941,368 | +53,800 |
| `.lwram_actor_runtime` | 0 | 65,536 | +65,536 |
| `.lwram_geo_traversal` | 0 | 3,072 | +3,072 |

## Reconciliation against the known ~49.6–53.6 KB figure

The raw A9A→current HWRAM diff is **+44 B net**, not ~49.6 KB — as expected,
because the current ELF already holds the 54,080 B hot set in LWRAM. The
49,648 figure is stage-1's measured relief requirement, not a raw diff:
`docs/saturn/evidence/reports/sprint1-stage1-link-smoke.md:55` — at pool 208
with the full hot set in HWRAM the link overflowed `ram` by 41,712 B, plus
the `0x1F00` (7,936 B) heap-margin gate = **49,648 B (0xC1F0)**; counting
the 3,960 B `.uncached` that also must fit, the effective requirement was
53,608 B, and the 54,080 B eviction covered it with 472 B to spare (now
504 B at this HEAD).

Symbol-accounted composition of the `.bss` delta (−7,476 sym-accounted vs
−7,488 header; 12 B is section padding):

| Component | Bytes |
| --- | ---: |
| Moved IN from `.lwram_cmdts`: `_sourceboot_vdp1_cmdts` | +131,072 |
| Moved OUT to `.lwram_bss` — the accepted split (`_s_bob_hot_workarea` 43,776 + actor scratch 10,304) | −54,080 |
| Moved OUT to `.lwram_bss` — CPU-state/telemetry evictions (`_sourceboot_fast3d` 44,616, `_sourceboot_math_route_capture` 1,564, `_sourceboot_render_snapshots` 424, remainder ~1,372 in 4–320 B telemetry/queue items) | −47,976 |
| REMOVED (4 syms: `_s_source_cart_stage` 16,384, `_s_source_cart_file_entries` 448, `_sourceboot_sky_gradient` 448, misc 4) | −17,284 |
| SHRUNK: `_gObjectPool` 240→208 (145,920→126,464) | −19,456 |
| NEW (6 syms, largest `_sourceboot_exception_record` 100) + GROWN (`_s_demo_render_transaction` +128) | +248 |
| **Net** | **−7,476** |

Actor scratch = `_s_actor_queue_merge_ids` 2,576 + `_s_actor_gouraud_addresses`
2,576 + `_s_actor_gouraud` 2,576 + `_s_actor_slots` 1,288 +
`_s_actor_texture_slots` 1,288 = 10,304 exactly.

Other sections, symbol-accounted: `.text` +10,544 = new +26,126 (116 syms)
+ grown +1,454 − removed −1,296 − shrunk −7,566 (dominated by
`_geo_process_node_and_siblings` 6,196→856) − moved to `.cart_rodata`
−8,174 (init/load-path functions under the existing cart-cold rules);
header delta +10,864 (320 B is literal pools/padding not covered by
symbols). `.data` −6,048 = moved to cart −5,992 (`_MacroObjectPresets`
2,928, `_sCamBBH` 1,464, `_sCamCastle` 840, other `sCam*` tables) − 76 to
LWRAM + 20 new. `.uncached` +2,224 = one new symbol, `_source_scene_owner`.
`.bootdata` +500 = `_saturn_build_identity`.

Cross-check totals: NEW in HWRAM 125 syms net +28,990; REMOVED 9 syms net
−18,580; GROWN 32 syms +1,582; SHRUNK 7 syms −27,022; moved out of HWRAM
102,056 (`.bss`) + 8,174 (`.text`) + 6,068 (`.data`); moved in 131,072.

## What owns committed HWRAM today (current ELF, `.bss` >= 1 KB)

| Symbol | Bytes | Owner (file:line of decl) | Hot/cold |
| --- | ---: | --- | --- |
| `_sourceboot_vdp1_cmdts` | 131,072 | `src/port/saturn/sourceboot/main.c:763` (2×2048 cmdts, `SOURCEBOOT_VDP1_COMMAND_CAPACITY` at :726) | HOT — per-frame CPU-DMAC/VDP1 transport; linker script forbids LWRAM return |
| `_gObjectPool` | 126,464 | `src/game/object_list_processor.c:75` (208 × 608 B; override at `object_list_processor.h:26`) | HOT — game-loop object processing |
| `_gGfxPools` | 51,276 | `src/buffers/buffers.c:36` (`GFX_POOL_SIZE` 6400 × 8 B Gfx + SPTask, `src/game/game_init.h:15`) | HOT — master display list rebuilt per frame (`game_init.c:362-365`), decoded by fast3d frontend |
| `__private_pool` | 40,960 | libyaul `kernel/mm/internal.c:22,28` (`TLSF_POOL_PRIVATE_SIZE 0xA000`, hardcoded) | MIXED — libyaul-internal TLSF heap; usage high-water unmeasured |
| `_sourceboot_gouraud_staging` | 24,576 | `src/port/saturn/sourceboot/main.c:806` (2 × 1536 × 8 B tables) | HOT — DMA source, must stay HWRAM |
| `__peripherals_memb_memb_mem` | 7,448 | libyaul `scu/bus/cpu/smpc/smpc_peripheral.c:85` (14-peripheral MEMB pool) | COLD-ish — VBlank input parse, one pad in use |
| `_s_terrain_queue_merge_ids` | 6,936 | `src/port/saturn/gfx/saturn_demo_render.c:395` (`DEMO_CPU_WORK_CACHE`, deliberately HWRAM per split note :210-233) | HOT — scene-construction merge |
| `_gStaticSurfacePartition` / `_gDynamicSurfacePartition` | 6,144 each | `src/engine/surface_load.c:25` (collision spatial partition) | HOT — collision queries |
| `_s_primitive_depth` | 3,468 | `saturn_demo_render.c` (per-primitive inner-loop operand) | HOT by design (split kept it) |
| `__state` + `__sector` (cdfs) | 2,064 + 2,048 | libyaul `cdfs.c` | COLD after load |
| `_gMatStack` / `_gMatStackQ` | 2,048 each | `src/game/rendering_graph_node.c` | HOT — geo walk |
| `_s_render_work_order` / `_s_primitive_leaf_id` | 1,734 each | `saturn_demo_render.c` | HOT by design |
| `_gObjectListArray` | 1,664 | `src/game/object_list_processor.c` | HOT |
| `__oreg_buf` | 1,568 | libyaul `smpc_peripheral.c:72` | COLD-ish |
| `_s_spatial_node_seen` | 1,183 | `saturn_demo_render.c:270` (BSP traversal guard) | HOT |
| `_D_8033A160` | 1,024 | `src/game/area.c:31` (`gLoadedGraphNodes`) | HOT-ish |

`.data` >= 256 B: `_gSineTable` 20,480 + `_gArctanTable` 2,050
(`src/engine/math_util.h:20-27`; `sins`/`coss` macros used across game-loop
code and `sm64_saturn_atan2_lookup_q16` under `atan2_variant=2` — HOT),
`_sCreditsSequence` 368, `_sParticleTypes` 304, `_gDialogCharWidths` 256.

## Attribution of NEW/GROWN HWRAM symbols >= 256 B (A9A → current)

All always-on under the R1 tuple unless noted. Subsystem determined by
`grep -rn` in `src/`.

| Symbol | Bytes | Sec | Subsystem | Hot/cold |
| --- | ---: | --- | --- | --- |
| `_sm64_saturn_scene_admit_with_scratch` | 2,376 | .text | scene residency | per-frame admission — HOT |
| `_source_scene_owner` | 2,224 | .uncached | scene residency (cross-CPU shared record, `DEMO_CROSS_CPU_SHARED` pattern) | HOT; must stay uncached HWRAM |
| `_saturn_geo_enter_object` +16 more `saturn_geo_*` handlers | ~7,900 total | .text | geo-walk runtime (replaces `_geo_process_node_and_siblings`, which shrank −5,340) | HOT — per-frame walk |
| `_actor_meshlet_core` / `_actor_meshlet_live_depth_bounds` / `_actor_meshlet_span` | 2,316 | .text | actor runtime (meshlets) | HOT |
| `_sm64_saturn_actor_instances_capture` / instance-bank family (~12 syms) | ~2,700 | .text | actor instance banks | capture/publish per frame — HOT (uncertain for recycle/quarantine paths) |
| `_sm64_saturn_actor_bank_open_validated` + `_read_variant_record` + `_sm64_saturn_actor_bundle_resolve` + validators (`_publication_entries_valid`, `_test_bounds`, `_publication_valid`, `_texture_binding_valid`, `_sm64_saturn_actor_bundle_variant`) | ~2,308 | .text | actor bank open/validation | COLD if only on bundle open (`saturn_actor_bundle.c:363`); **uncertain** — `saturn_actor_texture_residency.c:299` also calls resolve and its frequency was not proven here |
| `_sm64_saturn_hud_layout_build` + `_hud_atlas_upload_pattern` + `_sm64_saturn_hud_publish` + `_sm64_saturn_hud_atlas_write_cell` | 1,162 | .text | HUD | publish path (`saturn_hud_publish.c:12`) — treat as HOT |
| `_demo_actor_lower_compat_wrapper` / `_demo_actor_admit_compat_wrapper` | 1,136 | .text | render pipeline (compat) | HOT |
| `___divdi3` + `_actor_saturating_mul_i64` + `_actor_saturating_add_i64` + `_sm64_saturn_div_s64_s32` growth | 1,052 | .text | libgcc pulled in by actor 64-bit saturating math | HOT |
| `_saturn_geo_state_observer_*` (7 syms) | ~756 | .text | geo-walk state observer | HOT |
| `_demo_render_finalize` (+512) / `_demo_render_prepare_publish` (+272) / `_main` (+168) | +952 | .text | render pipeline / sourceboot | HOT |
| `_saturn_build_identity` | 500 | .bootdata | build identity record | COLD (but bootdata is pre-cart-load by design — not movable) |
| `_sm64_saturn_geo_walk_runtime_run` / `_next` | 616 | .text | geo-walk runtime | HOT |

## Destination capacity (owner directive: cart is first-class)

- **Cart (A-bus DRAM, 0x22400000, 4,194,304 B):** verified from the accepted
  candidate: `cd/SOURCE.DAT` = **3,775,280 B**, byte-identical to the
  `.cart_rodata` section size (0x399B30). Free cart = **419,024 B (~409 KB)**.
  NOTE: the tasking quoted "~4,045,792 of 4,194,304 (~148 KB free)"; that
  does not match this candidate's SOURCE.DAT — the verified figure is
  3,775,280/419,024. Cart placement is valid ONLY for boot/cold data and
  cold code (A-bus per-frame reads are how the LWRAM-class regressions
  happened); every hot-marked row above is cart-ineligible.
- **LWRAM:** sections end at `0x002F6940` (`.lwram_bss` 941,368 +
  `.lwram_actor_runtime` 65,536 + `.lwram_geo_traversal` 3,072). To-top
  (`0x00300000`) margin = **0x96C0 (38,592 B)** against the 0x4000 floor;
  free below the reserved slave stack base (`0x002FC000`) = 0x56C0
  (22,208 B). The tasking's `lwram_remaining = 0xA2C0` is the stage-1b
  figure and predates `.lwram_geo_traversal` (+3,072): 0xA2C0 − 0xC00 =
  0x96C0 exactly. Undoing the full split returns 54,080 B to LWRAM
  (to-top margin would become ~0x16A00 / 92,672 B), so LWRAM headroom is
  not a constraint on any package below.

## Ranked reclamation table

Thresholds (measured, from the 504 B slack): **(a)** return
`s_bob_hot_workarea` only = 43,776 − 504 = **43,272 B needed**;
**(b)** return the full 54,080 B and land ~8 KB above the floor =
54,080 + 8,192 − 504 = **61,768 B needed**.

Ranked by bytes-recovered-per-risk. "Gate" = the evidence that must be
captured before the change is safe.

| # | Candidate | Bytes | Subsystem | Disposition | Risk / gate | Evidence |
| --- | --- | ---: | --- | --- | --- | --- |
| 1 | `SOURCEBOOT_VDP1_COMMAND_CAPACITY` 2048→1664 (2×384×32) | 24,576 | VDP1 transport | shrink capacity | Hard floor 1536 (fast3d emits 1:1 per resolved triangle; resolve cap grounded on 1,365–1,431 real BOB frames) + control/HUD commands must fit in the remaining 128. Gate: max per-frame `profile.command_count` from a live capture. Also keep `vdp1_vram_partitions_set` arithmetic (`main.c:1559,1766,1799`) consistent; upload-stage reuse needs only 2,560 B (`source_scene_bundle.h:38`). Conservative variant 2048→1792 = 16,384 B. | `main.c:726,763`; `saturn_fast3d_frontend.h:26`, budget comment ~:590-625 |
| 2 | libyaul `_private_pool` `0xA000`→`0x4000` | 24,576 | libyaul kernel mm | shrink capacity via **external libyaul patch** (dependency — patch/fork, not in-tree edit; yaul-install is built from pinned `work/upstream/libyaul`) | Internal allocations (cdfs, vdp sync) fail if high-water exceeds the cut. Gate: one instrumented run reading libyaul mm stats (mm_stats API exists in the same file) for private-pool high-water. Conservative variant →`0x6000` = 16,384 B. | `work/upstream/libyaul/libyaul/kernel/mm/internal.c:22,28,46-51` |
| 3 | `GFX_POOL_SIZE` 6400→4096 | 18,432 | SM64 master display list | shrink capacity | Master DL overflow drops geometry. Gate: max per-frame `entries = gDisplayListHead - gGfxPool->buffer` (`game_init.c:260` computes it already — log it for one route). Variant 6400→4608 = 14,336 B. | `game_init.h:15`, `buffers.c:36`, `game_init.c:260` |
| 4 | SMPC peripheral pool 14→2 peripherals (+`_oreg_buf` trim) | 6,384–7,900 | libyaul input | shrink via external libyaul patch | Loses multitap/analog headroom — irrelevant for R1 (one digital pad, `live_input_mode=1`). Patch-maintenance burden is the real cost. | `smpc_peripheral.c:72,85` |
| 5 | Actor-bank open/validation `.text` → `.cart_cold_text` | ~2,308 | actor bank | move to cart-cold (cart has 419 KB free) | ONLY if caller audit proves boot/bundle-open-only: `saturn_actor_bundle.c:363` is open-path, but `saturn_actor_texture_residency.c:299` calls resolve too — frequency unproven here. If hot, A-bus execution regresses cadence. | symbols listed above; linker rule `*(.cart_cold_text)` in `sourceboot-cart.x` |
| 6 | Dead-under-R1 SM64 leftovers: `_sCutsceneVars` 360, `_sCurCreditsSplinePos`/`Focus` 512, `_gProfilerFrameData` 392, `_gDebugInfo` 256 | ~1,520 | SM64 game loop misc | delete / feature-gate | Low; credits/cutscene/profiler are unreachable on the demo route (not formally proven — gate on grep-level audit of the R1 code path). | `.bss` listing above |
| 7 | `_s_terrain_queue_merge_ids` → LWRAM | 6,936 | render-job merge | move to LWRAM | CONTRADICTS the accepted split rationale (`saturn_demo_render.c:223-227` keeps terrain scratch hot in HWRAM deliberately); scene construction already costs ~24.5 VBlanks. Last-resort filler only, behind a cadence A/B. | `saturn_demo_render.c:395,210-233` |
| 8 | `_gObjectPool` 208→176 | 19,456 | actor runtime | shrink capacity | GAMEPLAY-VISIBLE (spawn failures; `object_helpers.c:2109,2115` and `tree_particles.inc.c:17` key off capacity headroom); identity re-key (`object_pool_capacity` is in the manifest). NOT recommended without max-live-object telemetry from the BOB route. Listed for completeness because each −32 entries = 19,456 B (entries are HWRAM `.bss`, 608 B each). | `object_list_processor.c:75`, `.h:26-29` |
| — | `_gSineTable`/`_gArctanTable` (22,530), `_sourceboot_gouraud_staging` (24,576), surface partitions, matrix stacks, per-primitive scratch | — | — | HOLD | Sine/arctan: hot game-loop trig. Gouraud staging: DMA source, capacity pinned 1:1 to the 1536 resolve cap (only ~105 entries above the observed 1,431 peak). Per-primitive scratch: the split exists to keep it hot. | cited above |

## Recommended Sprint 2 reclamation package

Primary package (no gameplay-visible cuts, both members telemetry-gated):

- **(a) workarea return, needs 43,272 B:** #1 cmdt capacity 2048→1664
  (24,576) + #2 `_private_pool` →`0x4000` (24,576) = **49,152 B** →
  workarea back in HWRAM with 5,880 B of slack above the floor.
  - No-libyaul-patch fallback: #1 (24,576) + #3 `GFX_POOL_SIZE`→4096
    (18,432) + #6 leftovers (1,520) = **44,528 B** (thin: 1,256 B slack).
- **(b) full 54,080 B return + ~8 KB above floor, needs 61,768 B:**
  #1 (24,576) + #2 (24,576) + #3 →4608 (14,336) = **63,488 B** →
  1,720 B above the (b) target; add #4 SMPC (~6,384) as reserve if any
  gate forces a conservative variant.

Sequencing note: all three primary members are capacity shrinks whose gates
are one instrumented live capture each (max `command_count`, mm private
high-water, max DL entries) — they can share a single diagnostic build and
one Ymir run before any capacity edit lands, which fits the two-attempt
rule.

## Surprises and sanity checks

1. **Net committed HWRAM growth A9A→current is +44 B.** The "growth" story
   is really composition: the 131,072 B VDP1 command staging repatriation
   (A9A held it in `.lwram_cmdts`, an arrangement the current linker script
   hard-forbids with an ASSERT — SCU/CPU-DMAC transport out of LWRAM is the
   config-attributable regression class) was paid for by evicting 102,056 B
   of former-HWRAM `.bss` to LWRAM, deleting 17,284 B, and cutting the
   object pool by 19,456 B.
2. **`_sourceboot_fast3d` (44,616 B) is also in LWRAM** — beyond the
   publicized 54,080 B split. A9A ran it in HWRAM at 5.294 FPS; it holds
   the interpreter's matrix/vertex/resolved arrays, which are per-frame
   working set (`main.c:212`). Even the full threshold-(b) package returns
   only the 54,080 B; restoring complete A9A hot-set residency (workarea +
   actor scratch + fast3d) would need ~98,192 B. Sprint 2 planning should
   decide explicitly whether fast3d (or just its `resolved[]`) is in scope
   for a later rung, or accept its LWRAM placement as measured-OK.
3. **The donor relief already did the clever reuse:** the deleted 16,384 B
   `_s_source_cart_stage` was replaced by reusing idle command bank
   `sourceboot_vdp1_cmdts[0]` as the upload stage
   (`main.c:766-771` static-asserts it covers the 2,560 B stage). Going
   further in that direction (aliasing more cold staging onto hot arenas)
   is possible but each alias needs a lifetime proof.
4. **libyaul owns 55.1 KB of HWRAM `.bss`** (`_private_pool` 40,960 +
   SMPC pool 9,016 + cdfs 4,112 + misc ~1,000) — a subsystem nobody had on
   the suspect list; two of its three big blocks are shrinkable only via
   dependency patches.
5. The tasking's cart free-space (~148 KB) and `lwram_remaining` (0xA2C0)
   figures were both stale against the accepted candidate; verified values
   are 419,024 B free cart and 0x96C0 LWRAM to-top margin (reconciliations
   above).
