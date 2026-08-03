# Overlapped-render pipeline evidence — Task 1

## Source policy audit

Commit `4a8fe1ce` introduces an explicit scene-graph suppression policy for
Saturn demo ticks. `render_game()` reads the policy once, suppresses the
duplicate `geo_process_root()` traversal and source-only viewport/HUD/text/
scissor construction, while retaining `do_cutscene_handler()`,
`print_displaying_credits_entry()`, `render_menus_and_dialogs()`,
`render_screen_transition()`, warp delay/completion, and unconditional
`D_8032CE74`/`D_8032CE78` cleanup. Non-Saturn builds compile with suppression
fixed false. The sourceboot policy pair is scoped to one demo source tick and
is restored before the helper returns.

The new runtime counters are append-only in both the source-runtime state and
Fast3D profile. The profile decoder fixture asserts the two exact suffix
offsets and decodes hand-set big-endian values.

## Red evidence

Command:

```powershell
& tools/saturn/with-msys-toolchain.ps1 C:\msys64\usr\bin\make.exe -f Makefile.saturn.mk verify-source-render-policy
```

Result: expected failure before implementation. The source-policy test failed
because `render_game()` lacked
`sm64_saturn_source_runtime_scene_graph_suppressed`.

The red runtime-contract compilation was separately attempted through the
same wrapper; its pre-existing mixed MSYS/Windows path setup failed before
compilation could reach the missing API. This environmental failure was not
treated as a red API result.

## Green evidence

- `tools/saturn/test_source_render_suppression.py`: PASS (1 test).
- Exact host runtime-contract compilation using the commands emitted by
  `verify-runtime-contracts`, with `C:/msys64/mingw64/bin/gcc.exe`: PASS;
  `build/saturn/host-tests/runtime-contract-test.exe`: PASS.
- `python -m unittest tools.saturn.test_tools.Fast3dProfileDecodeTests`:
  PASS (13 tests, 1 expected skip).

The full wrapper invocation was attempted with the required explicit
`OS=Windows_NT`, `PYTHON`, `SATURN_TOOLS_PYTHON`, `HOST_CC`, and
`SATURN_REPO_ROOT` values. It compiles the runtime contract but exits nonzero
at the MSYS/Windows executable-path handoff. This is an aggregate environment
limitation, not a weakened test.

`python tools/saturn/test_tools.py` ran 193 tests in 204.286 seconds. It
failed with 17 Bob parity route-schema errors
(`required_probe_fields must match the SBR4 probe`) caused by preserved,
unrelated dirty route state. It initially also found two new profile-fixture
expectation errors; these were corrected and the focused profile class passes
above.

## Remaining gates

- Independent spec review, then independent quality review of `4a8fe1ce`.
- Controller-owned serial experimental CUE build and Ymir manual test with
  the established demo role. No target build, Ymir launch, or native-math
  census was performed here.
