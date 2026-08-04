# Overlapped-render pipeline evidence — Task 1

## Task 3 / A3 — active pre-transform compact-span substitution (2026-08-04)

The first scene-neutral A3 slice is intentionally host-only. It adds
`sm64_saturn_render_cluster_t` and a bounded admission helper that selects a
Z-Treme-style hysteretic LOD tier before any position transform, returning a
generation-tagged primitive and compact-position span. Optional clusters behind
the view or with an empty chosen-tier span fail closed; mandatory clusters stay
admitted. The type contains no BOB, game, VDP1, allocator, or pointer field.

`emit_bob_scene.py`, `emit_bob_bsp_fragments.py`, and
`extract_mario_actor.py` now emit deterministic compact near/mid/far position
streams. The accepted terrain renderer maps fragment-bank aliases to those
streams, filters far-only optional primitives before worker scheduling, and
marks exactly the selected reference span with fail-closed range validation.
The mandatory route prefix remains admitted. Per-primitive projected-area,
near, degeneracy, material, and capacity checks still run after transform.
The append-only profile exposes coarse clusters tested/admitted and positions
admitted/transformed; this is source evidence only, not a measured gain.
Mario's checked-in neutral-pose bank has 424 near/mid references and 228 far
references, verified from the regenerated intake report.

Review-remediation source evidence: the Mario transform worker now ranges over
the selected immutable reference list, stores output at each original vertex
ID, and uses a matching ownership map for cache-through reads. It therefore
does not claim a compact stream while scheduling all 424 source vertices. The
deterministic BOB and fragment generator gate now proves each emitted
one-primitive material partition has tight bounds enclosing every referenced
position, mandatory work retains a FAR span, references are sorted/unique, and
the aggregate FAR span is smaller. This closes only those host-source review
findings; generic per-cluster runtime admission and target evidence remain
open. No target build, CUE, Ymir, or performance capture was run.

Generic-runtime remediation: generated BOB and fragment headers now contain
`sm64_saturn_render_cluster_t` records with Q16 tight bounds, a material
partition, stable source ordinal, mandatory flag, and exact near/mid/far
cluster-reference spans. The accepted terrain path constructs a frame view
from the copied camera, calls `sm64_saturn_render_cluster_admit()` for each
deterministic candidate, and marks only the union of returned spans. Cluster
LOD state is reset across scene boundaries. Fine projected/near/material tests
remain after transform. Host-only validation passes; target evidence remains
unrun by authorization.

Final-review critical remediation: generic cluster depth now uses the
immutable Q16 camera-forward vector and a conservative per-axis AABB
projection. It no longer treats world Z as view depth. The focused C contract
fixture was written first and exercises non-axis-aligned yaw and pitch front
clusters with negative world Z, behind optional clusters with positive world
Z, mandatory behind work, exact selected compact spans, and yawed MID
hysteresis. `verify-render-clusters` passed after the repair through the MSYS
host shell on 2026-08-04. No target build, CUE, Ymir launch, target visual, or
  counter capture was run. The prior review's generation-wrap Minor is now
  repaired: one nonzero transform generation is derived before both admission
  and transform publication, and a result mismatch fails closed before compact
  spans are marked. The exact `UINT32_MAX -> 1` transition and reset hysteresis
  fixture pass in the focused host gate. Fresh independent A3 rereview and all
  target evidence gates remain open.

TDD evidence: the C fixture first failed because
`saturn_render_cluster.h` did not exist; the generator fixture then failed
because its compact stream fields did not exist. Green command:

```powershell
& C:\msys64\usr\bin\make.exe -f Makefile.saturn.mk \
  SATURN_REPO_ROOT=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge \
  HOST_CC=C:/Qt/Tools/mingw1310_64/bin/gcc.exe verify-render-clusters
```

Result: PASS (deterministic BOB/fragment generator and renderer-substitution
source checks, plus the C outside/inside, yaw/pitch view-space, mandatory,
hysteresis, empty-span, and invalid-argument fixture). The visible-position
host fixture additionally passes its direct compact-reference fail-closed
check. No target build, CUE, Ymir launch, or performance capture was run.
Remaining A3 gates: independent rereview and target visual/counter evidence.
Generation-repair commit: `1a4db6ed` (`fix(saturn): normalize A3 admission
generation`).

Source sub-slice commit: `feat(saturn): admit compact terrain position spans
before transform`. Independent review verdict: not yet requested; this remains
an active A3 task.

## Task 4 / A4 — bounded Mario meshlets and ordering (2026-08-04)

This source-only A4 slice adds an original generated actor-bank format: 31
Mario meshlets, each with at most 32 primitives, one material/opacity class,
a stable source ordinal, a tight actor-local AABB, and compact near/mid/far
primitive plus position-reference spans. It does not import upstream code.
The existing pinned Yaul/libmic3d depth-bucket record remains dependency/pattern
context only; the actor implementation is project-owned and preserves the
master as sole owner of Gouraud allocation, texture slots, terrain-relative
insertion, VDP1 command ownership, and presentation.

TDD red: before the actor API or implementation existed,
`verify-actor-meshlets` failed at compilation because both
`saturn_actor_meshlets.h` and `saturn_actor_meshlets.c` were absent. The
fixture names the observable failures: behind bounds must produce no admitted
positions, opaque records retain source order, textured/translucent records
remain stable far-to-near bins, capacity fails closed, invalid spans are
rejected, and source primitive identity is retained. Its explicit malformed
span mutation is caught after the normal green run.

Green host command:

```powershell
& tools/saturn/with-msys-toolchain.ps1 C:\msys64\usr\bin\make.exe \
  -f Makefile.saturn.mk \
  SATURN_REPO_ROOT=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge \
  HOST_CC=C:/Qt/Tools/mingw1310_64/bin/gcc.exe \
  verify-actor-meshlets verify-dual-actor-worker verify-terrain-depth-bins \
  verify-terrain-command-template
```

Result: PASS. The normal actor fixture and dual-actor fixture pass; the
invalid generated-span and cached-owner mutations are both caught as expected;
the retained terrain depth-bin and command-template gates pass. The dual actor
source assertion rejects reintroduction of `s_actor_order`, the former nested
insertion loop, or an unconditional Mario-vertex transform loop in the
accepted transform dispatch. No target build, CUE, Ymir launch, visual
capture, counter capture, or FPS measurement was run or claimed. Remaining
gates are the behavior commit, independent specification review, independent
quality review, and later target visual/counter evidence.

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

## Emergency A9.0 Task 1 — source-complete; serial target/Ymir pending

The full sourceboot path now consumes elapsed VBlank credit once before any
ticks, permits at most one normal and one recovery tick, and counts/drops only
the remaining whole eligible credit. A stale observed generation waits without
rebuilding/uploading/syncing VDP1; a fresh generation builds once and ends in
`sourceboot_present_generation()`, which contains the sole VDP1 sync pair and
geometry-free VDP2 frame commit. The VDP1 bank/profile generation and appended
dropped-credit counter make that ownership observable.

