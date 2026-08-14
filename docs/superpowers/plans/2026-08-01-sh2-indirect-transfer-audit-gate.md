# SH-2 Indirect-Transfer Audit Gate Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore a zero-unexplained-indirect-transfer native-math gate for the pinned Phase A `hot1/clip1` BOB build without fabricating direct-call evidence.

**Architecture:** The code-only analyzer remains fail-closed: a dereferenced function pointer never becomes a synthetic `DirectCallFact`. The route oracle may deliberately cover dynamic transfers, but only as dispatcher-granular, statically derived callback edges whose target closure is recursively audited. Statically knowable helper calls are repaired in the analyzer, never declared as callbacks.

**Tech Stack:** Python 3 `unittest`, SH-2 `sh-elf-objdump`, sourceboot `make -j1`, version-1 route-oracle syntax, and pinned SH-2 audit contracts.

## Global Constraints

- Use `SATURN_DEMO_HOT_PROMOTION=1`, `SATURN_DEMO_NEAR_CLIP=1`, `SATURN_SOURCE_CART_STAGE_SECTORS=16`, and the Phase A route flags for every authoritative observation.
- BOB is the first deterministic replay configuration, not a production-code special case.
- Never restore `<indirect:...>` synthetic direct-call facts or use a suppression list to make the gate pass.
- The complete unresolved inventory from the pinned ELF is the baseline, but only genuine dynamic dispatches may receive oracle coverage.
- The nine GraphNode-family transfers contain seven genuine callback dispatches and two statically knowable helper calls; do not describe them as nine callbacks.
- `_geo_process_held_object +296`, `_geo_process_node_and_siblings +2556`, and `_guMtxF2L +142` must resolve to real direct helper targets in analyzer output.
- `INDIRECT_EDGE` is dispatcher-granular. State and test that all sites in a dispatcher inherit its declared target set; do not imply per-instruction resolution.
- Entries are statically derived from pinned GeoLayout/route data, not runtime replay observation.
- An oracle edge is valid only when its dispatcher is reachable from the pinned root, its callback exists in the ELF, and it contributes to the audited closure. Duplicate, undeclared, stale, or unconsumed edges fail.
- Use one `make -j1` build at a time because the owner’s CPU is busy.

---

## File and Boundary Map

| File | Responsibility |
| --- | --- |
| `tools/saturn/verify_sh2_native_math.py` | Fail-closed analysis, oracle validation, declared-transfer coverage, and observations. |
| `tools/saturn/test_verify_sh2_native_math.py` | Exact SH-2 fixtures and oracle mutations. |
| `tools/saturn/sh2_native_math_sim_route_oracle_v1.txt` | Pinned, statically derived BOB edges consumed by the Phase A simulation audit. |
| `docs/superpowers/plans/2026-07-31-saturn-camera-phase-a-attribution.md` | Correct Phase A gate wording. |
| `docs/saturn/evidence/reports/` | Current ELF inventory evidence. |

## Tasks

### Task 1: Pin the authoritative Phase A indirect-transfer inventory

**Files:**
- Create: `docs/saturn/evidence/reports/sh2-native-math-hot1-clip1-indirect-inventory-2026-08-01.json`
- Create: `docs/saturn/evidence/reports/sh2-native-math-hot1-clip1-indirect-inventory-2026-08-01.md`
- Modify: `docs/superpowers/plans/2026-07-31-saturn-camera-phase-a-attribution.md`

**Interfaces:** The report contains `git rev-parse HEAD`, exact build command, ELF and oracle SHA-256, analysis mode, every `--audit-observation-only` unresolved-transfer field, and one classification for every record: `dynamic-dispatch`, `static-helper-provenance`, or `other-dynamic-dispatch`.

- [ ] **Step 1: Build route 0 using Task 5’s exact `hot1/clip1` command at `-j1`.** Preserve its ELF if `verify` reports the expected audit failure.
- [ ] **Step 2: Run the code-only observation against that ELF and retain JSON output.**
- [ ] **Step 3: Repeat Steps 1–2 for route 1, serially.**
- [ ] **Step 4: Write the JSON and Markdown inventory.** Identify all actual records, including seven genuine GraphNode callbacks, three static-helper provenance misses, and every remaining dispatcher.
- [ ] **Step 5: Amend the Phase A plan.** Replace the incorrect seven-site wording, state static derivation, and make zero unexplained transfers a Task 5 prerequisite.
- [ ] **Step 6: Commit.**

```powershell
git add docs/saturn/evidence/reports/sh2-native-math-hot1-clip1-indirect-inventory-2026-08-01.json docs/saturn/evidence/reports/sh2-native-math-hot1-clip1-indirect-inventory-2026-08-01.md docs/superpowers/plans/2026-07-31-saturn-camera-phase-a-attribution.md
git commit -m "docs: pin Phase A indirect transfer inventory"
```

### Task 2: Recover statically knowable helper calls

**Files:**
- Modify: `tools/saturn/verify_sh2_native_math.py`
- Modify: `tools/saturn/test_verify_sh2_native_math.py`

**Interfaces:** `analyze_code_only(...)` emits a real `CallSite` and `DirectCallFact` only when the recovered address maps to a linked function owner. A dereferenced GraphNode pointer remains unresolved with no fabricated direct-call fact.

