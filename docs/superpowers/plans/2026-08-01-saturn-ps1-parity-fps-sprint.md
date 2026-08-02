# Saturn PS1-Parity FPS Sprint Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make the BOB source-loop build materially faster by removing repeated VDP1 setup, wrong-shaped graph math, oversized frame records, redundant rejection work, and full-frame comparison sorting from the accepted Saturn hot path.

**Architecture:** Preserve SM64 simulation and Saturn's existing IR, BSP, clipping, texture-residency, dual-SH2 ownership, and VDP1 backend. Optimize four bounded waves: compile-once command state, native Q16 graph matrices, compact/fused visible records, then early culling and bounded ordering. Each micro-task is host-tested; each wave gets exactly one serial target build and one BOB comparison.

**Tech Stack:** C11, SuperH-2/Yaul, VDP1/VDP2, Python 3 bake and audit tools, the existing host contract executable, sourceboot replay/capture tooling, Ymir for comparative evidence.

## Global Constraints

- Work in `sm64-port/.worktrees/sh2-native-math-purge`; preserve unrelated untracked evidence and scratch directories.
- BOB is the deterministic proving ground, but all runtime code must remain scene-neutral.
- Pin `23c3cdd` as the pre-sprint predecessor and preserve its ELF/CUE identity, route-state report, screenshot, and stage counters before Wave 1 changes the baseline.
- Allocate implementation effort approximately 70% renderer and 25% graph math. The separate audio plan receives the remaining 5% and cannot enter this build.
- Keep transform-once vertex sharing, near clipping, BSP crossing support, texture selection, topology, visibility, and source simulation semantics.
- A primitive, node, or ordering path must have an explicit fallback until its replacement passes its wave gate.
- Do not run target builds or Ymir concurrently. The owner CPU is busy; use host tests during implementation and one target comparison per wave.
- Never invoke `sh-elf-*` or `m68keb-elf-*` directly from PowerShell. Run the entire cross-build/inspection command inside `C:/msys64/usr/bin/bash.exe -lc`, prepend `/usr/bin` to `PATH`, and set `TMPDIR=/tmp/sm64-saturn-$MSYSTEM` before launching tools. This keeps `msys-2.0.dll`, `msys-gcc_s-seh-1.dll`, `libgmp-10.dll`, `libmpfr-6.dll`, and `libisl-23.dll` discoverable.
- PS1 reference: `malucard/sm64-psx@3073845688ea273da78d539b20c45110d8a868c3`, files `src/port/gfx/gfx_rsp_jit.c` and `src/port/psx/gfx_dl_exec_psx.c`, behavior/pattern-only because no repository-wide license is established. Copy no code.
- Native-math reference: in-tree SM64 is the behavioral oracle. Use pinned libyaul `6012f79f237773378c8014e70d8998ad95a38d98` (MIT), especially `libyaul/gamemath/fix16/fix16_vec3.c` and `libyaul/scu/bus/cpu/cpu/divu.h`, by dependency/API use. Preserve existing Jo Engine, Z-Treme, and SlaveDriver provenance entries.

---

## Task 1: Restore a Truthful Hot-Closure Gate

**Files:**

- Modify: `tools/saturn/verify_sh2_native_math.py`
- Modify: `tools/saturn/test_verify_sh2_native_math.py`
- Modify: `tools/saturn/sh2_native_math_sim_route_oracle_v1.txt`
- Modify: `tools/saturn/sh2_native_math_sim_audit_contract_v1.json`
- Create: `docs/saturn/evidence/reports/sh2-native-math-hot1-clip1-pre-sprint-2026-08-01.md`

- [ ] Add a failing regression test proving a declared dispatcher clears only unresolved transfers classified as dynamic; a missed stack-spill static helper remains a failure.

```python
def test_declared_dispatcher_does_not_hide_static_transfer(self):
    report = audit_fixture(
        unresolved=[transfer("_geo_process_node_and_siblings", 0x100, provenance="static")],
        declared_edges=[("_geo_process_node_and_siblings", "_geo_camera_main")],
    )
    self.assertEqual(report.unresolved_count, 1)
```