`vdp1_bank_displayed` intentionally lags the just-armed presentation: it is
published from the prior `vdp1_bank_submitted` before construction overwrites
the current source bank. `vdp1_bank_submitted` and
`vblank_presentation_generation` instead identify the fresh generation armed
by `sourceboot_present_generation()`. Capture analysis must treat that
one-submission lag as the completed-list lifetime signal, not an ownership
off-by-one fault.

Implementation commit: `950ab37a` (`perf(saturn): fence presentation to VBlank`).

Focused RED command:

```powershell
& .\tools\saturn\with-msys-toolchain.ps1 C:\msys64\usr\bin\make.exe -f Makefile.saturn.mk verify-sourceboot-presentation-boundary
```

RED result before scheduler implementation: exit 1; `Ran 4 tests in 0.002s`;
the real source failed the required two-tick scheduler contract because it
defined `SOURCEBOOT_MAX_SIM_CATCHUP 4U`. A later focused RED during correction
also exited 1 (`Ran 4 tests in 0.006s`) because the dropped-credit assertion
correctly rejected accounting that included the retained fractional remainder.

Focused GREEN command: the same command. Result: exit 0; `Ran 4 tests in
0.009s`; `OK`. The in-memory mutations prove rejection of four-tick catch-up,
credit refill inside the tick loop, a second VDP1 submission path, and a VDP2
commit outside the terminal boundary.

Direct runtime-contract command:

```powershell
& .\tools\saturn\with-msys-toolchain.ps1 C:\msys64\usr\bin\make.exe -f Makefile.saturn.mk verify-runtime-contracts
```

Result: exit 1 after the three quad-map summaries. The wrapper passed an MSYS
`\\d\\Code...` path to Windows Python; directory creation failed with
`PermissionError: [WinError 5]` before the contract executable compiled. This
is the known unpassed wrapper-path gate, not a runtime-contract green result.

No target build, CUE, or Ymir run occurred. Independent specification review
was GO. Initial quality review was NO-GO for one Important mutation gap: the
focused gate did not reject two `sourceboot_present_generation()` calls in one
fresh generation. `2de483d9` adds an exact-one source assertion and a failing
duplicate-helper mutant, documents the intentional displayed-bank lag, and
removes the unused profile grouping. Scoped quality rereview was GO with no
new Critical or Important findings. The serial target/manual gate remains open.

The serial target build for this source-complete range passed on 2026-08-03 in
294.1 seconds through the audited wrapper. Its output is
`e2-bob-demo-replay-camroute0-live-input-boot600-atan2v2-camv3-stage8-r6000-slave1-poly2-hot1-clip1-bsp1-frag0-pipe4`.
The CUE/ISO were staged to the short temporary `presentation-boundary` path to
work around Ymir's long-path media-resolution failure, then Ymir was launched
with `.ymir-profile` (32-Mbit DRAM). Manual observation is pending; no speed,
controls, or displayed-frame claim is made yet.

## Emergency A9.0 Task 2 — bootstrap VDP2 retirement source evidence

The first A9 CUE exited/froze after BIOS. Read-only investigation found no
runtime fault record, but found that `950ab37a` removed the predecessor
`1a48bfb4` bootstrap VDP2 begin/commit plus `vdp2_sync_wait()` sequence.
`user_init()` queues sky DMA before this point, so Task 2 tests the narrow
hypothesis that the queue must retire before the first combined VDP1/VDP2
presentation.

`815c4352` restores only that predecessor sequence after `dbgio_flush()` and
before frontend/scheduler initialization. It does not modify the one-VBlank
scheduler or add a second VDP1 submission. The focused mutation gate now
requires exactly one null-snapshot bootstrap begin/commit/wait before frontend
and scheduler initialization; it rejects absent, late, duplicate, VDP1-work,
and source-tick bootstrap mutants. It continues to require every
post-bootstrap displayed generation to use the sole
`sourceboot_present_generation()` VDP1/VDP2 boundary.

Focused RED command:

```powershell
& .\tools\saturn\with-msys-toolchain.ps1 C:\msys64\usr\bin\make.exe -f Makefile.saturn.mk verify-sourceboot-presentation-boundary
```

Before the production edit: exit 1; `Ran 6 tests in 0.020s`; five tests
passed and the real-source contract failed only because the null-snapshot
bootstrap VDP2 begin was absent.

Focused GREEN command: the same command. After `815c4352`: exit 0; `Ran 6
tests in 0.020s`; `OK`. `git diff --check` over the behavior files also exited
0.

No target build, CUE, or Ymir launch was performed for Task 2. The bootstrap
barrier is source-complete but not a proven root-cause fix until independent
reviews and one replacement serial CUE/manual `.ymir-profile` observation
record post-BIOS liveness.

## Emergency A9.0 Task 3 — persistent post-BIOS trace source evidence

Both A9 CUEs stop immediately after BIOS without a target-readable fault
record. `30123c1b` therefore exports the non-static volatile
`sourceboot_boot_trace` symbol in target RAM. Its eight 32-bit words contain
magic, version, monotonically increasing write sequence, named stage, observed
VBlank generation, scheduler credit, and independent VDP1/VDP2 presentation
generations. The writer records only scalar stores; it performs no VDP sync,
allocation, or BOB-specific branch.

The named stages bracket bootstrap retirement, `thread5_game_loop()`, stale
VBlank waiting, source ticks, VDP1 render/sync, and VDP2 commit. The matching
`capture_sourceboot_boot_trace.py` resolves the global by `sh-elf-nm` from the
matching ELF, uses the existing bounded headless Ymir BIOS macro, performs one
post-handoff `mem.peek`, and emits the decoded last stage plus raw words. It
explicitly records that it is diagnostic-only, not a GUI launch, target build,
or performance measurement.

Focused RED: `test_sourceboot_boot_trace.py` failed against the pre-change
source because no trace magic existed; the reader test failed because the
module was absent. GREEN: both focused test modules pass (two tests each), and
the retained presentation-boundary mutation gate passes all six tests. Python
compilation and reader `--help` also pass. No target CUE, Ymir process, or
runtime observation was run. Independent review and the serial trace-CUE plus
bounded-capture gates remain open.

## Task 1D — sealed geo-walk upper-bound diagnostic

### Current verdict and scope

**COMPLETE — negative upper-bound result recorded.** Implementation
commit `98f26f26` adds the default-off `diag-skip-geo` configuration. Review-fix
round 1 is `fe1074b8`; its scoped independent spec rereview marks both prior
Important findings **ADDRESSED** with no new Critical or Important findings.
The independent quality review is **NO-GO**: trailing whitespace could activate
the diagnostic while bypassing its demo/replay prerequisites, the tests admitted
two containment mutants, and the live plan contradicted the sealed D1 exception.
Quality-fix round 2 closes those findings; both independent rereviews are GO.
The serial target CUE then built and was manually tested in Ymir with the
project 32-Mbit DRAM profile. The owner observed approximately 2 FPS and no
obvious improvement, so the duplicate source geo walk is not the dominant
bottleneck. This defers A1 behind the presentation-boundary correction.

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

