# Wave 1 checked dispatcher manifest report

## Result

The four remaining Wave 1 data dispatchers now have checked, source-derived
BOB callback manifests using the existing `STATIC_MANIFEST_EDGE` plus
`INDIRECT_EDGE` model:

- `_geo_call_global_function_nodes_helper +40`
- `_level_cmd_call +18`
- `_level_cmd_call_loop +18`
- `_process_geo_layout +90`

This is a static sourceboot/BOB route manifest. It is not described as a
deterministic-replay observation. The audit contract remains exactly
`EXPECTED_TOTAL 582`; neither the contract nor any target source was edited.

## Pinned source derivation

The derivation starts at `level_script_entry` in
`src/port/saturn/sourceboot/source_entry.c` and recursively follows only
`JUMP`, `JUMP_LINK`, `EXECUTE`, and `EXIT_AND_EXECUTE` targets defined in:

- `src/port/saturn/sourceboot/source_entry.c`
- `levels/bob/script.c`
- `levels/scripts.c`

That produces nine reachable `LevelScript` arrays: `level_script_entry`,
`sSourcebootLevelLoop`, `level_bob_entry`, `script_func_global_1`,
`script_func_global_4`, `script_func_global_15`, and the three BOB-local
scripts. The `CALL` and `CALL_LOOP` function arguments are read only from
those reached arrays.

The GeoLayout derivation takes only `LOAD_MODEL_FROM_GEO` and `AREA` arguments
from those nine arrays as roots. It resolves definitions from
`actors/**/geo.inc.c` and `levels/bob/**/*.c`, then recursively follows only
`GEO_BRANCH` and `GEO_BRANCH_AND_LINK`. The checked source currently yields 77
roots and 125 reached GeoLayout arrays. Their 27 used `GEO_*` command macros
are mapped to opcodes through `include/geo_commands.h`, then to handler symbols
through `GeoLayoutJumpTable` in `src/engine/geo_layout.c`.

The global-function traversal is narrower: it begins only at the reached
`AREA` root, `bob_geo_000488`. That root is defined in
`levels/bob/areas/1/geo.inc.c` and contains the five active BOB functional
callbacks below. Registered model GeoLayouts are not incorrectly treated as
children of the active area graph.

This implementation reuses the project's existing route-oracle format and
validation code directly. No external code or library was needed or copied.

## Exact declared sets

`_geo_call_global_function_nodes_helper`:

- `_geo_camera_fov`
- `_geo_camera_main`
- `_geo_cannon_circle_base`
- `_geo_envfx_main`
- `_geo_skybox_main`

`_level_cmd_call`:

- `_lvl_init_from_save_file`
- `_lvl_init_or_update`
- `_lvl_set_current_level`
- `_sourceboot_mark_save_file_exists`

`_level_cmd_call_loop`:

- `_lvl_init_or_update`

`_process_geo_layout`:

- `_geo_layout_cmd_branch`
- `_geo_layout_cmd_branch_and_link`
- `_geo_layout_cmd_close_node`
- `_geo_layout_cmd_end`
- `_geo_layout_cmd_node_animated_part`
- `_geo_layout_cmd_node_background`
- `_geo_layout_cmd_node_billboard`
- `_geo_layout_cmd_node_camera`
- `_geo_layout_cmd_node_culling_radius`
- `_geo_layout_cmd_node_display_list`
- `_geo_layout_cmd_node_generated`
- `_geo_layout_cmd_node_held_obj`
- `_geo_layout_cmd_node_level_of_detail`
- `_geo_layout_cmd_node_master_list`
- `_geo_layout_cmd_node_object_parent`
- `_geo_layout_cmd_node_ortho_projection`
- `_geo_layout_cmd_node_perspective`
- `_geo_layout_cmd_node_root`
- `_geo_layout_cmd_node_rotation`
- `_geo_layout_cmd_node_scale`
- `_geo_layout_cmd_node_shadow`
- `_geo_layout_cmd_node_start`
- `_geo_layout_cmd_node_switch_case`
- `_geo_layout_cmd_node_translation`
- `_geo_layout_cmd_node_translation_rotation`
- `_geo_layout_cmd_open_node`
- `_geo_layout_cmd_return`

There are 37 dispatcher/target pairs. Each appears once as a static manifest
edge and once as an indirect edge. `_lvl_init_or_update` is intentionally
present under both level dispatchers because the reached BOB script uses both
`CALL` and `CALL_LOOP` with that callback.

## Validation behavior

The exact source-to-manifest test compares the complete derived mapping to a
hand-checked literal mapping and then compares both oracle edge sets to it.
Removing a required callback or adding an underived callback breaks that exact
comparison. Duplicate input remains rejected by the parser. Missing owners,
unreachable dispatchers, unconsumed dispatchers, and targets reachable without
the declared callback group remain rejected.

The contribution check now handles the one legitimate shared target. Multiple
source-manifest-confirmed edges to the same callback are evaluated as a group:
removing all edges to that callback must remove it from closure. This permits
the two independently required `lvl_init_or_update` declarations without
allowing a callback already reachable through a direct or unrelated path.

Tests also cover all four exact Wave 1 caller/offset pairs, a parser-classified
static transfer in each declared dispatcher, and the accepted Phase-A
dispatcher-granularity rule. Static transfers remain unlisted and therefore
fail closed; a dispatcher declaration covers only parser-classified dynamic
transfers.

## Phase-A limitation

Declarations remain dispatcher-granular. Every structurally dynamic transfer
inside a declared dispatcher inherits that dispatcher's complete BOB-derived
callback set. This is deliberate accepted imprecision for Phase A, not
per-site resolution. The complete static derivation and stale-entry tests are
the compensating control.

## TDD and host verification

The first focused run failed because all 37 expected pairs were absent from
the checked oracle. A separate red regression showed that the previous
per-edge contribution rule rejected the real callback shared by `CALL` and
`CALL_LOOP`. After the scoped implementation, the focused command covered the
new source, omission/addition, exact-site, static-transfer, granularity, and
shared-target behavior plus the prior noncontribution controls:

```text
..\..\.venv-saturn-tools\Scripts\python.exe -m unittest <eight named tests>
```

Result: `Ran 8 tests in 0.065s` / `OK`.

Complete host verifier suite:

```text
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_verify_sh2_native_math.py
```

Fresh final result: `Ran 169 tests in 0.237s` / `OK`.

No target build, cross-tool audit, CUE/ISO generation, Ymir session, or
performance measurement was run. Therefore this host task does not yet claim
that the existing ELF passes the complete 582 gate; the next serial target
gate must establish that separately.
