# Task 6 tooling execution ledger — artifact binding

Status: **complete (host-tooling only)**.

The post-BIOS trace reader now resolves its CUE `FILE` record before it can
start Ymir, identifies the actual ISO it will load, requires the CUE/ISO/ELF
names and CUE-local `obj/<stem>.elf` path to match, and rejects an ISO older
than that ELF. The emitted report retains the existing `game`/`elf` fields and
adds the CUE, referenced ISO, and ELF SHA-256/size/mtime identities under
`artifacts`. The existing bad-sentinel failure path is unchanged: an invalid
trace still writes bounded raw words, protocol notifications, and capped
stderr.

Tests run:

- `test_capture_sourceboot_boot_trace.py` — 9 passed (the 7 existing trace
  sentinel/failure-report tests plus 2 artifact-binding cases).
- Source compile check and `git diff --check` — passed.

Independent-review verdict: **not run**. Remaining target gates are
intentionally unchecked: this task performed no target build, Ymir launch, or
capture. A matching CUE+ISO restage that still returns magic `0x045e02aa`
remains a separate root-cause investigation; this change is only a provenance
guard.
