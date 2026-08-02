# Post-manifest Wave 1 gate delta

## Scope and preserved evidence

This is a host-only diagnosis of the fresh gate failure after `9907ec8`. No
target build, cross tool, CUE/ISO generation, emulator, or tracked-code edit
was run. Evidence inspected:

- fresh linked camv3/stage8 ELF and its already-generated `.asm`/`.sym`
  (2026-08-02 13:19);
- `9907ec8` and its checked route-oracle additions;
- `audit-dispatcher-manifest-report.md`;
- the verifier, immutable v2 contract, and relevant C sources.

## 1425 versus the fixed 582 contract

`9907ec8` changes only the verifier/oracle/tests/report, not target program
source. Its 37 checked dispatcher/target pairs recursively add the five active
BOB GraphNode callbacks, five level callbacks (one shared), and 27 GeoLayout
command handlers to the audit closure. Those targets then add their own direct
descendants. The increase to 1425 is therefore a legitimate static-closure
expansion caused by making previously opaque dispatch edges visible. The 12
new unresolved transfers are the next frontier exposed by that expansion.

This does **not** justify repinning `EXPECTED_TOTAL 582`. The count is an exact
native-math contract, and the larger closure demonstrates that the current
route still exposes considerably more helper-shaped math than the contract
allows. It is also not a runtime weighting: one-time graph construction and
cold cutscene paths count statically, so 1425 must not be presented as an FPS
ratio or proof that every newly reached helper executes in the BOB replay.

## Classification of the 12 transfers

| Transfer | Classification | Evidence / required disposition |
|---|---|---|
| `_camera_course_processing +286` | Genuine dynamic dispatcher | Calls `sCameraTriggers[level][b].event(c)` for a matching-area trigger. Derive the BOB trigger-event set from `sCameraTriggers[LEVEL_BOB]`. |
| `_camera_course_processing +316` | Genuine dynamic dispatcher | Calls the same trigger event for the default-area case. It should share the same checked BOB event set; caller-granular Phase-A declarations will cover both sites. |
| `_init_graph_node_perspective +84` | Genuine dynamic dispatcher | Invokes the non-null `GraphNodeFunc nodeFunc` with `GEO_CONTEXT_CREATE`; derive perspective callbacks from reached BOB GeoLayouts. |
| `_init_graph_node_switch_case +74` | Genuine dynamic dispatcher | Invokes its non-null `GraphNodeFunc nodeFunc`; derive switch-case callbacks from reached BOB GeoLayouts. |
| `_init_graph_node_camera +90` | Genuine dynamic dispatcher | Invokes its non-null `GraphNodeFunc func`; derive camera-node callbacks from reached BOB GeoLayouts. |
| `_init_graph_node_generated +60` | Genuine dynamic dispatcher | Invokes its non-null `GraphNodeFunc gfxFunc`; derive generated-node callbacks from reached BOB GeoLayouts. |
| `_init_graph_node_background +70` | Genuine dynamic dispatcher | Invokes its non-null background `GraphNodeFunc`; derive background callbacks from reached BOB GeoLayouts. |
| `_init_graph_node_held_object +78` | Genuine dynamic dispatcher | Invokes its non-null held-object `GraphNodeFunc`; derive held-object callbacks from reached BOB GeoLayouts. |
| `_next_lakitu_state` (single reported site) | Static-recovery gap | The C function contains only named direct calls and compiler soft-float/integer helpers; it has no function-pointer call. Preserve the real instruction shape as a regression and recover the concrete target. |
| `_play_cutscene` (single reported site; preserved ASM `+186`) | Genuine dynamic dispatcher | The `CUTSCENE` macro invokes `cutscene[sCutsceneShot].shot(c)`. A manifest must be derived from the compiled cutscene tables/shot entries, with any route narrowing stated explicitly. |
| `_play_mode_change_level` (single reported site; preserved ASM `+14`) | Genuine dynamic dispatcher | Invokes non-null `sTransitionUpdate(&sTransitionTimer)`. Derive targets from all reachable assignments to `sTransitionUpdate`. |
| `_update_lakitu` (single reported site) | Static-recovery gap | The C function contains only named direct calls and compiler arithmetic helpers; it has no callback invocation. Preserve the real instruction shape and recover the concrete target. |

