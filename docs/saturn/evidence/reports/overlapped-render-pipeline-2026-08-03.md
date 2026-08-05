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

### A5.8 ordered terrain commands and callback-context publication (2026-08-04)

- Watched RED: the terrain command-stream fixture failed to compile because
  sorted refs had no command identity; terrain and actor route fixtures then
  failed because dormant callbacks did not open a published context.
- GREEN: command-stream sorting moves each result and descriptor-local command
  image as one pair. The union retains the existing eight-byte SH-2 ref size;
  legacy refs use the old arena resolver and queue refs use the paired command.
- GREEN: a pointer-free 16-byte P2 release record binds generation, sequence,
  descriptor index, payload byte bound, callback phase, and producer lane.
  Open requires the exact current claimant and returns a cache-through alias
  only to the peer lane. Both terrain and Mario cases reject corrupt phases,
  stale generations/sequences, incomplete publication, wrong claims, and
  out-of-range identities in an executable C11 fixture.
- Strict Qt MinGW C11/Werror command-stream, callback-context, terrain-route,
  actor-route, and existing terrain-depth fixtures PASS. The callback source
  is present in the sourceboot target list; target compilation remains an open
  post-review gate.
- Scope is dormant source only. The sole CPU-DUAL callback remains the legacy
  worker; there was no target build, CUE/Ymir run, cache proof, or FPS claim.

### A5.8 callback-context review repair (2026-08-04)

- NO-GO audit `891f64b2` found the renderer publisher unreachable and the
  outer P2 alias insufficient: terrain followed stack-local classify/spans
  pointers and Mario followed the master-cached dynamic vertex-ref list.
- Repair: Mario copies dynamic compact vertex references inline and resolves
  immutable generated primitive/material banks locally. Terrain copies its
  transform job and complete work-order stream into a self-contained bounded
  snapshot; each callback reconstructs only a caller-local classify view and
  never follows the legacy stack compact/spans object.
- The preclaim preparation now snapshots exact renderer-owned terrain state,
  then publishes every descriptor context before a future drain may claim it.
  Four phase-specific open APIs are used by the dormant callbacks and directly
  executed for master and slave claims. The mutation matrix now covers all
  four phases and generation, sequence, stored index, phase, payload bytes,
  producer lane, ready state, claimant, and out-of-range identity.
- Repair is source-complete pending fresh re-review. No callback activation,
  target build, CUE/Ymir run, cache proof, or FPS claim occurred.
- First re-review remained NO-GO because the phase/ready/sequence/claim/range
  cases still exercised only the two lower phases. The repair now runs one
  common complete mutation matrix, including the wrong phase-specific opener,
  for WORLD_ADMIT, WORLD_LOWER, ACTOR_ADMIT, and ACTOR_LOWER.
- Final independent source re-review is GO. Strict callback-context,
  terrain-route, actor-route, ordered-command-stream, and existing terrain-depth
  fixtures independently PASS. This does not close target compile/link,
  section-placement, live CPU-DUAL ownership, target cache, CUE/Ymir, or FPS
  gates.

### A5.8 first target-compile gate and include repair (2026-08-04)

- The exact guarded Route-0/live-input/Pipe4 `-B -j1` build was run once at
  `b1fb718a`. The complete transcript is retained in
  `.tmp-a58-target-build-20260804.log`.
- The build compiled through the new graph source and then failed before link
  in `saturn_render_job_queue.c:26`: `CPU_CACHE_THROUGH` was undeclared. Thus
  no link, section-placement, memory-margin, or fresh CUE/ISO/ELF evidence is
  credited from this attempt.
- Watched RED/GREEN: the queue source contract failed before the fix and then
  passed all four cases after a narrow SH-only `<cpu/cache.h>` import. The
  configured MSYS2 Python was launched through the dependency wrapper; result:
  `Ran 4 tests ... OK`. `git diff --check` also passed.
- This is a target include-boundary repair only. It does not activate the
  dormant queue, replace the legacy CPU-DUAL callback, launch Ymir, or make an
  FPS claim. One serialized post-review target rebuild remained required.

### A5.8 post-review target link and section proof (2026-08-04)

- Repair `eb2ec2a2` received independent GO with no findings in audit commit
  `8eef1c22`. The reviewer independently passed the wrapped four-case source
  contract and range `git diff --check`.
- The one authorized post-review exact Route-0/live-input/Pipe4 `-B -j1`
  rebuild completed with exit 0 in 490.4 seconds. Full transcript:
  `.tmp-a58-target-rebuild-20260804.log`. It compiled every new queue, graph,
  runtime, bridge, payload, and callback-context source, linked the ELF, made
  `SOURCE.DAT`, and packaged a fresh ISO/CUE. The log contains zero error,
  undefined-reference, overflow, or region-fit matches. Existing source
  warnings remain, plus the established RWX LOAD-segment linker warning; no
  new render-queue warning appears.
- Linked sections: `.text` `0x06004000+0x7d058`, `.data`
  `0x06082ef0+0x9cd4`, `.bss` `0x0608cbe0+0x717f0`, `.uncached` P2
  `0x260fe3d0+0x420` with physical load end `0x060fe7f0`, `.lwram_cmdts`
  `0x00200000+0x20000`, and `.lwram_bss` `0x00220000+0xd1430`.
  Physical HWRAM margin is `0x1810` (6,160 bytes), above the linker-enforced
  `0x1000` floor; LWRAM margin is `0xebd0` (60,368 bytes).
- The live initializers resolve in HWRAM at context `0x06073590`, graph
  `0x060735a8`, queue `0x060735d8`, and payload `0x0607363c`. Shared actor/
  terrain output metadata, callback contexts, graph, and queue resolve through
  P2 from `0x260fe3d0` through `0x260fe5ac`. Dormant unreferenced callback and
  drain routines are correctly garbage-collected until atomic cutover.
  `sh-elf-nm -u` reports no unresolved symbols.
- Artifact evidence (UTC 2026-08-04): CUE 88 bytes at `22:05:43.8512092`,
  SHA-256 `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`;
  ISO 4,657,152 bytes at `22:05:42.4357086`, SHA-256
  `3dfb1627bd38aeb7dcd296f913e778f045685861f66fb3aaa153500125901f01`;
  ELF 8,614,404 bytes at `22:05:35.4477100`, SHA-256
  `7c62b65e2fc83a70e81c0655a8607b74716f694070d9ebce6bdfe1331b010a48`;
  map SHA-256 `b9d3da9e5ea97a5d7016bb4fa03190279a3537e284d163cf55c546e8ae1a9426`;
  `SOURCE.DAT` SHA-256
  `1e4b622b7757367c8eba2e724db74e49a7fc4b4c5044182c6d09b7f8e62d266b`.
- This closes only dormant target compile/link/section placement. No Ymir was
  launched, the legacy CPU-DUAL path remains live, and cache/runtime/FPS gates
  remain open.

### A5.8 atomic live renderer cutover — source implementation (2026-08-04)

- Watched RED: `render_job_live_cutover_source_test.c` reported that the
  accepted frame still used the fixed terrain and Mario dispatchers. A second
  runtime RED failed to compile before positive notified-slave retirement was
  observable.
- GREEN: `sm64_saturn_demo_render_init()` installs one graph-aware CPU-DUAL
  callback table. Each frame publishes four coarse descriptors (terrain
  admit/lower and Mario admit/lower), resets and publishes pointer-free
  callback contexts before the first notify, drains eligible master work, and
  waits for the notified slave polling entry to positively return. Only a
  fully terminal graph may feed descriptor-owned terrain command assembly and
  Mario result assembly; final VDP1 order/lowering remains master-only.
- Correctness repair during the same TDD cycle: WORLD_ADMIT reserves a bounded
  full terrain capacity because it publishes transformed-position count, and
  it rebuilds all position ownership from its actual claimant lane. A stolen
  coarse admit therefore cannot inherit half of the removed logical split.
  Pre-notify publication failure quarantines and retires only unclaimed
  descriptors. Post-notify failure performs no serial replay and returns
  before backend begin, preserving the prior complete command list.
- Strict Qt MinGW C11/Werror live-cutover, terrain-route, actor-route, runtime,
  queue, graph, bridge, payload-bank, and callback-context fixtures PASS.
  `git diff --check` is clean apart from line-ending notices on pre-existing
  dirty files. This is source implementation only, pending fresh independent
  review. No post-cutover target build, CUE/Ymir run, cache proof, or FPS claim
  occurred.

### A5.8 atomic-cutover first-review repair (2026-08-04)

- Independent audit `8e64b482` was NO-GO despite all nine prior host gates
  passing. It found that the sole WORLD_ADMIT retained `dual_phase=true` and
  would wait for a peer transform descriptor that does not exist. It also
  found that slave-admit to master-lower could read a stale cached
  `s_position_owner` map from the previous generation.
- Watched RED/GREEN added `terrain_queue_handoff_test.c` and the
  `saturn_terrain_queue_handoff.h` policy. The executable fixture runs actual
  admit/lower callback-shaped transitions through two queue generations,
  poisons the prior lower-local owner in both directions, and proves that the
  exact admit claimant overwrites it with no peer-transform requirement.
