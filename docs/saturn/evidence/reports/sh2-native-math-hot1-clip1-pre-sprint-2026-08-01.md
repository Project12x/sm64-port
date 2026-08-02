# hot1/clip1 pre-sprint native-math closure

## Status

The 2026-08-01 pre-sprint hot1/clip1 sourceboot replay ELF was built
serially and its native-math closure gate **failed as intended**.  This is
baseline evidence, not an accepted performance result: the stricter closure
gate exposed a 656-helper route where the pinned post-conversion contract
expects 582, plus six unresolved indirect transfers.  No emulator was
launched.

## Reproducible build

- Producer source commit (the ELF): `cb7adc8355e7ceb16fc9d9289557a6146d1a23a5`
- Auditor/tool commit (the gate that inspected that ELF): `196f4ca`
- ELF:
  `build/saturn/sourceboot/e2-bob-demo-replay-camroute1-atan2v2-camv3-idle0-disc0-range0-stage8-r6000-slave1-poly0-hot1-clip1-bsp1-frag0-pipe2/obj/sm64-saturn-sourceboot-e2.elf`
- ELF SHA-256:
  `e1877d8a836b0b0a951a7028407f5a3ddd7dd28e170114e5ed01db8a3a47f27d`
- Profile: `SATURN_DEMO_PATH=1`, replay enabled, camera route enabled,
  camera variant 3, stage sectors 8, hot promotion, near clip, BSP order,
  renderer pipeline 2.
- Command result: `make -C src/port/saturn/sourceboot -B -j1 ... verify-sim-math-route`
  exited 1 after the complete ELF build, solely because the audit failures
  below were detected.

The linked worktree intentionally does not contain `.yaul.env`; the MSYS2
shell sourced the parent worktree's local environment (`../../.yaul.env`).
It also created the mandated
`/tmp/sm64-saturn-$MSYSTEM` directory before building, so all `sh-elf-*`
tools ran with their MSYS2 DLL dependencies discoverable.

## Closure result

- Contract helper total: `582`
- Observed helper total: `656`
- Unresolved transfers: `6`

| Dispatcher | Offset | Transfer |
| --- | ---: | --- |
| `_create_skybox_facing_camera` | `+132` | `jsr` |
| `_exec_display_list` | `+30` | `jmp` |
| `_geo_call_global_function_nodes_helper` | `+40` | `jsr` |
| `_geo_process_held_object` | `+296` | `jsr` |
| `_geo_process_node_and_siblings` | `+2556` | `jsr` |
| `_level_cmd_call` | `+18` | `jsr` |

The `_guMtxF2L` stack-spill form is not in that list: the parser's existing
stack-spill recovery test proves its `jsr @r7` resolves to the direct helper
fact.  A transfer retaining stack provenance is now explicitly classified
`static` only when that stack slot retains one linked function-address
literal and no unknown-store path; an unknown function-pointer parameter
moved through the stack, including one merged with a literal-backed path,
remains `dynamic`.

## Declared static callback manifest

These entries are declarations for parser-classified `dynamic` transfers
only.  GraphNode callbacks are statically derived from the pinned
`levels/bob/areas/1/geo.inc.c`; level-script callbacks are statically derived
by recursively traversing the pinned sourceboot level-script entry, its
`EXECUTE(level_bob_entry)` target `levels/bob/script.c`, and that script's
`JUMP_LINK(script_func_global_1/4/15)` targets in `levels/scripts.c`. The
test suite checks both derivations against the checked-in manifest.

- `_geo_process_node_and_siblings` → `_geo_skybox_main`,
  `_geo_camera_fov`, `_geo_camera_main`, `_geo_envfx_main`,
  `_geo_cannon_circle_base`
- `_geo_process_held_object` → `_geo_switch_mario_hand_grab_pos`
- `_level_script_execute` → `_level_cmd_init_level`,
  `_level_cmd_get_or_set_var`, `_level_cmd_call`,
  `_level_cmd_load_and_execute`, `_level_cmd_clear_level`,
  `_level_cmd_jump`, `_level_cmd_set_register`, `_level_cmd_alloc_level_pool`,
  `_level_cmd_free_level_pool`, `_level_cmd_load_model_from_dl`,
  `_level_cmd_load_model_from_geo`, `_level_cmd_begin_area`,
  `_level_cmd_call_loop`, `_level_cmd_end_area`, `_level_cmd_exit`,
  `_level_cmd_jump_and_link`, `_level_cmd_load_mio0`,
  `_level_cmd_load_mio0_texture`, `_level_cmd_load_raw`,
  `_level_cmd_set_macro_objects`, `_level_cmd_init_mario`,
  `_level_cmd_set_mario_start_pos`, `_level_cmd_place_object`,
  `_level_cmd_return`, `_level_cmd_set_music`, `_level_cmd_show_dialog`,
  `_level_cmd_sleep2`, `_level_cmd_set_terrain_data`,
  `_level_cmd_set_terrain_type`, `_level_cmd_create_warp_node`
- `_sm64_saturn_source_runtime_read_controllers` → `_controller_saturn_read`

An `INDIRECT_EDGE` is stale when its reachable dispatcher names a callback
absent from this static manifest; the gate rejects that declaration before it
can extend the route closure.

## Next disposition

Do not revise the 582 contract or fabricate call facts to make this result
pass.  The six unresolved sites and 74-helper total mismatch are an explicit
post-build closure-audit queue for the FPS sprint.  Subsequent wave work may
proceed with this truthful baseline, but no change may claim a clean native
math closure until the same profile passes this gate.