### Quality-fix round 2/5 TDD evidence

RED command:

```powershell
& .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_source_render_suppression.py
```

RED result: exit 1; `Ran 4 tests in 15.485s`; `FAILED (failures=3)`.
The real Make parse accepted `SATURN_EXPERIMENTAL_SKIP_GEO_WALK=1 `,
`= 1`, and `=1<TAB>` even with the diagnostic prerequisites present. The
observable trailing-space and trailing-tab cases proved the reviewed bypass.

The first production fix reduced this to one failure: exit 1;
`Ran 4 tests in 11.928s`; `FAILED (failures=1)`. GNU Make canonicalizes the
leading-space spelling before the Makefile can observe it. The test now records
that spelling as accepted only with demo+replay present and verifies that the
canonical value drives the compiler flag and diagnostic tag.

GREEN command: the same focused command.

GREEN result: exit 0; `Ran 4 tests in 11.766s`; `OK`. The Makefile preserves
the exact caller spelling, derives one stripped canonical value, rejects any
observable padding difference before validation or activation, and uses the
canonical value for prerequisite checks, the compiler definition, and output
tag. Malformed empty, `01`, `1x`, `1 0`, and non-binary values are rejected.
The tag test now compares otherwise identical demo+replay configurations with
the experimental flag at 0 and 1, so demo-keyed tagging fails. The source test
reconstructs prefix + actual normal `#else` + suffix and rejects the scene-graph
setter anywhere outside the exact direct experimental branch. The test locates
GNU Make from `SATURN_MSYS_MAKE`, `PATH`, or `MSYS2_ROOT`, and discovers Yaul
from `YAUL_INSTALL_ROOT` or a parent `.yaul.env`; it no longer pins this clone's
absolute paths.

Final focused verification used the same command: exit 0;
`Ran 4 tests in 11.980s`; `OK`.

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

Quality-fix round 2 reran this exact command and reproduced the same exit 1 /
WinError 5 failure before the host runtime-contract executable.

The existing host-contract executable was then freshly run through the same
DLL-safe wrapper:

```powershell
& tools\saturn\with-msys-toolchain.ps1 '.\build\saturn\host-tests\runtime-contract-test.exe'
```

It exited 0. This proves the produced contract executable remains runnable;
it does not erase the wrapper's failed compilation gate above.

### Commits, review, and open gates

- Implementation: `98f26f26`.
- Initial documentation transition: `f2b7ebf0`.
- Review-fix round 1: `fe1074b8`.
- Quality-fix round 2/5: `98670c48`.
- Independent spec review: initial **NO-GO**; scoped rereview marks both prior
  Important findings **ADDRESSED** with no new Critical or Important findings.
- Independent quality review: initial **NO-GO**; three Important findings
  addressed in quality-fix round 2/5; scoped quality rereview **GO**.
- Runtime-contract wrapper: blocked by the recorded MSYS/Windows path issue.
- Target build: PASS — serial `make -B -j1` through the audited wrapper, 258.5
  seconds. CUE SHA-256 `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`;
  ISO SHA-256 `fd44459260d8da08540a34560183c55da2a670c3dd743ba1ce6ae0e2a160cbef`.
- One authorized Ymir run: completed with the project 32-Mbit DRAM profile.
  Owner result: approximately 2 FPS and no obvious speed improvement. This is
  a negative diagnostic result, not a target-performance pass.
- A1: still blocked on a behavior-tested state/render separation seam.

No target build or Ymir run occurred during Task 1D implementation or either
review-fix round; the single serial build/run happened only after both scoped
rereviews were GO.

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

## Emergency A9.0 Task 10 — desktop Ymir launch observability

`tools/saturn/launch_ymir_desktop.py` is source-complete. The helper is
strictly desktop-only: it constructs `ymir-sdl3.exe -p <project
.ymir-profile> -d <explicit staged CUE>`, uses the GUI executable directory as
the working directory, and relies on the existing project profile for the
32-Mbit DRAM cart. It never rewrites the profile and has no headless fallback.
Every dry run writes a timestamped JSON plan with the exact command plus CUE
and cue-referenced ISO SHA-256/size/timestamp identity. `--launch` is explicit;
when selected, its bounded monitor records the process ID, startup time,
available stdout/stderr, and early exit code. A still-live interactive SDL
process outlives that monitor, so output and exit after return are explicitly
unobservable by this helper rather than fabricated.

Focused TDD evidence: `tools/saturn/test_launch_ymir_desktop.py` was RED for
the missing module, then GREEN: `Ran 2 tests ... OK`. Python compilation of
the helper and `git diff --check` also pass. No target build, headless run,
GUI launch, or Ymir configuration change occurred. The remaining gate is one
owner-observed desktop launch of a freshly staged CUE, followed by inspection
of its generated report before interpreting any post-BIOS failure.

## 2026-08-04 runtime observation — VDP1 is the active bottleneck

The owner manually ran the stable desktop-Ymir path with the project profile,
the staged CUE, and the profile-managed 32-Mbit DRAM cart. The target stayed
open and remained visibly slow. Ymir reported VDP1 at approximately 2 FPS and
VDP2 at approximately 60 FPS. This is qualitative target evidence, not a
benchmark, but it decisively contradicts the previous VDP1≈60/VDP2≈1–2
interpretation. The active optimization path is therefore command/plot work
sent to VDP1 and CPU work serialized ahead of it; VDP2 is not treated as the
current limiting subsystem. No headless BIOS trace result may be used to
override this observation because that harness did not load paired ELF main
bytes at their linked address.

Task transition: Emergency A9.0 is source-complete and launch-stable but has
no claimed FPS gain. Task 2/A2 begins at its failing lifecycle-test step.
Snapshot publication is a prerequisite for scene-neutral pre-transform
cluster/LOD admission in A3; it is not itself counted as a performance result.

## 2026-08-04 A3+A4 manual Ymir result

The owner manually tested the freshly linked Pipe4 Route0/live-input A3+A4
candidate in desktop Ymir with the project 32-Mbit DRAM-cart profile. The
observed rate improved from approximately 1–2 FPS to approximately 3–4 FPS.
This is qualitative emulator evidence, not a retail benchmark, but it is the
first positive manual result for the pre-transform terrain and live-pose Mario
workload reductions. Keep A3+A4 as the comparison baseline while A5 queue,
A8 transfer, and A9 overlap work proceed; target visual/counter gates remain
open.

## A2 immutable snapshot-bank source evidence (2026-08-04)

- TDD red: the new lifecycle fixture was compiled before the snapshot header
  existed and failed for the missing API/header. The normal MSYS wrapper first
  hit the known `\\d\\Code...` Windows-Python path translation failure; a direct
  host compiler invocation then reached the intended missing-header failure.
