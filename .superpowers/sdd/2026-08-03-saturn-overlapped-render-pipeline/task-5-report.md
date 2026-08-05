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
