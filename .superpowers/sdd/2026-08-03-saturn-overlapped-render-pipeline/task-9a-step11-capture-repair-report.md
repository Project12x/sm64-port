# Task 9A Step 11 Capture Observer Repair Report

Date: 2026-08-05

Base: `7b37dfc3`

Status: Fix Round 3 source-repaired and focused-green; independent review is
required before capture retry. The sole target build is retained unchanged.
No target rebuild, Ymir launch, capture retry, FPS measurement, broad verify,
or native-math census occurred.

## Failed evidence disposition

The first Step 11 report is
`docs/saturn/evidence/reports/a9a-step11-overlap-throughput-2026-08-05.json`.
It binds ELF SHA-256
`5afbc7527bf470e9c9b099d5874f13030f4a4406dc93d9a751b057584c3065f0`
but fails at `symbol-resolution`: `s_runtime` is 104 bytes while the observer
required 92. Protocol `ready` is false, notifications are empty, and Ymir did
not start. The report is observer-contract failure evidence only; it supports
no runtime, FPS, phase, P2-placement, or memory-margin claim.

## Root cause

The 92-byte legacy runtime contains four 32-bit owner pointers, then
`active`, runtime notify/retire sequences, and telemetry at offset 28. Fix
Round 2 added marker observer, marker clock, and marker context pointers before
`active`. On SH-2 those three fields add 12 bytes: the reviewed runtime is 104
bytes and telemetry begins at offset 40.

The capture had two coupled fixed-ABI assumptions: symbol resolution required
exactly 92 bytes, and observation always read 92 bytes and decoded fixed legacy
offsets. Merely accepting 104 would be unsafe because it would interpret the
shifted owner/runtime words as telemetry.

## Watched RED

Command:

```powershell
.venv-saturn-tools\Scripts\python.exe tools/saturn/test_capture_sourceboot_throughput.py
```

Result: exit 1; 35 tests ran with three errors. The new cases failed because
`RUNTIME_LAYOUTS` did not exist, 104-byte decode raised `runtime telemetry has
wrong size`, and the exact-sized ELF fixture raised `s_runtime has wrong size
104, expected 92`.

## Implementation

- Added an explicit runtime-layout table with exactly `92 -> telemetry 28` and
  `104 -> telemetry 40`.
- Required-symbol resolution accepts those two sizes only for `s_runtime`;
  other symbols retain their single exact sizes. Unknown runtime sizes remain
  rejected with the known-size contract.
- Runtime decode derives every generation, sequence, claim, wait, failure, and
  quarantine offset from the selected telemetry base.
- Observation reads exactly the resolved `s_runtime` symbol size rather than a
  fixed legacy byte count.
- Source-contract coverage pins the reviewed field insertion before `active`
  and telemetry. Fixtures prove both reachable layouts and reject sizes 88,
  96, 100, and 108.

No upstream code was copied or closely ported. The change is original capture
tooling and retains the existing pinned project provenance/reuse modes.

## GREEN evidence

The watched command now exits 0: 35/35 tests pass.

Read-only exact-ELF validation (no Ymir/capture):

```text
SHA-256 5afbc7527bf470e9c9b099d5874f13030f4a4406dc93d9a751b057584c3065f0
s_runtime {'address': 638598972, 'size': 104}
layout {'telemetry': 40}
```

## Remaining gates and concerns

- [ ] Independent review of this observer/test/docs range.
- [ ] Retry capture against the unchanged exact ELF/CUE only after approval.
- [ ] Record linked P2 addresses and memory margins from approved evidence.
- [ ] Manual Ymir acceptance.
- [ ] Broad verify and native-math publication census.

Concern: layout selection remains intentionally tied to exact known symbol
sizes and the reviewed source field order. A future runtime struct change must
add a new watched layout contract; it must not be accepted by a range or
minimum-size rule.
