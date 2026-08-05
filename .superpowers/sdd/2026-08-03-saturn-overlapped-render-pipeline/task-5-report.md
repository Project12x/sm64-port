# Task 5 — A5.9 automatic queue observation report

## Scope

Implemented only the controller-amended A5.9 host observation boundary. No
target C/C++ was changed, no target build or desktop Ymir was run, and no
scheduler policy or FPS claim changed.

## Implementation

- Added `tools/saturn/capture_sourceboot_throughput.py`.
- Added `tools/saturn/test_capture_sourceboot_throughput.py`.
- The tool reuses the established newline JSON-RPC `YmirClient` and sourceboot
  CUE/ISO binding/BIOS handoff helpers. It adds an in-process ELF32 symbol
  parser rather than invoking `sh-elf-*` through Windows `PATH`.
- It requires explicit matching `--game` CUE and `--elf`, hashes CUE/ISO/ELF/
  Ymir, rejects missing/duplicate/stripped/wrong-size target records, and
  proves immutable bytes from a linked executable ELF section exist in target
  memory before accepting any telemetry.
- It reads runtime and queue records through P2, decodes the required
  big-endian fields, advances exactly one VBlank per sample, observes only
  VDP2-generation changes, handles uint32 wrap, and fails closed without two
  presentation edges plus a coherent terminal queue record.
- The JSON report is bounded and structured in both paths. A successful report
  carries artifact identities, symbol addresses/sizes, target identity proof,
  cadence, latest coherent queue record, and presentation events. A failure
  carries `status: failed` plus stage/type/message and protocol diagnostics.

## TDD evidence

RED (before production module existed):

```text
python tools\saturn\test_capture_sourceboot_throughput.py
AssertionError: sourceboot throughput capture helper is missing
```

GREEN:

```text
python tools\saturn\test_capture_sourceboot_throughput.py
.........
Ran 9 tests ... OK

python tools\saturn\test_capture_sourceboot_boot_trace.py
................
Ran 16 tests ... OK

python -m py_compile tools\saturn\capture_sourceboot_throughput.py tools\saturn\test_capture_sourceboot_throughput.py
git diff --check
```

The focused fixture covers symbol names/sizes and missing/duplicate rejection,
ELF-byte target identity, every required big-endian field and phase order,
coherence rejection, sequence reuse protection, uint32 VBlank wrap, cadence
through skipped VBlanks, fewer-than-two edges, structured failure output, and
a fake Ymir sequence with an in-flight sample followed by a coherent terminal
record.

## Reference/provenance

- Local reference code inspected: `capture_hwtest.py`,
  `capture_camera_idle.py`, `capture_route_views.py`, and
  `capture_sourceboot_boot_trace.py`.
- Reuse mode: close adaptation of the project-owned newline JSON-RPC client,
  artifact binding, bounded reads, and sourceboot handoff helpers; new
  in-process ELF parser/cadence logic is project-owned.
- Ymir is protocol/API observation only; no Ymir source was copied.

## Remaining gates

- The A5.9 queue gate stays unchecked. A valid matching live Ymir capture must
  prove target identity and contain two presentation events plus one coherent
  queue record.
- Independent review remains open.
- No target build is credited or required for this host-tool source slice.

## Commit and review ledger

- Implementation/docs commit: recorded in the same commit after this report
  was prepared.
- Independent-review verdict: open.
- Target evidence: open; host-green is not target evidence.

## Review repair 1/5 (2026-08-05)

### Findings addressed

- **CRITICAL:** `observe_target()` now always calls `attach_queue_record()`
  for a coherent sample at a presentation edge. A sequence already attached to
  an earlier edge raises `ValueError`; it cannot be silently omitted while the
  capture succeeds.
- **IMPORTANT:** `build_elf_identity_probe()` now requires an executable
  identity window to be `SHT_PROGBITS`, `SHF_ALLOC|SHF_EXECINSTR`, and wholly
  contained in both file and virtual ranges of a `PT_LOAD` segment.
- **IMPORTANT:** protocol diagnostics now retain only a count- and
  serialized-byte-bounded tail of notifications, with original count/byte and
  truncation metadata calculated locally rather than relying on `YmirClient`.
- **MINOR:** removed the unused `time` import.

### Watched RED

```text
python tools\saturn\test_capture_sourceboot_throughput.py
Ran 12 tests ... FAILED
FAIL: test_repeated_coherent_sequence_fails_end_to_end_instead_of_being_silently_omitted
FAIL: test_identity_probe_rejects_non_alloc_non_progbits_or_unloaded_executable_sections
ERROR: test_protocol_diagnostics_bounds_many_and_oversized_notifications
```

### GREEN verification

```text
python tools\saturn\test_capture_sourceboot_throughput.py
............
Ran 12 tests ... OK

python tools\saturn\test_capture_sourceboot_boot_trace.py
................
Ran 16 tests ... OK

python -m py_compile tools\saturn\capture_sourceboot_throughput.py tools\saturn\test_capture_sourceboot_throughput.py
git diff --check
```

Covering tests: `test_repeated_coherent_sequence_fails_end_to_end_instead_of_being_silently_omitted`,
`test_identity_probe_rejects_non_alloc_non_progbits_or_unloaded_executable_sections`,
and `test_protocol_diagnostics_bounds_many_and_oversized_notifications`.

No target build or Ymir run occurred. The live queue-observation and review
gates remain open.

## Review repair 2/5 (2026-08-05)

### Finding addressed

- **Rereview blocker:** a candidate identity section could fit both the file
  and virtual ranges of a `PT_LOAD` while its section-relative file and target
  addresses differed. `build_elf_identity_probe()` now accepts a candidate only
  when `section.address - segment.address == section.offset - segment.offset`
  for that same load segment, so the hashed ELF bytes are exactly those mapped
  at the probed target address.

### Watched RED

```text
python tools\saturn\test_capture_sourceboot_throughput.py
Ran 13 tests ... FAILED
FAIL: test_identity_probe_rejects_offset_mapped_to_a_different_pt_load_address
AssertionError: ValueError not raised
```

### GREEN verification

```text
python tools\saturn\test_capture_sourceboot_throughput.py
.............
Ran 13 tests ... OK

python tools\saturn\test_capture_sourceboot_boot_trace.py
................
Ran 16 tests ... OK

python -m py_compile tools\saturn\capture_sourceboot_throughput.py tools\saturn\test_capture_sourceboot_throughput.py
git diff --check
```

Covering test:
`test_identity_probe_rejects_offset_mapped_to_a_different_pt_load_address`.
The root plan's prior blocker note is reconciled: this closes the source
defect, not the required fresh review or valid live queue-observation gate.
No target build or Ymir launch occurred.
