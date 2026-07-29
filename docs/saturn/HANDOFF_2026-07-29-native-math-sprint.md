# Handoff — SH-2 Native Math Purge (2026-07-29)

## Controller rule — mandatory

This sprint is governed by `docs/superpowers/plans/2026-07-29-sh2-native-math-purge.md` and the ledger at `.superpowers/sdd/2026-07-29-sh2-native-math-purge/progress.md`.

The controller **must not send a final response** while any sprint task is pending, any review is outstanding, or a dispatched agent is active. Agent completion is not a user-facing stopping point. The mandatory loop is:

1. Receive agent completion.
2. Read its report and inspect its commit.
3. Dispatch the required scoped review.
4. Resolve review findings through the fix loop.
5. Update the ledger.
6. Dispatch the next task and wait.

Only the entire sprint completing, a genuine external decision, or an explicit user stop authorizes a final response.

## Current branch and checkpoint

- Worktree: `sm64-port/.worktrees/sh2-native-math-purge`
- Branch: `sh2/native-math-purge`
- Current commit: `f07fdd9` (`docs: record Task 2 post-conversion captures`)

## Completed, review-clean tasks

- **Task 0:** native-math census gate. Renderer route is immutable and graph-derived; spill/reload calls and the pinned worker callback edge are covered.
- **Task 1:** port-owned hot render math is Q16. Route visual/checkpoint evidence is byte-identical to Pipe 8. Differential and mutation checks are wired into `verify-all`.

## Task 2 — implementation and acceptance evidence complete; review outstanding

Implementation commits:

- `30b9baa` / `8f39fd8`: source-simulation route audit and enforceable replay-only math capture.
- `d1e9043`: deterministic 2,000-tick SMC1 corpus capture.
- `376b032`: `TARGET_SATURN` Q16 atan2 seam.
- `32eeb25` / `363bce2`: portable host-fixture temporary-directory and MSYS compiler selection.
- `f07fdd9`: fresh post-conversion capture evidence.

Fresh target result:

- Yaul ELF SHA-256: `54d36006080f75119376a0bd16a09d99214f8027a04a63c155374b2d56462305`.
- Full `make verify` passed: Q16 differential + mutation, native-math HOT total 0, dual-CPU coherency.
- Two independent 2,000-tick Ymir runs accepted: 64 samples each for `atan2s` and `atan2_lookup`, counters 125,618 each.
- Final Mario XYZ bit patterns are exact against baseline; L-infinity positional divergence is **0.0 world units**.
- Live SMC1 corpus/counter differs from the pre-conversion run (128/128 samples, +186 calls); this is recorded, not hidden. The captured-input old-fixture differential remains passing.

Primary evidence: `docs/saturn/evidence/reports/task2-sh2-native-math-capture-2026-07-29.md` and the `task2-post376b032-*` JSON artifacts beside it.

## Required next action

Dispatch a fresh **Task 2 reviewer** over `9e4dd6a..f07fdd9` (or task-specific packages for the implementation/evidence commits). It must decide whether the documented live-corpus change is acceptable under the positional-divergence and output-contract requirements. Do not start Task 3 until Task 2 receives a clean review or its fix loop resolves findings.

## Environment facts

- Use the worktree-local `.msys-home` and `build/tmp` for MSYS home/temp.
- Initialize `third_party/libyaul` at pinned commit `6012f79f...` before top-level verification.
- Build target with Yaul `COMPILER_PATH`; run the host Q16 fixture with `COMPILER_PATH` unset so MinGW host GCC and assembler remain paired.
- Use fresh ELF/CUE symbols, Ymir build-agent2, and `--dram-cart` for capture.
