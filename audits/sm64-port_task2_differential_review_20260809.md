# Task 2 Differential Review — 2026-08-09

## Executive Summary

| Severity | Count |
| --- | ---: |
| Critical | 0 |
| High | 0 |
| Medium | 2 |
| Low | 0 |

**Overall risk:** Medium for future reuse; low for the committed capacity-240 measurement.

**Recommendation:** Approve Task 2's existing G1 evidence, with two mandatory Task 4 hardenings before the capture harness is reused for an overridden pool capacity.

**Key metrics:** 10 changed files; all changed runtime paths inspected; 11/11 probe-contract tests passed directly and through Make; `py_compile`, `git diff --check`, and the geo-walk runtime canary passed. No access-control, cryptographic, network, or external-input security regression was introduced.

## What Changed

**Commit range:** `6195306f..16ff8b4b`
**Commit:** `16ff8b4b feat(saturn): instrument object pool occupancy with fail-closed probe`

The change adds a volatile, target-visible occupancy record; hooks it into successful allocation, free, and unrecoverable exhaustion; samples it every source tick; adds a headless Ymir capture harness plus a source-text contract; and commits a 21,600-frame evidence capture.

| Area | Risk | Blast radius |
| --- | --- | --- |
| Pool hooks in `src/game/spawn_object.c` | Medium | All Saturn object allocation/free events |
| Source-tick sample in `sourceboot/main.c` | Medium | Every source simulation tick |
| Capture harness | Medium | Task 2 evidence and Task 4's required remeasurement |
| Source-text regression guard | Medium | Future detection of misplaced/missing hooks |

## Critical Findings

None.

## Medium Findings

### Capacity metadata will be false after Task 4 enables the planned override

**Files:** `tools/saturn/capture_object_pool_occupancy.py:73-78,193,270`; `docs/superpowers/plans/2026-08-09-memory-residency-campaign.md:95-104`
**Commit:** `16ff8b4b`
**Blast radius:** Every Task 4+ capture that invokes the existing harness with `SATURN_OBJECT_POOL_CAPACITY=<G1 value>`.

`read_pool_capacity()` reads a literal `#define OBJECT_POOL_CAPACITY <digits>` from the live source header. Task 4 deliberately changes that header to select `SATURN_OBJECT_POOL_CAPACITY_OVERRIDE` when the build receives `-D...`; the raw source still contains the fallback literal `240`, so the capture will report `pool_capacity: 240` even when the ELF was built for, for example, 208 slots. It also reads an unbound live header rather than capacity provenance from the CUE/ISO/ELF tuple.

The current committed capture is unaffected because its compiled capacity is the literal 240. Before Task 4 reuses this script, pass the selected capacity explicitly from the sealed build identity or verify it from an ELF/map-derived artifact, reject disagreement, and add an override-mode regression test.

### The new contract test does not prove its counter hooks are inside the named functions

**Files:** `tools/saturn/test_object_pool_probe_contract.py:194-247`
**Commit:** `16ff8b4b`
**Blast radius:** Future edits can relocate a hook into any unrelated `#ifdef TARGET_SATURN` block in `spawn_object.c` while all 11 tests remain green.

The tests' names and comments claim to validate `try_allocate_object()`, `deallocate_object()`, and `allocate_object()`'s fatal branch, but each scans every TARGET_SATURN branch in the entire file. The implementation is correctly located today (`src/game/spawn_object.c:125-130,169-171,287`), but the guard would not detect a later move to an unrelated allocation or cleanup path.

Before Task 4, extract balanced bodies for the three named C functions and assert each operation in its respective body; keep the existing mutation tests, but mutate those function-local bodies.

## Test Coverage Analysis

The structural guard covers probe shape, volatile declarations, compile-time gating, counter presence, and three non-tautological mutations. It does not test capture-harness helpers (`read_pool_capacity`, decode/error paths, symbol resolution, artifact mismatch handling) and it does not compile the changed SH-2 sources during this review. The committed evidence does record a clean canonical target build and a real Ymir run, while this review independently reran the host checks and canary.

## Blast Radius Analysis

`try_allocate_object()` is the shared allocation seam reached by `allocate_object()` and `create_object()`; it has a broad gameplay impact but is compiled only under `TARGET_SATURN`. `sourceboot_run_source_tick()` runs once per simulation generation, making the sample counter broad but observational. The two findings affect evidence integrity in the next capacity-cut task, not the present capacity-240 capture.

## Historical Context

No validation or security control was removed. `git log -S` shows the probe was introduced by this commit; the allocation routine predates the campaign. `git diff --check` is clean.

## Recommendations

### Immediate

- [ ] Record the Task 2 review as clear for G1, retaining the evidence report's idle-boot limitation.
- [ ] Do not use the harness as proof of an overridden capacity until the two fixes above land.

### Before Task 4 remeasurement

- [ ] Bind reported pool capacity to the sealed compiled artifact rather than the mutable source fallback.
- [ ] Make the source-text guard function-local and add an override-capacity regression case.

### Technical debt

- [ ] Add focused unit tests for the capture tool's decoding, argument validation, and artifact-binding failure paths when host-tool test coverage expands.

## Analysis Methodology

**Strategy:** Focused differential review of the 10-file commit in a large codebase.

**Techniques:** Parent/head diff inspection; full review of changed runtime and host-tool code; caller search; removed-validation scan; committed evidence consistency check; direct contract test; Makefile contract target; Python bytecode compilation; geo-walk runtime canary; and artifact/status inspection.

**Limitations:** No new SH-2 image or Ymir session was built/launched for this review; the committed target evidence is treated as historical evidence, not regenerated proof. The current worktree's unrelated SDD/artifact dirt was excluded from the commit-scoped review.

**Confidence:** High for the two findings and the present capture's stated scope.
