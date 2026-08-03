# Overlapped-render pipeline evidence — Task 1

## Current verdict

**BLOCKED — quality-fix round 1/5.** Full-range review covered
`4a8fe1ce^..70cb3fe1`, not only the original implementation commit. The
current Saturn policy suppresses all of `geo_process_root()`, but that function
is not a construction-only boundary. It owns or invokes source-state mutations
used by later gameplay and visual-state updates. The existing source test does
not prove otherwise.

No production change, target build, CUE, Ymir launch, or native-math census was
performed in this round. The suppression path must not be promoted or used for
the controller-owned manual checkpoint until the seam and tests below exist.

## Exact geo-walk mutation inventory

The audit began from the real `geo_process_root()` dispatch in
`src/game/rendering_graph_node.c`, then followed every area/actor callback
class reachable from geo layouts. The layouts name 54 distinct callback
symbols across `GEO_ASM`, `GEO_SWITCH_CASE`, camera-frustum, camera, and
background nodes. The stateful classes are:

| Mutation class | Exact write sites | Why whole-walk suppression is unsafe |
| --- | --- | --- |
| Object and held-object animation | `rendering_graph_node.c:899-917`, called for objects at `:1185` and held objects at `:1316` | Writes `AnimInfo.animFrame`, `animFrameAccelAssist`, and `animTimer`. Gameplay reads these fields in `object_helpers.c:979-1020`, `mario.c:49-58`, and Mario action code for completion, frame events, and sounds. |
| Painting floor/warp state | `paintings.c:522-606`, `:1110-1138`, `:1227-1284` | Writes painting floor-entry/ripple state, Mario floor/position snapshots, DDD painting position/status, save flags, and warp-driving state. `behaviors/ddd_warp.inc.c:4` reads `gDddPaintingStatus`. |
| WDW environment water | `moving_texture.c:304-325` | Writes `gEnvironmentRegions[*].height` and `gWdwWaterLevelSet`; the environment-region array is the source water-height data consumed by gameplay/collision and rendering. |
| Moving-texture state | `moving_texture.c:334-341`, `:687-701`, `:953-974` | Advances `gMovtexCounter`/`gMovtexCounterPrev` and mutates persistent moving-texture vertex offsets. Omitting it freezes or phase-shifts source visual state. |
| Flying-carpet state | `geo_misc.c:112-125`, `:133-188` | Advances the ripple timer and writes `gFlyingCarpetState` from platform/object state; `shadow.c:607` consumes that state. The state update is interleaved with dynamic display-list allocation. |
| Camera FOV evolution | `camera.c:11490-11587`, `:11593-11638` | Approaches and shakes `sFOVState`, writes the perspective node FOV, and advances shake phase/decay. Skipping it changes source camera evolution and the Saturn snapshot input. |
| Matrix-derived object positions | `behavior_actions.c:160-170`, `object_helpers.c:38-49`, `behaviors/king_bobomb.inc.c:4-16` | Geo callbacks call `obj_update_pos_from_parent_transformation()` and `obj_set_gfx_pos_from_pos()` for held/projectile/parent-linked objects. These require the graph's current object, camera, and matrix stack. |
| Environment particles and generated-node state | `level_geo.c:16-53`, `geo_misc.c:133-188`, `object_helpers.c:53-93` | Advances environment particles/counters and rewrites generated-node flags/layers while also allocating display-list data. |
| Traversal-owned graph state | `rendering_graph_node.c:1153-1223`, `:1316`, `:1431` | Writes `throwMatrix`, `cameraToObject`, temporary `sharedChild->parent` links, current graph globals, and held-object animation state. Some values are restored; `cameraToObject` and animation state persist. |

This inventory rules out an animation-only object-list loop: it would still
omit painting/warp, water, camera, attachment, and callback-owned state. It
also rules out calling the existing callbacks with a new context without
auditing them individually; most state writes are guarded specifically by
`GEO_CONTEXT_RENDER`, alongside allocations and display-list emission. A
state-only traversal that recreates the matrix/global context may be possible,
but it is not a bounded change until those 54 callback symbols are classified
and the stateful ones are split from construction.

## Test-gap audit

`tools/saturn/test_source_render_suppression.py` extracts the first
`if (!scene_graph_suppressed)` block in `render_game()`. Production contains
separate guarded blocks, so the test neither examines the full suppression
surface nor detects a new hidden state mutation. It asserts source placement,
not behavior. `runtime_contract_test.c` proves only policy/counter storage.

The following red/green behavioral evidence is required before implementation
can be accepted:

1. Differential animation progression for normal graph execution versus the
   state-only seam, including accelerated, looping, no-loop, same-timer, and
   held-object cases (`animFrame`, accelerator, and timer all compared).
2. Actual `render_game()` behavior for warp delay/decrement/completion and
   unconditional `D_8032CE74`/`D_8032CE78` cleanup in both policies.
3. Sourceboot tick behavior proving both display and graph policies are
   restored after the game-loop call on every return path.
4. Behavioral fixtures for every retained gameplay-affecting callback class,
   especially painting/DDD warp and WDW environment-water state.
5. A whole-function source audit that rejects any suppressed call not on the
   construction-only allowlist; it may supplement but not replace the C
   behavioral/differential tests.

No new red test was committed in this round because there is not yet an
approved production seam for a differential oracle. Committing a test that
only describes animation would falsely imply the remaining callback classes
are covered.

## Existing evidence, correctly scoped

The original focused evidence remains historical only:

- `tools/saturn/test_source_render_suppression.py`: PASS (1 structural test).
- Host runtime-contract executable: PASS for suppression flags and counters.
- `Fast3dProfileDecodeTests`: PASS (13 tests, 1 expected skip).
- The aggregate `test_tools.py` run had 17 unrelated dirty route-schema errors.

Those results do not establish semantic equivalence and do not satisfy the
blocking quality review. PC and N64 builds are unsupported; this report makes
no non-Saturn compile-compatibility claim. Commits `658d5ad9` and `9904097e`
are historical portability work superseded as gates by the Saturn-only scope
commit `a00cdd17`.

## Remaining gates

- Design and test a bounded state-only seam, or retain this task as blocked.
- Run the focused host behavioral/differential tests listed above plus the
  source-policy/runtime/profile gates. Keep any failed or unexecuted gate open.
- Obtain independent spec review and independent quality review of the full
  replacement range, not only `4a8fe1ce`.
- Only after both reviews are clean: controller-owned serial experimental CUE
  build and Ymir manual test with the established demo role.