- The live queue snapshot now has `dual_phase=false`. WORLD_ADMIT treats its
  actual claimant as sole producer and transforms every visible position
  without consulting the removed split's cached owner map. WORLD_LOWER
  rebuilds its own cached owner bytes from exact DONE admit metadata before
  projected/view/valid payload reads. The legacy diagnostic helper retains
  its old rendezvous and per-position ownership.
- The new handoff fixture and all nine original strict C11/Werror gates PASS.
  Fresh scoped re-review is GO at audit commit `1819f2b4`; the reviewer
  inspected actual renderer wiring and independently reran all ten gates plus
  diff-check. No target build, CUE/Ymir run, cache-runtime proof, or FPS claim
  occurred. One serialized post-cutover target build is the next gate.

### A5.8 atomic-cutover target-memory repair (2026-08-04)

- The one exact guarded Route-0/live-input/Pipe4 `-B -j1` build at source GO
  `1819f2b4` compiled successfully and reached the linker after 299.6 seconds.
  Link failed before artifact packaging: `.bss` overflowed HWRAM by 10,032
  bytes. Full log: `.tmp-a58-cutover-target-build-20260804.log`. No fresh
  ELF/ISO/CUE, runtime cache, Ymir, or FPS evidence is credited.
- Map/object inspection showed activation retained previously GC'd callback
  state. The two master-only final terrain streams `s_terrain_emit_refs` and
  `s_terrain_emit_scratch` consumed 13,872 bytes each in HWRAM. They are used
  only after terminal descriptor assembly by the master's stable sort and
  final VDP1 lower, so moving their 27,744 bytes to `.lwram_bss` does not
  change claimant ownership or expose VDP state to the slave.
- Watched RED/GREEN: the live-cutover source suite first failed placement and
  also exposed stale assertions for pre-graph direct queue calls. It now
  requires `.lwram_bss` on both merge streams and checks graph publication and
  runtime master drain. Wrapped Python result: two tests PASS; scoped
  `git diff --check` PASS.
- Fresh review and exactly one post-repair serialized target rebuild remain
  required. No repeated build or emulator launch occurred.

### A5.8 live-cutover target link proof (2026-08-04)

- Memory repair `0519f50d` received independent GO with no findings at audit
  `b997fea1`. The reviewer independently passed the two focused tests and
  confirmed the 27,744-byte relocation is master-only and leaves more than
  the 16 KiB required LWRAM floor.
- The sole post-review Route-0/live-input/Pipe4 `-B -j1` rebuild completed at
  exit 0 in 522.4 seconds. Full log:
  `.tmp-a58-cutover-target-rebuild-20260804.log`; it contains zero compile,
  unresolved-reference, overflow, or region-fit error matches. Link, binary,
  `SOURCE.DAT`, ISO, and CUE packaging all completed.
- Linked sections: `.text` `0x06004000+0x7f4d8`, `.data`
  `0x06085370+0x9cd4`, `.bss` `0x0608f060+0x6ca70`, P2 `.uncached`
  `0x260fbad0+0x61c` with physical `___end=0x060fc0ec`, `.lwram_cmdts`
  `0x00200000+0x20000`, and `.lwram_bss` `0x00220000+0xd87b0` ending
  `0x002f87b0`. HWRAM margin is `0x3f14` (16,148 bytes); LWRAM margin is
  `0x7850` (30,800 bytes). `sh-elf-nm -u` is empty.
- Live ELF symbols include WORLD_ADMIT `0x060711d8`, WORLD_LOWER
  `0x06070754`, ACTOR_ADMIT/transform `0x0606fc88`, ACTOR_LOWER/classify
  `0x0606fe90`, graph publish `0x0607474c`, runtime master drain
  `0x060757a4`, slave poll `0x06075690`, and slave entry `0x06075780`.
  These callbacks/drains are no longer garbage-collected.
- Disassembly proves one non-null application registration: runtime activation
  loads `render_job_slave_entry` into `r4` and calls `cpu_dual_slave_set` once.
  The only other linked call is libyaul reset passing null; the old
  SlaveDriver `dual_slave_entry` is absent. Thus the accepted application has
  exactly one CPU-DUAL owner.
- Fresh artifacts (UTC 2026-08-04): CUE 88 bytes at `23:08:00.6561115`,
  SHA-256 `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`;
  ISO 4,646,912 bytes at `23:07:59.2521110`, SHA-256
  `122682dc5f775b0459b878f3fabc56965fb1ae74baf4eba0381a7fe21be91bc1`;
  ELF 8,646,804 bytes at `23:07:51.7491114`, SHA-256
  `40fe0b737ccbce7a0815cd9eb9b108df4510c79664066cf8dc3a12fd93abf9f2`;
  map SHA-256 `ebbd62a560b858fb5100b4e65280e0d4537e67463090c3a9be6072f9efbc670c`;
  `SOURCE.DAT` SHA-256
  `9890b2d44f18c9e2828bec63de938bfb4d29136301a30ae1da99517dba2e3809`.
- Exact desktop-test CUE:
  `build/saturn/sourceboot/e2-bob-demo-replay-camroute0-live-input-boot600-atan2v2-camv3-stage8-r6000-slave1-poly2-hot1-clip1-bsp1-frag0-pipe4/sm64-saturn-sourceboot-e2.cue`.
  No Ymir launch or FPS claim occurred in this build gate.

### A5.8 atomic-cutover desktop-Ymir result (2026-08-04)

- The owner manually launched the exact fresh CUE recorded above with the
  project profile and observed roughly 3–4 FPS.
- This matches the prior A3+A4 candidate and is not a visible performance
  improvement. It confirms only that the cutover boots/renders in desktop
  Ymir; it does not prove useful slave work, balanced claims, reduced terminal
  waits, or target cache efficiency.
- Next evidence must expose master/slave claim counts by phase, positive slave
  retirement timing, and the master terminal-wait interval. A5.8 is not a
  performance win until those counters lead to a visibly faster candidate.

### A5.9 claim/retirement telemetry source evidence (2026-08-04)

- Watched RED: the live four-job runtime fixture failed to compile because no
  telemetry type, snapshot, or terminal-wait publication API existed; the HUD
  fixture likewise failed on absent append-only profile fields.
- GREEN: the runtime's existing uncached shared record now owns separate
  master/slave claim arrays in callback-ID order, notified/retired generation
  and sequence, per-CPU failures, bounded master wait iterations, and terminal
  quarantine count. The master increments only a local scalar in the existing
  retirement loop and performs one telemetry publication afterward.
- The delayed-slave schedule is executable evidence of a legal zero-overlap
  outcome: after notify, immediate master drain claims WORLD_ADMIT,
  ACTOR_ADMIT, WORLD_LOWER, and ACTOR_LOWER; the delayed slave retires with no
  claim. This does not establish that Ymir takes that schedule.
- A failure generation proves WORLD_ADMIT failure is counted once, dependent
  WORLD_LOWER becomes quarantined, and independent ACTOR_ADMIT/ACTOR_LOWER
  still complete. The VDP2 HUD exposes `QM/QS` and `QN/QR/QW/QF/QQ` without
  printf or floating-point formatting.
- Strict direct C11/Werror runtime and VDP2 fixtures PASS. Runtime and live
  cutover Python source suites pass (4 and 2 tests). The legacy graph-source
  suite remains stale at HEAD because it still asserts the now-landed graph
  include is absent; it is not credited. The broad tools suite was stopped at
  the 120-second host timeout after beginning green tests. No target build,
  CUE, Ymir run, cache observation, or FPS claim occurred.
- First independent review found a cross-CPU publication race: the slave set
  `retired_sequence` before its retired telemetry. A master could therefore
  observe positive retirement and snapshot stale zero generation/sequence.
  Watched RED/GREEN adds a source-order mutation test and publishes telemetry
  first, then the positive retirement release marker, in both SH-2 and host
  paths. Strict runtime fixture and five runtime source tests pass; fresh
  rereview is GO at `e98210ba`. The reviewer independently passed diff-check,
  strict runtime and VDP2 fixtures, and 20 focused Python cases (one historical
  capture skip). One serialized target build is the next gate.

### A5.9 telemetry target link proof (2026-08-04)

- The effective guarded Route-0/live-input/Pipe4 `-B -j1` target build exits
  0 in 447.8 seconds and completes ELF, binary, `SOURCE.DAT`, ISO, and CUE
  packaging. Full successful log:
  `.tmp-a59-telemetry-target-rebuild-20260804.log`; targeted scanning finds
  zero compiler errors, undefined references, overflows, or region-fit
  failures. The existing executable-stack/RWX linker warning remains.
- Two environment-only starts are not credited as target results: the first
  stopped before compilation because `.yaul.env` was not loaded; the second
  reached soft-fp compilation but MSYS selected an unwritable `/tmp`. No
  source was changed. The successful invocation redirected only compiler
  temporaries to the worktree-local `.msys-home/tmp` and loaded the established
  Yaul environment.