- Green: `verify-render-snapshot-bank` passed with an explicit forward-slash
  `SATURN_REPO_ROOT` override, compiling with `-std=c11 -Wall -Wextra -Werror`.
  It covers generation-zero rejection, write/publish/acquire/complete/retire,
  stale and mixed camera/actor generation rejection, double acquire, and
  quarantined-bank non-reuse. Its companion source test rejects pointer fields
  in the published view/snapshot/release, Mario scalar snapshot, and pose
  selector records.
- Green: `verify-dual-frame-bank` passed, including the uncached-release source
  gate and five-invalid-handoff mutation gate.
- Open: `verify-runtime-contracts` compiled but failed at the preserved
  terrain-command `memcmp` assertion in `tools/saturn/runtime_contract_test.c:4018`.
  It is not attributed to A2 and remains uncredited. No target build or Ymir
  run was performed. A2 is source-complete only and requires independent
  specification and code-quality review before task completion.

### A2 specification-review fix round 1 (2026-08-04)

- Red: the lifecycle fixture showed that `reset()` incorrectly made the second
  allocation available after a slot was quarantined and the other was writing.
  The source guard separately failed before the required cache-through helper
  names existed, and the C fixture then failed to compile before the exported
  generation validator existed.
- Green: reset clears only already-free slots; no lifecycle state, including
  `QUARANTINED`, is downgraded by the public API. Release state is read/written
  through the SH-2 `CPU_CACHE_THROUGH` accessor, and acquired peer payloads
  use the corresponding cache-through alias. The host identity branch permits
  the same state-machine test to execute without claiming host cache behavior.
- Green: `verify-render-snapshot-bank` passes the terminal-reset regression,
  direct generation-validator checks, and structural rejection of direct
  release-field access. No target build/Ymir was run. The unrelated
  `verify-runtime-contracts` failure remains open and uncredited.

### A2 concurrent-claim fix round 2 (2026-08-04)

- Red: a new deterministic contention fixture held the first release claim and
  asked the public acquire API for a second contender. Before the lock API
  existed, the fixture failed to compile; the companion source test also failed
  before the required SH-2 `tas.b` and acquire/release use existed.
- Green: the fixed-width uncached release record now has a claim byte. SH-2
  `tas.b` provides the bus-atomic zero-to-held transition; after claiming, the
  winner rechecks `READY`, generation, and payload validity, changes the state
  to `RENDERING`, then releases the claim. A loser returns no payload while the
  first claim is held, and later observes `RENDERING` rather than claiming it.
- Green: `verify-render-snapshot-bank` passes the deterministic contention
  test and source structural gate. No target build/Ymir was run; the unrelated
  runtime-contract failure remains open and uncredited.

### A2 payload-publication fix round 3 (2026-08-04)

- Critical review finding: the earlier code wrote the bulk snapshot through
  cached P1 and then released `READY` through P2. Compiler fences cannot
  writeback dirty P1 cache lines, so the slave could observe a fresh release
  record with stale P2 payload data. The focused lifecycle fixture was never
  credited as target cache evidence.
- Red: the new producer-visibility source gate failed before an owner-payload
  P2 accessor existed and before reset/begin/retire used it rather than direct
  `slot->snapshot` writes.
- Green: producer and peer now share the P2 cache-through payload accessor;
  `begin_write` returns it to sourceboot for authoritative post-tick filling,
  and lifecycle clears use it before the existing fenced uncached release
  writes. `verify-render-snapshot-bank` passes the structural and lifecycle
  checks. This is source correction only: target cache-coherency evidence and
  the preserved unrelated runtime-contract failure remain open.

### A3 sourceboot include-boundary repair (2026-08-04)

- Red: compiling `render_cluster_test.c` with only the `gfx` include directory,
  which matches sourceboot's declared Saturn include boundary, failed at
  `saturn_render_cluster.h` because its bare `ztreme_hot_promotion.h` include
  could not locate the isolated `gpl` header.
- Green: the render-cluster header now uses the established relative
  `../gpl/ztreme_hot_promotion.h` spelling. `verify-render-clusters` removes
  its previously masking `-I.../gpl` flag and passes, so the persistent host
  gate exercises the same boundary. No target build or Ymir run occurred.
- Commit: the scoped `fix(saturn): make A3 GPL include self-contained` repair.
  Independent specification and quality rereviews are not part of this scoped
  build repair and remain pending with A3's existing target gates.
- Remaining gates: A3 stays active; independent rereview and target visual,
  counter, and performance evidence remain open. This repair makes no target
  performance or correctness claim.

### A2 terminal-state ownership fix round 4 (2026-08-04)

- Critical review finding: `acquire_ready()` correctly held its claim while it
  validated `READY`, but quarantine wrote its terminal state without that
  lock. It could write `QUARANTINED` between validation and the claimant's
  `RENDERING` store, allowing the stale claimant to overwrite terminal
  ownership and later retire the bank.
- Red: the deterministic fixture held the release claim, called public
  quarantine, and expected it to fail closed instead of writing around the
  in-flight owner. The old implementation returned success; the companion
  source structural test also failed because quarantine, completion, and
  retirement did not all claim and release the lock.
- Green: reset/begin/publication and every terminal lifecycle transition now
  share the release claim and revalidate their predecessor state while held.
  Quarantine first prevents later acquisition from seeing `READY`; if another
  owner holds the claim, it returns false for retry rather than overwriting.
  `verify-render-snapshot-bank` passes its C race fixture and structural
  source gate. No target build/Ymir was run, target multicore/cache evidence
  remains open, and the unrelated runtime-contract failure remains uncredited.

### A3 HWRAM budget repair (2026-08-04)

- Reproduction: the exact serial route-0 A3 sourceboot configuration reached
  the final ELF link, where `ld` rejected `.bss`: `region ram overflowed by
  29680 bytes`. The map attributes 0x5ED4 bytes to
  `s_render_cluster_lod` and 0x3630 bytes to
  `s_admitted_cluster_results`—38,148 bytes introduced by A3's bulk
  CPU-only scratch.
- Red: `test_renderer_keeps_bulk_cluster_state_out_of_hwram_bss` failed for
  both declarations because neither selected the linker-owned `.lwram_bss`
  section.
- Green: both arrays now select `.lwram_bss`. The focused generator/contract
  test suite passes (7 tests), and the same single-job target build produced a
  fresh route-0 CUE/ISO. Its map reports `.bss` ending at `0x060FDEF0`, leaving
  0x2110 HWRAM bytes above the required 0x1000 libyaul TLSF floor; LWRAM ends
  at `0x002E33A0` with 0x1CC60 bytes free. No Ymir launch or performance claim
  was made by this repair.
- Remaining A3 gates: independent rereview plus target visual, counter, and
  FPS observation. The normal A3 performance candidate is now linkable.

## A4 — Mario meshlets and bounded ordering (2026-08-04)

- RED: `verify-actor-meshlets` first failed because
  `saturn_actor_meshlets.{h,c}` and the accepted-path source assertions did
  not exist.