- [ ] Run `./.venv-saturn-tools/Scripts/python.exe tools/saturn/test_verify_sh2_native_math.py`; expect the new assertion to fail because filtering is currently caller-granular.
- [ ] Add explicit dynamic/static provenance to unresolved transfer records and filter only `provenance == "dynamic"` against dispatcher declarations.
- [ ] Derive the GraphNode callback set from the pinned BOB GeoLayout and the level-script callback set from the pinned sourceboot script. Declare all statically reachable callback targets, not a source-grep count and not a claimed dynamic observation.
- [ ] Give `_guMtxF2L`'s `jsr @r7` stack-spill site an explicit parser fix or a retained unresolved disposition. Do not fabricate a direct-call fact.
- [ ] Define a stale declaration as an edge whose dispatcher is reachable in the pinned ELF but whose target is absent from the statically derived route manifest; define unused in those exact static terms.
- [ ] Run the Python test again; expect all tests to pass.
- [ ] In one MSYS2 shell, build only the hot1/clip1 replay profile with `-j1`, run `verify-sim-math-route`, and record the ELF SHA, observed count, unresolved transfers, and declared callbacks in the report. Do not launch Ymir.

```bash
export PATH="/usr/bin:$PATH"
export TMPDIR="/tmp/sm64-saturn-$MSYSTEM"
cd /d/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge
source .yaul.env
make -C src/port/saturn/sourceboot -B -j1 SATURN_DEMO_PATH=1 SATURN_SOURCEBOOT_ROUTE_REPLAY=1 SATURN_SOURCEBOOT_CAMERA_ROUTE=1 SATURN_CAMERA_VARIANT=3 SATURN_SOURCE_CART_STAGE_SECTORS=8 SATURN_DEMO_HOT_PROMOTION=1 SATURN_DEMO_NEAR_CLIP=1 SATURN_DEMO_BSP_ORDER=1 SATURN_RENDERER_PIPELINE=2 verify-sim-math-route
```

- [ ] Commit only these files: `fix: restore precise hot-closure audit gate`.

## Task 2: Add the Equal-Shade Flat VDP1 Fast Path

**Files:**

- Create: `src/port/saturn/gfx/saturn_terrain_emit_policy.h`
- Modify: `src/port/saturn/gfx/saturn_demo_render.c`
- Modify: `src/port/saturn/runtime/saturn_source_runtime.h`
- Modify: `tools/saturn/runtime_contract_test.c`

- [ ] Add failing host cases for four equal colors, one unequal color, textured primitives, and null/zero-count inputs.

```c
typedef enum sm64_saturn_shade_path {
    SM64_SATURN_SHADE_FLAT_REPLACE,
    SM64_SATURN_SHADE_GOURAUD,
    SM64_SATURN_SHADE_TEXTURED
} sm64_saturn_shade_path_t;

sm64_saturn_shade_path_t sm64_saturn_terrain_shade_path(
    uint16_t flags, const uint16_t colors[4]);
```

- [ ] Run `make -f Makefile.saturn.mk OS=Windows_NT verify-runtime-contracts SATURN_TOOLS_PYTHON=$PWD/.venv-saturn-tools/Scripts/python.exe`; expect compile failure because the policy does not exist.
- [ ] Implement the pure policy helper. Equal untextured colors select RGB1555/REPLACE; true gradients select Gouraud; textures retain their current texture path.
- [ ] Change `demo_emit_terrain_result()` so the flat path writes the first color directly, skips `sm64_saturn_gouraud_bank_alloc()`, and increments new `flat_primitives` and `gouraud_primitives` counters.
- [ ] Keep the existing counted flat fallback for actual Gouraud allocation failure; distinguish it from intentional flat emission.
- [ ] Re-run `verify-runtime-contracts`; expect pass.
- [ ] Commit: `perf: skip gouraud work for flat terrain`.

## Task 3: Initialize Immutable Terrain Command Templates Once

**Files:**