- Linked sections: `.text` `0x06004000+0x7f728`, `.data`
  `0x060855c0+0x9cd4`, `.bss` `0x0608f2a0+0x6cad0`, P2 `.uncached`
  `0x260fbd70+0x65c` with physical `___end=0x060fc3cc`, `.lwram_cmdts`
  `0x00200000+0x20000`, and `.lwram_bss` `0x00220000+0xd87b0` ending
  `0x002f87b0`. HWRAM margin is `0x3c34` (15,412 bytes); LWRAM margin is
  `0x7850` (30,800 bytes). `sh-elf-nm -u` is empty.
- The appended profile/runtime telemetry is target-link clean. The ELF retains
  `sm64_saturn_render_job_runtime_telemetry_snapshot` at `0x060759c0`, slave
  poll at `0x06075704`, slave entry at `0x06075820`, master drain at
  `0x0607584c`, VDP2 frame begin at `0x06076434`, and VDP2 frame commit at
  `0x06076684`. Thus both the producer snapshot and HUD consumers survived
  section garbage collection; live counter values remain a Ymir-only gate.
- Fresh artifacts (UTC 2026-08-04): CUE 88 bytes at `23:51:51.3516383`,
  SHA-256 `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`;
  ISO 4,646,912 bytes at `23:51:50.1756383`, SHA-256
  `d21138b2fa759543de21cf70a8521a9193ad4741683aebb688e8dac423d84f3f`;
  ELF 8,653,180 bytes at `23:51:43.5976385`, SHA-256
  `76bccfc48e55b3cd9d3f7cb3ef673aa51101b4fe3a10d944a227807816519f6a`;
  map SHA-256 `284aa0a4ada496969d7eba60644db433db511c1246a2160d9fc3ac22b45f1222`;
  `SOURCE.DAT` SHA-256
  `8f2a51029a9a22f35d63ff699c9596366104726128c9f2f1939f52b8c068e125`.
- Exact desktop-test CUE:
  `build/saturn/sourceboot/e2-bob-demo-replay-camroute0-live-input-boot600-atan2v2-camv3-stage8-r6000-slave1-poly2-hot1-clip1-bsp1-frag0-pipe4/sm64-saturn-sourceboot-e2.cue`.
  No Ymir launch, visible HUD transcription, scheduler conclusion, or FPS
  claim occurred in this gate.

### A5.9 automatic queue-observation source evidence (2026-08-05)

- Watched RED: `python tools/saturn/test_capture_sourceboot_throughput.py`
  failed with `AssertionError: sourceboot throughput capture helper is missing`.
- GREEN: the focused host suite reports `Ran 9 tests ... OK`; the existing
  sourceboot boot-trace suite reports `Ran 16 tests ... OK`; both modules pass
  `python -m py_compile`, and `git diff --check` is clean for this task.
- The new bounded tool requires explicit matching CUE and ELF inputs, records
  SHA-256 identities for them, their referenced ISO, and Ymir, resolves
  `_sourceboot_boot_trace` (32), `_s_runtime` (92), and
  `_s_render_job_queue` (232) with an in-process ELF32 parser, and rejects
  missing, duplicate, stripped, or wrong-size definitions.
- Before reading telemetry it matches a linked executable ELF byte window in
  target memory. It then reads the three records through P2 after exactly one
  emulated VBlank per sample. It accepts terminal queue data only when queue
  generation is zero, notify equals retire and is nonzero, and QN equals QR
  and is nonzero; a sequence cannot attach to two presentation edges.
- The report contains only bounded artifacts, addresses/sizes, identity proof,
  cadence summary, latest coherent queue data, and presentation edges. RPC,
  identity, or coherence failures instead produce `status: failed` with a
  stage/type/message; no success-shaped partial is emitted.
- No target build, Ymir invocation, live queue capture, FPS result, or
  scheduler-policy change occurred. The queue observation gate remains open
  until a matching CUE produces at least two VDP2 presentation edges and one
  coherent terminal queue record.

### A5.9 queue-observation review repair 1/5 (2026-08-05)

- A repeated coherent retirement sequence at a second presentation edge now
  fails the capture, rather than being excluded from that edge while allowing
  a success report. The end-to-end fake-Ymir regression test supplies the
  duplicate sequence and observes the required failure.
- Identity evidence now accepts only allocated executable `SHT_PROGBITS` bytes
  in a loadable `PT_LOAD` file/virtual range. Non-alloc, non-PROGBITS, and
  non-loadable executable fixtures are rejected.
- Serialized JSON-RPC notification diagnostics have an independent 64-record,
  64-KiB bound and report original count/bytes/truncation. This remains bounded
  even if the imported client retains an unbounded notification list.
- Focused capture tests: 12/12 PASS; existing boot-trace tests: 16/16 PASS;
  both modules compile. No target build, Ymir capture, or target conclusion
  occurred. The queue gate remains unchecked.

### A5.9 queue-observation review repair 2/5 (2026-08-05)

- A malformed identity section can fit one `PT_LOAD` file range and virtual
  range while mapping those ranges with different offsets. The probe now
  requires exact affine equality before hashing file bytes for a target
  address; the malformed fixture is rejected.
- Watched RED: the new offset-mismatch test failed because the old validator
  accepted that ELF. GREEN: capture tests are 13/13 and boot-trace tests are
  16/16, both modules compile, and whitespace verification passes.
- No target build, Ymir invocation, live queue observation, or scheduler/FPS
  conclusion occurred. The correction is source-only; fresh review and a valid
  matching live capture remain open.

### A5.9 queue-observation startup repair 3/5 (2026-08-05)

- The first live attempt's 120-byte historical blob is discarded as a wrong
  IPL. With the established 524,288-byte USA IPL, a diagnostic proves that the
  existing BIOS macro ends before sourceboot code is loaded: exact `main`
  bytes appear at post-BIOS +570 VBlanks and trace magic at +600.
- The collector now starts a separate bounded one-VBlank identity loop after
  BIOS handoff and before all telemetry reads. On success it reports
  `startup_vblanks_waited` and `startup_identity_attempts`; on exhaustion it
  fails at `target-identity` without consuming `--max-vblanks`.
- Focused capture tests: 17/17 PASS; boot-trace tests: 16/16 PASS; modules
  compile and diff-check is clean. No target build, Ymir launch, or new live
  observation is credited by this source repair.

### A5.9 automatic queue observation live proof (2026-08-05)

- Evidence JSON:
  `docs/saturn/evidence/reports/a59-sourceboot-queue-throughput-2026-08-05.json`.
  It binds CUE `cdbf0bfa...b0f46dba7`, ISO `d21138b2...23d84f3f`, ELF
  `76bccfc4...16519f6a`, and the post-DRAM-cart headless Ymir executable
  `fcc88d82...38d3943`.
- The earlier `build-agent` executable is rejected for this purpose: its July
  18 binary predates headless DRAM-cart support. Target `SCAR` telemetry proved
  `cart_id=0`, `cart_size=0`, and `MISSING_4MIB`. The existing `build-agent2`
  binary postdates `bf3e4a4a`, accepts the 32-Mbit cart, and completes the
  3.2-MiB `SOURCE.DAT` copy. No emulator or target rebuild was needed.
- Exact target ELF bytes match after 540 one-VBlank startup attempts. With the
  independent observation bound raised from 600 to 4096 to include cart-copy
  time, the capture completes after 1,185 observation VBlanks with three VDP2
  presentation events. Intervals are 11 and 14 VBlanks: 4.8 FPS mean, 4.87
  median, and 4.29 1%-low at nominal 60 Hz.
- Coherent retired sequence 1 reports `QN=3`, `QR=3`,
  `QM=[1,1,0,0]`, `QS=[0,0,1,1]`, `QW=0`, `QF=0`, `QQ=0`, and zero master or
  slave failures. Both SH-2s therefore claim useful nonduplicated work in the
  observed frame, and the master does not spin on retirement. A5.9's automatic
  observation gate is closed. The result does not prove equal per-job cost;
  final merge, VDP1 lowering/transfer, and total admitted geometry remain the
  next bottleneck candidates.

## 2026-08-05 — A7 source-bank ownership begins

Task 5/A5.9 is closed with exact-image queue evidence. Task 7/A7 is active at
the test-first lifecycle and memory-region contract. This transition adds no
target, Ymir, hardware, or FPS evidence; it prepares the lifetime boundary
needed by Task 8's deferred command/Gouraud transfers.

Design correction: the original four-function sketch could not represent the
`TRANSFERRING` state or prove retirement because both live emitters currently
wait internally and return `void`. A7 will expose transfer-obligation metadata
and a synchronous-complete adapter invoked only after that return. A frame with
zero Gouraud tables records an explicit satisfied no-op obligation. Actual
queue submission and polling remain deferred to A8.

The demo renderer also needs an explicit outcome: its pre-emission early
returns formerly left `main` unable to distinguish a complete upload from an
unchanged/stale backend prefix. A7 changes that boundary to return success;
only success may become `READY`, while failure quarantines the building bank
and preserves the previous publication. The profile frame serial remains
diagnostic-only.

