# Saturn Camera Phase A Attribution Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Measure the recoverable cost of the original camera on one identical BOB replay, then decide whether to proceed with the production fixed-point camera core.

**Architecture:** Preserve the committed SCC1 raw camera transport as evidence infrastructure. Build three separately identified roles—source baseline, held-camera bypass diagnostic, and minimal fixed follow candidate—without running two camera implementations in one timed artifact. The candidate remains a scene-neutral target seam and publishes fixed state through an integer-only compatibility boundary.

**Tech Stack:** C11/SM64 source tree, Saturn SH-2/Yaul, existing sourceboot Makefile, Ymir capture tooling, Python host contract tests, FRT timing, and the existing SH-2 linked-helper audit.

## Global Constraints

- BOB is the proving ground; production camera code must contain no BOB identifiers, coordinates, route checks, or exceptions.
- The source camera is an oracle in a separate baseline/replay build only; candidate builds never call it as a runtime fallback.
- Phase A uses the same deterministic 2,000-tick `bob-default-camera-v1` replay for all three roles.
- The candidate camera closure must contain zero soft-float/libm helper edges; generic 64-bit fixed-point expressions require disassembly evidence before use.
- Native 16-bit binary angles remain the public angle representation; world fixed-point format is selected from captured range evidence.
- Existing SCC1 payload layout remains versioned and raw-first; fields may be deprecated only after a replacement semantic or feel metric is recorded.
- Do not vendor `libfixmath` or `libfixmatrix` in this phase. Prefer in-tree primitives, libyaul APIs, and the existing attributed SH-2 multiply.
- A bypass improvement below 5% stops the replacement sprint; 5–10% requires an independent completion-cost justification; above 10% proceeds to Phase B. Every candidate must reduce camera-stage time by at least 50%.

---

## File and Boundary Map

| File | Responsibility in this plan |
| --- | --- |
| `src/game/camera.c` | Existing source camera seam, probe publication, and target-role dispatch boundary around `update_camera(struct Camera *)`. |
| `src/game/level_update.c` | Authoritative call sites that must remain source-owned while camera-stage timing is collected. |
| `src/port/saturn/runtime/saturn_camera_probe.h` | Versioned 77-word observable snapshot and diagnostics ABI. |
| `src/port/saturn/runtime/saturn_camera_role.h/.c` | Target-only role selection, bypass hold state, and fixed-candidate dispatch. |
| `src/port/saturn/runtime/saturn_camera_fixed.h/.c` | Minimal scene-neutral fixed follow state and integer-only publication. |
| `src/port/saturn/gfx/saturn_q16_sh2.h` | Existing attributed SH-2 multiply; no replacement generic math library. |
| `src/port/saturn/sourceboot/Makefile` | Role IDs, output identity, route selection, and role-specific build flags. |
| `src/port/saturn/sourceboot/main.c` | Source tick boundary, route reset, and route-probe publication. |
| `src/port/saturn/sourceboot/source_camera_idle_probe.c/.h` | Existing SCC1 capture window; preserve its raw layout and completion protocol. |
| `tools/saturn/camera_idle_contract.py` | Raw SCC1 decoder, role validation, and semantic/feel comparison helpers. |
| `tools/saturn/capture_camera_idle.py` | Symbol-derived target capture and artifact identity binding. |
| `tools/saturn/compare_camera_route_reports.py` | Same-role determinism and baseline/candidate Phase A comparison. |
| `tools/saturn/verify_camera_idle_capture.py` | Raw-first report validation. |
| `tools/saturn/test_camera_idle_contract.py` | SCC1 schema, role, stable-window, and mutation tests. |
| `tools/saturn/test_capture_camera_idle.py` | Bounded target-read and symbol/role binding tests. |
| `tools/saturn/test_compare_camera_route_reports.py` | Three-role timing and semantic comparison tests. |
| `tools/saturn/test_verify_sourceboot_memory_map.py` | Route-specific SCC1 linker-budget tests. |
| `docs/saturn/evidence/reports/` | Machine-readable Phase A results and decision record. |

