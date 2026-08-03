# Task 1D report

## Status

**ACTIVE — review-fix round 1; rereview required.** The sealed production
diagnostic remains unchanged from `98f26f26`. The first independent spec
review is **NO-GO** because the initial test used inert Makefile string checks,
inspected text before the diagnostic `#if` instead of its actual normal
`#else`, and omitted the central execution-evidence update. This round fixes
those two Important findings without changing runtime behavior.

## Files changed in review-fix round 1

- `tools/saturn/test_source_render_suppression.py`
- `docs/saturn/evidence/reports/overlapped-render-pipeline-2026-08-03.md`
- `docs/superpowers/plans/2026-08-03-saturn-overlapped-render-pipeline.md`
- `docs/superpowers/specs/2026-08-03-saturn-overlapped-render-pipeline-design.md`
- `.superpowers/sdd/2026-08-03-saturn-overlapped-render-pipeline/progress.md`
- `.superpowers/sdd/2026-08-03-saturn-overlapped-render-pipeline/task-1d-report.md`
- `CHANGELOG.md`

## TDD evidence

Review-fix RED command:

```powershell
& .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_source_render_suppression.py
```

RED result: exit 1; `Ran 4 tests in 0.016s`; `FAILED (errors=6)`. The two new
contracts failed because `sourceboot_make`, `make_value`, and
`extract_preprocessor_branches` did not yet exist. The errors were the intended
missing test infrastructure, before any helper implementation.

Review-fix GREEN command:

```powershell
& .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_source_render_suppression.py
```

GREEN result: exit 0; `Ran 4 tests in 8.173s`; `OK`. The gate now runs the real
sourceboot Makefile through GNU Make for default-off, the one accepted sealed
combination, a non-binary value, and each missing demo/replay prerequisite. It
also reads the evaluated compile flags and output directory, requiring exactly
one `diag-skip-geo` component only in the diagnostic output.

Final post-refactor verification used the same command: exit 0;
`Ran 4 tests in 4.499s`; `OK`. `git diff --check` over the seven Task 1D files
also exited 0; its only output was the existing LF-to-CRLF checkout warnings.

Runtime-contract command:

```powershell
& 'C:\Program Files\PowerShell\7\pwsh.exe' -File tools\saturn\with-msys-toolchain.ps1 make -f Makefile.saturn.mk verify-runtime-contracts
```

Result: exit 1 after generating the three quad-map summaries. The Windows
Python runtime received the MSYS path `\\d\\Code...\\build\\saturn\\host-tests`
and failed with `PermissionError: [WinError 5]`; Make exited at
`Makefile.saturn.mk:192`. This is the same host-wrapper path-translation
infrastructure failure already recorded for Task 1D, not a green runtime
contract and not evidence of a contract regression.

## Reviewed source ordering

The test now structurally extracts the matching
`#if SATURN_EXPERIMENTAL_SKIP_GEO_WALK`, its direct `#else`, and its matching
`#endif`, while accounting for nested preprocessor directives. The diagnostic
branch contains exactly two setter calls and one game-loop call in this order:
set true, call `game_loop_one_iteration()`, set false. The actual normal
`#else` contains exactly one game-loop call and no scene-graph setter.

## Commits and review

- `98f26f26`: sealed source diagnostic implementation.
- `f2b7ebf0`: initial Task 1D documentation transition.
- `fe1074b8`: review-fix round 1 test hardening and execution evidence.
- Independent specification review: **NO-GO** on the initial range, with two
  Important findings addressed by this round; independent rereview remains
  required.
- Independent quality review: not yet run.

## Remaining gates

- Obtain independent spec rereview and independent quality review.
- Repair or bypass the MSYS/Windows path-translation failure and obtain a
  genuinely green runtime-contract wrapper result.
- Build the one authorized diagnostic target and run it once in Ymir only
  after review authorizes that step; record hashes, visibility, controls, and
  the owner's qualitative upper-bound observation.
- Keep A1 blocked: the diagnostic knowingly invalidates animation, warp,
  camera, water, moving-texture, carpet, matrix, and other graph-owned state.

No target build or Ymir run occurred in implementation or review-fix round 1.
