# Task 1 implementation report

## Result

Implementation commit: `4a8fe1ce` (`perf(saturn): bypass duplicate source
scene construction`), corrected by `658d5ad9` (`fix(saturn): keep source
render guard portable`) and `9904097e` (`fix(saturn): keep render guard
N64-compatible`), with scope correction `a00cdd17` (`chore(saturn): limit
Task 1 gates to Saturn`). Task status is **BLOCKED**: full-range quality review
found that the proposed optimization suppresses authoritative state inside
`geo_process_root()`. The Critical finding remains unresolved as an
optimization.

Safety commit `77ee306c` (`fix(saturn): keep source geo traversal fail closed`)
is **GO for runtime safety**, with status/docs commit `19963fd3`. Sourceboot no
longer calls the reserved scene-graph suppression setter, so accepted Saturn
builds retain the full geo walk. This safety GO does not complete or approve
the optimization.

## Files changed

`Makefile.saturn.mk`; `src/game/area.c`; source runtime header/source;
sourceboot main; Fast3D profile header/decoder; runtime contract test;
source-policy test; profile decoder fixture; Task 1 plan/spec/SDD ledger and
evidence report; `docs/saturn/ENGINE_PORT_ARCHITECTURE.md`.

## Tests

- Red: `verify-source-render-policy` failed as expected before implementation
  because the scene-graph suppression getter was absent.
- Green before safety closure: source-policy test PASS (1); exact compiled runtime-contract test
  PASS; focused Fast3D profile decoder class PASS (13, 1 skip).
- Aggregate: `python tools/saturn/test_tools.py` ran 193 tests in 204.286 s;
  17 unrelated preserved route-schema errors remain. The wrapper Make target
  compiles but has an MSYS/Windows executable-path handoff failure; direct
  constituent compiler and executable commands pass.
- Owner scope correction: PC/N64 builds are unsupported and are not Task 1
  gates. The N64 syntax target is removed. “Ordinary interpreted build” means
  the Saturn interpreted renderer. Source-policy PASS (1), runtime-contract
  executable PASS, and Fast3D profile tests PASS (13, 1 skip).
- Fail-closed safety TDD: the new sourceboot activation test failed against the
  paired setter calls, then PASS (2 source-policy tests) after `77ee306c`.
  Runtime-contract executable PASS; profile decoder PASS (13, 1 skip).
- Quality safety fix round 2/5 is documentation-only. No code, test, target, or
  Ymir command was run; `git diff --check` is the only round-2 gate.

## Self-review

Full-range review invalidated the original whitelist assumption. The guard
evaluates once and the named calls remain live, but `geo_process_root()` itself
advances animation and invokes additional stateful callbacks. The Critical
task-owned optimization finding therefore remains open. The policy/counter ABI
is suffix-compatible and dormant; `77ee306c` closes the production exposure by
preventing sourceboot activation.

## Documentation and remaining gate

Updated the active plan, architecture decision ledger, SDD ledger, and evidence
report. Task 1 remains BLOCKED pending a behavior-tested state-only seam and
new full-range independent review. No optimization CUE or Ymir launch is
authorized while the Critical finding remains open. No target build or Ymir
launch was run.