## Tasks

### Task 1: Generalize SCC1 from legacy baseline/Q wording to Phase A roles

**Files:**
- Modify: `tools/saturn/camera_idle_contract.py`
- Modify: `tools/saturn/capture_camera_idle.py`
- Modify: `tools/saturn/verify_camera_idle_capture.py`
- Modify: `tools/saturn/compare_camera_route_reports.py`
- Modify: `tools/saturn/test_camera_idle_contract.py`
- Modify: `tools/saturn/test_capture_camera_idle.py`
- Modify: `tools/saturn/test_compare_camera_route_reports.py`

**Interfaces:**
- Role IDs are `1 = camera-source-baseline`, `2 = camera-bypass-diagnostic`, and `3 = camera-fixed-candidate`.
- `validate_scc1(capture, *, expected_role, expected_idle_start_tick, expected_route_id)` remains the raw-first entry point; bridge-count arguments are removed from the Phase A API.
- `compare_camera_route_reports(baseline_runs, candidate_runs, *, candidate_role, route, require_sim_improvement)` compares two same-role pairs and returns role names plus timing deltas.

- [ ] **Step 1: Write failing role-contract tests.** Add tests proving that all three role IDs decode, an unknown role is rejected, legacy Q bridge counters are not required for candidate validation, and the comparison report labels bypass and fixed candidates distinctly.
- [ ] **Step 2: Run the focused contract tests and verify failure.**

  ```powershell
  $env:PYTHONPATH = (Join-Path (Get-Location) 'tools\\saturn')
  & 'C:\\Users\\estee\\.cache\\codex-runtimes\\codex-primary-runtime\\dependencies\\python\\python.exe' -m unittest test_camera_idle_contract test_capture_camera_idle test_compare_camera_route_reports
  ```

  Expected: the new three-role assertions fail against the legacy baseline/Q mapping.
- [ ] **Step 3: Implement the role-neutral contract.** Accept role IDs 1–3, retain the existing header/sample sizes and raw-first decoding, remove bridge/generation requirements from Phase A validation, and make capture/report role choices use the three exact names above. Keep the old field offsets readable as diagnostic words; do not resize SCC1.
- [ ] **Step 4: Run the focused tests and verify pass.** The command above must pass, including mutation tests for role ID, raw bytes, sample stability, and artifact-marker mismatches.
- [ ] **Step 5: Commit.**

  ```powershell
  git add tools/saturn/camera_idle_contract.py tools/saturn/capture_camera_idle.py tools/saturn/verify_camera_idle_capture.py tools/saturn/compare_camera_route_reports.py tools/saturn/test_camera_idle_contract.py tools/saturn/test_capture_camera_idle.py tools/saturn/test_compare_camera_route_reports.py
  git commit -m "test: define Phase A camera role contract"
  ```

### Task 2: Add the held-camera bypass diagnostic role

**Files:**
- Create: `src/port/saturn/runtime/saturn_camera_role.h`
- Create: `src/port/saturn/runtime/saturn_camera_role.c`
- Modify: `src/game/camera.c:3177-3385`
- Modify: `src/port/saturn/sourceboot/Makefile`
- Modify: `src/port/saturn/sourceboot/main.c`
- Test: `tools/saturn/test_camera_role_contract.py`

**Interfaces:**
- `typedef enum sm64_saturn_camera_role { SM64_SATURN_CAMERA_SOURCE_BASELINE = 1, SM64_SATURN_CAMERA_BYPASS_DIAGNOSTIC = 2, SM64_SATURN_CAMERA_FIXED_CANDIDATE = 3 }`.
- `bool sm64_saturn_camera_role_update(struct Camera *camera)` returns true only when a target role has completely handled the current camera update.
- `void sm64_saturn_camera_bypass_arm(uint32_t source_tick)` captures one valid post-replay camera pose; `bool sm64_saturn_camera_bypass_is_armed(void)` reports whether the held pose is valid.

