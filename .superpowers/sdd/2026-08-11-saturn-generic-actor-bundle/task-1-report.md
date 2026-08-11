# Task 1 report — shared freestanding SHA-256

## Status

Source-complete at `1faa2ffb` (`refactor(saturn): share target SHA-256 validation`).
Scoped self-review passes; independent review is still required before Task 2.
This is host/source evidence only and makes no target, P2, Ymir, manual, reseal,
release, or S64F v3 behavior claim.

## Implementation

- Added `src/port/saturn/runtime/saturn_sha256.h/.c`, a freestanding
  incremental SHA-256 state/API with explicit null nonempty-input and total-byte
  overflow failures.
- Close-ported the in-tree S64P private implementation from
  `src/port/saturn/runtime/saturn_scene_package.c:6-118` and the S64F v2
  private implementation from `src/port/saturn/gfx/saturn_actor_bank.c:534-657`.
  Reuse mode: same-repository close-port/shared-core from base
  `0baac1a225d512f0f7eb95c36f2766fcef723c15`; no external source was used.
- S64P keeps segmented zero-digest hashing; S64F v2 keeps its exact header
  zeroing span. No payload copies or format changes were introduced.
- Added the source list correction in `src/port/saturn/sourceboot/Makefile` so
  the SH-2 source closure links the shared object with both validators.

## TDD evidence

RED command (with the required native forward-slash root override):

```powershell
powershell -ExecutionPolicy Bypass -File tools/saturn/with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 SATURN_REPO_ROOT=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge verify-saturn-sha256
```

RED output: `fatal error: saturn_sha256.h: No such file or directory` followed
by `fatal error: .../saturn_sha256.c: No such file or directory`; Make exited 1.

The initial GREEN run caught a transcription defect in two SHA round constants:
the empty-vector assertion failed. Comparing the close-port against the
reviewed in-tree implementation found the two constants; correcting only those
entries made the existing RED vectors green.

GREEN/regression command:

```powershell
powershell -ExecutionPolicy Bypass -File tools/saturn/with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 SATURN_REPO_ROOT=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge verify-saturn-sha256 verify-scene-package-runtime verify-actor-family-bank
```

Result: PASS. The primitive vectors (empty, `abc`, 64-byte, segmented,
null/nonzero, `UINT32_MAX` accounting) passed; the S64P fixture root hash is
pinned to `6e33c17546c04672b18f55e9edb6bdcfe287af8be5b31ca3cb2f6328b6d88297`;
the S64F v2 payload hash is pinned to
`00e5754c80762a15b5482fb1f2e88f4bc1fc7ab847f3463944e2e6689d412ee8`.
The actor report remained `47 families`, `13 unsupported representatives`, and
`14 records`.

## Link and review evidence

- Source-list assertion: PASS — `saturn_sha256.c` precedes both consumer
  sources inside Sourceboot `SH_SRCS`.
- A Yaul `make -n` attempt could not expand the target link because the existing
  generated `build/saturn/sourceboot/generated/saturn-source-closure-v2.json`
  prerequisite is absent. This is recorded as an open target gate, not a task
  failure or target claim.
- Scoped `git diff --check` passed for all Task 1 paths. Inspection confirmed
  no `sha256_state` or private `family_sha256_*` implementation remains in the
  two consumers; their digest paths call `sm64_saturn_sha256_*`.

## Changed files

`CHANGELOG.md`, `Makefile.saturn.mk`, `src/port/saturn/sourceboot/Makefile`,
`src/port/saturn/runtime/saturn_sha256.h/.c`,
`src/port/saturn/runtime/saturn_scene_package.c`,
`src/port/saturn/gfx/saturn_actor_bank.c`,
`tools/saturn/saturn_sha256_test.c`,
`tools/saturn/scene_package_runtime_test.c`,
`tools/saturn/actor_family_bank_test.c`, the implementation plan, and the Task
16 ledger.

## Open gates

Independent Task 1 review; Tasks 2-11; feature-off byte identity; target
sourceboot link/build and target/P2 evidence; Ymir/manual/visual evidence;
Task 9 reseal/repro/staging; and Task 10 smoke/release evidence.
