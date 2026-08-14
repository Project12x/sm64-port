# Dual-SH2 / Dual-VDP pipeline sprint — Task 12 preflight

**Status:** integration is not yet eligible for a target build or Ymir run.
This is a bounded preflight record, not performance evidence and not an
experimental-CUE publication.

## Scope and preserved state

- Worktree: `sm64-port/.worktrees/sh2-native-math-purge`.
- HEAD at preflight: `1850d18` (`test: cover appended VDP2 telemetry
  counters`).
- Existing unrelated audit/evidence dirt is preserved.  This report is the
  only Task 12 file created by this preflight.
- No MSYS/bash process, `sh-elf-*` program, target build, or Ymir process was
  launched during the inventory below.

## Required candidate and reference roles

Both roles must be built one at a time, with the same BOB route/input and the
same Q16 camera role.  The candidate role is exactly the plan command:

```powershell
make -C src/port/saturn/sourceboot -B -j1 `
  SATURN_DEMO_PATH=1 SATURN_SOURCEBOOT_ROUTE_REPLAY=1 `
  SATURN_SOURCEBOOT_LIVE_INPUT=1 `
  SATURN_SOURCEBOOT_LIVE_INPUT_BOOTSTRAP_TICKS=600 `
  SATURN_SOURCEBOOT_CAMERA_ROUTE=0 SATURN_CAMERA_VARIANT=3 `
  SATURN_SOURCE_CART_STAGE_SECTORS=8 SATURN_DEMO_HOT_PROMOTION=1 `
  SATURN_DEMO_NEAR_CLIP=1 SATURN_DEMO_BSP_ORDER=1 `
  SATURN_DEMO_POLY_TIER=2 SATURN_RENDERER_PIPELINE=3
```

The comparison reference must retain the same flags except for the explicitly
reference pipeline/LOD roles (`SATURN_DEMO_POLY_TIER=0` and the predecessor
pipeline role).  Do not overwrite its CUE: `sourceboot/Makefile` derives a
separate role-tagged output directory from the flags.  Each role must retain
its `sm64-saturn-sourceboot-e2.elf`, `.sym`, `.iso`, `.cue`, and `SOURCE.DAT`.
The CUE has a normal ISO prerequisite and the ISO has a normal `SOURCE.DAT`
prerequisite, so the identity set is SHA-256 of all five artifacts plus the
source commit and exact flags.

## Host suite inventory

The Task 12 host gate is the union of the per-task native fixtures plus the
existing generator, camera/Q16, terrain-template, memory-map, and coherency
checks.  The relevant Make targets are:

```text
verify-tools                         verify-visible-position-set
verify-terrain-depth-bins            verify-dual-frame-bank
verify-dma-queue                     verify-terrain-command-template
verify-hot-promotion                 verify-dual-actor-worker
verify-vdp2-frame                    verify-ir-transform
verify-terrain-clip                  verify-ztreme-frustum
verify-bob-bsp-header                verify-render-native-math
verify-render-native-math-mutation   verify-fast3d-q16-diff
verify-mtxf-lookat-host-diff         verify-mtxq-ctors
verify-mtxq-ctors-mutation           verify-graph-q16-contract
verify-mtxq-conversion-assembly
```

The decoder/layout selections in
`tools/saturn/test_tools.py` cover the appended profile counters.  The linked
candidate additionally needs `verify_sourceboot_memory_map.py`,
`verify_q16_sh2_disassembly.py`, `verify_sh2_native_math.py` with target
`objdump`/`readelf`/`addr2line`, and `verify_dual_cpu_coherency.py <candidate.sym>`.
The final two Make verification recipes already call the native-math and
coherency gates for demo replay images, but their results are not substitutes
for inspecting the candidate ELF and symbols.

### Executed host check and blocker

One direct native Windows attempt was made, without entering MSYS:

```powershell
mingw32-make -f Makefile.saturn.mk -j1 verify-tools ...
```

It failed before compiling or running any fixture:

```text
! was unexpected at this time.
mingw32-make: *** [Makefile.saturn.mk:171: check-host-tools] Error 255
```

Cause: Qt `mingw32-make.exe` selected `cmd.exe`, while the project host guard
at line 171 uses POSIX `[ ! -x ... ]`.  This is an invocation-environment
failure, not a failing host assertion.  The next host attempt must run the
same native make through the audited POSIX shell contract below; until then,
the complete Task 12 host gate is **not green**.

### Follow-up host execution (bounded, 2026-08-03)

The audited wrapper plus `C:\msys64\usr\bin\sh.exe` passes
`check-host-tools` only after this shell-local setup:

```sh
export PATH=/mingw64/bin:/usr/bin:$PATH
unset COMPILER_PATH AS AR RANLIB
```

Do **not** source `.yaul.env` for a host fixture.  That file deliberately
exports Yaul `COMPILER_PATH`; native GCC then sends x86-64 Windows assembly to
the SH assembler.  This was reproduced on `verify-visible-position-set` and
is an environment defect, not a source failure.  Conversely, under MSYS sh,
Qt `mingw32-make` misquotes the Makefile's quoted Windows `.exe` invocation
after a successful compile (`unexpected EOF while looking for matching
\`"\``).  It is likewise a make/shell invocation defect.