- GREEN: the regenerated Mario bank contains 31 source-ordered meshlets, each
  capped at 32 primitives and carrying material/opacity identity, bounds, and
  near/mid/far primitive and position spans. The master-side preparation API
  rejects behind meshlets before actor transform dispatch, transforms each
  admitted position once, preserves opaque source order, and
  emits textured/translucent references through fixed stable far-to-near bins.
  `s_actor_order` and its quadratic insertion loop are absent from
  `demo_prepare_mario`; master-owned Gouraud, texture-slot, terrain-relative
  insertion, and VDP1 responsibilities remain unchanged.
- Focused source-only host command (Qt MinGW host compiler):
  `verify-actor-meshlets verify-dual-actor-worker verify-terrain-depth-bins
  verify-terrain-command-template` passed. The actor invalid-span mutation and
  dual-worker cached-owner mutation both failed as required. The focused
  `MarioActorPoseTests` and `Fast3dProfileDecodeTests` run passed 30 tests with
  one expected skip.
- Open/uncredited: `verify-runtime-contracts` regenerated the three quad-map
  summaries then its host compile exited 1 without a compiler diagnostic, so
  it is an infrastructure gate rather than an A4 green result. A prior broad
  `test_tools.py` aggregate also exposed two A4 fixture expectations, repaired
  before the focused rerun above; its remaining Bob route-schema/emitter drift
  failures are preserved unrelated work. No target build, CUE, Ymir, visual,
  counter, or FPS gate ran. Independent specification and quality review remain
  required before A4 can be source-complete.
- Commit: `7e419484` (`perf(saturn): cull and bin Mario by meshlet`).

### A4 independent-review critical remediation (2026-08-04)

- NO-GO review finding: A4 used a neutral generated AABB centre for admission
  and binning, ignoring live animation vertices and Mario yaw; its renderer
  also rebuilt transform references from primitive corners instead of consuming
  generated compact position spans. This could falsely cull/LOD/bin ordinary
  turning or walking Mario geometry.
- RED: expanded actor fixture failed because the position telemetry did not
  equal the advertised transform stream. It additionally adds a yawed
  view-plane crossing and a walking-bank animated-pose crossing case, plus
  global deduplication/element validation of the generated position union.
- GREEN: admission now projects each near-tier meshlet position from the
  supplied live pose through the same Q16 Mario yaw used by transform. It culls
  only when the furthest live depth is non-positive, chooses LOD from nearest
  live depth, and bins translucent work by furthest live depth. The prepare API
  returns the exact globally deduplicated selected-tier position union, which
  the renderer passes directly to `transform_ref_count`; it no longer rebuilds
  that union from primitive corners. The focused actor/dual-worker/depth-bin/
  command-template host gates and 30 focused Mario/profile tests pass (one
  expected skip); both existing mutation fixtures still fail as required.
- Open: this is source-only remediation. A fresh independent specification and
  quality rereview, runtime-contract infrastructure closure, and target
  visual/counter/FPS evidence remain required; no target build, CUE, or Ymir
  run occurred.
- Commit: `c5944bac` (`fix(saturn): admit Mario meshlets from live poses`).

## A5 descriptor-owned output lanes (2026-08-04)

- RED: the direct Qt MinGW host command for
  `tools/saturn/render_output_bank_test.c` failed because
  `saturn_render_output_bank.{h,c}` did not exist. This intentionally preceded
  production code.
- GREEN: each immutable queue descriptor selects its output bank solely by
  type (`WORLD_*` terrain, `ACTOR_*` actor); its actual
  `CLAIMED_MASTER`/`CLAIMED_SLAVE` state atomically publishes the output lane.
  A reader obtains cached versus cache-through P2 access only through that
  record. Logical input offsets and `begin == 0` never select a lane. The
  release record is fixed-width, pointer-free, and its `ready` word is written
  only after generation/job/claimed-state metadata.
- Focused source-only host command:
  `C:\\Qt\\Tools\\mingw1310_64\\bin\\gcc.exe -std=c11 -Wall -Wextra -Werror
  -I src/port/saturn/gfx tools/saturn/render_output_bank_test.c
  src/port/saturn/gfx/saturn_render_output_bank.c
  src/port/saturn/gfx/saturn_render_job_queue.c` then
  `.tmp-task5/render-output-bank-test.exe` passed. It covers descriptor bank
  selection, a master-steal whose input offset lies in the former slave range,
  cache-through selection for the peer, mismatched-bank rejection, overwrite
  rejection, and a concurrent master/slave publication race with one winner.
- Focused coherency gate:
  `verify_dual_cpu_coherency.py --output-bank-source ... --output-bank-header
  ... --self-test` passed and rejected five mutations: cached metadata, missing
  `tas.b`, ready-before-owner publication, direct cached read, and logical-range
  lane inference.
- Design boundary: this source-only prerequisite does not modify
  `saturn_demo_render.c`, its current fixed split, or master-only VDP1 command
  lowering/painter ordering. No target build, CUE, desktop Ymir launch, visual,
  counter, or FPS claim occurred. Independent specification and quality review,
  live queue wiring, and target cache/ordering evidence remain open.
- Commit: `0026a3a1` (`feat(saturn): publish descriptor-owned output lanes`).

### A5 critical-review claimant binding (2026-08-04)

- Review finding: the initial output-bank API accepted a caller-provided
  `claimed_state`, so a callback could forge `MASTER` or `SLAVE` without an
  actual queue claim. That breaks the output-owner invariant even though the
  queue itself has exact-once claims.
- RED: the expanded host fixture invoked output publication before any queue
  claim. Its desired result is failure; the prior unbound API could not express
  that authority boundary.
- GREEN: publication now accepts the queue plus job index only. It takes the
  queue release claim lock, revalidates matching generation/descriptor and an
  actual `CLAIMED_MASTER` or `CLAIMED_SLAVE` state, then publishes the derived
  output lane while the queue release remains locked. The caller can no longer
  choose a lane. The fixture proves pre-claim publication fails and an actual
  master claim succeeds; the concurrent race still yields one recorded owner.
- The coherency mutation gate now rejects six variants, adding a removed
  claimed-state validation to the original five. No target build, CUE, Ymir,
  visual, counter, or FPS claim occurred. Independent review and live renderer
  queue wiring remain open.
- Commit: `f0a3b99c` (`fix(saturn): bind output lanes to queue claims`).

### A5.5 descriptor-to-result bridge (2026-08-04)

- RED: the direct Qt MinGW host compile of `render_job_bridge_test.c` failed
  because `saturn_render_job_bridge.h` did not exist.
- GREEN: the bridge first selects terrain versus actor output by immutable job
  kind, then binds writer/reader lane selection to the queue's actual claim
  and exact descriptor index. The host fixture proves a master stealing a
  former-slave input offset keeps its cached result range, its slave peer is
  rejected before that exact descriptor becomes `DONE`, a slave actor job
  routes to the master peer through its own bank, and a wrong descriptor index
  fails closed. It also
  proves the queue accepts one source-side arm and rejects a second.
