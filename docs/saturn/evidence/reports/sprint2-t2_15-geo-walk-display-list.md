# Sprint 2 Task T2.15 - the geo walk's display list, bounded and classified

- Date: 2026-08-16. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `e217cca4`.
- Task: bound the prize in suppressing the display-list construction the geo
  walk performs and nothing reads (T2.14 section 8.4), classify every site as
  state-owning or list-building, and land the removal if the measurement
  justifies it.
- Cadence basis: `summarize_cadence` only. Baseline to beat:
  **`id-a61d5203793986e7`, 5.3538 FPS mean / 11.2069 VB per frame**
  (median 5.4545, 1% low 5.0).
- **One build was made** - the sealed upper-bound diagnostic. **No code change
  was landed**, and section 4 says exactly why.

---

## Headline

**The ceiling is large and the reachable part of it is not.**

`SATURN_EXPERIMENTAL_SKIP_GEO_WALK=1` measures **6.3273 FPS mean / 9.4828 VB per
frame** - **+0.9735 FPS, +18.18%, -1.7241 VB/frame** against the baseline. That
is a real and substantial ceiling, far larger than T2.14 or this task expected.

**It is a ceiling, not an achievable number.** It suppresses the entire walk,
including all the state the walk owns, plus `render_hud()` and
`render_text_labels()`, and the flag's own comment plus section 3.4 below list
what it invalidates.

**The display-list construction inside that region - the thing this task was
chartered to remove - is 0.479% of it, worth about +0.005 FPS.** Measured, not
estimated: the four list-building symbols total **3,695.7 cycles per frame**
against the **771,443 cycles per frame** the suppressed region costs. The rest
is state the walk owns and the demo renderer reads.

**T2.14's "~10x the shadow path" estimate was an over-attribution and is
retired here.** It counted `_saturn_geo_enter_object` (1.7048%) and
`_saturn_mtxq_refresh_float_mirror` (0.5854%) as display-list construction.
Section 3 shows the list-building inside `saturn_geo_enter_object` is exactly
three statements, and that `saturn_mtxq_refresh_float_mirror` is 100%
state-owning - its own source comment explains why it cannot even be deferred.

**Recommendation: do not do this refactor.** It is worth ~0.09% of current FPS,
about half the shadow path T2.14 already declined to spend a build on, and it
would touch shared, non-Saturn-arm-carrying code in the only renderer that
works. Section 6 names where the +0.97 FPS actually lives and one concrete,
bit-exact lead found in passing.

---

## 1. Deliverable (1): the measured ceiling

### 1.1 The build

Sealed identity **`id-137ecb7d231a34d6`**, label
`feat001-pipe4-l9-a1-route0-replay1-live1-boot600-cam0v3-diag0-cart32-stage8-hot1-clip1-bsp1-poly2-frag0-cfg137ecb7d231a`.
The 27-variable product invocation from `sprint1-stage1-link-smoke.md` with
`SATURN_OBJECT_POOL_CAPACITY=208` and `SATURN_DIAGNOSTIC_MODE=0`, changing
exactly one variable: `SATURN_EXPERIMENTAL_SKIP_GEO_WALK=1`.