Step 1 RED evidence: the new host fixture covers lifecycle ordering, exact
ticket retirement, zero-Gouraud no-op completion, stale generations, LWRAM vs
HWRAM source classification, and quarantine retention. Its direct host compile
fails because `saturn_vdp1_frame_bank.h/.c` do not yet exist, which is the
intended missing-manager failure.

Steps 2–3 RED evidence: the strengthened memory-map fixture rejects wrong
command-section size/alignment/region, wrong or missing Gouraud range, and a
configured HWRAM floor below `0x1B00`; six mutations fail against the old
verifier. The new aggregate `verify-vdp1-frame-bank` target also fails on the
missing manager sources, as intended.

Step 4 host evidence: `verify-vdp1-frame-bank` passes with explicit
FREE/BUILDING/READY/TRANSFERRING/PUBLISHED/QUARANTINED transitions, bounded
counts, exact ticket retirement, synchronous completion, zero-Gouraud no-op,
generation wrap, publish-before-retire fallback retention, and no-free refusal.
This is host lifecycle evidence only.

### A7 source completion and target-link evidence (2026-08-05)

Implementation and synchronized behavior documents are committed at
`650b911a`; independent specification and quality reviews remain open.
Steps 5–6 are source-complete. Sourceboot now acquires a FREE bank through the
manager instead of XOR selection, binds the renderer only after acquisition,
publishes only an explicit successful render after synchronous obligation
completion, and quarantines failure while retaining the prior publication.
Build, published, and displayed generations are independent. Both VDP1 command
prefixes are initialized unconditionally.

Focused evidence is green: `verify-vdp1-frame-bank`; 10/10 sourceboot memory
map tests; `verify-dma-queue`; `verify-terrain-command-template`;
`verify-runtime-contracts`; 6/6 presentation-boundary tests; and the focused
live-cutover source gate. A serialized SH-2 sourceboot build compiled, linked,
and produced ELF SHA-256
`45387e5210a6966fc3ebf615c2973e9c46faea8ce85cfb6bb0c565f40cf6867b`.
Its `.lwram_cmdts` is NOBITS at `0x00200000`, size `0x20000`; Gouraud staging
is at `0x060D8FB8`, size `0x6000`; and `___end=0x060FC86C` leaves `0x3794`
bytes above the HWRAM top, exceeding the required `0x1B00` floor. The full
target verifier remains open because the unrelated strict native-math census
reports an unreachable oracle dispatcher
`_play_cutscene -> _cutscene_bbh_death`. No Ymir, hardware, deferred-transfer,
or FPS evidence is credited to A7.

Two broader host collections remain honestly open outside the A7 assertions:
the dual-actor binary stops at its existing "live game or VDP state" context
check, and the complete render-cluster Python suite expects generated Mario
LOD symbols absent from this tree. The A7-updated live renderer signature,
queue-merge, presentation, and one-generation-before-admission assertions pass
when run at their focused boundaries.

Provenance is pattern-only: SlaveDriver commit
`a8986591557b6e680550d3c23970284d3b38ff8f` (GPL-3.0-or-later, `WALLS.C`),
Z-Treme commit `cff75451c1616aac1236fc2b44223902b55c706b` (GPL-3.0,
`ZT_GAME.c`), and Yaul commit
`6012f79f237773378c8014e70d8998ad95a38d98` (MIT, `cpu_dmac.c`) were inspected.
No upstream code was copied. Yaul's convenience call waits before starting a
channel, so A8 must not treat it as a zero-wait submission primitive.

### A7 consolidated review repair begins (2026-08-05)

Independent review is NO-GO pending three fail-closed repairs: stale
out-of-order completion can currently regress `published`; both emitters can
publish after a second Gouraud queue-submit failure; and manager initialization
does not yet reject aliased, overlapping, or misaligned command/Gouraud
storage. The late-completed bank disposition is quarantine. Red direct tests
for all three contracts are required before production changes. The stale
`main.c` HWRAM comment must also say `0x1B00`, matching the linker/verifier.

RED evidence: the expanded frame-bank fixture aborts when aliased command banks
are incorrectly accepted (line 111); its later cases also pin command/Gouraud
overlap, alignment, and late `UINT32_MAX` completion after generation 1. The
new Gouraud-transfer target fails to compile because the shared result-bearing
helper does not exist. Production repair now begins from those failures.

Focused GREEN evidence: the expanded lifecycle binary passes all stale-wrap,
quarantine, alias, overlap, and alignment cases; the shared transfer helper
passes zero-work, first-submit, retry-success, and repeated-failure cases; and
2/2 source mutation checks pin stale quarantine before publication plus false
outcomes from both emitters propagated by sourceboot. Presentation remains 6/6
and memory-map validation 10/10. A serialized target compile/link check is next;
no Ymir/FPS claim is made.

Target integration GREEN: the single serialized incremental Pipe4 validation
compiled both renderer paths, the shared transfer helper, manager, and main;
then linked and packaged fresh ELF SHA-256
`b0ede6f93681984046904b1c4743f6bb38c7b3de89fa6f8e562baa23190401cc`.
The broad verifier subsequently stopped at the unchanged unrelated census
error `INDIRECT_EDGE has unreachable dispatcher: _play_cutscene ->
_cutscene_bbh_death`. No second target build was run. The live-cutover source
binary also passes after the repair. A final host-green strengthening rejects
overlapping or misaligned Gouraud manager objects before dereference; it was
not target-rebuilt because the review task allowed at most one serialized
target validation. Repair commit `41a4ce7e` contains the production, tests,
build integration, and synchronized behavior documents. Independent rereview
remains open.

### A7 final closure (2026-08-05)

Independent consolidated rereview of reviewed HEAD `deebd06e` is
PASS/APPROVED; both strict host fixtures independently pass. The exact audited
Route0/live-input/Pipe4 wrapper ran `make -B -j1` to exit 0 in 336.9 seconds.
Fresh ELF:
`build/saturn/sourceboot/e2-bob-demo-replay-camroute0-live-input-boot600-atan2v2-camv3-stage8-r6000-slave1-poly2-hot1-clip1-bsp1-frag0-pipe4/obj/sm64-saturn-sourceboot-e2.elf`,
SHA-256 `1eba88885b611f0c99dea3971dda871fcc30fdb8ac21c4fcfc5e051f0e99267c`.
A7 is complete. A8 is active next but has no behavior-complete claim; A7 adds
no Ymir, hardware, asynchronous-transfer, or FPS result.

### A8 deferred-transfer source integration (2026-08-05)

A8 removes both accepted emitters' blocking transfer/upload tails. Sourceboot
now crosses the single-destination VDP1 overwrite fence, atomically enqueues
the LWRAM command prefix as CPU-DMAC and the HWRAM Gouraud prefix as SCU-DMA,
kicks once, and returns without waiting. Ordinary and stale-loop iterations
poll exact descriptor status; both retirements are required before a later
fresh-field master transition consumes a one-shot resident-list arm, publishes
the bank, and presents that bank's snapshot generation. A first-descriptor
failure keeps the sibling draining before the bank becomes QUARANTINED, so
queued work cannot outlive reusable source data.

Focused host evidence is green: strict DMA queue, A7 frame-bank, A8 transfer
pipeline, VDP2 HUD, and profile layout/decode fixtures, plus the source
anti-pattern/state-machine checks. These prove serial transport selection,
zero submit waits, completion-IHR CPU retirement, guarded SCU entry, exact
destination/capacity rules,
atomic pair failure, staggered retirement, durable exact failure status, and
exactly-once arm. Independent review and target compilation remain open. No
target boot, Ymir capture, hardware result, or FPS improvement is claimed.

### A8 independent-review repair (2026-08-05)

The first A8 review is NO-GO. It found pinned Yaul's channel-busy calculation
can report false idle for active DE=1/TE=0, serial transfer stages advanced at
most once per fresh field, VDP2 reread mutable camera state, partial resident
VRAM failure could leave old metadata eligible for plotting, wait telemetry
timed nonblocking calls, and destination validation covered only used prefixes.

The repair hands CPU-DMAC channel 0 to the queue after boot work retires,
configures/starts it through public Yaul APIs, and retires only from its
completion IHR; an exact host model keeps `channel_busy=0` throughout the
active transfer and proves no early retirement. Stale loop iterations now
poll/kick until the serial CPU then SCU stages reach terminal status, while
publication remains VBlank-owned. A failed/partial resident transfer poisons
the destination and disables plotting. Each BUILDING bank captures the camera
consumed with its published VDP1 generation. Ordinary command/Gouraud and
terminal wait counters are explicitly zero, QNS is guarded and single-counted,
and full declared VRAM capacities plus Gouraud 8-byte alignment are enforced.

Fresh strict DMA, transfer-pipeline, frame-bank, VDP2, runtime-contract, and 15
source/mutation tests pass. Memory-map unit coverage passes 10/10; profile
layout/decode passes 21 tests with one historical-capture skip. The first
aggregate MSYS `make` invocation was discarded because POSIX `realpath` was
fed to Windows Python as `\d\...`; the same gates were run directly with the
configured Windows host compiler/Python. The scoped repair landed as
`8b037a7d` (`fix(saturn): harden deferred VDP1 completion`); independent
rereview and any serialized target build remain open. No target/Ymir/FPS claim
is made.

