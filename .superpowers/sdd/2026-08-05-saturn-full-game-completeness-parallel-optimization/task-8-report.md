# Task 8 implementation report

## Outcome

Blocked/deferred by evidence. No exact bounded state-only seam exists at the
current generic graph boundary, so the full `geo_process_root()` walk remains
active and authoritative. No renderer, source tick, source callback, or
suppression-policy behavior was changed.

The reserved `sm64_saturn_source_geo_update_state()` contract returns `false`
for every request and preserves the caller's digest. It is intentionally not
called from sourceboot or the source runtime.

## RED evidence

Before `saturn_source_geo_state.h/.c` existed:

- `test_source_geo_state_contract.py`: 7 source-domain checks passed and the
  API-containment test errored because the implementation was absent.
- `verify-source-geo-state-diff`: C compilation failed because the header and
  implementation were absent.

These failures were the expected missing-contract RED state.

## Why the seam is deferred

The executable source audit and digest fixture cover state, not merely draw
output:

- animation: `geo_mario_hand_foot_scaler()` advances punch state and scale in
  `GEO_CONTEXT_RENDER`;
- visibility: `geo_switch_mario_hand()` selects the active source branch in
  `GEO_CONTEXT_RENDER`;
- painting/warp: `geo_painting_draw()` constructs the painting display list
  and mutates painting floors in the same callback; warp-transition state is
  outside the suppressed graph block and remains unchanged in the reference
  digest;
- water/moving texture: WDW environment-region height and moving-texture pause
  counters mutate during render callbacks;
- camera/matrix-derived object state: `geo_camera_main()` updates the graph
  camera, and `geo_process_object()` derives all three `cameraToObject` axes
  before `obj_is_in_view()`;
- lifecycle: object `throwMatrix` ownership/cleanup occurs inside the walk;
- generated display construction: `geo_process_generated_list()` invokes the
  callback before `geo_append_display_list()`. Suppressing at the append point
  retains state but has already paid callback display construction; suppressing
  before the callback loses the state above.

The C fixture compares separate animation, painting, warp, water,
moving-texture, camera/matrix/object, lifecycle, and visibility digests for the
normal reference walk versus the old root skip. All graph-owned domains differ;
warp remains equal because its transition update is outside the old guard.

## Verification

- GREEN: `.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_source_geo_state_contract.py`
  — 8/8.
- GREEN through DLL-preflight: `powershell -ExecutionPolicy Bypass -File
  tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1
  verify-source-geo-state-diff` — C11 `-Wall -Wextra -Werror`, executable exit
  0. The target uses the pinned Python runtime to launch the fixture, avoiding
  the repository's known MSYS direct-EXE quote failure.
- OPEN inherited gate: the prescribed combined command reaches
  `verify-source-render-policy` but its internal sourceboot `make -pn` fails on
  the pre-existing recursive `SATURN_DEMO_BSP_FRAGMENTS` /
  `SATURN_DEMO_FRAGMENT_MODE` defaults. Explicitly pinning both to zero advances
  farther, then fails on a missing generated
  `saturn_build_identity_spec.json` prerequisite. Neither failure touches Task
  8 files, and neither was widened into this bounded optimization task.

## Claims not made

No target build, Ymir run, manual semantic check, native-math closure, or FPS
improvement is claimed. The legacy experimental whole-walk skip remains a
diagnostic only.