| Artifact | SHA-256 |
| --- | --- |
| `obj/sm64-saturn-sourceboot-e2.elf` | `71912abdde0599c1e5ada8e5127556692833a7bb8cb68fcab6707e57d6600af2` |
| `sm64-saturn-sourceboot-e2.iso` (5,179,392 B) | `0a3fb9814f7eb1e681f854fd8e38b2e14f0ba2d5c77cac312441ebdefe35cee3` |
| `sm64-saturn-sourceboot-e2.cue` (not identity-bearing) | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` |
| `saturn-release-manifest-v1.json` | `5c9fc8332ab193a1e96d6c21f6fefa271f10fc8d154b172fc5eaf2635e006232` |

Manifest `effective_config` confirms `experimental_skip_geo_walk: 1`,
`diagnostic_mode: 0`, `object_pool_capacity: 208`, `demo_path: 1`,
`live_input_mode: 1`, `features.complete_mario_animation: 0`, and
`provenance.git_revision e217cca4...`, `closure_clean: true`. Preserved to
`releases/2026-08-16_t2_15-ceiling-diag/id-137ecb7d231a34d6/`.

**Three build attempts, two of them infrastructure, one causal.** (1) failed at
`source-actor-family-bundle` with "actor family bundle publication is
incomplete" - the known g15 repair deletes `actors-v3-g15/` but the deletion
must be followed by an explicit republish, which the handoff note omits;
republished generation 15 and re-verified with `--verify-publication`, exit 0.
(2) failed the identity gate: "Make-provided config must exactly match target
profile release_config", because the profile JSON still carried
`experimental_skip_geo_walk: 0`. Flipped that one field for the build and
**reverted it**; `git diff` on
`tools/saturn/profiles/sourceboot-bob-demo-v1.json` is empty. (3) exit 0.

### 1.2 The number

`summarize_cadence` only. ymir-headless `build-agent2`, BIOS
`Sega Saturn BIOS (USA).bin`, absolute `--cue`, `--startup-vblanks 4096
--max-vblanks 3600 --presentation-events 30 --timeout 1800`, release-manifest
bound. **`status: complete`, exit 0, one attempt.** On-target identity
**MATCH** (`observed_sha256 == expected_sha256`, `d88b17a9...23b5`),
ELF build identity **`match: true`**, `diagnostic_mode: 0` in the target build
record. Evidence: `sprint2-t2_15-ceiling-throughput-30events.json`.

| Build | Events | Intervals | Window (VB) | **VB/frame** | **FPS mean** | Median | 1% low |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `id-a61d5203793986e7` (T2.13 baseline) | 30 | 29 | 325 | 11.2069 | 5.3538 | 5.4545 | 5.0 |
| **`id-137ecb7d231a34d6` (ceiling)** | 30 | 29 | **275** | **9.4828** | **6.3273** | **6.6667** | **5.4545** |
| **Delta** | | | **-50** | **-1.7241** | **+0.9735** | **+1.2121** | **+0.4545** |

**+18.18%.** Every one of mean, median and 1% low improves.

Per-frame phase profile, 29 intervals each:

| | T2.13 `a61d5203` | **ceiling `137ecb7d`** | Delta |
| --- | ---: | ---: | ---: |
| frame (`vblank_delta`) | 11.2069 | **9.4828** | **-1.7241** |
| construction | 5.6897 | 4.0000 | -1.6897 |
| - master finalization | 3.6897 | 2.0000 | -1.6897 |
| - slave work overlap | 2.4828 | 2.4828 | **0.0000** |
| simulation / source tick | 4.8276 | 2.0000 | -2.8276 |
| transport + presentation | 0.0000 | 0.0000 | 0.0000 |
| attributed | 10.5172 | 6.0000 | -4.5172 |
| **allowance actually needed** | **0 of 29** | **29 of 29** | **+29** |

Three honest readings of that table:

1. **`slave work overlap` is unchanged at 2.4828, to four decimals.** The walk
   is master-CPU work; the slave's window is untouched. That is the expected
   signature and it corroborates the measurement.
2. **`construction` and `simulation` together fall by 4.5173, far more than the
   frame's 1.7241.** The geo walk sits inside `game_loop_one_iteration()`,
   which the source tick times, so the same removed work is debited against two
   overlapping spans. **Only `vblank_delta` is measured directly; it is the one
   to trust** - the same caveat T2.13 recorded on its own phase table.
3. **The T2.11 concurrency allowance goes from needed in 0 of 29 intervals to
   needed in 29 of 29.** That is a rail regression, not a win, and it is worth
   recording: the faster frame is close enough to the phase boundaries that the
   allowance is now load-bearing on every interval. Any real change that
   captured a large share of this ceiling would have to be re-checked against
   that rail.

---

## 2. Deliverable (2): the state-vs-list classification

Scope: `src/game/rendering_graph_node.c` (2,996 lines) and what it calls. Line
numbers are at HEAD `e217cca4`.

### 2.1 The two containment proofs that decide the whole question

**`gMatStackFixed[32]` (`:41`) is a pure display-list construct.** Twelve
writes; **three** reads, all inside this one translation unit, and no reference
anywhere else in `src/` or `include/`:

| Read | Site | Live? |
| --- | --- | --- |
| `:419` | `listNode->transform = gMatStackFixed[gMatStackIndex]` inside `geo_append_display_list` | list-building |
| `:595`/`:600` | `saturn_geo_enter_level_of_detail` reads `mtx->m[1][3]` into `distanceFromCam` | **dead** - `:604` `#ifndef TARGET_N64` overwrites it with `distanceFromCam = 0` before use |
| `:2981` | `gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(gMatStackFixed[...]))` in `geo_process_root` | list-building |