- [ ] **Step 1: Write the role-dispatch and bypass tests.** Prove source-baseline dispatch returns false, bypass dispatch returns false before arming, and after arming it preserves the seeded `pos`, `focus`, yaw, mode, and Lakitu pose without invoking the source update body.
- [ ] **Step 2: Run the new host contract test and verify failure.**
- [ ] **Step 3: Implement the target-only dispatch seam.** Place the role check immediately after `gCamera = c` in `update_camera`. The bypass captures the first valid post-replay state, then copies that state back on later ticks and returns before the expensive source camera work. The source baseline path remains byte-for-byte behaviorally unchanged.
- [ ] **Step 4: Bind role 2 in sourceboot.** Add the bypass build identity to the Makefile, arm it at the Phase A replay boundary in `main.c`, and retain the existing route marker and SCC1 capture symbols.
- [ ] **Step 5: Run role tests and the existing 27-test camera evidence suite.** All must pass; no target build is claimed by this task.
- [ ] **Step 6: Commit.**

  ```powershell
  git add src/port/saturn/runtime/saturn_camera_role.h src/port/saturn/runtime/saturn_camera_role.c src/game/camera.c src/port/saturn/sourceboot/Makefile src/port/saturn/sourceboot/main.c tools/saturn/test_camera_role_contract.py
  git commit -m "feat: add held-camera attribution role"
  ```

### Task 3: Implement the minimal fixed follow candidate

**Files:**
- Create: `src/port/saturn/runtime/saturn_camera_fixed.h`
- Create: `src/port/saturn/runtime/saturn_camera_fixed.c`
- Modify: `src/port/saturn/runtime/saturn_camera_role.c`
- Modify: `src/game/camera.c`
- Modify: `src/port/saturn/sourceboot/Makefile`
- Test: `tools/saturn/test_camera_fixed.py`

**Interfaces:**
- `typedef struct sm64_saturn_camera_fixed_state { int32_t desired_focus[3], rendered_focus[3], rendered_position[3]; int16_t yaw, pitch; int32_t distance, vertical_offset; uint32_t diagnostics; } sm64_saturn_camera_fixed_state_t`.
- `void sm64_saturn_camera_fixed_init(sm64_saturn_camera_fixed_state_t *state, const struct Camera *source_camera)`.
- `void sm64_saturn_camera_fixed_step(sm64_saturn_camera_fixed_state_t *state, const struct MarioState *mario, int16_t input_yaw, int16_t input_pitch)`.
- `void sm64_saturn_camera_fixed_publish(const sm64_saturn_camera_fixed_state_t *state, struct Camera *camera)`.

- [ ] **Step 1: Write host differential tests for fixed state.** Cover zero movement, yaw wrap-around, bounded approach, distance/vertical saturation, and integer-only IEEE-754 publication bits. The tests must compare semantic outputs and bounded response, not private source-camera state.
- [ ] **Step 2: Run the focused fixed-camera tests and verify failure.**
- [ ] **Step 3: Implement the state and math façade.** Use `sm64_saturn_q16_mul_sh2`, existing 16-bit binary-angle tables, bounded add/subtract, and shift-based smoothing. Do not call `float`, `double`, `libm`, `fix16_div`, or generic 64-bit multiply/divide in the candidate closure. The Phase A candidate does not implement obstruction; it records an explicit unsupported-obstruction diagnostic for Task 4 to reject from production acceptance.
- [ ] **Step 4: Publish integer-only compatibility fields.** Convert Q values to IEEE-754 bit patterns with integer operations, then assign the resulting bits through a union or `memcpy`; no compiler float conversion is permitted in the candidate path.
- [ ] **Step 5: Run fixed-camera tests and inspect the host assembly for forbidden helper calls.** The focused suite must pass and the host test must exercise the same saturation and angle-wrap cases used by the target façade.
- [ ] **Step 6: Commit.**

  ```powershell
  git add src/port/saturn/runtime/saturn_camera_fixed.h src/port/saturn/runtime/saturn_camera_fixed.c src/port/saturn/runtime/saturn_camera_role.c src/game/camera.c src/port/saturn/sourceboot/Makefile tools/saturn/test_camera_fixed.py
  git commit -m "feat: add minimal fixed follow camera candidate"
  ```