- Focused direct host commands compiled with `-std=c11 -Wall -Wextra -Werror`:
  `render-job-bridge-test.exe` and `render-job-queue-test.exe` both report
  PASS. No MSYS target build, CUE, Ymir launch, cache-coherency target proof,
  visual test, counter result, or FPS claim ran.
- Reuse record: project-owned code, **pattern-only** from pinned SlaveDriver
  `a8986591557b6e680550d3c23970284d3b38ff8f` (GPL-3.0-or-later, disjoint
  result ownership) and pinned Z-Treme
  `cff75451c1616aac1236fc2b44223902b55c706b` (GPLv3, fixed work areas); no
  upstream lines were copied. Live renderer conversion and independent review
  remain required before the queue is allowed to activate an SH-2 callback.

### A5.5 CPU-DUAL coexistence correction (2026-08-04)

- NO-GO review finding: A5.5 compiled a second `cpu_dual_slave_set`/notify
  path while the linked legacy SlaveDriver worker still owns that callback.
- RED: `test_render_job_bridge_source.py` rejected those source tokens.
- GREEN: the source-only bridge retains its one-owner attach/notify state
  contract but compiles neither target registration nor notify. The focused
  source gate passes, as do the bridge and queue host fixtures. The atomic
  live cutover must remove all legacy dispatches before it owns Yaul callback
  registration; no target build/Ymir/FPS evidence ran.

### A5.5 source-arm terminology correction (2026-08-04)

- NO-GO review finding: `slave_attach`/`slave_notify` falsely implied target
  polling activation although A5.5 contains no Yaul callback.
- GREEN: the API is now `source_arm`/`source_armed`. It records only a unique
  source-side callback-table/context owner, cannot report a notification, and
  explicitly cannot activate the slave. The static source test scans both the
  queue and bridge sources for CPU-DUAL registration/notify tokens; it and the
  bridge/queue host fixtures pass. No target build/Ymir/FPS evidence ran.

### A5.6 live-cutover prerequisite (2026-08-04)

- **ACTIVE — watched RED, no source activation.** The A5.5 final review is GO
  for source-only bridge scope at 87824a5a, but direct inspection of the
  accepted A3/A4 renderer found the missing architectural seam: terrain writes
  live in fixed physical master/slave arrays, actor readers use fixed owner
  metadata, and the linked generic worker owns CPU-DUAL.
- RED: test_render_job_live_cutover_source.py correctly fails because the
  frame does not publish/drain/terminally join descriptor work and still calls
  the legacy terrain/Mario dispatch.
- verify-render-job-bridge now also invokes its source coexistence guard so
  bridge activation constraints cannot be skipped. No target build, CUE, Ymir
  run, or FPS claim occurred; the previously accepted 3–4 FPS A3+A4 CUE is
  retained as rollback baseline.
- Required next source scope: descriptor-indexed payload storage/readers,
  followed by one atomic queue CPU-DUAL lifecycle cutover. A callback-name
  substitution is explicitly unsafe and prohibited.

### A5.6 queue runtime foundation (2026-08-04)

- RED: direct Qt-host compile of render_job_runtime_test.c failed because the
  runtime header/source did not exist.
- GREEN: saturn_render_job_runtime now retains one queue/callback/context
  owner and installs the one Yaul polling entry only on SH-2. The host fixture
  activates once, rejects a second activation, drains one slave-claimed
  descriptor, and observes its terminal state: PASS with -std=c11 -Wall
  -Wextra -Werror. This does not bind the renderer or replace the linked legacy
  worker yet, so no target build/Ymir/FPS evidence is claimed.

### A5.6 runtime cache-through repair (2026-08-04)

- NO-GO review found the runtime slave poll read queue->generation through its
  stored cached pointer, which could stale-spin on SH-2.
- RED: the new runtime source gate failed because no public queue generation
  accessor existed and the runtime directly dereferenced its queue pointer.
- GREEN: a public generation accessor first selects the queue's P2
  cache-through alias; the runtime uses it exclusively. The gate mutation
  rejects a direct runtime generation read. Direct C11/Werror runtime and
  bridge fixtures plus bridge/runtime source guards PASS. No target build,
  Ymir, or FPS result is claimed.

### A5.6 payload-bank foundation (2026-08-04)

- RED: direct Qt-host compile of render_job_payload_bank_test.c failed for the
  absent payload-bank interface and source.
- GREEN: the payload-bank helper selects its physical master/slave write base
  only from the bridge execution's actual claimant and exact output offset; a
  DONE reader obtains the same descriptor-owned base through the bridge's
  metadata/P2 policy. The focused host fixture proves a master steal of an old
  slave-offset input writes only master payload and reads the exact terminal
  span: PASS with -std=c11 -Wall -Wextra -Werror.
- Terrain and actor production arrays/readers remain unmigrated, so this is a
  source prerequisite only. The live-cutover gate remains RED; no target
  build, CUE, Ymir, or FPS claim occurred.

### A5.7 queue job-graph foundation (2026-08-04)

- A5.6 caller-migration preflight found that independent descriptors cannot
  express terrain's transform→classify→ordered multi-result merge or Mario's
  transform→classify chain. A physical payload lane alone cannot stop an early
  consumer.
- RED: the graph fixture was written before the graph interface and failed
  because `saturn_render_job_graph.h` was absent. GREEN introduces P2-visible
  renderer-local dependency masks, exact indexed claims, failed-predecessor
  quarantine, and terminal terrain merge identities. It covers a slave
  producer followed by a master consumer, early-consumer rejection, ordered
  four-result terrain identity validation, independent work, and failure
  propagation without reclaiming claimed work.
- The renderer remains unbound and the `test_render_job_live_cutover_source.py`
  gate intentionally remains RED. No target build, CUE, Ymir run, counter, or
  FPS claim occurred.

### A5.7 graph review repair (2026-08-04)

- NO-GO found the original fixture incorrectly expected independent READY work
  to block, accepted cycles, and used one-pass failure propagation that could
  leave a reverse-chain dependent READY.
- RED: fixture additions required cycle rejection and complete reverse-chain
  quarantine; the foundation did not provide either. GREEN rejects self/cyclic
  masks before queue publication, leaves independent work eligible, and repeats
  quarantine to a fixed point. Direct MinGW C11 `-Wall -Wextra -Werror` fixture
  now reports `render job graph fixture: PASS`; the two-test static graph guard
  also passes. The separate live-cutover source gate remains intentionally RED.

### A5.8.1 graph-aware runtime prerequisite (2026-08-04)

- RED: the revised runtime fixture called the missing
  `sm64_saturn_render_job_runtime_activate_graph()` API and direct Qt MinGW
  C11 compilation failed with the expected implicit-declaration error.
- GREEN: the runtime now stores a graph alongside its queue and both slave
  polling and master drains claim only through graph eligibility, completing
  or failing the exact claimed descriptor and propagating dependency failure
  quarantine. The fixture intentionally publishes `WORLD_LOWER` first with a
  dependency on `WORLD_ADMIT`; `render-job-runtime-a58-test.exe` prints PASS
  under `-std=c11 -Wall -Wextra -Werror`, proving a raw storage-order drain is
  not accepted.
