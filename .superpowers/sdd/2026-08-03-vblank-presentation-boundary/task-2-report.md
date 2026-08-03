# Task 2 report — bootstrap VDP2 retirement

Date: 2026-08-03  
Status: **source-complete; independent review and target evidence pending**

## Scope and hypothesis

This is a single-hypothesis remediation for the first Emergency A9.0 CUE's
post-BIOS exit/freeze. The source-level hypothesis is that VDP2 work queued by
`user_init()` must retire through the predecessor's bootstrap
begin/commit/`vdp2_sync_wait()` barrier before the scheduler reaches its first
combined VDP1/VDP2 presentation. No target build, CUE construction, or Ymir
launch was authorized or performed in this task.

## Reference inspection and reuse record

- Investigation: `.superpowers/sdd/2026-08-03-vblank-presentation-boundary/crash-investigation.md`.
  It identifies the removed bootstrap barrier as the strongest new
  source-level correlation, but does not claim a proven root cause because no
  Ymir crash/PC/VDP state trace exists.
- Predecessor: `1a48bfb4576936185819635d93ac324021e3cd03`,
  `src/port/saturn/sourceboot/main.c`. The exact inspected sequence was after
  `dbgio_flush()` and before `sm64_saturn_fast3d_frontend_init()`:
  null-snapshot `sm64_saturn_vdp2_frame_begin()`,
  `sm64_saturn_vdp2_frame_commit()`, then `vdp2_sync_wait()`.
- Reuse mode: close-port of project-owned predecessor code only. No external
  source, dependency, or license obligation was introduced.

## TDD evidence

The focused mutation test was extended before the production edit. An initial
test-only run exposed that the pre-existing scheduler mutants were being
evaluated against the intentionally bootstrap-incomplete source; the test was
corrected to use a valid in-memory bootstrap fixture for those independent
mutants. No production source was changed during that test correction.

Expected RED command:

```powershell
& .\tools\saturn\with-msys-toolchain.ps1 C:\msys64\usr\bin\make.exe -f Makefile.saturn.mk verify-sourceboot-presentation-boundary
```

RED result: exit 1; `Ran 6 tests in 0.020s`; five tests passed. The one real
source failure was expected: `bootstrap must contain exactly one null-snapshot
VDP2 begin`.

Production change: commit `815c4352` restores only the predecessor bootstrap
VDP2 begin/commit/retirement wait immediately before frontend initialization.

Focused GREEN command: same as RED.

GREEN result: exit 0; `Ran 6 tests in 0.020s`; `OK`.

Additional check:

```powershell
git diff --check -- CHANGELOG.md src/port/saturn/sourceboot/main.c tools/saturn/test_sourceboot_presentation_boundary.py
```

Result: exit 0. (Only LF-to-CRLF checkout warnings were emitted.)

## Acceptance mapping

| Contract | Evidence |
| --- | --- |
| Exactly one null-snapshot bootstrap VDP2 begin/commit/wait before scheduler initialization | `bootstrap_vdp2_retirement()` counts all three calls and orders them before frontend and scheduler initialization. Absent, late, and duplicate in-memory mutants each fail. |
| Bootstrap has no VDP1 sync, source tick, geometry work, or generation publication | The protected bootstrap interval rejects VDP1 sync, `sourceboot_run_source_tick()`, `geo_process_root()`, presentation helper entry, and VDP1/profile generation-publication assignments. VDP1 and simulation insertion mutants fail. |
| Post-bootstrap displayed generations use only the paired terminal helper | Existing assertions retain exactly one `sourceboot_present_generation(scheduler_now)` call and require VDP1 sync plus VDP2 commit to remain inside that sole helper; the deliberate bootstrap commit is excluded from the terminal-boundary escape scan. |

## Commit and live-record updates

- Behavior commit: `815c4352` — `fix(saturn): restore bootstrap VDP2 retirement`.
  It contains `CHANGELOG.md`, the source change, and the focused mutation test.
- Updated: active Task 2 plan, master pipeline plan, architecture decision
  ledger, evidence report, and SDD progress ledger.

## Remaining gates

1. Independent specification review and independent quality review; neither
   verdict has been recorded.
2. One serial replacement CUE build, then a manual Ymir launch with the
   project `.ymir-profile`/32-Mbit DRAM cart. Record whether execution remains
   alive after BIOS before any speed conclusion.
3. The existing runtime-contract wrapper is still separately blocked by its
   known MSYS-to-Windows path `PermissionError` before compilation; Task 2 did
   not run or alter that gate.

The source test proves the sequencing contract only. It does not prove that
the barrier resolves the observed emulator freeze.