### A8 final runtime and cadence closure (2026-08-05)

The first exact A8 capture failed before DMA because a fully culled Mario
produced zero admitted actor positions and the renderer quarantined the frame.
The scene-neutral repair publishes a two-job terrain graph for successful zero
admission and retains the four-job graph for visible actors. Independent review
then found that the old count return also used zero for actual preparation
errors. A watched RED/GREEN boolean-success plus output-count contract restores
fail-closed behavior; independent rereview is PASS with no Important issue.

Seven A8 source contracts and 22 configurable-depth capture tests pass. The
serialized exact target build exits zero and produces ELF SHA-256
`10e92064175f1d277039322f6be874f646b71a486c7f18ccd8a2e9786df569ab`.
The exact report
`docs/saturn/evidence/reports/a8-deferred-transfer-throughput-long-2026-08-05.json`
matches that ELF and completes ten presentation events. Its nine intervals are
36--38 fields: 1.63 FPS mean, 1.62 median, and 1.58 1%-low. All ten queue
generations retire with `QM=[1,1,0,0]`, `QS=[0,0,1,1]`, `QN=QR=10`, and
`QW=QF=QQ=0`; worker failure counters are also zero. A8 closes the transfer
lifetime prerequisite but provides no measured cadence uplift. The next task
must split CPU construction and simulation timing before choosing A9 overlap.

### A9 Step 0 field-resolution attribution (2026-08-05)

A 60-byte cache-through seqlock trace samples cumulative VBlank crossings for
simulation, frame construction, and transport/presentation without changing
scheduler behavior. The first independent review rejected a one-marker
seqlock, a construction boundary after Mario pose, an incomplete publication
boundary, and lost diagnostics on torn reads. The repair marks both ends odd
before payload, includes snapshot/pose preparation in construction, reports an
explicit unattributed residual, names the combined transport/presentation
phase honestly, and wraps cadence decode failures in bounded diagnostics.

The serialized target build exits zero with exact ELF SHA-256
`1ffb47ccb10908507703eba6799d5891af3a71cada70ffac4de189a26e4edfe6`.
The exact ten-edge report
`docs/saturn/evidence/reports/a9-phase-attribution-throughput-2026-08-05.json`
accounts for all 333 interval fields: simulation 283 (85.0%), construction 49
(14.7%), transport/presentation 1 (0.3%), unattributed 0. Each presentation
interval executes six simulation ticks and the nine intervals drop 222 more
VBlank credits. Thus the current per-outer-iteration two-tick catch-up limit
repeats three times before one frame publishes. Task 9 Step 1 must make normal
plus recovery work presentation-generation scoped.

### A9 Steps 1--4 scheduler-model review (2026-08-05)

The first hardware-free model passes its nominal fixture and rejects the
four-tick, repeated-credit, and incomplete-publication mutations. Independent
review is nevertheless **NO-GO** for runtime integration. Generation wrap from
`UINT32_MAX` to zero aliases zero-valued unset completion and queued-snapshot
sentinels, which can make incomplete generation zero publishable. Publication
also resets generation-local SERVICE/POLL flags, permitting more work for a
promoted generation during the same observed VBlank. Finally,
`verify-frame-pipeline` is not yet part of `verify-all`.

The Step 5 integration map found a third contract defect: the model advances
displayed generation and resets cadence when it returns `PUBLISH_FRAME`, before
the target's fallible arm-resident-list, bank publication, retirement, and VDP
commit sequence succeeds. Publication must be an intent followed by an
exact-generation success acknowledgement; failure must retain the prior
displayed generation and must not reopen cadence credit.

Step 5 remains blocked. Required evidence is RED/GREEN wrap coverage including
a queued generation zero, a same-field post-publication service/poll rejection,
publish failure/acknowledgement coverage, aggregate-gate inclusion, fresh
nominal and mutation runs, and independent rereview. No target build, runtime
behavior, or FPS uplift is claimed.

The repair is now source-complete and awaiting independent rereview. Explicit
validity preserves active and queued generation zero; field-global epochs keep
SERVICE/POLL bounded across publish/promote; and target publication must
acknowledge the exact generation before scheduler display/cadence state commits.
Fresh directly compiled nominal coverage passes and all three mutation
executables are caught. `verify-frame-pipeline` is included in `verify-all`.
The host's native Make recipe still compiles then fails at the known MSYS
quoted-Windows-executable handoff, so the direct results are the product-test
evidence. Separately, the Step 5 source contract is RED 7/7 against the legacy
loop. No target build or uplift is claimed.

Independent rereview is **GO** for the scheduler layer. The reviewer reproduced
nominal PASS and mutation rejection, confirmed wrap-zero validity, global
per-field work epochs, exact two-phase publication acknowledgement, and
fail-closed null/wrong-generation handling. Steps 1--4 are complete; Step 5 is
active from the intentional 7/7 RED source contract. This is not yet runtime or
FPS evidence.

The reviewed scheduler/model and Step 5 RED contract are checkpointed in
`d49b8677`. Runtime integration remains active and unproven.

The Step 5 compatibility adapter is source-GREEN across 29 focused tests, but a
pre-target cadence audit is NO-GO: the model currently converts VBlank fields
to simulation credits 1:1, doubling healthy source logic from 30 Hz to 60 Hz.
Target build is intentionally withheld pending a wrap-safe two-field fractional
accumulator and rereview. Field-rate SERVICE/POLL/presentation behavior must
remain unchanged. No FPS evidence is claimed.

Combined review confirms the 30 Hz accumulator but remains **NO-GO** for target
build. The adapter must acknowledge exact successful publication before
refreshing telemetry, committing VDP2, and appending cadence evidence. The
scheduler must also skip wrapped generation zero consistently with downstream
snapshot/frame-bank ownership, which reserves zero as invalid. Both repairs
are active; prior source-green results do not satisfy these gates.

Both repairs are now source-complete and awaiting final rereview. The shared
successor skips reserved zero in scheduler and sourceboot. Exact publish ack now
precedes telemetry, presentation, and cadence evidence; failure cannot emit a
presentation edge. Focused source tests pass 30/30, the scheduler nominal test
passes, and all three mutations are rejected. Target evidence remains absent.

Final combined rereview is **GO** for a serial target build. Legacy telemetry
identifiers containing `vblank_credit` remain unchanged for compatibility, but
new A9 values represent discarded whole 30 Hz simulation-tick credits rather
than raw fields. Any comparison with earlier field-unit captures must convert
units explicitly. No target or FPS result has yet been recorded.

The forced serial target build exits zero after 331 seconds through the
DLL-safe wrapper. Exact artifacts are ELF SHA-256
`6685d058d876119118b2a5a65a5a682111a29596aff4ce24901954ee6073f689`
(8,694,212 bytes), ISO `7fbb642744a36efafc772c79148839009fe95729afd3ad11d0fca4ee8ab1834b`,
and CUE `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`.
The broad verifier separately exits on the retained native-math census error
`_play_cutscene -> _cutscene_bbh_death`; it remains unchecked. Exact-ELF
cadence capture is the next gate.

The corrected exact capture completes at
`docs/saturn/evidence/reports/a9-step5-frame-adapter-throughput-2026-08-05.json`.
All ten queue generations retire (`QN=QR=10`, `QW=QF=QQ=0`) and every interval
executes one simulation tick rather than six. The trace's real observed-field
deltas are `13,12,13,13,14,14,14,14,14`: 4.463 FPS mean and 4.286
median/1%-low, 2.752x / +175% over the 1.622-FPS Step 0 mean. The summarizer now
uses coherent `cadence.observed_vblank_generation` for ISR-field time and keeps
source presentation-generation deltas separate; legacy no-cadence captures
retain their fallback. Focused measurement tests pass 29/29 and independent
measurement rereview is GO. Manual Ymir remains open.
Independent measurement rereview is **GO**. On-disk artifact hashes match the
report, cadence/event generations 1--10 agree, ISR stamps sum to 121 fields
across nine intervals, and all queue generations retire without waits/faults.
Explicit negative unit cases pass for mixed-clock and generation-mismatch
branches; manual Ymir remains open.

Implementation and evidence are checkpointed at `36f4fe58`. Independent
scheduler, adapter, combined-tree, and measurement rereviews are all GO. The
manual desktop Ymir observation remains open, as does the unrelated broad
native-math census gate.

The profile-backed desktop helper launched the exact A9 CUE with the project's
32-Mbit DRAM configuration. PID 34856 remained live/responding after the
20-second monitor; details are in
`a9-step5-desktop-launch-2026-08-05.json`. This proves only launch configuration
and survival. Owner-visible speed, controls, and geometry remain unchecked.

## Task 9 Step 6 VDP2 generation-coherence reconciliation (2026-08-05)

