# Task 1 implementation report

## Result

Implementation commit: `4a8fe1ce` (`perf(saturn): bypass duplicate source
scene construction`), corrected by `658d5ad9` (`fix(saturn): keep source
render guard portable`) and `9904097e` (`fix(saturn): keep render guard
N64-compatible`), with scope correction `a00cdd17` (`chore(saturn): limit
Task 1 gates to Saturn`). Task status is **active**, pending independent
review and the controller-owned CUE/Ymir gate.

## Files changed

`Makefile.saturn.mk`; `src/game/area.c`; source runtime header/source;
sourceboot main; Fast3D profile header/decoder; runtime contract test;
source-policy test; profile decoder fixture; Task 1 plan/spec/SDD ledger and
evidence report; `docs/saturn/ENGINE_PORT_ARCHITECTURE.md`.

## Tests

- Red: `verify-source-render-policy` failed as expected before implementation
  because the scene-graph suppression getter was absent.
- Green: source-policy test PASS (1); exact compiled runtime-contract test
  PASS; focused Fast3D profile decoder class PASS (13, 1 skip).
- Aggregate: `python tools/saturn/test_tools.py` ran 193 tests in 204.286 s;
  17 unrelated preserved route-schema errors remain. The wrapper Make target
  compiles but has an MSYS/Windows executable-path handoff failure; direct
  constituent compiler and executable commands pass.
- Owner scope correction: PC/N64 builds are unsupported and are not Task 1
  gates. The N64 syntax target is removed. “Ordinary interpreted build” means
  the Saturn interpreted renderer. Source-policy PASS (1), runtime-contract
  executable PASS, and Fast3D profile tests PASS (13, 1 skip).

## Self-review

Reviewed the committed range for policy scope and ABI stability. The guard
evaluates once; the source-only construction is guarded; the required
stateful calls and pointer cleanup stay live; both counter additions are
suffixes. The project-native `s32`/`FALSE` local remains, but non-Saturn
compatibility is not claimed or tested. No task-owned review finding remains.

## Documentation and remaining gate

Updated the active plan, architecture decision ledger, SDD ledger, and evidence
report. Independent spec review, quality review, and the controller-owned
experimental CUE/Ymir gate remain; no target build or Ymir launch was run.
