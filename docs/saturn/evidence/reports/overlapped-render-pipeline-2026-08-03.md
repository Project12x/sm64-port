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

## Historical (superseded) spec-fix round 1 — non-Saturn guard portability

Spec review found that `bool`/`false` in the non-`TARGET_SATURN` branch of
`area.c` had been supplied only indirectly by the Saturn-only runtime header.
Commit `658d5ad9` provides `<stdbool.h>` unconditionally and adds
`verify-area-non-saturn-compile`, a host `-fsyntax-only` gate for the real
translation unit.

Red command/result:

```powershell
& tools/saturn/with-msys-toolchain.ps1 C:\msys64\mingw64\bin\gcc.exe '-std=c11' '-fsyntax-only' '-DVERSION_US=1' '-DNON_MATCHING=1' '-DAVOID_UB=1' '-D_LANGUAGE_C=1' '-DF3DEX_GBI_2E=1' '-I.' '-Iinclude' '-Isrc' 'src\game\area.c'
```

Before the fix, GCC reported unknown type name `bool` and undeclared `false`
at `render_game()`'s non-Saturn branch. The exact command passes after the
fix. `tools/saturn/test_source_render_suppression.py` also passes (1 test).
This historical check is superseded by the Saturn-only owner scope and is not
an active Task 1 gate.

## Historical (superseded) spec-fix round 2 — N64 `-nostdinc` compatibility

The round-1 standard-header correction was not valid for the N64 build's
`-nostdinc` compiler model. Commit `9904097e` removes `<stdbool.h>` and makes
the guard a project-native `const s32`, initialized with `FALSE` in the
non-Saturn branch. The focused syntax gate now includes `-DTARGET_N64
-nostdinc` plus `include`, `build/us_pc`, `build/us_pc/include`, `src`,
repository root, and `include/libc`.

Red command/output:

```powershell
& tools/saturn/with-msys-toolchain.ps1 C:\msys64\mingw64\bin\gcc.exe '-std=gnu90' '-fsyntax-only' '-fsigned-char' '-nostdinc' '-DTARGET_N64' '-D_LANGUAGE_C' '-DVERSION_US=1' '-DNON_MATCHING=1' '-DAVOID_UB=1' '-DF3DEX_GBI_2E=1' '-Iinclude' '-Ibuild/us_pc' '-Ibuild/us_pc/include' '-Isrc' '-I.' '-Iinclude/libc' 'src\game\area.c'
```

Before the fix, output was `fatal error: stdbool.h: No such file or
directory` at `area.c:2`. Green command/result:

```powershell
& tools/saturn/with-msys-toolchain.ps1 C:\msys64\usr\bin\make.exe -f Makefile.saturn.mk OS=Windows_NT HOST_CC=gcc SATURN_REPO_ROOT=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge verify-area-non-saturn-compile
& .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_source_render_suppression.py
```

Result: historical PASS. This N64-shaped check is removed and no longer makes
any PC/N64 compatibility claim.

## Owner scope correction — Saturn-only Task 1

Commit `a00cdd17` removes `verify-area-non-saturn-compile` and its dependency
from the source-policy gate. The owner clarified that this repository is
Saturn-exclusive: PC and N64 builds are unsupported and not Task 1 gates.
“Ordinary interpreted build” means the Saturn interpreted renderer. The
project-native `s32`/`FALSE` local remains a clean Saturn implementation
detail, not a PC/N64 compatibility contract.

Focused Saturn-only results:

- `tools/saturn/test_source_render_suppression.py`: PASS (1 test).
- `build/saturn/host-tests/runtime-contract-test.exe`: PASS.
- `python -m unittest tools.saturn.test_tools.Fast3dProfileDecodeTests`:
  PASS (13 tests, 1 expected skip).

No target build, Ymir launch, or non-Saturn compilation was run for this scope
correction.