Commit `2377bf8b` is source-complete: VDP2 accepts only a camera owned by the
displayed VDP1 bank and a displayed/rendered/simulation tuple, updates the HUD
tuple with sky identity changes, and rejects zero or mismatched ownership before
callbacks. Direct host VDP2 and runtime-contract binaries passed, and the
sourceboot presentation-boundary mutation suite passed 7/7. Fix Round 1 adds
direct fixture checks that zero displayed or simulation generations cause no
backend effects. Independent review found the code **APPROVED WITH MINOR
FOLLOW-UP**, but rendered the specification **NO-GO** solely because the active
plan and SDD ledger had not recorded the transition; this reconciliation closes
that process defect. Focused rereview is pending. No target build/capture,
manual Ymir observation, or broad native-math result is claimed here.

### Task 9 Step 6 Fix Round 2 (2026-08-05)

The first reconciliation commit placed its surgical ledger hunk inside an
unrelated Task 5.7 sentence, and its displayed-zero fixture also exercised
mismatch rejection. Fix Round 2 restores the surrounding Task 5.7 prose and
records Step 6 at a complete ledger boundary. The direct VDP2 fixture now uses
`snapshot == displayed == rendered == 0` with nonzero simulation, so removing
the explicit displayed-zero guard makes that case fail; its independent
simulation-zero case remains otherwise coherent. The direct C11/Werror fixture
passes. Scoped Fix Round 2 rereview is **PASS / APPROVED**: the Task 5.7 prose
is contiguous, Step 6 begins at a valid ledger boundary, and the displayed-zero
fixture now isolates the explicit reserved-zero guard. Step 6 closes as source-
complete. No target, Ymir, manual, or native-math gate is claimed.

## Task 9A plan transition — true frame-lifetime overlap (2026-08-05)

**Status: active plan; implementation and all evidence gates unchecked.** The
post-A9 bottleneck map proves that the accepted adapter still enters
`sm64_saturn_demo_render_frame()` and cannot return to the scheduler while
slave generation `N` is live. Task 9A is inserted before Task 10 to split that
one synchronous completion boundary into notify-only start and positive-
retirement poll/finalize phases. Task 10 remains hardening/publication and a
scene-neutral coverage gate; it is not the next expected FPS lever.

The planned transaction retains exactly one active render generation. Start
publishes immutable jobs for nonzero `N` and returns without master drain,
retirement wait/reset, final merge, Gouraud reservation, VDP1 lowering, or A8
transfer. Pending `N` retains its render snapshot, descriptor payloads, and
BUILDING source bank while the master may execute the one queued authoritative
source tick for `N+1`. The queued snapshot cannot be acquired as an active
render until `N` completes, transfers, receives exact publication
acknowledgement, and retires. Poll remains PENDING until positive slave
retirement; then the master drains remaining READY work, validates/merges,
lowers once, and retires `N`. FAILED quarantines `N`, preserves the prior
complete frame, and never replays a full frame.

Unchanged contracts are master-only simulation, input, source state,
allocation, final ordering, VDP1/VRAM, VDP2, and presentation; A9's nonzero
`UINT32_MAX -> 1` successor, 30 Hz remainder, one-normal-plus-one-recovery
budget, per-field service/poll epochs, exact publish acknowledgement, and
previous-frame reuse; and A8's sole transfer/resident-list ownership. Required
RED/GREEN evidence covers pending lifecycle, wrap, missed deadline/reuse,
failure/no replay, one active generation, queued-snapshot exclusion, and no BOB
symbol dependency in generic state. Target evidence must split source tick,
overlapping slave work, and master finalization.

Pinned prior art remains exactly as already recorded: SlaveDriver
`a8986591557b6e680550d3c23970284d3b38ff8f` (GPL-3.0-or-later; existing close
ports plus pattern-only lifetime split), Z-Treme
`cff75451c1616aac1236fc2b44223902b55c706b` (GPL-3.0; pattern-only), Yaul
`6012f79f237773378c8014e70d8998ad95a38d98` (MIT; dependency/API use), Jo
Engine `556d081146211b6a1cfa6591d70f9487d406758b` (MIT/BSD-style file notices;
pattern-only), and sm64-psx `3073845688ea273da78d539b20c45110d8a868c3`
(no repository-wide license; behavior-study only). No new upstream source is
copied or close-ported by this plan transition.

Unchecked implementation gates:

- [ ] renderer lifecycle/source integration RED evidence;
- [ ] lifecycle, wrap, deadline/reuse, failure, and scene-neutral GREEN tests;
- [ ] specification review and quality review;
- [ ] versioned target phase-trace RED/GREEN evidence;
- [ ] one serialized DLL-safe `make -B -j1` build after both reviews;
- [ ] exact-identity automatic capture with phase, queue, transfer, reuse, and
  FPS data;
- [ ] owner-visible Ymir acceptance; and
- [ ] broad native-math publication census (retained pre-existing blocker).

No tests, builds, captures, or reviews were run for this documentation-only
transition.

## Task 9A Steps 1--3 — watched lifecycle/source/cadence RED (2026-08-05)

Pinned prior art was re-inspected before production work: SlaveDriver
`a8986591557b6e680550d3c23970284d3b38ff8f` (GPL-3.0-or-later,
`WALLS.C:1240-1408,1803-1950`, `DMA.C`, `DMA.H`, `V_BLANK.C:94-145`), Sonic
Z-Treme `cff75451c1616aac1236fc2b44223902b55c706b` (GPL-3.0,
`ZT_RENDERING.c:406-505,718-786`, `ZT_FRUSTUM.c:126-161`,
`ZT_LOADING.c:118-176,299-355`, `workarea.c:12-25`), Yaul
`6012f79f237773378c8014e70d8998ad95a38d98` (MIT, public DMA/VDP1 APIs and
`libmic3d/render.c`), Jo Engine
`556d081146211b6a1cfa6591d70f9487d406758b` (MIT plus file-level BSD-style
notices, `vdp1_command_pipeline.c`, `3d.c`), and the recorded sm64-psx pin
`3073845688ea273da78d539b20c45110d8a868c3` (no repository-wide license,
source-loop/compact-render behavior study only). Reuse remains existing
dependency/API use and pattern-only project code; no source is copied.

Watched RED commands and intended failures:

- `.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_a9_frame_pipeline_integration_contract.py`
  exits 1: the service adapter has no `sm64_saturn_demo_render_start_frame()`
  or retained pending transaction.
- `.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_a9_sourceboot_cadence_trace_contract.py`
  exits 1: sourceboot still publishes version 1/60 bytes and calls the
  monolithic renderer.
- `mingw32-make -f Makefile.saturn.mk verify-demo-render-overlap` exits 1:
  the real fake-hook fixture cannot include or compile the missing
  `saturn_render_lifecycle.{h,c}` production seam.
- `.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_capture_sourceboot_throughput.py`
  runs 31 tests and exits 1 with one failure/one error: the decoder still
  reports 60 bytes/version 1 and no `slave_work_overlap_window` phase exists.

The initial literal `python` and `make` invocations were unavailable in this
PowerShell host, so the evidence reruns use the repository venv and native Qt
MinGW make. No MSYS/SH command, target build, Ymir, broad verify, or
native-math census was invoked. Steps 4--12 remain unchecked.

## Task 9A Steps 4--9 — source implementation GREEN (2026-08-05)

**Status: Steps 1--9 implemented and focused-host-green; review next.** This is
not a source-complete, target-build, runtime, capture, or FPS claim. Steps
10--12 remain unchecked.

The renderer now owns one scene-neutral exact-generation lifecycle record.
Start prepares/publishes the immutable graph, notifies once, and returns with
zero master drain/final merge/Gouraud/VDP1 lowering. Poll remains PENDING until
positive slave retirement, then drains remaining READY work and performs the
existing terminal validation, stable merge, queue reset, Gouraud reservation,
and VDP1 lowering exactly once. A failed generation is quarantined and cannot
replay. Sourceboot retains the exact snapshot, Mario pose, descriptor payloads,
and BUILDING bank across PENDING; only COMPLETE marks that bank READY and
acknowledges render completion. A8 still exclusively owns transfer and
resident-list publication.

The existing pure frame scheduler required no production change. Its extended
fixture proves a pending `N` across repeated fields, queued immutable `N+1`,
wrong-generation rejection, previous-frame reuse, exact publication
acknowledgement, and delayed promotion. Existing wrap, publish-failure,
normal-plus-recovery, and one-service/poll-per-field coverage stays green.

Cadence publication is now version 2, 76 bytes/19 words. Four appended
cumulative words record slave-work crossings/count and master-finalization
crossings/count. The decoder accepts only explicit v1/60-byte or v2/76-byte
records. It exposes `source_tick`, `slave_work_overlap_window`, and
`master_finalization`; the slave window is non-additive, preventing double
counting with concurrent source work.

Serial focused results:

- `mingw32-make -f Makefile.saturn.mk verify-demo-render-overlap`: PASS;
  nominal plus three caught mutations.
- `mingw32-make -f Makefile.saturn.mk verify-frame-pipeline`: PASS; nominal
  plus three caught mutations.