So on Saturn there are exactly **two** live readers and both are display-list
emission.

**`listHeads`/`listTails` (`graph_node.h:132-133`) are pure display-list
constructs.** Declared in the header, referenced only in this TU: written at
`:419-425` (`geo_append_display_list`), reset at `:449-451`
(`saturn_geo_enter_master_list`), read at `:382-398`
(`geo_process_master_list_sub`). Nothing else in the tree touches them.

**`gMatStack[32]` (`:40`) is state and must stay.** Read by every geo callback
(`:525`, `:652`, `:692`, `:1118`, `:1152`, `:1837`, `:1907`), by
`obj_is_in_view` (`:1634`), by `cameraToObject` (`:1624-1626`), by the
`throwMatrix` publication (`:1598`, `:1622`), by `get_pos_from_transform_mtx`
(`:1349`), and by `saturn_mtxq_gmatstack_index` (`:246`).
**`gMatStackQ[32]` (`:87`)** is its Q16 source of truth. Both stay.

### 2.2 Site classification

**LIST-BUILDING (removable).** Every one of these exists only to feed a display
list that `game_init.c:464` declines to submit:

| Kind | Sites |
| --- | --- |
| `alloc_display_list` for a wire `Mtx` | `:688`, `:689`, `:776`, `:839`, `:900`, `:959`, `:1018`, `:1201`, `:1387`, `:1643`, `:1829`, `:2955` |
| `alloc_display_list` for a viewport / background `Gfx` | `:2947`, `:1158`/`:1160` |
| `saturn_mtxq_write_wire` (the 64-byte Q16 memcpy into that `Mtx`) | `:490`, `:548`, `:723`, `:797`, `:859`, `:918`, `:976`, `:1040`, `:1260`, `:1399`, `:1647`, `:1911`, `:2978` |
| `gMatStackFixed[gMatStackIndex] = mtx` | `:727`, `:801`, `:863`, `:922`, `:980`, `:1057`, `:1264`, `:1405`, `:1650`, `:1914`, `:2979` |
| `geo_append_display_list` calls | `:803`, `:865`, `:924`, `:982`, `:1059`, `:1093`, `:1121`, `:1155`, `:1173`, `:1266`, `:1407`, `:1409`, `:1411` |
| `geo_append_display_list` body | `:410-425` (the `gSPLookAt` write, the `alloc_only_pool_alloc`, the list link) |
| `geo_process_master_list_sub` | `:342-402` in full - the `lookAt` basis, the `gDPPipeSync`/`gSPSetGeometryMode`/`gDPSetRenderMode`/`gSPMatrix`/`gSPDisplayList` drain |
| Projection emission | `:499-500` (ortho), `:558-560` (perspective), `:704` (camera roll), `:1833` (held-object `gSPLookAt`), `:2980-2982` (root viewport + matrix) |
| Background fill | `:1164-1171` (`gDPPipeSync`/`gDPSetCycleType`/`gDPSetFillColor`/`gDPFillRectangle`/`gSPEndDisplayList`) |

**STATE-OWNING (must stay).** Everything the demo renderer or the simulation
reads afterwards:

| What it owns | Sites |
| --- | --- |
| Q16 matrix stack composition | `:713-714`, `:785-786`, `:847-848`, `:908-909`, `:965-966`, `:1025-1037`, `:1250-1251`, `:1396`, `:1573-1596`, `:1878-1886` |
| Float mirror `gMatStack` refresh | `:193-199` and its 12 call sites - see `:1602-1607`, which states the refresh cannot be deferred because `cameraToObject` and `obj_is_in_view` read it first |
| `gMatStackIndex` push/pop pairing | every `saturn_geo_enter_*`/`saturn_geo_leave_*` pair |
| `throwMatrix` publication | `:1598`, `:1622`; cleared at `:1693` |
| `cameraToObject` (positional audio) | `:1624-1626` |
| Animation globals | `geo_set_animation_globals` `:1300+`, `gCurAnimType`/`gCurrAnimAttribute` mutation in `saturn_geo_enter_animated_part` `:1192-1240`, the save/restore in `saturn_geo_enter_held_object` `:1913-1922` |
| Frustum-cull decision | `obj_is_in_view` `:1462-1517`, observed at `:1633-1638` |
| Actor-state observer | `saturn_source_observe_object_begin` `:125-183`, `:1560`, `:1696-1699`; switch/LOD observers `:610-614`, `:655-657` |
| Camera claim + `matrixPtr` | `:729-733`, `sSaturnCameraMatrixQ` `:732` |
| `gCurGraphNodeObject`/`...HeldObject`/`...MasterList`/`...CamFrustum` claims | `:445`, `:453`, `:576`, `:1657`, `:1685`, `:1925` |

**BOTH - computes state *and* writes it into a display list.** These are the
risky ones and they are what caps how much of the ceiling is reachable:

| Site | State it owns | List it builds |
| --- | --- | --- |
| `saturn_geo_enter_generated_list` `:1114-1123` | `fnNode.func(GEO_CONTEXT_RENDER, ...)` runs the geo callbacks - `geo_movtex_pause_control` mutates `gMovtexCounter`/`gMovtexCounterPrev` (`moving_texture.c:334-341`), `geo_painting_update` mutates `gPaintingUpdateCounter` (`paintings.c:1274-1277`), envfx mutates `gEnvFxMode` and its particle arrays (`envfx_snow.c:90,142`) | the returned `Gfx *list` and its append at `:1121` |
| `saturn_geo_enter_background` `:1147-1173` | same callback contract | append at `:1155`, or the black-rect fallback at `:1158-1173` |
| `saturn_geo_enter_shadow` `:1335-1416` | `create_shadow_below_xyz` sets `gShadowAboveWaterOrLava` and `gMarioOnIceOrCarpet`, and performs the 11 `find_floor` queries T2.14 measured | `shadowList` and its append at `:1407`/`:1409`/`:1411` |
| `saturn_geo_enter_object` `:1551-1660` | the entire body except three statements | `alloc_display_list` `:1643`, `saturn_mtxq_write_wire` `:1647`, `gMatStackFixed[...] = mtx` `:1650` |
| `render_hud()` (`area.c:404`, inside the same suppression guard) | `render_hud_power_meter` -> `handle_power_meter_actions` mutates `sPowerMeterHUD.animation`/`.y` (`hud.c:229-233`), which `get_hud_power_meter_state` (`hud.c:387-390`) publishes into the render snapshot at `sourceboot/main.c:505-506` | everything else it emits |

That last row is a hazard in the sealed diagnostic that **its own comment does
not name**: `SATURN_EXPERIMENTAL_SKIP_GEO_WALK=1` also freezes a published
*snapshot* field, not only geo state.

### 2.3 Where the suppression actually happens

`SATURN_EXPERIMENTAL_SKIP_GEO_WALK` sets `scene_graph_suppressed`, and the
consumer is `src/game/area.c:400`:

```c
if (!scene_graph_suppressed) {
    geo_process_root(...);          /* :401 - the whole walk */
    gSPViewport(...); gDPSetScissor(...);
    render_hud();                   /* :409 */
    gDPSetScissor(...);
    render_text_labels();           /* :413 */
}
```

So the ceiling covers the walk **plus** `render_hud()` and
`render_text_labels()` plus five scissor/viewport emissions - it over-states the
geo walk alone.

---

## 3. Deliverable (3): how much of the ceiling is reachable

### 3.1 The arithmetic, from measured quantities only

From T2.14's cycle profile of the **baseline** build
(`sprint2-t2_14-softfloat-profile-a61d5203.json`), whose denominator is
**5,014,451 cycles per frame** at **11.2069 VB/frame**:

| List-building symbol | Cycle share | Cycles/frame | Calls/frame |
| --- | ---: | ---: | ---: |
| `_geo_append_display_list` | 0.0488% | 2,447.3 | 37.30 |
| `_alloc_only_pool_alloc` | 0.0124% | 621.1 | 31.10 |
| `_alloc_display_list` | 0.0088% | 441.0 | 31.10 |
| `_saturn_mtxq_write_wire` | 0.0037% | 186.3 | 37.30 |
| `_geo_process_master_list_sub` | absent | - | - |
| **Total** | **0.0737%** | **3,695.7** | |

The ceiling removes **1.7241 VB/frame**. At 5,014,451 cycles / 11.2069 VB =
**447,447 cycles per VBlank**, that is **771,443 cycles per frame**, i.e.
**15.39%** of the frame.

**List-building is 3,695.7 / 771,443 = 0.479% of the ceiling.**
**0.479% x 0.9735 FPS = +0.0047 FPS.**

Cross-check by the independent route T2.14 used (`d(FPS)/d(VB) = -59.94/11.2069²
= -0.4772`): 0.0737% x 11.2069 VB = 0.0083 VB saved -> **+0.0039 FPS** naive, or
**+0.0111 FPS** at T2.13's 2.81x amplification upper bound. The two methods
agree to within the amplification band.

**The prize is +0.004 to +0.011 FPS, i.e. 0.07% to 0.21% of 5.3538.**

### 3.2 For scale, the state-owning work in the same walk

| Symbol | Cycle share | Cycles/frame | Calls/frame |
| --- | ---: | ---: | ---: |
| `_saturn_geo_enter_object` | 1.7048% | 85,486.5 | 18.60 |
| `_sm64_saturn_matrix_mul.isra.0` | 0.8047% | 40,348.8 | 43.50 |
| `_saturn_mtxq_refresh_float_mirror` | 0.5854% | 29,354.8 | 43.50 |
| `_sm64_saturn_geo_walk_runtime_run` | 0.2598% | 13,025.2 | - |
| `_saturn_geo_walk_enter` | 0.1294% | 6,490.8 | 49.70 |
| `_sm64_saturn_geo_walk_runtime_next` | 0.1116% | 5,596.4 | 167.70 |
| `_saturn_geo_walk_leave` | 0.0282% | 1,416.2 | 62.10 |

`_saturn_geo_enter_object` **alone is 23x all list-building combined**, and the
three list statements inside it (section 2.2) are a bump allocation, a 64-byte
memcpy and a pointer store.

### 3.3 Retiring T2.14 section 8.4's estimate

T2.14 wrote: *"`_saturn_geo_enter_object` (1.705%) plus
`_saturn_mtxq_refresh_float_mirror` (0.585%), `_alloc_display_list` and
`_geo_append_display_list` sit inside it. That is ~10x the shadow path."*

The two large terms in that sum are state, not list. Only the two small terms
are removable. Corrected, the removable set is **0.0737%**, which is **0.41x**
the shadow path (0.18%), not 10x - **a 24-fold over-estimate**, and it points
the other way: the display list is **smaller** than the shadow path T2.14
declined to spend a build on.

---

## 4. What was landed, and what was not

**Nothing was landed in target source, deliberately.**

The brief's instruction was to bound the prize first and to stop if the
measurement did not justify the refactor. It does not:

1. **The measured value is +0.004 to +0.011 FPS** - below the run-to-run spread
   of the profiles that produced it, and about **half** the shadow-path gating
   that T2.14 recommended against on exactly these grounds.
2. **The blast radius is not confined to `rendering_graph_node.c`.**
   `gDisplayListHead` is written at **443 sites across 13 files**
   (`area.c`, `game_init.c`, `hud.c`, `ingame_menu.c`, `memory.c`, `print.c`,
   `profiler.c`, `rendering_graph_node.c`, `screen_transition.c`,
   `file_select.c`, `star_select.c`, plus two headers). A partial removal leaves
   a half-built list and a `gGfxPoolOverrun` latch (`game_init.c:459`) whose
   meaning has silently changed.
3. **Four of the highest-value sites are "both" sites** (section 2.2). Their
   list output cannot be removed without either keeping the callback and
   discarding its return - which saves only the append - or restructuring the
   callback contract, which is a far bigger change than the number supports.
4. **The correctness bar the brief set is expensive relative to the prize.** A
   byte-identical oracle over the published matrix stack, actor-bridge
   snapshot, animation IDs and camera matrices, plus its three-mutation
   companion, is a real piece of engineering. It is the right bar; it is simply
   not worth paying for +0.005 FPS in the only renderer that currently works.

The classification in section 2 is the durable deliverable: it is what makes
this decision checkable, and it is what any future attempt would need first.

---

## 5. The animation-sweep question - answered

**The sweep is not active in the shipped profile, and it is not the cause of the
owner's report.** Two independent witnesses:

1. **Source.** `sourceboot/main.c:441` guards the sweep with
   `#if SATURN_DIAGNOSTIC_MODE == 1`. The shipped profile
   (`tools/saturn/profiles/sourceboot-bob-demo-v1.json`) sets
   `diagnostic_mode: 0`, and the product tuple passes
   `SATURN_DIAGNOSTIC_MODE=0`.
2. **Binary.** `_sourceboot_animation_sweep` is **absent from the product ELF's
   symbol table** - `sh-elf-nm` over
   `releases/2026-08-16_t2_13-product/id-a61d5203793986e7/sm64-saturn-sourceboot-e2.elf`
   returns zero matches out of 8,241 symbols.

**But the same feature tuple contains a live explanation, and it is worth
recording.** With `SATURN_FEATURE_COMPLETE_MARIO_ANIMATION=0` - which the
shipped product tuple sets - `saturn_actor_bridge.c:212-216` reduces Mario's
entire pose to a **two-bank selector**:

```c
selector->vertex_bank_id = pose.walking_bank ?
    SM64_SATURN_MARIO_VERTEX_BANK_WALKING :
    SM64_SATURN_MARIO_VERTEX_BANK_NEUTRAL;
```

`pose.walking_bank` comes from `is_walking_family(animation_id)`
(`saturn_actor_bridge.c:91-105`, set at `:141-142`), and
`sm64_saturn_mario_actor_pose_from_selector` returns **0 unconditionally** under
this flag (`:249-251`). There is no per-animation skeletal evaluation on the
shipped build at all: every animation resolves to one of two static vertex
banks, and jump is not in the walking family, so it renders neutral. That is a
complete structural explanation for "walk and jump animations stopped working"
and it needs no bug.

Not fixed here - R3 scope, and it is a feature-flag decision, not a defect.

---

## 6. Where the +0.97 FPS actually is, and one lead found in passing

The ceiling is real and worth chasing; it is just not chaseable by deleting a
display list. Ranked by measured cycles per frame inside the suppressed region:

1. **`_saturn_geo_enter_object`, 85,486 cycles/frame over 18.6 objects** -
   4,596 cycles per object. Its list work is 3 statements; the rest is the actor
   observer, ~9 float->Q16 conversions, the matrix compose, `cameraToObject`,
   `geo_set_animation_globals`, and an inlined `obj_is_in_view`.