- Create: `src/port/saturn/gfx/saturn_terrain_command_template.h`
- Create: `src/port/saturn/gfx/saturn_terrain_command_template.c`
- Modify: `src/port/saturn/gfx/saturn_demo_render.c`
- Modify: `src/port/saturn/sourceboot/Makefile`
- Modify: `Makefile.saturn.mk`
- Create: `tools/saturn/terrain_command_template_test.c`

- [ ] Add a host test that builds templates for flat, textured, and Gouraud primitives, then patches two different vertex sets and proves all immutable words remain byte-identical.

```c
typedef struct sm64_saturn_terrain_command_template {
    uint16_t flags;
    uint16_t color;
    uint16_t texture_slot;
    uint16_t shade_path;
} sm64_saturn_terrain_command_template_t;

bool sm64_saturn_terrain_template_build(
    sm64_saturn_terrain_command_template_t *out,
    const sm64_saturn_terrain_primitive_t *primitive);
```

- [ ] Add `verify-terrain-command-template` to `Makefile.saturn.mk` and run it; expect failure before the module exists.
- [ ] Build one portable metadata template per static terrain primitive during `sm64_saturn_demo_render_init()`. Material flags, flat color, texture slot, and shading path are immutable; VDP1 addresses that depend on runtime partitions remain resolved once after residency initialization.
- [ ] In `demo_emit_terrain_result()`, select the primitive template and patch only screen coordinates, link/end state, and the Gouraud address when the selected path truly needs one.
- [ ] If template initialization fails for a primitive, mark that entry invalid and use the old per-primitive emitter for that entry only.
- [ ] Run `verify-terrain-command-template` and `verify-runtime-contracts`; expect pass.
- [ ] Commit: `perf: prebuild terrain command state`.

## Task 4: Wave 1 BOB Comparison

**Files:**

- Create: `docs/saturn/evidence/reports/ps1-parity-wave1-bob-2026-08-01.md`
- Modify: `docs/saturn/ROADMAP.md`

- [ ] Build the predecessor commit and Wave 1 head serially with identical hot1/clip1 replay flags, using `-B -j1` in the guarded MSYS2 shell from Task 1.
- [ ] Capture one named BOB route checkpoint per image. Require fresh ELF/CUE/source identities, visible non-sky terrain and Mario, matching route tick/game state, and unchanged overflow/fault flags.
- [ ] Record command count, intentional flat count, true Gouraud count, Gouraud allocation failures, command build/upload ticks, VDP wait ticks, and total-frame ticks.
- [ ] Accept Wave 1 if pixels/game state match and Gouraud/template work decreases. A timing regression keeps the code only as rejected evidence and restores the predecessor as the next baseline.
- [ ] Update the roadmap with the accepted/rejected decision and commit: `docs: record PS1-parity wave 1 evidence`.

## Task 5: Remove `__divdi3` from Q16 Vector Normalization

**Files:**

- Modify: `src/port/saturn/gfx/saturn_matrix_ctors.h`
- Modify: `tools/saturn/mtxq_ctor_diff_test.c`
- Modify: `tools/saturn/render_native_math_test.c`

- [ ] Add differential vectors covering zero, axis-aligned, mixed sign, near-unit, and maximum accepted graph ranges. Add a mutation case that perturbs the reciprocal by one bit and must be detected.
- [ ] Run `make -f Makefile.saturn.mk verify-mtxq-ctors verify-render-native-math`; expect the new target-helper assertion or mutation to fail.
- [ ] Compute one reciprocal scale, then multiply all three components:

```c
int32_t reciprocal_q16;
if (!sm64_saturn_div_s64_s32(INT64_C(1) << 32,
                              (int32_t)mag, &reciprocal_q16)) {
    return;
}
v[0] = sm64_saturn_q16_mul(v[0], reciprocal_q16);
v[1] = sm64_saturn_q16_mul(v[1], reciprocal_q16);
v[2] = sm64_saturn_q16_mul(v[2], reciprocal_q16);
```