- repository-venv `test_a9_frame_pipeline_integration_contract.py`: 9/9 PASS.
- repository-venv `test_a9_sourceboot_cadence_trace_contract.py`: 3/3 PASS.
- `verify-render-job-runtime`, `verify-render-job-live-cutover`,
  `verify-vdp1-frame-bank`, and `verify-vdp1-transfer-pipeline`: PASS.
- repository-venv `test_capture_sourceboot_throughput.py`: 31/31 PASS.
- `verify-render-job-terrain-route` and the migrated cluster-generation case:
  PASS. The optional full dual-actor executable compiles but reaches its
  pre-existing worker-context failure before the migrated renderer assertion;
  it is not counted as a required green gate.

Pinned prior art was re-inspected at the recorded commits before production
work. Reuse mode remains dependency/API use for Yaul and project-owned
pattern-only lifetime code for SlaveDriver, Sonic Z-Treme, and Jo Engine;
sm64-psx remains behavior-study only. No new upstream source was copied.

RED checkpoint is `ec81ddc6`; scoped implementation is `0f5ccd65`. Independent
specification/quality review,
the serialized target build, exact-identity cadence capture, manual Ymir, broad
verify, and native-math census remain unchecked.

## Task 9A Fix Round 1 — independent-review remediation (2026-08-05)

**Status: fixes implemented and focused-host-green; fresh rereview required.**
Independent review of `0350a473..d45c0a41` returned specification/code-quality
FAIL and target-build NO-GO. No target, Ymir, broad build, or native-math gate
was run during this correction. Fix Round 1 implementation is `24528bf6`.

- C1 is repaired by renderer-owned `saturn_lod_lifetime`: a source scene
  transition observed while generation N is active advances only pending
  master state. Worker tier/cluster storage remains unchanged until N reaches
  a terminal lifecycle, when the deferred reset is applied for N+1.
- I1 is repaired by scene-neutral `saturn_render_overlap_phase`. Sourceboot
  begins construction before snapshot acquisition, binds exact snapshot/bank
  identities, receives timestamps after actual notify and positive retirement,
  and terminates after complete renderer construction. Construction is start
  preparation plus finalization; master finalization remains its explicit
  subset, and host additive attribution now consumes complete construction.
- I2 is repaired by a wait-free terminal telemetry refresh before the queue is
  snapshotted/reset. A failed admit with a quarantined dependent publishes
  `QF=1, QQ=1` in the executable integration path.
- I3 is repaired by `render_overlap_integration_test.c`, which links the real
  job runtime/graph/queue, lifecycle, scheduler, LOD lifetime, and phase
  controller. It holds N pending across an N+1 scene transition, proves N uses
  its retained FAR tier, applies the reset only after retirement, completes
  exact generation promotion, accounts start/slave/finalize boundaries, and
  exercises failure quarantine. Four compiled mutations are rejected.
- M1 is resolved by narrowing compatibility language: the decoder accepts
  explicit direct/saved v1 60-byte buffers, while live target observation
  requires the current v2 symbol and 76-byte payload.

Watched RED:

- `mingw32-make -f Makefile.saturn.mk verify-render-overlap-integration`
  failed at the absent shared production controllers.
- `.venv-saturn-tools\\Scripts\\python.exe tools/saturn/test_capture_sourceboot_throughput.py`
  failed because v2 attribution returned 4 rather than the required complete 5
  crossings.

Focused GREEN:

- `mingw32-make -f Makefile.saturn.mk verify-render-overlap-integration`:
  nominal PASS; all four mutations caught.
- `mingw32-make -f Makefile.saturn.mk verify-demo-render-overlap`: nominal
  lifecycle PASS; prior early-finalize/double-lower/replay mutations and the
  new production integration mutations are caught.
- `.venv-saturn-tools\\Scripts\\python.exe tools/saturn/test_capture_sourceboot_throughput.py`:
  32/32 PASS.
- `test_a9_sourceboot_cadence_trace_contract.py`: 4/4 PASS;
  `test_a9_frame_pipeline_integration_contract.py`: 9/9 PASS.
- The direct render-job runtime fixture and source contract PASS. The combined
  optional graph gate still fails its pre-existing obsolete assertion that the
  already-landed production renderer must not include
  `saturn_render_job_graph.h`; it is not reported green.

Reference use is unchanged: SlaveDriver
`a8986591557b6e680550d3c23970284d3b38ff8f` (GPL-3.0-or-later), Sonic Z-Treme
`cff75451c1616aac1236fc2b44223902b55c706b` (GPL-3.0), Yaul
`6012f79f237773378c8014e70d8998ad95a38d98` (MIT), Jo Engine
`556d081146211b6a1cfa6591d70f9487d406758b` (MIT/BSD-style file notices), and
sm64-psx `3073845688ea273da78d539b20c45110d8a868c3` (no repository-wide
license). Reuse mode remains dependency/API use or pattern-only; no upstream
source was copied or close-ported. Fresh independent specification and quality
rereview remain mandatory before the serialized target-build gate can open.

## Task 9A Fix Round 2 — target-coherency and exact-marker remediation (2026-08-05)

**Status: focused host/source GREEN; fresh rereview required.** Review of
`d45c0a41..420b6ce8` remained FAIL/NO-GO on C1, I1, and I3. Fix Round 2 moves
the complete worker-visible LOD lifetime into the target-coherent partition,
stamps the true release sites, and strengthens the integrated failure case. It
does not mark Step 10, target evidence, Ymir, broad verify, or native math
complete.

Implementation and governing-doc commit: `162f2a7d`.

Watched RED:

- `.venv-saturn-tools\Scripts\python.exe tools/saturn/test_a9_overlap_target_coherency.py`
  failed 3/3: primitive tiers, cluster LOD state, and lifetime were cached;
  sourceboot's phase clock/record were cached and registered the lifecycle
  observer; runtime release-marker helpers did not exist.
- The direct C11/Werror integration compile failed on the absent
  `sm64_saturn_render_job_runtime_marker_t` and
  `sm64_saturn_render_job_runtime_observe_markers()` API. The aggregate target
  stopped at the earlier source-contract RED, so this compile was run directly
  with the same sources and flags.

Implementation and design correction:

- `s_primitive_lod_tier`, `s_render_cluster_lod`, and `s_lod_lifetime` now use
  `DEMO_CROSS_CPU_SHARED`, which the source/layout gate traces through
  `.uncached` to the sourceboot linker's P2 `0x20000000 | ___bss_end` mapping.
  Sourceboot's VBlank clock, phase record, and phase acceptance flag are also
  `__uncached` because the slave retirement hook writes them.
- Runtime marker registration stores the observer and target clock inside the
  already-uncached runtime owner. Notification captures its stamp at the
  notify release and publishes the phase record before MMIO wake. Retirement
  captures at the positive release site and publishes the phase record before
  the retirement sequence becomes visible. Compiled late-notify and late-
  retire mutations invert those boundaries and are rejected.
- The integration fixture rejects an N+1 LOD selection while N is active,
  combines an active deferred scene transition with terminal quarantine,
  proves no reset before `lod_lifetime_finish`, then proves reset plus
  `QF=1, QQ=1`. An ignored-generation mutation is rejected.

Focused GREEN:

- `mingw32-make -f Makefile.saturn.mk verify-render-overlap-integration`:
  production integration PASS; 3/3 target-coherency source assertions PASS;
  six mutations caught (active reset, omitted start, late notify, late retire,
  ignored generation, skipped quarantine refresh).
- `mingw32-make -f Makefile.saturn.mk verify-render-job-runtime verify-demo-render-overlap`:
  runtime C fixture PASS, runtime source contract 5/5 PASS, production
  integration and all six mutations PASS, lifecycle fixture PASS, and its
  early-finalize/double-lower/replay mutations are caught.
- `git diff --check` remains a required pre-commit gate and is recorded with
  the implementation commit below.

Prior-art record is unchanged: SlaveDriver
`a8986591557b6e680550d3c23970284d3b38ff8f` (GPL-3.0-or-later), Sonic Z-Treme
`cff75451c1616aac1236fc2b44223902b55c706b` (GPL-3.0), Yaul
`6012f79f237773378c8014e70d8998ad95a38d98` (MIT), Jo Engine
`556d081146211b6a1cfa6591d70f9487d406758b` (MIT/BSD-style file notices), and
sm64-psx `3073845688ea273da78d539b20c45110d8a868c3` (no repository-wide
license). Reuse remains dependency/API use or pattern-only; no upstream source
was copied or closely ported.

Scoped rereview of `420b6ce8..050aa3bc` is specification PASS and code-quality
PASS with no Critical, Important, or Minor findings. It verifies C1 target-
coherent LOD ownership, I1 exact release-marker timing, I3 production-path
coverage, and unchanged bank/A8/A9/VDP1/VDP2 ownership. Step 10 is complete and
the reviewer authorizes exactly one serialized Step 11 build/capture. Actual
ELF/map P2 placement, memory margins, manual Ymir acceptance, broad verify, and
native-math publication census remain unchecked.

## Task 9A Step 11 first target attempt (2026-08-05)