2. **`_sm64_saturn_matrix_mul` 40,349 and `_saturn_mtxq_refresh_float_mirror`
   29,355 cycles/frame, both at 43.5 calls/frame** - the matrix stack itself.
3. **`render_hud()` + `render_text_labels()`**, which the ceiling also
   suppresses and which this task did not separate out. Neither appears in the
   profile's top 60, so their share is small, but it is not zero and it is
   inside the 1.7241 VB.

**The concrete lead, bit-exact and cheap.** `obj_is_in_view`
(`rendering_graph_node.c:1462-1517`) is inlined into `saturn_geo_enter_object`
and recomputes, **per object, per frame**:

```c
halfFov = (gCurGraphNodeCamFrustum->fov / 2.0f + 1.0f) * 32768.0f / 180.0f + 0.5f;   /* :1477 */
hScreenEdge = -matrix[3][2] * sins(halfFov) / coss(halfFov);                          /* :1479 */
```

`gCurGraphNodeCamFrustum->fov` is fixed for the whole frame - the perspective
node sets `gCurGraphNodeCamFrustum` at `:566` and clears it at `:573`, so every
object beneath it sees one value. `halfFov`, `sins(halfFov)` and `coss(halfFov)`
are therefore **loop-invariant across all 18.6 objects**, and that expression is
about five soft-float operations plus a sine/cosine pair, on a route where
`___divsf3` is 0.7227% and `___mulsf3` 0.9582% of all cycles. Hoisting it to the
perspective node is **bit-identical by construction** (same inputs, same
operations, computed once) and needs no oracle beyond a value-equality check.
Not done here - it is a different task from this one's charter.

---

## 7. Gates

| Gate | Result |
| --- | --- |
| `verify-memory-map` (on `id-137ecb7d231a34d6`) | **RESULT OK** - `hwram_remaining 0x4A98` (required >= `0x1F00`), `lwram_remaining 0x17620` (floor >= `0x4000`) |
| `verify-saturn-geo-walk-contract` | PASS (`frame=16 walk=32`), plus source contract PASS |
| `verify-saturn-geo-depth-manifest` | PASS (manifest + storage contract) |
| `verify-saturn-geo-walk-source-policy` | PASS |
| `verify-render-native-math` / `-mutation` | PASS / PASS |
| `verify-shadow-trig` / `-mutation` | PASS / PASS |
| `verify-mtxq-ctors` / `-mutation` | PASS / PASS |
| `verify-source-geo-state-diff` | **BLOCKED by a recipe defect** - see below |
| `verify-graph-q16-contract` | **BLOCKED by a recipe defect** - see below |

**Two gates are blocked by the MSYS/path recipe-defect class STATE.md already
tracks, and neither is caused by this task - no tracked source was modified.**

- `verify-source-geo-state-diff` (`Makefile.saturn.mk:623`) hands the MSYS-form
  path `/d/Code/.../source-geo-state-contract-test.exe` to a **native-Windows**
  Python `subprocess.run`, which raises `FileNotFoundError [WinError 2]`. This
  is the same shape STATE.md records for `verify-render-clusters`'s final step.
  **Run by hand the binary passes, `rc=0`** - so the gate's subject is healthy
  and only its recipe is broken.
- `verify-graph-q16-contract` (`Makefile.saturn.mk:1925-1930`) is a **third,
  previously unrecorded instance** of exactly the defect STATE.md describes for
  `verify-render-clusters`: its `-I` list omits
  `build/saturn/sourceboot/generated`, where the generated
  `saturn_geo_depth_manifest.h` lives, and
  `src/port/saturn/runtime/saturn_geo_walk_storage.h:8` includes it. The gate
  dies at the preprocessor with `fatal error: saturn_geo_depth_manifest.h: No
  such file or directory` - it has been sitting in `verify-all` verifying
  nothing.

`verify-render-clusters` and `verify-softfp-bitexact` were not run; STATE.md
records both defects and neither pins anything this task touched (this task
touched no target source at all).

---

## 8. Honest limits

