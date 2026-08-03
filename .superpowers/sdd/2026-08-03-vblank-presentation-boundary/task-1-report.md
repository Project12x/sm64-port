# Emergency A9.0 Task 1 report — presentation boundary

## Status

**Implementation ready for independent review.** The sourceboot-wide cadence
correction is implemented and the focused source-mutation gate is green.
Specification review, quality review, a serial target CUE, and the project
profile/32-Mbit DRAM-cart Ymir result remain open; no target build or Ymir was
run in this task.

## Commit

- `950ab37a` — `perf(saturn): fence presentation to VBlank` (Task 1
  implementation and same-commit changelog entry).

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

## Quality fix round 1/5 — I1 resolved, M1/M2 addressed

The focused source checker now requires exactly one
`sourceboot_present_generation(scheduler_now)` invocation in `main()` and
verifies the stale-generation `wait_vblank()`/`continue` sequence occurs before
that terminal call. The new duplicate-helper in-memory mutant is rejected with
`fresh generation must make exactly one presentation attempt`; no production
scheduler behavior changed.

Mutation RED command:

```powershell
& .\tools\saturn\with-msys-toolchain.ps1 .\.venv-saturn-tools\Scripts\python.exe -c "import runpy; ns=runpy.run_path('tools/saturn/test_sourceboot_presentation_boundary.py'); source=ns['SOURCEBOOT_C'].read_text(encoding='utf-8'); call='    sourceboot_present_generation(scheduler_now);'; ns['assert_presentation_boundary'](source.replace(call, call+'\n'+call, 1))"
```

Result: exit 1, `AssertionError: fresh generation must make exactly one
presentation attempt`. This is the expected failing duplicate-dispatch mutant,
observed before any production change (none was needed).

Focused GREEN command:

```powershell
& .\tools\saturn\with-msys-toolchain.ps1 C:\msys64\usr\bin\make.exe -f Makefile.saturn.mk verify-sourceboot-presentation-boundary
```

Result: exit 0; `Ran 5 tests in 0.011s`; `OK`. The profile decoder subset also
passed: `python -m unittest tools.saturn.test_tools.Fast3dProfileDecodeTests`,
exit 0, `Ran 13 tests in 0.006s`, `OK (skipped=1)`.

M1 is documented in the evidence report: `vdp1_bank_displayed` deliberately
reports the previously submitted/retired list and is one submission behind the
fresh `vdp1_bank_submitted`/`vblank_presentation_generation` arm. M2 removes
the unused `PRESENTATION_BOUNDARY_COUNTERS` tuple; the decoder continues to
derive and expose both appended fields directly from the real profile header.

The unpassed runtime-wrapper gate, independent quality re-review, and serial
target/Ymir gates remain unchanged.
