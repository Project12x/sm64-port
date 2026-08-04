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
counter capture was run. The prior review's unrelated generation-wrap Minor,
fresh independent A3 rereview, and all target evidence gates remain open.

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
Remaining A3 gates: independent rereview, generation-wrap remediation, and
target visual/counter evidence.

Source sub-slice commit: `feat(saturn): admit compact terrain position spans
before transform`. Independent review verdict: not yet requested; this remains
an active A3 task.

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
