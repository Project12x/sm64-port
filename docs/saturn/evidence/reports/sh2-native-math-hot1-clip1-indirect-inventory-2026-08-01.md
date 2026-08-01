# SH-2 hot1/clip1 indirect-transfer inventory

Pinned source commit: `7f003be1ee729c9fc90cab8571aab650209fb664`.

Both routes use code-only analysis with `--audit-observation-only`. The
analyzer-observed route-oracle SHA-256 is
`3bde797d9f07323b112b297c49ff4debd2a786c81d1e1be382feaf857f278a2f`.
The companion JSON preserves every unresolved-transfer field emitted by the
analyzer and adds a classification to each record.

## Commands and build evidence

```text
cd /d/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge && source /d/Code/RetroDev/sm64-saturn-port/sm64-port/.yaul.env && cd src/port/saturn/sourceboot && make -j1 SATURN_DEMO_PATH=1 SATURN_SOURCEBOOT_ROUTE_REPLAY=1 SATURN_SOURCEBOOT_CAMERA_ROUTE=<0|1> SATURN_ATAN2_VARIANT=2 SATURN_SOURCE_CART_STAGE_SECTORS=16 SATURN_DEMO_HOT_PROMOTION=1 SATURN_DEMO_NEAR_CLIP=1 SATURN_RENDERER_PIPELINE=2 HOST_CC=C:/msys64/mingw64/bin/gcc.exe verify
```

| Route | ELF | SHA-256 | Expected verify failure | Observation |
| --- | --- | --- | --- | --- |
| 0 | `build/saturn/sourceboot/e2-bob-demo-replay-camroute0-atan2v2-camv1-stage16-r6000-slave1-poly0-hot1-clip1-bsp1-frag0-pipe2/obj/sm64-saturn-sourceboot-e2.elf` | `c60bddf89afe796a9a883e371bf6d0ffd322910303f5d8e8e47e2199c0a930cc` | total 599 vs fixed 582; 13 unresolved transfers | retained at `.tmp-task1-inventory-20260801/route0-observation.json` |
| 1 | `build/saturn/sourceboot/e2-bob-demo-replay-camroute1-atan2v2-camv1-idle0-disc0-range0-stage16-r6000-slave1-poly0-hot1-clip1-bsp1-frag0-pipe2/obj/sm64-saturn-sourceboot-e2.elf` | `661af2236669e81ff18973c314d1211f8480c1705db78de09f9d5c311679f1c3` | total 601 vs fixed 582; 14 unresolved transfers | retained at `.tmp-task1-inventory-20260801/route1-observation.json`, Idle priority, 247.027 s |

The route-1 first build stopped on MSYS `/tmp` permission denial. Its one
subsequent serial build used a task-scratch `TMPDIR` only; its make flags were
unchanged. Route-1 analysis was later allowed one Idle-priority, ten-minute
cap attempt and completed within that cap.

## Classification inventory

The entries are statically derived from the pinned GeoLayout and route data;
they are not claims of replay-observed targets.

| Classification | Dispatchers / offsets | Route coverage |
| --- | --- | --- |
| `dynamic-dispatch` | `_geo_process_held_object +84, +338`; `_geo_process_node_and_siblings +538, +1104, +1232, +5318, +5354` | 0, 1 |
| `static-helper-provenance` | `_geo_process_held_object +296`; `_geo_process_node_and_siblings +2556`; `_guMtxF2L +142` | 0, 1 |
| `other-dynamic-dispatch` | `_exec_display_list +30`; `_level_script_execute +78`; `_sm64_saturn_source_runtime_read_controllers +26` | 0, 1 |
| `other-dynamic-dispatch` | `_render_course_complete_lvl_info_and_hud_str +684` | 1 only |

Thus the nine GraphNode-family records are **seven genuine callback dispatches
and two statically knowable helper calls**, not nine callbacks. Including
`_guMtxF2L +142` yields three static-helper provenance misses. Route 0 has
13 unresolved transfers and route 1 has 14; both have zero unresolved effects.

## Verification result

The `verify` failures and nonzero observation exits are expected pre-gate
evidence. This inventory is the Task 2/3 baseline; it does not alter analyzer
code, the route oracle, contracts, or helper totals.
