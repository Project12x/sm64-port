# Peer Audit — 2026-08-09

## Context

- Prior agent: unknown (Task 2 was handed off as commit `16ff8b4b`).
- Task claimed: instrument `gObjectPool`, capture real occupancy in headless Ymir, and produce evidence for owner gate G1.
- Files examined: `src/game/object_list_processor.c`, `src/game/spawn_object.c`, `src/port/saturn/runtime/saturn_object_pool_probe.h`, `src/port/saturn/sourceboot/main.c`, `Makefile.saturn.mk`, `tools/saturn/capture_object_pool_occupancy.py`, `tools/saturn/test_object_pool_probe_contract.py`, and the committed JSON/Markdown evidence report.
- Commands run: `git diff --check 6195306f..16ff8b4b`; direct and Makefile invocations of `test_object_pool_probe_contract.py`; `py_compile` for the capture tool; `verify-saturn-geo-walk-runtime`; and direct JSON consistency checks.

## Findings

### Critical

None.

### Important

None for Task 2's stated G1 evidence purpose. The capacity-reporting and regression-guard hardenings identified by the separate code-quality review are required before Task 4 reuses this harness with an override; they do not alter the current, compiled-in capacity-240 capture.

### Minor

None.

### Discrepancies between summary and code

The evidence is explicit that the 21,600-frame capture is an idle boot, not a played route: no live input or replay runs, so pickup/hold and action-particle peaks remain unmeasured. This is not a concealed discrepancy: the committed report labels 138 as a measured floor and instructs G1 to carry the coverage limitation forward.

## What was done well

- The implementation uses the real free-list handoff sites: increment/peak after successful `try_allocate_object()` allocation, guarded decrement in `deallocate_object()`, and a failure increment immediately before the unrecoverable exhausted-pool loop.
- The target-visible 20-byte volatile record is TARGET_SATURN-gated, symbol-resolvable, magic-checked, and sampled once per authoritative source tick.
- The evidence binds CUE, ISO, and ELF identities; its JSON independently agrees on capacity 240, observed peak 138, zero allocation failures, 20,100 requested post-BIOS frames, and 67 post-BIOS samples (66 valid after the ELF image loads).
- The 11-test structural contract passed both directly and through `make -f Makefile.saturn.mk verify-saturn-object-pool-probe-contract`; the unrelated geo-walk runtime canary also passed.

## Recommended next actions

1. Treat Task 2 as review-cleared for G1, but present 138 as an observed idle-boot floor, never an interaction-route ceiling.
2. At G1, obtain the owner's explicit capacity choice with the idle-boot/pickup-and-hold coverage gap visible.
3. Before Task 4's remeasurement, apply both requirements in `audits/sm64-port_task2_differential_review_20260809.md`.