- This does not activate CPU-DUAL in the renderer, migrate production payload
  arrays, build a CUE, run Ymir, or make an FPS claim. The prior A3+A4 desktop
  observation of roughly 3–4 FPS remains the rollback baseline.

### A5.8.1 descriptor cache-through review repair (2026-08-04)

- NO-GO review found that a graph claim was followed by a raw
  `s_runtime.queue->jobs[index]` read, which could observe P1-stale descriptor
  data on SH-2.
- RED: the new source mutation test rejected that raw descriptor read.
  GREEN adds `sm64_saturn_render_job_queue_claimed_job()`, which selects the
  queue cache-through alias and verifies generation plus the exact claimed
  state before returning a descriptor. Both master and slave drain paths use
  it and fail/quarantine the exact claim if it cannot be reread.
- `test_render_job_runtime_source.py`, direct Qt MinGW runtime and graph C11
  `-Wall -Wextra -Werror` fixtures, and `git diff --check` pass. No target
  build, Ymir run, renderer binding, or FPS claim occurred.

### A5.8 terrain descriptor-binding milestone (2026-08-04)

- RED: the new host-compiled `render_job_live_cutover_source_test.c` reports
  `A5 renderer has not atomically cut over to graph runtime` against the
  current frame path. This replaces the unusable configured `py -3` launcher;
  the red result is expected and confirms no queue/legacy hybrid has been
  enabled.
- GREEN component: `demo_terrain_queue_bind_output()` requires an exact
  `CLAIMED_MASTER` or `CLAIMED_SLAVE` descriptor reread, publishes the matching
  terrain output release through the bridge, and derives terrain record and
  command pointers through `saturn_render_payload_bank`. Its storage selection
  does not use a range begin, work split, or actor owner array. Renderer init
  owns the queue, graph, output banks, and terrain payload-bank setup, but does
  not register a runtime callback or publish a generation.
- Direct Qt MinGW C11 `-std=c11 -Wall -Wextra -Werror` payload-bank fixture
  passes after the existing queue/bridge/output sources are linked. No target
  build, CUE, Ymir launch, target visual/counter capture, or FPS claim ran.
  Remaining gate: turn this binding into the complete terrain producer and
  reader callback, then migrate Mario before any atomic CPU-DUAL activation.

### A5.8 dormant terrain producer/terminal-reader increment (2026-08-04)

- RED: `render_job_terrain_route_source_test.c` failed with `terrain queue
  route does not bind exact descriptor ownership` before the exact compact
  producer and terminal reader existed.
