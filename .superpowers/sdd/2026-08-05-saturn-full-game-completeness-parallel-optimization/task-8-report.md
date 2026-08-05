# Task 8 implementation report

## Outcome

Blocked/deferred without the required real graph differential. The static
audit identifies no obvious bounded state-only seam at the current generic
graph boundary, but it cannot prove that none exists. The full
`geo_process_root()` walk therefore remains active and authoritative. No
renderer, source tick, source callback, or suppression-policy behavior was
changed.

The reserved `sm64_saturn_source_geo_update_state()` contract returns `false`
for every request and preserves the caller's digest. It is intentionally not
called from sourceboot or the source runtime.

## Contract-construction RED only

Before `saturn_source_geo_state.h/.c` existed:

- `test_source_geo_state_contract.py`: 7 source-domain checks passed and the
  API-containment test errored because the implementation was absent.
- `verify-source-geo-state-diff`: C compilation failed because the header and
  implementation were absent.

These were valid RED failures for adding the fail-closed reservation, but they
do **not** satisfy Task 8's required RED: neither test executes the real
`geo_process_root()` path in normal and suppressed modes. The true differential
checklist item remains unchecked.

## Static audit observations

The source-text audit identifies the following coupling. These observations
bound follow-up work; they are not runtime differential evidence:

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

The C fixture is an **illustrative model** of separate animation, painting,
warp, water, moving-texture, camera/matrix/object, lifecycle, and visibility
digests. It demonstrates the intended shape of a future comparison and keeps
warp equal because its transition update is outside the old guard. It invokes
neither `geo_process_root()` nor the original callbacks and cannot establish
that any real digest differs.

## Verification

- GREEN static audit/containment contract:
  `.\.venv-saturn-tools\Scripts\python.exe
  tools\saturn\test_source_geo_state_contract.py` — 8/8.
- GREEN through DLL-preflight: `powershell -ExecutionPolicy Bypass -File
  tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1
  verify-source-geo-state-diff` — fail-closed API plus illustrative digest
  model compile with C11 `-Wall -Wextra -Werror`, executable exit 0. Despite
  the legacy target name required by the plan, this is not a real graph
  differential. The target uses the pinned Python runtime to launch the
  fixture, avoiding the repository's known MSYS direct-EXE quote failure.
- OPEN inherited gate: the prescribed combined command reaches
  `verify-source-render-policy` but its internal sourceboot `make -pn` fails on
  the pre-existing recursive `SATURN_DEMO_BSP_FRAGMENTS` /
  `SATURN_DEMO_FRAGMENT_MODE` defaults. Explicitly pinning both to zero advances
  farther, then fails on a missing generated
  `saturn_build_identity_spec.json` prerequisite. Neither failure touches Task
  8 files, and neither was widened into this bounded optimization task.

## Claims not made

No target build, Ymir run, manual semantic check, native-math closure, or FPS
improvement is claimed. No real normal/suppressed source-graph differential is
claimed. The legacy experimental whole-walk skip remains a diagnostic only,
and Task 8's differential/seam checklist stays unchecked.
