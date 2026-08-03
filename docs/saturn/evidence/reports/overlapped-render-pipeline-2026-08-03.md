# Overlapped-render pipeline evidence — Task 1

## Current verdict

**SAFE-BLOCKED — quality-fix round 1/5.** Full-range review covered
`4a8fe1ce^..70cb3fe1`, not only the original implementation commit. The
reviewed Saturn policy suppressed all of `geo_process_root()`, but that
function is not a construction-only boundary. Safety commit `77ee306c` removes
all scene-graph suppression setter calls from `sourceboot_run_source_tick()`.
Accepted builds now retain the full geo walk; the reserved runtime policy and
counters remain dormant for a future behavior-tested seam.

No target build, CUE, Ymir launch, or native-math census was performed. The A1
performance path remains blocked and must not be promoted or used for the
controller-owned manual checkpoint until the seam and tests below exist.

## Task 1D — sealed geo-walk upper-bound diagnostic

### Current verdict and scope

**ACTIVE — review-fix round 1; rereview required.** Implementation commit
`98f26f26` adds the default-off `diag-skip-geo` configuration, and documentation
commit `f2b7ebf0` records its initial gates. The first independent specification
review is **NO-GO**: production containment is correct, but the initial host test
did not execute Make validation, inspected text before the experimental `#if`
rather than the actual normal `#else`, and this execution report had no Task 1D
entry. Review-fix round 1 addresses both Important findings without changing
the sealed runtime source. Independent spec rereview and quality review remain
open, so Task 1D is not `source-complete`.

This configuration is non-promotable. It exists only to measure an upper bound
on duplicate geo-walk cost. It knowingly invalidates geo-owned animation,
painting/warp, camera, water, moving-texture, flying-carpet, matrix-derived
object positions, and related graph state. BOB is only the deterministic
demonstrator; this is neither an A1 solution nor a full-game mode.

### Review-fix TDD evidence

RED command:

```powershell
& .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_source_render_suppression.py
```

RED result: exit 1; `Ran 4 tests in 0.016s`; `FAILED (errors=6)`. The new
behavioral Make and preprocessor-branch tests failed because their
`sourceboot_make`, `make_value`, and `extract_preprocessor_branches` helpers
did not yet exist. No production source was changed before this failure.

GREEN command:

```powershell
& .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_source_render_suppression.py
```

GREEN result: exit 0; `Ran 4 tests in 8.173s`; `OK`. The gate now performs six
real GNU Make parses: default-off, the accepted demo+replay diagnostic, a
non-binary value, and each incomplete prerequisite combination. It verifies
the evaluated macro value, compile definition, and output path, including
exactly one `diag-skip-geo` component in only the diagnostic output. The source
test structurally matches the experimental directive and its direct `#else`:
the diagnostic side is exactly setter-true, one
`game_loop_one_iteration()`, setter-false; the normal side contains one loop
call and no scene-graph setter.

Final post-refactor verification used the same focused command: exit 0;
`Ran 4 tests in 4.499s`; `OK`. `git diff --check` over the seven Task 1D files
also exited 0; only LF-to-CRLF checkout warnings were emitted.

Runtime-contract command:

```powershell
& 'C:\Program Files\PowerShell\7\pwsh.exe' -File tools\saturn\with-msys-toolchain.ps1 make -f Makefile.saturn.mk verify-runtime-contracts
```

Result: exit 1 after the three quad-map summaries. Windows Python received
`\\d\\Code...\\build\\saturn\\host-tests` from the MSYS wrapper and failed to
create it with `PermissionError: [WinError 5]`; Make stopped at
`Makefile.saturn.mk:192`. This is the previously observed host path-translation
failure. It remains an unpassed infrastructure gate, not a runtime-contract
failure and not a green result.

### Commits, review, and open gates

- Implementation: `98f26f26`.
- Initial documentation transition: `f2b7ebf0`.
- Review-fix round 1: this transition; the exact hash is appended by the
  follow-up evidence commit.
- Independent spec review: **NO-GO** on the initial range; both Important
  findings are addressed here, but rereview is pending.
- Independent quality review: not run.
- Runtime-contract wrapper: blocked by the recorded MSYS/Windows path issue.
- Target build and the one authorized Ymir run: not run; pending successful
  review and controller-owned serial execution.
- A1: still blocked on a behavior-tested state/render separation seam.

No target build or Ymir run occurred during Task 1D implementation or this
review-fix round.

## Fail-closed safety closure

TDD red command:

```powershell
& .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_source_render_suppression.py
```

Result before `77ee306c`: FAIL because the sourceboot body called the reserved
setter. Inspection showed the paired `set_scene_graph_suppressed(true)` enable
and `false` restore calls.

Implementation `77ee306c` removes both calls, leaves paired final-display
suppression intact, and documents the scene-graph API/counter ABI as reserved.
The source test scans the actual `sourceboot_run_source_tick()` body and
rejects any call to the scene-graph setter, independent of the argument used.

Focused green evidence:

- Source policy: PASS (2 tests).
- Runtime contract: compiled cleanly through `verify-source-render-policy`;
  direct execution of `build/saturn/host-tests/runtime-contract-test.exe`:
  PASS.
- `python -m unittest tools.saturn.test_tools.Fast3dProfileDecodeTests`:
  PASS (13 tests, 1 expected skip).

The wrapper target still exits nonzero at its known MSYS/Windows executable
handoff after compilation; the same emitted executable passes when invoked
directly. This is recorded as a wrapper failure, not a green aggregate target.

## Quality safety fix round 2/5 — documentation reconciliation

The ignored SDD progress ledger and Task 1 report now match the tracked source
of truth: Task 1 is **BLOCKED**, the Critical optimization finding remains
unresolved, and no optimization CUE build or Ymir launch is authorized. They
separately record `77ee306c` as **GO for fail-closed runtime safety**, not as an
A1 optimization approval.

The tracked plan now leaves the unsafe whole-walk suppression Step 5 unchecked
and explicitly BLOCKED. Its CUE/Ymir step is marked NOT AUTHORIZED until the
state-only seam and behavioral differential evidence exist. The plan summary,
architecture decision ledger, evidence, and SDD records all retain the same
safe-blocked status.

This round changed documentation only. No code, test, target, CUE, or Ymir
command was run. `git diff --check` is the sole round-2 verification gate.

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

The first test in `tools/saturn/test_source_render_suppression.py` extracts the
first `if (!scene_graph_suppressed)` block in `render_game()`. Production contains
separate guarded blocks, so the test neither examines the full suppression
surface nor detects a new hidden state mutation. It asserts source placement,
not behavior. The second test does enforce the fail-closed sourceboot caller
boundary. `runtime_contract_test.c` proves only policy/counter storage.

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

No state-only differential test was committed because there is not yet an
approved production seam for its oracle. The committed safety test is narrower:
it proves accepted sourceboot code cannot activate the unsafe policy.

## Existing evidence, correctly scoped

The original focused evidence remains historical only:

- `tools/saturn/test_source_render_suppression.py`: PASS (1 structural guard
  test plus 1 fail-closed sourceboot activation test).
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
- Obtain independent review of safety commit `77ee306c`; until then its status
  is implemented with focused host evidence, not source-complete.
- Run the focused host behavioral/differential tests listed above plus the
  source-policy/runtime/profile gates. Keep any failed or unexecuted gate open.
- Obtain independent spec review and independent quality review of the full
  replacement range, not only `4a8fe1ce`.
- Only after both reviews are clean: controller-owned serial experimental CUE
  build and Ymir manual test with the established demo role.