1. **The ceiling is a ceiling and includes more than the geo walk.**
   `area.c:400` also gates `render_hud()` and `render_text_labels()`. The walk's
   own share of the 1.7241 VB is therefore **less** than 1.7241, which makes the
   list-building fraction of the *walk* slightly larger than 0.479% - but only
   slightly, since neither HUD function appears in the profile's top 60.
2. **The 0.479% reachable figure mixes two builds.** The numerator is from
   T2.14's cycle profile of the **baseline**; the denominator is the **ceiling
   build's** measured VB delta. That is the right pairing (both describe the
   baseline's frame), but it is not a single-experiment number.
3. **Burst-sampled shares at the 0.01% level are noisy.** T2.14 recorded a 1.5x
   spread between two sweeps on `_find_floor_from_list`. Even a 3x error on the
   list-building total leaves it under 1.5% of the ceiling.
4. **No equivalence oracle was built and no mutations were run**, because no
   code change was landed. Those deliverables are void by the honest-stop
   clause, not skipped.
5. **This is not an owner observation.** The ceiling build is a sealed
   diagnostic that intentionally invalidates animation, warp, camera, water,
   moving-texture, carpet and matrix state, and - newly documented here - the
   HUD power-meter snapshot field. It must never ship. No desktop Ymir session
   was launched.
6. **The +18.18% is the largest single lever measured on this route so far**,
   and this report deliberately does not claim any part of it. Whoever takes it
   next should start from section 6, not from the display list.

---

## 9. Reproduction

```
# Ceiling build. FREEZE ALL TRACKED SOURCE FIRST, then flip exactly one
# profile field (and revert it afterwards -- git diff must be empty):
#   tools/saturn/profiles/sourceboot-bob-demo-v1.json
#     "experimental_skip_geo_walk":0 -> 1
# Known repair, in this order (deleting without republishing fails the build):
#   rm -rf build/saturn/packages/bob/1/actors-v3-g15
#   .venv-saturn-tools/Scripts/python.exe tools/saturn/compile_actor_family_bundle.py \
#     --root . --closure build/saturn/packages/bob/1/closure.json \
#     --family-report build/saturn/packages/bob/1/actors/actor-families.json \
#     --model-ids include/model_ids.h --package-generation 15 \
#     --output-dir build/saturn/packages/bob/1/actors-v3-g15
# Then the 27-variable invocation from sprint1-stage1-link-smoke.md with
# SATURN_OBJECT_POOL_CAPACITY=208, SATURN_DIAGNOSTIC_MODE=0 and
# SATURN_EXPERIMENTAL_SKIP_GEO_WALK=1, via tools/saturn/with-msys-toolchain.ps1.

# Cadence: summarize_cadence only. NOTE the manifest binds the ELF at
# `obj/sm64-saturn-sourceboot-e2.elf` RELATIVE TO THE MANIFEST, so point
# --game/--elf at the build directory, not at a flattened releases/ copy.
python tools/saturn/capture_sourceboot_throughput.py \
  --ymir <ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe> \
  --ipl  <sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin> \
  --game <build/saturn/sourceboot/e2-bob-identity-id-137ecb7d231a34d6/...e2.cue> \
  --elf  <.../e2-bob-identity-id-137ecb7d231a34d6/obj/...e2.elf> \
  --release-manifest <.../e2-bob-identity-id-137ecb7d231a34d6/saturn-release-manifest-v1.json> \
  --output docs/saturn/evidence/reports/sprint2-t2_15-ceiling-throughput-30events.json \
  --startup-vblanks 4096 --max-vblanks 3600 --presentation-events 30 --timeout 1800

# Cycle shares in sections 3.1/3.2 are read straight out of the existing
# sprint2-t2_14-softfloat-profile-a61d5203.json -- no new profile was captured.

# Animation-sweep witness (no build, no emulator):
work/yaul-install/bin/sh-elf-nm.exe \
  releases/2026-08-16_t2_13-product/id-a61d5203793986e7/sm64-saturn-sourceboot-e2.elf \
  | grep animation_sweep   # -> no matches
```