The six `init_graph_node_*` cases are not false positives merely because their
callbacks are stored in the node. Each initializer also immediately invokes a
non-null callback in `GEO_CONTEXT_CREATE`, so they require checked dispatch
manifests rather than parser suppression.

## Smallest fail-closed next step

1. Repair only the two static-recovery gaps (`next_lakitu_state` and
   `update_lakitu`) with exact linked-instruction regressions. Require a real
   direct-call fact and zero unresolved transfer at each site; do not add
   declarations for them.
2. In a separate task, extend the existing source derivation to the ten real
   dynamic sites: six initializer callback sets, the BOB camera-trigger event
   set, cutscene shot callbacks, and transition-update callbacks. Compare the
   complete derived mapping against both oracle edge sets, with omission,
   underived-addition, stale, and exact-site tests. State the existing
   dispatcher-granular imprecision.
3. Run one serial target audit after both host changes. Keep 582 fixed and keep
   every still-unlisted transfer visible. If the transfer gate clears while
   the helper total remains above 582, treat that remaining delta as the next
   native-math optimization inventory, not as permission to generate or test a
   CUE.

This ordering is intentionally fail-closed: static analyzer defects cannot be
hidden behind declarations, and genuine callback closure is admitted only
from checked source data.

## Static-recovery implementation

The two static sites are now represented by exact host regressions at their
linked caller offsets. `_next_lakitu_state +108` must emit the direct fact
`___subsf3`; `_update_lakitu +464` must emit the direct fact `___addsf3`.
Neither site may retain an unresolved transfer. A near-match in which the
`find_floor` output argument is an unknown frame-derived alias remains
unresolved and emits no direct fact.

Both positive fixtures failed before the analyzer change. The
`_next_lakitu_state` loop initially resolved `r2`, then collapsed its advancing
`r9`/`r14` frame pointers to an address-anywhere state on the backedge. Their
non-overlapping output stores consequently poisoned the earlier `r2` spill.
The `_update_lakitu` fixture treated the `find_floor` output pointer at the
exclusive end of the four-byte helper spill as overlapping that spill.

The analyzer now retains monotone frame-relative pointer ranges through joins
and immediate increments. Backedge widening makes an expanding endpoint
unbounded while preserving the opposite bound, so a forward-only store range
cannot overwrite an earlier spill, but a range that may overlap a slot still
poisons it. Exact escaped-pointer invalidation now uses the correct half-open
longword interval. `MaybeStackPtr` and other unknown frame aliases retain the
existing whole-frame fail-closed behavior.

Focused verification covered both exact sites, the unknown-alias control, the
adjacent-slot rule, and a truly overlapping escaped pointer:

```text
Ran 5 tests in 0.019s
OK
```

The complete host verifier suite then passed:

```text
Ran 172 tests in 0.275s
OK
```

An observation-only run against the preserved camv3 ELF was stopped by the
90-second bound after producing no stdout and no JSON result. It is recorded
as incomplete, not passed or failed. No target build, CUE/ISO generation, or
Ymir session was run. At that static-recovery stage, `EXPECTED_TOTAL 582` and
both route-oracle manifest edge sets remained unchanged; the following
host-only step deliberately extends the checked edge sets while preserving the
582 contract.

## Checked post-manifest dynamic dispatchers

The next host-only step adds 103 checked dispatcher/target pairs for eight of
the ten genuine dynamic transfers exposed by the camv3 closure:

- the six callback-bearing `init_graph_node_*` families: 22 pairs;
- `_play_cutscene +186`: 80 pairs; and
- `_play_mode_change_level +14`: one pair.

Every pair is present once as `STATIC_MANIFEST_EDGE` and once as
`INDIRECT_EDGE`. The pinned oracle digest is now
`084313eeeb16ace7a05b252a0519bfbc86cc2f43db1260292d1db77da388af44`.
`EXPECTED_TOTAL 582` remains unchanged.

