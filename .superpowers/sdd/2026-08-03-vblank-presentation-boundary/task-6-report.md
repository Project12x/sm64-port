# Task 6 report — failed boot-trace evidence preservation

Date: 2026-08-03

Status: **source-complete; target capture pending**

## Outcome

`capture_sourceboot_boot_trace.py` now writes its requested JSON report before
returning nonzero when the target record is absent or invalid.  The failure
report retains the raw `mem.peek` byte payload and big-endian words whenever
available, all observed Ymir protocol notifications, and Ymir stderr bounded
through the shared `capture_hwtest.cap_stderr` policy.  It does not treat an
invalid header as a successful target observation.

## TDD and verification

RED: `test_invalid_trace_report_retains_raw_target_and_ymir_evidence` failed
because `build_failed_trace_report` did not exist.

GREEN:

```powershell
& 'C:\Users\estee\AppData\Local\Programs\Python\Python312\python.exe' tools\saturn\test_capture_sourceboot_boot_trace.py
& 'C:\Users\estee\AppData\Local\Programs\Python\Python312\python.exe' -m py_compile tools\saturn\capture_sourceboot_boot_trace.py
```

Both pass: 7 focused reader tests and Python compilation.  The new test covers
raw target words, ready/stopped notifications, and stderr evidence; the
bounded-stderr test covers the shared 64 KiB cap.

## Remaining gate

Run one bounded headless capture with the explicitly paired current CUE, ISO,
and ELF, then use this report to determine whether the observed RAM address is
wrong, the media launch is wrong, or target execution stops before the trace.
No target build or Ymir launch was performed for this task.