To keep the host results meaningful and bounded, the following fixtures were
compiled with `C:\Qt\Tools\mingw1310_64\bin\gcc.exe` and then executed,
each as a separate `with-msys-toolchain.ps1` launch and without `.yaul.env`:

| Fixture | Result |
| --- | --- |
| `visible_position_set_test.c` | PASS |
| `terrain_depth_bins_test.c` | PASS |
| `terrain_command_template_test.c` | PASS |
| `vdp2_frame_contract_test.c` | PASS |
| `dual_frame_bank_test.c` | PASS |
| `dual_actor_worker_test.c` | PASS (`dual actor worker fixture: PASS`) |
| `dma_queue_test.c` | PASS |
| `hot_promotion_test.c` | PASS (`hot promotion contract: PASS`) |
| `fast3d_q16_diff_test.c` | PASS |
| Fast3D profile layout/decode selection | PASS: 21 tests, 1 expected skip |

The first profile-decoder attempt produced one failure because the wrapper
made the test's auto-detected `gcc` select an MSYS compiler unable to launch
`cc1`; 20 tests passed and one skipped.  Re-running the exact selection with
`CC=C:\Qt\Tools\mingw1310_64\bin\gcc.exe` produced 21 passing tests and one
expected skip.  This is recorded as an invocation defect, not a profile ABI
failure.

`verify-tools` was started through the audited POSIX contract but produced no
output for more than 60 seconds, so it was terminated.  It has no pass/fail
result and blocks a claim that the entire host gate is green.  The remaining
focused runtime/camera/Q16 and source-generator checks also remain pending.

## DLL and launch-contract audit

The inherited desktop PATH does not contain `C:\msys64\usr\bin`; it contains
Qt MinGW and Git command directories.  Direct `mingw32-make` therefore cannot
be treated as a POSIX/MSYS launch environment.

The following required DLLs are present:

```text
C:\msys64\usr\bin\msys-2.0.dll
C:\msys64\usr\bin\msys-gcc_s-seh-1.dll
C:\msys64\mingw64\bin\libgmp-10.dll
C:\msys64\mingw64\bin\libmpfr-6.dll
C:\msys64\mingw64\bin\libisl-23.dll
```

The target programs also exist at the Yaul location encoded by the parent
worktree's ignored `.yaul.env`:

```text
D:\Code\RetroDev\sm64-saturn-port\work\yaul-install\bin\sh-elf-gcc.exe
D:\Code\RetroDev\sm64-saturn-port\work\yaul-install\bin\sh-elf-readelf.exe
D:\Code\RetroDev\sm64-saturn-port\work\yaul-install\bin\sh-elf-objdump.exe
D:\Code\RetroDev\sm64-saturn-port\work\yaul-install\bin\sh-elf-addr2line.exe
D:\Code\RetroDev\sm64-saturn-port\work\yaul-install\bin\sh-elf-nm.exe
```

**Required safe contract for every later MSYS, target, ELF, and build
command:** invoke it from PowerShell through
`tools/saturn/with-msys-toolchain.ps1`.  The wrapper fails closed if any of
the five DLLs is missing and prepends both `C:\msys64\mingw64\bin` and
`C:\msys64\usr\bin` before resolving the requested executable.  The wrapped
POSIX shell must source `../../.yaul.env` when its working directory is this
worktree, then run exactly one serial build or verifier.  Never invoke
`sh-elf-*`, `bash`, or `make` from the inherited PATH.

For a **target-only** command, and only after the host gate is complete,
source `../../.yaul.env` inside the wrapped MSYS shell *after* setting the
POSIX path above.  That target-only scope is essential: do not carry its
`COMPILER_PATH`, `AS`, `AR`, or `RANLIB` into a native host compiler process.

## ELF acceptance inventory

For the fresh candidate only, prove all of the following against its own ELF
and `.sym`:

- no camera/transform soft-float helper or generic 64-bit division call in
  the accepted render frame path;
- no accepted-frame `cpu_cache_purge` call;
- both master and slave worker symbols are present and the VDP1 and VDP2
  commit paths are reachable;
- uncached cross-CPU publication records satisfy
  `verify_dual_cpu_coherency.py`;
- sourceboot memory map, cart-stage size, entry point, cart section, and
  `SOURCE.DAT <= 4 MiB` pass their existing verification scripts.

