# A5.8 Cutover Memory Repair Review — 2026-08-04

## Context

- Prior agent: Codex
- Task claimed: repair the A5.8 live-cutover HWRAM overflow by moving two master-only terrain merge streams to LWRAM and update the live-cutover source contract.
- Range reviewed: `1819f2b4..0519f50d`
- Files examined: `src/port/saturn/gfx/saturn_demo_render.c`, `src/port/saturn/gfx/saturn_terrain_depth_bins.h`, `tools/saturn/test_render_job_live_cutover_source.py`, `CHANGELOG.md`, `STATE.md`, `ROADMAP.md`, the active plan/evidence report, the failed build log, and its linker map.
- Commands run: range `git diff`, `git diff --check`, focused wrapped Python test, symbol/use searches, failed-log inspection, and linker-map budget calculation. Per review scope, no target build or emulator was run.

## Verdict

**GO** for the one serialized post-repair target rebuild. This verdict does not credit target placement, runtime cache behavior, Ymir boot, or FPS evidence.

## Findings

### Critical

None.

### Important

None.

### Minor

None.

### Discrepancies between summary and code

None. The failed build log explicitly reports `region 'ram' overflowed by 10032 bytes`. The changed declarations place exactly the two claimed streams in `.lwram_bss`, and the documentation preserves the still-open target rebuild and runtime gates.

## Claim verification

- `SM64_SATURN_BOB_PRIMITIVE_COUNT` is 867 and `DEMO_TERRAIN_RESULT_CAPACITY` is twice that, or 1,734 entries. `sm64_saturn_terrain_emit_ref_t` is statically asserted to 8 bytes, so each array is 13,872 bytes and the total move is 27,744 bytes.
- The failed-link map reports `.lwram_bss` size `0xd1b50` after the fixed 128 KiB command arena. That left 58,544 bytes in the usable LWRAM arena before this repair; adding 27,744 bytes leaves a projected 30,800 bytes, above the linker's 16,384-byte retained floor. The move is sufficient to remove the 10,032-byte HWRAM overflow, with 17,712 bytes projected HWRAM headroom before any linker alignment difference.
- Every use of `s_terrain_emit_refs` and `s_terrain_emit_scratch` is in final terrain stream assembly, stable depth sorting, or master-owned VDP1 lowering. Queue callbacks publish descriptor-owned result and command payloads; they do not consume these two arrays. LWRAM placement therefore introduces no slave ownership or cache-publication contract.
- Replacing source assertions for direct `sm64_saturn_render_job_queue_publish` / `sm64_saturn_render_job_queue_drain_master` calls with `sm64_saturn_render_job_graph_publish` / `sm64_saturn_render_job_runtime_drain_master` matches the accepted frame implementation. The new placement test covers both exact declarations.
- Wrapped focused test result: 2 tests passed. Range `git diff --check` passed.

## What was done well

The repair is minimal and ownership-driven: it moves final master scratch rather than cross-CPU metadata or worker payloads. The test correction tracks the actual graph/runtime public seam and the documentation clearly refuses to over-credit a source-only placement check as target evidence.

## Recommended next actions

1. Run the single serialized guarded target rebuild already specified by the plan.
2. Verify the linked symbol/section placement and record actual HWRAM/LWRAM margins from the fresh ELF/map.
3. Only after target link and section checks pass, proceed to the documented desktop-Ymir runtime comparison.