- GREEN: `demo_terrain_compact_exact()` takes explicit work bounds, claimant
  lane, and caller-owned arena; the fixed range callback is now only a legacy
  adapter. Dormant `WORLD_LOWER` validates its exact input span, obtains its
  descriptor-owned result arena through the bridge, and seals it before it
  returns success to graph runtime (which alone publishes the claim's DONE).
  The matching reader first requires the exact `DONE` descriptor, then obtains
  record and command payload aliases through the payload bank; neither queue
  route reads `s_slave_begin`.
- GREEN host evidence: direct Qt MinGW C11 `-std=c11 -Wall -Wextra -Werror`
  route source contract, payload-bank fixture, and graph fixture all PASS;
  `git diff --check` passes. The configured Make/MSYS path was not credited:
  `make` is unavailable in this host shell and no target build was attempted.
  No graph generation is published, no queue callback is registered, and no
  CUE/Ymir/FPS claim occurred. Remaining: WORLD_ADMIT descriptor-indexed
  transformed-position publication, persistent per-job terrain counts and
  merge-span assembly, Mario's matching producer/reader route, independent
  specification and quality reviews, then one atomic CPU-DUAL activation.

### A5.8 terrain claimant-lane review repair (2026-08-04)

- NO-GO review found that `demo_terrain_compact_exact()` accepted a claimed
  lane but called `demo_classify_range()`, which re-derived it as
  `begin == 0 ? MASTER : SLAVE`. A valid slave descriptor with input offset
  zero would read/write with the master cache policy.
- RED: the expanded route source fixture required an explicit classify lane
  and prohibited a range-derived lane in every queue-reachable compact/
  classify callback; it failed against the reviewed code.
- GREEN: `demo_classify_exact(context, begin, end, lane)` receives the exact
  producer's claimant lane and passes it to all projected reads. The former
  range choice exists only in the legacy fixed-worker adapter. Direct Qt
  MinGW C11 `-std=c11 -Wall -Wextra -Werror` route fixture passes; no runtime
  activation, target build, CUE/Ymir run, or FPS claim occurred. Fresh scoped
  re-review remains mandatory.

### A5.8 terrain admit/merge metadata increment (2026-08-04)

- RED: the expanded host route contract required a `WORLD_ADMIT` callback,
  descriptor-keyed admit publication, terminal result publication, and a
  DONE reader that derives count/sequence from stored metadata; it failed
  before those symbols existed.
- GREEN: WORLD_ADMIT transforms its claimant-owned position payload, then
  publishes a P2-visible record keyed by generation, job index, claimant
  state, lane, and sequence. WORLD_LOWER uses the already-admitted transform
  payload, seals its result arena, and publishes its own exact count/sequence
  record before returning to graph runtime. The DONE reader revalidates the
  descriptor/output-bank lane and returns the stored count/sequence rather
  than trusting caller-supplied merge metadata.
- GREEN host evidence: direct Qt MinGW C11 `-std=c11 -Wall -Wextra -Werror`
  route source contract prints PASS. No target build, queue activation, CUE,
  Ymir run, or FPS claim occurred. Open gates: actual terrain merge-span
  assembly, Mario parity, independent review, atomic CPU-DUAL cutover, then
  target/cache/visual evidence.

### A5.8 terrain predecessor-proof review repair (2026-08-04)

- NO-GO review found that metadata publication alone did not prove the lower
  callback consumed the intended admit producer. A malformed graph edge or a
  premature consumer could classify against unrelated transform state.
- RED: the route source contract required the lower callback to call a
  checked graph API and validate admit metadata; it failed before those calls
  existed. The graph fixture added mutations for an unready predecessor and a
  completed wrong-type predecessor.
- GREEN: `sm64_saturn_render_job_graph_world_lower_admit_done()` P2-rereads
  the exact current lower claim, requires a one-bit immutable dependency mask,
  and accepts only a terminal WORLD_ADMIT predecessor. WORLD_LOWER then checks
  the corresponding P2 publication/output-bank lane before classification.
  Both mutations fail closed; normal graph route and source contracts pass.
- No queue activation, target build, CUE/Ymir run, or FPS claim occurred.

### A5.8 dormant terrain merge-span assembly (2026-08-04)

- RED: the expanded direct C11 route-source contract required a distinct
  descriptor-stream merge assembler, terminal DONE reader, graph identity
  validation, and the existing master stable depth-bin builder; it failed
  before that route existed.
- GREEN: the dormant assembler consumes only `done_job` WORLD_LOWER entries.
  It rereads validated P2 release metadata through the terminal reader,
  requires matching generation/sequence/count/claimant lane, constructs
  descriptor/local-output identities, validates them against the graph, then
  runs the master-owned stable stream ordering. It preserves per-descriptor
  record/command aliases and does not recreate or consult the legacy
  master/slave result spans.
- GREEN host evidence: direct Qt MinGW C11 `-std=c11 -Wall -Wextra -Werror`
  terrain route-source and graph fixtures PASS. No queue activation, target
  build, CUE/Ymir run, or FPS claim occurred. Remaining: command lookup from
  the ordered descriptor stream, Mario parity, independent review, one atomic
  CPU-DUAL cutover, then target/cache/visual evidence.

### A5.8 terrain merge completeness repair (2026-08-04)

- NO-GO review found that iterating only `done_job` results could silently
  omit a current READY/CLAIMED WORLD_LOWER descriptor rather than fail closed.
- RED: the graph fixture required a generation-current immutable lower
  descriptor to be inspectable before DONE; it failed to compile because the
  queue exposed only claimed/DONE reads.
- GREEN: `published_job()` P2-rereads a generation-current immutable
  descriptor without granting payload access. The dormant assembler now
  enumerates every graph descriptor through that accessor and rejects a
  WORLD_LOWER unless its exact same descriptor is terminal DONE before result
  metadata or payload reads. Direct C11/Werror graph and terrain-route
  fixtures PASS. No queue activation, target build, CUE/Ymir run, or FPS
  claim occurred; fresh scoped re-review remains required.

### A5.8 executable merge-completeness and empty-frame repair (2026-08-04)

- NO-GO rereview found two gaps: the incomplete-lower guarantee was asserted
  only by source substring, and `validate_terrain_merge()` rejected a valid
  all-culled frame because its identity count was zero.
- RED: the graph fixture required an executable collection API, READY and
  CLAIMED lower rejection, exact DONE enumeration, and successful zero-result
  validation after that lower retired. It failed before the collection API.
- GREEN: graph collection P2-enumerates every current immutable WORLD_LOWER
  and requires exact DONE identity. The renderer consumes that list before
  metadata/payload reads. Merge identity validation accepts `count == 0` only
  after collection proves at least one expected lower exists and every lower
  is DONE; the stable stream builder then returns an empty merge. Direct strict
  queue, graph, and terrain-route fixtures PASS. No activation, target build,
  CUE/Ymir run, or FPS claim occurred; fresh scoped re-review remains required.

### A5.8 dormant Mario queue parity (2026-08-04)

- RED: a new direct C11 route-source executable required an ACTOR_ADMIT
  descriptor-owned transform payload, an ACTOR_LOWER exact predecessor proof,
  terminal metadata/readback, fail-before-mutation validation, and a
  descriptor/local-order master merge. It failed before those functions
  existed. The graph executable separately failed to compile before actor
  claimed/DONE predecessor and terminal collection APIs existed.
- GREEN: the actor snapshot copies the live posed vertex and lighting banks,
  animation frame/bank metadata, actor transform, and compact meshlet
  references. ACTOR_ADMIT publishes dense vertex-id/projected records to the
  actual claimant's payload lane. ACTOR_LOWER requires its exact one-bit DONE
  ACTOR_ADMIT edge, reads that lane through the terminal bridge, classifies its
  immutable primitive span, and publishes count/sequence/claimant metadata.
  Master terminal assembly requires all lower descriptors DONE and complete
  contiguous primitive coverage, validates every vertex/ref payload before
  mutating renderer banks, and retains descriptor/local order.
- Regression correction during self-review: the shared transform helper keeps
  sine/cosine calculation once per range rather than once per vertex, and the
  legacy non-slave classification path explicitly retains master ownership.
  This avoids slowing or corrupting the accepted legacy candidate while the
  queue route remains dormant.
- GREEN host evidence using Qt MinGW C11 `-std=c11 -Wall -Wextra -Werror`:
  actor route-source, graph, terrain route-source, queue, bridge, graph-aware
  runtime, and payload-bank executables PASS. The configured Windows Python
  launcher remains unavailable, so Python source wrappers were not credited.
- Expected RED: the live-cutover executable still reports that A5 has not
  atomically replaced the legacy worker. No target build, CUE/Ymir run, or FPS
  claim occurred.
- Open activation gates: resolve global queue output offsets versus type-local
  payload-bank offsets; connect terrain command
  lookup to ordered descriptor streams; publish/drain the combined graph under
  the sole CPU-DUAL callback; prove target link/cache behavior; then manually
  validate a new Ymir CUE.

### A5.8 Mario queue-parity independent review (2026-08-04)

- Commit `1d1137f1`; audit commit `3ad0d23e`.
- Verdict: GO for dormant source scope, with no critical or important finding.
  The reviewer independently reran all seven strict Qt MinGW C fixtures and
  verified full live-pose preservation, claimant-derived payload lanes, exact
  ACTOR_ADMIT dependency proof, terminal metadata, validate-before-mutate
  ordered merge, legacy-path ownership, and absence of CPU-DUAL activation.
- Minor activation debt: the actor route fixture is structural rather than a
  direct callback behavior harness. Before activation, add corrupt vertex/ref
  identity, incomplete coverage, stale sequence, and cross-lane read cases and
  explicitly publish the callback context through P2/cache-through ownership.
- The target/CUE/Ymir/FPS gates remain open. The output-offset namespace was
  the next source gate and is resolved below pending independent review.

### A5.8 payload-kind output namespace (2026-08-04)

- Design resolution: the renderer has four separate bounded physical outputs,
  so a synthetic global offset layout would waste address range without
  strengthening ownership. Queue publication instead derives the physical
  kind from the immutable type/callback pair and validates overlap within
  that kind only. The 16-byte pointer-free descriptor is unchanged.
- Watched RED: four descriptors using bank-local offset zero were rejected by
  the old global overlap rule. GREEN: all four distinct kinds publish, while
  two overlapping WORLD_LOWER spans, a WORLD/ACTOR callback mismatch, and an
  unknown type all fail closed.
- Strict Qt MinGW C11/Werror queue, graph, bridge, graph-runtime, and payload
  fixtures PASS. `verify_dual_cpu_coherency.py` was not credited because no
  installed Python launcher was available. `git diff --check` PASS.
- Independent source review is GO: implementation `db28fd87`, audit
  `c5da1516`, with no critical, important, or minor finding. The reviewer
  independently reran the five strict C fixtures and `diff --check`.
- Scope remains source-only and dormant. No CPU-DUAL activation, target build,
  CUE/Ymir run, cache proof, or FPS claim occurred. Ordered terrain command
  lookup, callback-context P2 publication, direct
  callback corruption/cross-lane tests, and the atomic cutover remain open.
