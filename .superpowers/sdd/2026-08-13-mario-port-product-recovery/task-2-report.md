# Task 2 — baseline Mario restoration

Status: **BLOCKED BEFORE TARGET CUE**

## Verified source-level result

- Accepted source anchor: `d7b04d61` (A9A Pipe-4 renderer donor).
- Current `demo_emit_mario_range` diverged from the donor by using a neutral
  fixed Gouraud row with material base color and textured `CC_GOURAUD`.
- A focused source-policy regression was RED against that state and GREEN 4/4
  after a bounded local restoration of per-material RGB-times-light Gouraud
  entries, neutral polygon base, and direct textured `CC_REPLACE`.
- This implementation is uncommitted inside an already dirty renderer file. It
  is not a target-proven or accepted transplant.

## Build attempts

1. Current normal-BOB serial build stopped before SH-2 compilation at
   `compile_actor_family_bundle.py`: `family report does not match
   closure-derived semantics`. Dynamic-actor disablement did not bypass the
   unconditional package build/seal.
2. Isolated `d7b04d61` donor build bound only canonical ROM, extracted-PC source
   assets, and Python environment. The apparent missing Windows `./tools/mio0`
   was caused by `make -n` propagating dry-run into the extractor's child tool
   build. A real extractor run regenerated the donor BOB PNG and helpers, then
   stopped at the sound branch's literal `python3`, which selects the unavailable
   Windows App Execution Alias instead of the approved interpreter. No current
   Saturn output or package content was imported.

No CUE, ELF completion, Ymir launch, screenshot, audio result, or live attempt
exists. The detailed donor log is
`sm64-port/.worktrees/task2-a9a-donor-20260814/build/saturn/task2-a9a-donor-feasibility-20260814.md`.

## Remaining boundary

The next action needs an explicit product-boundary decision: make the one-line,
donor-only `sys.executable` repair for the historical sound extractor, then
produce a baseline-derived CUE; or authorize a different named build boundary.
Do not open actor, audio, or Whomp's Fortress work first.