- [ ] Use `sm64_saturn_div_s64_s32()` so SH-2 uses libyaul DIVU and host tests use signed reference division. Preserve the existing zero-vector behavior and saturation contract.
- [ ] Run both host targets; expect pass.
- [ ] Inspect the linked hot profile inside MSYS2 and prove `sm64_saturn_q16_vec3_normalize` has no `___divdi3` edge.
- [ ] Commit: `perf: normalize Q16 vectors with one DIVU`.

## Task 6: Route Graph Matrix Nodes Directly into the Q16 Stack

**Files:**

- Modify: `src/game/rendering_graph_node.c`
- Modify: `src/port/saturn/gfx/saturn_matrix.h`
- Modify: `src/port/saturn/gfx/saturn_matrix_ctors.h`
- Modify: `tools/saturn/mtxq_ctor_diff_test.c`
- Modify: `tools/saturn/runtime_contract_test.c`

- [ ] Add table-driven differential cases for look-at, ZXY rotation/translation, perspective, and orthographic constructors using BOB-range values and singular/limit cases.
- [ ] Add compile-time assertions that the target branch updates `gMatStackQ` and that the PC/source branch remains unchanged.
- [ ] Run `verify-mtxq-ctors` and `verify-runtime-contracts`; expect the new graph seam contract to fail.
- [ ] Introduce one target-only graph helper per node type:

```c
#if defined(TARGET_SATURN) && SATURN_SH2_NATIVE_MATH_HOT
static void geo_push_camera_q16(const struct GraphNodeCamera *node,
                                const struct Camera *camera);
static void geo_push_perspective_q16(const struct GraphNodePerspective *node);
#endif
```

- [ ] Lower source float inputs once at the graph boundary with the existing IEEE decoder, construct directly into `gMatStackQ`, and export to float only where an unconverted consumer explicitly requires it. Do not call `guLookAtReflectF`, `guRotateF`, `guPerspectiveF`, `guOrthoF`, or `guNormalize` from accepted target branches.
- [ ] Convert graph node types one at a time and keep the existing source branch behind the compile-time role switch until the whole wave passes.
- [ ] Run the host targets and the static native-math verifier; expect pass and no legacy constructor edges in the accepted hot closure.
- [ ] Commit: `perf: build graph matrices directly in Q16`.

## Task 7: Convert the Held-Object Transform Bridge

**Files:**

- Modify: `src/game/rendering_graph_node.c`
- Modify: `src/port/saturn/gfx/saturn_matrix.h`
- Modify: `tools/saturn/runtime_contract_test.c`

- [ ] Add host fixtures for identity, translated, rotated, negative-coordinate, and near-limit held-object matrices. Compare fixed results with the current float oracle within the established Q16 tolerance.
- [ ] Run `verify-runtime-contracts`; expect failure because the fixed bridge does not exist.
- [ ] Add a bounded helper that derives held-object position from the authoritative Q16 stack:

```c
static void get_pos_from_transform_mtx_q16(
    int32_t out_q16[3], const sm64_saturn_mtx_t *object,
    const sm64_saturn_mtx_t *camera);
```

- [ ] Use the fixed result throughout the Saturn held-object branch; perform at most one explicit float export for an unconverted source callback. Preserve source/PC code byte-for-byte outside target guards.
- [ ] Run `verify-runtime-contracts`, `verify-mtxq-ctors`, and the hot-closure verifier; expect pass and removal of the bridge's helper edges.
- [ ] Commit: `perf: keep held-object transforms in Q16`.

## Task 8: Wave 2 BOB Comparison

**Files:**

- Create: `docs/saturn/evidence/reports/ps1-parity-wave2-bob-2026-08-01.md`
- Modify: `docs/saturn/ROADMAP.md`

- [ ] Repeat the single serial predecessor/head BOB comparison from Task 4.
- [ ] Add graph-matrix ticks, Q16 normalization calls, float-boundary exports, and the linked helper closure to the evidence.
- [ ] Require matching gameplay state and visible geometry. Accept only if `guLookAtReflectF`, `guRotateF`, `guPerspectiveF`, `guOrthoF`, `guNormalize`, and normalization `__divdi3` are absent from the accepted hot closure.
- [ ] Commit: `docs: record PS1-parity wave 2 evidence`.