`fast3d_profile_decode.py` must decode the captured renderer profile at the
fresh linked `_sourceboot_fast3d` address; it is deliberately not the hwtest
`SAT0` decoder.

## Ymir and evidence acceptance inventory

Use the project USA BIOS at
`sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin` and DRAM cart
for both automated comparison and the final manual run.  Automated evidence
must bind a fresh candidate CUE/ISO/ELF/SOURCE.DAT hash set, resolve fresh
`_sourceboot_fast3d` and route-checkpoint addresses from that candidate, and
record a valid renderer-profile decode.

Reject: black or sky-only captures, missing/invalid probe addresses,
gameplay-state mismatch, route/fault/timeout, command-arena overflow, DMA
overflow, slave timeout, or nonzero pipeline fault counter.  Correctness
captures may tune only `slave_begin`, depth-bin count, and LOD thresholds;
they must establish bounded work on both SH-2s, stable visible order, and no
source-state regression.

The final manual DRAM-cart run must occur after the 600-tick bootstrap with
live input and demonstrate terrain, Mario, responsive controls, VDP2 sky/HUD,
and a subjective improvement over the predecessor.  The final report must
then add screenshots, counter table, source commits, upstream provenance,
exact flags, all SHA-256 values, visual tradeoffs, and rollback roles.
`EXPERIMENTAL_BUILD.txt` belongs beside the published CUE and must name the
dual-SH2 pipeline, VDP1 geometry, VDP2 sky/HUD, tier-2 LOD, 600-tick BOB
bootstrap then live input, DRAM-cart requirement, and intentionally skipped
static census.

## Candidate-flag preflight

The Makefile explicitly accepts camera variant `3`, route `0`, live input
with the `600` tick bootstrap, cart stage `8`, and terrain tier `2`; the
corresponding flags propagate to `SH_CFLAGS` and into the distinct output tag.
Hot promotion, near clipping, and BSP order are compiled conditionals in the
renderer.  `SATURN_RENDERER_PIPELINE=3` currently participates in the output
directory identity (`-pipe3`) only: it is not validated or passed as a C
preprocessor define.  This is acceptable only if the integrated source is the
pipeline implementation for every role; the fresh candidate ELF/Ymir proof
must demonstrate the active stages rather than treating `-pipe3` as proof.

## Next command, not yet authorized or executed

Only after the remaining bounded host checks are green, the first target build
must use the audited wrapper and scope the Yaul environment to that one MSYS
shell.  The exact candidate invocation is:

```powershell
$w = 'D:\Code\RetroDev\sm64-saturn-port\sm64-port\.worktrees\sh2-native-math-purge'
$cmd = @'
export PATH=/mingw64/bin:/usr/bin:$PATH
cd /d/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge
source ../../.yaul.env
export SHELL=/usr/bin/sh
make -C src/port/saturn/sourceboot -B -j1 SATURN_DEMO_PATH=1 SATURN_SOURCEBOOT_ROUTE_REPLAY=1 SATURN_SOURCEBOOT_LIVE_INPUT=1 SATURN_SOURCEBOOT_LIVE_INPUT_BOOTSTRAP_TICKS=600 SATURN_SOURCEBOOT_CAMERA_ROUTE=0 SATURN_CAMERA_VARIANT=3 SATURN_SOURCE_CART_STAGE_SECTORS=8 SATURN_DEMO_HOT_PROMOTION=1 SATURN_DEMO_NEAR_CLIP=1 SATURN_DEMO_BSP_ORDER=1 SATURN_DEMO_POLY_TIER=2 SATURN_RENDERER_PIPELINE=3
'@
& "$w\tools\saturn\with-msys-toolchain.ps1" 'C:\msys64\usr\bin\sh.exe' '--noprofile' '--norc' '-lc' $cmd
```

It is serial by construction and leaves the Yaul variables inside the child
shell.  It must not be run until this report is updated with the remaining
host-gate outcomes and the owner confirms readiness.

## Reference build attempt (2026-08-03)

The authorized serial reference attempt used the wrapper command above with
`SATURN_DEMO_POLY_TIER=0` and `SATURN_RENDERER_PIPELINE=2`.  It ran for about
161 seconds, generated the role-specific assets, compiled the source closure,
and linked the expected `-poly0-...-pipe2` ELF.  No DLL popup or target
compiler/runtime-load error occurred.  The linker issued its existing RWX
LOAD-segment warning.

The build then failed before ISO/CUE publication:

```text
mktemp: failed to create file via template '/tmp/XXXX': Permission denied
wrap-error: Error: Couldn't create file
make: *** [.../yaul-install/share/build.post.iso-cue.mk:41: .../obj/IP.BIN] Error 1
```