The sole DLL-safe forced `-B -j1` build exits zero in 334.1 seconds. Exact
artifacts are ELF `5afbc752...3065f0` (8,745,796 bytes), ISO
`1d5f55f2...ab5411` (4,679,680 bytes), and CUE
`cdbf0bfa...f46dba7` (88 bytes). The first bounded capture fails closed at
symbol resolution before Ymir starts: reviewed runtime-marker fields increase
`s_runtime` from 92 to 104 bytes, but the observer still requires 92. The
failed JSON report path is
`a9a-step11-overlap-throughput-2026-08-05.json`. Git history retains that
symbol-resolution result; the working-tree path now records the later
authorized unchanged-target retry. No FPS, runtime phase, linked P2 placement,
or memory-margin claim is credited. Repair and independently review the
observer contract against this exact ELF; do not rebuild the target.

### Step 11 capture observer Fix Round 3

Root cause is an ABI-aware observer defect, not a target runtime failure. The
reviewed marker repair added three 32-bit function/context pointers before
`active`: exact `s_runtime` therefore evolved from 92 bytes/telemetry offset 28
to 104 bytes/telemetry offset 40. Accepting size 104 while retaining the old
offsets would silently misdecode active/runtime words as telemetry, so the
repair uses two explicit source-validated layouts rather than a minimum-size or
arbitrary-size rule.

Watched RED: repository-venv
`test_capture_sourceboot_throughput.py` ran 35 tests and failed three exact new
cases: the layout table was absent, 104-byte decode raised `runtime telemetry
has wrong size`, and ELF resolution raised `s_runtime has wrong size 104,
expected 92`. GREEN: the same suite passes 35/35. It proves legacy 92/28 and
marker 104/40 decoding, requires the reviewed source field order, accepts the
exact two symbol sizes, and rejects 88/96/100/108. A read-only symbol check
against ELF SHA-256
`5afbc7527bf470e9c9b099d5874f13030f4a4406dc93d9a751b057584c3065f0`
resolves `s_runtime` as 104 bytes and selects telemetry offset 40.

Git history retains the first attempt's `symbol-resolution`, `ready=false`, no-
notification, pre-Ymir disposition. The canonical path was subsequently
updated by the authorized unchanged-target retry below. The initial attempt
provides no FPS, phase, P2 placement, or margin evidence. Scoped review is
specification PASS and code-quality PASS;
the 92/28 and 104/40 ABI map, exact live read, fail-closed unknown sizes,
behavioral tests, and retained artifact hashes verify. Retry is authorized only
against unchanged ELF `5afbc752...3065f0` / CUE `cdbf0bfa...f46dba7`, with no
rebuild. One non-blocking stale STATE sentence is corrected in the next docs
transition. No target rebuild, successful capture, broad verify, or native-
math census occurred. Scoped repair/docs/evidence commit: `39b99c21`.

## Task 9A Fix Round 4 — HWRAM boot-boundary repair (2026-08-05)

**Status: source/layout GREEN; independently approved for one serialized
repaired rebuild.** The Fix
Round 3 observer was retried only against the unchanged reviewed artifacts.
The canonical report failed target identity after 600 one-VBlank startup
attempts; a diagnostic extension failed the same stage after 4,096 attempts.
Both logs authenticate the disc and load `A.BIN`, then stop before target
identity. Exact failed reports:

- `docs/saturn/evidence/reports/a9a-step11-overlap-throughput-2026-08-05.json`
- `docs/saturn/evidence/reports/a9a-step11-overlap-throughput-startup4096-2026-08-05.json`

Both bind ELF SHA-256
`5afbc7527bf470e9c9b099d5874f13030f4a4406dc93d9a751b057584c3065f0`,
ISO `1d5f55f2...ab5411`, and CUE `cdbf0bfa...f46dba7`. They are failed boot/
identity evidence, not FPS, phase, P2-placement, or runtime acceptance.

### Root cause and map evidence

Read-only inspection of that exact ELF reports:

- `___bss_end=0x060FD7D0`;
- P2 `.uncached=0x260FD7D0+0x6900`;
- `___end=0x061040D0`, which is `0x40D0` bytes beyond HWRAM top
  `0x06100000`;
- `s_render_cluster_lod` accounts for about `0x5ED4` bytes and the primitive
  tier array for another `0x364` bytes of the bulk LOD allocation.

Fix Round 2 had placed both bulk arrays in `.uncached`. The linker asserted
`0x06100000 - ___end >= 0x1B00` without first proving `___end` was in HWRAM;
unsigned subtraction wrapped and accepted an invalid image. The hardened
Python verifier now stops this exact ELF with `ELF end is past HWRAM top`.
This is a root-cause inference for the observed non-boot until a repaired image
boots; it is not retrospective target proof.

### Watched RED and implementation

The new contracts were observed RED before production edits:

- `test_a9_overlap_target_coherency.py`: 3 failures / 2 passes. It found no
  combined LWRAM LOD owner, no single P2 accessor shared by both CPU paths, and
  no ordered linker upper-bound assertions.
- `test_verify_sourceboot_memory_map.py`: 3 failures / 10 passes. The verifier
  accepted route-0 LWRAM with only `0x3000` free, accepted the exact HWRAM
  overflow via only a generic margin failure, and accepted a `.uncached` end
  inconsistent with `___end`.

GREEN changes:

- `demo_lod_storage_t` combines primitive tiers and cluster LOD state in one
  `.lwram_bss` object. `demo_lod_storage_cache_through()` is the only storage
  accessor and returns the same P2 alias for master admission/lifetime setup
  and whichever CPU claims WORLD_LOWER. No cached P1 access or purge protocol
  remains. The small `s_lod_lifetime` publication record stays `.uncached`.
- `sourceboot-cart.x` rejects HWRAM and LWRAM physical-top overflow before
  subtracting the `0x1B00` and `0x4000` floors. The final LWRAM floor applies
  to route 0 as well as the optional SCC1 route.
- `verify_sourceboot_memory_map.py` checks HWRAM overflow first, requires P2
  `.uncached` NOBITS to terminate physically at `___end`, validates LWRAM bulk
  bounds, and reports/enforces the final LWRAM margin.
- The real production-linked integration fixture passes one shared LOD storage
  pointer through master and worker paths while retaining exact-generation,
  deferred-scene, terminal-quarantine, and nonzero-`QQ` coverage.

Focused GREEN actually run, serially:

- `.venv-saturn-tools\Scripts\python.exe tools\saturn\test_a9_overlap_target_coherency.py`:
  5/5 PASS.
- `.venv-saturn-tools\Scripts\python.exe tools\saturn\test_verify_sourceboot_memory_map.py`:
  13/13 PASS.
- `mingw32-make -f Makefile.saturn.mk verify-render-overlap-integration`:
  production integration PASS; all six active-reset, omitted-start,
  late-notify, late-retire, ignored-generation, and skipped-quarantine-refresh
  mutations rejected.
- The changed cluster-storage test passes. Running its full seven-test file
  also exposed one unrelated pre-existing assertion for the absent
  `sm64_mario_render_cluster_lod_vertex_offsets` reference-stream symbol; no
  unrelated production or verifier file was changed for that failure.

No target build, Ymir launch, capture, broad verify, or native-math census was
run. Based only on the old map and exact symbol sizes, the projected repaired
end is about `0x060FDE98` (roughly `0x2168` HWRAM free) and projected LWRAM
free is about `0x74E0`. The HWRAM estimate is only `0x668` above the required
floor, so both values must be replaced by reviewed rebuilt-ELF/map evidence.
Canonical P2 cluster access may also affect performance and must be remeasured.

### Authorized repaired rebuild and verifier stop

The one authorized forced `-j1` rebuild passed in 331.4 seconds without a DLL
failure. Exact hashes are ELF `1905ec8d...fc2e2`, ISO
`1ccaef4f...cfaf96`, and CUE `cdbf0bfa...f46dba7`. The rebuilt map reports
`___bss_end=0x060FD810`, P2 `.uncached=0x260FD810+0x6C8`,
`___end=0x060FDED8`, HWRAM margin `0x2128`, `.lwram_bss` ending at
`0x002F8B10`, and LWRAM margin `0x74F0`. Thus both physical WRAM floors and the
P2-to-physical end identity are satisfied.

The hardened verifier nevertheless stopped before capture because it requires
`.uncached` to be `NOBITS`; the exact ELF emits `PROGBITS`. The map shows why:
the output section includes Yaul's `.uncached.function` cache-helper code in
addition to project shared data. This is a verifier-contract mismatch, not an
accepted target result. No Ymir process or capture was started. A watched
fail-closed correction and fresh independent review are required before this
same hash-bound artifact may proceed to boot/identity capture.

Prior-art record is unchanged: pinned SlaveDriver, Z-Treme, Yaul, Jo Engine,
and sm64-psx sources retain their recorded dependency/API or pattern-only reuse
modes. This repair applies existing project `.lwram_bss`, dual-frame cache-
through, and canonical-P2 snapshot patterns; no upstream source was copied or
closely ported. Scoped implementation/tests/docs/evidence commit: `d8dfe35f`.
Fresh specification and code-quality review are the next gate; only a GO may
authorize one serialized repaired target build.