## Task 9: Replace Large Terrain Results with Compact Visible Descriptors

**Files:**

- Modify: `src/port/saturn/gpl/slavedriver_terrain_result.h`
- Modify: `src/port/saturn/gfx/saturn_demo_render.c`
- Modify: `src/port/saturn/runtime/saturn_source_runtime.h`
- Modify: `tools/saturn/runtime_contract_test.c`

- [ ] Add layout assertions and round-trip fixtures proving a descriptor identifies the primitive, transformed-corner span, painter key, clip class, and BSP leaf without copying material, texture, and four Gouraud colors.

```c
typedef struct sm64_saturn_visible_terrain {
    uint16_t primitive_id;
    uint16_t corner_base;
    uint16_t painter_key;
    uint8_t corner_count;
    uint8_t clip_class;
    uint16_t bsp_leaf;
} sm64_saturn_visible_terrain_t;
```

- [ ] Run `verify-runtime-contracts`; expect size/assertion failure against the old result record.
- [ ] Make classification emit the compact descriptor and reference the transform-owner cache and immutable command template bank. Do not copy material, texture slot, flags, or four colors through the per-frame result bank.
- [ ] Make emission consume the descriptor directly. Retain explicit master/slave result spans, sequence publication, cache purge boundaries, and bounded capacities.
- [ ] Add counters for descriptor bytes written/read and legacy fallback count.
- [ ] Run `verify-runtime-contracts` and `verify-dual-transform`; expect pass.
- [ ] Commit: `perf: compact visible terrain records`.

## Task 10: Fuse Classification and Command Patching Where Ownership Allows

**Files:**

- Modify: `src/port/saturn/gfx/saturn_demo_render.c`
- Modify: `src/port/saturn/runtime/saturn_source_runtime.h`
- Modify: `tools/saturn/runtime_contract_test.c`

- [ ] Add a deterministic fixture that runs split and fused paths on the same synthetic primitive set and compares visibility, primitive IDs, coordinates, clip class, painter keys, and emitted command order.
- [ ] Run `verify-runtime-contracts`; expect failure before the fused comparison path exists.
- [ ] Patch each visible primitive's private command record immediately after its final coordinates and clip classification are known. Publish only compact references to the merge stage.
- [ ] Do not share the command arena across CPUs. Each worker owns a bounded span; the master concatenates spans only after sequence/fence validation.
- [ ] Remove repeated material lookup, texture binding, corner copying, and cross-product calculation from `demo_emit_terrain_result()` for valid templates; retain per-primitive fallback.
- [ ] Run `verify-runtime-contracts`; expect split/fused equivalence and all capacity/fence mutations to pass.
- [ ] Commit: `perf: patch terrain commands during classification`.

## Task 11: Wave 3 BOB Comparison

**Files:**

- Create: `docs/saturn/evidence/reports/ps1-parity-wave3-bob-2026-08-01.md`
- Modify: `docs/saturn/ROADMAP.md`

- [ ] Run the one serial BOB comparison and record descriptor bytes, transform/classify/compact/merge/emit ticks, DMA/upload ticks, total-frame ticks, and all overflow/fallback counters.
- [ ] Accept only with matching state/pixels and no sequence, cache, command, or capacity fault. Record whether the performance change is obvious in manual feel without presenting feel as a timing measurement.
- [ ] Commit: `docs: record PS1-parity wave 3 evidence`.

## Task 12: Bake Cull Policy and Remove the Duplicate Area Test

**Files:**

- Modify: `tools/saturn/emit_bob_scene.py`
- Modify: `src/port/saturn/gfx/saturn_demo_render.c`
- Modify: `tools/saturn/test_tools.py`
- Modify: `tools/saturn/runtime_contract_test.c`

