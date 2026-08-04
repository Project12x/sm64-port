# Task 5 report — boot-trace ELF data sentinel

Date: 2026-08-03

Status: **source-complete; target evidence pending**

## Outcome

`sourceboot_boot_trace` now has a non-zero static initializer for its existing
magic and version words. This places the record in the ELF `.data` image while
preserving the fixed eight-word ABI, exported symbol, and runtime
`CPU_CACHE_THROUGH` writer. Before any sourceboot C entry executes, a correct
target-RAM read therefore decodes as `elf-data-initialized` with stage 0. An
all-zero read still indicates the wrong address, memory mapping, or image was
observed. `user-init-entry` remains the first runtime stage.

This is diagnostic observability only. It changes no scheduler, VDP, memory
allocation, or presentation behavior.

## TDD evidence

RED before the source change:

```powershell
& .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_sourceboot_boot_trace.py
```

Exit 1. Both source-contract tests failed because the exported trace was a
zero-initialized BSS global rather than an ELF `.data` record with seeded
magic/version.

RED before the reader change:

```powershell
& .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_capture_sourceboot_boot_trace.py
```

Exit 1. The valid magic/version stage-zero fixture decoded as `unknown`, not
`elf-data-initialized`.

Focused GREEN:

```powershell
& .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_sourceboot_boot_trace.py
& .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_capture_sourceboot_boot_trace.py
```

Both commands exit 0. The source mutation suite runs two tests and rejects a
regression to BSS; the reader suite runs five tests and distinguishes a valid
stage-zero header from an all-zero invalid record.

## Remaining gates

1. Reconcile this source-complete Task 5 transition into the active shared
   plan and execution ledger alongside concurrent Task 3/4 updates.
2. Build one serial diagnostic CUE through the audited wrapper.
3. Run one bounded headless Ymir capture with the matching ELF and record all
   raw words. Do not infer target evidence from the host-only tests.

No target build, CUE construction, Ymir launch, or capture was performed.
