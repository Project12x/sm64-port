# Task 2 / A2 — immutable render snapshot banks

Status: source-complete; runtime-contract closure, target evidence, and two
independent reviews remain open.

Behavior/docs commit: `56866662` (`feat(saturn): add immutable render snapshot
banks`).

## Scope and design

- Added the Saturn-only `sm64_saturn_render_snapshot_bank_t`: two slots with
  `FREE`, `WRITING`, `READY`, `RENDERING`, `COMPLETE`, and `QUARANTINED`
  release states.
- A published snapshot carries only fixed-width scene/area identifiers, Q16
  camera values, a copied scalar Mario snapshot, a scalar pose selector, and
  immutable generated vertex/material-bank IDs. It contains no live SM64,
  graph-node, VDP1 backend, or VRAM pointer.
- The master captures the record only after `game_loop_one_iteration()` has
  completed. `publish` validates snapshot/camera/actor generation equality and
  writes the fenced release state last. Invalid transitions leave a slot
  unchanged; quarantine is terminal and cannot be reused.
- `sourceboot/main.c` retains sole ownership of live globals and presentation.
  No slave-facing type includes a game or VDP pointer.

## Provenance

- SlaveDriver Engine, `Lobotomy-Software/SlaveDriver-Engine`, pinned
  `a8986591557b6e680550d3c23970284d3b38ff8f`, GPL-3.0-or-later; inspected
  `WALLS.C:1240-1280` cache-through result staging.
- Sonic Z-Treme, `Maxime-XL2/SONIC-Z-TREME`, pinned
  `cff75451c1616aac1236fc2b44223902b55c706b`, GPL-3.0; inspected
  `Projects/SONIC Z-TREME/ZTE/ZT_RENDERING.c:439-480` LOD/admission flow.
- Reuse mode is pattern study/close-port only; no upstream source was copied.

## TDD and verification evidence

1. Red: `render_snapshot_bank_test.c` was added before the production header.
   The requested wrapper could not reach the compiler because it translates the
   worktree to `\\d\\Code...`, and Windows Python fails creating that path. A
   direct host compile then failed on the absent `saturn_render_snapshot.h`,
   the expected missing API/header condition.
2. Green: `C:\\msys64\\usr\\bin\\make.exe -f Makefile.saturn.mk
   SATURN_REPO_ROOT=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge
   HOST_CC=C:/Qt/Tools/mingw1310_64/bin/gcc.exe verify-render-snapshot-bank`
   passed. The test compiles `-std=c11 -Wall -Wextra -Werror` and validates all
   required lifecycle/mixed-generation/terminal-quarantine cases. The source
   pointer-field check also passed.
3. Green: the same overridden host environment ran
   `verify-dual-frame-bank`; its executable, uncached-release source gate, and
   five-invalid-handoff mutation gate passed.
4. Open/failing unrelated gate: `verify-runtime-contracts` compiled but failed
   at the preserved terrain-command `memcmp` assertion in
   `runtime_contract_test.c:4018`. It was not changed or suppressed.

## Remaining gates

- Resolve or classify the runtime-contract failure and re-run it green.
- Obtain independent specification and code-quality reviews.
- Run the later authorized target build/evidence gate; no target build or Ymir
  run was performed for A2.

## Specification-review fix round 1

The review correctly found three gaps. First, public reset cleared a terminal
quarantine; the new test creates a quarantined slot plus a writing companion,
calls reset, and proves no third allocation is available. Reset now clears only
already-free slots. Second, `volatile` and a compiler fence did not select the
SH-2 uncached address: release words and peer payload now use explicit P2
`CPU_CACHE_THROUGH` helpers, while host aliases remain identity for the real
state-machine fixture. The source test rejects both an absent helper and direct
release-field accesses. Third, the exported
`sm64_saturn_render_snapshot_generation_valid(snapshot, generation)` now owns
the nonzero/expected/camera/actor coherence rule and is called by publish and
acquire as well as directly by the fixture.

TDD red evidence was observed in order: reset-reuse assertion failure,
cache-through structural assertion failure, then C compilation failure for the
missing generation-validator declaration. The focused gate is green after the
repair. Runtime contracts still fail at the preserved terrain-command
comparison, and target/Ymir plus fresh reviews remain open.