### Task 4: Add camera-stage timing and Phase A report generation

**Files:**
- Modify: `src/game/level_update.c:955-1078`
- Modify: `src/port/saturn/sourceboot/source_route_probe.h`
- Modify: `src/port/saturn/sourceboot/main.c`
- Modify: `tools/saturn/capture_camera_idle.py`
- Modify: `tools/saturn/compare_camera_route_reports.py`
- Create: `tools/saturn/capture_camera_phase_a.py`
- Test: `tools/saturn/test_capture_camera_phase_a.py`

**Interfaces:**
- `typedef struct sm64_saturn_camera_timing { uint32_t ticks_last, ticks_accum, invocations; uint16_t ticks_max; }` is reset at route start and published beside the existing SBR4 timing fields.
- `capture_camera_phase_a.py` accepts exactly three role-tagged report paths and emits `phase_a_attribution.json` with `maximum_opportunity`, `realized_saving`, `bypass_whole_sim_percent`, `candidate_camera_stage_percent`, and artifact identities.

- [ ] **Step 1: Write report tests.** Assert exact role ordering, same route manifest digest, two same-role deterministic pairs, computed maximum opportunity, realized saving, and the three decision bands (<5%, 5–10%, >10%).
- [ ] **Step 2: Run the report tests and verify failure.**
- [ ] **Step 3: Instrument only the authoritative camera call sites.** Surround each `update_camera(gCurrentArea->camera)` call in `basic_update`, `play_mode_normal`, and `play_mode_change_area` with the FRT timer. Do not time render work inside the camera interval and do not alter source update ordering.
- [ ] **Step 4: Publish camera timing in the existing route probe.** Append fields only through a versioned SBR4 change or a named extension; preserve all existing offsets consumed by current reports.
- [ ] **Step 5: Implement the Phase A report adapter.** Decode raw SBR4/SCC1 bytes, bind ELF/ISO/source identities, compute:

  ```text
  maximum_opportunity = source_baseline.sim_frt_ticks_accum - bypass.sim_frt_ticks_accum
  realized_saving     = source_baseline.sim_frt_ticks_accum - fixed_candidate.sim_frt_ticks_accum
  bypass_percent      = 100 * maximum_opportunity / source_baseline.sim_frt_ticks_accum
  camera_stage_percent = 100 * fixed_candidate.camera_ticks_accum / source_baseline.camera_ticks_accum
  ```

- [ ] **Step 6: Run report tests and raw-first capture tests.**
- [ ] **Step 7: Commit.**

  ```powershell
  git add src/game/level_update.c src/port/saturn/sourceboot/source_route_probe.h src/port/saturn/sourceboot/main.c tools/saturn/capture_camera_idle.py tools/saturn/compare_camera_route_reports.py tools/saturn/capture_camera_phase_a.py tools/saturn/test_capture_camera_phase_a.py
  git commit -m "feat: report camera Phase A attribution"
  ```

### Task 5: Capture the three roles and record the go/no-go decision

**Prerequisite:** Do not begin these captures until the pinned `hot1/clip1`
indirect-transfer gate reports zero unexplained transfers and zero unresolved
effects on both routes. The indirect entries are statically derived from the
pinned GeoLayout/route data, not replay-observed targets. In particular, the
nine GraphNode-family sites are seven genuine callback dispatches plus two
statically knowable helper calls; together with `_guMtxF2L +142`, those helper
calls are three static-helper provenance repairs, not callbacks.

**Files:**
- Create: `docs/saturn/evidence/reports/camera-phase-a-attribution-2026-07-31.json`
- Create: `docs/saturn/evidence/reports/camera-phase-a-attribution-2026-07-31.md`
- Modify: `docs/saturn/HANDOFF_2026-07-29-native-math-sprint.md`
- Modify: `docs/superpowers/specs/2026-07-31-saturn-fixed-point-camera-core-design.md`