- [ ] Add generator fixtures for front-only, back-only, both, and neither Fast3D cull policy, including reversed winding.
- [ ] Add a runtime fixture proving projected signed area is computed once and that emission never re-evaluates it.
- [ ] Run `verify-tools verify-runtime-contracts`; expect new metadata/contract failures.
- [ ] Emit compact cull metadata per primitive and perform the single signed-area reject during classification before shading/template work.
- [ ] Carry only the accepted winding/degeneracy result in the visible descriptor. Delete the redundant cross product from emission.
- [ ] Run both host targets; expect pass.
- [ ] Commit: `perf: reject terrain once before material work`.

## Task 13: Select and Implement Bounded Stable Ordering

**Files:**

- Create: `src/port/saturn/gfx/saturn_terrain_order.h`
- Create: `src/port/saturn/gfx/saturn_terrain_order.c`
- Create: `tools/saturn/terrain_order_test.c`
- Modify: `src/port/saturn/gfx/saturn_demo_render.c`
- Modify: `Makefile.saturn.mk`

- [ ] Add captured and synthetic ordering corpora with equal-depth stability, extreme keys, empty/full capacity, BSP-crossing markers, and adversarial reverse order.
- [ ] Implement host-only candidates behind one interface: stable fixed depth bins and stable two-pass 8-bit radix. Compare both with the current stable merge output.

```c
typedef enum sm64_saturn_order_mode {
    SM64_SATURN_ORDER_MERGE,
    SM64_SATURN_ORDER_BINS,
    SM64_SATURN_ORDER_RADIX
} sm64_saturn_order_mode_t;

size_t sm64_saturn_terrain_order(
    sm64_saturn_visible_terrain_t *items, size_t count,
    sm64_saturn_visible_terrain_t *scratch,
    sm64_saturn_order_mode_t mode);
```

- [ ] Add `verify-terrain-order` and run it; expect failure before the module exists.
- [ ] Select the candidate that is stable, bounded, and exactly matches the comparator corpus with the smallest fixed memory/work envelope. Record the decision in the test header.
- [ ] Integrate the selected mode under a build switch while retaining merge sort as a comparison build. Route BSP-crossing terrain through the existing BSP path.
- [ ] Run `verify-terrain-order verify-runtime-contracts`; expect pass.
- [ ] Commit: `perf: replace terrain merge sort with bounded ordering`.

## Task 14: Wave 4 and Sprint Acceptance

**Files:**

- Create: `docs/saturn/evidence/reports/ps1-parity-wave4-bob-2026-08-01.md`
- Create: `docs/saturn/evidence/reports/ps1-parity-fps-sprint-summary-2026-08-01.md`
- Modify: `docs/saturn/ROADMAP.md`
- Modify: `docs/saturn/HANDOFF_2026-07-29-native-math-sprint.md`

- [ ] Run `verify-tools`, `verify-runtime-contracts`, focused template/order/math tests, and the native hot-closure gate.
- [ ] Build predecessor and Wave 4 head serially, then perform the single BOB comparison with the same identity/state/pixel/fault gates.
- [ ] Record rejected work as rejected evidence; do not promote it. For accepted work, report stage deltas and the remaining top hot categories rather than claiming all source floats are gone.
- [ ] Require the accepted hot closure to contain no unintended soft-float, libm, or generic 64-bit division edges.
- [ ] Manually launch the normal-control build only after automated state validation. Confirm controls remain owned by the player; do not use the replay build for this check.
- [ ] Mark each wave landed or explicitly rejected, link every report, and state that Ymir timings are comparative while retail Saturn remains authoritative.
- [ ] Commit: `docs: close PS1-parity FPS sprint`.

## Final Review Checklist

- [ ] Every task has a focused red/green test and a bounded commit.
- [ ] Target tools were run only inside the guarded MSYS2 shell; no missing-DLL popup was triggered by bare Windows launches.
- [ ] Only one target build/capture pair was performed per wave.
- [ ] Source/PC behavior remains unchanged outside target guards.
- [ ] No PS1 source was copied; the pinned behavior-only reference is recorded.
- [ ] The audio prototype was not linked into any FPS comparison image.
- [ ] BOB is visibly and measurably faster, or every rejected wave has evidence explaining why it was not promoted.
