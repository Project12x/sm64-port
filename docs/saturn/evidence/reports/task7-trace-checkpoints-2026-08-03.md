# Task 7 execution ledger — BIOS trace checkpoints

Status: **complete (host-tooling only)**.

The sourceboot boot-trace reader now takes a raw 32-byte `mem.peek` sample at
protocol-ready and after each pre-existing BIOS-handoff run boundary. Each
sample records its stable label, accumulated emulated frame count, raw bytes
and big-endian words, accumulated `instance.stopped` PCs, and notification
count. The final decoded trace and the existing artifact binding/failure report
remain unchanged; checkpoints are present in both successful and failed
reports.

This is an observability change only: it performs no target build, does not
launch the GUI, and makes no scheduler, VDP, or target-memory-layout change.

Tests run:

- `C:\Users\estee\AppData\Local\Programs\Python\Python312\python.exe tools\saturn\test_capture_sourceboot_boot_trace.py` — 10 passed.
- `git diff --check` — pending before commit.

Independent review: **not run**. Remaining gate: a bounded headless capture
using a freshly paired CUE/ISO/ELF must locate the first checkpoint where the
sentinel becomes invalid; that is target evidence and remains unchecked.
