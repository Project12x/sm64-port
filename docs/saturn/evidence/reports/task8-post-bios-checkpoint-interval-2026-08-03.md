# Task 8 execution ledger — post-BIOS trace checkpoint interval

Status: **complete (host-tooling only)**.

`capture_sourceboot_boot_trace.py` now provides the optional
`--post-bios-checkpoint-interval <positive frames>` argument. With the option
omitted, its pre-existing behavior is exactly preserved: it executes the full
post-BIOS frame window and records the sole `post-bios` checkpoint. With the
option present, it executes no more than the requested chunk size at a time
and records a raw checkpoint after every chunk, including a shorter final
remainder. Checkpoint labels encode their cumulative post-BIOS frame offset
(for example `post-bios-64`, `post-bios-128`, `post-bios-180`).

This is a bounded observability change only. It performs no target build,
does not launch the GUI, changes neither the sourceboot scheduler nor the
trace ABI, and leaves target evidence unchecked until a fresh paired
CUE/ISO/ELF headless capture is run.

Implementation commit: this ledger's `diag(saturn): sample post-BIOS trace
boundaries` commit (the same scoped behavior-and-documentation change).

Tests run:

- `C:\Users\estee\AppData\Local\Programs\Python\Python312\python.exe tools\saturn\test_capture_sourceboot_boot_trace.py` — 13 passed.
- `git diff --check` — passed.

Independent review: **not run**. Remaining gate: run a bounded headless
capture with a positive checkpoint interval against one freshly paired
CUE/ISO/ELF and inspect the raw sequence. That result is diagnostic evidence,
not a performance measurement.
