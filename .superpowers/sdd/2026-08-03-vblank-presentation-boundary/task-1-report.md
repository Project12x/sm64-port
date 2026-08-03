# Emergency A9.0 Task 1 report — presentation boundary

## Status

**Implementation ready for independent review.** The sourceboot-wide cadence
correction is implemented and the focused source-mutation gate is green.
Specification review, quality review, a serial target CUE, and the project
profile/32-Mbit DRAM-cart Ymir result remain open; no target build or Ymir was
run in this task.

## Commit

- `perf(saturn): fence presentation to VBlank` — Task 1 changeset; final hash
  is assigned by this task's commit transition.

## TDD evidence

All shell/MSYS commands used the audited wrapper.

RED command:

```powershell
& .\tools\saturn\with-msys-toolchain.ps1 C:\msys64\usr\bin\make.exe -f Makefile.saturn.mk verify-sourceboot-presentation-boundary
```

RED result: exit 1; `Ran 4 tests in 0.002s`; `FAILED (failures=3)`. The real
pre-change source violated the first production assertion because it retained
`SOURCEBOOT_MAX_SIM_CATCHUP 4U`. The mutation cases then correctly remained
blocked by that same missing contract.

Correction RED command: the same command. Result: exit 1; `Ran 4 tests in
0.006s`; `FAILED (failures=2)`. The updated test caught the implementation's
incorrect attempt to count the retained fractional VBlank remainder as dropped
credit.

GREEN command: the same command. Result: exit 0; `Ran 4 tests in 0.009s`;
`OK`.

Direct relevant runtime-contract command:

```powershell
& .\tools\saturn\with-msys-toolchain.ps1 C:\msys64\usr\bin\make.exe -f Makefile.saturn.mk verify-runtime-contracts
```

Result: exit 1. Quad-map preparation emitted its three expected summaries, but
the wrapper gave Windows Python `\\d\\Code...`; `Path.mkdir()` failed with
`PermissionError: [WinError 5]` before the executable compiled. This is the
known wrapper-path gate and is not treated as a passing runtime contract.

Additional non-gate check actually run: the broad `tools/saturn/test_tools.py`
through the wrapper was stopped by the 60-second automation timeout while its
Bob mesh tests were still running. It has no pass/fail verdict and is not used
as evidence for this task.

## Acceptance mapping

1. `main()` samples `sourceboot_vblank_out_count` once at outer-loop entry and
   adds credit once. The mutation test rejects a tick-loop refill.
2. The cap is `SOURCEBOOT_MAX_SIM_CATCHUP 2U`; only whole post-recovery credit
   is discarded, accumulating `sourceboot_sim_vblank_credit_dropped`.
3. Equal observed/presented generations wait and `continue`, so the previous
   completed list is retained; only a fresh generation reaches construction or
   `sourceboot_present_generation()`.
4. `sourceboot_present_generation(generation)` is the only VDP1 sync-pair and
   VDP2 frame-commit location; it records the same presentation generation in
   VDP1/profile state. The VDP2 frame API remains composition-only.
5. The fence encloses the common sourceboot path, retains
   `sourceboot_run_source_tick()` and its normal geo walk, and adds no BOB
   branch.

## Files inspected

- Task requirements and ledgers: `task-1-brief.md`,
  `docs/superpowers/plans/2026-08-03-vblank-presentation-boundary.md`, and
  `.superpowers/sdd/2026-08-03-vblank-presentation-boundary/progress.md`.
- Destination source/tooling: `src/port/saturn/sourceboot/main.c`,
  `src/port/saturn/gfx/saturn_fast3d_frontend.h`,
  `src/port/saturn/gfx/saturn_vdp2_frame.{h,c}`, `Makefile.saturn.mk`,
  `tools/saturn/test_source_render_suppression.py`,
  `tools/saturn/fast3d_profile_decode.py`, and `tools/saturn/test_tools.py`.
- Pinned references, pattern-only/clean-room: SlaveDriver GPL-3.0-or-later
  `a8986591557b6e680550d3c23970284d3b38ff8f` — `V_BLANK.C`, `V_BLANK.H`,
  `DMA.C`, `DMA.H`; Sonic Z-Treme GPLv3
  `cff75451c1616aac1236fc2b44223902b55c706b` —
  `Projects/SONIC Z-TREME/ZTE/ZT_GAME.c`, `ZT_RENDERING.c`. No upstream code
  was copied. Existing Yaul `vdp1_sync_*`/VDP2 APIs at pinned MIT revision
  `6012f79f237773378c8014e70d8998ad95a38d98` were cadence-reused unchanged.

## Changed and staged files

- `src/port/saturn/sourceboot/main.c`
- `src/port/saturn/gfx/saturn_fast3d_frontend.h`
- `tools/saturn/test_sourceboot_presentation_boundary.py`
- `Makefile.saturn.mk`
- `tools/saturn/fast3d_profile_decode.py`
- `tools/saturn/test_tools.py`
- `CHANGELOG.md`
- `docs/saturn/evidence/reports/overlapped-render-pipeline-2026-08-03.md`
- `docs/superpowers/plans/2026-08-03-vblank-presentation-boundary.md`
- `docs/superpowers/plans/2026-08-03-saturn-overlapped-render-pipeline.md`
- `docs/superpowers/specs/2026-08-03-saturn-overlapped-render-pipeline-design.md`
- `.superpowers/sdd/2026-08-03-vblank-presentation-boundary/progress.md`
- `.superpowers/sdd/2026-08-03-vblank-presentation-boundary/task-1-report.md`

## Remaining gates

- Independent specification review and independent quality review; resolve all
  Important findings.
- Reproduce/fix the MSYS-wrapper host-path gate and then pass
  `verify-runtime-contracts`.
- Exactly one serial target CUE plus a project-profile Ymir manual run with the
  32-Mbit DRAM cart, recording VDP1/VDP2 counters, controls, BOB visibility,
  and qualitative speed. Emulator timing is not retail-hardware proof.
