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
was run. Implementation commit `950ab37a` carries the same-commit changelog
entry; reviewer verdicts remain pending.

Task 1: specification review GO; quality review NO-GO. Fix round 1/5 resolves
I1: the focused mutation gate now requires exactly one
`sourceboot_present_generation(scheduler_now)` invocation and its in-memory
duplicate-call mutant fails with the one-presentation diagnostic. M1 documents
the intentional displayed-bank lag; M2 removes the unused
`PRESENTATION_BOUNDARY_COUNTERS` grouping. Scoped quality rereview is GO:
I1/M1/M2 are addressed and no new Critical/Important finding exists. The
duplicate-helper mutant RED, focused gate GREEN (5 tests), and profile subset
GREEN (13, 1 skipped) are recorded in the implementation report. Task 1:
source-complete (commits `950ab37a..2de483d9`, reviews clean); Step 7 serial
target/Ymir evidence remains open. Runtime-contract wrapper still fails before
compilation with the known MSYS-to-Windows path translation WinError 5.

Task 1 Step 7: serial target build PASS (294.1 seconds); GUI Ymir launch used
the project DRAM profile and short staged CUE path. Owner reported immediate
post-BIOS exit/freeze. No crash dump or Ymir log exists. Read-only investigation
attributes the strongest new correlation to removal of the predecessor's
bootstrap VDP2 commit plus `vdp2_sync_wait()` retirement barrier; this is a
testable hypothesis, not yet proven root cause.

Task 2: source-complete pending independent reviews and replacement target
evidence. `815c4352` restores only the predecessor boot-time null-snapshot
VDP2 begin/commit plus `vdp2_sync_wait()` retirement barrier before frontend
and scheduler initialization. The focused mutation gate was observed RED
(exit 1; 6 tests, one expected missing-bootstrap failure) before the source
edit and GREEN afterward (exit 0; 6 tests). It rejects absent, late, duplicate,
VDP1-work, and simulation-work bootstrap mutants while retaining the exact-one
post-bootstrap terminal presentation contract. No target build or Ymir launch
was performed; the post-BIOS causal hypothesis remains unproven until the
replacement serial CUE/manual gate. Independent spec and quality reviews are
still pending.

Task 1 Step 7: serial target build PASS (294.1 seconds), output
`e2-bob-demo-replay-camroute0-live-input-boot600-atan2v2-camv3-stage8-r6000-slave1-poly2-hot1-clip1-bsp1-frag0-pipe4`.
CUE/ISO copied without modification to `.tmp-experimental-cue/presentation-boundary`
for Ymir's long-path limitation. Ymir launched with `.ymir-profile`; manual
owner observation is active, so speed/controls/VDP counter evidence remains
unrecorded and Step 7 stays unchecked.

Task 3: source-complete; independent review and target capture pending.
Commit `30123c1b` (`diag(saturn): trace post-BIOS presentation boundaries`)
adds a stable non-static volatile eight-word target-RAM trace and a bounded,
symbol-aware headless Ymir reader. RED was observed before source work: the
two-test source contract failed for missing magic, and the reader contract
failed because its module was absent. GREEN: `test_sourceboot_boot_trace.py`
(2), `test_capture_sourceboot_boot_trace.py` (2), and the retained
`test_sourceboot_presentation_boundary.py` (6) all passed; the reader's help
and Python compilation also passed. `git diff --check` passed before the
commit. No target build, CUE construction, Ymir launch, or capture occurred.
Remaining gates: independent specification/quality review, one serial trace
CUE, then one bounded post-BIOS debug capture that records its exact last
stage/raw words. The existing runtime-contract wrapper remains separately
unpassed due to its known MSYS-to-Windows pre-compilation PermissionError.

Task 3 fix round 1/5: specification review REJECT found that the headless
reader accepted zero post-BIOS frames but then issued an invalid Ymir RPC.
`2c1acca8` adds and uses a 1..3600 validator before any client is created.
The new test was observed RED (validator absent), then GREEN: reader (3),
trace source (2), and retained presentation (6) gates pass; Python
compilation passes. No target build/CUE/Ymir action occurred. Independent
rereview remains required; Step 3 stays unchecked.

Task 3 fix round 2/5: quality review NO-GO identified cached-P1 trace writes
as invisible to Ymir/hardware debug reads of backing WRAM. `b317a2e5` retains
the exported symbol but publishes every record word via the project hwtest
precedent, `CPU_CACHE_THROUGH | (uintptr_t)&sourceboot_boot_trace`; it also
pins the exact eight-word ABI with a 32-byte C static assertion. RED: the
strengthened source contract failed for the absent ABI guard. GREEN: trace
source (2), reader (3), and presentation (6) pass plus Python compilation;
the mutation suite rejects cached-only alias/writer and ABI-size changes. No
target build/CUE/Ymir action occurred. Quality rereview, serial trace CUE, and
bounded capture remain unexecuted.

Task 3 fix round 3/5: the attempted bounded capture stopped before emulation
when direct `sh-elf-nm` resolution produced no usable trace symbol. `2ba3ba64`
uses the audited DLL-safe MSYS wrapper and accepts the target ABI's one leading
underscore. RED covered empty direct output versus wrapper output, then the
underscored symbol spelling. GREEN: reader (4), source trace (2), presentation
(6), and Python compilation pass. The newest existing linked ELF resolves
read-only at `0x060eb53c`; no target build/CUE/Ymir action occurred. Quality
rereview and target capture remain pending.
