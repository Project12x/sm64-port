# Task 1 implementation report

## Result

Implementation commit: `4a8fe1ce` (`perf(saturn): bypass duplicate source
scene construction`), corrected by `658d5ad9` (`fix(saturn): keep source
render guard portable`) and `9904097e` (`fix(saturn): keep render guard
N64-compatible`). Task status is **active**, pending independent review and
the controller-owned CUE/Ymir gate.

## Files changed

`Makefile.saturn.mk`; `src/game/area.c`; source runtime header/source;
sourceboot main; Fast3D profile header/decoder; runtime contract test;
source-policy test; profile decoder fixture; Task 1 plan/spec/SDD ledger and
evidence report.

## Tests

- Red: `verify-source-render-policy` failed as expected before implementation
  because the scene-graph suppression getter was absent.
- Green: source-policy test PASS (1); exact compiled runtime-contract test
  PASS; focused Fast3D profile decoder class PASS (13, 1 skip).
- Aggregate: `python tools/saturn/test_tools.py` ran 193 tests in 204.286 s;
  17 unrelated preserved route-schema errors remain. The wrapper Make target
  compiles but has an MSYS/Windows executable-path handoff failure; direct
  constituent compiler and executable commands pass.
- Spec-fix round 1 red: the wrapper-hosted non-Saturn `area.c` syntax command
  failed with unknown `bool` and undeclared `false`. Green: the same command
  passes after unconditional `<stdbool.h>`; source-policy test also PASS (1).
- Spec-fix round 2 red command:
  `with-msys-toolchain.ps1 ... gcc.exe -std=gnu90 -fsyntax-only -fsigned-char
  -nostdinc -DTARGET_N64 -D_LANGUAGE_C -DVERSION_US=1 -DNON_MATCHING=1
  -DAVOID_UB=1 -DF3DEX_GBI_2E=1 -Iinclude -Ibuild/us_pc
  -Ibuild/us_pc/include -Isrc -I. -Iinclude/libc src/game/area.c`; output:
  `fatal error: stdbool.h: No such file or directory`. Green commands:
  `with-msys-toolchain.ps1 ... make.exe -f Makefile.saturn.mk
  OS=Windows_NT HOST_CC=gcc SATURN_REPO_ROOT=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge
  verify-area-non-saturn-compile` and
  `.venv-saturn-tools\\Scripts\\python.exe tools\\saturn\\test_source_render_suppression.py`;
  both PASS (the syntax gate has expected host-width warnings only).

## Self-review

Reviewed the committed range for policy scope and ABI stability. The guard
evaluates once; the source-only construction is guarded; the required
stateful calls and pointer cleanup stay live; both counter additions are
suffixes. Spec review's non-Saturn type-definition finding is fixed by the
project-native `s32`/`FALSE` definition and N64-shaped real-translation-unit
syntax gate. No task-owned review finding remains.

## Documentation and remaining gate

Updated the active plan, architecture decision ledger, SDD ledger, and evidence
report. Independent spec review, quality review, and the controller-owned
experimental CUE/Ymir gate remain; no target build or Ymir launch was run.
