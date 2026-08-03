# SDD ledger — plan: docs/superpowers/plans/2026-08-03-vblank-presentation-boundary.md

Workspace: D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge
Branch: sh2/native-math-purge
Execution base: 1a48bfb4576936185819635d93ac324021e3cd03

Preflight: existing linked worktree verified; master plan reconciled with the
completed negative D1 diagnostic. Preserved unrelated native-math verifier,
audit, evidence, temporary build, and route scope. Reference inspection:
SlaveDriver `a8986591557b6e680550d3c23970284d3b38ff8f` VBlank/DMA ownership
patterns and Z-Treme `cff75451c1616aac1236fc2b44223902b55c706b`
`ZT_GAME.c` terminal `slSynch()` cadence; clean-room/pattern-only reuse.

Task 1: implementation ready for independent specification and quality review.
BASE 1a48bfb4576936185819635d93ac324021e3cd03. The focused source mutation
gate was observed RED against the four-tick/refill scheduler and GREEN after
the one-observed-generation fence, two-tick cap, whole-credit drop counter,
and terminal VDP1/VDP2 boundary. Direct `verify-runtime-contracts` reproduced
the known MSYS-to-Windows `\\d\\Code...` `PermissionError: [WinError 5]` before
host-contract compilation; it remains unchecked. No target build/Ymir launch
was run. Task files are staged only with the same-commit changelog entry;
commit hash and reviewer verdicts are pending this transition.
