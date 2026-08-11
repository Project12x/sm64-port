# Task 10 execution report

Date: 2026-08-11

## Status

Task 10 is active. Task 9 is complete at controller base `bb9d3c9f`. The
canonical staged integrated BOB candidate verified before emulator I/O at
manifest SHA-256
`9110b40da0e890b7b03dc5748e9ead4a47865ea4f9e3df21869b47de33679b99`
and identity `id-a40f992c085da2f0`. No rebuild or restage occurred.

## Repair round 1: bounded loaded-identity wait

The first exact combined-smoke command exited 1 after 12.8 seconds and 1,500
emulated frames. Its report SHA-256 was
`9261cc61b624a69e565f81d62a90f08d4c7166e92f0d5d098a1ae8369af8acc2`.
The fixed BIOS macro had completed, but immutable target code had not loaded;
the report contained zero valid samples and the failure `running target does
not contain immutable bytes from the matching ELF`. Visual and desktop chains
did not run.

Exact-candidate diagnostics established the publication sequence after the
1,500-frame macro: immutable code first matched at +540 VBlanks, and the
initialized sealed build identity first matched at +577. The canonical target
therefore booted and matched; the harness proof instant was premature.

Reference-first inspection used
`tools/saturn/capture_sourceboot_throughput.py::wait_for_target_identity` at
commit `bb9d3c9f`; reuse mode is same-repository close-port. The combined smoke
now waits within a 3,600-VBlank bound until both immutable code and the sealed
initialized build identity match, then begins the unchanged 20,100-frame
sample interval. Target bytes and release-manifest binding inputs did not
change, so Task 9 rebuild/repro/v4/staging gates were not reopened.

TDD RED was the focused regression error that the dual-identity wait API was
absent. Focused GREEN passed the regression and CLI pair 2/2, then the full
capture suite passed 13/13 with normal Windows ancestor-handle access. The
sandboxed full-suite setup run had two environmental `C:\Users\estee` handle
errors and did not exercise those release fixtures; it is discarded rather
than counted as product evidence.

## Environment

The brief-relative `.ymir-profile` is absent in the linked worktree. The
established canonical project profile was used from
`D:\Code\RetroDev\sm64-saturn-port\sm64-port\.ymir-profile`; its 524,288-byte
USA BIOS has SHA-256
`96e106f740ab448cf89f0dd49dfbac7fe5391cb6bd6e14ad5e3061c13330266f`.

## Open gates

- Commit and independently review the tooling repair.
- Rerun and accept the exact staged 20,100-frame smoke.
- Run the same-release visual capture and independently inspect its PNG.
- Run and verify the desktop launch report.
- Present the owner checklist; do not mark manual acceptance without verdict.
- Complete docs, focused verification, self-review, and both controller reviews.

The sealed capacity remains 208. Peak 138 remains only a prior idle-boot floor
without pickup/hold or action-particle coverage. `sm64-saturn-full` remains
non-releasable because its content/system inventory and game-wide target
evidence are incomplete; no total-game completion is claimed.