### BOB camera-table correction

The two `_camera_course_processing` transfers at `+286` and `+316` are not
declared in this change. Source derivation found that the sourceboot route
selects `LEVEL_BOB` in
`src/port/saturn/sourceboot/source_entry.c`, whose `DEFINE_LEVEL` row in
`levels/level_defines.h` supplies `_` as its camera-table field. In
`src/game/camera.c`, `_` is defined as `NULL` while that header is projected
into `sCameraTriggers`. Thus `sCameraTriggers[LEVEL_BOB]` is NULL and its exact
event set is empty.

The nearby `sCamBOB` array is explicitly documented as unused and is not the
selected table. Declaring `cam_bob_tower` or `cam_bob_default_free_roam` would
therefore fabricate route provenance. Tests require both oracle edge sets for
`_camera_course_processing` to stay empty and require both exact dynamic
transfers to remain unlisted. A separate configured-null-table proof is needed
to discharge them without inventing callbacks.

### GeoLayout initializer derivation and exact sets

The derivation reuses the pinned recursive sourceboot/BOB closure documented
above: nine reached `LevelScript` arrays provide the `AREA` and
`LOAD_MODEL_FROM_GEO` roots, and `GEO_BRANCH`/`GEO_BRANCH_AND_LINK` traversal
reaches 125 GeoLayout arrays from `actors/**/geo.inc.c` and
`levels/bob/**/*.c`. The six sets are read from the precise callback arguments
of the corresponding reached macros defined by `include/geo_commands.h`.

`_init_graph_node_perspective` (`GEO_CAMERA_FRUSTUM_WITH_FUNC`):

- `_geo_camera_fov`

`_init_graph_node_switch_case` (`GEO_SWITCH_CASE`):

- `_geo_switch_anim_state`
- `_geo_switch_mario_cap_effect`
- `_geo_switch_mario_cap_on_off`
- `_geo_switch_mario_eyes`
- `_geo_switch_mario_hand`
- `_geo_switch_mario_stand_run`

`_init_graph_node_camera` (`GEO_CAMERA`):

- `_geo_camera_main`

`_init_graph_node_generated` (`GEO_ASM`):

- `_geo_cannon_circle_base`
- `_geo_envfx_main`
- `_geo_mario_hand_foot_scaler`
- `_geo_mario_head_rotation`
- `_geo_mario_rotate_wing_cap_wings`
- `_geo_mario_tilt_torso`
- `_geo_mirror_mario_backface_culling`
- `_geo_mirror_mario_set_alpha`
- `_geo_move_mario_part_from_parent`
- `_geo_scale_bowser_key`
- `_geo_update_held_mario_pos`
- `_geo_update_layer_transparency`

`_init_graph_node_background` (`GEO_BACKGROUND`):

- `_geo_skybox_main`

`_init_graph_node_held_object` (`GEO_HELD_OBJECT`):

- `_geo_switch_mario_hand_grab_pos`

### Cutscene derivation and exact set

`src/game/camera.c` does not provide source dataflow that bounds
`c->cutscene` to a narrower BOB-only value set. The fail-closed manifest
therefore follows all 43 unique `struct Cutscene` arrays selected by the
compiled `CUTSCENE` cases in `play_cutscene`; the unreferenced
`sCutsceneWaterDeath` array is excluded. Their 80 unique shot callbacks are:

- `_cutscene_bbh_death`
- `_cutscene_bowser_arena`
- `_cutscene_bowser_arena_dialog`
- `_cutscene_bowser_arena_end`
- `_cutscene_cap_switch_press`
- `_cutscene_credits`
- `_cutscene_dance_closeup`
- `_cutscene_dance_default_rotate`
- `_cutscene_dance_fly_away`
- `_cutscene_death_standing`
- `_cutscene_death_stomach`
- `_cutscene_dialog`
- `_cutscene_dialog_end`
- `_cutscene_dialog_set_flag`
- `_cutscene_door_end`
- `_cutscene_door_fix_cam`
- `_cutscene_door_follow_mario`
- `_cutscene_door_loop`
- `_cutscene_door_mode`
- `_cutscene_door_move_behind_mario`
- `_cutscene_door_start`
- `_cutscene_double_doors_end`
- `_cutscene_end_waving`
- `_cutscene_ending_cake_for_mario`
- `_cutscene_ending_dialog`
- `_cutscene_ending_kiss`
- `_cutscene_ending_mario_fall`
- `_cutscene_ending_mario_land`
- `_cutscene_ending_mario_land_closeup`
- `_cutscene_ending_mario_to_peach`
- `_cutscene_ending_peach_appears`
- `_cutscene_ending_peach_descends`
- `_cutscene_ending_peach_wakeup`
- `_cutscene_ending_stars_free_peach`
- `_cutscene_ending_stop`
- `_cutscene_enter_cannon_end`
- `_cutscene_enter_cannon_raise`
- `_cutscene_enter_cannon_start`
- `_cutscene_enter_painting`
- `_cutscene_enter_pool`
- `_cutscene_enter_pyramid_top`
- `_cutscene_exit_bowser_death`
- `_cutscene_exit_bowser_succ`
- `_cutscene_exit_fall_to_castle_grounds`
- `_cutscene_exit_non_painting_succ`
- `_cutscene_exit_painting`
- `_cutscene_exit_painting_end`
- `_cutscene_exit_to_castle_grounds_end`
- `_cutscene_exit_waterfall`
- `_cutscene_grand_star`
- `_cutscene_grand_star_fly`
- `_cutscene_intro_peach_dialog`
- `_cutscene_intro_peach_fly_to_pipe`
- `_cutscene_intro_peach_letter`
- `_cutscene_intro_peach_mario_appears`
- `_cutscene_intro_peach_reset_fov`
- `_cutscene_key_dance`
- `_cutscene_mario_dialog`
- `_cutscene_non_painting_death`
- `_cutscene_non_painting_end`
- `_cutscene_prepare_cannon`
- `_cutscene_prepare_cannon_end`
- `_cutscene_pyramid_top_explode`
- `_cutscene_pyramid_top_explode_end`
- `_cutscene_quicksand_death`
- `_cutscene_read_message`
- `_cutscene_read_message_end`
- `_cutscene_read_message_set_flag`
- `_cutscene_red_coin_star`
- `_cutscene_red_coin_star_end`
- `_cutscene_sliding_doors_open`
- `_cutscene_star_spawn`
- `_cutscene_star_spawn_back`
- `_cutscene_star_spawn_end`
- `_cutscene_suffocation`
- `_cutscene_unlock_key_door`
- `_cutscene_unused_exit_focus_mario`
- `_cutscene_unused_exit_start`
- `_cutscene_unused_loop`
- `_cutscene_unused_start`

### Transition-update derivation

The complete `src/**/*.c` assignment path contains only
`sTransitionUpdate = updateFunction` and clears to `NULL`. Every call to
`level_set_transition` passes either `NULL` or `basic_update`; the sole
non-call state is omitted. The exact `_play_mode_change_level` set is:

- `_basic_update`

### Validation and limitations

The tests compare the complete source-derived mapping with a hand-checked
literal mapping and with both oracle edge sets. They exercise omission and
underived-addition mutations against the derived map, the eight exact linked
offsets, mismatched dispatcher ownership, one static near-match per family,
and grouped contribution for `_geo_camera_main`, which is shared by
`_geo_call_global_function_nodes_helper` and `_init_graph_node_camera`.

The accepted Phase-A limitation remains dispatcher granularity: each dynamic
site inherits its dispatcher's complete source-derived callback set. In
particular, `_play_cutscene +186` inherits all 80 shots because no narrower
static route proof exists. Parser-classified static transfers remain unlisted.

The focused RED first failed because all eight derived families were absent
from the oracle and because the BOB camera-table derivation did not yet exist.
After implementation, the focused set passed six tests. The complete host
verifier suite passed:

```text
Ran 176 tests in 0.920s
OK
```

No external library or upstream code was used. No target build, cross-tool
audit, CUE/ISO generation, Ymir session, or FPS measurement was run.
