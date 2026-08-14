# Peer Audit — 2026-08-03

## Context
- Prior agent: Codex
- Task claimed: Correct Task 7's compact BSP work-span contract so accepted terrain admission uses node-local spans without an admitted-array or post-traversal primitive scan, with generated-artifact identity protection.
- Files examined: `.superpowers/sdd/2026-08-02-saturn-dual-sh2-vdp-pipeline-sprint/task-7-report.md`; `src/port/saturn/gfx/saturn_demo_render.c`; `tools/saturn/compile_bob_bsp.py`; `tools/saturn/emit_bob_scene.py`; `tools/saturn/bob_bsp_header_smoke.c`; `tools/saturn/test_tools.py`; `src/port/saturn/sourceboot/Makefile`; `Makefile.saturn.mk`; generated `build/saturn/sourceboot/generated/bob_scene.h`, `bob_bsp.h`, and `bob_area1_bsp_report.json`.
- Commands run: `git diff --check 524c48c..79923ce`; exact-commit source/diff inspection; generated-header/report identity inspection; focused host test command `./.venv-saturn-tools/Scripts/python.exe tools/saturn/test_tools.py` with the four Task 7 tests.

## Findings

### Critical
None.

### Important
None.

### Minor
None.

### Discrepancies between summary and code
None. The task report's corrective terminology is accurate: the arrays are node spans, not leaf spans. `demo_spatial_append_node_span()` consumes `sm64_saturn_bob_node_first_ref`, `sm64_saturn_bob_node_ref_count`, and `sm64_saturn_bob_primitive_refs`; terminal-leaf metadata remains separately named `sm64_saturn_bob_bsp_leaf_ranges`.

## What was done well
- The accepted target route (`SATURN_DEMO_BSP_ORDER=1`, `SATURN_DEMO_BSP_FRAGMENTS=0`) resets only the 28-word first-reference-wins bitset and node-visit guard, then appends spans during traversal (`saturn_demo_render.c:341-475`). It has neither the old 867-entry admission array nor a subsequent all-primitive admission scan. The fallback full-list loop is explicitly outside that accepted route.
- Runtime span bounds and primitive IDs fail closed. Work-list capacity preserves first-reference-wins semantics.
- `compile_bob_bsp.py` constructs source-stable node-local packed spans and computes a digest over all three identity-defining arrays. `emit_bob_scene.py` recomputes that digest before stamping the scene header; same-cardinality stale reordered reports therefore fail rather than being stamped.
- The committed generated artifacts agree on both the 64-bit preprocessor identity and full SHA-256: `47e9e06669381637b8889fc73bdb8d5b87c1a99ff07efe4985167f991f06ff9c`. Renderer and C smoke compile-time checks compare the emitted IDs; C smoke additionally verifies cardinalities, ranges, in-range IDs, within-span uniqueness, and complete primitive coverage.
- The host oracle is behavioral rather than merely checking that fields exist: it compares packed-span first-reference-wins order with the predecessor local-reference stream over three deterministic admitted-node masks. The emitter test independently exercises both stale digest rejection and re-digested same-cardinality reorder identity divergence.
- Focused host verification passed: 4/4 tests in 105.409 seconds. No MSYS, Bash, `sh-elf-*`, target build, or Ymir process was invoked.

## Recommended next actions
1. Mark Task 7 complete and begin Task 8.