**Interfaces:**
- Inputs are exactly two source-baseline captures, two bypass captures, and two fixed-candidate captures, all from the same `bob-default-camera-v1` manifest and 2,000-tick replay.
- The JSON report records each ELF/ISO/source identity, raw SBR4/SCC1 hashes, camera-stage totals, whole-simulation totals, render/culling workload, route identity, and the decision band.

- [ ] **Step 1: Build each role with a unique output tag and run the same route.** Use the corrected MSYS environment and the sourceboot linker verifier; do not run source and candidate cameras together. The three builds use this common command prefix and vary only `SATURN_CAMERA_VARIANT`:

  ```powershell
  $buildBase = "cd /d/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge && source /d/Code/RetroDev/sm64-saturn-port/sm64-port/.yaul.env && cd src/port/saturn/sourceboot && make -j1 SATURN_DEMO_PATH=1 SATURN_SOURCEBOOT_ROUTE_REPLAY=1 SATURN_SOURCEBOOT_CAMERA_ROUTE=1 SATURN_ATAN2_VARIANT=2 SATURN_SOURCE_CART_STAGE_SECTORS=16 SATURN_DEMO_HOT_PROMOTION=1 SATURN_DEMO_NEAR_CLIP=1 SATURN_RENDERER_PIPELINE=2 HOST_CC=C:/msys64/mingw64/bin/gcc.exe"
  $baseline = "$buildBase SATURN_CAMERA_VARIANT=1 verify"
  $bypass = "$buildBase SATURN_CAMERA_VARIANT=2 verify"
  $candidate = "$buildBase SATURN_CAMERA_VARIANT=3 verify"
  ```

  Each command must emit a distinct role-tagged ELF/CUE and pass `verify_sourceboot_memory_map.py` before capture.
- [ ] **Step 2: Capture two independent runs per role.** A same-role pair must have byte-identical semantic/SCC1 evidence and identical route/source identities.
- [ ] **Step 3: Generate and verify the raw-first Phase A report.** Reject any report with missing raw windows, stale ELF markers, route digest mismatch, nonzero diagnostics, unsupported capability events, or changed replay input.
- [ ] **Step 4: Record the decision.** Mark Phase B eligible only when bypass opportunity exceeds 10% or the owner records an independent 5–10% justification, and the fixed candidate camera stage is at least 50% below source. If bypass is below 5%, record `stop-replacement` and do not begin production fixed-camera work.
- [ ] **Step 5: Commit the evidence and documentation decision.**

  ```powershell
  git add docs/saturn/evidence/reports/camera-phase-a-attribution-2026-07-31.json docs/saturn/evidence/reports/camera-phase-a-attribution-2026-07-31.md docs/saturn/HANDOFF_2026-07-29-native-math-sprint.md docs/superpowers/specs/2026-07-31-saturn-fixed-point-camera-core-design.md
  git commit -m "evidence: record camera Phase A decision"
  ```

## Verification Checklist

- [ ] `PYTHONPATH=tools/saturn` host contract suite passes for SCC1, capture, comparison, memory-map, role, fixed-camera, and Phase A report tests.
- [ ] Two same-role captures are byte-identical for every role.
- [ ] Baseline, bypass, and fixed candidate use the same route manifest, input replay, source data, and capture schema.
- [ ] Bypass never executes the normal camera update after arming.
- [ ] Fixed candidate helper audit reports zero soft-float/libm edges and no generic 64-bit division in its reachable closure.
- [ ] Candidate unsupported-obstruction diagnostics are explicit and prevent Phase B acceptance; they are not silently treated as a production pass.
- [ ] The final report contains both camera-stage and whole-simulation measurements and names the next decision: stop, justify/continue, or proceed to Phase B.

## Handoff

Plan complete and saved to `docs/superpowers/plans/2026-07-31-saturn-camera-phase-a-attribution.md`. Implementation must use fresh subagent-driven tasks with a spec-compliance and quality review after each task. Phase B production obstruction and full capability expansion are separate follow-on work and may not be folded into this attribution plan.