- [ ] **Step 1: Add three failing minimal disassembly fixtures.** Reproduce the Task 1 stack-relative spill/reload shapes for `_geo_process_held_object +296`, `_geo_process_node_and_siblings +2556`, and `_guMtxF2L +142`; each asserts its concrete helper target and no unresolved transfer at that address.
- [ ] **Step 2: Run those tests and verify they fail because the transfers are unresolved.**
- [ ] **Step 3: Implement the smallest frame-slot provenance repair.** Preserve an exact symbolic atom through a proven r15-relative spill/reload across ABI clobbering; invalidate it on r15 movement, conflicting joins, escaped slots, unknown stores, or a callee that may write memory.
- [ ] **Step 4: Confirm the existing GraphNode dereference regressions still emit zero `CallSite`/`DirectCallFact` entries and one unresolved transfer.**
- [ ] **Step 5: Run the complete verifier suite and commit.**

```powershell
$env:PYTHONPATH = (Join-Path (Get-Location) 'tools\\saturn')
& 'C:\\Users\\estee\\.cache\\codex-runtimes\\codex-primary-runtime\\dependencies\\python\\python.exe' tools\\saturn\\test_verify_sh2_native_math.py
git add tools/saturn/verify_sh2_native_math.py tools/saturn/test_verify_sh2_native_math.py
git commit -m "fix: recover static SH-2 helper call provenance"
```

### Task 3: Make indirect oracle coverage explicit and checked

**Files:**
- Modify: `tools/saturn/verify_sh2_native_math.py`
- Modify: `tools/saturn/test_verify_sh2_native_math.py`
- Modify: `tools/saturn/sh2_native_math_sim_route_oracle_v1.txt`

**Interfaces:** `INDIRECT_EDGE <dispatcher> <callback>` covers unresolved transfers only in `<dispatcher>` and recursively adds `<callback>` to the audit closure. The verifier rejects an edge with an unreachable dispatcher, a missing callback owner, or no closure contribution, and reports every unlisted unresolved transfer.

- [ ] **Step 1: Add failing tests.** Cover a declared GraphNode callback, undeclared dynamic transfer, duplicate edge, unreachable dispatcher, missing callback, unconsumed edge, and six unresolved sites in one dispatcher inheriting the same callback set.
- [ ] **Step 2: Run focused tests and verify failure.**
- [ ] **Step 3: Implement validation and declared-transfer accounting.** Keep `DirectCallFact` behavior unchanged; the oracle changes closure and declared dynamic-transfer coverage only.
- [ ] **Step 4: Derive exact BOB entries from the pinned GeoLayout/route data in Task 1.** Include all required dynamic dispatchers, including non-GraphNode sites; never add the three static-helper records. For `_exec_display_list`, prove the guarded `sTaskSubmit` transfer unreachable in the exact `SATURN_DEMO_PATH=1` build rather than inventing an unlinked callback edge; a non-null or unknown variant remains unresolved.
- [ ] **Step 5: Run the full verifier suite and commit.**

```powershell
& 'C:\\Users\\estee\\.cache\\codex-runtimes\\codex-primary-runtime\\dependencies\\python\\python.exe' tools\\saturn\\test_verify_sh2_native_math.py
git add tools/saturn/verify_sh2_native_math.py tools/saturn/test_verify_sh2_native_math.py tools/saturn/sh2_native_math_sim_route_oracle_v1.txt
git commit -m "feat: declare Phase A indirect callback edges"
```

### Task 4: Re-run the native-math gate and resume Phase A Task 5

**Files:**
- Modify: `docs/saturn/evidence/reports/sh2-native-math-hot1-clip1-indirect-inventory-2026-08-01.json`
- Modify: `docs/saturn/evidence/reports/sh2-native-math-hot1-clip1-indirect-inventory-2026-08-01.md`
- Modify: `docs/superpowers/plans/2026-07-31-saturn-camera-phase-a-attribution.md`

**Interfaces:** Both routes use the same oracle digest and report zero unlisted unresolved transfers and zero unresolved effects. Helper totals are not repinned in this task.

- [ ] **Step 1: Build and verify route 0 using the exact `hot1/clip1` Task 5 command at `-j1`.**
- [ ] **Step 2: Build and verify route 1 using the same flags at `-j1`.**
- [ ] **Step 3: Run code-only observations for both ELFs.** Assert equal oracle digests and empty unresolved-transfer/effect lists.
- [ ] **Step 4: Append commands, ELF hashes, and comparison results to the inventory evidence.**
- [ ] **Step 5: Commit the evidence and Phase A gate note.**

## Verification Checklist

- [ ] The complete `test_verify_sh2_native_math.py` suite passes.
- [ ] The GraphNode dereference regression remains fail-closed.
- [ ] The three helper sites become real direct-call facts with no unresolved transfer.
- [ ] Every dynamic transfer is validated by a declared edge or fails the gate.
- [ ] Both routes agree on oracle digest and have no unexplained transfers/effects.
- [ ] No helper-count contract is silently altered.

## Handoff

Only after Task 4 is clean may the camera Phase A plan resume at Task 5’s three-role capture step. The camera performance decision remains unmeasured until those separate artifacts are captured.