This is an MSYS temporary-directory permission failure, not a source compile
or link failure.  Do not build the candidate yet.  Rerun this same reference
only after setting a writable, worktree-contained MSYS `TMPDIR`/`TMP` inside
the audited child shell (for example
`/d/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge/.msys-home/tmp`), then verify that its ISO/CUE and artifact hashes exist before proceeding.

## Existing artifact boundary

`.tmp-experimental-cue/ymir-final-wave/final.json` is not Task 12 evidence:
it records a `pipe2`/`poly0` predecessor CUE and has no sourceboot probe
address.  Its telemetry explicitly says that the hwtest `SAT0` decoder was
pointed at ordinary sourceboot memory.  It therefore cannot substantiate a
candidate counter or performance claim.

## Bounded linked-ELF verifier design (Task 12)

The current acceptance path in `verify_sh2_native_math.py` obtains full
`objdump -d`, section/symbol tables, and decoded lines; it calls
`scan_call_graph()` to expand pinned `RouteOracle.roots`, then materializes
every instruction with `parse_instructions()` before `analyze_code_only()`.
This is the scalability failure on the candidate ELF.

The new opt-in `--analysis-mode=code-only-route-bounded` must retain default
`code-only` behavior.  It will: (1) build the direct call graph from the
disassembly text, (2) expand only the immutable route roots and required
indirect edges, (3) filter complete symbol-owned disassembly blocks to that
closure before `parse_instructions`, and (4) pass the same owners, decoded
profiles, null-transfer proofs, and indirect-edge audit to
`analyze_code_only`.  Missing roots, a function block without an owner, or an
unresolved/ambiguous edge are errors, never a reduced successful verdict.

Test matrix: a synthetic two-function disassembly must prove the bounded
filter retains the root and helper but excludes a large unrelated function;
an oracle root absent from the graph must raise; and an unresolved indirect
edge must remain rejected by the existing code-only analysis.  A small
fixture will compare bounded and full verdicts for the same route closure.

## Bounded BOB `bf` branch repair (2026-08-03)

Parser/test repair commits: `b29d9397` (`fix: parse SH-2 bf branches in bounded
routes`) and `6cfefc74` (`test: make bounded bf regression self-contained`).
The first independent exact-commit review correctly found that the test used a
controller-dirt-only module alias; `6cfefc74` changes it to the test module's
existing committed alias.  Exact-tip rereview verdict: **GO**.  An isolated
archive passed `py_compile`, focused branch tests 3/3, and the full committed
suite 214/214; the shared suite passed 219/219.  Runtime restoration of the old
greedy regex selected only `_root`, while the repaired parser selected `_root`
and `_child`.  The reviewed range is exactly two files, +30/-1, with a clean
`git diff --check`.  Review audit: `audit-20260803-1449.md`.  This documentation
remains controller-owned and unstaged in the shared worktree.

The exact candidate gate after independently approved commit `bf1e7c13`
completed in about 221.6 seconds and rejected the declared
`_geo_process_node_and_siblings -> _geo_camera_main` edge because its
dispatcher appeared unreachable.  Source and ELF evidence show that the edge
is applicable to the pinned route:

- BOB area 1 includes the camera callback in its GeoLayout-derived manifest.
- The candidate's direct path is `_game_loop_one_iteration ->
  _level_script_execute -> _render_game -> _geo_process_root ->
  _geo_process_node_and_siblings`.
- At `0x0600bcfc`, `_render_game` has nondelayed `bf 0x0600bd02`; its taken arm
  loads `_geo_process_root` at `0x0600bd0e` and calls it at `0x0600bd16`.

The bounded instruction regex accepted one through four apparent byte tokens.
Because `bf` is also hexadecimal text, it consumed the branch mnemonic as a
third byte, followed only the fallthrough `bra`, and omitted the camera graph
dispatcher from both the preparatory and analyzed closures.  The repair makes
the parser honor the SH-2 ISA's exact two-byte instruction width.  A bounded
unseeded-branch regression was observed red because its taken-arm child was
absent, then green after the repair.  The focused branch set passed 5/5 and the
complete verifier suite passed 219/219.  A real candidate provenance probe
then included `0x0600bcfc`, `0x0600bd0e`, and `0x0600bd16`.

The exact bounded candidate gate was rerun without rebuilding the target.  It
completed in 274.4 seconds, passed the repaired camera-dispatcher reachability
point, and stopped fail-closed at the next independent rejection:

```text
unconsumed INDIRECT_EDGE has no structurally dynamic unresolved transfer in
_geo_process_held_object: _geo_process_held_object ->
_geo_switch_mario_hand_grab_pos
```

Therefore the native-math candidate gate, target acceptance, automated Ymir,
and manual Ymir gates remain unchecked.  No target rebuild or Ymir run was
performed during this repair.
